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
#include "hw/pci/pci_ids.h"

#define PCI_VENDOR_ID_ZORAN 0x11DE
#define PCI_DEVICE_ID_ZORAN_36057 0x6057

#define TYPE_PCIBASE_DEVICE "zr36067_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define ZR36057_MMIO_BAR 0

/* Register offsets */
#define ZR36057_I2CBR           0x044
#define ZR36057_SPGPPCR         0x028
#define ZR36057_POR             0x200
#define ZR36057_GPPGCR1         0x02c
#define ZR36057_ISR             0x03c
#define ZR36057_JMC             0x100
#define ZR36057_JPC             0x104
#define ZR36057_VFESPFR         0x008
#define ZR36057_VDCR            0x018
#define ZR36057_ICR             0x040
#define ZR36057_VSSFGR          0x014
#define ZR36057_MCTCR           0x034

/* Bit definitions */
#define ZR36057_SPGPPCR_SOFT_RESET   BIT(24)
#define ZR36057_ISR_GIRQ1            BIT(30)
#define ZR36057_ISR_GIRQ0            BIT(29)
#define ZR36057_POR_PO_DIR           BIT(23)
#define ZR36057_POR_PO_TIME          BIT(24)
#define ZR36057_POR_PO_PEN           BIT(25)
#define ZR36057_ISR_JPEG_REP_IRQ     BIT(27)
#define ZR36057_ICR_INT_PIN_EN       BIT(24)
#define ZR36057_ICR_JPEG_REP_IRQ     BIT(27)
#define ZR36057_JMC_MJPG_EXP_MODE    (2 << 29)
#define ZR36057_JMC_SYNC_MSTR        BIT(4)
#define ZR36057_JMC_MJPG_CMP_MODE    (3 << 29)
#define ZR36057_VSSFGR_SNAP_SHOT     BIT(1)
#define ZR36057_JPC_P_RESET          BIT(7)
#define ZR36057_JPC_COD_TRNS_EN      BIT(5)
#define ZR36057_MCTCR_C_FLUSH        BIT(28)
#define ZR36057_VFESPFR_LITTLE_ENDIAN BIT(0)
#define ZR36057_VFESPFR_EXT_FL       BIT(26)
#define ZR36057_VFESPFR_TOP_FIELD    BIT(25)
#define ZR36057_VFESPFR_VCLK_POL     BIT(24)

/* Codec flags (not register bits) */
#define CODEC_FLAG_DECODER   0x00008000L
#define CODEC_FLAG_VFE       0x00002000L
#define CODEC_FLAG_ENCODER   0x00004000L
#define CODEC_FLAG_JPEG      0x00000001L

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    bool has_msi;
    bool has_msix;
    /* MMIO register space: 0x400 bytes, 32-bit aligned */
    uint32_t mmio_regs[0x400 / sizeof(uint32_t)];
    uint8_t irq_level;
};

/* Forward declarations */
static void pcibase_update_irq(PCIBaseState *s);

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;
    uint32_t reg_offset = addr & ~0x3;
    uint32_t index = reg_offset / sizeof(uint32_t);

    if (addr + size > sizeof(s->mmio_regs)) {
        return 0xffffffff;
    }

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported access size %u at 0x" HWADDR_FMT_plx "\n",
                      __func__, size, addr);
        return 0xffffffff;
    }

    val = s->mmio_regs[index];

    /* Special handling for reads that may have side effects */
    switch (reg_offset) {
    case ZR36057_ISR:
        /* ISR read: just return value, no clear */
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_offset = addr & ~0x3;
    uint32_t index = reg_offset / sizeof(uint32_t);
    uint32_t data = (uint32_t)val;

    if (addr + size > sizeof(s->mmio_regs)) {
        return;
    }

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported access size %u at 0x" HWADDR_FMT_plx "\n",
                      __func__, size, addr);
        return;
    }

    /* Handle W1C (Write-1-to-Clear) and special registers */
    switch (reg_offset) {
    case ZR36057_SPGPPCR:
        /* Write to SPGPPCR: if soft reset bit set or value 0, reset device */
        s->mmio_regs[index] = data;
        if (data & ZR36057_SPGPPCR_SOFT_RESET) {
            /* Trigger soft reset */
            device_cold_reset(DEVICE(s));
        }
        break;
    case ZR36057_ISR:
        /* ISR is W1C: writing 1 clears the bit */
        s->mmio_regs[index] &= ~data;
        pcibase_update_irq(s);
        break;
    case ZR36057_ICR:
        s->mmio_regs[index] = data;
        pcibase_update_irq(s);
        break;
    default:
        s->mmio_regs[index] = data;
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Removed config_write override to rely on default PCI handling,
   which correctly manages BAR and capability accesses. */

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t isr = s->mmio_regs[ZR36057_ISR / sizeof(uint32_t)];
    uint32_t icr = s->mmio_regs[ZR36057_ICR / sizeof(uint32_t)];
    int level = 0;

    /* IRQ is raised if any interrupt source enabled in ICR is active in ISR
     * and global INT_PIN_EN is set. */
    if (icr & ZR36057_ICR_INT_PIN_EN) {
        uint32_t pending = isr & icr;
        /* Mask to only those bits that are interrupt sources */
        if (pending & (ZR36057_ISR_JPEG_REP_IRQ | ZR36057_ISR_GIRQ0 | ZR36057_ISR_GIRQ1)) {
            level = 1;
        }
    }
    if (s->irq_level != level) {
        s->irq_level = level;
        pci_set_irq(pdev, level);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    s->irq_level = 0;
    /* Default values for some registers after reset */
    s->mmio_regs[ZR36057_VFESPFR / sizeof(uint32_t)] = 0;
    s->mmio_regs[ZR36057_JMC / sizeof(uint32_t)] = 0;
    s->mmio_regs[ZR36057_JPC / sizeof(uint32_t)] = 0;
    s->mmio_regs[ZR36057_I2CBR / sizeof(uint32_t)] = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ZORAN);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_ZORAN_36057);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_VIDEO);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCI Express capability: mark device as PCI Express capable
     * and initialize endpoint capability. This is required to
     * operate behind a PCIe root port. */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    /* Add some Power Management capability as a typical feature */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Set revision to 3 to emulate a ZR36067, enabling auto-detection.
     * Use pci_set_byte to ensure the write takes effect. */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x03);

    /* Set subsystem IDs to zero (generic) */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x0000);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID,        0x0000);

    /* Setup BARs */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x400,
        .name = "zr36057-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "zr36067_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(irq_level, PCIBaseState),
        VMSTATE_UINT32_ARRAY(mmio_regs, PCIBaseState, 0x400 / sizeof(uint32_t)),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    /* No config_write override; default PCI handler is used to
     * correctly manage BAR writes and capability registers. */
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

type_init(pcibase_register_types)
