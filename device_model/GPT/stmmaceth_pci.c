/*
 * QEMU PCI device model for stmmaceth_pci
 * Phase 2: basic behavioral implementation for Linux stmmac_pci glue
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

#define TYPE_PCIBASE_DEVICE "stmmaceth_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x0700
#define PCIBASE_DEVICE_ID 0x1108
#define PCIBASE_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

#define MAC_CTRL_REG        0x00000000
#define GMAC_VERSION        0x00000020
#define GMAC4_VERSION       0x00000110

/* DMA HW feature register bits (used as static constants only) */
#define DMA_HW_FEAT_MIISEL      0x00000001
#define DMA_HW_FEAT_GMIISEL     0x00000002
#define DMA_HW_FEAT_HDSEL       0x00000004
#define DMA_HW_FEAT_HASHSEL     0x00000010
#define DMA_HW_FEAT_ADDMAC      0x00000020
#define DMA_HW_FEAT_PCSSEL      0x00000040
#define DMA_HW_FEAT_SMASEL      0x00000100
#define DMA_HW_FEAT_MGKSEL      0x00000400
#define DMA_HW_FEAT_RWKSEL      0x00000200
#define DMA_HW_FEAT_MMCSEL      0x00000800
#define DMA_HW_FEAT_TSVER1SEL   0x00001000
#define DMA_HW_FEAT_TSVER2SEL   0x00002000
#define DMA_HW_FEAT_EEESEL      0x00004000
#define DMA_HW_FEAT_AVSEL       0x00008000
#define DMA_HW_FEAT_RXTYP1COE   0x00020000
#define DMA_HW_FEAT_RXTYP2COE   0x00040000
#define DMA_HW_FEAT_RXFIFOSIZE  0x00080000
#define DMA_HW_FEAT_RXCHCNT     0x00300000
#define DMA_HW_FEAT_TXCHCNT     0x00c00000
#define DMA_HW_FEAT_ENHDESSEL   0x01000000
#define DMA_HW_FEAT_ACTPHYIF    0x70000000

#define LPI_CTRL_STATUS_TLPIEN  (1U << 0)
#define LPI_CTRL_STATUS_TLPIEX  (1U << 1)
#define LPI_CTRL_STATUS_RLPIEN  (1U << 2)
#define LPI_CTRL_STATUS_RLPIEX  (1U << 3)
#define LPI_CTRL_STATUS_LPIEN   (1U << 16)
#define LPI_CTRL_STATUS_PLS     (1U << 17)
#define LPI_CTRL_STATUS_LPITXA  (1U << 19)
#define LPI_CTRL_STATUS_LPIATE  (1U << 20)
#define LPI_CTRL_STATUS_LPITCSE (1U << 21)

#define CORE_IRQ_TX_PATH_IN_LPI_MODE      (1U << 0)
#define CORE_IRQ_TX_PATH_EXIT_LPI_MODE    (1U << 1)
#define CORE_IRQ_MTL_RX_OVERFLOW          (1U << 8)

#define PCS_ANE_IRQ   (1U << 2)
#define PCS_LINK_IRQ  (1U << 1)

#define MAC_ENABLE_RX 0x00000004
#define MAC_ENABLE_TX 0x00000008

#define STMMAC_RESOURCE_NAME   "stmmaceth"

#define DMA_AXI_BLEN4   (1U << 1)
#define DMA_AXI_BLEN8   (1U << 2)
#define DMA_AXI_BLEN16  (1U << 3)
#define DMA_AXI_BLEN32  (1U << 4)
#define DMA_AXI_AAL     (1U << 12)
#define DMA_AXI_BLEN_MASK  GENMASK(7, 1)

#define STMMAC_CHAIN_MODE    0x1
#define STMMAC_RING_MODE     0x2

#define DMA_DEFAULT_RX_SIZE  512
#define DMA_DEFAULT_TX_SIZE  512
#define DMA_MIN_RX_SIZE      64
#define DMA_MAX_RX_SIZE      1024
#define DMA_MIN_TX_SIZE      64
#define DMA_MAX_TX_SIZE      1024

#define BUF_SIZE_2KiB  2048
#define BUF_SIZE_4KiB  4096
#define BUF_SIZE_8KiB  8188
#define BUF_SIZE_16KiB 16368

#define STMMAC_RX_FRAMES        0
#define STMMAC_TX_FRAMES        25
#define STMMAC_COAL_TX_TIMER    5000
#define DEF_DMA_RIWT            0xa0

#define STMMAC_RSS_MAX_TABLE_SIZE 256
#define STMMAC_RSS_HASH_KEY_SIZE  40

#define STMMAC_PPS_MAX 4

#define STMMAC_TBS_AVAIL  (1U << 0)
#define STMMAC_TBS_EN     (1U << 1)

#define STMMAC_FLOW_ACTION_DROP (1U << 0)

#define STMMAC_ET_MAX 0xFFFFF

#define HASH_TABLE_SIZE 64
#define JUMBO_LEN       9000

#define STMMAC_VLAN_INSERT 0x2

#define STMMAC_RING_CHAN0 0

#define PHY_INTF_GMII          0
#define PHY_INTF_RGMII         1
#define PHY_INTF_SEL_SGMII     2
#define PHY_INTF_SEL_TBI       3
#define PHY_INTF_SEL_RMII      4
#define PHY_INTF_SEL_RTBI      5
#define PHY_INTF_SEL_SMII      6
#define PHY_INTF_SEL_REVMII    7
#define PHY_INTF_SEL_GMII_MII  0

#define STMMAC_PCS_SGMII   (1U << 1)

#define CSR_F_35M   35000000
#define CSR_F_60M   60000000
#define CSR_F_100M  100000000
#define CSR_F_150M  150000000
#define CSR_F_250M  250000000
#define CSR_F_300M  300000000
#define CSR_F_500M  500000000
#define CSR_F_800M  800000000

#define PAUSE_TIME 0xffff

#define EST_GCL 1024

#define MMC_GMAC3_X_OFFSET 0x100
#define MMC_GMAC4_OFFSET   0x700
#define MMC_XGMAC_OFFSET   0x800

#define DWXGMAC_CORE_2_10 0x21
#define DWXGMAC_CORE_2_20 0x22
#define DWXLGMAC_CORE_2_00 0x20
#define DWMAC_CORE_3_50   0x35
#define DWMAC_CORE_4_00   0x40
#define DWMAC_CORE_4_10   0x41
#define DWMAC_CORE_5_10   0x51
#define DWMAC_CORE_5_20   0x52

#define DWXLGMAC_ID 0x27
#define DWXGMAC_ID  0x76

#define DWMAC_SNPSVER GENMASK_U32(7, 0)
#define DWMAC_USERVER GENMASK_U32(15, 8)

#define LPI_CTRL_STATUS_TLPIEN  (1U << 0)

#define MMC_CNTRL_COUNTER_RESET     0x1
#define MMC_CNTRL_RESET_ON_READ     0x4
#define MMC_CNTRL_PRESET            0x10
#define MMC_CNTRL_FULL_HALF_PRESET  0x20

#define FLOW_RX 1
#define FLOW_TX 2

#define STMMAC_CHAN0 0

#define TX_CIC_FULL 3

#define RDES0_RX_MAC_ADDR       (1U << 0)
#define RDES0_CRC_ERROR         (1U << 1)
#define RDES0_DRIBBLING         (1U << 2)
#define RDES0_MII_ERROR         (1U << 3)
#define RDES0_RECEIVE_WATCHDOG  (1U << 4)
#define RDES0_FRAME_TYPE        (1U << 5)
#define RDES0_COLLISION         (1U << 6)
#define RDES0_IPC_CSUM_ERROR    (1U << 7)
#define RDES0_LAST_DESCRIPTOR   (1U << 8)
#define RDES0_OVERFLOW_ERROR    (1U << 11)
#define RDES0_LENGTH_ERROR      (1U << 12)
#define RDES0_SA_FILTER_FAIL    (1U << 13)
#define RDES0_DESCRIPTOR_ERROR  (1U << 14)
#define RDES0_ERROR_SUMMARY     (1U << 15)
#define RDES0_VLAN_TAG          (1U << 10)
#define RDES0_DA_FILTER_FAIL    (1U << 30)

#define RDES1_BUFFER1_SIZE_MASK   GENMASK(10, 0)
#define RDES1_BUFFER2_SIZE_MASK   GENMASK(21, 11)
#define RDES1_SECOND_ADDRESS_CHAINED (1U << 24)
#define RDES1_END_RING            (1U << 25)
#define RDES1_DISABLE_IC          (1U << 31)

#define TDES0_DEFERRED             (1U << 0)
#define TDES0_UNDERFLOW_ERROR      (1U << 1)
#define TDES0_EXCESSIVE_DEFERRAL   (1U << 2)
#define TDES0_COLLISION_COUNT_MASK GENMASK(6, 3)
#define TDES0_EXCESSIVE_COLLISIONS (1U << 8)
#define TDES0_LATE_COLLISION       (1U << 9)
#define TDES0_NO_CARRIER           (1U << 10)
#define TDES0_LOSS_CARRIER         (1U << 11)
#define TDES0_JABBER_TIMEOUT       (1U << 14)
#define TDES0_ERROR_SUMMARY        (1U << 15)
#define TDES0_VLAN_FRAME           (1U << 7)

#define TDES1_BUFFER1_SIZE_MASK  GENMASK(10, 0)
#define TDES1_BUFFER2_SIZE_MASK  GENMASK(21, 11)
#define TDES1_SECOND_ADDRESS_CHAINED (1U << 24)
#define TDES1_END_RING            (1U << 25)
#define TDES1_LAST_SEGMENT        (1U << 30)
#define TDES1_FIRST_SEGMENT       (1U << 29)
#define TDES1_CHECKSUM_INSERTION_MASK GENMASK(28, 27)
#define TDES1_INTERRUPT              (1U << 31)
#define TDES1_TIME_STAMP_ENABLE      (1U << 22)

#define RDES0_OWN   (1U << 31)
#define TDES0_OWN   (1U << 31)
#define ETDES0_OWN  (1U << 31)

#define ERDES1_BUFFER1_SIZE_MASK    GENMASK(12, 0)
#define ERDES1_BUFFER2_SIZE_MASK    GENMASK(28, 16)
#define ERDES1_SECOND_ADDRESS_CHAINED (1U << 14)
#define ERDES1_END_RING             (1U << 15)
#define ERDES1_DISABLE_IC           (1U << 31)

#define ERDES4_MSG_TYPE_MASK          GENMASK(11, 8)
#define ERDES4_PTP_FRAME_TYPE         (1U << 12)
#define ERDES4_PTP_VER                (1U << 13)
#define ERDES4_TIMESTAMP_DROPPED      (1U << 14)
#define ERDES4_AV_PKT_RCVD            (1U << 16)
#define ERDES4_AV_TAGGED_PKT_RCVD     (1U << 17)
#define ERDES4_IPV4_PKT_RCVD          (1U << 6)
#define ERDES4_IPV6_PKT_RCVD          (1U << 7)
#define ERDES4_IP_CSUM_BYPASSED       (1U << 5)
#define ERDES4_IP_PAYLOAD_ERR         (1U << 4)
#define ERDES4_IP_HDR_ERR             (1U << 3)
#define ERDES4_L3_FILTER_MATCH        (1U << 24)
#define ERDES4_L4_FILTER_MATCH        (1U << 25)
#define ERDES4_L3_L4_FILT_NO_MATCH_MASK GENMASK(27, 26)

#define ETDES0_SECOND_ADDRESS_CHAINED (1U << 20)
#define ETDES0_LAST_SEGMENT           (1U << 29)
#define ETDES0_FIRST_SEGMENT          (1U << 28)
#define ETDES0_CHECKSUM_INSERTION_MASK GENMASK(23, 22)
#define ETDES0_TIME_STAMP_ENABLE      (1U << 25)
#define ETDES0_TIME_STAMP_STATUS      (1U << 17)
#define ETDES0_INTERRUPT              (1U << 30)
#define ETDES0_UNDERFLOW_ERROR        (1U << 1)
#define ETDES0_DEFERRED               (1U << 0)
#define ETDES0_LATE_COLLISION         (1U << 9)
#define ETDES0_EXCESSIVE_COLLISIONS   (1U << 8)
#define ETDES0_VLAN_FRAME             (1U << 7)
#define ETDES0_ERROR_SUMMARY          (1U << 15)
#define ETDES0_NO_CARRIER             (1U << 10)
#define ETDES0_LOSS_CARRIER           (1U << 11)
#define ETDES0_JABBER_TIMEOUT         (1U << 14)
#define ETDES0_PAYLOAD_ERROR          (1U << 12)
#define ETDES0_FRAME_FLUSHED          (1U << 13)
#define ETDES0_IP_HEADER_ERROR        (1U << 16)
#define ETDES1_BUFFER2_SIZE_MASK      GENMASK(28, 16)

#define RDES_PTP_ANNOUNCE              0x8
#define RDES_PTP_MANAGEMENT            0x9
#define RDES_PTP_PKT_RESERVED_TYPE     0xf
#define RDES_EXT_NO_PTP                0x0
#define RDES_EXT_SYNC                  0x1
#define RDES_EXT_FOLLOW_UP             0x2
#define RDES_EXT_DELAY_REQ             0x3
#define RDES_EXT_DELAY_RESP            0x4
#define RDES_EXT_PDELAY_REQ            0x5
#define RDES_EXT_PDELAY_RESP           0x6
#define RDES_EXT_PDELAY_FOLLOW_UP      0x7

#define STMMAC_SAFETY_FEAT_SIZE (sizeof(struct stmmac_safety_stats) / sizeof(unsigned long))

#define STMMAC_GET_ENTRY(x, size) ((x + 1) & (size - 1))

#define STMMAC_RX_FRAMES 0

#define STMMAC_TBS_AVAIL (1U << 0)
#define STMMAC_TBS_EN    (1U << 1)

#define STMMAC_RING_MODE 0x2

struct stmmac_safety_stats {
    unsigned long mac_errors[32];
    unsigned long mtl_errors[32];
    unsigned long dma_errors[32];
    unsigned long dma_dpp_errors[32];
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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t mac_ctrl;
    uint32_t version;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The PCI glue driver only uses legacy INTx via pdev->irq. There are no
     * device-specific interrupt status registers exposed here, so we do not
     * implement any interrupt generation logic. This helper is kept as a
     * placeholder for possible future extensions when more driver sources
     * (MAC/DMA core) are provided.
     */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The provided stmmac_pci glue does not program any bus-mastering DMA
     * registers directly; all DMA setup is done in the common stmmac core
     * driver which is not part of the provided snippet. Without explicit
     * register offsets or descriptor formats, we must not implement DMA
     * transfers here.
     */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Provide only minimal behavior that is explicitly derivable from the
     * PCI glue: a MAC control register and a core-version register. All other
     * locations read as 0 so that the common stmmac core can treat the
     * hardware as present but feature-limited.
     */

    if (size != 4 && size != 8) {
        /* stmmac core mostly uses 32-bit accesses; for unsupported sizes
         * return 0.
         */
        return 0;
    }

    switch (addr) {
    case MAC_CTRL_REG:
        val = s->mac_ctrl;
        break;
    case GMAC_VERSION:
        /* For GMAC4-based core, the driver expects a non-zero version. The
         * exact encoding is defined in common headers, but here we only need
         * to expose a stable, non-zero value.
         */
        val = s->version;
        break;
    case GMAC4_VERSION:
        /* Same value for the extended GMAC4 version register so that the
         * core may treat this as a GMAC4 instance when it probes.
         */
        val = s->version;
        break;
    default:
        /* Unimplemented registers read as 0. */
        val = 0;
        break;
    }

    /* Truncate to requested access size */
    if (size == 4) {
        val &= 0xffffffffU;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 && size != 8) {
        return;
    }

    switch (addr) {
    case MAC_CTRL_REG:
        /* Store MAC control shadow; this lets the common stmmac core believe
         * it successfully enabled/disabled RX/TX paths.
         */
        s->mac_ctrl = (uint32_t)val;
        break;
    default:
        /* All other locations are write-ignored because their semantics are
         * defined in the common stmmac core which is outside the provided
         * source.
         */
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
    /* The glue driver maps only MMIO resources; PIO is unused. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* No PIO behavior is required by the glue driver. */
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

    s->mac_ctrl = 0;
    /* Choose a non-zero GMAC4 version value consistent with a valid core.
     * We use DWMAC_CORE_4_00 from the provided defines, which maps to 0x40
     * and should be accepted by the common stmmac core.
     */
    s->version = DWMAC_CORE_4_00;
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

    /* BAR Initialization: the glue driver iterates over all standard BARs and
     * maps the first one with non-zero length using pcim_iomap_region(). We
     * expose a single MMIO BAR[0] large enough for MAC + DMA register space.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000; /* generic size used in Phase 1 */
    s->bar_info[0].name = STMMAC_RESOURCE_NAME;
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X presence is not explicitly required by the PCI glue driver; leave disabled. */
    s->has_msi = false;
    s->has_msix = false;

    /* Final state initialization: match reset behavior. */
    s->mac_ctrl = 0;
    s->version = DWMAC_CORE_4_00;
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
    .name = "stmmaceth_pci",
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
