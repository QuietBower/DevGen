/*
 * QEMU PCI device model for rtl8188ee (Phase 2: Behavioral Implementation)
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

#include <linux/pci_regs.h>

#define TYPE_PCIBASE_DEVICE "rtl8188ee_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID   PCI_VENDOR_ID_REALTEK
#define PCIBASE_DEVICE_ID   0x8179
#define PCIBASE_CLASS_ID    0x0280

#define REG_SYS_ISO_CTRL    0x0000
#define REG_SYS_FUNC_EN     0x0002
#define REG_SYS_CLKR        0x0008
#define REG_EFUSE_ACCESS    0x00CF
#define REG_EFUSE_CTRL      0x0030
#define REG_EFUSE_TEST      0x0034
#define REG_HSISR           0x005c
#define REG_GPIO_OUTPUT     0x006c
#define REG_HSIMR           0x0058
#define REG_HIMR            0x00B0
#define REG_HIMRE           0x00B8
#define REG_HISRE           0x00BC
#define REG_PCIE_CTRL_REG   0x0300
#define REG_INT_MIG         0x0304
#define REG_BCNQ_DESA       0x0308
#define REG_HQ_DESA         0x0310
#define REG_MGQ_DESA        0x0318
#define REG_VOQ_DESA        0x0320
#define REG_VIQ_DESA        0x0328
#define REG_BEQ_DESA        0x0330
#define REG_BKQ_DESA        0x0338
#define REG_RX_DESA         0x0340
#define REG_PCIE_HRPWM      0x0361
#define REG_WATCH_DOG       0x0368
#define REG_TCR             0x0604
#define REG_RCR             0x0608
#define REG_TDECTRL         0x0208
#define REG_BCN_CTRL        0x0550
#define REG_BCNTCFG         0x0510
#define REG_BCN_INTERVAL    0x0554
#define REG_DUAL_TSF_RST    0x0553
#define REG_NAV_CTRL        0x0650
#define REG_RRSR            0x0440
#define REG_AGGLEN_LMT      0x0458
#define REG_AMPDU_MIN_SPACE 0x045C
#define REG_TXPAUSE         0x0522
#define REG_EDCA_VO_PARAM   0x0500
#define REG_EDCA_VI_PARAM   0x0504
#define REG_EDCA_BE_PARAM   0x0508
#define REG_EDCA_BK_PARAM   0x050C
#define REG_ACMHWCTRL       0x05C0
#define REG_TRXPTCL_CTL     0x0668
#define REG_MACID           0x0610
#define REG_BSSID           0x0618
#define REG_RESP_SIFS_OFDM  0x063E
#define REG_MAC_SPEC_SIFS   0x063A
#define REG_SPEC_SIFS       0x0428
#define REG_RL              0x042A
#define REG_INIRTS_RATE_SEL 0x0480
#define REG_FWHW_TXQ_CTRL   0x0420
#define REG_TSFTR           0x0560
#define REG_SIFS_CTX        0x0514
#define REG_SIFS_TRX        0x0516
#define REG_SLOT            0x051B
#define REG_RXFLTMAP2       0x06A4
#define REG_BCN_PSR_RPT     0x06A8
#define REG_9346CR          0x000A
#define REG_APS_FSMCO       0x0004
#define REG_RSV_CTRL        0x001C
#define REG_GPIO_MUXCFG     0x0040
#define REG_C2HEVT_CLEAR    0x01AF
#define REG_EARLY_MODE_CONTROL 0x04D0
#define REG_TX_RPT_CTRL     0x04EC
#define REG_TX_RPT_TIME     0x04F0
#define REG_TRXDMA_CTRL     0x010C
#define REG_RXTSF_OFFSET_OFDM 0x055F
#define REG_RXTSF_OFFSET_CCK  0x055E
#define REG_ATIMWND         0x055A
#define REG_BWOPMODE        0x0603
#define REG_SC_CNT          0x08c4
#define REG_SPS0_CTRL       0x0011
#define REG_CAMCMD          0x0670
#define REG_CAMWRITE        0x0674

#define IMR_DISABLED        0x00000000

#define RTL88EE_BAR_INDEX   2

#define SYS_CLK             0x8542
#define EFUSE_TEST_ALIAS    REG_EFUSE_TEST
#define EFUSE_CTRL_ALIAS    REG_EFUSE_CTRL


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
    uint32_t intr_status;
    uint32_t intr_mask;

    struct {
        uint32_t sys_iso_ctrl;
        uint32_t sys_func_en;
        uint32_t sys_clkr;
        uint32_t rcr;
        uint32_t tcr;
        uint32_t himr;
        uint32_t himre;
        uint32_t hsisr;
        uint32_t bcn_ctrl;
        uint32_t bcn_interval;
        uint32_t mac_rcr;
    } regs;

    uint32_t status_flags;
    uint32_t reset_state;
    uint8_t pm_state;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    uint32_t pending = s->intr_status & s->intr_mask;

    if (pending) {
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

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_SYS_ISO_CTRL:
        if (size == 2) {
            val = s->regs.sys_iso_ctrl & 0xFFFF;
        } else {
            val = s->regs.sys_iso_ctrl;
        }
        break;
    case REG_SYS_FUNC_EN:
        if (size == 2) {
            val = s->regs.sys_func_en & 0xFFFF;
        } else {
            val = s->regs.sys_func_en;
        }
        break;
    case REG_SYS_CLKR:
        if (size == 2) {
            val = s->regs.sys_clkr & 0xFFFF;
        } else {
            val = s->regs.sys_clkr;
        }
        break;
    case REG_RCR:
        if (size == 2) {
            val = s->regs.rcr & 0xFFFF;
        } else {
            val = s->regs.rcr;
        }
        break;
    case REG_TCR:
        if (size == 2) {
            val = s->regs.tcr & 0xFFFF;
        } else {
            val = s->regs.tcr;
        }
        break;
    case REG_HIMR:
        val = s->regs.himr;
        break;
    case REG_HIMRE:
        val = s->regs.himre;
        break;
    case REG_HSISR:
        val = s->regs.hsisr;
        break;
    case REG_BCN_CTRL:
        val = s->regs.bcn_ctrl;
        break;
    case REG_BCN_INTERVAL:
        if (size == 2) {
            val = s->regs.bcn_interval & 0xFFFF;
        } else {
            val = s->regs.bcn_interval;
        }
        break;
    case REG_EFUSE_ACCESS:
    case REG_EFUSE_CTRL:
    case REG_EFUSE_TEST:
    case REG_GPIO_OUTPUT:
    case REG_HSIMR:
    case REG_HISRE:
    case REG_PCIE_CTRL_REG:
    case REG_INT_MIG:
    case REG_BCNQ_DESA:
    case REG_HQ_DESA:
    case REG_MGQ_DESA:
    case REG_VOQ_DESA:
    case REG_VIQ_DESA:
    case REG_BEQ_DESA:
    case REG_BKQ_DESA:
    case REG_RX_DESA:
    case REG_PCIE_HRPWM:
    case REG_WATCH_DOG:
    case REG_TDECTRL:
    case REG_BCNTCFG:
    case REG_DUAL_TSF_RST:
    case REG_NAV_CTRL:
    case REG_RRSR:
    case REG_AGGLEN_LMT:
    case REG_AMPDU_MIN_SPACE:
    case REG_TXPAUSE:
    case REG_EDCA_VO_PARAM:
    case REG_EDCA_VI_PARAM:
    case REG_EDCA_BE_PARAM:
    case REG_EDCA_BK_PARAM:
    case REG_ACMHWCTRL:
    case REG_TRXPTCL_CTL:
    case REG_MACID:
    case REG_BSSID:
    case REG_RESP_SIFS_OFDM:
    case REG_MAC_SPEC_SIFS:
    case REG_SPEC_SIFS:
    case REG_RL:
    case REG_INIRTS_RATE_SEL:
    case REG_FWHW_TXQ_CTRL:
    case REG_TSFTR:
    case REG_SIFS_CTX:
    case REG_SIFS_TRX:
    case REG_SLOT:
    case REG_RXFLTMAP2:
    case REG_BCN_PSR_RPT:
    case REG_9346CR:
    case REG_APS_FSMCO:
    case REG_RSV_CTRL:
    case REG_GPIO_MUXCFG:
    case REG_C2HEVT_CLEAR:
    case REG_EARLY_MODE_CONTROL:
    case REG_TX_RPT_CTRL:
    case REG_TX_RPT_TIME:
    case REG_TRXDMA_CTRL:
    case REG_RXTSF_OFFSET_OFDM:
    case REG_RXTSF_OFFSET_CCK:
    case REG_ATIMWND:
    case REG_BWOPMODE:
    case REG_SC_CNT:
    case REG_SPS0_CTRL:
    case REG_CAMCMD:
    case REG_CAMWRITE:
        val = 0;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_SYS_ISO_CTRL:
        if (size == 2) {
            s->regs.sys_iso_ctrl = (s->regs.sys_iso_ctrl & ~0xFFFF) | (val & 0xFFFF);
        } else if (size == 4) {
            s->regs.sys_iso_ctrl = (uint32_t)val;
        }
        break;
    case REG_SYS_FUNC_EN:
        if (size == 2) {
            s->regs.sys_func_en = (s->regs.sys_func_en & ~0xFFFF) | (val & 0xFFFF);
        } else if (size == 4) {
            s->regs.sys_func_en = (uint32_t)val;
        }
        break;
    case REG_SYS_CLKR:
        if (size == 2) {
            s->regs.sys_clkr = (s->regs.sys_clkr & ~0xFFFF) | (val & 0xFFFF);
        } else if (size == 4) {
            s->regs.sys_clkr = (uint32_t)val;
        }
        break;
    case REG_RCR:
        if (size == 2) {
            s->regs.rcr = (s->regs.rcr & ~0xFFFF) | (val & 0xFFFF);
        } else if (size == 4) {
            s->regs.rcr = (uint32_t)val;
        }
        s->regs.mac_rcr = s->regs.rcr;
        break;
    case REG_TCR:
        if (size == 2) {
            s->regs.tcr = (s->regs.tcr & ~0xFFFF) | (val & 0xFFFF);
        } else if (size == 4) {
            s->regs.tcr = (uint32_t)val;
        }
        break;
    case REG_HIMR:
        s->regs.himr = (uint32_t)val;
        s->intr_mask = s->regs.himr;
        pcibase_update_irq(s);
        break;
    case REG_HIMRE:
        s->regs.himre = (uint32_t)val;
        break;
    case REG_HSISR:
        s->regs.hsisr &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case REG_BCN_CTRL:
        s->regs.bcn_ctrl = (uint32_t)val;
        break;
    case REG_BCN_INTERVAL:
        if (size == 2) {
            s->regs.bcn_interval = (s->regs.bcn_interval & ~0xFFFF) | (val & 0xFFFF);
        } else if (size == 4) {
            s->regs.bcn_interval = (uint32_t)val;
        }
        break;

    case REG_EFUSE_ACCESS:
    case REG_EFUSE_CTRL:
    case REG_EFUSE_TEST:
    case REG_GPIO_OUTPUT:
    case REG_HSIMR:
    case REG_HISRE:
    case REG_PCIE_CTRL_REG:
    case REG_INT_MIG:
    case REG_BCNQ_DESA:
    case REG_HQ_DESA:
    case REG_MGQ_DESA:
    case REG_VOQ_DESA:
    case REG_VIQ_DESA:
    case REG_BEQ_DESA:
    case REG_BKQ_DESA:
    case REG_RX_DESA:
    case REG_PCIE_HRPWM:
    case REG_WATCH_DOG:
    case REG_TDECTRL:
    case REG_BCNTCFG:
    case REG_DUAL_TSF_RST:
    case REG_NAV_CTRL:
    case REG_RRSR:
    case REG_AGGLEN_LMT:
    case REG_AMPDU_MIN_SPACE:
    case REG_TXPAUSE:
    case REG_EDCA_VO_PARAM:
    case REG_EDCA_VI_PARAM:
    case REG_EDCA_BE_PARAM:
    case REG_EDCA_BK_PARAM:
    case REG_ACMHWCTRL:
    case REG_TRXPTCL_CTL:
    case REG_MACID:
    case REG_BSSID:
    case REG_RESP_SIFS_OFDM:
    case REG_MAC_SPEC_SIFS:
    case REG_SPEC_SIFS:
    case REG_RL:
    case REG_INIRTS_RATE_SEL:
    case REG_FWHW_TXQ_CTRL:
    case REG_TSFTR:
    case REG_SIFS_CTX:
    case REG_SIFS_TRX:
    case REG_SLOT:
    case REG_RXFLTMAP2:
    case REG_BCN_PSR_RPT:
    case REG_9346CR:
    case REG_APS_FSMCO:
    case REG_RSV_CTRL:
    case REG_GPIO_MUXCFG:
    case REG_C2HEVT_CLEAR:
    case REG_EARLY_MODE_CONTROL:
    case REG_TX_RPT_CTRL:
    case REG_TX_RPT_TIME:
    case REG_TRXDMA_CTRL:
    case REG_RXTSF_OFFSET_OFDM:
    case REG_RXTSF_OFFSET_CCK:
    case REG_ATIMWND:
    case REG_BWOPMODE:
    case REG_SC_CNT:
    case REG_SPS0_CTRL:
    case REG_CAMCMD:
    case REG_CAMWRITE:
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    s->intr_status = 0;
    s->intr_mask = IMR_DISABLED;
    s->status_flags = 0;
    s->reset_state = 0;
    s->pm_state = 0;

    s->regs.sys_iso_ctrl = 0;
    s->regs.sys_func_en = 0;
    s->regs.sys_clkr = 0;
    s->regs.rcr = 0;
    s->regs.tcr = 0;
    s->regs.himr = 0;
    s->regs.himre = 0;
    s->regs.hsisr = 0;
    s->regs.bcn_ctrl = 0;
    s->regs.bcn_interval = 0;
    s->regs.mac_rcr = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    s->bar_info[0].index = RTL88EE_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "rtl8188ee-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    s->has_msix = false;
    if (s->has_msi) {
        Error *local_err = NULL;
        if (msi_init(pdev, 0, 1, true, false, &local_err) != 0) {
            error_propagate(errp, local_err);
        }
    }

    s->intr_status = 0;
    s->intr_mask = IMR_DISABLED;
    s->status_flags = 0;
    s->reset_state = 0;
    s->pm_state = 0;
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
    .name = "rtl8188ee_pci",
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
