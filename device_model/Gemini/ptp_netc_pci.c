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

#define TYPE_PCIBASE_DEVICE "ptp_netc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define NETC_TMR_PCI_VENDOR_NXP     0x1131
#define NETC_TMR_CTRL               0x0080
#define TMR_CTRL_CK_SEL             (3 << 0)
#define TMR_CTRL_TE                 (1 << 2)
#define TMR_ETEP(i)                 (1 << (8 + (i)))
#define TMR_COMP_MODE               (1 << 15)
#define TMR_CTRL_TCLK_PERIOD        (0x3ff << 16)
#define TMR_CTRL_PPL(i)             (1 << (27 - (i)))
#define TMR_CTRL_FS                 (1 << 28)
#define NETC_TMR_TEVENT             0x0084
#define TMR_TEVNET_PPEN(i)          (1 << (7 - (i)))
#define TMR_TEVENT_PPEN_ALL         (7 << 5)
#define TMR_TEVENT_ALMEN(i)         (1 << (16 + (i)))
#define TMR_TEVENT_ETS_THREN(i)     (1 << (20 + (i)))
#define TMR_TEVENT_ETSEN(i)         (1 << (24 + (i)))
#define TMR_TEVENT_ETS_OVEN(i)      (1 << (28 + (i)))
#define TMR_TEVENT_ETS(i)           (TMR_TEVENT_ETS_THREN(i) | TMR_TEVENT_ETSEN(i) | TMR_TEVENT_ETS_OVEN(i))
#define NETC_TMR_TEMASK             0x0088
#define NETC_TMR_STAT               0x0094
#define TMR_STAT_ETS_VLD(i)         (1 << (24 + (i)))
#define NETC_TMR_CNT_L              0x0098
#define NETC_TMR_CNT_H              0x009c
#define NETC_TMR_ADD                0x00a0
#define NETC_TMR_PRSC               0x00a8
#define NETC_TMR_ECTRL              0x00ac
#define NETC_TMR_OFF_L              0x00b0
#define NETC_TMR_OFF_H              0x00b4
#define NETC_TMR_ALARM_L(i)         (0x00b8 + (i) * 8)
#define NETC_TMR_ALARM_H(i)         (0x00bc + (i) * 8)
#define NETC_TMR_FIPER(i)           (0x00d0 + (i) * 4)
#define NETC_TMR_FIPER_CTRL         0x00dc
#define FIPER_CTRL_DIS(i)           ((1 << 7) << ((i) * 8))
#define FIPER_CTRL_PG(i)            ((1 << 6) << ((i) * 8))
#define FIPER_CTRL_FS_ALARM(i)      ((1 << 5) << ((i) * 8))
#define FIPER_CTRL_PW(i)            (0x1f << ((i) * 8))
#define FIPER_CTRL_SET_PW(i, v)     (((v) & 0x1f) << (8 * (i)))
#define NETC_TMR_ETTS_L(i)          (0x00e0 + (i) * 8)
#define NETC_TMR_ETTS_H(i)          (0x00e4 + (i) * 8)
#define NETC_TMR_CUR_TIME_L         0x00f0
#define NETC_TMR_CUR_TIME_H         0x00f4
#define NETC_TMR_REGS_BAR           0
#define NETC_GLOBAL_OFFSET          0x10000
#define NETC_GLOBAL_IPBRR0          0xbf8
#define IPBRR0_IP_REV               0xffff
#define NETC_REV_4_1                0x0401
#define NETC_TMR_FIPER_NUM          3
#define NETC_TMR_INVALID_CHANNEL    NETC_TMR_FIPER_NUM
#define NETC_TMR_DEFAULT_PRSC       2
#define NETC_TMR_DEFAULT_ALARM      0xffffffffffffffffULL
#define NETC_TMR_DEFAULT_FIPER      0xffffffff
#define NETC_TMR_FIPER_MAX_PW       0x1f
#define NETC_TMR_ALARM_NUM          2
#define NETC_TMR_DEFAULT_ETTF_THR   7
#define NETC_TMR_CCM_TIMER1         0
#define NETC_TMR_SYSTEM_CLK         1
#define NETC_TMR_EXT_OSC            2
#define NETC_TMR_SYSCLK_333M        333333333U

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

enum netc_pp_type {
    NETC_PP_PPS = 1,
    NETC_PP_PEROUT,
};
struct netc_pp {
    enum netc_pp_type type;
    bool enabled;
    int alarm_id;
    uint32_t period;
    uint64_t stime;
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t tmr_tevent;
    uint32_t tmr_temask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t tmr_ctrl;
    uint32_t tmr_stat;
    uint32_t tmr_cnt_l;
    uint32_t tmr_cnt_h;
    uint32_t tmr_add;
    uint32_t tmr_prsc;
    uint32_t tmr_ectrl;
    uint32_t tmr_off_l;
    uint32_t tmr_off_h;
    uint32_t tmr_alarm_l[NETC_TMR_ALARM_NUM];
    uint32_t tmr_alarm_h[NETC_TMR_ALARM_NUM];
    uint32_t tmr_fiper[NETC_TMR_FIPER_NUM];
    uint32_t tmr_fiper_ctrl;
    uint32_t tmr_etts_l[2];
    uint32_t tmr_etts_h[2];
    uint32_t tmr_cur_time_l;
    uint32_t tmr_cur_time_h;
    uint32_t global_ipbrr0;

    uint32_t clk_select;
    uint32_t clk_freq;
    uint32_t oclk_prsc;
    uint64_t period;
    int revision;
    uint8_t pps_channel;
    uint8_t fs_alarm_num;
    uint8_t fs_alarm_bitmap;
    struct netc_pp pp[NETC_TMR_FIPER_NUM];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->tmr_tevent & s->tmr_temask) != 0;

    if (level) {
        if (s->has_msix && msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
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
    case NETC_TMR_CTRL: val = s->tmr_ctrl; break;
    case NETC_TMR_TEVENT: val = s->tmr_tevent; break;
    case NETC_TMR_TEMASK: val = s->tmr_temask; break;
    case NETC_TMR_STAT: val = s->tmr_stat; break;
    case NETC_TMR_CNT_L: val = s->tmr_cnt_l; break;
    case NETC_TMR_CNT_H: val = s->tmr_cnt_h; break;
    case NETC_TMR_ADD: val = s->tmr_add; break;
    case NETC_TMR_PRSC: val = s->tmr_prsc; break;
    case NETC_TMR_ECTRL: val = s->tmr_ectrl; break;
    case NETC_TMR_OFF_L: val = s->tmr_off_l; break;
    case NETC_TMR_OFF_H: val = s->tmr_off_h; break;
    case NETC_TMR_ALARM_L(0): val = s->tmr_alarm_l[0]; break;
    case NETC_TMR_ALARM_H(0): val = s->tmr_alarm_h[0]; break;
    case NETC_TMR_ALARM_L(1): val = s->tmr_alarm_l[1]; break;
    case NETC_TMR_ALARM_H(1): val = s->tmr_alarm_h[1]; break;
    case NETC_TMR_FIPER(0): val = s->tmr_fiper[0]; break;
    case NETC_TMR_FIPER(1): val = s->tmr_fiper[1]; break;
    case NETC_TMR_FIPER(2): val = s->tmr_fiper[2]; break;
    case NETC_TMR_FIPER_CTRL: val = s->tmr_fiper_ctrl; break;
    case NETC_TMR_ETTS_L(0): val = s->tmr_etts_l[0]; break;
    case NETC_TMR_ETTS_H(0): val = s->tmr_etts_h[0]; break;
    case NETC_TMR_ETTS_L(1): val = s->tmr_etts_l[1]; break;
    case NETC_TMR_ETTS_H(1): val = s->tmr_etts_h[1]; break;
    case NETC_TMR_CUR_TIME_L: val = s->tmr_cur_time_l; break;
    case NETC_TMR_CUR_TIME_H: val = s->tmr_cur_time_h; break;
    case NETC_GLOBAL_OFFSET + NETC_GLOBAL_IPBRR0: val = s->global_ipbrr0; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case NETC_TMR_CTRL: s->tmr_ctrl = val; break;
    case NETC_TMR_TEVENT:
        s->tmr_tevent &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case NETC_TMR_TEMASK:
        s->tmr_temask = val;
        pcibase_update_irq(s);
        break;
    case NETC_TMR_CNT_L: s->tmr_cnt_l = val; break;
    case NETC_TMR_CNT_H: s->tmr_cnt_h = val; break;
    case NETC_TMR_ADD: s->tmr_add = val; break;
    case NETC_TMR_PRSC: s->tmr_prsc = val; break;
    case NETC_TMR_ECTRL: s->tmr_ectrl = val; break;
    case NETC_TMR_OFF_L: s->tmr_off_l = val; break;
    case NETC_TMR_OFF_H: s->tmr_off_h = val; break;
    case NETC_TMR_ALARM_L(0): s->tmr_alarm_l[0] = val; break;
    case NETC_TMR_ALARM_H(0): s->tmr_alarm_h[0] = val; break;
    case NETC_TMR_ALARM_L(1): s->tmr_alarm_l[1] = val; break;
    case NETC_TMR_ALARM_H(1): s->tmr_alarm_h[1] = val; break;
    case NETC_TMR_FIPER(0): s->tmr_fiper[0] = val; break;
    case NETC_TMR_FIPER(1): s->tmr_fiper[1] = val; break;
    case NETC_TMR_FIPER(2): s->tmr_fiper[2] = val; break;
    case NETC_TMR_FIPER_CTRL: s->tmr_fiper_ctrl = val; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->tmr_ctrl = 0;
    s->tmr_tevent = 0;
    s->tmr_temask = 0;
    s->tmr_stat = 0;
    s->tmr_cnt_l = 0;
    s->tmr_cnt_h = 0;
    s->tmr_add = 0;
    s->tmr_prsc = 0;
    s->tmr_ectrl = 0;
    s->tmr_off_l = 0;
    s->tmr_off_h = 0;
    for (int i = 0; i < NETC_TMR_ALARM_NUM; i++) {
        s->tmr_alarm_l[i] = 0;
        s->tmr_alarm_h[i] = 0;
    }
    for (int i = 0; i < NETC_TMR_FIPER_NUM; i++) {
        s->tmr_fiper[i] = 0;
    }
    s->tmr_fiper_ctrl = 0;
    for (int i = 0; i < 2; i++) {
        s->tmr_etts_l[i] = 0;
        s->tmr_etts_h[i] = 0;
    }
    s->tmr_cur_time_l = 0;
    s->tmr_cur_time_h = 0;
    s->global_ipbrr0 = NETC_REV_4_1;
    
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  NETC_TMR_PCI_VENDOR_NXP );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0xee02 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xff00 );
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
    s->bar_info[0].index = NETC_TMR_REGS_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x20000; /* 128KB */
    s->bar_info[0].name = "netc_tmr_regs";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;
    if (msix_init(pdev, 1, &s->bar_regions[0], 0, 0x18000, &s->bar_regions[0], 0, 0x18800, 0, errp) < 0) {
        s->has_msix = false;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ptp_netc_pci",
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
