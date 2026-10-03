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
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "ipu3_imgu_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL         0x8086
#define IMGU_PCI_ID                 0x1919
#define IMGU_PCI_BAR                0
#define IMGU_DMA_MASK               DMA_BIT_MASK(39)

#define IMGU_REG_BASE               0x4000
#define IMGU_REG_PM_CTRL            (IMGU_REG_BASE + 0x0)
#define IMGU_REG_SYSTEM_REQ         (IMGU_REG_BASE + 0x18)
#define IMGU_REG_INT_STATUS         (IMGU_REG_BASE + 0x30)
#define IMGU_REG_INT_ENABLE         (IMGU_REG_BASE + 0x34)
#define IMGU_REG_STATE              (IMGU_REG_BASE + 0x130)
#define IMGU_REG_TLB_INVALIDATE     (IMGU_REG_BASE + 0x300)
#define REG_L1_PHYS                 (IMGU_REG_BASE + 0x304)
#define IMGU_REG_GP_BUSY            (IMGU_REG_BASE + 0x500)
#define IMGU_REG_GP_HALT            (IMGU_REG_BASE + 0x5dc)
#define IMGU_REG_CIO_GATE_BURST_STATE (IMGU_REG_BASE + 0x404)
#define IMGU_REG_ISP_CTRL           (IMGU_REG_BASE + 0x00)
#define IMGU_REG_ISP_START_ADDR     (IMGU_REG_BASE + 0x04)
#define IMGU_REG_ISP_ICACHE_ADDR    (IMGU_REG_BASE + 0x10)
#define IMGU_REG_ISP_DMEM_BASE      (IMGU_REG_BASE + 0xc000)
#define IMGU_REG_SP_CTRL(sp)        (IMGU_REG_BASE + (sp) * 0x100 + 0x100)
#define IMGU_REG_SP_START_ADDR(sp)  (IMGU_REG_BASE + (sp) * 0x100 + 0x104)
#define IMGU_REG_SP_ICACHE_ADDR(sp) (IMGU_REG_BASE + (sp) * 0x100 + 0x11c)
#define IMGU_REG_SP_DMEM_BASE(n)    (IMGU_REG_BASE + (n) * 0x4000 + 0x4000)
#define IMGU_SP_PMEM_BASE(n)        (0x20000 + (n) * 0x4000)
#define IMGU_REG_GDC_BASE           (IMGU_REG_BASE + 0x18000)
#define IMGU_REG_GDC_LUT_BASE       (IMGU_REG_GDC_BASE + 0x140)

#define IMGU_REG_IRQCTRL_BASE(n)    (IMGU_REG_BASE + (n) * 0x100 + 0x700)
#define IMGU_REG_IRQCTRL_EDGE(n)    (IMGU_REG_IRQCTRL_BASE(n) + 0x00)
#define IMGU_REG_IRQCTRL_MASK(n)    (IMGU_REG_IRQCTRL_BASE(n) + 0x04)
#define IMGU_REG_IRQCTRL_STATUS(n)  (IMGU_REG_IRQCTRL_BASE(n) + 0x08)
#define IMGU_REG_IRQCTRL_CLEAR(n)   (IMGU_REG_IRQCTRL_BASE(n) + 0x0c)
#define IMGU_REG_IRQCTRL_ENABLE(n)  (IMGU_REG_IRQCTRL_BASE(n) + 0x10)
#define IMGU_REG_IRQCTRL_EDGE_NOT_PULSE(n) (IMGU_REG_IRQCTRL_BASE(n) + 0x14)
#define IMGU_IRQCTRL_NUM            3
#define IMGU_IRQCTRL_MAIN           0
#define IMGU_IRQCTRL_SP0            1
#define IMGU_IRQCTRL_SP1            2

#define IMGU_GP_STRMON_STAT_ISP_PORT_DMA2ISP         BIT(2)
#define IMGU_GP_STRMON_STAT_ISP_PORT_SP12ISP         BIT(6)
#define IMGU_GP_STRMON_STAT_MOD_PORT_SP12DMA        BIT(0)
#define IMGU_GP_STRMON_STAT_SP1_PORT_DMA2SP1         BIT(2)
#define IMGU_GP_STRMON_STAT_MOD_PORT_ISP2DMA        BIT(8)
#define IMGU_GP_STRMON_STAT_SP1_PORT_ISP2SP1        BIT(10)
#define IMGU_GP_STRMON_STAT_MOD_PORT_CELLS2GDC      BIT(12)
#define IMGU_GP_STRMON_STAT_MOD_PORT_GDC2CELLS      BIT(14)

#define IMGU_REG_GP_SP1_STRMON_STAT  (IMGU_REG_BASE + 0x520)
#define IMGU_REG_GP_ISP_STRMON_STAT  (IMGU_REG_BASE + 0x528)
#define IMGU_REG_GP_MOD_STRMON_STAT  (IMGU_REG_BASE + 0x52c)

#define IMGU_PM_CTRL_START              BIT(0)
#define IMGU_PM_CTRL_RACE_TO_HALT       BIT(2)
#define IMGU_PM_CTRL_CSS_PWRDN          BIT(4)
#define IMGU_PM_CTRL_RST_AT_EOF         BIT(5)
#define IMGU_PM_CTRL_FORCE_HALT         BIT(6)
#define IMGU_PM_CTRL_FORCE_UNHALT       BIT(7)
#define IMGU_PM_CTRL_FORCE_RESET        BIT(9)

#define IMGU_STATE_HALT_STS             BIT(0)
#define IMGU_STATE_IDLE_STS             BIT(1)
#define IMGU_STATE_POWER_DOWN           BIT(3)
#define IMGU_STATE_PWRDNM_FSM_MASK      0x1E00000

#define IMGU_CTRL_IDLE                  BIT(5)
#define IMGU_CTRL_START                 BIT(1)
#define IMGU_CTRL_RUN                   BIT(3)
#define IMGU_CTRL_ICACHE_INV            BIT(12)
#define IMGU_CTRL_IRQ_CLEAR             BIT(8)
#define IMGU_CTRL_IRQ_READY             BIT(10)

#define IMGU_REG_INT_CSS_IRQ            BIT(31)
#define IMGU_IRQCTRL_IRQ_SP1            BIT(0)
#define IMGU_IRQCTRL_IRQ_SP2            BIT(1)
#define IMGU_IRQCTRL_IRQ_SW_PIN(n)      BIT(23 + (n))

#define IMGU_TLB_INVALIDATE             1

/* More register and bit definitions omitted for brevity; they are all extracted from the driver headers */

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
    uint32_t irqctrl_status[IMGU_IRQCTRL_NUM];
    uint32_t irqctrl_mask[IMGU_IRQCTRL_NUM];
    uint32_t irqctrl_enable[IMGU_IRQCTRL_NUM];
    uint32_t irqctrl_edge[IMGU_IRQCTRL_NUM];
    uint32_t irqctrl_clear[IMGU_IRQCTRL_NUM];
    uint32_t irqctrl_edge_not_pulse[IMGU_IRQCTRL_NUM];
    uint32_t int_status;
    uint32_t int_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Register file array covering the MMIO address space starting from BAR0 */
    /* The exact size is unknown; placeholder size 0x20000 bytes */
    uint32_t regs[0x20000 / 4];

    /* DMA Context */
    dma_addr_t dma_mask;
    /* Additional DMA state; structure to be defined when DMA logic is implemented */

    /* Operational status flags */
    uint32_t state_reg;     /* IMGU_REG_STATE */
    uint32_t pm_ctrl;       /* IMGU_PM_CTRL */
    uint32_t system_req;    /* SYSTEM_REQ */
    bool in_reset;

    /* Power management state (D0-D3) */
    /* Present but not detailed */
};

static void pcibase_reset(DeviceState *dev);

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;

    /* Main interrupt status */
    if (s->int_status & s->int_enable) {
        raise = true;
    }

    /* Check IRQCTRL blocks */
    for (int i = 0; i < IMGU_IRQCTRL_NUM; i++) {
        if ((s->irqctrl_status[i] & s->irqctrl_mask[i]) &&
            (s->irqctrl_enable[i] & s->irqctrl_mask[i])) {
            raise = true;
            break;
        }
    }

    if (raise) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Helper to update state register based on PM_CTRL bits */
static void pcibase_update_state_from_pm(PCIBaseState *s)
{
    uint32_t pm = s->pm_ctrl;

    /* Power up/down handling */
    if (pm & IMGU_PM_CTRL_START) {
        /* Start sequence: power up */
        s->state_reg &= ~(IMGU_STATE_POWER_DOWN | IMGU_STATE_HALT_STS);
        s->state_reg |= IMGU_STATE_IDLE_STS;
        /* START bit is self-clearing after effect */
        s->pm_ctrl &= ~IMGU_PM_CTRL_START;
    }
    if (pm & IMGU_PM_CTRL_CSS_PWRDN) {
        s->state_reg |= IMGU_STATE_POWER_DOWN;
        s->state_reg &= ~(IMGU_STATE_IDLE_STS | IMGU_STATE_HALT_STS);
    }
    if (pm & IMGU_PM_CTRL_FORCE_HALT) {
        s->state_reg |= IMGU_STATE_HALT_STS;
        s->state_reg &= ~IMGU_STATE_IDLE_STS;
    }
    if (pm & IMGU_PM_CTRL_FORCE_UNHALT) {
        s->state_reg &= ~IMGU_STATE_HALT_STS;
        s->state_reg |= IMGU_STATE_IDLE_STS;
    }
    if (pm & IMGU_PM_CTRL_RACE_TO_HALT) {
        /* Race to halt: typically leads to halt if START not set */
        if (!(pm & IMGU_PM_CTRL_START)) {
            s->state_reg |= IMGU_STATE_HALT_STS;
            s->state_reg &= ~IMGU_STATE_IDLE_STS;
        }
    }
    if (pm & IMGU_PM_CTRL_FORCE_RESET) {
        /* Force reset: reset entire device state except PCI config */
        pcibase_reset(DEVICE(s));
        s->pm_ctrl = 0;  /* reset clears pm_ctrl */
        return;
    }
    /* Other bits like RST_AT_EOF are left as is but could affect state in future */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return val;
    }

    switch (addr) {
    case IMGU_REG_PM_CTRL:
        val = s->pm_ctrl;
        break;
    case IMGU_REG_STATE:
        val = s->state_reg;
        break;
    case IMGU_REG_INT_STATUS:
        val = s->int_status;
        break;
    case IMGU_REG_INT_ENABLE:
        val = s->int_enable;
        break;
    case IMGU_REG_SYSTEM_REQ:
        val = s->system_req;
        break;
    case IMGU_REG_TLB_INVALIDATE:
        val = 0; /* Write-only maybe, return 0 */
        break;
    default:
        /* Handle IRQCTRL register reads */
        for (int i = 0; i < IMGU_IRQCTRL_NUM; i++) {
            if (addr == IMGU_REG_IRQCTRL_EDGE(i)) {
                val = s->irqctrl_edge[i];
                return val;
            }
            if (addr == IMGU_REG_IRQCTRL_MASK(i)) {
                val = s->irqctrl_mask[i];
                return val;
            }
            if (addr == IMGU_REG_IRQCTRL_STATUS(i)) {
                val = s->irqctrl_status[i];
                return val;
            }
            if (addr == IMGU_REG_IRQCTRL_CLEAR(i)) {
                val = s->irqctrl_clear[i];
                return val;
            }
            if (addr == IMGU_REG_IRQCTRL_ENABLE(i)) {
                val = s->irqctrl_enable[i];
                return val;
            }
            if (addr == IMGU_REG_IRQCTRL_EDGE_NOT_PULSE(i)) {
                val = s->irqctrl_edge_not_pulse[i];
                return val;
            }
        }
        /* For any other address, return from regs array */
        if (addr < sizeof(s->regs)) {
            val = s->regs[addr / 4];
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case IMGU_REG_PM_CTRL:
        s->pm_ctrl = val;
        pcibase_update_state_from_pm(s);
        break;
    case IMGU_REG_STATE:
        /* Read-only, ignore writes */
        break;
    case IMGU_REG_INT_STATUS:
        /* W1C: write-1-to-clear */
        s->int_status &= ~val;
        pcibase_update_irq(s);
        break;
    case IMGU_REG_INT_ENABLE:
        s->int_enable = val;
        pcibase_update_irq(s);
        break;
    case IMGU_REG_SYSTEM_REQ:
        s->system_req = val;
        break;
    case IMGU_REG_TLB_INVALIDATE:
        if (val == IMGU_TLB_INVALIDATE) {
            /* TLB invalidate action */
        }
        break;
    default:
        /* Handle IRQCTRL writes */
        for (int i = 0; i < IMGU_IRQCTRL_NUM; i++) {
            if (addr == IMGU_REG_IRQCTRL_EDGE(i)) {
                s->irqctrl_edge[i] = val;
                return;
            }
            if (addr == IMGU_REG_IRQCTRL_MASK(i)) {
                s->irqctrl_mask[i] = val;
                pcibase_update_irq(s);
                return;
            }
            if (addr == IMGU_REG_IRQCTRL_STATUS(i)) {
                /* W1C */
                s->irqctrl_status[i] &= ~val;
                pcibase_update_irq(s);
                return;
            }
            if (addr == IMGU_REG_IRQCTRL_CLEAR(i)) {
                /* Write to clear: clear the specified bits from status */
                s->irqctrl_status[i] &= ~val;
                pcibase_update_irq(s);
                return;
            }
            if (addr == IMGU_REG_IRQCTRL_ENABLE(i)) {
                s->irqctrl_enable[i] = val;
                pcibase_update_irq(s);
                return;
            }
            if (addr == IMGU_REG_IRQCTRL_EDGE_NOT_PULSE(i)) {
                s->irqctrl_edge_not_pulse[i] = val;
                return;
            }
        }
        /* For any other address, store in regs array */
        if (addr < sizeof(s->regs)) {
            s->regs[addr / 4] = val;
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used; device is MMIO-only */
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used; device is MMIO-only */
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
    /* Reset registers to power-on defaults */
    s->pm_ctrl = 0;
    s->state_reg = IMGU_STATE_IDLE_STS | IMGU_STATE_POWER_DOWN; /* Idle but powered down */
    s->int_status = 0;
    s->int_enable = 0;
    memset(s->irqctrl_status, 0, sizeof(s->irqctrl_status));
    memset(s->irqctrl_mask, 0, sizeof(s->irqctrl_mask));
    memset(s->irqctrl_enable, 0, sizeof(s->irqctrl_enable));
    s->system_req = 0;
    memset(s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x1919);
    pci_config_set_class(pci_conf, 0x0400);  /* multimedia video */
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
    s->bar_info[0].index = IMGU_PCI_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x20000;  /* Placeholder: actual BAR0 size unknown */
    s->bar_info[0].name = "imgu-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Enable MSI as driver requests */
    s->has_msi = true;
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }

    /* DMA configuration */
    s->dma_mask = (1ULL << 39) - 1;  /* 39-bit DMA mask */

    /* Set initial state to idle and powered down to satisfy CSS power-up sequence */
    s->state_reg = IMGU_STATE_IDLE_STS | IMGU_STATE_POWER_DOWN;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* Free any resources if allocated */
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
