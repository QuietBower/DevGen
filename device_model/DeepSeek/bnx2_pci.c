/*
 * QEMU PCI device model for Broadcom bnx2 (minimal behavioral emulation)
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

#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "bnx2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Provide local definitions for IDs if headers don't define them */
#ifndef PCI_VENDOR_ID_BROADCOM
#define PCI_VENDOR_ID_BROADCOM 0x14e4
#endif

#ifndef PCI_DEVICE_ID_NX2_5706
#define PCI_DEVICE_ID_NX2_5706 0x164a
#endif

#define BNX2_VENDOR_ID PCI_VENDOR_ID_BROADCOM
#define BNX2_DEVICE_ID PCI_DEVICE_ID_NX2_5706
#define BNX2_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

#define BNX2_PCICFG_REG_WINDOW_ADDRESS            0x00000078
#define BNX2_PCICFG_REG_WINDOW                    0x00000080
#define BNX2_CTX_DATA_ADR                         0x00001010
#define BNX2_CTX_DATA                             0x00001014
#define BNX2_CTX_CTX_CTRL                         0x0000101c
#define BNX2_CTX_CTX_DATA                         0x00001020
#define BNX2_CTX_HOST_PAGE_TBL_CTRL               0x000010c8
#define BNX2_CTX_HOST_PAGE_TBL_DATA0              0x000010cc
#define BNX2_CTX_HOST_PAGE_TBL_DATA1              0x000010d0
#define BNX2_CTX_COMMAND                          0x00001000
#define BNX2_CTX_PAGE_TBL                         0x0000100c
#define BNX2_CTX_VIRT_ADDR                        0x00001008
#define BNX2_RBUF_FW_BUF_FREE                     0x00200014
#define BNX2_RBUF_COMMAND                         0x00200000
#define BNX2_RBUF_STATUS1                         0x00200004
#define BNX2_RBUF_FW_BUF_ALLOC                    0x00200010
#define BNX2_RBUF_CONFIG3                         0x00200020
#define BNX2_RBUF_CONFIG2                         0x0020001c
#define BNX2_RBUF_CONFIG                          0x0020000c
#define BNX2_EMAC_MAC_MATCH1                      0x00001414
#define BNX2_EMAC_MAC_MATCH0                      0x00001410
#define BNX2_PCICFG_STATUS_BIT_SET_CMD            0x00000088
#define BNX2_PCICFG_STATUS_BIT_CLEAR_CMD          0x0000008c
#define BNX2_EMAC_TX_MODE                         0x000014bc
#define BNX2_EMAC_RX_MODE                         0x000014c8
#define BNX2_EMAC_TX_LENGTHS                      0x000014c4
#define BNX2_EMAC_STATUS                          0x00001404
#define BNX2_EMAC_MODE                            0x00001400
#define BNX2_EMAC_ATTENTION_ENA                   0x00001408
#define BNX2_FW_MB                                0x00000008
#define BNX2_DEV_INFO_SIGNATURE                   0x00000020
#define BNX2_DRV_MB                               0x00000004
#define BNX2_BC_STATE_RESET_TYPE                  0x000001c0
#define BNX2_MCP_CPU_MODE                         0x00145000
#define BNX2_MCP_CPU_STATE                        0x00145004
#define BNX2_MCP_CPU_EVENT_MASK                   0x00145008
#define BNX2_MCP_CPU_INSTRUCTION                  0x00145020
#define BNX2_MCP_CPU_PROGRAM_COUNTER              0x0014501c
#define BNX2_BC_STATE_CONDITION                   0x000001c8
#define BNX2_MCP_STATE_P1                         0x0016f9c8
#define BNX2_MCP_STATE_P0                         0x0016fdc8
#define BNX2_MCP_STATE_P0_5708                    0x00169dc8
#define BNX2_MCP_STATE_P1_5708                    0x001699c8
#define BNX2_DRV_PULSE_MB                         0x00000010
#define BNX2_DRV_RESET_SIGNATURE                  0x00000000
#define BNX2_BC_RESET_TYPE                        0x000001c0
#define BNX2_MISC_ENABLE_SET_BITS                 0x00000810
#define BNX2_MISC_ENABLE_CLR_BITS                 0x00000814
#define BNX2_MISC_ENABLE_DEFAULT                  0x17ffffff
#define BNX2_MISC_COMMAND                         0x00000800
#define BNX2_MISC_ID                              0x00000808
#define BNX2_MISC_CFG                             0x00000804
#define BNX2_MISC_ECO_HW_CTL                      0x000008cc
#define BNX2_MISC_NEW_CORE_CTL                    0x000008c8
#define BNX2_MISC_VREG_CONTROL                    0x000008b4
#define BNX2_MISC_DUAL_MEDIA_CTRL                 0x000008ec
#define BNX2_PCICFG_MISC_STATUS                   0x0000006c
#define BNX2_PCICFG_MSI_CONTROL                   0x00000058
#define BNX2_PCICFG_DEVICE_CONTROL                0x000000b4
#define BNX2_PCICFG_MISC_CONFIG                   0x00000068
#define BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS        0x00000070
#define BNX2_PCICFG_INT_ACK_CMD                   0x00000084
#define BNX2_PCI_GRC_WINDOW_ADDR                  0x00000400
#define BNX2_PCI_CONFIG_3                         0x0000040c
#define BNX2_PCI_SWAP_DIAG0                       0x00000418
#define BNX2_PCI_GRC_WINDOW2_ADDR                 0x00000614
#define BNX2_PCI_GRC_WINDOW3_ADDR                 0x00000618
#define BNX2_HC_COMMAND                           0x00006800
#define BNX2_HC_STATUS_ADDR_L                     0x00006810
#define BNX2_HC_STATUS_ADDR_H                     0x00006814
#define BNX2_HC_STATISTICS_ADDR_L                 0x00006818
#define BNX2_HC_STATISTICS_ADDR_H                 0x0000681c
#define BNX2_HC_TX_QUICK_CONS_TRIP                0x00006820
#define BNX2_HC_RX_QUICK_CONS_TRIP                0x00006828
#define BNX2_HC_RX_TICKS                          0x0000682c
#define BNX2_HC_TX_TICKS                          0x00006830
#define BNX2_HC_COM_TICKS                         0x00006834
#define BNX2_HC_CMD_TICKS                         0x00006838
#define BNX2_HC_STAT_COLLECT_TICKS                0x00006840
#define BNX2_HC_STATS_TICKS                       0x00006844
#define BNX2_HC_STATS_INTERRUPT_STATUS            0x00006848
#define BNX2_HC_ATTN_BITS_ENABLE                  0x0000680c
#define BNX2_HC_CONFIG                            0x00006808
#define BNX2_HC_SB_CONFIG_1                       0x00006a00
#define BNX2_HC_SB_CONFIG_2                       0x00006a24
#define BNX2_HC_RX_QUICK_CONS_TRIP_1              0x00006a0c
#define BNX2_HC_TX_QUICK_CONS_TRIP_1              0x00006a04
#define BNX2_HC_TX_TICKS_1                        0x00006a14
#define BNX2_HC_RX_TICKS_1                        0x00006a10
#define BNX2_DMA_CONFIG                           0x00000c08
#define BNX2_TDMA_CONFIG                          0x00005c08
#define BNX2_TBDR_CONFIG                          0x00005008
#define BNX2_MQ_CONFIG                            0x00003c08
#define BNX2_MQ_KNL_BYP_WIND_START                0x00003c1c
#define BNX2_MQ_KNL_WIND_END                      0x00003c20
#define BNX2_MQ_MAP_L2_3                          0x00003d2c
#define BNX2_MQ_MAP_L2_5                          0x00003d34
#define BNX2_RLUP_RSS_CONFIG                      0x0000201c
#define BNX2_RLUP_RSS_COMMAND                     0x00002048
#define BNX2_RLUP_RSS_DATA                        0x0000204c
#define BNX2_TSCH_TSS_CFG                         0x00004c1c
#define BNX2_RPM_CONFIG                           0x00001808
#define BNX2_RPM_SORT_USER0                       0x00001820
#define BNX2_RPM_MGMT_PKT_CTRL                    0x0000180c
#define BNX2_EMAC_BACKOFF_SEED                    0x00001498
#define BNX2_EMAC_RX_MTU_SIZE                     0x0000149c
#define BNX2_EMAC_LED                             0x0000140c
#define BNX2_EMAC_RX_STATUS                       0x000014cc
#define BNX2_EMAC_TX_STATUS                       0x000014c0
#define BNX2_EMAC_MULTICAST_HASH0                 0x000014d0
#define BNX2_PORT_FEATURE                         0x000000d8
#define BNX2_PORT_HW_CFG_CONFIG                   0x00000058
#define BNX2_PORT_HW_CFG_MAC_UPPER                0x00000050
#define BNX2_PORT_HW_CFG_MAC_LOWER                0x00000054
#define BNX2_SHARED_HW_CFG_CONFIG                 0x0000003c
#define BNX2_SHARED_HW_CFG_CONFIG2                0x00000040
#define BNX2_MISC_GP_HW_CTL0                      0x000008bc
#define BNX2_NVM_SW_ARB                           0x00006420
#define BNX2_NVM_COMMAND                          0x00006400
#define BNX2_NVM_ACCESS_ENABLE                    0x00006424
#define BNX2_NVM_ADDR                             0x0000640c
#define BNX2_NVM_READ                             0x00006410
#define BNX2_NVM_WRITE                            0x00006408
#define BNX2_NVM_CFG1                             0x00006414
#define BNX2_NVM_CFG2                             0x00006418
#define BNX2_NVM_CFG3                             0x0000641c
#define BNX2_NVM_WRITE1                           0x00006428
#define BNX2_DRV_ACK_CAP_MB                       0x00000364
#define BNX2_FW_CAP_MB                            0x00000368
#define BNX2_PCI_MSIX_CONTROL                     0x000004c0
#define BNX2_PCI_MSIX_TBL_OFF_BIR                 0x000004c4
#define BNX2_PCI_MSIX_PBA_OFF_BIT                 0x000004c8
#define BNX2_MSIX_TABLE_ADDR                      0x00318000
#define BNX2_MSIX_PBA_ADDR                        0x0031c000
#define BNX2_HC_MSIX_BIT_VECTOR                   0x00006918
#define BNX2_RV2P_INSTR_HIGH                      0x00002830
#define BNX2_RV2P_INSTR_LOW                       0x00002834
#define BNX2_RV2P_COMMAND                         0x00002800
#define BNX2_RV2P_PROC1_ADDR_CMD                  0x00002838
#define BNX2_RV2P_PROC2_ADDR_CMD                  0x0000283c
#define BNX2_RV2P_CONFIG                          0x00002808
#define BNX2_TXP_CPU_MODE                         0x00045000
#define BNX2_CP_CPU_MODE                          0x00185000
#define BNX2_TBDC_COMMAND                         0x00005400
#define BNX2_TBDC_STATUS                          0x00005404
#define BNX2_TBDC_BD_ADDR                         0x00005424
#define BNX2_TBDC_BIDX                            0x0000542c
#define BNX2_TBDC_CID                             0x00005430
#define BNX2_TBDC_CAM_OPCODE                      0x00005434
#define BNX2_MCP_TOE_ID                           0x001400a0
#define BNX2_FW_EVT_CODE_MB                       0x00000354
#define BNX2_FW_RX_LOW_LATENCY                    0x00120058
#define BNX2_FW_RX_DROP_COUNT                     0x00120084
#define BNX2_FW_MAX_ISCSI_CONN                    0x001a0080
#define BNX2_RXP_SCRATCH_RSS_TBL_SZ               0x000e0038
#define BNX2_RPHY_SERDES_LINK                     0x00000374
#define BNX2_RPHY_COPPER_LINK                     0x00000378
#define BNX2_BNX2_MCP_SCRATCH_BASE                0x00160000
#define BNX2_SHM_HDR_SIGNATURE                    BNX2_MCP_SCRATCH
#define BNX2_SHM_HDR_ADDR_0                       (BNX2_MCP_SCRATCH + 4)
#define BNX2_MFW_VER_PTR                          0x0000014c

#define BNX2_LINK_STATUS                      0x0000000c
#define BNX2_MCP_SCRATCH                      0x00160000

#define CONFIG_PAGE_SHIFT 12
#define PAGE_SHIFT      CONFIG_PAGE_SHIFT

struct bnx2_tx_bd {
    uint32_t tx_bd_haddr_hi;
    uint32_t tx_bd_haddr_lo;
    uint32_t tx_bd_mss_nbytes;
    uint32_t tx_bd_vlan_tag_flags;
};

struct bnx2_rx_bd {
    uint32_t rx_bd_haddr_hi;
    uint32_t rx_bd_haddr_lo;
    uint32_t rx_bd_len;
    uint32_t rx_bd_flags;
};

#define BNX2_PAGE_BITS    PAGE_SHIFT
#define BNX2_PAGE_SIZE    (1 << BNX2_PAGE_BITS)

#define BNX2_TX_DESC_CNT  (BNX2_PAGE_SIZE / sizeof(struct bnx2_tx_bd))
#define BNX2_MAX_TX_DESC_CNT (BNX2_TX_DESC_CNT - 1)
#define BNX2_RX_DESC_CNT  (BNX2_PAGE_SIZE / sizeof(struct bnx2_rx_bd))
#define BNX2_MAX_RX_DESC_CNT (BNX2_RX_DESC_CNT - 1)

#define BNX2_HC_SB_CONFIG_SIZE (BNX2_HC_SB_CONFIG_2 - BNX2_HC_SB_CONFIG_1)

/* Some minimal status bit definitions used in driver */
#define STATUS_ATTN_BITS_LINK_STATE    0x00000001
#define STATUS_ATTN_BITS_TIMER_ABORT   0x00000002

/* Simplified link status values used with BNX2_LINK_STATUS */
#define BNX2_LINK_STATUS_LINK_UP       0x00000001
#define BNX2_LINK_STATUS_LINK_DOWN     0x00000000

/* simple EMAC status bits */
#define BNX2_EMAC_STATUS_LINK          0x00000001
#define BNX2_EMAC_STATUS_LINK_CHANGE   0x00000001

/* INT_ACK_CMD bits used in driver */
#define BNX2_PCICFG_INT_ACK_CMD_MASK_INT        0x00000001
#define BNX2_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM 0x00000002
#define BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID     0x00000004

/* HC_COMMAND bits */
#define BNX2_HC_COMMAND_COAL_NOW               0x00000001
#define BNX2_HC_COMMAND_COAL_NOW_WO_INT        0x00000002
#define BNX2_HC_COMMAND_STATS_NOW              0x00000004

/* DMA config simple bits */
#define BNX2_DMA_CONFIG_DATA_BYTE_SWAP         0x00000001
#define BNX2_DMA_CONFIG_DATA_WORD_SWAP         0x00000002
#define BNX2_DMA_CONFIG_CNTL_WORD_SWAP         0x00000008

/* Misc enable bits used in init */
#define BNX2_MISC_ENABLE_SET_BITS_RX_MBUF_ENABLE           0x00000001
#define BNX2_MISC_ENABLE_SET_BITS_HOST_COALESCE_ENABLE     0x00000002
#define BNX2_MISC_ENABLE_STATUS_BITS_RX_V2P_ENABLE         0x00000004
#define BNX2_MISC_ENABLE_STATUS_BITS_CONTEXT_ENABLE        0x00000008

/* HC_CONFIG ATTENTION events */
#define STATUS_ATTN_EVENTS (STATUS_ATTN_BITS_LINK_STATE | STATUS_ATTN_BITS_TIMER_ABORT)

/* Simple DRV/FW message fields */
#define BNX2_FW_CAP_SIGNATURE            0x46574350
#define BNX2_FW_CAP_SIGNATURE_MASK       0xffff0000
#define BNX2_FW_CAP_CAN_KEEP_VLAN        0x00000001
#define BNX2_FW_CAP_REMOTE_PHY_CAPABLE   0x00000002
#define BNX2_DRV_ACK_CAP_SIGNATURE       0x00001000

/* DEV_INFO signature magic from driver */
#define BNX2_DEV_INFO_SIGNATURE_MAGIC          0x44564900

/* Very small host-status block used only for status_idx and attn bits */
struct bnx2_status_block {
    uint16_t status_tx_quick_consumer_index0;
    uint16_t status_rx_quick_consumer_index0;
    uint16_t status_idx;
    uint16_t _rsv0;
    uint32_t status_attn_bits;
    uint32_t status_attn_bits_ack;
};

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
        uint32_t misc_id;
        uint32_t misc_cfg;
        uint32_t link_status;
    } regs;

    /* simple internal register shadows for frequently accessed regs */
    uint32_t reg_window_addr;
    uint32_t reg_window_data;

    uint32_t emac_mode;
    uint32_t emac_status;
    uint32_t emac_rx_mode;
    uint32_t emac_tx_mode;
    uint32_t emac_led;

    uint32_t hc_command;
    uint32_t hc_config;
    uint32_t hc_attn_bits_enable;

    uint32_t pcicfg_misc_status;
    uint32_t pcicfg_int_ack_cmd;
    uint32_t pcicfg_msi_control;
    uint32_t pcicfg_misc_config;

    uint32_t misc_enable_set_bits;
    uint32_t misc_enable_clr_bits;

    uint32_t port_hw_cfg_mac_upper;
    uint32_t port_hw_cfg_mac_lower;

    uint32_t shmem_base;

    /* simple scratch memory to back shmem accesses (size 64KB for now) */
    uint32_t *shmem;
    size_t shmem_words;

    /* status/statistics memory accessed via HC_STATUS_ADDR_L/H etc. */
    dma_addr_t status_dma_addr;
    dma_addr_t stats_dma_addr;

    /* pseudo status block we maintain for interrupts */
    struct bnx2_status_block sblk;

    /* link / flow state */
    bool link_up;
};

static inline uint32_t pcibase_shmem_offset(PCIBaseState *s, uint32_t addr)
{
    if (addr < s->shmem_base) {
        return 0xffffffffu;
    }
    return (addr - s->shmem_base) >> 2;
}

static inline void pcibase_shmem_write32(PCIBaseState *s, uint32_t addr, uint32_t val)
{
    uint32_t off = pcibase_shmem_offset(s, addr);
    if (off == 0xffffffffu || off >= s->shmem_words) {
        return;
    }
    s->shmem[off] = cpu_to_be32(val);
}

static inline uint32_t pcibase_shmem_read32(PCIBaseState *s, uint32_t addr)
{
    uint32_t off = pcibase_shmem_offset(s, addr);
    if (off == 0xffffffffu || off >= s->shmem_words) {
        return 0;
    }
    return be32_to_cpu(s->shmem[off]);
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    bool asserted = (s->intr_status & ~s->intr_mask) != 0;

    if (asserted) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint32_t pcibase_read_indirect(PCIBaseState *s, uint32_t regaddr)
{
    /* Implement minimal subset of indirect space the driver touches */
    switch (regaddr) {
    case BNX2_DEV_INFO_SIGNATURE:
        return BNX2_DEV_INFO_SIGNATURE_MAGIC; /* simple magic so driver thinks FW up */
    case BNX2_LINK_STATUS:
        return s->regs.link_status;
    case BNX2_PORT_FEATURE:
        return 0; /* no WOL, no ASF */
    default:
        /* For other addresses, proxy to shmem backing if in that range */
        return pcibase_shmem_read32(s, regaddr);
    }
}

static void pcibase_write_indirect(PCIBaseState *s, uint32_t regaddr, uint32_t val)
{
    if (regaddr == BNX2_DRV_PULSE_MB) {
        pcibase_shmem_write32(s, regaddr, val);
        return;
    }
    pcibase_shmem_write32(s, regaddr, val);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t off = (uint32_t)addr;
    uint32_t val = 0;

    if (size == 1) {
        return 0xff;
    }

    switch (off) {
    case BNX2_MISC_ID:
        val = 0x57060000; /* fake chip id */
        break;
    case BNX2_MISC_CFG:
        val = s->regs.misc_cfg;
        break;
    case BNX2_PCICFG_MISC_STATUS:
        val = s->pcicfg_misc_status;
        break;
    case BNX2_PCICFG_MISC_CONFIG:
        val = s->pcicfg_misc_config;
        break;
    case BNX2_PCICFG_MSI_CONTROL:
        val = s->pcicfg_msi_control;
        break;
    case BNX2_PCICFG_REG_WINDOW_ADDRESS:
    case BNX2_PCICFG_REG_WINDOW_ADDRESS + 4:
        val = s->reg_window_addr;
        break;
    case BNX2_PCICFG_REG_WINDOW:
    case BNX2_PCICFG_REG_WINDOW + 4:
        val = pcibase_read_indirect(s, s->reg_window_addr);
        s->reg_window_data = val;
        break;
    case BNX2_EMAC_MODE:
        val = s->emac_mode;
        break;
    case BNX2_EMAC_STATUS:
        val = s->emac_status;
        break;
    case BNX2_EMAC_RX_MODE:
        val = s->emac_rx_mode;
        break;
    case BNX2_EMAC_TX_MODE:
        val = s->emac_tx_mode;
        break;
    case BNX2_EMAC_LED:
        val = s->emac_led;
        break;
    case BNX2_HC_COMMAND:
        val = s->hc_command;
        break;
    case BNX2_HC_CONFIG:
        val = s->hc_config;
        break;
    case BNX2_HC_ATTN_BITS_ENABLE:
        val = s->hc_attn_bits_enable;
        break;
    /* BNX2_PCICFG_INT_ACK_CMD is write-only in this minimal model; fall through to default returning 0 */
    case BNX2_MISC_ENABLE_SET_BITS:
        val = s->misc_enable_set_bits;
        break;
    case BNX2_MISC_ENABLE_CLR_BITS:
        val = s->misc_enable_clr_bits;
        break;
    case BNX2_PORT_HW_CFG_MAC_UPPER:
        val = s->port_hw_cfg_mac_upper;
        break;
    case BNX2_PORT_HW_CFG_MAC_LOWER:
        val = s->port_hw_cfg_mac_lower;
        break;
    case BNX2_HC_STATS_INTERRUPT_STATUS:
        /* just return 0, no stats interrupts */
        val = 0;
        break;
    case BNX2_PCI_SWAP_DIAG0:
        val = 0x01020304;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t data, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t off = (uint32_t)addr;
    uint32_t val = (uint32_t)data;

    if (size != 4) {
        return;
    }

    switch (off) {
    case BNX2_MISC_CFG:
        s->regs.misc_cfg = val;
        break;
    case BNX2_PCICFG_MISC_CONFIG:
        s->pcicfg_misc_config = val;
        break;
    case BNX2_PCICFG_MSI_CONTROL:
        s->pcicfg_msi_control = val;
        break;
    case BNX2_PCICFG_REG_WINDOW_ADDRESS:
    case BNX2_PCICFG_REG_WINDOW_ADDRESS + 4:
        s->reg_window_addr = val;
        break;
    case BNX2_PCICFG_REG_WINDOW:
    case BNX2_PCICFG_REG_WINDOW + 4:
        pcibase_write_indirect(s, s->reg_window_addr, val);
        s->reg_window_data = val;
        break;
    case BNX2_EMAC_MODE:
        s->emac_mode = val;
        break;
    case BNX2_EMAC_STATUS:
        /* write-1-to-clear link change */
        s->emac_status &= ~val;
        break;
    case BNX2_EMAC_RX_MODE:
        s->emac_rx_mode = val;
        break;
    case BNX2_EMAC_TX_MODE:
        s->emac_tx_mode = val;
        break;
    case BNX2_EMAC_LED:
        s->emac_led = val;
        break;
    default:
        if (off == BNX2_PCICFG_INT_ACK_CMD) {
            /* handle interrupt acknowledge writes */
            s->pcicfg_int_ack_cmd = val;
            if (val & BNX2_PCICFG_INT_ACK_CMD_MASK_INT) {
                /* driver acks interrupt, drop INTx */
                s->intr_status = 0;
                pcibase_update_irq(s);
            }
        } else if (off == BNX2_HC_COMMAND) {
            s->hc_command = val;
            if (val & (BNX2_HC_COMMAND_COAL_NOW | BNX2_HC_COMMAND_COAL_NOW_WO_INT)) {
                /* simulate that hardware has updated status_idx and attention bits */
                s->sblk.status_idx++;
                if (s->hc_attn_bits_enable & STATUS_ATTN_BITS_LINK_STATE) {
                    s->sblk.status_attn_bits ^= STATUS_ATTN_BITS_LINK_STATE;
                }
            }
        } else if (off == BNX2_HC_CONFIG) {
            s->hc_config = val;
        } else if (off == BNX2_HC_ATTN_BITS_ENABLE) {
            s->hc_attn_bits_enable = val;
        } else if (off == BNX2_MISC_ENABLE_SET_BITS) {
            s->misc_enable_set_bits |= val;
        } else if (off == BNX2_MISC_ENABLE_CLR_BITS) {
            s->misc_enable_clr_bits |= val;
        } else if (off == BNX2_DMA_CONFIG) {
            /* accept writes but ignore */
        } else if (off == BNX2_TDMA_CONFIG ||
                   off == BNX2_TBDR_CONFIG ||
                   off == BNX2_MQ_CONFIG ||
                   off == BNX2_MQ_KNL_BYP_WIND_START ||
                   off == BNX2_MQ_KNL_WIND_END ||
                   off == BNX2_RPM_CONFIG ||
                   off == BNX2_RPM_SORT_USER0) {
            /* not modeled, just store nowhere */
        } else if (off == BNX2_PCI_GRC_WINDOW_ADDR ||
                   off == BNX2_PCI_GRC_WINDOW2_ADDR ||
                   off == BNX2_PCI_GRC_WINDOW3_ADDR) {
            /* ignore windowing for now */
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
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
    s->regs.misc_id = 0;
    s->regs.misc_cfg = 0;
    s->regs.link_status = BNX2_LINK_STATUS_LINK_UP;

    s->reg_window_addr = 0;
    s->reg_window_data = 0;
    s->emac_mode = 0;
    s->emac_status = BNX2_EMAC_STATUS_LINK;
    s->emac_rx_mode = 0;
    s->emac_tx_mode = 0;
    s->emac_led = 0;
    s->hc_command = 0;
    s->hc_config = 0;
    s->hc_attn_bits_enable = 0;
    s->pcicfg_misc_status = 0;
    s->pcicfg_int_ack_cmd = 0;
    s->pcicfg_msi_control = 0;
    s->pcicfg_misc_config = 0;
    s->misc_enable_set_bits = 0;
    s->misc_enable_clr_bits = 0;

    s->sblk.status_tx_quick_consumer_index0 = 0;
    s->sblk.status_rx_quick_consumer_index0 = 0;
    s->sblk.status_idx = 0;
    s->sblk.status_attn_bits = 0;
    s->sblk.status_attn_bits_ack = 0;

    s->link_up = true;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  BNX2_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  BNX2_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, BNX2_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Advertise conventional PCI-X capability so the driver does not abort */
    /* Use a realistic capability length (8 bytes) and fill minimal fields */
    int pcix_pos = pci_add_capability(pdev, PCI_CAP_ID_PCIX, 0, 8, errp);
    if (pcix_pos > 0) {
        /* Set command and status to benign non-zero values so driver sees cap */
        pci_set_word(pci_conf + pcix_pos + 2, 0x0000); /* PCI-X command */
        pci_set_word(pci_conf + pcix_pos + 4, 0x0000); /* PCI-X status low */
        pci_set_word(pci_conf + pcix_pos + 6, 0x0000); /* PCI-X status high/reserved */
    }

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100000;
    s->bar_info[0].name = "bnx2-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    s->has_msix = true;
    if (s->has_msix) {
        msix_init_exclusive_bar(pdev, 1, 0, errp);
    } else if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
    }

    s->intr_status = 0;
    s->intr_mask = 0;
    s->regs.misc_id = 0;
    s->regs.misc_cfg = 0;
    s->regs.link_status = BNX2_LINK_STATUS_LINK_UP;

    s->reg_window_addr = 0;
    s->reg_window_data = 0;
    s->emac_mode = 0;
    s->emac_status = BNX2_EMAC_STATUS_LINK;
    s->emac_rx_mode = 0;
    s->emac_tx_mode = 0;
    s->emac_led = 0;
    s->hc_command = 0;
    s->hc_config = 0;
    s->hc_attn_bits_enable = 0;
    s->pcicfg_misc_status = 0;
    s->pcicfg_int_ack_cmd = 0;
    s->pcicfg_msi_control = 0;
    s->pcicfg_misc_config = 0;
    s->misc_enable_set_bits = 0;
    s->misc_enable_clr_bits = 0;

    s->sblk.status_tx_quick_consumer_index0 = 0;
    s->sblk.status_rx_quick_consumer_index0 = 0;
    s->sblk.status_idx = 0;
    s->sblk.status_attn_bits = 0;
    s->sblk.status_attn_bits_ack = 0;

    s->link_up = true;

    /* allocate simple shmem backing (64KB) and set base to MCP scratch */
    s->shmem_words = (64 * KiB) / sizeof(uint32_t);
    s->shmem = g_malloc0(s->shmem_words * sizeof(uint32_t));
    s->shmem_base = BNX2_MCP_SCRATCH;

    /* Initialize DEV_INFO_SIGNATURE so driver sees firmware up */
    pcibase_shmem_write32(s, BNX2_DEV_INFO_SIGNATURE, BNX2_DEV_INFO_SIGNATURE_MAGIC);
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

    if (s->shmem) {
        g_free(s->shmem);
        s->shmem = NULL;
        s->shmem_words = 0;
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "bnx2_pci",
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

type_init(pcibase_register_types)
