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

/* Additional include files retrieved from driver context */
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "addi_apci_1500_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMCC		0x10e8
#define APCI1500_Z8536_PORTC_REG	0x00
#define APCI1500_Z8536_PORTB_REG	0x01
#define APCI1500_Z8536_PORTA_REG	0x02
#define APCI1500_Z8536_CTRL_REG		0x03
#define APCI1500_CLK_SEL_REG		0x00
#define APCI1500_DI_REG			0x00
#define APCI1500_DO_REG			0x02
#define Z8536_PC_DPP_REG		0x05
#define Z8536_PAB_MODE_PMS_DISABLE	Z8536_PAB_MODE_PMS(0)
#define Z8536_PB_DD_REG			0x2b
#define Z8536_CFG_CTRL_REG		0x01
#define Z8536_PA_CMDSTAT_REG		0x08
#define Z8536_PB_DPP_REG		0x2a
#define Z8536_CT_CMDSTAT_REG(x)		(0x0a + (x))
#define Z8536_PAB_MODE_SB		BIT(4)
#define Z8536_PB_MODE_REG		0x28
#define Z8536_CMD_CLR_IP_IUS		Z8536_CMD(1)
#define Z8536_PC_DD_REG			0x06
#define Z8536_PA_DD_REG			0x23
#define Z8536_INT_CTRL_REG		0x00
#define Z8536_PB_CMDSTAT_REG		0x09
#define Z8536_PA_MODE_REG		0x20
#define Z8536_CMD_CLR_IE		Z8536_CMD(7)
#define Z8536_PAB_MODE_PTS_BIT		Z8536_PAB_MODE_PTS(0 << 6)
#define Z8536_CFG_CTRL_PBE		BIT(7)
#define Z8536_CFG_CTRL_PAE		BIT(2)
#define Z8536_CFG_CTRL_CT1E		BIT(6)
#define Z8536_CFG_CTRL_PCE_CT3E		BIT(4)
#define Z8536_CFG_CTRL_CT2E		BIT(5)
#define Z8536_STAT_IE_IP		(Z8536_STAT_IE | Z8536_STAT_IP)
#define INTCSR_INTR_ASSERTED	0x800000
#define AMCC_OP_REG_INTCSR       0x38
#define Z8536_INT_CTRL_MIE		BIT(7)
#define Z8536_PAB_MODE_PMS_AND		Z8536_PAB_MODE_PMS(1)
#define Z8536_PA_PM_REG			0x27
#define Z8536_INT_CTRL_DLC		BIT(6)
#define Z8536_PAB_MODE_IMO		BIT(3)
#define Z8536_PB_PP_REG			0x2d
#define Z8536_PAB_MODE_PMS_OR		Z8536_PAB_MODE_PMS(2)
#define Z8536_PB_PT_REG			0x2e
#define Z8536_PB_PM_REG			0x2f
#define Z8536_PA_PP_REG			0x25
#define Z8536_PA_PT_REG			0x26
#define Z8536_CMD_SET_IE		Z8536_CMD(6)
#define Z8536_PAB_MODE_PMS_MASK		Z8536_PAB_MODE_PMS(3)
#define Z8536_CT_MODE_CSC		BIT(7)
#define Z8536_CT_MODE_EGE		BIT(3)
#define Z8536_CT_MODE_DCS_SQRWAVE	Z8536_CT_MODE_DCS(2)
#define Z8536_CT_MODE_REB		BIT(2)
#define Z8536_CT_MODE_ECE		BIT(5)
#define Z8536_CT_MODE_DCS_PULSE		Z8536_CT_MODE_DCS(0)
#define Z8536_STAT_IP			BIT(5)
#define Z8536_CT_RELOAD_LSB_REG(x)	(0x17 + ((x) * 2))
#define Z8536_CT_MODE_EOE		BIT(6)
#define Z8536_CT_MODE_REG(x)		(0x1c + (x))
#define Z8536_CT_RELOAD_MSB_REG(x)	(0x16 + ((x) * 2))
#define Z8536_CT_CMDSTAT_GCB		BIT(2)
#define Z8536_CT_MODE_ETE		BIT(4)
#define Z8536_CT_STAT_CIP		BIT(0)
#define Z8536_CT_MODE_DCS_ONESHOT	Z8536_CT_MODE_DCS(1)
#define Z8536_CT_CMD_TCB		BIT(1)
#define Z8536_CT_VAL_LSB_REG(x)		(0x11 + ((x) * 2))
#define Z8536_CT_CMD_RCC		BIT(3)
#define Z8536_CT_VAL_MSB_REG(x)		(0x10 + ((x) * 2))
#define INTCSR_INBOX_INTR_STATUS	0x20000
#define INTCSR_INBOX_FULL_INT	0x1000
#define AMCC_OP_REG_IMB1         0x10
#define Z8536_PAB_MODE_PMS(x)		(((x) & 0x3) << 1)
#define Z8536_CMD(x)			(((x) & 0x7) << 5)
#define Z8536_PAB_MODE_PTS(x)		(((x) & 0x3) << 6)
#define Z8536_STAT_IE			BIT(6)
#define Z8536_CT_MODE_DCS(x)		(((x) & 0x3) << 0)

struct apci1500_private {
    unsigned long amcc;
    unsigned long addon;
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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct apci1500_private apci_priv;

    uint32_t amcc_intcsr;
    uint32_t amcc_imb1;
    uint16_t addon_do;
    uint16_t addon_clk_sel;
    uint8_t z8536_regs[64];
    uint8_t z8536_state;
    uint8_t z8536_index;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = false;

    if ((s->z8536_regs[Z8536_PA_CMDSTAT_REG] & Z8536_STAT_IE_IP) == Z8536_STAT_IE_IP ||
        (s->z8536_regs[Z8536_PB_CMDSTAT_REG] & Z8536_STAT_IE_IP) == Z8536_STAT_IE_IP ||
        (s->z8536_regs[Z8536_CT_CMDSTAT_REG(0)] & Z8536_STAT_IE_IP) == Z8536_STAT_IE_IP ||
        (s->z8536_regs[Z8536_CT_CMDSTAT_REG(1)] & Z8536_STAT_IE_IP) == Z8536_STAT_IE_IP ||
        (s->z8536_regs[Z8536_CT_CMDSTAT_REG(2)] & Z8536_STAT_IE_IP) == Z8536_STAT_IE_IP) {
        irq_active = true;
    }

    if (irq_active) {
        s->amcc_intcsr |= INTCSR_INTR_ASSERTED;
        pci_set_irq(pdev, 1);
    } else {
        s->amcc_intcsr &= ~INTCSR_INTR_ASSERTED;
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No MMIO used by driver */

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO used by driver */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 4) { /* AMCC BAR */
        if (addr == AMCC_OP_REG_INTCSR) {
            val = s->amcc_intcsr;
        } else if (addr == AMCC_OP_REG_IMB1) {
            val = s->amcc_imb1;
        }
    } else if (size == 1) { /* IOBASE BAR */
        if (addr == APCI1500_Z8536_CTRL_REG) {
            if (s->z8536_state == 1) {
                val = s->z8536_regs[s->z8536_index];
                s->z8536_state = 0;
            }
        } else if (addr == APCI1500_Z8536_PORTB_REG) {
            val = 0;
        }
    } else if (size == 2) { /* ADDON BAR */
        if (addr == APCI1500_DI_REG) {
            val = 0;
        }
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    if (size == 4) { /* AMCC BAR */
        if (addr == AMCC_OP_REG_INTCSR) {
            s->amcc_intcsr = val;
            pcibase_update_irq(s);
        }
    } else if (size == 1) { /* IOBASE BAR */
        if (addr == APCI1500_Z8536_CTRL_REG) {
            if (s->z8536_state == 0) {
                s->z8536_index = val & 0x3F;
                s->z8536_state = 1;
            } else {
                uint8_t reg = s->z8536_index;
                uint8_t cmd = (val >> 5) & 0x7;
                if (reg == Z8536_PA_CMDSTAT_REG || reg == Z8536_PB_CMDSTAT_REG ||
                    reg == Z8536_CT_CMDSTAT_REG(0) || reg == Z8536_CT_CMDSTAT_REG(1) ||
                    reg == Z8536_CT_CMDSTAT_REG(2)) {
                    if (cmd == 1) {
                        s->z8536_regs[reg] &= ~Z8536_STAT_IP;
                    } else if (cmd == 6) {
                        s->z8536_regs[reg] |= Z8536_STAT_IE;
                    } else if (cmd == 7) {
                        s->z8536_regs[reg] &= ~Z8536_STAT_IE;
                    }
                    s->z8536_regs[reg] = (s->z8536_regs[reg] & 0xE0) | (val & 0x1F);
                } else {
                    s->z8536_regs[reg] = val;
                }
                s->z8536_state = 0;
                pcibase_update_irq(s);
            }
        }
    } else if (size == 2) { /* ADDON BAR */
        if (addr == APCI1500_DO_REG) {
            s->addon_do = val;
        } else if (addr == APCI1500_CLK_SEL_REG) {
            s->addon_clk_sel = val;
        }
    }
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

    s->amcc_intcsr = 0;
    s->amcc_imb1 = 0;
    s->addon_do = 0;
    s->addon_clk_sel = 0;
    memset(s->z8536_regs, 0, sizeof(s->z8536_regs));
    s->z8536_state = 0;
    s->z8536_index = 0;
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMCC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x80fc );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 0x100, "amcc"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_PIO, 0x100, "iobase"};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_PIO, 0x100, "addon"};
    
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
