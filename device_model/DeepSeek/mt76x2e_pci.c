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

#define TYPE_PCIBASE_DEVICE "mt76x2e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_MEDIATEK  0x14c3
#define MT76X2E_DEVICE_ID       0x7662
#define MT76X2E_CLASS_ID        0x0280  /* PCI_CLASS_NETWORK_OTHER */

#define MT_ASIC_VERSION         0x0000
#define MT_INT_STATUS_CSR       0x0200
#define MT_INT_MASK_CSR         0x0204
#define MT_CH_BUSY              0x1134
#define MT_WLAN_FUN_CTRL        0x0080
#define MT_WLAN_MTC_CTRL        0x10148
#define MT_COEXCFG0             0x0040
#define MT_FCE_L2_STUFF         0x080c
#define MT_WPDMA_GLO_CFG        0x0208
#define MT_WPDMA_RST_IDX        0x020c
#define MT_TX_RING_BASE         0x0300
#define MT_RX_RING_BASE         0x03c0
#define MT_WCID_ADDR_BASE       0x1800
#define MT_WCID_ATTR_BASE       0xa800
#define MT_WCID_DROP_BASE       0x106c
#define MT_WCID_TX_RATE_BASE    0x1c00
#define MT_TX_STAT_FIFO         0x1718
#define MT_TX_STAT_FIFO_EXT     0x1798
#define MT_TX_STA_0             0x170c
#define MT_TX_STA_1             0x1710
#define MT_RX_STAT_0            0x1700
#define MT_RX_STAT_1            0x1704
#define MT_RX_STAT_2            0x1708
#define MT_TX_STA_2             0x1714
#define MT_TX_AGG_CNT_BASE0     0x1720
#define MT_TX_AGG_CNT_BASE1     0x174c
#define MT_BCN_OFFSET_BASE      0x041c
#define MT_EFUSE_CTRL           0x0024
#define MT_EFUSE_DATA_BASE      0x0028
#define MT_SKEY_MODE_BASE_0     0xb000
#define MT_SKEY_MODE_BASE_1     0xb3f0
#define MT_SKEY_BASE_0          0xac00
#define MT_SKEY_BASE_1          0xb400
#define MT_LED_CTRL             0x0770
#define MT_LED_S0_BASE          0x077C
#define MT_LED_S1_BASE          0x0780
#define MT_MAC_APC_BSSID_BASE   0x1090
#define MT_BEACON_TIME_CFG      0x1114
#define MT_TBTT_SYNC_CFG        0x1118
#define MT_BCN_BYPASS_MASK      0x108c
#define MT_MAC_SYS_CTRL         0x1004
#define MT_MAC_ADDR_DW0         0x1008
#define MT_MAC_ADDR_DW1         0x100c
#define MT_MAC_BSSID_DW0        0x1010
#define MT_MAC_BSSID_DW1        0x1014
#define MT_MAX_LEN_CFG          0x1018
#define MT_AMPDU_MAX_LEN_20M1S  0x1030
#define MT_AMPDU_MAX_LEN_20M2S  0x1034
#define MT_CH_TIME_CFG          0x110c
#define MT_CH_IDLE              0x1130
#define MT_CH_BUSY              0x1134
#define MT_ED_CCA_TIMER         0x1140
#define MT_INT_TIMER_CFG        0x1128
#define MT_INT_TIMER_EN         0x112c
#define MT_MAC_STATUS           0x1200
#define MT_PWR_PIN_CFG          0x1204
#define MT_AUX_CLK_CFG          0x120c
#define MT_BB_PA_MODE_CFG0      0x1214
#define MT_BB_PA_MODE_CFG1      0x1218
#define MT_RF_PA_MODE_CFG0      0x121c
#define MT_RF_PA_MODE_CFG1      0x1220
#define MT_RF_PA_MODE_ADJ0      0x1228
#define MT_RF_PA_MODE_ADJ1      0x122c
#define MT_DACCLK_EN_DLY_CFG    0x1264
#define MT_XIFS_TIME_CFG        0x1100
#define MT_BKOFF_SLOT_CFG       0x1104
#define MT_TX_PWR_CFG_0         0x1314
#define MT_TX_PWR_CFG_1         0x1318
#define MT_TX_PWR_CFG_2         0x131c
#define MT_TX_PWR_CFG_3         0x1320
#define MT_TX_PWR_CFG_4         0x1324
#define MT_TX_PIN_CFG           0x1328
#define MT_TX_BAND_CFG          0x132c
#define MT_TX_SW_CFG0           0x1330
#define MT_TX_SW_CFG1           0x1334
#define MT_TX_SW_CFG2           0x1338
#define MT_TX_SW_CFG3           0x1478
#define MT_TXOP_CTRL_CFG        0x1340
#define MT_TX_RTS_CFG           0x1344
#define MT_TX_TIMEOUT_CFG       0x1348
#define MT_TX_RETRY_CFG         0x134c
#define MT_TX_LINK_CFG          0x1350
#define MT_VHT_HT_FBK_CFG1      0x1358
#define MT_CCK_PROT_CFG         0x1364
#define MT_OFDM_PROT_CFG        0x1368
#define MT_MM20_PROT_CFG        0x136c
#define MT_MM40_PROT_CFG        0x1370
#define MT_GF20_PROT_CFG        0x1374
#define MT_GF40_PROT_CFG        0x1378
#define MT_TX_PROT_CFG6         0x13e0
#define MT_TX_PROT_CFG7         0x13e4
#define MT_TX_PROT_CFG8         0x13e8
#define MT_PIFS_TX_CFG          0x13ec
#define MT_TX_ALC_CFG_0         0x13b0
#define MT_TX_ALC_CFG_1         0x13b4
#define MT_TX_ALC_CFG_2         0x13a8
#define MT_TX_ALC_CFG_3         0x13ac
#define MT_TX_ALC_CFG_4         0x13c0
#define MT_TX0_RF_GAIN_CORR     0x13a0
#define MT_TX1_RF_GAIN_CORR     0x13a4
#define MT_TX_ALC_VGA3          0x13c8
#define MT_TX_PWR_CFG_7         0x13d4
#define MT_TX_PWR_CFG_8         0x13d8
#define MT_TX_PWR_CFG_9         0x13dc
#define MT_HT_FBK_TO_LEGACY     0x1384
#define MT_HT_CTRL_CFG          0x1410
#define MT_HT_BASIC_RATE        0x140c
#define MT_LEGACY_BASIC_RATE    0x1408
#define MT_RX_FILTR_CFG         0x1400
#define MT_AUTO_RSP_CFG         0x1404
#define MT_EXT_CCA_CFG          0x141c
#define MT_PROT_AUTO_TX_CFG     0x1648
#define MT_TXOP_HLDR_ET         0x1608
#define MT_EXP_ACK_TIME         0x1380
#define MT_PN_PAD_MODE          0x150c
#define MT_HEADER_TRANS_CTRL_REG 0x0260
#define MT_TSO_CTRL             0x0250
#define MT_WPDMA_DELAY_INT_CFG  0x0210
#define MT_PBF_CFG              0x0404
#define MT_PBF_SYS_CTRL         0x0400
#define MT_PBF_TX_MAX_PCNT      0x0408
#define MT_PBF_RX_MAX_PCNT      0x040c
#define MT_FCE_PSE_CTRL         0x0800
#define MT_FCE_WLAN_FLOW_CONTROL1 0x0824
#define MT_PAUSE_ENABLE_CONTROL1 0x0a38
#define MT_XO_CTRL5             0x0114
#define MT_XO_CTRL6             0x0118
#define MT_XO_CTRL7             0x011c
#define MT_RF_BYPASS_0          0x0504
#define MT_RF_SETTING_0         0x050c

#define BAR0_SIZE (128 * 1024)

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

    /* MMIO register storage */
    uint32_t mmio_regs[BAR0_SIZE / 4];
};

/* Hardware descriptor structures from driver */
typedef struct {
    uint32_t buf0;
    uint32_t ctrl;
    uint32_t buf1;
    uint32_t info;
} __attribute__((packed)) mt76_desc;

typedef struct {
    uint32_t desc_base;
    uint32_t ring_size;
    uint32_t cpu_idx;
    uint32_t dma_idx;
} __attribute__((packed)) mt76_queue_regs;

typedef struct {
    uint16_t flags;
    uint16_t rate;
    uint8_t ack_ctl;
    uint8_t wcid;
    uint16_t len_ctl;
    uint32_t iv;
    uint32_t eiv;
    uint8_t aid;
    uint8_t txstream;
    uint8_t ctl2;
    uint8_t pktid;
} __attribute__((packed)) mt76x02_txwi;

typedef struct {
    uint32_t rxinfo;
    uint32_t ctl;
    uint16_t tid_sn;
    uint16_t rate;
    uint8_t rssi[4];
    uint32_t bbp_rxinfo[4];
} __attribute__((packed)) mt76x02_rxwi;

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & ~s->intr_mask;

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

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4 || (addr & 3)) {
        return 0;
    }

    switch (addr) {
    case MT_ASIC_VERSION:
        return 0x76620001;
    case MT_INT_STATUS_CSR:
        return s->intr_status;
    case MT_INT_MASK_CSR:
        return s->intr_mask;
    case MT_EFUSE_CTRL:
        /* EFUSE control always reports idle (not busy) */
        return 0;
    case 0x1000: /* MAC_CSR0 */
        /* Always report MAC ready to prevent polling timeouts */
        return 1;
    default:
        if (addr < BAR0_SIZE) {
            val = s->mmio_regs[addr >> 2];
        }
        return val;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 || (addr & 3)) {
        return;
    }

    switch (addr) {
    case MT_INT_STATUS_CSR:
        /* W1C */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case MT_INT_MASK_CSR:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case 0x1000: /* MAC_CSR0 */
        s->mmio_regs[addr >> 2] = val;
        break;
    default:
        if (addr < BAR0_SIZE) {
            s->mmio_regs[addr >> 2] = val;
        }
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
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    /* MAC_CSR0: set to non-zero to satisfy mt76x02_wait_for_mac() */
    s->mmio_regs[0x1000 >> 2] = 0x00000001;
    /* Additional registers to simulate firmware loaded and CPU running */
    s->mmio_regs[MT_WLAN_FUN_CTRL >> 2] = 0x00000001;   /* WLAN function enabled */
    s->mmio_regs[MT_MAC_SYS_CTRL >> 2] = 0x00000001;    /* CPU running, ready */
    /* EFUSE initialization: provide plausible eeprom header and MAC */
    s->mmio_regs[MT_EFUSE_CTRL >> 2] = 0x00000000;      /* idle */
    s->mmio_regs[MT_EFUSE_DATA_BASE >> 2] = 0x00007662; /* eeprom magic 0x7662 (little-endian) */
    s->mmio_regs[(MT_EFUSE_DATA_BASE + 4) >> 2] = 0x00000002; /* MAC byte 0-3 */
    s->mmio_regs[(MT_EFUSE_DATA_BASE + 8) >> 2] = 0x00000100; /* MAC byte 4-7 */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MEDIATEK );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  MT76X2E_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, MT76X2E_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_MEDIATEK);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, MT76X2E_DEVICE_ID);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "mt76x2e-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
