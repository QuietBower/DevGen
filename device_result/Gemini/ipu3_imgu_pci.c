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

#define TYPE_PCIBASE_DEVICE "ipu3_imgu_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IMGU_PCI_ID 0x1919
#define IMGU_PCI_BAR 0
#define IMGU_REG_BASE 0x4000
#define IMGU_REG_PM_CTRL 0x0
#define IMGU_REG_ISP_START_ADDR 0x04
#define IMGU_REG_ISP_ICACHE_ADDR 0x10
#define IMGU_REG_SYSTEM_REQ 0x18
#define IMGU_REG_INT_STATUS 0x30
#define IMGU_REG_INT_ENABLE 0x34
#define IMGU_REG_STATE 0x130
#define IMGU_REG_TLB_INVALIDATE 0x300
#define IMGU_REG_CIO_GATE_BURST_STATE (IMGU_REG_BASE + 0x404)
#define IMGU_REG_GP_BUSY (IMGU_REG_BASE + 0x500)
#define IMGU_REG_GP_SP1_STRMON_STAT (IMGU_REG_BASE + 0x520)
#define IMGU_REG_GP_ISP_STRMON_STAT (IMGU_REG_BASE + 0x528)
#define IMGU_REG_GP_MOD_STRMON_STAT (IMGU_REG_BASE + 0x52c)
#define IMGU_REG_GP_HALT (IMGU_REG_BASE + 0x5dc)
#define IMGU_REG_IRQCTRL_BASE(n) (IMGU_REG_BASE + (n) * 0x100 + 0x700)
#define IMGU_REG_SP_CTRL(sp) (IMGU_REG_BASE + (sp) * 0x100 + 0x100)
#define IMGU_REG_SP_START_ADDR(sp) (IMGU_REG_BASE + (sp) * 0x100 + 0x104)
#define IMGU_REG_SP_ICACHE_ADDR(sp) (IMGU_REG_BASE + (sp) * 0x100 + 0x11c)
#define IMGU_REG_ISP_DMEM_BASE (IMGU_REG_BASE + 0xc000)
#define IMGU_REG_SP_DMEM_BASE(n) (IMGU_REG_BASE + (n) * 0x4000 + 0x4000)
#define IMGU_REG_GDC_BASE (IMGU_REG_BASE + 0x18000)
#define IMGU_REG_GDC_LUT_BASE (IMGU_REG_GDC_BASE + 0x140)

#define IMGU_STATE_IDLE_STS			(1U << 1)
#define IMGU_PM_CTRL_FORCE_RESET		(1U << 9)
#define IMGU_PM_CTRL_RACE_TO_HALT		(1U << 2)
#define IMGU_PM_CTRL_START			(1U << 0)
#define IMGU_PM_CTRL_CSS_PWRDN			(1U << 4)
#define IMGU_PM_CTRL_RST_AT_EOF			(1U << 5)
#define IMGU_STATE_POWER_DOWN			(1U << 3)
#define IMGU_STATE_PWRDNM_FSM_MASK		0x1E00000
#define IMGU_SYSTEM_REQ_FREQ_DIVIDER		25
#define IMGU_SYSTEM_REQ_FREQ_MASK		0x3f
#define IMGU_PM_CTRL_FORCE_HALT			(1U << 6)
#define IMGU_STATE_HALT_STS			(1U << 0)
#define IMGU_PM_CTRL_FORCE_UNHALT		(1U << 7)
#define REG_L1_PHYS		(IMGU_REG_BASE + 0x304)
#define IMGU_IRQCTRL_NUM			3
#define IMGU_REG_IRQCTRL_STATUS(n)	(IMGU_REG_IRQCTRL_BASE(n) + 0x08)
#define IMGU_IRQCTRL_SP0			1
#define IMGU_IRQCTRL_IRQ_SW_PIN(n)		(1U << (23 + (n)))
#define IMGU_REG_IRQCTRL_CLEAR(n)	(IMGU_REG_IRQCTRL_BASE(n) + 0x0c)
#define IMGU_REG_IRQCTRL_ENABLE(n)	(IMGU_REG_IRQCTRL_BASE(n) + 0x10)
#define IMGU_IRQCTRL_MAIN			0
#define IMGU_IRQCTRL_SP1			2
#define IMGU_CIO_GATE_BURST_MASK        0x80

#define IMGU_DMA_MASK ((1ULL << 39) - 1)

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
    uint32_t int_status;
    uint32_t int_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t pm_ctrl;
    uint32_t isp_start_addr;
    uint32_t isp_icache_addr;
    uint32_t system_req;
    uint32_t state;
    uint32_t tlb_invalidate;
    uint32_t cio_gate_burst_state;
    uint32_t gp_busy;
    uint32_t gp_sp1_strmon_stat;
    uint32_t gp_isp_strmon_stat;
    uint32_t gp_mod_strmon_stat;
    uint32_t gp_halt;
    uint32_t irqctrl_edge[3];
    uint32_t irqctrl_mask[3];
    uint32_t irqctrl_status[3];
    uint32_t irqctrl_clear[3];
    uint32_t irqctrl_enable[3];
    uint32_t irqctrl_edge_not_pulse[3];
    uint32_t sp_ctrl[2];
    uint32_t sp_start_addr[2];
    uint32_t sp_icache_addr[2];
    uint32_t l1_phys;

    /* DMA Context */
    uint64_t dma_mask;

    uint32_t halt_sts;
    uint32_t idle_sts;
    uint32_t power_down;
    bool is_resetting;
    uint32_t pm_state;
    
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool should_interrupt = (s->int_status & s->int_enable) != 0;
    
    for (int i = 0; i < 3; i++) {
        if (s->irqctrl_status[i] & s->irqctrl_enable[i]) {
            should_interrupt = true;
        }
    }

    if (should_interrupt) {
        if (s->has_msi) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case IMGU_REG_PM_CTRL: val = s->pm_ctrl; break;
    case IMGU_REG_ISP_START_ADDR: val = s->isp_start_addr; break;
    case IMGU_REG_ISP_ICACHE_ADDR: val = s->isp_icache_addr; break;
    case IMGU_REG_SYSTEM_REQ: val = s->system_req; break;
    case IMGU_REG_INT_STATUS: val = s->int_status; break;
    case IMGU_REG_INT_ENABLE: val = s->int_enable; break;
    case IMGU_REG_STATE: val = s->state; break;
    case IMGU_REG_TLB_INVALIDATE: val = s->tlb_invalidate; break;
    case IMGU_REG_CIO_GATE_BURST_STATE: val = s->cio_gate_burst_state; break;
    case IMGU_REG_GP_BUSY: val = s->gp_busy; break;
    case IMGU_REG_GP_SP1_STRMON_STAT: val = s->gp_sp1_strmon_stat; break;
    case IMGU_REG_GP_ISP_STRMON_STAT: val = s->gp_isp_strmon_stat; break;
    case IMGU_REG_GP_MOD_STRMON_STAT: val = s->gp_mod_strmon_stat; break;
    case IMGU_REG_GP_HALT: val = s->gp_halt; break;
    case REG_L1_PHYS: val = s->l1_phys; break;
    default:
        if (addr >= IMGU_REG_IRQCTRL_BASE(0) && addr < IMGU_REG_IRQCTRL_BASE(3) + 0x100) {
            int n = (addr - IMGU_REG_IRQCTRL_BASE(0)) / 0x100;
            uint32_t offset = (addr - IMGU_REG_IRQCTRL_BASE(0)) % 0x100;
            if (n < 3) {
                switch (offset) {
                case 0x08: val = s->irqctrl_status[n]; break;
                case 0x0c: val = s->irqctrl_clear[n]; break;
                case 0x10: val = s->irqctrl_enable[n]; break;
                }
            }
        } else if (addr >= IMGU_REG_SP_CTRL(0) && addr < IMGU_REG_SP_CTRL(2) + 0x100) {
            int sp = (addr - IMGU_REG_SP_CTRL(0)) / 0x100;
            uint32_t offset = (addr - IMGU_REG_SP_CTRL(0)) % 0x100;
            if (sp < 2) {
                switch (offset) {
                case 0x00: val = s->sp_ctrl[sp]; break;
                case 0x04: val = s->sp_start_addr[sp]; break;
                case 0x1c: val = s->sp_icache_addr[sp]; break;
                }
            }
        }
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case IMGU_REG_PM_CTRL: 
        s->pm_ctrl = val; 
        if (val & IMGU_PM_CTRL_FORCE_RESET) {
            s->state = IMGU_STATE_IDLE_STS | IMGU_STATE_POWER_DOWN;
        }
        if (val & IMGU_PM_CTRL_START) {
            s->pm_ctrl &= ~IMGU_PM_CTRL_START; /* Auto-clear on start */
        }
        if (val & IMGU_PM_CTRL_FORCE_HALT) {
            s->state |= IMGU_STATE_HALT_STS;
        }
        if (val & IMGU_PM_CTRL_FORCE_UNHALT) {
            s->state &= ~IMGU_STATE_HALT_STS;
        }
        if (!(val & IMGU_PM_CTRL_CSS_PWRDN)) {
            s->state &= ~IMGU_STATE_PWRDNM_FSM_MASK;
            s->state &= ~IMGU_STATE_POWER_DOWN;
        } else {
            s->state |= IMGU_STATE_POWER_DOWN;
        }
        break;
    case IMGU_REG_ISP_START_ADDR: s->isp_start_addr = val; break;
    case IMGU_REG_ISP_ICACHE_ADDR: s->isp_icache_addr = val; break;
    case IMGU_REG_SYSTEM_REQ: s->system_req = val; break;
    case IMGU_REG_INT_STATUS: 
        s->int_status &= ~val; /* W1C assumption */
        pcibase_update_irq(s);
        break;
    case IMGU_REG_INT_ENABLE: 
        s->int_enable = val; 
        pcibase_update_irq(s);
        break;
    case IMGU_REG_STATE: s->state = val; break;
    case IMGU_REG_TLB_INVALIDATE: s->tlb_invalidate = val; break;
    case IMGU_REG_CIO_GATE_BURST_STATE: 
        s->cio_gate_burst_state = val; 
        /* Driver expects this to clear IMGU_CIO_GATE_BURST_MASK */
        s->cio_gate_burst_state &= ~IMGU_CIO_GATE_BURST_MASK;
        break;
    case IMGU_REG_GP_BUSY: s->gp_busy = val; break;
    case IMGU_REG_GP_SP1_STRMON_STAT: s->gp_sp1_strmon_stat = val; break;
    case IMGU_REG_GP_ISP_STRMON_STAT: s->gp_isp_strmon_stat = val; break;
    case IMGU_REG_GP_MOD_STRMON_STAT: s->gp_mod_strmon_stat = val; break;
    case IMGU_REG_GP_HALT: 
        s->gp_halt = val; 
        if (val == 1) {
            s->state |= IMGU_STATE_HALT_STS;
        } else {
            s->state &= ~IMGU_STATE_HALT_STS;
        }
        break;
    case REG_L1_PHYS: s->l1_phys = val; break;
    default:
        if (addr >= IMGU_REG_IRQCTRL_BASE(0) && addr < IMGU_REG_IRQCTRL_BASE(3) + 0x100) {
            int n = (addr - IMGU_REG_IRQCTRL_BASE(0)) / 0x100;
            uint32_t offset = (addr - IMGU_REG_IRQCTRL_BASE(0)) % 0x100;
            if (n < 3) {
                switch (offset) {
                case 0x08: s->irqctrl_status[n] = val; break;
                case 0x0c: 
                    s->irqctrl_clear[n] = val; 
                    s->irqctrl_status[n] &= ~val;
                    pcibase_update_irq(s);
                    break;
                case 0x10: 
                    s->irqctrl_enable[n] = val; 
                    pcibase_update_irq(s);
                    break;
                }
            }
        } else if (addr >= IMGU_REG_SP_CTRL(0) && addr < IMGU_REG_SP_CTRL(2) + 0x100) {
            int sp = (addr - IMGU_REG_SP_CTRL(0)) / 0x100;
            uint32_t offset = (addr - IMGU_REG_SP_CTRL(0)) % 0x100;
            if (sp < 2) {
                switch (offset) {
                case 0x00: s->sp_ctrl[sp] = val; break;
                case 0x04: s->sp_start_addr[sp] = val; break;
                case 0x1c: s->sp_icache_addr[sp] = val; break;
                }
            }
        }
        break;
    }
}

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

    s->pm_ctrl = 0;
    s->isp_start_addr = 0;
    s->isp_icache_addr = 0;
    s->system_req = 0;
    s->state = IMGU_STATE_IDLE_STS | IMGU_STATE_POWER_DOWN;
    s->tlb_invalidate = 0;
    s->cio_gate_burst_state = 0;
    s->gp_busy = 0;
    s->gp_sp1_strmon_stat = 0;
    s->gp_isp_strmon_stat = 0;
    s->gp_mod_strmon_stat = 0;
    s->gp_halt = 0;
    s->int_status = 0;
    s->int_enable = 0;
    s->l1_phys = 0;
    for (int i = 0; i < 3; i++) {
        s->irqctrl_status[i] = 0;
        s->irqctrl_clear[i] = 0;
        s->irqctrl_enable[i] = 0;
    }
    for (int i = 0; i < 2; i++) {
        s->sp_ctrl[i] = 0;
        s->sp_start_addr[i] = 0;
        s->sp_icache_addr[i] = 0;
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IMGU_PCI_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_OTHER );
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
    s->bar_info[0] = (BARInfo){ .index = IMGU_PCI_BAR, .type = BAR_TYPE_MMIO, .size = 0x400000, .name = "imgu-mmio" };
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;
    s->dma_mask = IMGU_DMA_MASK;
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
    .name = "ipu3_imgu_pci",
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
