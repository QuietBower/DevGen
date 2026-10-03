/*
 * QEMU PCI device model for Cavium Thunder BGX (behavioral skeleton)
 * Generated to satisfy Linux driver probing and basic register access.
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

#define PCI_VENDOR_ID_CAVIUM        0x177d
#define PCI_CLASS_NETWORK_ETHERNET  0x0200

#define PCIBASE_VENDOR_ID   PCI_VENDOR_ID_CAVIUM
#define PCIBASE_DEVICE_ID   PCI_DEVICE_ID_THUNDER_BGX
#define PCIBASE_CLASS_ID    PCI_CLASS_NETWORK_ETHERNET

#define BIT(nr)                     (UL(1) << (nr))
#define BIT_ULL(nr)                 (ULL(1) << (nr))

#define BCAST_ACCEPT      BIT(0)
#define CAM_ACCEPT        BIT(3)
#define MCAST_MODE_MASK   0x3
#define BGX_MCAST_MODE(x) (x << 1)
#define PCI_SUBSYS_DEVID_83XX_BGX        0xA326
#define PCI_SUBSYS_DEVID_81XX_RGX        0xA254
#define MAX_BGX_PER_CN83XX               4
#define MAX_BGX_PER_CN88XX               2
#define PCI_SUBSYS_DEVID_81XX_BGX        0xA226
#define PCI_SUBSYS_DEVID_88XX_BGX        0xA126
#define MAX_BGX_PER_CN81XX               3
#define BGX_CMR_RX_DMACX_CAM             0x200
#define RX_DMACX_CAM_EN                  BIT_ULL(48)
#define LMAC_ID_MASK                     0x3
#define RX_DMACX_CAM_LMACID(x)           (((uint64_t)(x)) << 49)
#define BGX_XCAST_MCAST_ACCEPT           BIT(1)
#define BGX_CMRX_RX_DMAC_CTL             0x0E8
#define BGX_XCAST_BCAST_ACCEPT           BIT(0)
#define BGX_XCAST_MCAST_FILTER           BIT(2)
#define BGX_CMRX_CFG                     0x00
#define BGX_GMP_GMI_TXX_INT_ENA_W1S      0x38518
#define CMR_PKT_RX_EN                    BIT_ULL(14)
#define GMI_TXX_INT_UNDFLW               BIT_ULL(0)
#define BGX_GMP_GMI_TXX_INT_ENA_W1C      0x38510
#define CMR_PKT_TX_EN                    BIT_ULL(13)
#define BGX_GMP_GMI_RXX_FRM_CTL          0x38028
#define BGX_SMUX_RX_FRM_CTL              0x20020
#define BGX_PKT_RX_PTP_EN                BIT_ULL(12)
#define BGX_SMUX_CBFC_CTL                0x20218
#define RX_EN                            BIT_ULL(0)
#define TX_EN                            BIT_ULL(1)
#define BGX_GMP_PCS_MISCX_CTL            0x30078
#define BGX_GMP_GMI_PRTX_CFG             0x38020
#define GMI_PORT_CFG_SPEED               BIT_ULL(1)
#define BGX_GMP_GMI_TXX_SLOT             0x38220
#define PCS_MISC_CTL_SAMP_PT_MASK        0x7Full
#define BGX_GMP_GMI_TXX_BURST            0x38228
#define GMI_PORT_CFG_DUPLEX              BIT_ULL(2)
#define GMI_PORT_CFG_TX_IDLE             BIT_ULL(13)
#define GMI_PORT_CFG_SLOT_TIME           BIT_ULL(3)
#define GMI_PORT_CFG_SPEED_MSB           BIT_ULL(8)
#define PCS_MISC_CTL_GMX_ENO             BIT_ULL(11)
#define GMI_PORT_CFG_RX_IDLE             BIT_ULL(12)
#define BGX_CMRX_RX_STAT0                0x70
#define BGX_CMRX_TX_STAT0                0x600
#define SPU_CTL_LOOPBACK                 BIT_ULL(14)
#define BGX_SPUX_CONTROL1                0x10000
#define BGX_GMP_PCS_MRX_CTL              0x30000
#define PCS_MRX_CTL_LOOPBACK1            BIT_ULL(14)
#define MAX_FRAME_SIZE                   9216
#define CMR_EN                           BIT_ULL(15)
#define PCS_MISC_CTL_DISP_EN             BIT_ULL(13)
#define BGX_GMP_GMI_RXX_JABBER           0x38038
#define BGX_GMP_GMI_TXX_THRESH           0x38210
#define BGX_GMP_GMI_TXX_SGMII_CTL        0x38300
#define PCS_MRX_CTL_PWR_DN               BIT_ULL(11)
#define PCS_MRX_CTL_RST_AN               BIT_ULL(9)
#define PCS_MRX_CTL_AN_EN                BIT_ULL(12)
#define BGX_GMP_GMI_TXX_APPEND           0x38218
#define BGX_GMP_PCS_MRX_STATUS           0x30008
#define PCS_MRX_STATUS_AN_CPT            BIT_ULL(5)
#define PCS_MRX_CTL_RESET                BIT_ULL(15)
#define SMU_TX_CTL_UNI_EN                BIT_ULL(1)
#define SMU_TX_CTL_DIC_EN                BIT_ULL(0)
#define BGX_SMUX_TX_THRESH               0x20180
#define SMU_TX_APPEND_FCS_D              BIT_ULL(2)
#define BGX_SMUX_RX_INT                  0x20000
#define BGX_SMUX_TX_INT                  0x20140
#define BGX_SMUX_TX_PAUSE_ZERO           0x20138
#define SPU_CTL_LOW_POWER                BIT_ULL(11)
#define BGX_SPUX_BR_PMD_LP_CUP           0x10078
#define BGX_SPUX_AN_CONTROL              0x100C8
#define BGX_SPUX_AN_ADV                  0x100D8
#define BGX_SMUX_TX_CTL                  0x20178
#define BGX_SMUX_TX_PAUSE_PKT_TIME       0x20110
#define BGX_SMUX_RX_JABBER               0x20030
#define BGX_SPUX_BR_PMD_LD_REP           0x10090
#define BGX_SPUX_BR_PMD_CRTL             0x10068
#define SPU_AN_CTL_AN_EN                 BIT_ULL(12)
#define BGX_SMUX_TX_APPEND               0x20100
#define BGX_SPUX_MISC_CONTROL            0x10218
#define SPU_DBG_CTL_AN_ARB_LINK_CHK_EN   BIT_ULL(18)
#define BGX_SPUX_INT                     0x10220
#define BGX_SPU_DBG_CONTROL              0x10300
#define SPU_CTL_RESET                    BIT_ULL(15)
#define SPU_MISC_CTL_RX_DIS              BIT_ULL(12)
#define DRP_EN                           BIT_ULL(3)
#define BCK_EN                           BIT_ULL(2)
#define BGX_SPUX_BR_PMD_LD_CUP           0x10088
#define SPU_AN_CTL_XNP_EN                BIT_ULL(13)
#define SPU_MISC_CTL_INTLV_RDISP         BIT_ULL(10)
#define BGX_SMUX_TX_PAUSE_PKT_INTERVAL   0x20120
#define DEFAULT_PAUSE_TIME               0xFFFF
#define BGX_SPUX_FEC_CONTROL             0x100A0
#define SPU_PMD_CRTL_TRAIN_EN            BIT_ULL(1)
#define SPU_FEC_CTL_FEC_EN               BIT_ULL(0)
#define BGX_SMUX_CTL                     0x20200
#define BGX_SPUX_BX_STATUS               0x10028
#define SPU_BR_STATUS_BLK_LOCK           BIT_ULL(0)
#define SPU_BX_STATUS_RX_ALIGN           BIT_ULL(12)
#define BGX_SMUX_RX_CTL                  0x20048
#define BGX_SPUX_STATUS2                 0x10020
#define SMU_CTL_RX_IDLE                  BIT_ULL(0)
#define SMU_CTL_TX_IDLE                  BIT_ULL(1)
#define SMU_RX_CTL_STATUS                (3ull << 0)
#define BGX_SPUX_BR_STATUS1              0x10030
#define SPU_STATUS2_RCVFLT               BIT_ULL(10)
#define BGX_GMP_PCS_ANX_AN_RESULTS       0x30020
#define PCS_MRX_STATUS_LINK              BIT_ULL(2)
#define SPU_STATUS1_RCV_LNK              BIT_ULL(2)
#define BGX_SPUX_STATUS1                 0x10008
#define BGX_SMUX_TX_MIN_PKT              0x20118
#define PCS_LINKX_TIMER_COUNT            0x1E84
#define RX_DMAC_COUNT                    32
#define BGX_GMP_PCS_LINKX_TIMER          0x30040
#define BGX_GMP_GMI_TXX_MIN_PKT          0x38240
#define BGX_CMRX_TX_FIFO_LEN             0x518
#define BGX_CMRX_RX_FIFO_LEN             0x108
#define BGX_CMR_GLOBAL_CFG               0x08
#define CMR_GLOBAL_CFG_FCS_STRIP         BIT_ULL(6)
#define BGX_CMR_CHAN_MSK_AND             0x450
#define BGX_CMR_TX_LMACS                 0x1000
#define RX_TRAFFIC_STEER_RULE_COUNT      8
#define BGX_CMR_RX_LMACS                 0x468
#define MAX_BGX_CHANS_PER_LMAC           16
#define BGX_CMR_BIST_STATUS              0x460
#define BGX_CMR_RX_STEERING              0x300
#define BGX_GMP_GMI_TXX_INT              0x38500
#define BGX_LMAC_VEC_OFFSET              7
#define GMPX_GMI_TX_INT                  6
#define PCI_CFG_REG_BAR_NUM              0
#define MAX_LMAC_PER_BGX                 4
#define BGX_ID_MASK                      0x3
#define PCI_DEVICE_ID_THUNDER_RGX        0xA054
#define MAX_BGX_THUNDER                  8
#define PCI_DEVICE_ID_THUNDER_BGX        0xA026
#define NIC_NODE_ID_MASK                 0x03
#define NIC_NODE_ID_SHIFT                44

typedef enum MCAST_MODE {
    MCAST_MODE_REJECT = 0x0,
    MCAST_MODE_ACCEPT = 0x1,
    MCAST_MODE_CAM_FILTER = 0x2,
    RSVD = 0x3
} MCAST_MODE;

#define PCI_BGX_BAR_INDEX   PCI_CFG_REG_BAR_NUM
#define PCI_BGX_BAR_SIZE    (1 * MiB)

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

    uint32_t dummy_reg;

    /* Simple register backing store for BAR0 (64-bit regs) */
    uint64_t *regs;
    size_t regs_count;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    (void)is_write;
}

static inline uint32_t bgx_lmac_from_addr(hwaddr addr)
{
    return (addr >> 20) & 0xFF;
}

static inline hwaddr bgx_offset_from_addr(hwaddr addr)
{
    return addr & ((1ULL << 20) - 1);
}

static inline size_t bgx_reg_index(hwaddr offset)
{
    return offset >> 3; /* 8-byte registers */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t lmac = bgx_lmac_from_addr(addr);
    hwaddr off = bgx_offset_from_addr(addr);

    /* Driver uses readq_relaxed(), expect 8-byte aligned accesses. */
    if (size != 8) {
        return 0;
    }

    /* For now we ignore LMAC and just use offset; layout is per-LMAC window. */
    (void)lmac;

    size_t idx = bgx_reg_index(off);
    if (idx < s->regs_count) {
        val = s->regs[idx];
    } else {
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t lmac = bgx_lmac_from_addr(addr);
    hwaddr off = bgx_offset_from_addr(addr);

    if (size != 8) {
        return;
    }

    (void)lmac;

    size_t idx = bgx_reg_index(off);
    if (idx >= s->regs_count) {
        return;
    }

    /* Implement minimal semantics for a few W1C / W1S style registers. */
    switch (off) {
    case BGX_GMP_GMI_TXX_INT_ENA_W1S:
        /* write-one-to-set: OR bits into current value */
        s->regs[idx] |= val;
        break;
    case BGX_GMP_GMI_TXX_INT_ENA_W1C:
        /* write-one-to-clear: clear bits written as 1 */
        s->regs[idx] &= ~val;
        break;
    case BGX_GMP_GMI_TXX_INT: {
        /* Interrupt status W1C: clear bits that are 1 in val */
        s->regs[idx] &= ~val;
        /* No actual interrupt generation modeled here. */
        break;
    }
    default:
        s->regs[idx] = val;
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

    s->dummy_reg = 0;

    if (s->regs && s->regs_count) {
        memset(s->regs, 0, s->regs_count * sizeof(uint64_t));
        /* Some registers have non-zero defaults used by the driver. */
        /* BGX_CMR_RX_LMACS and BGX_CMR_TX_LMACS default to 0; driver will write. */
    }
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
    s->bar_info[0].index = PCI_BGX_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PCI_BGX_BAR_SIZE;
    s->bar_info[0].name = "thunder_bgx-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->dummy_reg = 0;

    /* Allocate flat register space for BAR0: 1 MiB / 8 bytes per reg. */
    s->regs_count = (PCI_BGX_BAR_SIZE >> 3);
    s->regs = g_new0(uint64_t, s->regs_count);
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

    if (s->regs) {
        g_free(s->regs);
        s->regs = NULL;
        s->regs_count = 0;
    }
}

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

type_init(pcibase_register_types)
