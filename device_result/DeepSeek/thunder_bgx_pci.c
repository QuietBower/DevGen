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

#define TYPE_PCIBASE_DEVICE "thunder_bgx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define BGX_VENDOR_ID            0x177d
#define BGX_DEVICE_ID            0xA026
#define BGX_CLASS_ID             PCI_CLASS_NETWORK_ETHERNET

#define DRV_NAME                 "thunder_bgx"
#define DRV_VERSION              "1.0"
#define BCAST_ACCEPT             BIT(0)
#define CAM_ACCEPT               BIT(3)
#define MCAST_MODE_MASK          0x3
#define BGX_MCAST_MODE(x)        (x << 1)
#define PCI_SUBSYS_DEVID_88XX_BGX 0xA126
#define PCI_SUBSYS_DEVID_83XX_BGX 0xA326
#define PCI_SUBSYS_DEVID_81XX_RGX 0xA254
#define MAX_BGX_PER_CN81XX       3
#define PCI_SUBSYS_DEVID_81XX_BGX 0xA226
#define MAX_BGX_PER_CN88XX       2
#define MAX_BGX_PER_CN83XX       4
#define BGX_CMR_RX_DMACX_CAM     0x200
#define RX_DMACX_CAM_LMACID(x)   (((u64)x) << 49)
#define RX_DMACX_CAM_EN          BIT_ULL(48)
#define LMAC_ID_MASK             0x3
#define BGX_CMRX_RX_DMAC_CTL     0x0E8
#define BGX_XCAST_MCAST_ACCEPT   BIT(1)
#define BGX_XCAST_MCAST_FILTER   BIT(2)
#define BGX_XCAST_BCAST_ACCEPT   BIT(0)
#define BGX_CMRX_CFG             0x00
#define BGX_GMP_GMI_TXX_INT_ENA_W1C  0x38510
#define CMR_PKT_RX_EN            BIT_ULL(14)
#define CMR_PKT_TX_EN            BIT_ULL(13)
#define GMI_TXX_INT_UNDFLW       BIT_ULL(0)
#define BGX_GMP_GMI_TXX_INT_ENA_W1S  0x38518
#define BGX_PKT_RX_PTP_EN        BIT_ULL(12)
#define BGX_SMUX_RX_FRM_CTL      0x20020
#define BGX_GMP_GMI_RXX_FRM_CTL  0x38028
#define TX_EN                    BIT_ULL(1)
#define RX_EN                    BIT_ULL(0)
#define BGX_SMUX_CBFC_CTL        0x20218
#define GMI_PORT_CFG_SPEED_MSB   BIT_ULL(8)
#define BGX_GMP_GMI_TXX_BURST    0x38228
#define PCS_MISC_CTL_SAMP_PT_MASK  0x7Full
#define BGX_GMP_PCS_MISCX_CTL    0x30078
#define GMI_PORT_CFG_RX_IDLE     BIT_ULL(12)
#define BGX_GMP_GMI_PRTX_CFG     0x38020
#define GMI_PORT_CFG_DUPLEX      BIT_ULL(2)
#define PCS_MISC_CTL_GMX_ENO     BIT_ULL(11)
#define GMI_PORT_CFG_TX_IDLE     BIT_ULL(13)
#define GMI_PORT_CFG_SLOT_TIME   BIT_ULL(3)
#define GMI_PORT_CFG_SPEED       BIT_ULL(1)
#define BGX_GMP_GMI_TXX_SLOT     0x38220
#define BGX_CMRX_RX_STAT0        0x70
#define BGX_CMRX_TX_STAT0        0x600
#define SPU_CTL_LOOPBACK         BIT_ULL(14)
#define BGX_SPUX_CONTROL1        0x10000
#define PCS_MRX_CTL_LOOPBACK1    BIT_ULL(14)
#define BGX_GMP_PCS_MRX_CTL      0x30000
#define PCS_MRX_CTL_AN_EN        BIT_ULL(12)
#define BGX_GMP_PCS_MRX_STATUS   0x30008
#define PCS_MRX_CTL_PWR_DN       BIT_ULL(11)
#define BGX_GMP_GMI_TXX_APPEND   0x38218
#define PCS_MRX_CTL_RST_AN       BIT_ULL(9)
#define PCS_MRX_STATUS_AN_CPT    BIT_ULL(5)
#define BGX_GMP_GMI_TXX_SGMII_CTL  0x38300
#define BGX_GMP_GMI_RXX_JABBER   0x38038
#define PCS_MRX_CTL_RESET        BIT_ULL(15)
#define MAX_FRAME_SIZE           9216
#define BGX_GMP_GMI_TXX_THRESH   0x38210
#define CMR_EN                   BIT_ULL(15)
#define PCS_MISC_CTL_DISP_EN     BIT_ULL(13)
#define BGX_SPUX_FEC_CONTROL     0x100A0
#define BGX_SMUX_RX_JABBER       0x20030
#define BGX_SMUX_TX_INT          0x20140
#define BGX_SPUX_AN_CONTROL      0x100C8
#define SPU_DBG_CTL_AN_ARB_LINK_CHK_EN  BIT_ULL(18)
#define BGX_SMUX_TX_PAUSE_ZERO   0x20138
#define BGX_SMUX_RX_INT          0x20000
#define BCK_EN                   BIT_ULL(2)
#define DEFAULT_PAUSE_TIME       0xFFFF
#define BGX_SPUX_BR_PMD_CRTL     0x10068
#define BGX_SPUX_MISC_CONTROL    0x10218
#define SPU_MISC_CTL_INTLV_RDISP  BIT_ULL(10)
#define SPU_FEC_CTL_FEC_EN       BIT_ULL(0)
#define SMU_TX_CTL_DIC_EN        BIT_ULL(0)
#define BGX_SPUX_AN_ADV          0x100D8
#define BGX_SMUX_TX_APPEND       0x20100
#define DRP_EN                   BIT_ULL(3)
#define BGX_SMUX_TX_PAUSE_PKT_INTERVAL  0x20120
#define BGX_SMUX_TX_THRESH       0x20180
#define SPU_AN_CTL_XNP_EN        BIT_ULL(13)
#define SPU_CTL_LOW_POWER        BIT_ULL(11)
#define SPU_AN_CTL_AN_EN         BIT_ULL(12)
#define SMU_TX_APPEND_FCS_D      BIT_ULL(2)
#define BGX_SPUX_INT             0x10220
#define SPU_CTL_RESET            BIT_ULL(15)
#define SPU_PMD_CRTL_TRAIN_EN    BIT_ULL(1)
#define BGX_SMUX_TX_CTL          0x20178
#define SPU_MISC_CTL_RX_DIS      BIT_ULL(12)
#define BGX_SMUX_TX_PAUSE_PKT_TIME  0x20110
#define SMU_TX_CTL_UNI_EN        BIT_ULL(1)
#define BGX_SPUX_BR_PMD_LD_CUP   0x10088
#define BGX_SPUX_BR_PMD_LD_REP   0x10090
#define BGX_SPU_DBG_CONTROL      0x10300
#define BGX_SPUX_BR_PMD_LP_CUP   0x10078
#define BGX_SPUX_BX_STATUS       0x10028
#define BGX_SMUX_RX_CTL          0x20048
#define SMU_CTL_TX_IDLE          BIT_ULL(1)
#define BGX_SMUX_CTL             0x20200
#define BGX_SPUX_BR_STATUS1      0x10030
#define SPU_BX_STATUS_RX_ALIGN   BIT_ULL(12)
#define SMU_RX_CTL_STATUS        (3ull << 0)
#define SMU_CTL_RX_IDLE          BIT_ULL(0)
#define SPU_BR_STATUS_BLK_LOCK   BIT_ULL(0)
#define BGX_SPUX_STATUS2         0x10020
#define SPU_STATUS2_RCVFLT       BIT_ULL(10)
#define BGX_GMP_PCS_ANX_AN_RESULTS  0x30020
#define PCS_MRX_STATUS_LINK      BIT_ULL(2)
#define SPU_STATUS1_RCV_LNK      BIT_ULL(2)
#define BGX_SPUX_STATUS1         0x10008
#define BGX_GMP_GMI_TXX_MIN_PKT  0x38240
#define BGX_GMP_PCS_LINKX_TIMER  0x30040
#define BGX_SMUX_TX_MIN_PKT      0x20118
#define PCS_LINKX_TIMER_COUNT    0x1E84
#define RX_DMAC_COUNT            32
#define BGX_CMRX_TX_FIFO_LEN     0x518
#define BGX_CMRX_RX_FIFO_LEN     0x108
#define BGX_CMR_RX_STEERING      0x300
#define MAX_BGX_CHANS_PER_LMAC   16
#define BGX_CMR_TX_LMACS         0x1000
#define BGX_CMR_RX_LMACS         0x468
#define BGX_CMR_BIST_STATUS      0x460
#define BGX_CMR_GLOBAL_CFG       0x08
#define CMR_GLOBAL_CFG_FCS_STRIP BIT_ULL(6)
#define RX_TRAFFIC_STEER_RULE_COUNT  8
#define BGX_CMR_CHAN_MSK_AND     0x450
#define BGX_GMP_GMI_TXX_INT      0x38500
#define BGX_LMAC_VEC_OFFSET      7
#define GMPX_GMI_TX_INT          6
#define BGX_ID_MASK              0x3
#define MAX_LMAC_PER_BGX         4
#define PCI_DEVICE_ID_THUNDER_RGX  0xA054
#define PCI_CFG_REG_BAR_NUM      0
#define MAX_BGX_THUNDER          8
#define PCI_DEVICE_ID_THUNDER_BGX  0xA026
#define NIC_NODE_ID_MASK         0x03
#define NIC_NODE_ID_SHIFT        44
#define MCAST_MODE_REJECT        0x0
#define MCAST_MODE_ACCEPT        0x1
#define MCAST_MODE_CAM_FILTER    0x2
#define RSVD                     0x3

#define BGX_BAR0_SIZE            (4 * MiB)
#define BGX_MSI_VECTORS          8  /* rounded up to power of 2 (original 7) to satisfy QEMU msi_init assertion */

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

typedef struct {
    uint64_t cmrx_cfg;
    uint64_t cmrx_rx_dmac_ctl;
    uint64_t gmp_gmi_prtx_cfg;
    uint64_t gmp_pcs_miscx_ctl;
    uint64_t gmp_pcs_mrx_ctl;
    uint64_t gmp_gmi_txx_int;
    uint64_t int_enable;
    uint64_t gmp_gmi_txx_thresh;
    uint64_t gmp_gmi_rxx_jabber;
    uint64_t gmp_gmi_txx_append;
    uint64_t gmp_gmi_txx_slot;
    uint64_t gmp_gmi_txx_burst;
    uint64_t gmp_gmi_txx_min_pkt;
    uint64_t gmp_gmi_txx_sgmii_ctl;
} LMACState;

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

    /* Operational Status */
    uint32_t status;

    /* Per-LMAC state */
    LMACState lmac[MAX_LMAC_PER_BGX];
    uint32_t lmac_count;

    /* Global registers (accessible via lmac=0) */
    uint64_t cmr_rx_lmacs;
    uint64_t cmr_tx_lmacs;
    uint64_t cmr_global_cfg;
    uint64_t cmr_bist_status;
    uint64_t cmr_chan_msk_and;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    /* No interrupt generation needed for probe success */
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 8) {
        return ~0ULL;
    }

    if (addr >= (4 * MiB)) {
        return 0;
    }

    int lmac = (addr >> 20) & 0x3;
    uint32_t offset = addr & 0xFFFFF;

    /* Handle global registers for lmac=0 */
    if (lmac == 0) {
        switch (offset) {
        case BGX_CMR_RX_LMACS:
            val = s->cmr_rx_lmacs;
            break;
        case BGX_CMR_BIST_STATUS:
            val = s->cmr_bist_status;
            break;
        case BGX_CMR_GLOBAL_CFG:
            val = s->cmr_global_cfg;
            break;
        case BGX_CMR_TX_LMACS:
            val = s->cmr_tx_lmacs;
            break;
        case BGX_CMR_CHAN_MSK_AND:
            val = s->cmr_chan_msk_and;
            break;
        }
    }

    /* Per-LMAC registers */
    LMACState *lm = &s->lmac[lmac];
    switch (offset) {
    case BGX_CMRX_CFG:
        val = lm->cmrx_cfg;
        break;
    case BGX_CMRX_RX_DMAC_CTL:
        val = lm->cmrx_rx_dmac_ctl;
        break;
    case BGX_GMP_GMI_PRTX_CFG:
        val = lm->gmp_gmi_prtx_cfg;
        break;
    case BGX_GMP_PCS_MISCX_CTL:
        val = lm->gmp_pcs_miscx_ctl;
        break;
    case BGX_GMP_PCS_MRX_CTL:
        val = lm->gmp_pcs_mrx_ctl;
        break;
    case BGX_GMP_GMI_TXX_INT:
        val = lm->gmp_gmi_txx_int;
        break;
    case BGX_GMP_GMI_TXX_THRESH:
        val = lm->gmp_gmi_txx_thresh;
        break;
    case BGX_GMP_GMI_RXX_JABBER:
        val = lm->gmp_gmi_rxx_jabber;
        break;
    case BGX_GMP_GMI_TXX_APPEND:
        val = lm->gmp_gmi_txx_append;
        break;
    case BGX_GMP_GMI_TXX_SLOT:
        val = lm->gmp_gmi_txx_slot;
        break;
    case BGX_GMP_GMI_TXX_BURST:
        val = lm->gmp_gmi_txx_burst;
        break;
    case BGX_GMP_GMI_TXX_MIN_PKT:
        val = lm->gmp_gmi_txx_min_pkt;
        break;
    case BGX_GMP_GMI_TXX_SGMII_CTL:
        val = lm->gmp_gmi_txx_sgmii_ctl;
        break;
    case BGX_CMRX_RX_FIFO_LEN:
        val = 0;
        break;
    case BGX_CMRX_TX_FIFO_LEN:
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

    if (size != 8) {
        return;
    }

    if (addr >= (4 * MiB)) {
        return;
    }

    int lmac = (addr >> 20) & 0x3;
    uint32_t offset = addr & 0xFFFFF;

    /* Global registers for lmac=0 */
    if (lmac == 0) {
        switch (offset) {
        case BGX_CMR_GLOBAL_CFG:
            s->cmr_global_cfg = val;
            return;
        case BGX_CMR_TX_LMACS:
            s->cmr_tx_lmacs = val;
            return;
        case BGX_CMR_CHAN_MSK_AND:
            s->cmr_chan_msk_and = val;
            return;
        }
    }

    /* Per-LMAC registers */
    LMACState *lm = &s->lmac[lmac];
    switch (offset) {
    case BGX_CMRX_CFG:
        lm->cmrx_cfg = val;
        break;
    case BGX_CMRX_RX_DMAC_CTL:
        lm->cmrx_rx_dmac_ctl = val;
        break;
    case BGX_GMP_GMI_PRTX_CFG:
        lm->gmp_gmi_prtx_cfg = val;
        break;
    case BGX_GMP_PCS_MISCX_CTL:
        lm->gmp_pcs_miscx_ctl = val;
        break;
    case BGX_GMP_PCS_MRX_CTL:
        /* Ignore reset bit to simulate immediate reset completion */
        lm->gmp_pcs_mrx_ctl = val & ~PCS_MRX_CTL_RESET;
        break;
    case BGX_GMP_GMI_TXX_INT_ENA_W1C:
        lm->int_enable &= ~val;
        break;
    case BGX_GMP_GMI_TXX_INT_ENA_W1S:
        lm->int_enable |= val;
        break;
    case BGX_GMP_GMI_TXX_INT:
        /* W1C: write 1 to clear interrupt status */
        lm->gmp_gmi_txx_int &= ~val;
        break;
    case BGX_GMP_GMI_TXX_THRESH:
        lm->gmp_gmi_txx_thresh = val;
        break;
    case BGX_GMP_GMI_RXX_JABBER:
        lm->gmp_gmi_rxx_jabber = val;
        break;
    case BGX_GMP_GMI_TXX_APPEND:
        lm->gmp_gmi_txx_append = val;
        break;
    case BGX_GMP_GMI_TXX_SLOT:
        lm->gmp_gmi_txx_slot = val;
        break;
    case BGX_GMP_GMI_TXX_BURST:
        lm->gmp_gmi_txx_burst = val;
        break;
    case BGX_GMP_GMI_TXX_MIN_PKT:
        lm->gmp_gmi_txx_min_pkt = val;
        break;
    case BGX_GMP_GMI_TXX_SGMII_CTL:
        lm->gmp_gmi_txx_sgmii_ctl = val;
        break;
    default:
        /* Ignore unimplemented registers */
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

    s->lmac_count = MAX_LMAC_PER_BGX;
    s->cmr_rx_lmacs = 4;
    s->cmr_tx_lmacs = 0;
    s->cmr_global_cfg = 0;
    s->cmr_bist_status = 0;
    s->cmr_chan_msk_and = 0;

    for (int i = 0; i < MAX_LMAC_PER_BGX; i++) {
        memset(&s->lmac[i], 0, sizeof(LMACState));
        s->lmac[i].cmrx_cfg = (0 << 8) | i;  /* SGMII mode, lane_to_sds = lmacid */
        s->lmac[i].gmp_gmi_prtx_cfg = GMI_PORT_CFG_RX_IDLE | GMI_PORT_CFG_TX_IDLE;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  BGX_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  BGX_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, BGX_CLASS_ID );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BGX_BAR0_SIZE, .name = "bar0" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization: driver requests BGX_LMAC_VEC_OFFSET (7) vectors,
     * but QEMU requires power-of-2, so allocate 8 vectors. */
    if (msi_init(pdev, 0, BGX_MSI_VECTORS, true, false, errp)) {
        return;
    }
    s->has_msi = true;

    /* Initialize hardware state */
    pcibase_reset(DEVICE(pdev));
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
    .name = "thunder_bgx_pci",
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
