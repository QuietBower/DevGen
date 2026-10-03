/*
 * QEMU PCI device model for MediaTek mt7615e (minimal emulation for driver probe)
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

/* Note: linux/pci_regs.h is a kernel header and not available in QEMU build.
 * The include from the template is removed to keep this file self-contained. */

#define TYPE_PCIBASE_DEVICE "mt7615e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x14c3
#define PCIBASE_DEVICE_ID 0x7615
#define PCIBASE_CLASS_ID  0x0280

#define MT_HIF2_BASE                 0xf0000

#define MT_PDMA_BUSY                 0x82000504
#define MT_PDMA_BUSY_STATUS          0x00000168
#define MT_PDMA_BUSY_IDX             (1u << 31)
#define MT_PDMA_TX_BUSY              (1u << 0)
#define MT_PDMA_RX_BUSY              (1u << 1)
#define MT_PDMA_TX_IDX_BUSY          (1u << 2)

#define MT_PDMA_SLP_PROT             0x00000154
#define MT_PDMA_AXI_SLPPROT_RDY      (1u << 16)
#define MT_PDMA_AXI_SLPPROT_ENABLE   (1u << 0)

#define MT_PCIE_IRQ_ENABLE           (MT_HIF2_BASE + 0x188)
#define MT_MCU2HOST_INT_ENABLE       0x000001f4

#define MT_WPDMA_TX_RING0_CTRL0      0x00000300
#define MT_WPDMA_TX_RING0_CTRL1      0x00000304
#define MT_WPDMA_GLO_CFG1            0x00000500
#define MT_WPDMA_TX_PRE_CFG          0x00000510
#define MT_WPDMA_RX_PRE_CFG          0x00000520
#define MT_WPDMA_ABT_CFG             0x00000530
#define MT_WPDMA_ABT_CFG1            0x00000534
#define MT_DELAY_INT_CFG             0x00000210

#define MT_WPDMA_GLO_CFG_FIFO_LITTLE_ENDIAN    (1u << 12)
#define MT_WPDMA_GLO_CFG_MULTI_DMA_EN          (0x3u << 10)
#define MT_WPDMA_GLO_CFG_TX_BT_SIZE_BIT0       (1u << 9)
#define MT_WPDMA_GLO_CFG_TX_BT_SIZE_BIT21      (0x3u << 22)
#define MT_WPDMA_GLO_CFG_FIRST_TOKEN_ONLY      (1u << 26)
#define MT_WPDMA_GLO_CFG_OMIT_TX_INFO          (1u << 28)

#define MT_MCU_PCIE_REMAP_2_BASE     (0xffffu << 19)
#define MT_MCU_PCIE_REMAP_2_OFFSET   (0x7ffffu)
#define MT7663_MCU_PCIE_REMAP_2_BASE (0xffffu << 16)
#define MT7663_MCU_PCIE_REMAP_2_OFFSET (0xffffu)

#define MT_MCU_CIRQ_BASE             0x000c0000
#define MT_MCU_CIRQ_IRQ_SEL(n)       (MT_MCU_CIRQ_BASE + ((n) << 2))

#define MT_DMASHDL_BASE              0x5000a000
#define MT_DMASHDL_SCHED_SET0        0x000000b0
#define MT_DMASHDL_SCHED_SET1        0x000000b4

#define MT_LED_BASE_PHYS             0x80024000
#define MT_LED_STATUS_0(_n)          (MT_LED_BASE_PHYS + 0x10 + ((_n) * 8))
#define MT_LED_STATUS_1(_n)          (MT_LED_BASE_PHYS + 0x14 + ((_n) * 8))
#define MT_LED_CTRL_BAND(_n)         (1u << (4 + (8 * (_n))))

#define MT_EFUSE_BASE_CTRL           0x00000000
#define MT_EFUSE_BASE_CTRL_EMPTY     (1u << 30)
#define MT_EFUSE_RDATA(_i)           (0x030 + ((_i) * 4))
#define MT7615_EEPROM_SIZE           1024
#define MT7663_EEPROM_SIZE           1536

#define MT_WTBL_ENTRY_SIZE           256
#define MT7615_WTBL_SIZE             128
#define MT7663_WTBL_SIZE             32

#define MT7615_MAX_INTERFACES        16
#define MT7615_WTBL_RESERVED         (MT7615_WTBL_SIZE - MT7615_MAX_INTERFACES - 1)
#define MT7615_TOKEN_SIZE            4096
#define MT7615_MAX_WMM_SETS          4

#define MT7615_TX_RING_SIZE          1024
#define MT7615_RX_RING_SIZE          1024
#define MT7615_TX_MCU_RING_SIZE      128
#define MT7615_RX_MCU_RING_SIZE      512
#define MT7615_TX_FWDL_RING_SIZE     128
#define MT7615_TX_MGMT_RING_SIZE     128

#define MT_TXD_SIZE                  (8 * 4)
#define MT_USB_TXD_SIZE              (MT_TXD_SIZE + 8 * 4)
#define MT_TXP_MAX_BUF_NUM           6
#define MT_HW_TXP_MAX_MSDU_NUM       4
#define MT_HW_TXP_MAX_BUF_NUM        4
#define MT_RX_BUF_SIZE               2048

#define MT7615_PM_TIMEOUT            (HZ / 12)
#define MT7615_HW_SCAN_TIMEOUT       (HZ / 10)
#define MT76_CONNAC_COREDUMP_TIMEOUT (HZ / 20)
#define MT_TX_STATUS_SKB_TIMEOUT     (HZ / 4)

#define MT7615_CFEND_RATE_DEFAULT    0x49
#define MT7615_CFEND_RATE_11B        0x03
#define MT7615_RATE_RETRY            2

#define MT_QFLAG_WED_RING            (0x3u)
#define MT_QFLAG_WED                 (1u << 5)
#define MT_QFLAG_WED_RRO             (1u << 6)
#define MT_QFLAG_WED_RRO_EN          (1u << 7)
#define MT_QFLAG_EMI_EN              (1u << 8)
#define MT_QFLAG_NPU                 (1u << 9)
#define MT_QFLAG_WED_TYPE            (0x7u << 2)

#define MT_SKU_POWER_LIMIT           161

#define MT_DRV_TXWI_NO_FREE          (1u << 0)
#define MT_DRV_TX_ALIGNED4_SKBS      (1u << 1)
#define MT_DRV_SW_RX_AIRTIME         (1u << 2)
#define MT_DRV_HW_MGMT_TXQ           (1u << 4)
#define MT_DRV_AMSDU_OFFLOAD         (1u << 5)

#define MT_TX_CB_DMA_DONE            (1u << 0)
#define MT_TX_CB_TXS_DONE            (1u << 1)
#define MT_TX_CB_TXS_FAILED          (1u << 2)

#define MT_TXQ_FREE_THR              32
#define MT_MAX_NON_AQL_PKT           16

#define MT_PACKET_ID_NO_ACK          0
#define MT_PACKET_ID_NO_SKB          1
#define MT_PACKET_ID_WED             2
#define MT_PACKET_ID_FIRST           3
#define MT_PACKET_ID_MASK            (0x7fu)

#define MT_MSDU_ID_VALID             (1u << 15)
#define MT_TXD_LEN_MASK              (0xfffu)
#define MT_TXD_LEN_LAST              (1u << 15)
#define MT_TXD_LEN_MSDU_LAST         (1u << 14)
#define MT_TXD_LEN_AMSDU_LAST        (1u << 15)

#define MT_WCID_TX_INFO_SET          (1u << 31)

#define MT_WPDMA_GLO_CFG_SW_RESET    (1u << 24)

#define MT_TOP_STRAP_STA             0x00000010
#define MT_TOP_3NSS                  (1u << 24)

#define MT_WF_PHY_BASE               0x82070000
#define MT_WF_PHY_RXTD_BASE          (MT_WF_PHY_BASE + 0x2200)
#define MT_WF_PHY_RXTD2_BASE         (MT_WF_PHY_BASE + 0x2a00)
#define MT_WF_PHY_RXTD(_n)           (MT_WF_PHY_RXTD_BASE + ((_n) << 2))
#define MT_WF_PHY_RXTD2(_n)          (MT_WF_PHY_RXTD2_BASE + ((_n) << 2))

#define MT_WF_PHY_RFINTF3_0(_n)      (MT_WF_PHY_BASE + 0x1100 + (_n) * 0x400)
#define MT_WF_PHY_RFINTF3_0_ANT      (0xf0u)

#define MT_WF_PHY_MIN_PRI_PWR(_phy)  (MT_WF_PHY_BASE + ((_phy) ? 0x084 : 0x229c))
#define MT7663_WF_PHY_MIN_PRI_PWR(_phy) (MT_WF_PHY_BASE + ((_phy) ? 0x2aec : 0x22f0))

#define MT_WF_PHY_RXTD_CCK_PD(_phy)      (MT_WF_PHY_BASE + ((_phy) ? 0x2314 : 0x2310))
#define MT7663_WF_PHY_RXTD_CCK_PD(_phy)  (MT_WF_PHY_BASE + ((_phy) ? 0x2350 : 0x234c))

#define MT_WF_PHY_PD_OFDM_MASK(_phy) (( _phy ) ? (0x1ffu << 16) : (0x1ffu << 20))
#define MT_WF_PHY_PD_OFDM(_phy, v)   ((v) << ((_phy) ? 16 : 20))

#define MT_WF_PHY_PD_CCK_MASK(_phy)  ( (_phy) ? (0xffu << 24) : (0xffu << 1))
#define MT_WF_PHY_PD_CCK(_phy, v)    ((v) << ((_phy) ? 24 : 1))

#define MT_MIB_M0_MISC_CR(_band)     (0)

#define MT_WF_RMAC_MIB_TIME5         0x000003d8
#define MT_WF_RMAC_MIB_TIME6         0x000003dc

#define MT76_RNR_SCAN_MAX_BSSIDS     16
#define MT76_CONNAC_MAX_TIME_SCHED_SCAN_INTERVAL  0xffff
#define MT76_CONNAC_MAX_SCHED_SCAN_SSID           10
#define MT76_CONNAC_MAX_SCAN_MATCH                16
#define MT76_CONNAC_SCAN_IE_LEN                   600

#define MT_MCU_PTA_BASE              0x81060000
#define MT_MCU_PTA(_n)               (MT_MCU_PTA_BASE + (_n))
#define MT_ANT_SWITCH_CON(_n)        (MT_MCU_PTA_BASE + 0x0c8 + ((_n) - 1) * 4)
#define MT_ANT_SWITCH_CON_MODE(_n)   ((0x1fu) << (_n * 8))
#define MT_ANT_SWITCH_CON_MODE1(_n)  ((0x0fu) << (_n * 8))

#define MT_PSE_SRC_CNT               (0x0fff0000u)
#define MT_PSE_PG_INFO               0x00000194

#define MT76_N_WCIDS                 1088

#define MT7615_FIRMWARE_V2           2

#define MT_ARB_RQCR                  0x00000070
#define MT_ARB_RQCR_RX_START         (1u << 0)
#define MT_ARB_RQCR_RXV_START        (1u << 4)
#define MT_ARB_RQCR_RXV_R_EN         (1u << 7)
#define MT_ARB_RQCR_RXV_T_EN         (1u << 8)
#define MT_ARB_RQCR_BAND_SHIFT       16

#define MT_ARB_SCR_TX0_DISABLE       (1u << 8)
#define MT_ARB_SCR_TX1_DISABLE       (1u << 10)
#define MT_ARB_SCR_RX0_DISABLE       (1u << 9)
#define MT_ARB_SCR_RX1_DISABLE       (1u << 11)

#define MT_MCU2HOST_INT_ENABLE       0x000001f4
#define MT7663_INT_MCU_CMD           (1u << 29)

#define MT_VEND_TYPE_EEPROM          (1u << 31)
#define MT_VEND_TYPE_CFG             (1u << 30)
#define MT_VEND_TYPE_MASK            (MT_VEND_TYPE_EEPROM | MT_VEND_TYPE_CFG)

#define MT_EFUSE_BASE_CTRL_VALID     (1u << 29)

/* Simple replacement for kernel BIT/GENMASK helper macros for local use. */
#ifndef GENMASK
#define __GENMASK(h, l) \
    (((~0u) - ((1u << (l)) - 1u)) & \
     (~0u >> (31 - (h))))
#define GENMASK(h, l) __GENMASK(h, l)
#endif

/* BAR description */
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
    uint32_t reg_pdma_busy;
    uint32_t reg_pdma_busy_status;
    uint32_t reg_pcie_irq_enable;
    uint32_t reg_wpdma_glo_cfg1;
    uint32_t reg_delay_int_cfg;

    /* Additional register shadows used in driver interactions */
    uint32_t reg_pdma_slp_prot;
    uint32_t reg_wpdma_tx_ring0_ctrl0;
    uint32_t reg_wpdma_tx_ring0_ctrl1;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Simple level-triggered INTx based on intr_status & intr_mask. */
    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns - not used here */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

/* Translate guest MMIO address (BAR0) to our internal register space.
 * The Linux driver uses pcim_iomap_regions with BAR0; no offset is applied,
 * so BAR0 base is treated as SoC register base.
 */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* All accesses are little-endian; driver mainly uses 32-bit ops. */
    if (size != 4 && size != 1 && size != 2 && size != 8) {
        return 0;
    }

    switch (addr) {
    case MT_PDMA_BUSY:
        /* PDMA not busy in this minimal model */
        val = s->reg_pdma_busy;
        break;
    case MT_PDMA_BUSY_STATUS:
        val = s->reg_pdma_busy_status;
        break;
    case MT_PDMA_SLP_PROT:
        val = s->reg_pdma_slp_prot;
        break;
    case MT_PCIE_IRQ_ENABLE:
        val = s->reg_pcie_irq_enable;
        break;
    case MT_MCU2HOST_INT_ENABLE:
        /* Return current interrupt mask. */
        val = s->intr_mask;
        break;
    case MT_WPDMA_TX_RING0_CTRL0:
        val = s->reg_wpdma_tx_ring0_ctrl0;
        break;
    case MT_WPDMA_TX_RING0_CTRL1:
        val = s->reg_wpdma_tx_ring0_ctrl1;
        break;
    case MT_WPDMA_GLO_CFG1:
        val = s->reg_wpdma_glo_cfg1;
        break;
    case MT_DELAY_INT_CFG:
        val = s->reg_delay_int_cfg;
        break;
    default:
        /* For all other registers we simply return 0. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 && size != 1 && size != 2 && size != 8) {
        return;
    }

    switch (addr) {
    case MT_PDMA_BUSY:
        /* Busy register is RO in hardware; ignore writes in this model. */
        break;
    case MT_PDMA_BUSY_STATUS:
        /* Likewise, status is typically RO; ignore. */
        break;
    case MT_PDMA_SLP_PROT:
        /* Sleep protection control - track bits so polling can observe them. */
        s->reg_pdma_slp_prot = (uint32_t)val;
        /* If sleep protection enable is set, mark RDY bit as set so that
         * mt76_poll_msec() in suspend path will succeed quickly.
         */
        if (s->reg_pdma_slp_prot & MT_PDMA_AXI_SLPPROT_ENABLE) {
            s->reg_pdma_slp_prot |= MT_PDMA_AXI_SLPPROT_RDY;
        } else {
            s->reg_pdma_slp_prot &= ~MT_PDMA_AXI_SLPPROT_RDY;
        }
        break;
    case MT_PCIE_IRQ_ENABLE:
        /* Simple enable/disable of INTx signaling. */
        s->reg_pcie_irq_enable = (uint32_t)val;
        /* Use it as a global gate for intr_mask; here we keep it simple and
         * do not change intr_mask itself, only affect line via update.
         */
        pcibase_update_irq(s);
        break;
    case MT_MCU2HOST_INT_ENABLE:
        /* Interrupt mask written by driver. */
        s->intr_mask = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case MT_WPDMA_TX_RING0_CTRL0:
        s->reg_wpdma_tx_ring0_ctrl0 = (uint32_t)val;
        break;
    case MT_WPDMA_TX_RING0_CTRL1:
        s->reg_wpdma_tx_ring0_ctrl1 = (uint32_t)val;
        break;
    case MT_WPDMA_GLO_CFG1:
        s->reg_wpdma_glo_cfg1 = (uint32_t)val;
        break;
    case MT_DELAY_INT_CFG:
        s->reg_delay_int_cfg = (uint32_t)val;
        break;
    default:
        /* Any other register write is simply stored nowhere and ignored. */
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
    s->intr_mask = 0;
    s->reg_pdma_busy = 0;
    s->reg_pdma_busy_status = 0;
    s->reg_pcie_irq_enable = 0;
    s->reg_wpdma_glo_cfg1 = 0;
    s->reg_delay_int_cfg = 0;
    s->reg_pdma_slp_prot = 0;
    s->reg_wpdma_tx_ring0_ctrl0 = 0;
    s->reg_wpdma_tx_ring0_ctrl1 = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* PCI requires BAR sizes to be a power of 2. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Express capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Power management capability (dummy but required by some drivers). */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: BAR0 MMIO 1 MiB. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 1 * MiB;
    s->bar_info[0].name = "mt7615e-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadows to reset values. */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->reg_pdma_busy = 0;
    s->reg_pdma_busy_status = 0;
    s->reg_pcie_irq_enable = 0;
    s->reg_wpdma_glo_cfg1 = 0;
    s->reg_delay_int_cfg = 0;
    s->reg_pdma_slp_prot = 0;
    s->reg_wpdma_tx_ring0_ctrl0 = 0;
    s->reg_wpdma_tx_ring0_ctrl1 = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mt7615e_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
        VMSTATE_UINT32(reg_pdma_busy, PCIBaseState),
        VMSTATE_UINT32(reg_pdma_busy_status, PCIBaseState),
        VMSTATE_UINT32(reg_pcie_irq_enable, PCIBaseState),
        VMSTATE_UINT32(reg_wpdma_glo_cfg1, PCIBaseState),
        VMSTATE_UINT32(reg_delay_int_cfg, PCIBaseState),
        VMSTATE_UINT32(reg_pdma_slp_prot, PCIBaseState),
        VMSTATE_UINT32(reg_wpdma_tx_ring0_ctrl0, PCIBaseState),
        VMSTATE_UINT32(reg_wpdma_tx_ring0_ctrl1, PCIBaseState),
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
