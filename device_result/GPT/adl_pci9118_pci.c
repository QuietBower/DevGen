/*
 * QEMU PCI device model for adl_pci9118
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

#include <stdint.h>

#define TYPE_PCIBASE_DEVICE "adl_pci9118_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define ADL_PCI9118_VENDOR_ID 0x10e8 /* AMCC */
#define ADL_PCI9118_DEVICE_ID 0x80d9
#define ADL_PCI9118_PCI_CLASS_ID PCI_CLASS_OTHERS

#define PCI9118_TIMER_BASE              0x00
#define PCI9118_AI_FIFO_REG             0x10
#define PCI9118_AO_REG(x)               (0x10 + ((x) * 4))
#define PCI9118_AI_STATUS_REG           0x18
#define PCI9118_AI_STATUS_NFULL         (1U << 8)
#define PCI9118_AI_STATUS_NHFULL        (1U << 7)
#define PCI9118_AI_STATUS_NEPTY         (1U << 6)
#define PCI9118_AI_STATUS_ACMP          (1U << 5)
#define PCI9118_AI_STATUS_DTH           (1U << 4)
#define PCI9118_AI_STATUS_BOVER         (1U << 3)
#define PCI9118_AI_STATUS_ADOS          (1U << 2)
#define PCI9118_AI_STATUS_ADOR          (1U << 1)
#define PCI9118_AI_STATUS_ADRDY         (1U << 0)
#define PCI9118_AI_CTRL_REG             0x18
#define PCI9118_AI_CTRL_UNIP            (1U << 7)
#define PCI9118_AI_CTRL_DIFF            (1U << 6)
#define PCI9118_AI_CTRL_SOFTG           (1U << 5)
#define PCI9118_AI_CTRL_EXTG            (1U << 4)
#define PCI9118_AI_CTRL_EXTM            (1U << 3)
#define PCI9118_AI_CTRL_TMRTR           (1U << 2)
#define PCI9118_AI_CTRL_INT             (1U << 1)
#define PCI9118_AI_CTRL_DMA             (1U << 0)
#define PCI9118_DIO_REG                 0x1c
#define PCI9118_SOFTTRG_REG             0x20
#define PCI9118_AI_CHANLIST_REG         0x24
#define PCI9118_AI_CHANLIST_RANGE(x)    (((x) & 0x3) << 8)
#define PCI9118_AI_CHANLIST_CHAN(x)     ((x) << 0)
#define PCI9118_AI_BURST_NUM_REG        0x28
#define PCI9118_AI_AUTOSCAN_MODE_REG    0x2c
#define PCI9118_AI_CFG_REG              0x30
#define PCI9118_AI_CFG_PDTRG            (1U << 7)
#define PCI9118_AI_CFG_PETRG            (1U << 6)
#define PCI9118_AI_CFG_BSSH             (1U << 5)
#define PCI9118_AI_CFG_BM               (1U << 4)
#define PCI9118_AI_CFG_BS               (1U << 3)
#define PCI9118_AI_CFG_PM               (1U << 2)
#define PCI9118_AI_CFG_AM               (1U << 1)
#define PCI9118_AI_CFG_START            (1U << 0)
#define PCI9118_FIFO_RESET_REG          0x34
#define PCI9118_INT_CTRL_REG            0x38
#define PCI9118_INT_CTRL_TIMER          (1U << 3)
#define PCI9118_INT_CTRL_ABOUT          (1U << 2)
#define PCI9118_INT_CTRL_HFULL          (1U << 1)
#define PCI9118_INT_CTRL_DTRG           (1U << 0)
#define START_AI_EXT    0x01
#define STOP_AI_EXT     0x02
#define STOP_AI_INT     0x08
#define AMCC_OP_REG_MWAR         0x24
#define AMCC_OP_REG_MWTC         0x28
#define A2P_HI_PRIORITY          0x00000100UL
#define AMCC_OP_REG_MCSR         0x3c
#define RESET_A2P_FLAGS          0x04000000UL
#define EN_A2P_TRANSFERS         0x00000400UL
#define AMCC_OP_REG_INTCSR       0x38
#define TARGET_ABORT_INT         0x00200000UL
#define MASTER_ABORT_INT         0x00100000UL
#define ANY_S593X_INT            0x00800000UL
#define AINT_WRITE_COMPL         0x00004000UL

/* Simple model constants */
#define PCI9118_FIFO_DEPTH 1024

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

    struct {
        uint32_t ai_status;
        uint32_t ai_ctrl;
        uint32_t dio;
        uint32_t softtrg;
        uint32_t ai_chanlist;
        uint32_t ai_burst_num;
        uint32_t ai_autoscan_mode;
        uint32_t ai_cfg;
        uint32_t fifo_reset;
        uint32_t int_ctrl;

        uint32_t amcc_mwar;
        uint32_t amcc_mwtc;
        uint32_t amcc_mcsr;
        uint32_t amcc_intcsr;
    } regs;

    uint16_t ai_fifo[PCI9118_FIFO_DEPTH];
    unsigned int fifo_head;
    unsigned int fifo_tail;

    uint32_t ao_reg[2];

    QEMUTimer conv_timer;
    bool conv_in_progress;
};

static inline bool fifo_is_empty(PCIBaseState *s)
{
    return s->fifo_head == s->fifo_tail;
}

static inline bool fifo_is_full(PCIBaseState *s)
{
    return ((s->fifo_head + 1) % PCI9118_FIFO_DEPTH) == s->fifo_tail;
}

static inline void fifo_push_sample(PCIBaseState *s, uint16_t sample)
{
    if (fifo_is_full(s)) {
        s->regs.ai_status &= ~PCI9118_AI_STATUS_NFULL;
        s->regs.ai_status |= PCI9118_AI_STATUS_BOVER;
        return;
    }
    s->ai_fifo[s->fifo_head] = sample;
    s->fifo_head = (s->fifo_head + 1) % PCI9118_FIFO_DEPTH;
    s->regs.ai_status |= PCI9118_AI_STATUS_NEPTY;
    s->regs.ai_status |= PCI9118_AI_STATUS_ADRDY;
}

static inline uint16_t fifo_pop_sample(PCIBaseState *s)
{
    uint16_t val = 0;
    if (fifo_is_empty(s)) {
        s->regs.ai_status &= ~PCI9118_AI_STATUS_NEPTY;
        s->regs.ai_status &= ~PCI9118_AI_STATUS_ADRDY;
        return 0;
    }
    val = s->ai_fifo[s->fifo_tail];
    s->fifo_tail = (s->fifo_tail + 1) % PCI9118_FIFO_DEPTH;
    if (fifo_is_empty(s)) {
        s->regs.ai_status &= ~PCI9118_AI_STATUS_NEPTY;
        s->regs.ai_status &= ~PCI9118_AI_STATUS_ADRDY;
    }
    s->regs.ai_status |= PCI9118_AI_STATUS_NFULL;
    return val;
}

static bool pcibase_irq_enabled(PCIBaseState *s)
{
    return s->regs.int_ctrl != 0;
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (pcibase_irq_enabled(s) && (s->regs.ai_status & PCI9118_AI_STATUS_ADRDY)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_conv_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    uint16_t sample = 0x8000;

    fifo_push_sample(s, sample);
    s->conv_in_progress = false;
    pcibase_update_irq(s);
}

static void pcibase_start_conversion(PCIBaseState *s)
{
    if (s->conv_in_progress) {
        return;
    }
    s->conv_in_progress = true;
    timer_mod(&s->conv_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case PCI9118_AI_FIFO_REG:
        if (size == 4) {
            uint16_t sample = fifo_pop_sample(s);
            val = sample;
        }
        break;
    case PCI9118_AI_STATUS_REG:
        if (size == 4) {
            val = s->regs.ai_status;
            s->regs.ai_status &= ~PCI9118_AI_STATUS_ADRDY;
            pcibase_update_irq(s);
        }
        break;
    case PCI9118_DIO_REG:
        if (size == 4) {
            val = s->regs.dio;
        }
        break;
    case PCI9118_SOFTTRG_REG:
        if (size == 4) {
            val = s->regs.softtrg;
        }
        break;
    case PCI9118_AI_CHANLIST_REG:
        if (size == 4) {
            val = s->regs.ai_chanlist;
        }
        break;
    case PCI9118_AI_BURST_NUM_REG:
        if (size == 4) {
            val = s->regs.ai_burst_num;
        }
        break;
    case PCI9118_AI_AUTOSCAN_MODE_REG:
        if (size == 4) {
            val = s->regs.ai_autoscan_mode;
        }
        break;
    case PCI9118_AI_CFG_REG:
        if (size == 4) {
            val = s->regs.ai_cfg;
        }
        break;
    case PCI9118_FIFO_RESET_REG:
        if (size == 4) {
            val = s->regs.fifo_reset;
        }
        break;
    case PCI9118_INT_CTRL_REG:
        if (size == 4) {
            val = s->regs.int_ctrl;
            s->regs.ai_status &= ~PCI9118_AI_STATUS_ADRDY;
            pcibase_update_irq(s);
        }
        break;
    default:
        if (addr >= AMCC_OP_REG_MWAR && addr < AMCC_OP_REG_MWAR + 0x20) {
            switch (addr) {
            case AMCC_OP_REG_MWAR:
                if (size == 4) {
                    val = s->regs.amcc_mwar;
                }
                break;
            case AMCC_OP_REG_MWTC:
                if (size == 4) {
                    val = s->regs.amcc_mwtc;
                }
                break;
            case AMCC_OP_REG_INTCSR:
                if (size == 4) {
                    val = s->regs.amcc_intcsr;
                }
                break;
            case AMCC_OP_REG_MCSR:
                if (size == 4) {
                    val = s->regs.amcc_mcsr;
                }
                break;
            default:
                break;
            }
        } else if (addr == PCI9118_AO_REG(0)) {
            if (size == 4) {
                val = s->ao_reg[0];
            }
        } else if (addr == PCI9118_AO_REG(1)) {
            if (size == 4) {
                val = s->ao_reg[1];
            }
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v = (uint32_t)val;

    switch (addr) {
    case PCI9118_AI_CTRL_REG:
        if (size == 4) {
            s->regs.ai_ctrl = v;
        }
        break;
    case PCI9118_DIO_REG:
        if (size == 4) {
            s->regs.dio = v & 0x0f;
        }
        break;
    case PCI9118_SOFTTRG_REG:
        if (size == 4) {
            s->regs.softtrg = v;
            pcibase_start_conversion(s);
        }
        break;
    case PCI9118_AI_CHANLIST_REG:
        if (size == 4) {
            s->regs.ai_chanlist = v;
        }
        break;
    case PCI9118_AI_BURST_NUM_REG:
        if (size == 4) {
            s->regs.ai_burst_num = v;
        }
        break;
    case PCI9118_AI_AUTOSCAN_MODE_REG:
        if (size == 4) {
            s->regs.ai_autoscan_mode = v;
        }
        break;
    case PCI9118_AI_CFG_REG:
        if (size == 4) {
            s->regs.ai_cfg = v;
        }
        break;
    case PCI9118_FIFO_RESET_REG:
        if (size == 4) {
            s->regs.fifo_reset = v;
            s->fifo_head = s->fifo_tail = 0;
            s->regs.ai_status &= ~(PCI9118_AI_STATUS_NEPTY | PCI9118_AI_STATUS_ADRDY | PCI9118_AI_STATUS_BOVER);
            s->regs.ai_status |= PCI9118_AI_STATUS_NFULL;
        }
        break;
    case PCI9118_INT_CTRL_REG:
        if (size == 4) {
            s->regs.int_ctrl = v & 0x0f;
            pcibase_update_irq(s);
        }
        break;
    default:
        if (addr >= AMCC_OP_REG_MWAR && addr < AMCC_OP_REG_MWAR + 0x20) {
            switch (addr) {
            case AMCC_OP_REG_MWAR:
                if (size == 4) {
                    s->regs.amcc_mwar = v;
                }
                break;
            case AMCC_OP_REG_MWTC:
                if (size == 4) {
                    s->regs.amcc_mwtc = v;
                }
                break;
            case AMCC_OP_REG_MCSR:
                if (size == 4) {
                    s->regs.amcc_mcsr = v;
                }
                break;
            case AMCC_OP_REG_INTCSR:
                if (size == 4) {
                    if (v & 0x00ff0000) {
                        s->regs.amcc_intcsr &= ~0x00ff0000;
                    } else {
                        s->regs.amcc_intcsr = v;
                    }
                }
                break;
            default:
                break;
            }
        } else if (addr == PCI9118_AO_REG(0)) {
            if (size == 4) {
                s->ao_reg[0] = v & 0x0fff;
            }
        } else if (addr == PCI9118_AO_REG(1)) {
            if (size == 4) {
                s->ao_reg[1] = v & 0x0fff;
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->regs.ai_status = PCI9118_AI_STATUS_NFULL;
    s->regs.ai_ctrl = 0;
    s->regs.dio = 0;
    s->regs.softtrg = 0;
    s->regs.ai_chanlist = 0;
    s->regs.ai_burst_num = 0;
    s->regs.ai_autoscan_mode = 0;
    s->regs.ai_cfg = PCI9118_AI_CFG_PDTRG | PCI9118_AI_CFG_PETRG;
    s->regs.fifo_reset = 0;
    s->regs.int_ctrl = 0;

    s->regs.amcc_mwar = 0;
    s->regs.amcc_mwtc = 0;
    s->regs.amcc_mcsr = 0;
    s->regs.amcc_intcsr = 0;

    s->fifo_head = s->fifo_tail = 0;
    s->conv_in_progress = false;

    s->ao_reg[0] = 2047;
    s->ao_reg[1] = 2047;

    timer_del(&s->conv_timer);

    pcibase_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  ADL_PCI9118_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ADL_PCI9118_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ADL_PCI9118_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x40;
    s->bar_info[0].name = "adl_pci9118-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    timer_init_ns(&s->conv_timer, QEMU_CLOCK_VIRTUAL, pcibase_conv_timer_cb, s);

    s->regs.ai_status = PCI9118_AI_STATUS_NFULL;
    s->regs.ai_ctrl = 0;
    s->regs.dio = 0;
    s->regs.softtrg = 0;
    s->regs.ai_chanlist = 0;
    s->regs.ai_burst_num = 0;
    s->regs.ai_autoscan_mode = 0;
    s->regs.ai_cfg = PCI9118_AI_CFG_PDTRG | PCI9118_AI_CFG_PETRG;
    s->regs.fifo_reset = 0;
    s->regs.int_ctrl = 0;

    s->regs.amcc_mwar = 0;
    s->regs.amcc_mwtc = 0;
    s->regs.amcc_mcsr = 0;
    s->regs.amcc_intcsr = 0;

    s->fifo_head = s->fifo_tail = 0;
    s->conv_in_progress = false;

    s->ao_reg[0] = 2047;
    s->ao_reg[1] = 2047;

    pcibase_update_irq(s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    timer_del(&s->conv_timer);

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

