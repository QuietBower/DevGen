/*
 * QEMU PCI Device Model for AMD64 AGP Bridge (agpgart-amd64)
 * Emulates hardware required for Linux driver amd64-agp.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
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

#define TYPE_PCIBASE_DEVICE "agpgart_amd64_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1022
#define DEVICE_ID 0x7454
#define CLASS_ID PCI_CLASS_BRIDGE_HOST
#define AGP_APERTURE_BAR 0

#define AGPSTAT   0x04
#define AGPCMD    0x08
#define AGPCTRL   0x10
#define AGPNISTAT 0x0c
#define AGPNICMD  0x20
#define AMD64_GARTAPERTURECTL 0x90

#define AGPSTAT_AGP_ENABLE  (1<<8)
#define AGPSTAT_MODE_3_0    (1<<3)
#define AGPSTAT_FW          (1<<4)
#define AGPSTAT_RQ_DEPTH    (0xff000000)
#define AGPSTAT_SBA         (1<<9)
#define AGPSTAT_ARQSZ       (1<<15|1<<14|1<<13)
#define AGPSTAT_CAL_MASK    (1<<12|1<<11|1<<10)
#define AGPSTAT2_1X         (1<<0)
#define AGPSTAT2_2X         (1<<1)
#define AGPSTAT2_4X         (1<<2)
#define AGPSTAT3_4X         (1)
#define AGPSTAT3_8X         (1<<1)
#define AGPSTAT3_RSVD       (1<<2)

#define AGP3_RESERVED_MASK  0x00ff00c4
#define AGP2_RESERVED_MASK  0x00fffcc8

#define GART_PAGE_SHIFT 12
#define GART_PAGE_SIZE  (1 << GART_PAGE_SHIFT)

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

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t agpstat;
    uint32_t agpcmd;
    uint32_t agpnistat;
    uint32_t agpctrl;
    uint32_t agpnicmd;
    uint32_t gart_aperture_ctl;

    /* GATT table (PTE storage for MMIO aperture) */
    uint32_t *gatt_table;
    uint32_t num_entries;

    /* Power management state (D0-D3) */
    uint8_t power_state;
};

/* MMIO Handlers for Aperture BAR */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr & (size - 1)) {
        qemu_log_mask(LOG_GUEST_ERROR, 
                      "%s: unaligned read at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return val;
    }

    if (addr < s->bar_info[AGP_APERTURE_BAR].size) {
        uint32_t index = addr >> 2;  /* each GART entry is 4 bytes */
        if (index < s->num_entries) {
            val = s->gatt_table[index];
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr & (size - 1)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unaligned write at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    if (addr < s->bar_info[AGP_APERTURE_BAR].size) {
        uint32_t index = addr >> 2;
        if (index < s->num_entries) {
            if (size == 4) {
                s->gatt_table[index] = (uint32_t) val;
            } else {
                /* Handle non-4-byte writes for safety */
                uint32_t shift = (addr & 3) * 8;
                uint32_t mask = (1ULL << (size * 8)) - 1;
                s->gatt_table[index] &= ~(mask << shift);
                s->gatt_table[index] |= (val & mask) << shift;
            }
        }
    }
}

/* PIO Handlers - not used by this driver, stubs return 0 */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset shadow registers */
    s->agpstat = 0;
    s->agpcmd = 0;
    s->agpnistat = 0;
    s->agpctrl = 0;
    s->agpnicmd = 0;
    s->gart_aperture_ctl = 0;
    s->power_state = 0;

    /* Clear GATT table */
    if (s->gatt_table) {
        memset(s->gatt_table, 0, s->num_entries * sizeof(uint32_t));
    }
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
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
    int cap_offset;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x13); /* >= B2 to avoid AGP version correction */
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Add AGP capability */
    cap_offset = pci_add_capability(pdev, PCI_CAP_ID_AGP, 0, 12, errp);
    if (cap_offset < 0) {
        return;
    }
    /* Set AGP capability initial values */
    pci_conf[cap_offset + 2] = 0x30;          /* AGP major/minor 3.0 */
    pci_conf[cap_offset + 3] = 0;
    pci_set_long(pci_conf + cap_offset + 4, 0x1f00021b); /* AGP status (4x/8x, FW, SBA, etc.) */
    pci_set_long(pci_conf + cap_offset + 8, 0);         /* AGP command initially 0 */

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x80000000, /* 2048 MB aperture */
        .name = "agp-aperture"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Allocate GATT table shadow for MMIO */
    s->num_entries = s->bar_info[AGP_APERTURE_BAR].size >> GART_PAGE_SHIFT;
    s->gatt_table = g_malloc0(s->num_entries * sizeof(uint32_t));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    g_free(s->gatt_table);
    s->gatt_table = NULL;

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "agpgart_amd64_pci",
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