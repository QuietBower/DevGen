/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
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

#define TYPE_PCIBASE_DEVICE "amd_xgbe_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Define VENDOR_ID, DEVICE_ID, CLASS_ID, and register offsets here */
#define PCI_VENDOR_ID_AMD 0x1022
#define XGBE_YC_PCI_DEVICE_ID 0x14b5
#define XGBE_RV_PCI_DEVICE_ID 0x15d0
#define XGBE_RN_PCI_DEVICE_ID 0x1630
#define XGBE_XGMAC_BAR 0
#define XGBE_XPCS_BAR 1
#define XGBE_MAC_PROP_OFFSET 0x1d000
#define XGBE_I2C_CTRL_OFFSET 0x1e000
#define PCS_RN_PORT_ADDR_SIZE 0x100000
#define MAC_VR 0x0110
#define MAC_HWF0R 0x011c
#define MAC_HWF1R 0x0120
#define MAC_HWF2R 0x0124
#define XP_PROP_0 0x0000
#define XP_PROP_1 0x0004
#define XP_PROP_2 0x0008
#define XP_PROP_3 0x000c
#define XP_PROP_4 0x0010
#define XP_MAC_ADDR_LO 0x0020
#define XP_MAC_ADDR_HI 0x0024
#define XP_INT_EN 0x0078
#define XP_INT_REISSUE_EN 0x0074
#define XP_DRIVER_SCRATCH_0 0x0068
#define XP_DRIVER_SCRATCH_1 0x006c
#define XP_DRIVER_INT_REQ 0x0060
#define XP_DRIVER_INT_RO 0x0064
#define XP_ECC_IER 0x0034
#define XP_ECC_ISR 0x0030
#define MAC_RCR 0x0004
#define MAC_TCR 0x0000
#define MAC_PFR 0x0008
#define MAC_HTR0 0x0010
#define MAC_HTR_INC 4
#define MAC_TIR 0x00e0
#define MAC_RFCR 0x0090
#define MAC_VLANTR 0x0050
#define MAC_VLANHTR 0x0058
#define MAC_VLANIR 0x0060
#define MAC_RQC0R 0x00a0
#define MAC_RQC2R 0x00a8
#define MAC_RQC2_INC 4
#define MAC_RQC2_Q_PER_REG 4
#define MAC_RSSAR 0x0c88
#define MAC_RSSDR 0x0c8c
#define MAC_RSSCR 0x0c80
#define MAC_MDIOIER 0x0218
#define MAC_MDIOSCAR 0x0200
#define MAC_MDIOSCCDR 0x0204
#define MAC_MDIOCL22R 0x0220
#define MAC_IER 0x00b4
#define MAC_TSAR 0x0d18
#define MAC_TSCR 0x0d00
#define MAC_TSCR_TSUPDT_INDEX 3
#define MAC_STSR 0x0d08
#define MAC_STNR 0x0d0c
#define MAC_STSUR 0x0d10
#define MAC_STNUR 0x0d14
#define MAC_GPIOSR 0x027c
#define MAC_Q0TFCR 0x0070
#define MAC_QTFCR_INC 4
#define MAC_PPS0_TTSR 0x0d80
#define MAC_PPS0_WIDTH 0x0d8c
#define MAC_PPS0_INTERVAL 0x0d88
#define MAC_PPS0_TTNSR 0x0d84
#define MAC_PPSCR 0x0d70
#define MAC_PPSx_TTSR(x) ((MAC_PPS0_TTSR) + ((x) * 0x10))
#define MAC_PPSx_WIDTH(x) ((MAC_PPS0_WIDTH) + ((x) * 0x10))
#define MAC_PPSx_INTERVAL(x) ((MAC_PPS0_INTERVAL) + ((x) * 0x10))
#define MAC_PPSx_TTNSR(x) ((MAC_PPS0_TTNSR) + ((x) * 0x10))
#define MAC_MACA1HR 0x0308
#define MAC_MACA_INC 4
#define DMA_MR 0x3000
#define DMA_SBMR 0x3004
#define DMA_SBMR_BLEN_64 64
#define DMA_AXIARCR 0x3010
#define DMA_AXIAWCR 0x3018
#define DMA_AXIAWARCR 0x301c
#define DMA_RXEDMACR 0x3044
#define DMA_TXEDMACR 0x3040
#define DMA_DSR0 0x3020
#define DMA_DSR1 0x3024
#define DMA_DSR0_TPS_START 12
#define DMA_DSRX_TPS_START 4
#define DMA_DSRX_FIRST_QUEUE 3
#define DMA_DSRX_QPR 4
#define DMA_DSR_RPS_WIDTH 4
#define DMA_TPS_STOPPED 0x00
#define DMA_TPS_SUSPENDED 0x06
#define DMA_DSR_TPS_WIDTH 4
#define DMA_DSR_Q_WIDTH (DMA_DSR_RPS_WIDTH + DMA_DSR_TPS_WIDTH)
#define DMA_DSRX_INC 4
#define DMA_CH_CR 0x00
#define DMA_CH_TCR 0x04
#define DMA_CH_RCR 0x08
#define DMA_CH_TDLR_HI 0x10
#define DMA_CH_TDLR_LO 0x14
#define DMA_CH_RDLR_HI 0x18
#define DMA_CH_RDLR_LO 0x1c
#define DMA_CH_TDTR_LO 0x24
#define DMA_CH_RDTR_LO 0x2c
#define DMA_CH_TDRLR 0x30
#define DMA_CH_RDRLR 0x34
#define DMA_CH_RIWT 0x3c
#define DMA_CH_IER 0x38
#define DMA_CH_SR 0x60
#define MMC_CR 0x0800
#define MMC_RISR 0x0804
#define MMC_TISR 0x0808
#define MMC_RIER 0x080c
#define MMC_TIER 0x0810
#define MMC_RXFRAMECOUNT_GB_LO 0x0900
#define MMC_RXOCTETCOUNT_G_LO 0x0910
#define MMC_RXOCTETCOUNT_GB_LO 0x0908
#define MMC_RXBROADCASTFRAMES_G_LO 0x0918
#define MMC_RXMULTICASTFRAMES_G_LO 0x0920
#define MMC_RXCRCERROR_LO 0x0928
#define MMC_RXRUNTERROR 0x0930
#define MMC_RXJABBERERROR 0x0934
#define MMC_RXUNDERSIZE_G 0x0938
#define MMC_RXOVERSIZE_G 0x093c
#define MMC_RX64OCTETS_GB_LO 0x0940
#define MMC_RX65TO127OCTETS_GB_LO 0x0948
#define MMC_RX128TO255OCTETS_GB_LO 0x0950
#define MMC_RX256TO511OCTETS_GB_LO 0x0958
#define MMC_RX512TO1023OCTETS_GB_LO 0x0960
#define MMC_RX1024TOMAXOCTETS_GB_LO 0x0968
#define MMC_RXUNICASTFRAMES_G_LO 0x0970
#define MMC_RXLENGTHERROR_LO 0x0978
#define MMC_RXOUTOFRANGETYPE_LO 0x0980
#define MMC_RXPAUSEFRAMES_LO 0x0988
#define MMC_RXFIFOOVERFLOW_LO 0x0990
#define MMC_RXVLANFRAMES_GB_LO 0x0998
#define MMC_RXWATCHDOGERROR 0x09a0
#define MMC_RXALIGNMENTERROR 0x09bc
#define MMC_TXOCTETCOUNT_GB_LO 0x0814
#define MMC_TXFRAMECOUNT_GB_LO 0x081c
#define MMC_TX512TO1023OCTETS_GB_LO 0x0854
#define MMC_TXFRAMECOUNT_G_LO 0x088c
#define MMC_TXBROADCASTFRAMES_GB_LO 0x0874
#define MMC_TXMULTICASTFRAMES_GB_LO 0x086c
#define MMC_TX128TO255OCTETS_GB_LO 0x0844
#define MMC_TXUNDERFLOWERROR_LO 0x087c
#define MMC_TX1024TOMAXOCTETS_GB_LO 0x085c
#define MMC_TXVLANFRAMES_G_LO 0x089c
#define MMC_TXMULTICASTFRAMES_G_LO 0x082c
#define MMC_TXPAUSEFRAMES_LO 0x0894
#define MMC_TX256TO511OCTETS_GB_LO 0x084c
#define MMC_TXUNICASTFRAMES_GB_LO 0x0864
#define MMC_TX64OCTETS_GB_LO 0x0834
#define MMC_TXBROADCASTFRAMES_G_LO 0x0824
#define MMC_TXOCTETCOUNT_G_LO 0x0884
#define MMC_TX65TO127OCTETS_GB_LO 0x083c
#define MTL_Q_BASE 0x1100
#define MTL_Q_INC 0x80
#define MTL_Q_TQOMR 0x00
#define MTL_Q_RQOMR 0x40
#define MTL_Q_TQDR 0x08
#define MTL_Q_RQDR 0x48
#define MTL_Q_RQFCR 0x50
#define MTL_Q_IER 0x70
#define MTL_Q_ISR 0x74
#define MTL_OMR 0x1000
#define MTL_TC_ETSCR 0x10
#define MTL_TC_QWR 0x18
#define MTL_TCPM0R 0x1040
#define MTL_TCPM_TC_PER_REG 4
#define MTL_TCPM_INC 4
#define MTL_RQDCM0R 0x1030
#define MTL_RQDCM_INC 4
#define MTL_RQDCM_Q_PER_REG 4
#define MTL_TSA_SP 0x00
#define MTL_TSA_ETS 0x02
#define MTL_ETSALG_WRR 0x00
#define MTL_ETSALG_DWRR 0x02
#define MTL_RAA_SP 0x00
#define MTL_TSF_ENABLE 0x01
#define MTL_RSF_DISABLE 0x00
#define MTL_Q_ENABLED 0x02
#define MTL_RX_THRESHOLD_64 0x00
#define MTL_TX_THRESHOLD_64 0x00
#define DMA_PBL_128 128
#define DMA_OSP_ENABLE 0x01
#define DMA_PBL_X8_ENABLE 0x01
#define DMA_PBL_X8_DISABLE 0x00
#define XGBE_DMA_PCI_ARCR 0x000f0f0f
#define XGBE_DMA_PCI_AWCR 0x0f0f0f0f
#define XGBE_DMA_PCI_AWARCR 0x00000f0f
#define XGBE_MSI_BASE_COUNT 4
#define XGBE_MSI_MIN_COUNT 8
#define XGBE_MAC_VER_33 0x33

/* Additional constants */
#define XGBE_MAC_SS_10G 0x00
#define XGBE_MAC_SS_1G 0x03
#define XGBE_MAC_SS_2_5G_XGMII 0x06
#define XGBE_MAC_SS_10M 0x07
#define XGBE_V2_DMA_CLOCK_FREQ 500000000
#define XGBE_V2_PTP_CLOCK_FREQ 125000000
#define XGBE_ETH_FRAME_HDR (ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN)
#define XGBE_RX_DESC_CNT 512
#define XGBE_TX_DESC_CNT 512
#define XGBE_MAX_QUEUES 16
#define XGBE_MAX_DMA_CHANNELS 16
#define XGBE_RSS_MAX_TABLE_SIZE 256
#define XGBE_RSS_HASH_KEY_SIZE 40
#define XGBE_XGMAC_BAR 0
#define XGBE_XPCS_BAR 1

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
    uint32_t intr_status;   /* XP_DRIVER_INT_RO or combined interrupt status */
    uint32_t intr_mask;     /* XP_INT_EN */

    /* Hardware Register Shadows */
    uint32_t *bar0_regs;
    uint32_t bar0_size;
    uint32_t *bar1_regs;
    uint32_t bar1_size;

    /* DMA Context - not used yet, kept for future expansion */
    struct {
        /* DMA descriptor ring information and status */
    } dma;

    /* Operational status flags */
    struct {
        /* State used to handle reset sequences */
    } reset_state;

    /* Power management state */
    uint8_t power_state;
};

/* Update IRQ level based on intr_status and intr_mask */
static void xgbe_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        if (s->has_msi) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* BAR0 MMIO read handler */
static uint64_t pcibase_bar0_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    hwaddr idx = addr >> 2;
    if (size != 4) {
        return ~0ULL;
    }
    if (idx >= (s->bar0_size >> 2)) {
        return 0;
    }
    val = s->bar0_regs[idx];
    return val;
}

/* BAR0 MMIO write handler */
static void pcibase_bar0_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr idx = addr >> 2;
    if (size != 4) {
        return;
    }
    if (idx >= (s->bar0_size >> 2)) {
        return;
    }
    s->bar0_regs[idx] = (uint32_t)val;
    /* Side effects for specific registers */
    switch (addr) {
    case XGBE_MAC_PROP_OFFSET + XP_INT_EN:
        s->intr_mask = val;
        xgbe_update_irq(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_bar0_mmio_ops = {
    .read = pcibase_bar0_mmio_read,
    .write = pcibase_bar0_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* BAR1 MMIO read handler */
static uint64_t pcibase_bar1_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    hwaddr idx = addr >> 2;
    if (size != 4) {
        return ~0ULL;
    }
    if (idx >= (s->bar1_size >> 2)) {
        return 0;
    }
    val = s->bar1_regs[idx];
    return val;
}

/* BAR1 MMIO write handler */
static void pcibase_bar1_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr idx = addr >> 2;
    if (size != 4) {
        return;
    }
    if (idx >= (s->bar1_size >> 2)) {
        return;
    }
    s->bar1_regs[idx] = (uint32_t)val;
}

static const MemoryRegionOps pcibase_bar1_mmio_ops = {
    .read = pcibase_bar1_mmio_read,
    .write = pcibase_bar1_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to power-on defaults */
    memset(s->bar0_regs, 0, s->bar0_size);
    memset(s->bar1_regs, 0, s->bar1_size);

    /* Initialize specific default values */
    /* MAC version register */
    s->bar0_regs[MAC_VR >> 2] = XGBE_MAC_VER_33;

    /* Port property registers */
    s->bar0_regs[(XGBE_MAC_PROP_OFFSET + XP_PROP_0) >> 2] = 0x00000000;
    s->bar0_regs[(XGBE_MAC_PROP_OFFSET + XP_PROP_1) >> 2] = 0x04040404; /* example: 4 channels, 4 queues */
    s->bar0_regs[(XGBE_MAC_PROP_OFFSET + XP_PROP_2) >> 2] = 0x0f0f0000; /* TX/RX FIFO sizes = 15*16384 */
    s->bar0_regs[(XGBE_MAC_PROP_OFFSET + XP_PROP_3) >> 2] = 0x00000000;
    s->bar0_regs[(XGBE_MAC_PROP_OFFSET + XP_PROP_4) >> 2] = 0x00000000;

    /* MAC address: 02:00:00:00:00:01 with valid bit set */
    s->bar0_regs[(XGBE_MAC_PROP_OFFSET + XP_MAC_ADDR_LO) >> 2] = 0x00000002;
    s->bar0_regs[(XGBE_MAC_PROP_OFFSET + XP_MAC_ADDR_HI) >> 2] = 0x80000100;

    /* Interrupt mask initially disabled */
    s->intr_mask = 0;
    s->intr_status = 0;
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
        if (bi->index == XGBE_XGMAC_BAR) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_bar0_mmio_ops, s, bi->name, aligned_size);
        } else if (bi->index == XGBE_XPCS_BAR) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_bar1_mmio_ops, s, bi->name, aligned_size);
        } else {
            /* fallback, should not happen */
            memory_region_init_io(mr, OBJECT(s), &pcibase_bar0_mmio_ops, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* Not used by this device */
        g_assert_not_reached();
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_AMD);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x14b5);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200); /* PCI_CLASS_NETWORK_ETHERNET */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar0_size = 0x20000;
    s->bar1_size = PCS_RN_PORT_ADDR_SIZE;

    /* Allocate shadow register arrays */
    s->bar0_regs = g_new0(uint32_t, s->bar0_size >> 2);
    s->bar1_regs = g_new0(uint32_t, s->bar1_size >> 2);

    s->bar_info[0] = (BARInfo){
        .index = XGBE_XGMAC_BAR,
        .type = BAR_TYPE_MMIO,
        .size = s->bar0_size,
        .name = "xgmac-mmio"
    };
    s->bar_info[1] = (BARInfo){
        .index = XGBE_XPCS_BAR,
        .type = BAR_TYPE_MMIO,
        .size = s->bar1_size,
        .name = "xpcs-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI Initialization */
    if (msi_init(pdev, 0, XGBE_MSI_MIN_COUNT, true, false, errp) < 0) {
        return;
    }
    s->has_msi = true;
    s->has_msix = false;

    /* Final state initialization */
    s->power_state = 0; /* D0 */
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

    g_free(s->bar0_regs);
    g_free(s->bar1_regs);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "amd_xgbe_pci",
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
