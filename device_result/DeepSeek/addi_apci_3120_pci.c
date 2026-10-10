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

#define TYPE_PCIBASE_DEVICE "addi_apci_3120_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10e8
#define DEVICE_ID 0x818d
#define CLASS_ID 0x0FF00

/* Add-on register offsets */
#define APCI3120_AI_FIFO_REG		0x00
#define APCI3120_CTRL_REG		0x00
#define APCI3120_AI_SOFTTRIG_REG	0x02
#define APCI3120_STATUS_REG		0x02
#define APCI3120_TIMER_REG		0x04
#define APCI3120_CHANLIST_REG		0x06
#define APCI3120_AO_REG(x)		(0x08 + (((x) / 4) * 2))
#define APCI3120_TIMER_MODE_REG		0x0c
#define APCI3120_CTR0_REG		0x0d
#define APCI3120_MODE_REG		0x0e

/* Add-on register bits */
#define APCI3120_FIFO_ADVANCE_ON_BYTE_2		BIT(29)
#define APCI3120_CTRL_EXT_TRIG			BIT(15)
#define APCI3120_CTRL_GATE(x)			BIT(12 + (x))
#define APCI3120_CTRL_PR(x)			(((x) & 0xf) << 8)
#define APCI3120_CTRL_PA(x)			(((x) & 0xf) << 0)
#define APCI3120_STATUS_EOC_INT			BIT(15)
#define APCI3120_STATUS_AMCC_INT		BIT(14)
#define APCI3120_STATUS_EOS_INT			BIT(13)
#define APCI3120_STATUS_TIMER2_INT		BIT(12)
#define APCI3120_STATUS_INT_MASK		(0xf << 12)
#define APCI3120_STATUS_TO_DI_BITS(x)		(((x) >> 8) & 0xf)
#define APCI3120_STATUS_TO_VERSION(x)		(((x) >> 4) & 0xf)
#define APCI3120_STATUS_FIFO_FULL		BIT(2)
#define APCI3120_STATUS_FIFO_EMPTY		BIT(1)
#define APCI3120_STATUS_DA_READY		BIT(0)
#define APCI3120_CHANLIST_INDEX(x)		(((x) & 0xf) << 8)
#define APCI3120_CHANLIST_UNIPOLAR		BIT(7)
#define APCI3120_CHANLIST_GAIN(x)		(((x) & 0x3) << 4)
#define APCI3120_CHANLIST_MUX(x)		(((x) & 0xf) << 0)
#define APCI3120_AO_MUX(x)			(((x) & 0x3) << 14)
#define APCI3120_AO_DATA(x)			((x) << 0)
#define APCI3120_TIMER_MODE(_t, _m)		((_m) << ((_t) * 2))
#define APCI3120_TIMER_MODE0			0
#define APCI3120_TIMER_MODE2			1
#define APCI3120_TIMER_MODE4			2
#define APCI3120_TIMER_MODE5			3
#define APCI3120_TIMER_MODE_MASK(_t)		(3 << ((_t) * 2))
#define APCI3120_CTR0_DO_BITS(x)		((x) << 4)
#define APCI3120_CTR0_TIMER_SEL(x)		((x) << 0)
#define APCI3120_MODE_TIMER2_CLK(x)		(((x) & 0x3) << 6)
#define APCI3120_MODE_TIMER2_CLK_OSC		APCI3120_MODE_TIMER2_CLK(0)
#define APCI3120_MODE_TIMER2_CLK_OUT1		APCI3120_MODE_TIMER2_CLK(1)
#define APCI3120_MODE_TIMER2_CLK_EOC		APCI3120_MODE_TIMER2_CLK(2)
#define APCI3120_MODE_TIMER2_CLK_EOS		APCI3120_MODE_TIMER2_CLK(3)
#define APCI3120_MODE_TIMER2_CLK_MASK		APCI3120_MODE_TIMER2_CLK(3)
#define APCI3120_MODE_TIMER2_AS(x)		(((x) & 0x3) << 4)
#define APCI3120_MODE_TIMER2_AS_TIMER		APCI3120_MODE_TIMER2_AS(0)
#define APCI3120_MODE_TIMER2_AS_COUNTER		APCI3120_MODE_TIMER2_AS(1)
#define APCI3120_MODE_TIMER2_AS_WDOG		APCI3120_MODE_TIMER2_AS(2)
#define APCI3120_MODE_TIMER2_AS_MASK		APCI3120_MODE_TIMER2_AS(3)
#define APCI3120_MODE_SCAN_ENA			BIT(3)
#define APCI3120_MODE_TIMER2_IRQ_ENA		BIT(2)
#define APCI3120_MODE_EOS_IRQ_ENA		BIT(1)
#define APCI3120_MODE_EOC_IRQ_ENA		BIT(0)

/* Add-on indirect access registers */
#define APCI3120_ADDON_ADDR_REG			0x00
#define APCI3120_ADDON_DATA_REG			0x02
#define APCI3120_ADDON_CTRL_REG			0x04
#define APCI3120_ADDON_CTRL_AMWEN_ENA		BIT(1)
#define APCI3120_ADDON_CTRL_A2P_FIFO_ENA	BIT(0)

/* Revision constants */
#define APCI3120_REVA				0xa
#define APCI3120_REVB				0xb
#define APCI3120_REVA_OSC_BASE			70
#define APCI3120_REVB_OSC_BASE			50

/* AMCC S5933 operational registers */
#define AMCC_OP_REG_INTCSR	0x38
#define AMCC_OP_REG_AGCSTS	0x3c
#define AMCC_OP_REG_MCSR	0x3c
#define AMCC_OP_REG_AMWTC	0x58
#define AMCC_OP_REG_AMWAR	0x24
#define AMCC_OP_REG_MWTC	0x28

/* AMCC register bits */
#define AGCSTS_TC_ENABLE	0x10000000
#define RESET_A2P_FLAGS		0x04000000L
#define AGCSTS_RESET_A2P_FIFO	0x02000000
#define AINT_WRITE_COMPL	0x00004000
#define EN_A2P_TRANSFERS	0x00000400
#define AINT_WT_COMPLETE	0x00040000
#define TARGET_ABORT_INT	0x00200000L
#define MASTER_ABORT_INT	0x00100000L
#define AINT_INT_MASK		0x00ff0000
#define ANY_S593X_INT		0x00800000L


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
    struct {
        uint16_t ai_fifo;
        uint16_t ctrl;
        uint16_t softtrig;
        uint16_t status;
        uint16_t timer;
        uint16_t chanlist;
        uint16_t ao_low;    /* offset 0x08: AO channels 0-3 */
        uint16_t ao_high;   /* offset 0x0a: AO channels 4-7 */
        uint16_t timer_mode;
        uint16_t ctr0;
        uint16_t mode;
    } addon_regs;

    /* DMA Context */
    struct {
        dma_addr_t src;
        dma_addr_t dst;
        uint32_t count;
        bool running;
        bool irq_on_complete;
    } dma_state;

    /* AMCC S5933 operational registers */
    struct {
        uint32_t intcsr;
        uint32_t agcsts;
        uint32_t amwtc;
        uint32_t amwar;
        uint32_t mwtc;
    } amcc_regs;

    /* Additional state for emulation */
    uint32_t timers[3];
    uint8_t timer_sel;
    uint16_t addon_addr;
    uint16_t addon_ctrl;
};


/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq = false;
    if (s->addon_regs.status & APCI3120_STATUS_INT_MASK) {
        irq = true;
    }
    if (s->amcc_regs.intcsr & (AINT_WRITE_COMPL | AINT_WT_COMPLETE | MASTER_ABORT_INT | TARGET_ABORT_INT)) {
        irq = true;
    }
    pci_set_irq(pdev, irq);
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Not used by this driver; DMA is done via AMCC registers */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* BAR2 Addon (offsets 0x00-0x0F) */
    if (addr < 0x10) {
        switch (addr) {
        case 0x00: /* Addr register, not readable */ break;
        case 0x02: /* Data register, not readable */ break;
        case 0x04: val = s->addon_ctrl; break;
        }
        return val;
    }

    /* BAR0 AMCC (offsets >= 0x10) */
    switch (addr) {
    case 0x24: val = s->amcc_regs.amwar; break;
    case 0x28: val = s->amcc_regs.mwtc; break;
    case 0x38: {
        uint32_t raw = s->amcc_regs.intcsr;
        if (raw & (AINT_WRITE_COMPL | AINT_WT_COMPLETE | MASTER_ABORT_INT | TARGET_ABORT_INT)) {
            raw |= ANY_S593X_INT;
        } else {
            raw &= ~ANY_S593X_INT;
        }
        val = raw;
        break;
    }
    case 0x3c: val = s->amcc_regs.agcsts; break;
    case 0x58: val = s->amcc_regs.amwtc; break;
    default: break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* BAR2 Addon */
    if (addr < 0x10) {
        switch (addr) {
        case 0x00: /* Address latch (16-bit) */
            s->addon_addr = val & 0xffff;
            break;
        case 0x02: /* Data write to latched address */
        {
            uint32_t reg = s->addon_addr;
            uint16_t data = val & 0xffff;
            if (reg == AMCC_OP_REG_AGCSTS) {
                s->amcc_regs.agcsts = (s->amcc_regs.agcsts & 0xffff0000) | data;
            } else if (reg == (AMCC_OP_REG_AGCSTS + 2)) {
                s->amcc_regs.agcsts = (s->amcc_regs.agcsts & 0xffff) | ((uint32_t)data << 16);
            } else if (reg == AMCC_OP_REG_AMWAR) {
                s->amcc_regs.amwar = (s->amcc_regs.amwar & 0xffff0000) | data;
            } else if (reg == (AMCC_OP_REG_AMWAR + 2)) {
                s->amcc_regs.amwar = (s->amcc_regs.amwar & 0xffff) | ((uint32_t)data << 16);
            } else if (reg == AMCC_OP_REG_AMWTC) {
                s->amcc_regs.amwtc = (s->amcc_regs.amwtc & 0xffff0000) | data;
            } else if (reg == (AMCC_OP_REG_AMWTC + 2)) {
                s->amcc_regs.amwtc = (s->amcc_regs.amwtc & 0xffff) | ((uint32_t)data << 16);
            } else {
                qemu_log_mask(LOG_UNIMP, "addon: write to unimplemented reg 0x%x\n", reg);
            }
            break;
        }
        case 0x04: /* Addon Control */
            s->addon_ctrl = val & 0xffff;
            /* Check if DMA enabled */
            if ((s->addon_ctrl & 0x3) == 0x3) {
                /* Start DMA: simulate instant completion */
                s->amcc_regs.intcsr |= AINT_WRITE_COMPL;
                s->addon_regs.status |= APCI3120_STATUS_AMCC_INT;
                s->amcc_regs.mwtc = 0;
                pcibase_update_irq(s);
            }
            break;
        }
        return;
    }

    /* BAR0 AMCC */
    switch (addr) {
    case 0x24: /* AMWAR */
        if (size == 4) {
            s->amcc_regs.amwar = val;
        }
        break;
    case 0x28: /* MWTC - read-only? ignore writes */
        break;
    case 0x38: /* INTCSR */
        if (size == 4) {
            /* Write-1-to-clear : clear bits that are set in val */
            s->amcc_regs.intcsr &= ~val;
            if (val & AINT_WT_COMPLETE) {
                s->addon_regs.status &= ~APCI3120_STATUS_AMCC_INT;
            }
            pcibase_update_irq(s);
        }
        break;
    case 0x3c: /* AGCSTS / MCSR */
        if (size == 4) {
            s->amcc_regs.agcsts = val;
        }
        break;
    case 0x58: /* AMWTC */
        if (size == 4) {
            s->amcc_regs.amwtc = val;
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00: /* AI_FIFO (16-bit read) */
        if (size == 2) {
            val = s->addon_regs.ai_fifo;
        }
        break;
    case 0x02: /* STATUS */
        if (size == 2) {
            val = s->addon_regs.status;
        }
        break;
    case 0x04: /* TIMER */
        if (size == 2) {
            if (s->timer_sel == 3) {
                val = (s->timers[2] >> 16) & 0xffff;
            } else {
                val = s->timers[s->timer_sel] & 0xffff;
            }
        }
        break;
    case 0x0c: /* TIMER_MODE (8-bit) */
        if (size == 1) {
            val = s->addon_regs.timer_mode & 0xff;
        }
        break;
    case 0x0d: /* CTR0 (8-bit) */
        if (size == 1) {
            val = s->addon_regs.ctr0 & 0xff;
            /* Reading CTR0 clears timer2 interrupt */
            s->addon_regs.status &= ~APCI3120_STATUS_TIMER2_INT;
            pcibase_update_irq(s);
        }
        break;
    case 0x0e: /* MODE (8-bit) */
        if (size == 1) {
            val = s->addon_regs.mode & 0xff;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pio_read: unimpl addr 0x%x size %d\n", (unsigned)addr, size);
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00: /* CTRL (16-bit) */
        if (size == 2) {
            s->addon_regs.ctrl = val & 0xffff;
        }
        break;
    case 0x02: /* SOFTTRIG (16-bit) - write */
        if (size == 2) {
            s->addon_regs.softtrig = val & 0xffff;
        }
        break;
    case 0x04: /* TIMER write (16-bit) */
        if (size == 2) {
            if (s->timer_sel == 3) {
                s->timers[2] = (s->timers[2] & 0xffff) | ((val & 0xffff) << 16);
            } else {
                s->timers[s->timer_sel] = (s->timers[s->timer_sel] & 0xffff0000) | (val & 0xffff);
            }
        }
        break;
    case 0x06: /* CHANLIST (16-bit) */
        if (size == 2) {
            s->addon_regs.chanlist = val & 0xffff;
        }
        break;
    case 0x08: /* AO_REG(0) (16-bit) */
        if (size == 2) {
            s->addon_regs.ao_low = val & 0xffff;
        }
        break;
    case 0x0a: /* AO_REG(4) (16-bit) */
        if (size == 2) {
            s->addon_regs.ao_high = val & 0xffff;
        }
        break;
    case 0x0c: /* TIMER_MODE (8-bit) */
        if (size == 1) {
            s->addon_regs.timer_mode = (s->addon_regs.timer_mode & 0xff00) | (val & 0xff);
        }
        break;
    case 0x0d: /* CTR0 (8-bit) */
        if (size == 1) {
            /* Update DO bits and timer selection */
            s->addon_regs.ctr0 = (s->addon_regs.ctr0 & 0xff00) | (val & 0xff);
            s->timer_sel = val & 0x3;
        }
        break;
    case 0x0e: /* MODE (8-bit) */
        if (size == 1) {
            s->addon_regs.mode = (s->addon_regs.mode & 0xff00) | (val & 0xff);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "pio_write: unimpl addr 0x%x size %d val 0x%lx\n", (unsigned)addr, size, val);
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

    memset(&s->addon_regs, 0, sizeof(s->addon_regs));
    s->addon_regs.status = (0xb << 4) | 0x03; /* REVB, FIFO_EMPTY, DA_READY */
    memset(&s->amcc_regs, 0, sizeof(s->amcc_regs));
    s->timers[0] = s->timers[1] = s->timers[2] = 0;
    s->timer_sel = 0;
    s->addon_addr = 0;
    s->addon_ctrl = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "amcc";
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 0x10;
    s->bar_info[1].name = "iobase";
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x10;
    s->bar_info[2].name = "addon";
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
    .name = "addi_apci_3120_pci",
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
