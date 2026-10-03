/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

#define TYPE_PCIBASE_DEVICE "intel_qep_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x4bc3
#define CLASS_ID PCI_CLASS_OTHERS

#define INTEL_QEPCON           0x00
#define INTEL_QEPFLT           0x04
#define INTEL_QEPCOUNT         0x08
#define INTEL_QEPMAX           0x0c
#define INTEL_QEPWDT           0x10
#define INTEL_QEPCAPDIV        0x14
#define INTEL_QEPCNTR          0x18
#define INTEL_QEPCAPBUF        0x1c
#define INTEL_QEPINT_STAT      0x20
#define INTEL_QEPINT_MASK      0x24

#define INTEL_QEPCON_EN            BIT(0)
#define INTEL_QEPCON_FLT_EN        BIT(1)
#define INTEL_QEPCON_EDGE_A        BIT(2)
#define INTEL_QEPCON_EDGE_B        BIT(3)
#define INTEL_QEPCON_EDGE_INDX     BIT(4)
#define INTEL_QEPCON_SWPAB         BIT(5)
#define INTEL_QEPCON_OP_MODE       BIT(6)
#define INTEL_QEPCON_PH_ERR        BIT(7)
#define INTEL_QEPCON_COUNT_RST_MODE BIT(8)
#define INTEL_QEPCON_INDX_GATING_MASK  GENMASK(10, 9)
#define INTEL_QEPCON_INDX_GATING(n)    (((n) & 3) << 9)
#define INTEL_QEPCON_INDX_PAL_PBL      INTEL_QEPCON_INDX_GATING(0)
#define INTEL_QEPCON_INDX_PAL_PBH      INTEL_QEPCON_INDX_GATING(1)
#define INTEL_QEPCON_INDX_PAH_PBL      INTEL_QEPCON_INDX_GATING(2)
#define INTEL_QEPCON_INDX_PAH_PBH      INTEL_QEPCON_INDX_GATING(3)
#define INTEL_QEPCON_CAP_MODE      BIT(11)
#define INTEL_QEPCON_FIFO_THRE_MASK    GENMASK(14, 12)
#define INTEL_QEPCON_FIFO_THRE(n)      ((((n) - 1) & 7) << 12)
#define INTEL_QEPCON_FIFO_EMPTY        BIT(15)

#define INTEL_QEPFLT_MAX_COUNT(n)  ((n) & 0x1fffff)

#define INTEL_QEPINT_FIFOCRIT      BIT(5)
#define INTEL_QEPINT_FIFOENTRY     BIT(4)
#define INTEL_QEPINT_QEPDIR        BIT(3)
#define INTEL_QEPINT_QEPRST_UP     BIT(2)
#define INTEL_QEPINT_QEPRST_DOWN   BIT(1)
#define INTEL_QEPINT_WDT           BIT(0)
#define INTEL_QEPINT_MASK_ALL      GENMASK(5, 0)

#define INTEL_QEP_CLK_PERIOD_NS    10

#define BAR0_SIZE 0x1000

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

struct IntelQEPRegs {
    uint32_t qepcon;
    uint32_t qepflt;
    uint32_t qepcount;
    uint32_t qepmax;
    uint32_t qepwdt;
    uint32_t qepcapdiv;
    uint32_t qepcntr;
    uint32_t qepcapbuf;
    uint32_t qepint_stat;
    uint32_t qepint_mask;
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct IntelQEPRegs regs;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (addr >= BAR0_SIZE) {
        return val;
    }
    if (size != 4) {
        return val;
    }
    switch (addr) {
    case INTEL_QEPCON:
        val = s->regs.qepcon;
        break;
    case INTEL_QEPFLT:
        val = s->regs.qepflt;
        break;
    case INTEL_QEPCOUNT:
        val = s->regs.qepcount;
        break;
    case INTEL_QEPMAX:
        val = s->regs.qepmax;
        break;
    case INTEL_QEPWDT:
        val = s->regs.qepwdt;
        break;
    case INTEL_QEPCAPDIV:
        val = s->regs.qepcapdiv;
        break;
    case INTEL_QEPCNTR:
        val = s->regs.qepcntr;
        break;
    case INTEL_QEPCAPBUF:
        val = s->regs.qepcapbuf;
        break;
    case INTEL_QEPINT_STAT:
        val = s->regs.qepint_stat;
        break;
    case INTEL_QEPINT_MASK:
        val = s->regs.qepint_mask;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= BAR0_SIZE) {
        return;
    }
    if (size != 4) {
        return;
    }
    switch (addr) {
    case INTEL_QEPCON:
        s->regs.qepcon = (uint32_t)val;
        break;
    case INTEL_QEPFLT:
        s->regs.qepflt = (uint32_t)(val & INTEL_QEPFLT_MAX_COUNT(val));
        break;
    case INTEL_QEPMAX:
        s->regs.qepmax = (uint32_t)val;
        break;
    case INTEL_QEPWDT:
        s->regs.qepwdt = (uint32_t)val;
        break;
    case INTEL_QEPCAPDIV:
        s->regs.qepcapdiv = (uint32_t)val;
        break;
    case INTEL_QEPCNTR:
        s->regs.qepcntr = (uint32_t)val;
        break;
    case INTEL_QEPCAPBUF:
        s->regs.qepcapbuf = (uint32_t)val;
        break;
    case INTEL_QEPINT_STAT:
        s->regs.qepint_stat &= ~(uint32_t)val;
        break;
    case INTEL_QEPINT_MASK:
        s->regs.qepint_mask = (uint32_t)val;
        break;
    case INTEL_QEPCOUNT:
        /* read-only, ignore writes */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
    memset(&s->regs, 0, sizeof(s->regs));
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

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "qep-mmio";
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
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "intel_qep_pci",
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