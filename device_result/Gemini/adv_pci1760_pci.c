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

#define TYPE_PCIBASE_DEVICE "adv_pci1760_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ADVANTECH		0x13fe
#define PCI1760_OMB_REG(x)		(0x0c + (x))
#define PCI1760_IMB_REG(x)		(0x1c + (x))
#define PCI1760_INTCSR_REG(x)		(0x38 + (x))
#define PCI1760_INTCSR1_IRQ_ENA		BIT(5)
#define PCI1760_INTCSR2_OMB_IRQ		BIT(0)
#define PCI1760_INTCSR2_IMB_IRQ		BIT(1)
#define PCI1760_INTCSR2_IRQ_STATUS	BIT(6)
#define PCI1760_INTCSR2_IRQ_ASSERTED	BIT(7)
#define PCI1760_CMD_CLR_IMB2		0x00
#define PCI1760_CMD_SET_DO		0x01
#define PCI1760_CMD_GET_DO		0x02
#define PCI1760_CMD_GET_STATUS		0x07
#define PCI1760_CMD_GET_FW_VER		0x0e
#define PCI1760_CMD_GET_HW_VER		0x0f
#define PCI1760_CMD_SET_PWM_HI(x)	(0x10 + (x) * 2)
#define PCI1760_CMD_SET_PWM_LO(x)	(0x11 + (x) * 2)
#define PCI1760_CMD_SET_PWM_CNT(x)	(0x14 + (x))
#define PCI1760_CMD_ENA_PWM		0x1f
#define PCI1760_CMD_ENA_FILT		0x20
#define PCI1760_CMD_ENA_PAT_MATCH	0x21
#define PCI1760_CMD_SET_PAT_MATCH	0x22
#define PCI1760_CMD_ENA_RISE_EDGE	0x23
#define PCI1760_CMD_ENA_FALL_EDGE	0x24
#define PCI1760_CMD_ENA_CNT		0x28
#define PCI1760_CMD_RST_CNT		0x29
#define PCI1760_CMD_ENA_CNT_OFLOW	0x2a
#define PCI1760_CMD_ENA_CNT_MATCH	0x2b
#define PCI1760_CMD_SET_CNT_EDGE	0x2c
#define PCI1760_CMD_GET_CNT		0x2f
#define PCI1760_CMD_SET_HI_SAMP(x)	(0x30 + (x))
#define PCI1760_CMD_SET_LO_SAMP(x)	(0x38 + (x))
#define PCI1760_CMD_SET_CNT(x)		(0x40 + (x))
#define PCI1760_CMD_SET_CNT_MATCH(x)	(0x48 + (x))
#define PCI1760_CMD_GET_INT_FLAGS	0x60
#define PCI1760_CMD_GET_INT_FLAGS_MATCH	BIT(0)
#define PCI1760_CMD_GET_INT_FLAGS_COS	BIT(1)
#define PCI1760_CMD_GET_INT_FLAGS_OFLOW	BIT(2)
#define PCI1760_CMD_GET_OS		0x61
#define PCI1760_CMD_GET_CNT_STATUS	0x62
#define PCI1760_CMD_TIMEOUT		250
#define PCI1760_CMD_RETRIES		3
#define PCI1760_PWM_TIMEBASE		100000

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
    uint32_t intcsr[4];

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t omb[4];
    uint8_t imb[4];

    uint8_t do_state;
    uint8_t di_state;
    uint16_t pwm_hi[2];
    uint16_t pwm_lo[2];
    uint16_t pwm_cnt[2];
    uint16_t pwm_ena;
    uint16_t cnt_ena;
    uint16_t cnt_oflow_ena;
    uint16_t cnt_match_ena;
    uint16_t cnt_match[8];
    uint16_t cnt[8];
    uint16_t cnt_edge;
    uint16_t filt_ena;
    uint16_t pat_match_ena;
    uint16_t pat_match;
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

static void pcibase_process_cmd(PCIBaseState *s)
{
    uint8_t cmd = s->omb[2];
    uint16_t val = s->omb[0] | (s->omb[1] << 8);
    uint16_t ret_val = 0;

    switch (cmd) {
    case PCI1760_CMD_CLR_IMB2:
        break;
    case PCI1760_CMD_SET_DO:
        s->do_state = val & 0xff;
        break;
    case PCI1760_CMD_GET_DO:
        ret_val = s->do_state;
        break;
    case PCI1760_CMD_GET_STATUS:
        switch (val) {
        case PCI1760_CMD_ENA_PWM: ret_val = s->pwm_ena; break;
        case PCI1760_CMD_SET_PWM_HI(0): ret_val = s->pwm_hi[0]; break;
        case PCI1760_CMD_SET_PWM_HI(1): ret_val = s->pwm_hi[1]; break;
        case PCI1760_CMD_SET_PWM_LO(0): ret_val = s->pwm_lo[0]; break;
        case PCI1760_CMD_SET_PWM_LO(1): ret_val = s->pwm_lo[1]; break;
        }
        break;
    case PCI1760_CMD_ENA_PWM: s->pwm_ena = val; break;
    case PCI1760_CMD_SET_PWM_HI(0): s->pwm_hi[0] = val; break;
    case PCI1760_CMD_SET_PWM_HI(1): s->pwm_hi[1] = val; break;
    case PCI1760_CMD_SET_PWM_LO(0): s->pwm_lo[0] = val; break;
    case PCI1760_CMD_SET_PWM_LO(1): s->pwm_lo[1] = val; break;
    case PCI1760_CMD_SET_PWM_CNT(0): s->pwm_cnt[0] = val; break;
    case PCI1760_CMD_SET_PWM_CNT(1): s->pwm_cnt[1] = val; break;
    case PCI1760_CMD_ENA_FILT: s->filt_ena = val; break;
    case PCI1760_CMD_ENA_PAT_MATCH: s->pat_match_ena = val; break;
    case PCI1760_CMD_SET_PAT_MATCH: s->pat_match = val; break;
    case PCI1760_CMD_ENA_CNT: s->cnt_ena = val; break;
    case PCI1760_CMD_RST_CNT:
        for (int i = 0; i < 8; i++) {
            if (val & (1 << i)) {
                s->cnt[i] = 0;
            }
        }
        break;
    case PCI1760_CMD_ENA_CNT_OFLOW: s->cnt_oflow_ena = val; break;
    case PCI1760_CMD_ENA_CNT_MATCH: s->cnt_match_ena = val; break;
    case PCI1760_CMD_SET_CNT_EDGE: s->cnt_edge = val; break;
    default:
        if (cmd >= PCI1760_CMD_SET_CNT_MATCH(0) && cmd <= PCI1760_CMD_SET_CNT_MATCH(7)) {
            s->cnt_match[cmd - PCI1760_CMD_SET_CNT_MATCH(0)] = val;
        } else if (cmd >= PCI1760_CMD_SET_CNT(0) && cmd <= PCI1760_CMD_SET_CNT(7)) {
            s->cnt[cmd - PCI1760_CMD_SET_CNT(0)] = val;
        }
        break;
    }

    s->imb[0] = ret_val & 0xff;
    s->imb[1] = (ret_val >> 8) & 0xff;
    s->imb[2] = cmd;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= PCI1760_OMB_REG(0) && addr <= PCI1760_OMB_REG(3)) {
        val = s->omb[addr - PCI1760_OMB_REG(0)];
    } else if (addr >= PCI1760_IMB_REG(0) && addr <= PCI1760_IMB_REG(3)) {
        if (addr == PCI1760_IMB_REG(3)) {
            val = s->di_state;
        } else {
            val = s->imb[addr - PCI1760_IMB_REG(0)];
        }
    } else if (addr >= PCI1760_INTCSR_REG(0) && addr <= PCI1760_INTCSR_REG(3)) {
        val = s->intcsr[addr - PCI1760_INTCSR_REG(0)];
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= PCI1760_OMB_REG(0) && addr <= PCI1760_OMB_REG(3)) {
        s->omb[addr - PCI1760_OMB_REG(0)] = val & 0xff;
        if (addr == PCI1760_OMB_REG(3)) {
            pcibase_process_cmd(s);
        }
    } else if (addr >= PCI1760_INTCSR_REG(0) && addr <= PCI1760_INTCSR_REG(3)) {
        s->intcsr[addr - PCI1760_INTCSR_REG(0)] = val & 0xff;
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

    memset(s->omb, 0, sizeof(s->omb));
    memset(s->imb, 0, sizeof(s->imb));
    memset(s->intcsr, 0, sizeof(s->intcsr));
    s->do_state = 0;
    s->di_state = 0;
    memset(s->pwm_hi, 0, sizeof(s->pwm_hi));
    memset(s->pwm_lo, 0, sizeof(s->pwm_lo));
    memset(s->pwm_cnt, 0, sizeof(s->pwm_cnt));
    s->pwm_ena = 0;
    s->cnt_ena = 0;
    s->cnt_oflow_ena = 0;
    s->cnt_match_ena = 0;
    memset(s->cnt_match, 0, sizeof(s->cnt_match));
    memset(s->cnt, 0, sizeof(s->cnt));
    s->cnt_edge = 0;
    s->filt_ena = 0;
    s->pat_match_ena = 0;
    s->pat_match = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ADVANTECH );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1760 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
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
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x40;
    s->bar_info[0].name = "adv_pci1760_pio";

    /* BAR Initialization */
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
    .name = "adv_pci1760_pci",
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
