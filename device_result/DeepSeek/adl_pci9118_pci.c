/*
 * QEMU emulation of ADLINK PCI-9118 PCI device (AMCC S5933 + analog I/O)
 * Generated from Linux driver adl_pci9118.c
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

#define PCI_VENDOR_ID_AMCC  0x10e8
#define PCI_CLASS_OTHERS    0xff

#define TYPE_PCIBASE_DEVICE "adl_pci9118_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register definitions extracted from driver */
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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    struct PCI9118Regs {
        uint32_t timer_base;
        uint32_t ai_fifo_reg;
        uint32_t ai_status;
        uint32_t ai_ctrl;
        uint32_t dio;
        uint32_t softtrg;
        uint32_t chanlist;
        uint32_t burst_num;
        uint32_t autoscan_mode;
        uint32_t ai_cfg;
        uint32_t fifo_reset;
        uint32_t int_ctrl;
    } regs;

    struct AMCCDMA {
        uint32_t mwar;
        uint32_t mwtc;
        uint32_t mcsr;
        uint32_t intcsr;
        bool dma_active;
    } amcc;

    bool ai_running;
    bool reset_active;
};

/* BAR0: AMCC S5933 PCI interface registers */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case AMCC_OP_REG_MWAR:
        val = s->amcc.mwar;
        break;
    case AMCC_OP_REG_MWTC:
        val = s->amcc.mwtc;
        break;
    case AMCC_OP_REG_MCSR:
        val = s->amcc.mcsr;
        break;
    case AMCC_OP_REG_INTCSR:
        val = s->amcc.intcsr;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case AMCC_OP_REG_MWAR:
        s->amcc.mwar = val;
        break;
    case AMCC_OP_REG_MWTC:
        s->amcc.mwtc = val;
        break;
    case AMCC_OP_REG_MCSR:
        s->amcc.mcsr = val;
        break;
    case AMCC_OP_REG_INTCSR:
        s->amcc.intcsr = val;
        break;
    }
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* BAR2: PCI-9118 analog I/O device registers */
static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case 0x10: /* AI_FIFO_REG read */
        val = s->regs.ai_fifo_reg;
        break;
    case 0x18: /* AI_STATUS_REG read */
        val = s->regs.ai_status;
        break;
    case 0x1c: /* DIO_REG */
        val = s->regs.dio;
        break;
    case 0x30: /* AI_CFG_REG */
        val = s->regs.ai_cfg;
        break;
    case 0x38: /* INT_CTRL_REG */
        val = s->regs.int_ctrl;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x10: /* AO_REG(0) - analog output channel 0 */
        /* accept write */
        break;
    case 0x14: /* AO_REG(1) */
        break;
    case 0x18: /* AI_CTRL_REG */
        s->regs.ai_ctrl = val;
        break;
    case 0x1c: /* DIO_REG */
        s->regs.dio = val;
        break;
    case 0x20: /* SOFTTRG_REG - trigger A/D conversion */
        s->regs.ai_status |= PCI9118_AI_STATUS_ADRDY;
        break;
    case 0x24: /* AI_CHANLIST_REG */
        break;
    case 0x28: /* AI_BURST_NUM_REG */
        s->regs.burst_num = val;
        break;
    case 0x2c: /* AI_AUTOSCAN_MODE_REG */
        s->regs.autoscan_mode = val;
        break;
    case 0x30: /* AI_CFG_REG */
        s->regs.ai_cfg = val;
        break;
    case 0x34: /* FIFO_RESET_REG */
        s->regs.ai_fifo_reg = 0;
        break;
    case 0x38: /* INT_CTRL_REG */
        s->regs.int_ctrl = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->regs, 0, sizeof(s->regs));
    memset(&s->amcc, 0, sizeof(s->amcc));
    s->ai_running = false;
    s->reset_active = false;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMCC);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x80d9);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR0: AMCC S5933 registers, I/O space, 256 bytes */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_bar0_ops,
                          s, "adl_pci9118-bar0", 256);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO,
                     &s->bar_regions[0]);

    /* BAR2: PCI-9118 device registers, I/O space, 256 bytes */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_bar2_ops,
                          s, "adl_pci9118-bar2", 256);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO,
                     &s->bar_regions[2]);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSI-X to clean up */
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
