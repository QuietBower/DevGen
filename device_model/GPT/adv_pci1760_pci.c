/*
 * QEMU PCI device model for Advantech PCI-1760 based on
 * Linux driver drivers/comedi/drivers/adv_pci1760.c
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

#define TYPE_PCIBASE_DEVICE "adv_pci1760_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI1760_VENDOR_ID 0x13fe /* PCI_VENDOR_ID_ADVANTECH */
#define PCI1760_DEVICE_ID 0x1760
#define PCI1760_CLASS_ID  PCI_CLASS_OTHERS

#define PCI1760_OMB_REG(x)        (0x0c + (x))
#define PCI1760_IMB_REG(x)        (0x1c + (x))
#define PCI1760_INTCSR_REG(x)     (0x38 + (x))
#define PCI1760_INTCSR1_IRQ_ENA       (1U << 5)
#define PCI1760_INTCSR2_OMB_IRQ       (1U << 0)
#define PCI1760_INTCSR2_IMB_IRQ       (1U << 1)
#define PCI1760_INTCSR2_IRQ_STATUS    (1U << 6)
#define PCI1760_INTCSR2_IRQ_ASSERTED  (1U << 7)

#define PCI1760_CMD_CLR_IMB2          0x00
#define PCI1760_CMD_SET_DO            0x01
#define PCI1760_CMD_GET_DO            0x02
#define PCI1760_CMD_GET_STATUS        0x07
#define PCI1760_CMD_GET_FW_VER        0x0e
#define PCI1760_CMD_GET_HW_VER        0x0f
#define PCI1760_CMD_SET_PWM_HI(x)     (0x10 + (x) * 2)
#define PCI1760_CMD_SET_PWM_LO(x)     (0x11 + (x) * 2)
#define PCI1760_CMD_SET_PWM_CNT(x)    (0x14 + (x))
#define PCI1760_CMD_ENA_PWM           0x1f
#define PCI1760_CMD_ENA_FILT          0x20
#define PCI1760_CMD_ENA_PAT_MATCH     0x21
#define PCI1760_CMD_SET_PAT_MATCH     0x22
#define PCI1760_CMD_ENA_RISE_EDGE     0x23
#define PCI1760_CMD_ENA_FALL_EDGE     0x24
#define PCI1760_CMD_ENA_CNT           0x28
#define PCI1760_CMD_RST_CNT           0x29
#define PCI1760_CMD_ENA_CNT_OFLOW     0x2a
#define PCI1760_CMD_ENA_CNT_MATCH     0x2b
#define PCI1760_CMD_SET_CNT_EDGE      0x2c
#define PCI1760_CMD_GET_CNT           0x2f
#define PCI1760_CMD_SET_HI_SAMP(x)    (0x30 + (x))
#define PCI1760_CMD_SET_LO_SAMP(x)    (0x38 + (x))
#define PCI1760_CMD_SET_CNT(x)        (0x40 + (x))
#define PCI1760_CMD_SET_CNT_MATCH(x)  (0x48 + (x))
#define PCI1760_CMD_GET_INT_FLAGS     0x60
#define PCI1760_CMD_GET_INT_FLAGS_MATCH   (1U << 0)
#define PCI1760_CMD_GET_INT_FLAGS_COS     (1U << 1)
#define PCI1760_CMD_GET_INT_FLAGS_OFLOW   (1U << 2)
#define PCI1760_CMD_GET_OS           0x61
#define PCI1760_CMD_GET_CNT_STATUS   0x62
#define PCI1760_CMD_TIMEOUT          250
#define PCI1760_CMD_RETRIES          3
#define PCI1760_PWM_TIMEBASE         100000

/* BAR description helpers */
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

    /* Simple shadow state derived from driver usage */
    uint8_t omb[4];        /* outbound mailboxes 0..3 */
    uint8_t imb[4];        /* inbound mailboxes 0..3 */

    /* internal logical state visible via commands */
    uint8_t do_state;      /* 8 digital outputs */

    uint8_t pwm_enabled;   /* bitmask for channels 0..7, only 0..1 used */
    uint16_t pwm_hi[2];    /* HI divisor per channel */
    uint16_t pwm_lo[2];    /* LO divisor per channel */
    uint16_t pwm_cnt[2];   /* PWM counter preload per channel */

    uint8_t cnt_enabled;       /* from CMD_ENA_CNT */
    uint8_t cnt_oflow_enabled; /* from CMD_ENA_CNT_OFLOW */
    uint8_t cnt_match_enabled; /* from CMD_ENA_CNT_MATCH */
    uint8_t cnt_edge;          /* from CMD_SET_CNT_EDGE */

    uint16_t cnt_val[8];       /* counters (CMD_SET_CNT / GET_CNT) */
    uint16_t cnt_match[8];     /* match values */

    uint8_t filt_enabled;      /* CMD_ENA_FILT */
    uint8_t pat_match_enabled; /* CMD_ENA_PAT_MATCH */
    uint16_t pat_match;        /* CMD_SET_PAT_MATCH */

    uint16_t int_flags;        /* for CMD_GET_INT_FLAGS */

    uint8_t intcsr[4];         /* INTCSR registers 0..3 */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_asserted = false;

    /* Basic model: if INTCSR1_IRQ_ENA is set and there are interrupt flags,
     * assert the PCI INTx line and reflect status into INTCSR2 bits.
     */
    if (s->intcsr[1] & PCI1760_INTCSR1_IRQ_ENA) {
        if (s->int_flags != 0) {
            irq_asserted = true;
        }
    }

    if (irq_asserted) {
        s->intcsr[2] |= PCI1760_INTCSR2_IRQ_STATUS | PCI1760_INTCSR2_IRQ_ASSERTED;
        pci_set_irq(pdev, 1);
    } else {
        s->intcsr[2] &= ~(PCI1760_INTCSR2_IRQ_STATUS | PCI1760_INTCSR2_IRQ_ASSERTED);
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_process_command(PCIBaseState *s)
{
    uint8_t cmd = s->omb[2];
    uint16_t val = s->omb[0] | ((uint16_t)s->omb[1] << 8);
    uint16_t ret = 0;
    bool have_ret = true;

    switch (cmd) {
    case PCI1760_CMD_CLR_IMB2:
        s->imb[2] = 0x00;
        have_ret = false;
        break;

    case PCI1760_CMD_SET_DO:
        s->do_state = val & 0xff;
        ret = s->do_state;
        break;

    case PCI1760_CMD_GET_DO:
        ret = s->do_state;
        break;

    case PCI1760_CMD_GET_STATUS:
        switch (val) {
        case PCI1760_CMD_ENA_PWM:
            ret = s->pwm_enabled;
            break;
        default:
            have_ret = false;
            break;
        }
        break;

    case PCI1760_CMD_ENA_PWM:
        s->pwm_enabled = val & 0xff;
        ret = s->pwm_enabled;
        break;

    case PCI1760_CMD_SET_PWM_CNT(0):
    case PCI1760_CMD_SET_PWM_CNT(1):
        if (cmd == PCI1760_CMD_SET_PWM_CNT(0)) {
            s->pwm_cnt[0] = val;
            ret = s->pwm_cnt[0];
        } else {
            s->pwm_cnt[1] = val;
            ret = s->pwm_cnt[1];
        }
        break;

    case PCI1760_CMD_SET_PWM_HI(0):
    case PCI1760_CMD_SET_PWM_HI(1):
        if (cmd == PCI1760_CMD_SET_PWM_HI(0)) {
            s->pwm_hi[0] = val;
            ret = s->pwm_hi[0];
        } else {
            s->pwm_hi[1] = val;
            ret = s->pwm_hi[1];
        }
        break;

    case PCI1760_CMD_SET_PWM_LO(0):
    case PCI1760_CMD_SET_PWM_LO(1):
        if (cmd == PCI1760_CMD_SET_PWM_LO(0)) {
            s->pwm_lo[0] = val;
            ret = s->pwm_lo[0];
        } else {
            s->pwm_lo[1] = val;
            ret = s->pwm_lo[1];
        }
        break;

    case PCI1760_CMD_GET_INT_FLAGS:
        ret = s->int_flags;
        break;

    case PCI1760_CMD_GET_CNT:
        if (val < 8) {
            ret = s->cnt_val[val];
        } else {
            have_ret = false;
        }
        break;

    case PCI1760_CMD_ENA_CNT:
        s->cnt_enabled = val & 0xff;
        ret = s->cnt_enabled;
        break;

    case PCI1760_CMD_ENA_CNT_OFLOW:
        s->cnt_oflow_enabled = val & 0xff;
        ret = s->cnt_oflow_enabled;
        break;

    case PCI1760_CMD_ENA_CNT_MATCH:
        s->cnt_match_enabled = val & 0xff;
        ret = s->cnt_match_enabled;
        break;

    case PCI1760_CMD_RST_CNT:
        /* reset counters which have corresponding bits set in val */
        for (int i = 0; i < 8; i++) {
            if (val & (1U << i)) {
                s->cnt_val[i] = 0;
            }
        }
        ret = 0;
        break;

    case PCI1760_CMD_SET_CNT_EDGE:
        s->cnt_edge = val & 0xff;
        ret = s->cnt_edge;
        break;

    case PCI1760_CMD_SET_CNT(0):
    case PCI1760_CMD_SET_CNT(1):
    case PCI1760_CMD_SET_CNT(2):
    case PCI1760_CMD_SET_CNT(3):
    case PCI1760_CMD_SET_CNT(4):
    case PCI1760_CMD_SET_CNT(5):
    case PCI1760_CMD_SET_CNT(6):
    case PCI1760_CMD_SET_CNT(7):
        {
            int idx = cmd - PCI1760_CMD_SET_CNT(0);
            if (idx >= 0 && idx < 8) {
                s->cnt_val[idx] = val;
                ret = s->cnt_val[idx];
            } else {
                have_ret = false;
            }
        }
        break;

    case PCI1760_CMD_SET_CNT_MATCH(0):
    case PCI1760_CMD_SET_CNT_MATCH(1):
    case PCI1760_CMD_SET_CNT_MATCH(2):
    case PCI1760_CMD_SET_CNT_MATCH(3):
    case PCI1760_CMD_SET_CNT_MATCH(4):
    case PCI1760_CMD_SET_CNT_MATCH(5):
    case PCI1760_CMD_SET_CNT_MATCH(6):
    case PCI1760_CMD_SET_CNT_MATCH(7):
        {
            int idx = cmd - PCI1760_CMD_SET_CNT_MATCH(0);
            if (idx >= 0 && idx < 8) {
                s->cnt_match[idx] = val;
                ret = s->cnt_match[idx];
            } else {
                have_ret = false;
            }
        }
        break;

    case PCI1760_CMD_ENA_FILT:
        s->filt_enabled = val & 0xff;
        ret = s->filt_enabled;
        break;

    case PCI1760_CMD_ENA_PAT_MATCH:
        s->pat_match_enabled = val & 0xff;
        ret = s->pat_match_enabled;
        break;

    case PCI1760_CMD_SET_PAT_MATCH:
        s->pat_match = val;
        ret = s->pat_match;
        break;

    case PCI1760_CMD_GET_FW_VER:
    case PCI1760_CMD_GET_HW_VER:
        /* Provide fixed dummy versions */
        ret = 0x0100;
        break;

    case PCI1760_CMD_GET_OS:
        /* Return a dummy OS code */
        ret = 0x0001;
        break;

    case PCI1760_CMD_GET_CNT_STATUS:
        /* Return a simple aggregation of counter enable flags */
        ret = s->cnt_enabled;
        break;

    default:
        have_ret = false;
        break;
    }

    if (have_ret) {
        s->imb[0] = ret & 0xff;
        s->imb[1] = (ret >> 8) & 0xff;
        s->imb[2] = cmd;
    } else {
        /* Indicate completion without data */
        s->imb[0] = 0;
        s->imb[1] = 0;
        s->imb[2] = cmd;
    }

    /* optional: mark IMB IRQ flag if enabled */
    if (s->intcsr[1] & PCI1760_INTCSR1_IRQ_ENA) {
        s->int_flags |= PCI1760_CMD_GET_INT_FLAGS_MATCH;
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t val8 = 0xff;

    /* The driver only uses byte I/O via inb()/outb() */
    if (size != 1) {
        return 0xff;
    }

    switch (addr) {
    case 0x1c: /* IMB0 */
        val8 = s->imb[0];
        break;
    case 0x1d: /* IMB1 */
        val8 = s->imb[1];
        break;
    case 0x1e: /* IMB2 */
        val8 = s->imb[2];
        break;
    case 0x1f: /* IMB3 - DI inputs */
        /* The driver reads DI here; provide a simple fixed pattern. */
        val8 = 0x00;
        break;
    case 0x38: /* INTCSR0 */
        val8 = s->intcsr[0];
        break;
    case 0x39: /* INTCSR1 */
        val8 = s->intcsr[1];
        break;
    case 0x3a: /* INTCSR2 (status - RO in hw, but we model) */
        val8 = s->intcsr[2];
        break;
    case 0x3b: /* INTCSR3 */
        val8 = s->intcsr[3];
        break;
    default:
        val8 = 0xff;
        break;
    }

    return val8;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v = (uint8_t)val;

    if (size != 1) {
        return;
    }

    switch (addr) {
    case 0x0c: /* OMB0 */
        s->omb[0] = v;
        break;
    case 0x0d: /* OMB1 */
        s->omb[1] = v;
        break;
    case 0x0e: /* OMB2 (command) */
        s->omb[2] = v;
        break;
    case 0x0f: /* OMB3 (unused by driver) */
        s->omb[3] = v;
        /* When driver writes 0 here after setting command, process it. */
        pcibase_process_command(s);
        break;

    case 0x38: /* INTCSR0 */
        s->intcsr[0] = v;
        pcibase_update_irq(s);
        break;
    case 0x39: /* INTCSR1 - interrupt enable */
        s->intcsr[1] = v;
        pcibase_update_irq(s);
        break;
    case 0x3b: /* INTCSR3 */
        s->intcsr[3] = v;
        pcibase_update_irq(s);
        break;

    default:
        /* other addresses ignored */
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

    memset(s->omb, 0, sizeof(s->omb));
    memset(s->imb, 0, sizeof(s->imb));
    memset(s->intcsr, 0, sizeof(s->intcsr));

    s->do_state = 0x00;

    s->pwm_enabled = 0x00;
    s->pwm_hi[0] = s->pwm_hi[1] = 0;
    s->pwm_lo[0] = s->pwm_lo[1] = 0;
    s->pwm_cnt[0] = s->pwm_cnt[1] = 0;

    s->cnt_enabled = 0;
    s->cnt_oflow_enabled = 0;
    s->cnt_match_enabled = 0;
    s->cnt_edge = 0;

    for (int i = 0; i < 8; i++) {
        s->cnt_val[i] = 0;
        s->cnt_match[i] = 0x8000;
    }

    s->filt_enabled = 0;
    s->pat_match_enabled = 0;
    s->pat_match = 0;

    s->int_flags = 0;

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI1760_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI1760_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI1760_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x40; /* covers OMB/IMB/INTCSR regions (<= 0x3b) */
    s->bar_info[0].name = "adv_pci1760-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    pcibase_reset(DEVICE(s));
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

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "adv_pci1760_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(omb, PCIBaseState, 4),
        VMSTATE_UINT8_ARRAY(imb, PCIBaseState, 4),
        VMSTATE_UINT8(do_state, PCIBaseState),
        VMSTATE_UINT8(pwm_enabled, PCIBaseState),
        VMSTATE_UINT16_ARRAY(pwm_hi, PCIBaseState, 2),
        VMSTATE_UINT16_ARRAY(pwm_lo, PCIBaseState, 2),
        VMSTATE_UINT16_ARRAY(pwm_cnt, PCIBaseState, 2),
        VMSTATE_UINT8(cnt_enabled, PCIBaseState),
        VMSTATE_UINT8(cnt_oflow_enabled, PCIBaseState),
        VMSTATE_UINT8(cnt_match_enabled, PCIBaseState),
        VMSTATE_UINT8(cnt_edge, PCIBaseState),
        VMSTATE_UINT16_ARRAY(cnt_val, PCIBaseState, 8),
        VMSTATE_UINT16_ARRAY(cnt_match, PCIBaseState, 8),
        VMSTATE_UINT8(filt_enabled, PCIBaseState),
        VMSTATE_UINT8(pat_match_enabled, PCIBaseState),
        VMSTATE_UINT16(pat_match, PCIBaseState),
        VMSTATE_UINT16(int_flags, PCIBaseState),
        VMSTATE_UINT8_ARRAY(intcsr, PCIBaseState, 4),
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
