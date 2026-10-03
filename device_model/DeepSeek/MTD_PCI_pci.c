#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bswap.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "MTD_PCI_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x530d
#define CLASS_ID 0x0580
#define BAR0_SIZE (16 * MiB)
#define FLASH_SIZE (8 * MiB)

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;    /* BAR index 0-5 */
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Flash emulation */
    uint8_t *flash_data; /* 8MB flash memory */
    bool cfi_query_mode;
};

/* CFI query data for minimal probe success */
static const uint8_t cfi_query_data[64] = {
    [0] = 'Q',
    [1] = 'R',
    [2] = 'Y',
    [3] = 0x02,   /* Primary OEM command set: AMD/Fujitsu standard */
    [4] = 0x40,   /* Address for primary extended table (0x0040) */
    [5] = 0x00,
    [6] = 0x00,   /* Alternate OEM command set */
    [7] = 0x00,   /* Address for alternate extended */
    [8] = 0x00,
    [13] = 0x07,   /* Typical timeout per byte write */
    [14] = 0x07,   /* Typical timeout buffer write */
    [15] = 0x0A,   /* Typical timeout block erase */
    [16] = 0x0A,   /* Typical timeout chip erase */
};

static inline bool pcibase_is_flash(PCIBaseState *s, hwaddr addr, uint32_t *flash_offset)
{
    if (addr >= 0x00400000 && addr < 0x00800000) {
        *flash_offset = (addr - 0x00400000) + 0x400000;
        return true;
    }
    if (addr >= 0x00800000 && addr < 0x01000000) {
        *flash_offset = addr - 0x00800000;
        return true;
    }
    return false;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t flash_off;
    uint64_t val = 0;

    if (pcibase_is_flash(s, addr, &flash_off)) {
        if (flash_off >= FLASH_SIZE) {
            return ~0ULL; /* Out of bounds */
        }
        if (s->cfi_query_mode) {
            /* Return CFI query data for offsets 0x10-0x40 */
            if (flash_off >= 0x10 && flash_off < 0x10 + sizeof(cfi_query_data)) {
                uint8_t b = cfi_query_data[flash_off - 0x10];
                if (size == 1) {
                    val = b;
                } else if (size == 2) {
                    val = b | (cfi_query_data[flash_off - 0x10 + 1] << 8);
                } else if (size == 4) {
                    val = ldl_le_p(&cfi_query_data[flash_off - 0x10]);
                } else if (size == 8) {
                    val = ldq_le_p(&cfi_query_data[flash_off - 0x10]);
                } else {
                    val = ~0ULL;
                }
            } else {
                val = ~0ULL;
            }
        } else {
            /* Normal flash read */
            if (size == 1) {
                val = s->flash_data[flash_off];
            } else if (size == 2) {
                val = lduw_le_p(&s->flash_data[flash_off]);
            } else if (size == 4) {
                val = ldl_le_p(&s->flash_data[flash_off]);
            } else if (size == 8) {
                val = ldq_le_p(&s->flash_data[flash_off]);
            } else {
                val = ~0ULL;
            }
        }
    } else {
        /* Non-flash region (control registers and unmapped): return 0 */
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t flash_off;

    if (pcibase_is_flash(s, addr, &flash_off)) {
        if (flash_off >= FLASH_SIZE) {
            return; /* Out of bounds, ignore */
        }
        /* CFI command handling: entry to query mode */
        if (flash_off == 0x55 && size == 1 && val == 0x98) {
            s->cfi_query_mode = true;
            return;
        }
        /* Any other write exits query mode */
        s->cfi_query_mode = false;
        /* Write to flash array */
        if (size == 1) {
            s->flash_data[flash_off] = val;
        } else if (size == 2) {
            stw_le_p(&s->flash_data[flash_off], val);
        } else if (size == 4) {
            stl_le_p(&s->flash_data[flash_off], val);
        } else if (size == 8) {
            stq_le_p(&s->flash_data[flash_off], val);
        }
        /* Other sizes ignored */
    } else {
        /* Non-flash region: ignore writes (control registers) */
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Reset flash state */
    s->cfi_query_mode = false;
    /* Optional: clear flash_data? Typically not needed */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* PIO not used by driver; define dummy ops or remove? Removing to avoid undeclared symbol */
        /* memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size); */
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize custom config register at offset 0x44 (win_base) */
    pci_set_long(pci_conf + 0x44, 0x00001000); /* default base address */

    /* Allocate flash memory */
    s->flash_data = g_malloc0(FLASH_SIZE);
    s->cfi_query_mode = false;

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    g_free(s->flash_data);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "MTD_PCI_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
