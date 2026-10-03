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

#define TYPE_PCIBASE_DEVICE "ptp_netc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1131
#define DEVICE_ID 0xee02
#define CLASS_ID 0xFF00

#define BAR0_SIZE 0x20000

#define NETC_TMR_CTRL			0x0080
#define TMR_CTRL_CK_SEL			GENMASK(1, 0)
#define TMR_CTRL_TE			BIT(2)
#define TMR_ETEP(i)			BIT(8 + (i))
#define TMR_COMP_MODE			BIT(15)
#define TMR_CTRL_TCLK_PERIOD		GENMASK(25, 16)
#define TMR_CTRL_PPL(i)			BIT(27 - (i))
#define TMR_CTRL_FS			BIT(28)
#define NETC_TMR_TEVENT			0x0084
#define TMR_TEVNET_PPEN(i)		BIT(7 - (i))
#define TMR_TEVENT_PPEN_ALL		GENMASK(7, 5)
#define TMR_TEVENT_ALMEN(i)		BIT(16 + (i))
#define TMR_TEVENT_ETS_THREN(i)		BIT(20 + (i))
#define TMR_TEVENT_ETSEN(i)		BIT(24 + (i))
#define TMR_TEVENT_ETS_OVEN(i)		BIT(28 + (i))
#define TMR_TEVENT_ETS(i)		(TMR_TEVENT_ETS_THREN(i) | \
					 TMR_TEVENT_ETSEN(i) | \
					 TMR_TEVENT_ETS_OVEN(i))
#define NETC_TMR_TEMASK			0x0088
#define NETC_TMR_STAT			0x0094
#define TMR_STAT_ETS_VLD(i)		BIT(24 + (i))
#define NETC_TMR_CNT_L			0x0098
#define NETC_TMR_CNT_H			0x009c
#define NETC_TMR_ADD			0x00a0
#define NETC_TMR_PRSC			0x00a8
#define NETC_TMR_ECTRL			0x00ac
#define NETC_TMR_OFF_L			0x00b0
#define NETC_TMR_OFF_H			0x00b4
#define NETC_TMR_ALARM_L(i)		(0x00b8 + (i) * 8)
#define NETC_TMR_ALARM_H(i)		(0x00bc + (i) * 8)
#define NETC_TMR_FIPER(i)		(0x00d0 + (i) * 4)
#define NETC_TMR_FIPER_CTRL		0x00dc
#define FIPER_CTRL_DIS(i)		(BIT(7) << (i) * 8)
#define FIPER_CTRL_PG(i)		(BIT(6) << (i) * 8)
#define FIPER_CTRL_FS_ALARM(i)		(BIT(5) << (i) * 8)
#define FIPER_CTRL_PW(i)		(GENMASK(4, 0) << (i) * 8)
#define FIPER_CTRL_SET_PW(i, v)	((v) & GENMASK(4, 0)) << 8 * (i))
#define NETC_TMR_ETTS_L(i)		(0x00e0 + (i) * 8)
#define NETC_TMR_ETTS_H(i)		(0x00e4 + (i) * 8)
#define NETC_TMR_CUR_TIME_L		0x00f0
#define NETC_TMR_CUR_TIME_H		0x00f4
#define NETC_TMR_REGS_BAR		0
#define NETC_GLOBAL_OFFSET		0x10000
#define NETC_GLOBAL_IPBRR0		0xbf8
#define IPBRR0_IP_REV			GENMASK(15, 0)
#define NETC_REV_4_1			0x0401
#define NETC_TMR_FIPER_NUM		3
#define NETC_TMR_INVALID_CHANNEL	NETC_TMR_FIPER_NUM
#define NETC_TMR_DEFAULT_PRSC		2
#define NETC_TMR_DEFAULT_ALARM		GENMASK_ULL(63, 0)
#define NETC_TMR_DEFAULT_FIPER		GENMASK(31, 0)
#define NETC_TMR_FIPER_MAX_PW		GENMASK(4, 0)
#define NETC_TMR_ALARM_NUM		2
#define NETC_TMR_DEFAULT_ETTF_THR	7
#define NETC_TMR_CCM_TIMER1		0
#define NETC_TMR_SYSTEM_CLK		1
#define NETC_TMR_EXT_OSC		2
#define NETC_TMR_SYSCLK_333M		333333333U

enum netc_pp_type {
	NETC_PP_PPS = 1,
	NETC_PP_PEROUT,
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
    struct {
        uint32_t ctrl;
        uint32_t tevent;
        uint32_t temask;
        uint32_t res1; /* 0x8c */
        uint32_t res2; /* 0x90 */
        uint32_t stat;
        uint32_t cnt_l;
        uint32_t cnt_h;
        uint32_t add;
        uint32_t res3; /* 0xa4 */
        uint32_t prsc;
        uint32_t ectrl;
        uint32_t off_l;
        uint32_t off_h;
        uint32_t alarm_l[NETC_TMR_ALARM_NUM];
        uint32_t alarm_h[NETC_TMR_ALARM_NUM];
        uint32_t fiper[NETC_TMR_FIPER_NUM];
        uint32_t fiper_ctrl;
        uint32_t etts_l[2];
        uint32_t etts_h[2];
        uint32_t cur_time_l;
        uint32_t cur_time_h;
    } regs;
    uint64_t tmr_cnt;   /* Internal counter, loaded from CNT write */
    uint64_t tmr_off;   /* Offset, loaded from OFF write */
    uint32_t ip_revision; /* at offset NETC_GLOBAL_OFFSET + NETC_GLOBAL_IPBRR0 */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->regs.tevent & s->regs.temask) {
        msix_notify(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read size %u at offset 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return ~0ULL;
    }

    switch (addr) {
    case 0x0080: val = s->regs.ctrl; break;
    case 0x0084: val = s->regs.tevent; break;
    case 0x0088: val = s->regs.temask; break;
    case 0x008c: val = s->regs.res1; break;
    case 0x0090: val = s->regs.res2; break;
    case 0x0094: val = s->regs.stat; break;
    case 0x0098: val = s->regs.cnt_l; break;
    case 0x009c: val = s->regs.cnt_h; break;
    case 0x00a0: val = s->regs.add; break;
    case 0x00a4: val = s->regs.res3; break;
    case 0x00a8: val = s->regs.prsc; break;
    case 0x00ac: val = s->regs.ectrl; break;
    case 0x00b0: val = s->regs.off_l; break;
    case 0x00b4: val = s->regs.off_h; break;
    case 0x00b8: val = s->regs.alarm_l[0]; break;
    case 0x00bc: val = s->regs.alarm_h[0]; break;
    case 0x00c0: val = s->regs.alarm_l[1]; break;
    case 0x00c4: val = s->regs.alarm_h[1]; break;
    case 0x00d0: val = s->regs.fiper[0]; break;
    case 0x00d4: val = s->regs.fiper[1]; break;
    case 0x00d8: val = s->regs.fiper[2]; break;
    case 0x00dc: val = s->regs.fiper_ctrl; break;
    case 0x00e0: val = s->regs.etts_l[0]; break;
    case 0x00e4:
        val = s->regs.etts_h[0];
        /* Reading ETTS_H clears corresponding VLD bit */
        s->regs.stat &= ~TMR_STAT_ETS_VLD(0);
        break;
    case 0x00e8: val = s->regs.etts_l[1]; break;
    case 0x00ec:
        val = s->regs.etts_h[1];
        s->regs.stat &= ~TMR_STAT_ETS_VLD(1);
        break;
    case 0x00f0: val = (uint32_t)((s->tmr_cnt + s->tmr_off) & 0xffffffff); break;
    case 0x00f4: val = (uint32_t)(((s->tmr_cnt + s->tmr_off) >> 32) & 0xffffffff); break;
    case 0x10bf8: val = s->ip_revision; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unhandled read at offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write size %u at offset 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return;
    }

    switch (addr) {
    case 0x0080:
        s->regs.ctrl = val;
        break;
    case 0x0084:
        /* W1C: write-1-to-clear */
        s->regs.tevent &= ~val;
        pcibase_update_irq(s);
        break;
    case 0x0088:
        s->regs.temask = val;
        pcibase_update_irq(s);
        break;
    case 0x008c: s->regs.res1 = val; break;
    case 0x0090: s->regs.res2 = val; break;
    case 0x0094:
        /* STAT is read-only, ignore writes */
        break;
    case 0x0098:
        s->regs.cnt_l = val;
        break;
    case 0x009c:
        s->regs.cnt_h = val;
        s->tmr_cnt = ((uint64_t)s->regs.cnt_h << 32) | s->regs.cnt_l;
        break;
    case 0x00a0: s->regs.add = val; break;
    case 0x00a4: s->regs.res3 = val; break;
    case 0x00a8: s->regs.prsc = val; break;
    case 0x00ac: s->regs.ectrl = val; break;
    case 0x00b0: s->regs.off_l = val; break;
    case 0x00b4:
        s->regs.off_h = val;
        s->tmr_off = ((uint64_t)s->regs.off_h << 32) | s->regs.off_l;
        break;
    case 0x00b8: s->regs.alarm_l[0] = val; break;
    case 0x00bc: s->regs.alarm_h[0] = val; break;
    case 0x00c0: s->regs.alarm_l[1] = val; break;
    case 0x00c4: s->regs.alarm_h[1] = val; break;
    case 0x00d0: s->regs.fiper[0] = val; break;
    case 0x00d4: s->regs.fiper[1] = val; break;
    case 0x00d8: s->regs.fiper[2] = val; break;
    case 0x00dc: s->regs.fiper_ctrl = val; break;
    case 0x00e0: s->regs.etts_l[0] = val; break;
    case 0x00e4: /* Should be read-only in hardware, ignore writes */ break;
    case 0x00e8: s->regs.etts_l[1] = val; break;
    case 0x00ec: break;
    case 0x00f0: s->regs.cur_time_l = val; break;
    case 0x00f4: s->regs.cur_time_h = val; break;
    case 0x10bf8: /* Read-only, ignore writes */ break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unhandled write at offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO registers implemented */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO registers implemented */
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

    memset(&s->regs, 0, sizeof(s->regs));
    s->tmr_cnt = 0;
    s->tmr_off = 0;
    s->ip_revision = NETC_REV_4_1;
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
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    if (msix_init(pdev, 1, &s->bar_regions[0], 0, 0x10000, &s->bar_regions[0], 0, 0x11000, 0, errp)) {
        return;
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
