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


#define TYPE_PCIBASE_DEVICE "mt76x0e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_MEDIATEK
#define PCI_VENDOR_ID_MEDIATEK 0x14c3
#endif

#define MT_ASIC_VERSION 0x0000
#define MT_COEXCFG0 0x0040
#define MT_EE_NIC_CONF_0_PA_IO_CURRENT (1 << 10)
#define MT_XO_CTRL7 0x011c
#define MT_MAX_LEN_CFG 0x1018
#define MAC_CSR0 0x1000

#define MT_WPDMA_GLO_CFG 0x0208
#define MT_INT_MASK_CSR 0x0204
#define MT_MAC_SYS_CTRL 0x1004
#define MT_MAC_STATUS 0x1200
#define MT_TXOP_CTRL_CFG 0x1340
#define MT_RXQ_STA 0x0430
#define MT_WPDMA_RST_IDX 0x020c
#define MT_TX_RING_BASE 0x0300
#define MT_RX_RING_BASE 0x03c0
#define MT_INT_TIMER_EN 0x112c
#define MT_INT_TIMER_CFG 0x1128
#define MT_WLAN_FUN_CTRL 0x0080
#define MT_CH_BUSY 0x1134
#define MT_MCU_INT_LEVEL 0x0718
#define MT_WMM_CWMIN 0x0218
#define MT_WMM_CWMAX 0x021c
#define MT_WMM_AIFSN 0x0214
#define MT_BEACON_TIME_CFG 0x1114
#define MT_RF_SETTING_0 0x050c
#define MT_RF_BYPASS_0 0x0504
#define MT_MCU_COM_REG0 0x0730
#define MT_TX_STA_0 0x170c
#define MT_TX_STA_1 0x1710
#define MT_TX_STA_2 0x1714
#define MT_RX_STAT_0 0x1700
#define MT_RX_STAT_1 0x1704
#define MT_RX_STAT_2 0x1708
#define MT_TX_STAT_FIFO 0x1718
#define MT_MCU_RESET_CTL 0x070C
#define MT_MCU_SEMAPHORE_00 0x07B0
#define MT_MCU_PCIE_REMAP_BASE4 0x074C
#define MT_EXT_CCA_CFG 0x141c
#define MT_WMM_CTRL 0x0230
#define MT_FCE_L2_STUFF 0x080c
#define MT_CMB_CTRL 0x0020
#define MT_TX_SW_CFG0 0x1330
#define MT_TXOP_HLDR_ET 0x1608
#define MT_TX_LINK_CFG 0x1350
#define MT_ED_CCA_TIMER 0x1140
#define MT_CH_IDLE 0x1130
#define MT_CH_TIME_CFG 0x110c
#define MT_XIFS_TIME_CFG 0x1100
#define MT_BKOFF_SLOT_CFG 0x1104
#define MT_TX_TIMEOUT_CFG 0x1348
#define MT_CCK_PROT_CFG 0x1364
#define MT_TX_RTS_CFG 0x1344
#define MT_OFDM_PROT_CFG 0x1368
#define MT_TX_PROT_CFG6 0x13e0
#define MT_AUTO_RSP_CFG 0x1404
#define MT_MAC_ADDR_DW1 0x100c
#define MT_MAC_ADDR_DW0 0x1008
#define MT_MAC_BSSID_DW1 0x1014
#define MT_MAC_BSSID_DW0 0x1010
#define MT_TX_BAND_CFG 0x132c
#define MT_TX0_RF_GAIN_CORR 0x13a0
#define MT_TX_ALC_VGA3 0x13c8
#define MT_TX0_RF_GAIN_ATTEN 0x13a8
#define MT_RF_MISC 0x0518
#define MT_TX_ALC_CFG_1 0x13b4
#define MT_TX_PIN_CFG 0x1328
#define MT_TX_STAT_FIFO_EXT 0x1798
#define MT_LED_CTRL 0x0770
#define MT_RF_CSR_CFG 0x0500
#define MT_EFUSE_CTRL 0x0024

#define MT_WLAN_FUN_CTRL_GPIO_OUT_EN	0xff000000
#define MT_WLAN_FUN_CTRL_FRC_WL_ANT_SEL	(1 << 5)
#define MT_WLAN_FUN_CTRL_WLAN_EN	(1 << 0)
#define MT_WLAN_FUN_CTRL_WLAN_RESET	(1 << 3)
#define MT_WLAN_FUN_CTRL_WLAN_RESET_RF	(1 << 2)
#define MT_WPDMA_GLO_CFG_DMA_BURST_SIZE	0x30
#define MT_WPDMA_GLO_CFG_BIG_ENDIAN	(1 << 7)
#define MT_WPDMA_GLO_CFG_HDR_SEG_LEN	0xff00
#define MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE	(1 << 6)
#define MT_WPDMA_GLO_CFG_TX_DMA_BUSY	(1 << 1)
#define MT_WPDMA_GLO_CFG_RX_DMA_BUSY	(1 << 3)
#define MT_WPDMA_GLO_CFG_TX_DMA_EN	(1 << 0)
#define MT_WPDMA_GLO_CFG_RX_DMA_EN	(1 << 2)
#define MT_MAC_STATUS_TX		(1 << 0)
#define MT_MAC_STATUS_RX		(1 << 1)
#define MT_RX_HEADROOM			32
#define MT76x02_TX_RING_SIZE	512
#define MT_TX_HW_QUEUE_MGMT		9
#define MT76x02_PSD_RING_SIZE	128
#define MT_TX_HW_QUEUE_MCU		8
#define MT_MCU_RING_SIZE	32
#define MT_INT_TX_DONE(_n)		(1 << ((_n) + 4))
#define MT_RX_BUF_SIZE		2048
#define MT76X02_RX_RING_SIZE		256
#define MT_RX_FILTR_CFG			0x1400
#define MT_INT_TIMER_CFG_PRE_TBTT	0xffff
#define MT_INT_TIMER_CFG_GP_TIMER	0xffff0000
#define MT_DFS_GP_INTERVAL		(10 << 4)
#define MT_INT_SOURCE_CSR		0x0200
#define MT_INT_RX_DONE_ALL		0x3
#define MT_INT_GPTIMER			(1 << 24)
#define MT_INT_TX_DONE_ALL		0x3ff0
#define MT_INT_TX_STAT			(1 << 22)
#define MT_INT_RX_DONE(_n)		(1 << (_n))
#define MT_INT_PRE_TBTT			(1 << 21)
#define MT_INT_TBTT			(1 << 20)

struct mt76x02_txwi {
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
};

struct mt76x02_rxwi {
    uint32_t rxinfo;
    uint32_t ctl;
    uint16_t tid_sn;
    uint16_t rate;
    uint8_t rssi[4];
    uint32_t bbp_rxinfo[4];
};

struct mt76x02_tx_status {
    uint8_t valid:1;
    uint8_t success:1;
    uint8_t aggr:1;
    uint8_t ack_req:1;
    uint8_t wcid;
    uint8_t pktid;
    uint8_t retry;
    uint16_t rate;
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
    uint32_t int_mask;
    uint32_t int_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x10000 / 4];

    /* DMA Context */
    uint32_t tx_ring_base;
    uint32_t rx_ring_base;
    uint32_t wpdma_glo_cfg;

    uint32_t mac_status;
    uint32_t mcu_reset_ctl;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->int_status & s->int_mask) != 0;

    if (msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        val = s->regs[addr / 4];
    }

    switch (addr) {
    case MT_ASIC_VERSION:
        val = 0x76100001;
        break;
    case MAC_CSR0:
        val = 1; /* Return non-zero and non-~0 to pass mt76x02_wait_for_mac */
        break;
    case MT_WPDMA_GLO_CFG:
        val = s->wpdma_glo_cfg;
        break;
    case MT_MAC_STATUS:
        val = s->mac_status;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->regs)) {
        s->regs[addr / 4] = val;
    }

    switch (addr) {
    case MT_INT_MASK_CSR:
        s->int_mask = val;
        pcibase_update_irq(s);
        break;
    case MT_WPDMA_GLO_CFG:
        s->wpdma_glo_cfg = val;
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

    memset(s->regs, 0, sizeof(s->regs));
    s->int_mask = 0;
    s->int_status = 0;
    s->wpdma_glo_cfg = 0;
    s->mac_status = 0;
    
    s->regs[MT_ASIC_VERSION / 4] = 0x76100001;
    s->regs[MAC_CSR0 / 4] = 1;
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x7610 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280 );
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
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "mt76x0e-mmio";  
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
    .name = "mt76x0e_pci",
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
