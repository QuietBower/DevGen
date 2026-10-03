/*
 * QEMU PCI Device Model for mt76x2e
 * Phase 2 - Functional Behavior (minimal for probe/bind)
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

#define TYPE_PCIBASE_DEVICE "mt76x2e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_MEDIATEK     0x14c3
#define PCIBASE_VENDOR_ID  PCI_VENDOR_ID_MEDIATEK
#define PCIBASE_DEVICE_ID  0x7662
#define PCIBASE_CLASS_ID   PCI_CLASS_NETWORK_OTHER

#define MT_ASIC_VERSION             0x0000
#define MT_WLAN_FUN_CTRL            0x0080
#define MT_COEXCFG0                 0x0040
#define MT_EFUSE_CTRL               0x0024
#define MT_EFUSE_DATA_BASE          0x0028
#define MT_XO_CTRL5                 0x0114
#define MT_XO_CTRL6                 0x0118
#define MT_XO_CTRL7                 0x011c
#define MT_PBF_SYS_CTRL             0x0400
#define MT_PBF_CFG                  0x0404
#define MT_PBF_TX_MAX_PCNT          0x0408
#define MT_PBF_RX_MAX_PCNT          0x040c
#define MT_BCN_OFFSET_BASE          0x041c
#define MT_RF_SETTING_0             0x050c
#define MT_FCE_L2_STUFF             0x080c
#define MT_FCE_WLAN_FLOW_CONTROL1   0x0824
#define MT_LED_CTRL                 0x0770
#define MT_LED_S0_BASE              0x077C
#define MT_LED_S1_BASE              0x0780
#define MT_RX_RING_BASE             0x03c0
#define MT_PBF_SYS_CTRL_MAC_RESET   BIT(2)
#define MT_PBF_SYS_CTRL_MCU_RESET   BIT(0)
#define MT_PBF_SYS_CTRL_ASY_RESET   BIT(4)
#define MT_PBF_SYS_CTRL_DMA_RESET   BIT(1)
#define MT_PBF_SYS_CTRL_PBF_RESET   BIT(3)
#define MT_WLAN_FUN_CTRL_WLAN_EN        BIT(0)
#define MT_WLAN_FUN_CTRL_WLAN_CLK_EN    BIT(1)
#define MT_WLAN_FUN_CTRL_WLAN_RESET_RF  BIT(2)
#define MT_WLAN_FUN_CTRL_FRC_WL_ANT_SEL BIT(5)
#define MT_COEXCFG0_COEX_EN         BIT(0)
#define MT_INT_MASK_CSR             0x0204
#define MT_WPDMA_GLO_CFG            0x0208
#define MT_WPDMA_RST_IDX            0x020c
#define MT_WPDMA_DELAY_INT_CFG      0x0210
#define MT_HEADER_TRANS_CTRL_REG    0x0260
#define MT_TSO_CTRL                 0x0250
#define MT_MAC_ADDR_DW0             0x1008
#define MT_MAC_ADDR_DW1             0x100c
#define MT_MAC_BSSID_DW0            0x1010
#define MT_MAC_BSSID_DW1            0x1014
#define MT_MAX_LEN_CFG              0x1018
#define MT_MAC_SYS_CTRL             0x1004
#define MT_MAC_STATUS               0x1200
#define MT_CH_TIME_CFG              0x110c
#define MT_CH_IDLE                  0x1130
#define MT_CH_BUSY                  0x1134
#define MT_INT_TIMER_CFG            0x1128
#define MT_INT_TIMER_EN             0x112c
#define MT_INT_TIMER_EN_GP_TIMER_EN BIT(1)
#define MT_INT_TIMER_EN_PRE_TBTT_EN BIT(0)
#define MT_INT_MASK_RX_DONE_ALL     MT_INT_RX_DONE_ALL
#define MT_INT_MASK_TX_DONE_ALL     MT_INT_TX_DONE_ALL
#define MT_INT_TIMER_EN_MASK        (MT_INT_TIMER_EN_GP_TIMER_EN | \
                                     MT_INT_TIMER_EN_PRE_TBTT_EN)
#define MT_INT_RX_DONE_ALL          GENMASK(1, 0)
#define MT_INT_TX_DONE_ALL          GENMASK(13, 4)
#define MT_INT_TX_STAT              BIT(22)
#define MT_INT_GPTIMER              BIT(24)
#define MT_INT_PRE_TBTT             BIT(21)
#define MT_INT_TBTT                 BIT(20)
#define MT_INT_RX_DONE(_n)          BIT(_n)
#define MT_INT_TX_DONE(_n)          BIT((_n) + 4)
#define MT_RX_FILTR_CFG             0x1400
#define MT_AUTO_RSP_CFG             0x1404
#define MT_LEGACY_BASIC_RATE        0x1408
#define MT_HT_BASIC_RATE            0x140c
#define MT_BKOFF_SLOT_CFG           0x1104
#define MT_XIFS_TIME_CFG            0x1100
#define MT_TX_LINK_CFG              0x1350
#define MT_TX_SW_CFG0               0x1330
#define MT_TX_SW_CFG1               0x1334
#define MT_TX_SW_CFG2               0x1338
#define MT_TX_SW_CFG3               0x1478
#define MT_TX_TIMEOUT_CFG           0x1348
#define MT_TX_RTS_CFG               0x1344
#define MT_TX_RETRY_CFG             0x134c
#define MT_TX_BAND_CFG              0x132c
#define MT_TX_PIN_CFG               0x1328
#define MT_TX_PWR_CFG_0             0x1314
#define MT_TX_PWR_CFG_1             0x1318
#define MT_TX_PWR_CFG_2             0x131c
#define MT_TX_PWR_CFG_3             0x1320
#define MT_TX_PWR_CFG_4             0x1324
#define MT_TX_PWR_CFG_7             0x13d4
#define MT_TX_PWR_CFG_8             0x13d8
#define MT_TX_PWR_CFG_9             0x13dc
#define MT_TX_ALC_CFG_0             0x13b0
#define MT_TX_ALC_CFG_1             0x13b4
#define MT_TX_ALC_CFG_2             0x13a8
#define MT_TX_ALC_CFG_3             0x13ac
#define MT_TX_ALC_CFG_4             0x13c0
#define MT_TX_ALC_VGA3              0x13c8
#define MT_TX0_RF_GAIN_CORR         0x13a0
#define MT_TX1_RF_GAIN_CORR         0x13a4
#define MT_BB_PA_MODE_CFG0          0x1214
#define MT_BB_PA_MODE_CFG1          0x1218
#define MT_RF_PA_MODE_CFG0          0x121c
#define MT_RF_PA_MODE_CFG1          0x1220
#define MT_RF_PA_MODE_ADJ0          0x1228
#define MT_RF_PA_MODE_ADJ1          0x122c
#define MT_AUX_CLK_CFG              0x120c
#define MT_PWR_PIN_CFG              0x1204
#define MT_DACCLK_EN_DLY_CFG        0x1264
#define MT_TBTT_SYNC_CFG            0x1118
#define MT_BEACON_TIME_CFG          0x1114
#define MT_BEACON_TIME_CFG_TBTT_EN  BIT(19)
#define MT_BEACON_TIME_CFG_TIMER_EN BIT(16)
#define MT_BEACON_TIME_CFG_BEACON_TX BIT(20)
#define MT_BEACON_TIME_CFG_SYNC_MODE GENMASK(18, 17)
#define MT_BCN_BYPASS_MASK          0x108c
#define MT_MAC_APC_BSSID_BASE       0x1090
#define MT_MAC_APC_BSSID_L(_n)      (MT_MAC_APC_BSSID_BASE + ((_n) * 8))
#define MT_MAC_APC_BSSID_H(_n)      (MT_MAC_APC_BSSID_BASE + ((_n) * 8 + 4))
#define MT_MAC_APC_BSSID_H_ADDR     GENMASK(15, 0)
#define MT_WCID_ADDR_BASE           0x1800
#define MT_WCID_ATTR_BASE           0xa800
#define MT_WCID_TX_RATE_BASE        0x1c00
#define MT_WCID_ADDR(_n)            (MT_WCID_ADDR_BASE + (_n) * 8)
#define MT_WCID_ATTR(_n)            (MT_WCID_ATTR_BASE + (_n) * 4)
#define MT_WCID_DROP_BASE           0x106c
#define MT_WCID_DROP(_n)            (MT_WCID_DROP_BASE + ((_n) >> 5) * 4)
#define MT_WCID_DROP_MASK(_n)       BIT((_n) % 32)
#define MT_WPDMA_GLO_CFG_TX_DMA_EN  BIT(0)
#define MT_WPDMA_GLO_CFG_RX_DMA_EN  BIT(2)
#define MT_WPDMA_GLO_CFG_TX_DMA_BUSY BIT(1)
#define MT_WPDMA_GLO_CFG_RX_DMA_BUSY BIT(3)
#define MT_WPDMA_GLO_CFG_DMA_BURST_SIZE GENMASK(5, 4)
#define MT_WPDMA_GLO_CFG_HDR_SEG_LEN GENMASK(15, 8)
#define MT_WPDMA_GLO_CFG_BIG_ENDIAN BIT(7)
#define MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE BIT(6)
#define MT_FCE_PSE_CTRL            0x0800
#define MT_FCE_L2_STUFF_WR_MPDU_LEN_EN BIT(4)
#define MT_FCE_WLAN_FLOW_CONTROL1  0x0824
#define MT_TX_STAT_FIFO            0x1718
#define MT_TX_STAT_FIFO_EXT        0x1798
#define MT_TX_STA_0                0x170c
#define MT_TX_STA_1                0x1710
#define MT_TX_STA_2                0x1714
#define MT_RX_STAT_0               0x1700
#define MT_RX_STAT_1               0x1704
#define MT_RX_STAT_2               0x1708

#define MT_WLAN_MTC_CTRL           0x10148
#define MT_WLAN_MTC_CTRL_MTCMOS_PWR_UP BIT(0)
#define MT_WLAN_MTC_CTRL_PWR_ACK   BIT(12)
#define MT_WLAN_MTC_CTRL_PWR_ACK_S BIT(13)
#define MT_WLAN_MTC_CTRL_STATE_UP  BIT(28)

#define MT_RX_HEADROOM             32
#define MT_RX_BUF_SIZE             2048
#define MT76X02_RX_RING_SIZE       256
#define MT_MCU_RING_SIZE           32
#define MT_RING_SIZE               0x10

#define MT_WPDMA_RING_DESC_SIZE    16

#define MT7662_EEPROM_SIZE         512
#define MT7662_ROM_PATCH           "mt7662_rom_patch.bin"
#define MT7662_FIRMWARE            "mt7662.bin"

#define MT_RX_RING_BASE            0x03c0

#define MT_LED_S0(_n)              (MT_LED_S0_BASE + 8 * (_n))
#define MT_LED_S1(_n)              (MT_LED_S1_BASE + 8 * (_n))

#define MT_RX_STAT_0               0x1700
#define MT_RX_STAT_1               0x1704
#define MT_RX_STAT_2               0x1708

#define MT_TX_STAT_FIFO_SUCCESS    BIT(5)
#define MT_TX_STAT_FIFO_RATE       GENMASK(31, 16)
#define MT_TX_STAT_FIFO_EXT_PKTID  GENMASK(15, 8)
#define MT_TX_STAT_FIFO_VALID      BIT(0)
#define MT_TX_STAT_FIFO_AGGR       BIT(6)
#define MT_TX_STAT_FIFO_ACKREQ     BIT(7)
#define MT_TX_STAT_FIFO_WCID       GENMASK(15, 8)
#define MT_TX_STAT_FIFO_EXT_RETRY  GENMASK(7, 0)

#define MT_INT_MASK_CSR            0x0204

#define MT_RXMK_BAR0_SIZE          0x10000

#define MT_PCI_BAR0_INDEX          0


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
        uint32_t asic_version;
        uint32_t wlan_fun_ctrl;
        uint32_t coexcfg0;
        uint32_t mac_sys_ctrl;
        uint32_t mac_status;
        uint32_t wpdma_glo_cfg;
        uint32_t int_mask_csr;
        uint32_t int_status;
        uint32_t rx_filtr_cfg;
        uint32_t auto_rsp_cfg;
        uint32_t legacy_basic_rate;
        uint32_t ht_basic_rate;
    } regs;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    uint32_t pending = s->intr_status & s->intr_mask;

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else if (!msix_enabled(pdev)) {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case MT_ASIC_VERSION:
        val = s->regs.asic_version;
        break;
    case MT_WLAN_FUN_CTRL:
        val = s->regs.wlan_fun_ctrl;
        break;
    case MT_COEXCFG0:
        val = s->regs.coexcfg0;
        break;
    case MT_MAC_SYS_CTRL:
        val = s->regs.mac_sys_ctrl;
        break;
    case MT_MAC_STATUS:
        val = s->regs.mac_status;
        break;
    case MT_WPDMA_GLO_CFG:
        val = s->regs.wpdma_glo_cfg;
        break;
    case MT_INT_MASK_CSR:
        val = s->regs.int_mask_csr;
        break;
    case MT_RX_FILTR_CFG:
        val = s->regs.rx_filtr_cfg;
        break;
    case MT_AUTO_RSP_CFG:
        val = s->regs.auto_rsp_cfg;
        break;
    case MT_LEGACY_BASIC_RATE:
        val = s->regs.legacy_basic_rate;
        break;
    case MT_HT_BASIC_RATE:
        val = s->regs.ht_basic_rate;
        break;
    case MT_RX_STAT_0:
    case MT_RX_STAT_1:
    case MT_RX_STAT_2:
    case MT_TX_STA_0:
    case MT_TX_STA_1:
    case MT_TX_STA_2:
    case MT_TX_STAT_FIFO:
    case MT_TX_STAT_FIFO_EXT:
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
    case MT_WLAN_FUN_CTRL:
        s->regs.wlan_fun_ctrl = (uint32_t)val;
        break;
    case MT_COEXCFG0:
        s->regs.coexcfg0 = (uint32_t)val;
        break;
    case MT_MAC_SYS_CTRL:
        s->regs.mac_sys_ctrl = (uint32_t)val;
        break;
    case MT_WPDMA_GLO_CFG:
        s->regs.wpdma_glo_cfg = (uint32_t)val;
        break;
    case MT_INT_MASK_CSR:
        s->regs.int_mask_csr = (uint32_t)val;
        s->intr_mask = s->regs.int_mask_csr;
        pcibase_update_irq(s);
        break;
    case MT_RX_FILTR_CFG:
        s->regs.rx_filtr_cfg = (uint32_t)val;
        break;
    case MT_AUTO_RSP_CFG:
        s->regs.auto_rsp_cfg = (uint32_t)val;
        break;
    case MT_LEGACY_BASIC_RATE:
        s->regs.legacy_basic_rate = (uint32_t)val;
        break;
    case MT_HT_BASIC_RATE:
        s->regs.ht_basic_rate = (uint32_t)val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    uint64_t val = 0;

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

    s->regs.asic_version = 0x76620000;
    s->regs.wlan_fun_ctrl = 0;
    s->regs.coexcfg0 = 0;
    s->regs.mac_sys_ctrl = 0;
    s->regs.mac_status = 0x1;
    s->regs.wpdma_glo_cfg = 0;
    s->regs.int_mask_csr = 0;
    s->regs.int_status = 0;
    s->regs.rx_filtr_cfg = 0;
    s->regs.auto_rsp_cfg = 0;
    s->regs.legacy_basic_rate = 0;
    s->regs.ht_basic_rate = 0;

    s->intr_status = 0;
    s->intr_mask = 0;

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = MT_PCI_BAR0_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = MT_RXMK_BAR0_SIZE;
    s->bar_info[0].name = "mt76x2e-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
    }
    s->has_msix = false;

    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "mt76x2e_pci",
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
