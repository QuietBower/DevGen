/*
 * QEMU PCI device model for addi_apci_1500
 * Phase 2: Behavioral implementation for Linux comedi driver
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

#define TYPE_PCIBASE_DEVICE "addi_apci_1500_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI identifiers: use first entry of apci1500_pci_table */
#define PCI_VENDOR_ID_AMCC              0x10e8
#define PCI_CLASS_OTHERS                0xff
#define APCI1500_PCI_VENDOR_ID   PCI_VENDOR_ID_AMCC
#define APCI1500_PCI_DEVICE_ID   0x80fc

#define APCI1500_PCI_CLASS_ID    PCI_CLASS_OTHERS

/* Hardware register offsets as used by the driver (IO ports within BARs) */
#define APCI1500_Z8536_PORTC_REG       0x00
#define APCI1500_Z8536_PORTB_REG       0x01
#define APCI1500_Z8536_PORTA_REG       0x02
#define APCI1500_Z8536_CTRL_REG        0x03
#define APCI1500_CLK_SEL_REG           0x00
#define APCI1500_DI_REG                0x00
#define APCI1500_DO_REG                0x02
#define Z8536_PB_MODE_REG              0x28
#define Z8536_PA_DD_REG                0x23
#define Z8536_CT_CMDSTAT_REG(x)        (0x0a + (x))
#define Z8536_PA_CMDSTAT_REG           0x08
#define Z8536_CFG_CTRL_REG             0x01
#define Z8536_PC_DPP_REG               0x05
#define Z8536_INT_CTRL_REG             0x00
#define Z8536_PA_MODE_REG              0x20
#define Z8536_PB_CMDSTAT_REG           0x09
#define Z8536_PB_DD_REG                0x2b
#define Z8536_PC_DD_REG                0x06
#define Z8536_PB_DPP_REG               0x2a
#define Z8536_PA_PP_REG                0x25
#define Z8536_PA_PM_REG                0x27
#define Z8536_PA_PT_REG                0x26
#define Z8536_PB_PP_REG                0x2d
#define Z8536_PB_PM_REG                0x2c
#define Z8536_PB_PT_REG                0x2e
#define Z8536_CT_RELOAD_LSB_REG(x)     (0x17 + ((x) * 2))
#define Z8536_CT_RELOAD_MSB_REG(x)     (0x16 + ((x) * 2))
#define Z8536_CT_MODE_REG(x)           (0x1c + (x))
#define Z8536_CT_VAL_MSB_REG(x)        (0x10 + ((x) * 2))
#define Z8536_CT_VAL_LSB_REG(x)        (0x11 + ((x) * 2))
#define AMCC_OP_REG_INTCSR             0x38
#define AMCC_OP_REG_IMB1               0x10

/* Bit macros from driver */
#define BIT(nr)                 (1UL << (nr))
#define Z8536_PAB_MODE_PMS(x)          (((x) & 0x3) << 1)
#define Z8536_CMD(x)                   (((x) & 0x7) << 5)
#define Z8536_PAB_MODE_PTS(x)          (((x) & 0x3) << 6)

#define Z8536_PAB_MODE_PMS_DISABLE     Z8536_PAB_MODE_PMS(0)
#define Z8536_PAB_MODE_SB              BIT(4)
#define Z8536_CMD_CLR_IP_IUS           Z8536_CMD(1)
#define Z8536_PAB_MODE_PTS_BIT         Z8536_PAB_MODE_PTS(0)
#define Z8536_CMD_CLR_IE               Z8536_CMD(7)
#define Z8536_CFG_CTRL_PBE             BIT(7)
#define Z8536_CFG_CTRL_PAE             BIT(2)
#define Z8536_CFG_CTRL_PCE_CT3E        BIT(4)
#define Z8536_CFG_CTRL_CT2E            BIT(5)
#define Z8536_CFG_CTRL_CT1E            BIT(6)
#define Z8536_STAT_IE                  BIT(6)
#define Z8536_STAT_IP                  BIT(5)
#define Z8536_PAB_MODE_PMS_AND         Z8536_PAB_MODE_PMS(1)
#define Z8536_INT_CTRL_MIE             BIT(7)
#define Z8536_INT_CTRL_DLC             BIT(6)
#define Z8536_PAB_MODE_PMS_MASK        Z8536_PAB_MODE_PMS(3)
#define Z8536_PAB_MODE_PMS_OR          Z8536_PAB_MODE_PMS(2)
#define Z8536_CMD_SET_IE               Z8536_CMD(6)
#define Z8536_PAB_MODE_IMO             BIT(3)
#define Z8536_CT_MODE_DCS(x)           (((x) & 0x3) << 0)
#define Z8536_CT_MODE_DCS_ONESHOT      Z8536_CT_MODE_DCS(1)
#define Z8536_CT_MODE_CSC              BIT(7)
#define Z8536_CT_MODE_EGE              BIT(3)
#define Z8536_CT_MODE_ETE              BIT(4)
#define Z8536_CT_STAT_CIP              BIT(0)
#define Z8536_CT_MODE_DCS_SQRWAVE      Z8536_CT_MODE_DCS(2)
#define Z8536_CT_MODE_DCS_PULSE        Z8536_CT_MODE_DCS(0)
#define Z8536_CT_MODE_EOE              BIT(6)
#define Z8536_CT_CMD_TCB               BIT(1)
#define Z8536_CT_CMD_RCC               BIT(3)

#define INTCSR_INTR_ASSERTED           0x800000
#define INTCSR_INBOX_INTR_STATUS       0x20000
#define INTCSR_INBOX_FULL_INT          0x1000

/* Structural representation of driver's private data (subset) */
struct apci1500_private_shadow {
    uint32_t amcc_base;
    uint32_t addon_base;
    unsigned int clk_src;
    unsigned int pm[2];
    unsigned int pt[2];
    unsigned int pp[2];
};

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

/* Z8536 internal state shadow */
#define Z8536_REG_SPACE 0x40

typedef struct Z8536State {
    uint8_t regs[Z8536_REG_SPACE];
    uint8_t last_cmd;      /* last command written via CTRL */
    bool ies_pa;
    bool ies_pb;
    bool ies_ct[3];
    bool ip_pa;
    bool ip_pb;
    bool ip_ct[3];
} Z8536State;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Register shadows */
    Z8536State z8536;
    uint16_t di_reg;     /* addon + APCI1500_DI_REG */
    uint16_t do_reg;     /* addon + APCI1500_DO_REG */
    uint16_t clk_sel;    /* addon + APCI1500_CLK_SEL_REG */

    uint32_t intcsr;
    uint32_t imb1;

    /* simple interrupt state */
    bool irq_asserted;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool want = false;

    /* Interrupt is asserted if INTCSR_INTR_ASSERTED is set */
    if (s->intcsr & INTCSR_INTR_ASSERTED) {
        want = true;
    }

    if (want && !s->irq_asserted) {
        s->irq_asserted = true;
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else if (!want && s->irq_asserted) {
        s->irq_asserted = false;
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* No DMA is used by the driver, so omit DMA engine logic */

static uint8_t z8536_read_reg(PCIBaseState *s, uint8_t reg)
{
    if (reg >= Z8536_REG_SPACE) {
        return 0xff;
    }
    return s->z8536.regs[reg];
}

static void z8536_write_reg(PCIBaseState *s, uint8_t reg, uint8_t val)
{
    Z8536State *z = &s->z8536;

    if (reg >= Z8536_REG_SPACE) {
        return;
    }

    /* Save to shadow array */
    z->regs[reg] = val;

    /* Limited behaviour for command/status registers: handle clear IE/IP */
    if (reg == Z8536_PA_CMDSTAT_REG) {
        if (val & Z8536_CMD_CLR_IE) {
            z->ies_pa = false;
        }
        if (val & Z8536_CMD_CLR_IP_IUS) {
            z->ip_pa = false;
        }
        if (val & Z8536_CMD_SET_IE) {
            z->ies_pa = true;
        }
    } else if (reg == Z8536_PB_CMDSTAT_REG) {
        if (val & Z8536_CMD_CLR_IE) {
            z->ies_pb = false;
        }
        if (val & Z8536_CMD_CLR_IP_IUS) {
            z->ip_pb = false;
        }
        if (val & Z8536_CMD_SET_IE) {
            z->ies_pb = true;
        }
    } else if (reg == Z8536_CT_CMDSTAT_REG(0) ||
               reg == Z8536_CT_CMDSTAT_REG(1) ||
               reg == Z8536_CT_CMDSTAT_REG(2)) {
        int chan = reg - Z8536_CT_CMDSTAT_REG(0);
        if (chan >= 0 && chan < 3) {
            if (val & Z8536_CMD_CLR_IE) {
                z->ies_ct[chan] = false;
            }
            if (val & Z8536_CMD_CLR_IP_IUS) {
                z->ip_ct[chan] = false;
            }
            if (val & Z8536_CMD_SET_IE) {
                z->ies_ct[chan] = true;
            }
        }
    } else if (reg == Z8536_INT_CTRL_REG) {
        /* Master interrupt enable is just stored in regs[] */
    }
}

static uint8_t z8536_read_port(PCIBaseState *s, uint8_t offset)
{
    switch (offset) {
    case APCI1500_Z8536_PORTA_REG:
        return s->di_reg & 0xff; /* inputs 0-7 */
    case APCI1500_Z8536_PORTB_REG:
        return (s->di_reg >> 8) & 0xff; /* inputs 8-15 */
    case APCI1500_Z8536_PORTC_REG:
        /* Port C not explicitly used; return 0 */
        return 0x00;
    default:
        return 0xff;
    }
}

static void z8536_write_port(PCIBaseState *s, uint8_t offset, uint8_t val)
{
    /* Driver never writes data ports directly, so ignore */
    (void)s;
    (void)offset;
    (void)val;
}

/* BAR mapping plan:
 * BAR0: AMCC space (INTCSR, IMB1) - we model as MMIO 0x100
 * BAR1: Z8536 space (iobase)     - PIO  0x100 (used with inb/outb)
 * BAR2: addon space (DI/DO/clock) - PIO  0x10
 */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* AMCC region: only a few registers are used */
    switch (addr) {
    case AMCC_OP_REG_INTCSR:
        val = s->intcsr;
        break;
    case AMCC_OP_REG_IMB1:
        val = s->imb1;
        break;
    default:
        val = 0;
        break;
    }

    switch (size) {
    case 1:
        return (uint8_t)val;
    case 2:
        return (uint16_t)val;
    default:
        return (uint32_t)val;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    switch (addr) {
    case AMCC_OP_REG_INTCSR:
        /* Driver writes masks; maintain simple status bits */
        s->intcsr = v32;
        /* Keep the asserted bit if software sets it; we don't auto-set here */
        pcibase_update_irq(s);
        break;
    case AMCC_OP_REG_IMB1:
        s->imb1 = v32;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    /* Determine which BAR this I/O port belongs to by range.
     * BAR1: 0x000-0x0ff (Z8536 space)
     * BAR2: 0x100-0x10f (addon space) -- but QEMU presents BAR-relative
     * addresses per region, so we actually rely on which MemoryRegion
     * is hooked. pcibase_pio_ops is used for both BAR1 and BAR2; we
     * distinguish by size configured in bar_info.
     * To avoid guessing, we rely on index via s->bar_info sizes:
     *   BAR1 size 0x100: addresses [0,0xff] -> Z8536
     *   BAR2 size 0x10 : addresses [0,0x0f] -> addon
     * QEMU provides addr relative to each BAR separately, so this
     * function will be used with separate PCIBaseState instances per BAR.
     * We therefore must interpret by current region size; but we don't
     * have that here. So we assume:
     *   - For BAR1 accesses, pcibase_pio_ops is installed with size 0x100
     *   - For BAR2 accesses, size 0x10.
     * QEMU doesn't pass size of region here, but the address range is
     * within that size. We can't tell which BAR from addr alone, but
     * we know driver patterns: Z8536 uses offsets 0-3 and >=0x01,0x02,
     * while addon uses DI_REG(0), DO_REG(2), CLK_SEL(0) with 16-bit
     * I/O (inw/outw). We'll treat all <=0x3 as Z8536 control/ports,
     * and 0/2 with 16-bit size as addon. This minimal behaviour
     * matches driver usage without inventing new registers.
     */

    if (size == 1) {
        /* 8-bit accesses -> Z8536 or port B diagnostic read */
        if (addr == APCI1500_Z8536_CTRL_REG) {
            /* Control port read: command/status register pointer */
            val = z8536_read_reg(s, s->z8536.last_cmd);
        } else if (addr == APCI1500_Z8536_PORTA_REG ||
                   addr == APCI1500_Z8536_PORTB_REG ||
                   addr == APCI1500_Z8536_PORTC_REG) {
            val = z8536_read_port(s, addr);
        } else {
            val = 0xff;
        }
    } else if (size == 2) {
        /* 16-bit accesses go to addon area: DI/DO/CLK */
        if (addr == APCI1500_DI_REG) {
            /* DI_REG: read digital inputs */
            val = s->di_reg;
        } else if (addr == APCI1500_DO_REG) {
            /* No 16-bit read of DO in driver; return shadow */
            val = s->do_reg;
        } else if (addr == APCI1500_CLK_SEL_REG) {
            val = s->clk_sel;
        } else {
            val = 0xffff;
        }
    } else {
        val = 0xffffffff;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        uint8_t v8 = (uint8_t)val;
        /* 8-bit I/O used for Z8536 control */
        if (addr == APCI1500_Z8536_CTRL_REG) {
            Z8536State *z = &s->z8536;
            /* The driver always performs outb(reg); outb(val). We
             * emulate by treating the first write as selecting a
             * register (when value < Z8536_REG_SPACE), and the second
             * as data. A simple heuristic: if v8 < 0x40, just set
             * last_cmd; otherwise treat as data for current reg.
             */
            if (v8 < Z8536_REG_SPACE) {
                z->last_cmd = v8;
            } else {
                z8536_write_reg(s, z->last_cmd, v8);
            }
        } else if (addr == APCI1500_Z8536_PORTA_REG ||
                   addr == APCI1500_Z8536_PORTB_REG ||
                   addr == APCI1500_Z8536_PORTC_REG) {
            z8536_write_port(s, addr, v8);
        } else {
            /* ignore */
        }
    } else if (size == 2) {
        uint16_t v16 = (uint16_t)val;
        /* 16-bit I/O used for addon: DI_REG (read-only), DO_REG, CLK_SEL */
        if (addr == APCI1500_DO_REG) {
            s->do_reg = v16;
        } else if (addr == APCI1500_CLK_SEL_REG) {
            s->clk_sel = v16;
        } else if (addr == APCI1500_DI_REG) {
            /* driver never writes DI; ignore */
        } else {
            /* ignore */
        }
    } else {
        /* ignore other sizes */
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->z8536, 0, sizeof(s->z8536));
    s->z8536.last_cmd = 0;
    s->di_reg = 0x0000;
    s->do_reg = 0x0000;
    s->clk_sel = 0x0000;
    s->intcsr = 0x0;
    s->imb1 = 0x0;
    s->irq_asserted = false;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  APCI1500_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  APCI1500_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, APCI1500_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Configure BARs to reflect driver usage:
     * BAR0: AMCC MMIO (INTCSR, IMB1) - 0x100 bytes
     * BAR1: Z8536 IO (iobase)        - 0x100 bytes I/O
     * BAR2: addon IO (DI/DO/CLK)     - 0x10 bytes I/O
     */
    s->num_bars = 3;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x100;
    s->bar_info[0].name  = "apci1500-amcc";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_PIO;
    s->bar_info[1].size  = 0x100;
    s->bar_info[1].name  = "apci1500-z8536";

    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_PIO;
    s->bar_info[2].size  = 0x10;
    s->bar_info[2].name  = "apci1500-addon";

    for (int i = 3; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = "unused";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    pcibase_reset(DEVICE(pdev));
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
    .name = "addi_apci_1500_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16(di_reg, PCIBaseState),
        VMSTATE_UINT16(do_reg, PCIBaseState),
        VMSTATE_UINT16(clk_sel, PCIBaseState),
        VMSTATE_UINT32(intcsr, PCIBaseState),
        VMSTATE_UINT32(imb1, PCIBaseState),
        VMSTATE_BOOL(irq_asserted, PCIBaseState),
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

type_init(pcibase_register_types)

