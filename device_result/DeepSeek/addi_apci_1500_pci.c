/*
 * QEMU model for Add-Data APCI-1500 PCI device
 * Based on Linux driver addi_apci_1500.c
 * Implements Z8536 CIO, AMCC interrupt controller, digital I/O
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

/* Vendor and Device IDs from driver */
#define APCI1500_VENDOR_ID       0x10e8
#define APCI1500_DEVICE_ID       0x80fc
#define APCI1500_CLASS_ID        PCI_CLASS_OTHERS

#ifndef BIT
#define BIT(x) (1UL << (x))
#endif

/* BAR sizes (minimum required based on register access) */
#define APCI1500_BAR0_SIZE 0x100
#define APCI1500_BAR1_SIZE 0x100
#define APCI1500_BAR2_SIZE 0x10

/* Z8536 register definitions (from addi_apci_1500.c) */
#define APCI1500_Z8536_PORTC_REG    0x00
#define APCI1500_Z8536_PORTB_REG    0x01
#define APCI1500_Z8536_PORTA_REG    0x02
#define APCI1500_Z8536_CTRL_REG     0x03
#define APCI1500_CLK_SEL_REG        0x00
#define APCI1500_DI_REG             0x00
#define APCI1500_DO_REG             0x02
#define Z8536_PC_DPP_REG           0x05
#define Z8536_PAB_MODE_PMS_DISABLE  Z8536_PAB_MODE_PMS(0)
#define Z8536_PB_DD_REG            0x2b
#define Z8536_CFG_CTRL_REG         0x01
#define Z8536_PA_CMDSTAT_REG       0x08
#define Z8536_PB_DPP_REG           0x2a
#define Z8536_CT_CMDSTAT_REG(x)    (0x0a + (x))
#define Z8536_PAB_MODE_SB          BIT(4)
#define Z8536_PB_MODE_REG          0x28
#define Z8536_CMD_CLR_IP_IUS        Z8536_CMD(1)
#define Z8536_PC_DD_REG            0x06
#define Z8536_PA_DD_REG            0x23
#define Z8536_INT_CTRL_REG         0x00
#define Z8536_PB_CMDSTAT_REG       0x09
#define Z8536_PA_MODE_REG          0x20
#define Z8536_CMD_CLR_IE            Z8536_CMD(7)
#define Z8536_PAB_MODE_PTS_BIT      0
#define Z8536_CFG_CTRL_PBE         BIT(7)
#define Z8536_CFG_CTRL_PAE         BIT(2)
#define Z8536_CFG_CTRL_CT1E        BIT(6)
#define Z8536_CFG_CTRL_PCE_CT3E    BIT(4)
#define Z8536_CFG_CTRL_CT2E        BIT(5)
#define Z8536_STAT_IE_IP           (Z8536_STAT_IE | Z8536_STAT_IP)
#define INTCSR_INTR_ASSERTED       0x800000
#define AMCC_OP_REG_INTCSR         0x38
#define Z8536_INT_CTRL_MIE         BIT(7)
#define Z8536_PAB_MODE_PMS_AND     Z8536_PAB_MODE_PMS(1)
#define Z8536_PA_PM_REG            0x27
#define Z8536_INT_CTRL_DLC         BIT(6)
#define Z8536_PAB_MODE_IMO         BIT(3)
#define Z8536_PB_PP_REG            0x2d
#define Z8536_PAB_MODE_PMS_OR      Z8536_PAB_MODE_PMS(2)
#define Z8536_PB_PT_REG            0x2e
#define Z8536_PB_PM_REG            0x2f
#define Z8536_PA_PP_REG            0x25
#define Z8536_PA_PT_REG            0x26
#define Z8536_CMD_SET_IE           Z8536_CMD(6)
#define Z8536_PAB_MODE_PMS_MASK    Z8536_PAB_MODE_PMS(3)
#define Z8536_CT_MODE_CSC          BIT(7)
#define Z8536_CT_MODE_EGE          BIT(3)
#define Z8536_CT_MODE_DCS_SQRWAVE  Z8536_CT_MODE_DCS(2)
#define Z8536_CT_MODE_REB          BIT(2)
#define Z8536_CT_MODE_ECE          BIT(5)
#define Z8536_CT_MODE_DCS_PULSE    Z8536_CT_MODE_DCS(0)
#define Z8536_STAT_IP              BIT(5)
#define Z8536_CT_RELOAD_LSB_REG(x) (0x17 + ((x) * 2))
#define Z8536_CT_MODE_EOE          BIT(6)
#define Z8536_CT_MODE_REG(x)       (0x1c + (x))
#define Z8536_CT_RELOAD_MSB_REG(x) (0x16 + ((x) * 2))
#define Z8536_CT_CMDSTAT_GCB       BIT(2)
#define Z8536_CT_MODE_ETE          BIT(4)
#define Z8536_CT_STAT_CIP          BIT(0)
#define Z8536_CT_MODE_DCS_ONESHOT  Z8536_CT_MODE_DCS(1)
#define Z8536_CT_CMD_TCB           BIT(1)
#define Z8536_CT_VAL_LSB_REG(x)    (0x11 + ((x) * 2))
#define Z8536_CT_CMD_RCC           BIT(3)
#define Z8536_CT_VAL_MSB_REG(x)    (0x10 + ((x) * 2))
#define INTCSR_INBOX_INTR_STATUS   0x20000
#define INTCSR_INBOX_FULL_INT      0x1000
#define AMCC_OP_REG_IMB1           0x10
#define Z8536_PAB_MODE_PMS(x)      (((x) & 0x3) << 1)
#define Z8536_CMD(x)               (((x) & 0x7) << 5)
#define Z8536_PAB_MODE_PTS(x)      (((x) & 0x3) << 6)
#define Z8536_STAT_IE              BIT(6)
#define Z8536_CT_MODE_DCS(x)       (((x) & 0x3) << 0)

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* AMCC registers */
    uint32_t amcc_intcsr;
    uint32_t amcc_imb1;

    /* Z8536 internal registers (indexed by register address) */
    uint8_t z8536_regs[256];
    uint8_t z8536_addr;
    bool z8536_data_phase;

    /* Digital I/O */
    uint16_t di_value;
    uint16_t do_value;
    uint16_t clk_src;
};

/* Forward declarations */
static uint64_t amcc_pio_read(void *opaque, hwaddr addr, unsigned size);
static void amcc_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t z8536_pio_read(void *opaque, hwaddr addr, unsigned size);
static void z8536_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t addon_pio_read(void *opaque, hwaddr addr, unsigned size);
static void addon_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps amcc_pio_ops = {
    .read = amcc_pio_read,
    .write = amcc_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps z8536_pio_ops = {
    .read = z8536_pio_read,
    .write = z8536_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static const MemoryRegionOps addon_pio_ops = {
    .read = addon_pio_read,
    .write = addon_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

static void apci1500_update_irq(PCIBaseState *s);

/* ------------------------------------------------------------------ */
/* AMCC BAR0 (I/O) handlers */
static uint64_t amcc_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case AMCC_OP_REG_INTCSR:
        /* The INTR_ASSERTED bit reflects Z8536 interrupt status */
        val = s->amcc_intcsr;
        break;
    case AMCC_OP_REG_IMB1:
        val = s->amcc_imb1;
        break;
    default:
        val = ~0ULL;
        break;
    }

    return val;
}

static void amcc_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case AMCC_OP_REG_INTCSR:
        /* Write clears any bits that are set in the value */
        s->amcc_intcsr &= ~val;
        /* Also set bits that are written with 1 (if any) */
        s->amcc_intcsr |= val;
        apci1500_update_irq(s);
        break;
    case AMCC_OP_REG_IMB1:
        s->amcc_imb1 = val;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Z8536 BAR1 (I/O) handlers */

static void z8536_write_cmdstat(PCIBaseState *s, uint8_t reg_addr, uint8_t value)
{
    uint8_t *reg = &s->z8536_regs[reg_addr];
    uint8_t cmd = value & 0xE0; /* upper 3 bits */

    switch (cmd) {
    case Z8536_CMD_CLR_IP_IUS:
        *reg &= ~Z8536_STAT_IP;
        break;
    case Z8536_CMD_CLR_IE:
        *reg &= ~Z8536_STAT_IE;
        break;
    case Z8536_CMD_SET_IE:
        *reg |= Z8536_STAT_IE;
        break;
    default:
        /* For other commands, just store the value; may need further modeling */
        *reg = value;
        break;
    }
    apci1500_update_irq(s);
}

static uint64_t z8536_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case APCI1500_Z8536_PORTC_REG: /* Port C direct */
        val = s->z8536_regs[APCI1500_Z8536_PORTC_REG];
        break;
    case APCI1500_Z8536_PORTB_REG: /* Port B direct */
        val = s->z8536_regs[APCI1500_Z8536_PORTB_REG];
        break;
    case APCI1500_Z8536_PORTA_REG: /* Port A direct */
        val = s->z8536_regs[APCI1500_Z8536_PORTA_REG];
        break;
    case APCI1500_Z8536_CTRL_REG: /* Control register / indirect access */
        if (s->z8536_data_phase) {
            val = s->z8536_regs[s->z8536_addr];
            s->z8536_data_phase = false;
        } else {
            /* If not in data phase, reading returns 0 and doesn't change state */
            val = 0;
        }
        break;
    default:
        val = ~0ULL;
        break;
    }

    return val;
}

static void z8536_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t byte = val & 0xFF;

    switch (addr) {
    case APCI1500_Z8536_PORTC_REG:
        s->z8536_regs[APCI1500_Z8536_PORTC_REG] = byte;
        break;
    case APCI1500_Z8536_PORTB_REG:
        s->z8536_regs[APCI1500_Z8536_PORTB_REG] = byte;
        break;
    case APCI1500_Z8536_PORTA_REG:
        s->z8536_regs[APCI1500_Z8536_PORTA_REG] = byte;
        break;
    case APCI1500_Z8536_CTRL_REG:
        if (!s->z8536_data_phase) {
            /* First write: store register address */
            s->z8536_addr = byte;
            s->z8536_data_phase = true;
        } else {
            /* Second write: data to previously addressed register */
            uint8_t reg_addr = s->z8536_addr;

            /* Special handling for command/status registers */
            if (reg_addr == Z8536_PA_CMDSTAT_REG ||
                reg_addr == Z8536_PB_CMDSTAT_REG ||
                reg_addr == Z8536_CT_CMDSTAT_REG(0) ||
                reg_addr == Z8536_CT_CMDSTAT_REG(1) ||
                reg_addr == Z8536_CT_CMDSTAT_REG(2)) {
                z8536_write_cmdstat(s, reg_addr, byte);
            } else {
                s->z8536_regs[reg_addr] = byte;
            }
            s->z8536_data_phase = false;

            /* After writing to certain registers, update IRQ */
            if (reg_addr == Z8536_INT_CTRL_REG ||
                reg_addr == Z8536_CFG_CTRL_REG) {
                apci1500_update_irq(s);
            }
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Digital I/O (addon) BAR2 handlers */
static uint64_t addon_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case APCI1500_DI_REG: /* also CLK_SEL_REG (read may return di value) */
        val = s->di_value;
        break;
    case APCI1500_DO_REG:
        val = s->do_value;
        break;
    default:
        val = ~0ULL;
        break;
    }

    return val;
}

static void addon_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case APCI1500_DI_REG: /* used as CLK_SEL write */
        s->clk_src = val & 0xFFFF;
        break;
    case APCI1500_DO_REG:
        s->do_value = val & 0xFFFF;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Interrupt logic */
static bool z8536_irq_pending(PCIBaseState *s)
{
    /* Check each interrupt source: Port A, Port B, CT0, CT1, CT2 */
    uint8_t pa = s->z8536_regs[Z8536_PA_CMDSTAT_REG];
    uint8_t pb = s->z8536_regs[Z8536_PB_CMDSTAT_REG];
    uint8_t ct0 = s->z8536_regs[Z8536_CT_CMDSTAT_REG(0)];
    uint8_t ct1 = s->z8536_regs[Z8536_CT_CMDSTAT_REG(1)];
    uint8_t ct2 = s->z8536_regs[Z8536_CT_CMDSTAT_REG(2)];

    if ((pa & Z8536_STAT_IE) && (pa & Z8536_STAT_IP)) return true;
    if ((pb & Z8536_STAT_IE) && (pb & Z8536_STAT_IP)) return true;
    if ((ct0 & Z8536_STAT_IE) && (ct0 & Z8536_STAT_IP)) return true;
    if ((ct1 & Z8536_STAT_IE) && (ct1 & Z8536_STAT_IP)) return true;
    if ((ct2 & Z8536_STAT_IE) && (ct2 & Z8536_STAT_IP)) return true;

    return false;
}

static void apci1500_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool z_int = z8536_irq_pending(s);
    bool amcc_int_en = (s->amcc_intcsr & INTCSR_INBOX_FULL_INT) != 0;

    if (z_int && amcc_int_en) {
        s->amcc_intcsr |= INTCSR_INTR_ASSERTED;
        pci_set_irq(pdev, 1);
    } else {
        s->amcc_intcsr &= ~INTCSR_INTR_ASSERTED;
        pci_set_irq(pdev, 0);
    }
}

/* ------------------------------------------------------------------ */
/* Reset */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear AMCC registers */
    s->amcc_intcsr = 0;
    s->amcc_imb1 = 0;

    /* Clear Z8536 state */
    memset(s->z8536_regs, 0, sizeof(s->z8536_regs));
    s->z8536_addr = 0;
    s->z8536_data_phase = false;

    /* Digital I/O */
    s->di_value = 0;
    s->do_value = 0;
    s->clk_src = 0;

    /* Ensure IRQ is lowered */
    apci1500_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* BAR registration helper */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        if (bi->index == 0) {
            memory_region_init_io(mr, OBJECT(s), &amcc_pio_ops, s, bi->name, aligned_size);
        } else if (bi->index == 1) {
            memory_region_init_io(mr, OBJECT(s), &z8536_pio_ops, s, bi->name, aligned_size);
        } else if (bi->index == 2) {
            memory_region_init_io(mr, OBJECT(s), &addon_pio_ops, s, bi->name, aligned_size);
        } else {
            error_setg(errp, "Invalid BAR index %d", bi->index);
            return;
        }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  APCI1500_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  APCI1500_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, APCI1500_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR configuration */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = APCI1500_BAR0_SIZE,
        .name = "amcc"
    };
    s->bar_info[1] = (BARInfo){
        .index = 1,
        .type = BAR_TYPE_PIO,
        .size = APCI1500_BAR1_SIZE,
        .name = "z8536"
    };
    s->bar_info[2] = (BARInfo){
        .index = 2,
        .type = BAR_TYPE_PIO,
        .size = APCI1500_BAR2_SIZE,
        .name = "addon"
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
    .name = "addi_apci_1500_pci",
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
