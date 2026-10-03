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
#include "hw/pci/pci_ids.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

/* Additional include files retrieved from driver context */
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "nicvf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Vendor and Device IDs */
#define PCI_VENDOR_ID_CAVIUM         0x177d
#define PCI_DEVICE_ID_THUNDER_NIC_VF 0xA034
#define PCI_SUBSYS_DEVID_88XX_NIC_VF 0xA134
#define PCI_CLASS_NICVF PCI_CLASS_NETWORK_ETHERNET

/* Mailbox communication */
#define NIC_VF_PF_MAILBOX_0_1        0x000130
#define NIC_MBOX_MSG_TIMEOUT         2000
#define NIC_MBOX_MSG_READY           0x01
#define NIC_MBOX_MSG_CFG_DONE        0xF0
#define NIC_MBOX_MSG_NACK            0x03
#define NIC_MBOX_MSG_ALLOC_SQS       0x12
#define NIC_MBOX_MSG_BGX_STATS       0x10
#define NIC_MBOX_MSG_SNICVF_PTR      0x15
#define NIC_MBOX_MSG_RSS_SIZE        0x0B
#define NIC_MBOX_MSG_PNICVF_PTR      0x14
#define NIC_MBOX_MSG_PFC             0x18
#define NIC_MBOX_MSG_BGX_LINK_CHANGE 0x11
#define NIC_MBOX_MSG_ACK             0x02
#define NIC_MBOX_MSG_SET_MAC         0x08
#define NIC_MBOX_MSG_CPI_CFG         0x0A
#define NIC_MBOX_MSG_RSS_CFG_CONT    0x0D
#define NIC_MBOX_MSG_RSS_CFG         0x0C
#define NIC_MBOX_MSG_SHUTDOWN        0xF1
#define NIC_MBOX_MSG_PTP_CFG         0x19
#define NIC_MBOX_MSG_SET_MAX_FRS     0x09
#define NIC_MBOX_MSG_LOOPBACK        0x16
#define NIC_MBOX_MSG_RESET_XCAST     0xF2
#define NIC_MBOX_MSG_ADD_MCAST       0xF3
#define NIC_MBOX_MSG_SET_XCAST       0xF4
#define NIC_MBOX_MSG_QS_CFG          0x04
#define NIC_MBOX_MSG_RQ_CFG          0x05
#define NIC_MBOX_MSG_SQ_CFG          0x06
#define NIC_MBOX_MSG_RQ_DROP_CFG     0x07
#define NIC_MBOX_MSG_RQ_BP_CFG       0x0E
#define NIC_MBOX_MSG_RQ_SW_SYNC      0x0F
#define NIC_MBOX_MSG_RESET_STAT_COUNTER 0x17
#define NIC_MBOX_MSG_NICVF_PTR       0x13

/* Queue number shift */
#define NIC_Q_NUM_SHIFT              18
/* PF-VF mailbox size */
#define NIC_PF_VF_MAILBOX_SIZE       2

/* VNIC global registers */
#define NIC_VNIC_CFG                 0x000020
#define NIC_VNIC_RSS_CFG             0x0020E0
#define NIC_VNIC_RSS_KEY_0_4         0x002200
#define NIC_VF_INT                   0x000200
#define NIC_VF_INT_W1S               0x000220
#define NIC_VF_ENA_W1S               0x000260
#define NIC_VF_ENA_W1C               0x000240

/* Queue set registers (offsets from base) */
#define NIC_QSET_RQ_GEN_CFG          0x010010
#define NIC_QSET_CQ_0_7_CFG          0x010400
#define NIC_QSET_CQ_0_7_CFG2         0x010408
#define NIC_QSET_CQ_0_7_THRESH       0x010410
#define NIC_QSET_CQ_0_7_STATUS       0x010440
#define NIC_QSET_CQ_0_7_STATUS2      0x010448
#define NIC_QSET_CQ_0_7_DEBUG        0x010450
#define NIC_QSET_CQ_0_7_HEAD         0x010428
#define NIC_QSET_CQ_0_7_TAIL         0x010430
#define NIC_QSET_CQ_0_7_DOOR         0x010438
#define NIC_QSET_CQ_0_7_BASE         0x010420

#define NIC_QSET_SQ_0_7_CFG          0x010800
#define NIC_QSET_SQ_0_7_THRESH       0x010810
#define NIC_QSET_SQ_0_7_BASE         0x010820
#define NIC_QSET_SQ_0_7_HEAD         0x010828
#define NIC_QSET_SQ_0_7_TAIL         0x010830
#define NIC_QSET_SQ_0_7_DOOR         0x010838
#define NIC_QSET_SQ_0_7_STATUS       0x010840
#define NIC_QSET_SQ_0_7_DEBUG        0x010848
#define NIC_QSET_SQ_0_7_STAT_0_1     0x010900

#define NIC_QSET_RQ_0_7_CFG          0x010600
#define NIC_QSET_RQ_0_7_STAT_0_1     0x010700

#define NIC_QSET_RBDR_0_1_CFG        0x010C00
#define NIC_QSET_RBDR_0_1_BASE       0x010C20
#define NIC_QSET_RBDR_0_1_HEAD       0x010C28
#define NIC_QSET_RBDR_0_1_TAIL       0x010C30
#define NIC_QSET_RBDR_0_1_DOOR       0x010C38
#define NIC_QSET_RBDR_0_1_STATUS0    0x010C40
#define NIC_QSET_RBDR_0_1_STATUS1    0x010C48
#define NIC_QSET_RBDR_0_1_PREFETCH_STATUS 0x010C50
#define NIC_QSET_RBDR_0_1_THRESH     0x010C10

/* Hardware constants */
#define NIC_VF_MSIX_VECTORS          20
#define MSIX_BAR_SIZE                0x4000ull
#define NIC_VF_REG_COUNT             249
#define MAX_QUEUES_PER_QSET          8
#define MAX_SQS_PER_VF               11
#define MAX_SQS_PER_VF_SINGLE_NODE   5
#define MAX_CMP_QUEUES_PER_QS        8
#define MAX_SND_QUEUES_PER_QS        8
#define MAX_RCV_QUEUES_PER_QS        8
#define MAX_RCV_BUF_DESC_RINGS_PER_QS 2

/* Interrupt indices and masks */
#define NICVF_INTR_MBOX_SHIFT         22
#define NICVF_INTR_QS_ERR_SHIFT       23
#define NICVF_INTR_CQ_SHIFT           0
#define NICVF_INTR_SQ_SHIFT           8
#define NICVF_INTR_RBDR_SHIFT         16
#define NICVF_INTR_PKT_DROP_SHIFT     20
#define NICVF_INTR_TCP_TIMER_SHIFT    21

#define NICVF_INTR_MBOX              5
#define NICVF_INTR_QS_ERR            6
#define NICVF_INTR_CQ                0
#define NICVF_INTR_SQ                1
#define NICVF_INTR_RBDR              2
#define NICVF_INTR_PKT_DROP          3
#define NICVF_INTR_TCP_TIMER         4

#define NICVF_INTR_ID_CQ             0
#define NICVF_INTR_ID_SQ             8
#define NICVF_INTR_ID_RBDR           16
#define NICVF_INTR_ID_MISC           18
#define NICVF_INTR_ID_QS_ERR         19

#define NICVF_INTR_MBOX_MASK         BIT(NICVF_INTR_MBOX_SHIFT)

/* Fix for missing BIT_ULL macro in QEMU */
#ifndef BIT_ULL
#define BIT_ULL(n) (1ULL << (n))
#endif

/* Configuration bit defaults */
#define NICVF_SQ_EN                  BIT_ULL(19)
#define NICVF_SQ_RESET               BIT_ULL(17)
#define NICVF_CQ_RESET               BIT_ULL(41)
#define NICVF_RBDR_RESET             BIT_ULL(43)

/* Completion queue error masks */
#define CQ_WR_DISABLE                BIT(25)
#define CQ_WR_FULL                   BIT(26)
#define CQ_WR_FAULT                  BIT(24)
#define CQ_ERR_MASK                  (CQ_WR_FULL | CQ_WR_DISABLE | CQ_WR_FAULT)

/* RSS */
#define RSS_IP_HASH_ENA              BIT(1)
#define RSS_TCP_HASH_ENA             BIT(2)
#define RSS_UDP_HASH_ENA             BIT(4)
#define RSS_L2_EXTENDED_HASH_ENA     BIT(0)
#define RSS_L4_EXTENDED_HASH_ENA     BIT(5)
#define RSS_ROCE_ENA                 BIT(6)
#define RSS_L3_BI_DIRECTION_ENA      BIT(7)
#define RSS_L4_BI_DIRECTION_ENA      BIT(8)
#define RSS_TCP_SYN_DIS              BIT(3)
#define RSS_HASH_KEY_SIZE            5
#define NIC_MAX_RSS_HASH_BITS        8
#define NIC_MAX_RSS_IDR_TBL_SIZE     (1 << NIC_MAX_RSS_HASH_BITS)
#define RSS_IND_TBL_LEN_PER_MBX_MSG  8

/* Sizes and lengths */
#define DMA_BUFFER_LEN               1536
#define CMP_QUEUE_DESC_SIZE          512
#define SND_QUEUE_DESC_SIZE          16
#define MIN_SQ_DESC_PER_PKT_XMIT     2
#define NIC_HW_MAX_FRS               9190
#define NIC_HW_MIN_FRS               64
#define CMP_QUEUE_TIMER_THRESH       80
#define CMP_QUEUE_PIPELINE_RSVD      544
#define SND_QUEUE_THRESH             2ULL
#define RBDR_THRESH                  (RCV_BUF_COUNT / 2)
#define NICVF_RCV_BUF_ALIGN          7
#define NICVF_RCV_BUF_ALIGN_BYTES    (1ULL << NICVF_RCV_BUF_ALIGN)
#define NICVF_SQ_BASE_ALIGN_BYTES    128
#define NICVF_CQ_BASE_ALIGN_BYTES    512
#define SND_QUEUE_SIZE0              0ULL
#define CMP_QUEUE_SIZE0              0ULL
#define RBDR_SIZE0                   0ULL
#define SND_QUEUE_SIZE6              6ULL
#define CMP_QUEUE_SIZE6              6ULL
#define SND_QUEUE_LEN                (1ULL << (SND_QUEUE_SIZE0 + 10))
#define CMP_QUEUE_LEN                (1ULL << (CMP_QUEUE_SIZE0 + 10))
#define MAX_SND_QUEUE_LEN            (1ULL << (SND_QUEUE_SIZE6 + 10))
#define MAX_CMP_QUEUE_LEN            (1ULL << (CMP_QUEUE_SIZE6 + 10))
#define MIN_SND_QUEUE_LEN            (1ULL << (SND_QUEUE_SIZE0 + 10))
#define MIN_CMP_QUEUE_LEN            (1ULL << (CMP_QUEUE_SIZE0 + 10))
#define RCV_BUF_COUNT                (1ULL << (RBDR_SIZE0 + 13))
#define DEFAULT_RBDR_CNT             1

/* BGX stats */
#define BGX_TX_STATS_COUNT           18
#define BGX_RX_STATS_COUNT           11

/* Mailbox message flags for XCAST */
#define BGX_XCAST_MCAST_FILTER       BIT(2)
#define BGX_XCAST_BCAST_ACCEPT       BIT(0)
#define BGX_XCAST_MCAST_ACCEPT       BIT(1)

/* PCI subsystem IDs */
#define PCI_SUBSYS_DEVID_88XX_NIC_PF 0xA11E
#define PCI_SUBSYS_DEVID_83XX_NIC_VF 0xA334
#define PCI_SUBSYS_DEVID_81XX_NIC_VF 0xA234
#define PCI_DEVICE_ID_THUNDER_PASS1_NIC_VF 0x0011
#define PCI_SUBSYS_DEVID_88XX_PASS1_NIC_VF 0xA11E

/* Enum-like defines for CPI algorithm */
#define CPI_ALG_NONE                 0

/* Mailbox message structures (packed, little-endian) */
typedef struct __attribute__((packed)) {
    uint8_t msg;
    uint8_t vf_id;
    uint8_t node_id;
    uint8_t tns_mode:1;
    uint8_t sqs_mode:1;
    uint8_t loopback_supported:1;
    uint8_t reserved:5;
    uint8_t mac_addr[6];
} nic_cfg_msg_t;

/* Bitfield definitions for register layouts (little-endian) */

/* qs_cfg: 64-bit queue set configuration */
struct qs_cfg {
    uint64_t vnic:7;
    uint64_t reserved_7_15:9;
    uint64_t be:1;
    uint64_t send_tstmp_ena:1;
    uint64_t lock_viol_cqe_ena:1;
    uint64_t lock_ena:1;
    uint64_t sq_ins_pos:6;
    uint64_t sq_ins_ena:1;
    uint64_t reserved_27_30:4;
    uint64_t ena:1;
    uint64_t reserved_32_63:32;
};

/* cq_cfg: 64-bit completion queue configuration */
struct cq_cfg {
    uint64_t reserved_0_15:16;
    uint64_t avg_con:9;
    uint64_t reserved_25_31:7;
    uint64_t qsize:3;
    uint64_t reserved_35_39:5;
    uint64_t caching:1;
    uint64_t reset:1;
    uint64_t ena:1;
    uint64_t reserved_43_63:21;
};

/* sq_cfg: 64-bit send queue configuration */
struct sq_cfg {
    uint64_t tstmp_bgx_intf:3;
    uint64_t reserved_3_7:5;
    uint64_t qsize:3;
    uint64_t reserved_11_15:5;
    uint64_t ldwb:1;
    uint64_t reset:1;
    uint64_t reserved_18_18:1;
    uint64_t ena:1;
    uint64_t reserved_20_23:4;
    uint64_t cq_limit:8;
    uint64_t reserved_32_63:32;
};

/* rq_cfg: 64-bit receive queue configuration */
struct rq_cfg {
    uint64_t tcp_ena:1;
    uint64_t ena:1;
    uint64_t reserved_2_63:62;
};

/* rbdr_cfg: 64-bit receive buffer descriptor ring configuration */
struct rbdr_cfg {
    uint64_t lines:12;
    uint64_t reserved_12_15:4;
    uint64_t avg_con:9;
    uint64_t reserved_25_31:7;
    uint64_t qsize:4;
    uint64_t reserved_36_41:6;
    uint64_t ldwb:1;
    uint64_t reset:1;
    uint64_t ena:1;
    uint64_t reserved_45_63:19;
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
    uint32_t intr_status;   /* pending interrupts */
    uint32_t intr_mask;     /* mask to disable specific interrupts */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint64_t regs[NIC_VF_REG_COUNT]; /* indexed by (offset >> 3); offset must be 8-byte aligned */

    /* DMA Context (to be elaborated later) */
    /* #DMA_Info_Stru# */

    /* Operational status flags */
    uint32_t status;

    /* #Probe_Reset_Stru# */
    /* #Pow_Man_Stru# */
    /* #Other_Addition_Info_Stru# */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* #IRQ_Update_Logic# */
    uint32_t pending = s->intr_status & s->intr_mask;
    if (pending) {
        /* Notify the appropriate MSI-X vectors based on pending bits */
        for (int bit = 0; bit < 24; bit++) {
            if (pending & (1u << bit)) {
                int vector = -1;
                if (bit >= 0 && bit <= 7) {
                    vector = bit; /* CQ interrupts */
                } else if (bit >= 16 && bit <= 17) {
                    vector = 16 + (bit - 16); /* RBDR interrupts */
                } else if (bit == 22) {
                    vector = 18; /* MISC (mailbox) */
                } else if (bit == 23) {
                    vector = 19; /* QS error */
                }
                if (vector >= 0 && vector < NIC_VF_MSIX_VECTORS) {
                    msix_notify(pdev, vector);
                }
            }
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* #DMA_Transfer_Logic# */
}

/* Mailbox handling */
static void nicvf_process_mbx(PCIBaseState *s)
{
    /* Mailbox register pointer (two 64-bit words) */
    uint64_t *mbx = s->regs + (0x130 >> 3);
    uint8_t msg_type = mbx[0] & 0xFF;

    /* Prepare response (two 64-bit words) */
    uint64_t resp[2] = {0};

    switch (msg_type) {
    case NIC_MBOX_MSG_READY:
        {
            nic_cfg_msg_t cfg;
            memset(&cfg, 0, sizeof(cfg));
            cfg.msg = NIC_MBOX_MSG_READY;
            cfg.vf_id = 0;   /* default VF id */
            cfg.node_id = 0;
            cfg.tns_mode = 0;
            cfg.sqs_mode = 0;
            cfg.loopback_supported = 1;
            /* Default MAC: 00:11:22:33:44:55 */
            uint8_t defmac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
            memcpy(cfg.mac_addr, defmac, 6);
            /* Copy configuration struct into the mailbox response (will occupy first 10 bytes) */
            memcpy(resp, &cfg, sizeof(cfg));
        }
        break;
    case NIC_MBOX_MSG_NICVF_PTR:
        resp[0] = NIC_MBOX_MSG_ACK;
        break;
    case NIC_MBOX_MSG_CFG_DONE:
    case NIC_MBOX_MSG_ALLOC_SQS:
    case NIC_MBOX_MSG_BGX_STATS:
    case NIC_MBOX_MSG_RSS_SIZE:
    case NIC_MBOX_MSG_SET_MAC:
    case NIC_MBOX_MSG_CPI_CFG:
    case NIC_MBOX_MSG_RSS_CFG:
    case NIC_MBOX_MSG_RSS_CFG_CONT:
    case NIC_MBOX_MSG_SET_MAX_FRS:
    case NIC_MBOX_MSG_LOOPBACK:
    case NIC_MBOX_MSG_RESET_XCAST:
    case NIC_MBOX_MSG_ADD_MCAST:
    case NIC_MBOX_MSG_SET_XCAST:
    case NIC_MBOX_MSG_SHUTDOWN:
    case NIC_MBOX_MSG_PTP_CFG:
    case NIC_MBOX_MSG_PFC:
        resp[0] = NIC_MBOX_MSG_ACK;
        break;
    default:
        resp[0] = NIC_MBOX_MSG_NACK;
        break;
    }

    /* Store response into shadow registers */
    s->regs[0x130 >> 3] = resp[0];
    s->regs[0x138 >> 3] = resp[1];

    /* Set mailbox interrupt status bit */
    s->intr_status |= (1u << NICVF_INTR_MBOX_SHIFT);
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* #MMIO_Read_Func# */
    switch (addr) {
    case NIC_VF_INT:
        val = s->intr_status;
        break;
    case NIC_VF_ENA_W1C:
    case NIC_VF_ENA_W1S:
        val = s->intr_mask;
        break;
    case NIC_VF_INT_W1S:
        /* Write-1-to-set register, read may not be defined; return 0 */
        break;
    default:
        /* For other offsets, just return what's in the shadow array if within bounds */
        if ((addr >> 3) < NIC_VF_REG_COUNT) {
            val = s->regs[addr >> 3];
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* #MMIO_Write_Func# */
    switch (addr) {
    case NIC_VF_INT:
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case NIC_VF_INT_W1S:
        s->intr_status |= val;
        pcibase_update_irq(s);
        break;
    case NIC_VF_ENA_W1C:
        s->intr_mask &= ~val;
        pcibase_update_irq(s);
        break;
    case NIC_VF_ENA_W1S:
        s->intr_mask |= val;
        pcibase_update_irq(s);
        break;
    case NIC_VF_PF_MAILBOX_0_1:
    case NIC_VF_PF_MAILBOX_0_1 + 8:
        /* Write to mailbox registers */
        if ((addr >> 3) < NIC_VF_REG_COUNT) {
            s->regs[addr >> 3] = val;
        }
        /* If both mailbox words written (offset 0x138), process message */
        if (addr == NIC_VF_PF_MAILBOX_0_1 + 8) {
            nicvf_process_mbx(s);
        }
        break;
    default:
        if ((addr >> 3) < NIC_VF_REG_COUNT) {
            s->regs[addr >> 3] = val;
        }
        break;
    }
}

/* PIO handlers deleted - driver does not use PIO */

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 8, .max_access_size = 8 },
    .impl  = { .min_access_size = 8, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* #Reset_Func# */
    s->intr_status = 0;
    s->intr_mask = 0;
    memset(s->regs, 0, sizeof(s->regs));
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
        /* Not used - PIO deleted */
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Define constants missing from driver source but needed for device */
#define PCI_BAR0_SIZE               (2 * 1024 * 1024)   /* 2 MB */
#define MSIX_BAR_INDEX              1

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_THUNDER_NIC_VF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NICVF);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    /* BAR0: MMIO register space */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PCI_BAR0_SIZE;
    s->bar_info[0].name = "nicvf-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    MemoryRegion *msix_bar = &s->bar_regions[MSIX_BAR_INDEX];
    memory_region_init_io(msix_bar, OBJECT(s), NULL, NULL, "nicvf-msix", MSIX_BAR_SIZE);
    if (msix_init(pdev, NIC_VF_MSIX_VECTORS,
                  msix_bar, MSIX_BAR_INDEX, 0,
                  msix_bar, MSIX_BAR_INDEX, 0, 0, errp)) {
        error_setg(errp, "Failed to initialize MSI-X");
        return;
    }

    /* #DMA_Config_Real# */
    /* #Timer_Config_Real# (deleted - no timers) */
    /* #Field_Init_Real# */
    s->intr_status = 0;
    s->intr_mask = 0;
    memset(s->regs, 0, sizeof(s->regs));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* #Uninit_Func# */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "nicvf_pci",
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
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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
