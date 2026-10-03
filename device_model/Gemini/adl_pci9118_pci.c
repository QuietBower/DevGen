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

#define TYPE_PCIBASE_DEVICE "adl_pci9118_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMCC		0x10e8
#define PCI9118_TIMER_BASE		0x00
#define PCI9118_AI_FIFO_REG		0x10
#define PCI9118_AO_REG(x)		(0x10 + ((x) * 4))
#define PCI9118_AI_STATUS_REG		0x18
#define PCI9118_AI_STATUS_NFULL		BIT(8)
#define PCI9118_AI_STATUS_NHFULL	BIT(7)
#define PCI9118_AI_STATUS_NEPTY		BIT(6)
#define PCI9118_AI_STATUS_ACMP		BIT(5)
#define PCI9118_AI_STATUS_DTH		BIT(4)
#define PCI9118_AI_STATUS_BOVER		BIT(3)
#define PCI9118_AI_STATUS_ADOS		BIT(2)
#define PCI9118_AI_STATUS_ADOR		BIT(1)
#define PCI9118_AI_STATUS_ADRDY		BIT(0)
#define PCI9118_AI_CTRL_REG		0x18
#define PCI9118_AI_CTRL_UNIP		BIT(7)
#define PCI9118_AI_CTRL_DIFF		BIT(6)
#define PCI9118_AI_CTRL_SOFTG		BIT(5)
#define PCI9118_AI_CTRL_EXTG		BIT(4)
#define PCI9118_AI_CTRL_EXTM		BIT(3)
#define PCI9118_AI_CTRL_TMRTR		BIT(2)
#define PCI9118_AI_CTRL_INT		BIT(1)
#define PCI9118_AI_CTRL_DMA		BIT(0)
#define PCI9118_DIO_REG			0x1c
#define PCI9118_SOFTTRG_REG		0x20
#define PCI9118_AI_CHANLIST_REG		0x24
#define PCI9118_AI_CHANLIST_RANGE(x)	(((x) & 0x3) << 8)
#define PCI9118_AI_CHANLIST_CHAN(x)	((x) << 0)
#define PCI9118_AI_BURST_NUM_REG	0x28
#define PCI9118_AI_AUTOSCAN_MODE_REG	0x2c
#define PCI9118_AI_CFG_REG		0x30
#define PCI9118_AI_CFG_PDTRG		BIT(7)
#define PCI9118_AI_CFG_PETRG		BIT(6)
#define PCI9118_AI_CFG_BSSH		BIT(5)
#define PCI9118_AI_CFG_BM		BIT(4)
#define PCI9118_AI_CFG_BS		BIT(3)
#define PCI9118_AI_CFG_PM		BIT(2)
#define PCI9118_AI_CFG_AM		BIT(1)
#define PCI9118_AI_CFG_START		BIT(0)
#define PCI9118_FIFO_RESET_REG		0x34
#define PCI9118_INT_CTRL_REG		0x38
#define PCI9118_INT_CTRL_TIMER		BIT(3)
#define PCI9118_INT_CTRL_ABOUT		BIT(2)
#define PCI9118_INT_CTRL_HFULL		BIT(1)
#define PCI9118_INT_CTRL_DTRG		BIT(0)

#define START_AI_EXT	0x01
#define STOP_AI_EXT	0x02
#define STOP_AI_INT	0x08

#define AMCC_OP_REG_MWAR         0x24
#define AMCC_OP_REG_MWTC         0x28
#define A2P_HI_PRIORITY		0x00000100L
#define AMCC_OP_REG_MCSR         0x3c
#define EN_A2P_TRANSFERS	0x00000400
#define RESET_A2P_FLAGS		0x04000000L
#define AMCC_OP_REG_INTCSR       0x38
#define TARGET_ABORT_INT	0x00200000L
#define ANY_S593X_INT		0x00800000L
#define MASTER_ABORT_INT	0x00100000L
#define AINT_WRITE_COMPL	0x00004000

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
    uint32_t int_ctrl;
    uint32_t amcc_intcsr;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t ai_ctrl;
    uint32_t ai_cfg;
    uint32_t ai_status;
    uint32_t ai_chanlist;
    uint32_t ai_burst_num;
    uint32_t ai_autoscan_mode;
    uint32_t amcc_mcsr;

    /* DMA Context */
    uint32_t amcc_mwar;
    uint32_t amcc_mwtc;
    QEMUTimer *dma_timer;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = (s->amcc_intcsr & ANY_S593X_INT) != 0;
    pci_set_irq(pdev, irq_active ? 1 : 0);
}

static void pcibase_dma_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    if (s->amcc_mcsr & EN_A2P_TRANSFERS) {
        if (s->amcc_mwtc > 0) {
            uint8_t *buf = g_malloc0(s->amcc_mwtc);
            pci_dma_write(PCI_DEVICE(s), s->amcc_mwar, buf, s->amcc_mwtc);
            g_free(buf);
            
            s->amcc_mcsr &= ~EN_A2P_TRANSFERS;
            s->amcc_intcsr |= ANY_S593X_INT;
            pcibase_update_irq(s);
        }
    }
}

static uint64_t amcc_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case AMCC_OP_REG_MWAR: return s->amcc_mwar;
    case AMCC_OP_REG_MWTC: return s->amcc_mwtc;
    case AMCC_OP_REG_MCSR: return s->amcc_mcsr;
    case AMCC_OP_REG_INTCSR: return s->amcc_intcsr;
    default: return 0;
    }
}

static void amcc_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case AMCC_OP_REG_MWAR:
        s->amcc_mwar = val;
        break;
    case AMCC_OP_REG_MWTC:
        s->amcc_mwtc = val;
        if ((s->amcc_mcsr & EN_A2P_TRANSFERS) && s->amcc_mwtc > 0) {
            timer_mod(s->dma_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
        }
        break;
    case AMCC_OP_REG_MCSR:
        s->amcc_mcsr = val;
        if ((s->amcc_mcsr & EN_A2P_TRANSFERS) && s->amcc_mwtc > 0) {
            timer_mod(s->dma_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
        } else if (!(s->amcc_mcsr & EN_A2P_TRANSFERS)) {
            timer_del(s->dma_timer);
        }
        break;
    case AMCC_OP_REG_INTCSR:
        if (val & 0x00ff0000) {
            s->amcc_intcsr &= ~ANY_S593X_INT;
            pcibase_update_irq(s);
        }
        s->amcc_intcsr = (s->amcc_intcsr & 0x00ff0000) | (val & ~0x00ff0000);
        break;
    }
}

static uint64_t pci9118_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case PCI9118_AI_FIFO_REG:
        return 0x8000;
    case PCI9118_AI_STATUS_REG:
        return PCI9118_AI_STATUS_NFULL | PCI9118_AI_STATUS_ADRDY | PCI9118_AI_STATUS_NEPTY;
    case PCI9118_DIO_REG:
        return 0x0f;
    case PCI9118_INT_CTRL_REG:
        return s->int_ctrl;
    default:
        return 0;
    }
}

static void pci9118_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case PCI9118_AI_CTRL_REG:
        s->ai_ctrl = val;
        break;
    case PCI9118_SOFTTRG_REG:
        break;
    case PCI9118_AI_CHANLIST_REG:
        s->ai_chanlist = val;
        break;
    case PCI9118_AI_BURST_NUM_REG:
        s->ai_burst_num = val;
        break;
    case PCI9118_AI_AUTOSCAN_MODE_REG:
        s->ai_autoscan_mode = val;
        break;
    case PCI9118_AI_CFG_REG:
        s->ai_cfg = val;
        break;
    case PCI9118_FIFO_RESET_REG:
        break;
    case PCI9118_INT_CTRL_REG:
        s->int_ctrl = val;
        break;
    case PCI9118_AO_REG(0):
    case PCI9118_AO_REG(1):
        break;
    case PCI9118_DIO_REG:
        break;
    }
}

static const MemoryRegionOps amcc_pio_ops = {
    .read = amcc_pio_read,
    .write = amcc_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pci9118_pio_ops = {
    .read = pci9118_pio_read,
    .write = pci9118_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->ai_ctrl = 0;
    s->ai_cfg = 0;
    s->ai_status = PCI9118_AI_STATUS_NFULL | PCI9118_AI_STATUS_ADRDY | PCI9118_AI_STATUS_NEPTY;
    s->ai_chanlist = 0;
    s->ai_burst_num = 0;
    s->ai_autoscan_mode = 0;
    s->amcc_mcsr = 0;
    s->amcc_mwar = 0;
    s->amcc_mwtc = 0;
    s->int_ctrl = 0;
    s->amcc_intcsr = 0;
    if (s->dma_timer) {
        timer_del(s->dma_timer);
    }
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        const MemoryRegionOps *ops = (bi->index == 0) ? &amcc_pio_ops : &pci9118_pio_ops;
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMCC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x80d9 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->dma_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, pcibase_dma_timer_cb, s);

    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 0x40, "amcc-pio"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_NONE, 0, ""};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_PIO, 0x40, "pci9118-pio"};

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (s->dma_timer) {
        timer_free(s->dma_timer);
    }
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "adl_pci9118_pci",
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
