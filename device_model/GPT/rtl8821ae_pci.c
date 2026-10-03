/*
 * QEMU PCI device model for rtl8821ae_pci (behavioral stub sufficient for driver probe)
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

/* Additional include files retrieved from driver context */
#include <linux/pci_regs.h>

#define TYPE_PCIBASE_DEVICE "rtl8821ae_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x10ec
#define PCIBASE_DEVICE_ID 0x8812
#define PCIBASE_CLASS_ID  PCI_CLASS_NETWORK_OTHER

/* BAR index used by the driver (rtl_hal_cfg.bar_id) */
#define PCIBASE_BAR_INDEX 2

/*
 * We only have sw.c, which does not define concrete MMIO register
 * layout for rtlpriv->io.read{8,16,32} backends. To remain within the
 * no-hallucination constraint, we implement a generic byte-addressable
 * MMIO space that simply stores and returns values written by the
 * driver (shadow RAM). This is sufficient for basic probe paths that
 * only perform read-back verification without relying on hardcoded
 * reset values.
 */

#define PCIBASE_MMIO_SIZE 0x1000

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

    /* Generic MMIO shadow storage */
    uint8_t mmio_buf[PCIBASE_MMIO_SIZE];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* sw.c only programs irq_mask fields; the actual ISR/IMR registers
     * live in other rtl8821ae files which we do not have. Without
     * explicit register semantics, we cannot raise specific interrupts
     * in response to MMIO. For probe success, it is enough that the
     * device advertises an interrupt pin/MSI and does not crash.
     */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* sw.c does not expose device DMA descriptor registers; keep empty. */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= PCIBASE_MMIO_SIZE) {
        /* Out-of-range reads return 0 */
        return 0;
    }

    /* Simple little-endian shadow RAM behavior */
    switch (size) {
    case 1:
        val = s->mmio_buf[addr];
        break;
    case 2:
        if (addr + 1 < PCIBASE_MMIO_SIZE) {
            val = s->mmio_buf[addr] |
                  ((uint16_t)s->mmio_buf[addr + 1] << 8);
        }
        break;
    case 4:
        if (addr + 3 < PCIBASE_MMIO_SIZE) {
            val = (uint32_t)s->mmio_buf[addr] |
                  ((uint32_t)s->mmio_buf[addr + 1] << 8) |
                  ((uint32_t)s->mmio_buf[addr + 2] << 16) |
                  ((uint32_t)s->mmio_buf[addr + 3] << 24);
        }
        break;
    case 8:
        if (addr + 7 < PCIBASE_MMIO_SIZE) {
            val = (uint64_t)s->mmio_buf[addr] |
                  ((uint64_t)s->mmio_buf[addr + 1] << 8) |
                  ((uint64_t)s->mmio_buf[addr + 2] << 16) |
                  ((uint64_t)s->mmio_buf[addr + 3] << 24) |
                  ((uint64_t)s->mmio_buf[addr + 4] << 32) |
                  ((uint64_t)s->mmio_buf[addr + 5] << 40) |
                  ((uint64_t)s->mmio_buf[addr + 6] << 48) |
                  ((uint64_t)s->mmio_buf[addr + 7] << 56);
        }
        break;
    default:
        /* Unsupported size; return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= PCIBASE_MMIO_SIZE) {
        /* Ignore out-of-range writes */
        return;
    }

    /* Simple little-endian shadow RAM behavior */
    switch (size) {
    case 1:
        s->mmio_buf[addr] = (uint8_t)val;
        break;
    case 2:
        if (addr + 1 < PCIBASE_MMIO_SIZE) {
            s->mmio_buf[addr] = (uint8_t)(val & 0xff);
            s->mmio_buf[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        }
        break;
    case 4:
        if (addr + 3 < PCIBASE_MMIO_SIZE) {
            s->mmio_buf[addr] = (uint8_t)(val & 0xff);
            s->mmio_buf[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->mmio_buf[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->mmio_buf[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        }
        break;
    case 8:
        if (addr + 7 < PCIBASE_MMIO_SIZE) {
            s->mmio_buf[addr] = (uint8_t)(val & 0xff);
            s->mmio_buf[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->mmio_buf[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->mmio_buf[addr + 3] = (uint8_t)((val >> 24) & 0xff);
            s->mmio_buf[addr + 4] = (uint8_t)((val >> 32) & 0xff);
            s->mmio_buf[addr + 5] = (uint8_t)((val >> 40) & 0xff);
            s->mmio_buf[addr + 6] = (uint8_t)((val >> 48) & 0xff);
            s->mmio_buf[addr + 7] = (uint8_t)((val >> 56) & 0xff);
        }
        break;
    default:
        /* Ignore unsupported sizes */
        break;
    }

    /* In a full model, writes to specific addresses would update
     * interrupt state, DMA engines, etc. We cannot implement those
     * without additional register documentation or driver sources.
     */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No legacy PIO usage is described in sw.c; keep as zero. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* No legacy PIO usage is described in sw.c; ignore writes. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Clear MMIO shadow on reset */
    memset(s->mmio_buf, 0, sizeof(s->mmio_buf));
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    s->bar_info[0].index = PCIBASE_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* Size is not explicitly specified in sw.c; keep 0x1000 as in Phase 1 */
    s->bar_info[0].size = PCIBASE_MMIO_SIZE;
    s->bar_info[0].name = "rtl8821ae-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MMIO shadow */
    memset(s->mmio_buf, 0, sizeof(s->mmio_buf));

    /* MSI support is enabled by default via module params; advertise MSI capability */
    s->has_msi = true;
    s->has_msix = false;
    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "rtl8821ae_pci",
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
