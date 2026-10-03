/*
 * QEMU PCI device model for Broadcom tg3 (behavioral subset for driver bind)
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
/* Removed missing include: hw/net/tg3_regs.h */

/* Define missing PCI IDs locally to satisfy compiler; values chosen to match Broadcom Tigon3 5700 */
#ifndef PCI_VENDOR_ID_BROADCOM
#define PCI_VENDOR_ID_BROADCOM 0x14e4
#endif
#ifndef PCI_DEVICE_ID_TIGON3_5700
#define PCI_DEVICE_ID_TIGON3_5700 0x1644
#endif

#define TYPE_PCIBASE_DEVICE "tg3_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define TG3_PCI_VENDOR_ID   PCI_VENDOR_ID_BROADCOM
#define TG3_PCI_DEVICE_ID   PCI_DEVICE_ID_TIGON3_5700
#define TG3_PCI_CLASS_ID    PCI_CLASS_NETWORK_ETHERNET

/* Commonly used MMIO register offsets from tg3 driver */
#define TG3_REG_TG3PCI_VENDOR             0x00000000
#define TG3_REG_TG3PCI_MISC_HOST_CTRL     0x00000068
#define TG3_REG_TG3PCI_DMA_RW_CTRL        0x0000006c
#define TG3_REG_TG3PCI_PCISTATE           0x00000070
#define TG3_REG_TG3PCI_CLOCK_CTRL         0x00000074
#define TG3_REG_TG3PCI_REG_BASE_ADDR      0x00000078
#define TG3_REG_TG3PCI_MEM_WIN_BASE_ADDR  0x0000007c
#define TG3_REG_TG3PCI_REG_DATA           0x00000080
#define TG3_REG_TG3PCI_MEM_WIN_DATA       0x00000084
#define TG3_REG_TG3PCI_MISC_LOCAL_CTRL    0x00000090
#define TG3_REG_TG3PCI_STD_RING_PROD_IDX  0x00000098
#define TG3_REG_TG3PCI_RCV_RET_RING_CON_IDX 0x000000a0
#define TG3_REG_TG3PCI_DEV_STATUS_CTRL    0x000000b4
#define TG3_REG_TG3PCI_DUAL_MAC_CTRL      0x000000b8
#define TG3_REG_TG3PCI_PRODID_ASICREV     0x000000bc
#define TG3_REG_TG3_PCIE_TLDLPL_PORT      0x00007c00
#define TG3_REG_TG3_PCIE_LNKCTL           0x00007d54
#define TG3_REG_TG3_PCIE_PHY_TSTCTL       0x00007e2c
#define TG3_REG_TG3_PCIE_EIDLE_DELAY      0x00007e70
#define TG3_REG_TG3_REG_BLK_SIZE          0x00008000

#define TG3_REG_GRC_MODE                  0x00006800
#define TG3_REG_GRC_MISC_CFG              0x00006804
#define TG3_REG_GRC_LOCAL_CTRL            0x00006808
#define TG3_REG_GRC_RX_CPU_EVENT          0x00006810
#define TG3_REG_GRC_EEPROM_ADDR           0x00006838
#define TG3_REG_GRC_EEPROM_DATA           0x0000683c
#define TG3_REG_GRC_VCPU_EXT_CTRL         0x00006890
#define TG3_REG_GRC_FASTBOOT_PC           0x00006894

#define TG3_REG_HOSTCC_MODE               0x00003c00
#define TG3_REG_HOSTCC_STATUS_BLK_HOST_ADDR 0x00003c30
#define TG3_REG_HOSTCC_STATS_BLK_HOST_ADDR  0x00003c30
#define TG3_REG_HOSTCC_STATUS_BLK_NIC_ADDR  0x00003c44
#define TG3_REG_HOSTCC_FLOW_ATTN          0x00003c48
#define TG3_REG_HOSTCC_RXCOL_TICKS        0x00003c08
#define TG3_REG_HOSTCC_TXCOL_TICKS        0x00003c0c
#define TG3_REG_HOSTCC_RXMAX_FRAMES       0x00003c10
#define TG3_REG_HOSTCC_TXMAX_FRAMES       0x00003c14
#define TG3_REG_HOSTCC_RXCOAL_TICK_INT    0x00003c18
#define TG3_REG_HOSTCC_TXCOAL_TICK_INT    0x00003c1c
#define TG3_REG_HOSTCC_RXCOAL_MAXF_INT    0x00003c20
#define TG3_REG_HOSTCC_TXCOAL_MAXF_INT    0x00003c24
#define TG3_REG_HOSTCC_STAT_COAL_TICKS    0x00003c28

#define TG3_REG_MAC_MODE                  0x00000400
#define TG3_REG_MAC_STATUS                0x00000404
#define TG3_REG_MAC_EVENT                 0x00000408
#define TG3_REG_MAC_LED_CTRL              0x0000040c
#define TG3_REG_MAC_ADDR_0_HIGH           0x00000410
#define TG3_REG_MAC_ADDR_0_LOW            0x00000414
#define TG3_REG_MAC_ADDR_1_HIGH           0x00000418
#define TG3_REG_MAC_ADDR_1_LOW            0x0000041c
#define TG3_REG_MAC_RX_MTU_SIZE           0x0000043c
#define TG3_REG_MAC_MI_STAT               0x00000450
#define TG3_REG_MAC_MI_COM                0x0000044c
#define TG3_REG_MAC_MI_MODE               0x00000454
#define TG3_REG_MAC_TX_MODE               0x0000045c
#define TG3_REG_MAC_TX_STATUS             0x00000460
#define TG3_REG_MAC_TX_LENGTHS            0x00000464
#define TG3_REG_MAC_RX_MODE               0x00000468
#define TG3_REG_MAC_HASH_REG_0            0x00000470
#define TG3_REG_MAC_HASH_REG_1            0x00000474
#define TG3_REG_MAC_HASH_REG_2            0x00000478
#define TG3_REG_MAC_HASH_REG_3            0x0000047c
#define TG3_REG_MAC_RCV_RULE_0            0x00000480
#define TG3_REG_MAC_RCV_VALUE_0           0x00000484
#define TG3_REG_MAC_RCV_RULE_1            0x00000488
#define TG3_REG_MAC_RCV_VALUE_1           0x0000048c
#define TG3_REG_MAC_RCV_RULE_5            0x000004a8
#define TG3_REG_MAC_RCV_VALUE_5           0x000004ac
#define TG3_REG_MAC_RCV_RULE_15           0x000004f8
#define TG3_REG_MAC_RCV_VALUE_15          0x000004f4
#define TG3_REG_MAC_RX_STATS_UCAST        0x0000088c
#define TG3_REG_MAC_TX_STATS_UCAST        0x0000086c

#define TG3_REG_RX_MODE                   0x00000468
#define TG3_REG_TX_MODE                   0x0000045c

#define TG3_REG_RCVBDI_MODE               0x00002c00
#define TG3_REG_RCVBDI_STD_THRESH         0x00002c18
#define TG3_REG_RCVBDI_JUMBO_THRESH       0x00002c1c
#define TG3_REG_RCVBDI_STD_BD             0x00002450
#define TG3_REG_RCVBDI_JUMBO_BD           0x00002440

#define TG3_REG_RDMAC_MODE                0x00004800
#define TG3_REG_RDMAC_STATUS              0x00004804

#define TG3_REG_WDMAC_MODE                0x00004c00
#define TG3_REG_WDMAC_STATUS              0x00004c04

#define TG3_REG_MSGINT_MODE               0x00006000
#define TG3_REG_MSGINT_STATUS             0x00006004

#define TG3_REG_GRCMBOX_INTERRUPT_0       0x00005800
#define TG3_REG_GRCMBOX_RCVSTD_PROD_IDX   0x00005868
#define TG3_GRCMBOX_RCVJUMBO_PROD_IDX     0x00005870
#define TG3_REG_GRCMBOX_RCVRET_CON_IDX_0  0x00005880
#define TG3_REG_GRCMBOX_SNDHOST_PROD_IDX_0 0x00005900

#define TG3_REG_MAILBOX_INTERRUPT_0       0x00000200
#define TG3_REG_MAILBOX_RCV_STD_PROD_IDX  0x00000268
#define TG3_REG_MAILBOX_RCV_JUMBO_PROD_IDX 0x00000270
#define TG3_REG_MAILBOX_RCVRET_CON_IDX_0  0x00000280
#define TG3_REG_MAILBOX_SNDNIC_PROD_IDX_0 0x00000380

#define TG3_REG_VCPU_STATUS               0x00005100
#define TG3_REG_VCPU_CFGSHDW              0x00005104

#define TG3_REG_NIC_SRAM_STATS_BLK        0x00000300
#define TG3_REG_NIC_SRAM_SEND_RCB         0x00000100
#define TG3_REG_NIC_SRAM_RCV_RET_RCB      0x00000200
#define TG3_REG_NIC_SRAM_STATUS_BLK       0x00000b00
#define TG3_REG_NIC_SRAM_TX_BUFFER_DESC   0x00004000
#define TG3_REG_NIC_SRAM_RX_BUFFER_DESC   0x00006000
#define TG3_REG_NIC_SRAM_RX_JUMBO_BUFFER_DESC 0x00007000
#define TG3_REG_NIC_SRAM_MBUF_POOL_BASE   0x00008000
#define TG3_REG_NIC_SRAM_MBUF_POOL_BASE5705 0x00010000

#define TG3_REG_TG3_RX_TSTAMP_LSB         0x000006b0
#define TG3_REG_TG3_RX_TSTAMP_MSB         0x000006b4
#define TG3_REG_TG3_TX_TSTAMP_LSB         0x000005c0
#define TG3_REG_TG3_TX_TSTAMP_MSB         0x000005c4

#define TG3_REG_TG3_RX_PTP_CTL            0x000006c8

#define TG3_REG_TG3_CPMU_CTRL             0x00003600
#define TG3_REG_TG3_CPMU_STATUS           0x0000362c
#define TG3_REG_TG3_CPMU_CLCK_STAT        0x00003630
#define TG3_REG_TG3_CPMU_LSPD_10MB_CLK    0x00003604
#define TG3_REG_TG3_CPMU_LSPD_1000MB_CLK  0x0000360c
#define TG3_REG_TG3_CPMU_LNK_AWARE_PWRMD  0x00003610
#define TG3_REG_TG3_CPMU_CLCK_ORIDE       0x00003624
#define TG3_REG_TG3_CPMU_CLCK_ORIDE_ENABLE 0x00003628
#define TG3_REG_TG3_CPMU_HST_ACC          0x0000361c
#define TG3_REG_TG3_CPMU_EEE_MODE         0x000036b0
#define TG3_REG_TG3_CPMU_EEE_DBTMR1       0x000036b4
#define TG3_REG_TG3_CPMU_EEE_DBTMR2       0x000036b8
#define TG3_REG_TG3_CPMU_EEE_LNKIDL_CTRL  0x000036bc
#define TG3_REG_TG3_CPMU_EEE_CTRL         0x000036d0

#define TG3_REG_TG3_EAV_REF_CLCK_LSB      0x00006900
#define TG3_REG_TG3_EAV_REF_CLCK_MSB      0x00006904
#define TG3_REG_TG3_EAV_REF_CLCK_CTL      0x00006908
#define TG3_REG_TG3_EAV_WATCHDOG0_LSB     0x00006918
#define TG3_REG_TG3_EAV_WATCHDOG0_MSB     0x0000691c
#define TG3_REG_TG3_EAV_REF_CLK_CORRECT_CTL 0x00006928

#define TG3_REG_APE_LOCK_GRANT            0x004c
#define TG3_REG_APE_LOCK_REQ              0x002c
#define TG3_REG_APE_EVENT_STATUS          0x4300
#define TG3_REG_APE_EVENT                 0x000c
#define TG3_REG_APE_SHMEM_BASE            0x4000
#define TG3_REG_APE_FW_STATUS             0x400c
#define TG3_REG_APE_SEG_MSG_BUF_OFF       0x401c
#define TG3_REG_APE_SEG_MSG_BUF_LEN       0x4020
#define TG3_REG_APE_HOST_BEHAVIOR         0x4210
#define TG3_REG_APE_HOST_SEG_SIG          0x4200
#define TG3_REG_APE_HOST_SEG_LEN          0x4204
#define TG3_REG_APE_HOST_DRIVER_ID        0x420c
#define TG3_REG_APE_HOST_INIT_COUNT       0x4208
#define TG3_REG_APE_HOST_DRVR_STATE       0x421c
#define TG3_REG_APE_HOST_HEARTBEAT_COUNT  0x4218
#define TG3_REG_APE_HOST_WOL_SPEED        0x4224
#define TG3_REG_APE_FW_FEATURES           0x4010
#define TG3_REG_APE_FW_VERSION            0x4018

#define TG3_REG_TG3_APE_GPIO_MSG          0x0008

#define TG3_REG_TG3_APE_HOST_HEARTBEAT_INT_MS 0x4214

#define TG3_REG_TG3_APE_OTP_CTRL          0x00e8
#define TG3_REG_TG3_APE_OTP_STATUS        0x00ec
#define TG3_REG_TG3_APE_OTP_ADDR          0x00f0
#define TG3_REG_TG3_APE_OTP_RD_DATA       0x00f8

#define TG3_REG_OTP_MODE                  0x00007500
#define TG3_REG_OTP_CTRL                  0x00007504
#define TG3_REG_OTP_STATUS                0x00007508
#define TG3_REG_OTP_ADDRESS               0x0000750c
#define TG3_REG_OTP_READ_DATA             0x00007514

#define TG3_REG_NVRAM_CMD                 0x00007000
#define TG3_REG_NVRAM_WRDATA              0x00007008
#define TG3_REG_NVRAM_ADDR                0x0000700c
#define TG3_REG_NVRAM_RDDATA              0x00007010
#define TG3_REG_NVRAM_CFG1                0x00007014
#define TG3_REG_NVRAM_SWARB               0x00007020
#define TG3_REG_NVRAM_ACCESS              0x00007024
#define TG3_REG_NVRAM_WRITE1              0x00007028
#define TG3_REG_NVRAM_ADDR_LOCKOUT        0x00007030
#define TG3_REG_NVRAM_AUTOSENSE_STATUS    0x00007038

#define TG3_REG_TG3_CORR_ERR_STAT         0x00000110

#define TG3_REG_RX_CPU_BASE               0x00005000
#define TG3_REG_RX_CPU_STATE              0x00005004
#define TG3_REG_RX_CPU_PGMCTR             0x0000501c
#define TG3_REG_RX_CPU_HWBKPT             0x00005034
#define TG3_REG_TX_CPU_BASE               0x00005400
#define TG3_REG_TX_CPU_STATE              0x00005404
#define TG3_REG_TX_CPU_PGMCTR             0x0000541c

#define TG3_REG_GRCMBOX_BASE              0x00005600

#define TG3_HW_STATUS_SIZE                0x50

/* very small subset of bits needed by driver paths we emulate */
#define TG3_MISC_HOST_CTRL_TAGGED_STATUS  (1U << 29)
#define TG3_MISC_HOST_CTRL_MASK_PCI_INT   (1U << 0)

#define TG3_MAC_STATUS_LNKSTATE_CHANGED   0x00000010
#define TG3_MAC_STATUS_PCS_SYNCED         0x00000008
#define TG3_MAC_STATUS_SIGNAL_DET         0x00000004

#define TG3_MAC_RX_MODE_ENABLE            0x00000001
#define TG3_MAC_TX_MODE_ENABLE            0x00000001

#define TG3_HOSTCC_MODE_ENABLE            0x00000001
#define TG3_HOSTCC_MODE_NOW               0x00000002

#define TG3_PCISTATE_INT_NOT_ACTIVE       0x00000001

#define TG3_MSGINT_MODE_ENABLE            0x00000001

#define TG3_FLOW_ATTN_MBUF_LWM            0x00000001

#define TG3_SD_STATUS_UPDATED             0x00000001
#define TG3_SD_STATUS_ERROR               0x00000002
#define TG3_SD_STATUS_LINK_CHG            0x00000004

#define TG3_MAX_INTR_COAL_TICKS           0xffff

#define TG3_DEFAULT_INTR_COAL_TICKS       150


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
    struct {
        uint32_t mac_mode;
        uint32_t mac_status;
        uint32_t mac_event;
        uint32_t mac_rx_mode;
        uint32_t mac_tx_mode;
        uint32_t mac_led_ctrl;
        uint32_t mac_addr0_high;
        uint32_t mac_addr0_low;
        uint32_t mac_addr1_high;
        uint32_t mac_addr1_low;
        uint32_t mac_rx_mtu_size;
        uint32_t mac_mi_stat;
        uint32_t mac_mi_com;
        uint32_t mac_mi_mode;
        uint32_t hostcc_mode;
        uint32_t hostcc_flow_attn;
        uint32_t rx_mode;
        uint32_t tx_mode;
        uint32_t grc_mode;
        uint32_t grc_misc_cfg;
        uint32_t grc_local_ctrl;
        uint32_t dma_rwctrl;
        uint32_t msgint_mode;
        uint32_t msgint_status;
        uint32_t pcistate;
        uint32_t misc_host_ctrl;
        uint32_t clock_ctrl;
        uint32_t pci_chip_rev_id;
        uint32_t mem_win_base;
        uint32_t mem_win_data;
        uint32_t reg_base_addr;
        uint32_t reg_data;
        uint32_t mailbox_int0;
        uint32_t mailbox_rcvret_con_idx0;
        uint32_t mailbox_sndnic_prod_idx0;
    } regs;

    /* internal helper to track 'link up' */
    bool link_up;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Very simple model: raise INTx if flow attention or MAC status change */
    bool want_irq = false;

    if (!(s->regs.misc_host_ctrl & TG3_MISC_HOST_CTRL_MASK_PCI_INT)) {
        if (s->regs.hostcc_flow_attn & TG3_FLOW_ATTN_MBUF_LWM) {
            want_irq = true;
        }
        if (s->regs.mac_status & (TG3_MAC_STATUS_LNKSTATE_CHANGED)) {
            want_irq = true;
        }
        if (s->regs.mailbox_int0 & 0x1) {
            want_irq = true;
        }
    }

    pci_set_irq(pdev, want_irq ? 1 : 0);
}

/* Device-initiated DMA logic: not used by driver paths we emulate, so empty. */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Helper: 32-bit register read */
static uint32_t pcibase_reg_readl(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case TG3_REG_MAC_MODE:                return s->regs.mac_mode;
    case TG3_REG_MAC_STATUS:             return s->regs.mac_status;
    case TG3_REG_MAC_EVENT:              return s->regs.mac_event;
    case TG3_REG_MAC_LED_CTRL:           return s->regs.mac_led_ctrl;
    case TG3_REG_MAC_ADDR_0_HIGH:        return s->regs.mac_addr0_high;
    case TG3_REG_MAC_ADDR_0_LOW:         return s->regs.mac_addr0_low;
    case TG3_REG_MAC_ADDR_1_HIGH:        return s->regs.mac_addr1_high;
    case TG3_REG_MAC_ADDR_1_LOW:         return s->regs.mac_addr1_low;
    case TG3_REG_MAC_RX_MTU_SIZE:        return s->regs.mac_rx_mtu_size;
    case TG3_REG_MAC_MI_STAT:            return s->regs.mac_mi_stat;
    case TG3_REG_MAC_MI_COM:             return s->regs.mac_mi_com;
    case TG3_REG_MAC_MI_MODE:            return s->regs.mac_mi_mode;
    case TG3_REG_HOSTCC_MODE:            return s->regs.hostcc_mode;
    case TG3_REG_HOSTCC_FLOW_ATTN:       return s->regs.hostcc_flow_attn;
    case TG3_REG_RX_MODE:                return s->regs.rx_mode;
    case TG3_REG_TX_MODE:                return s->regs.tx_mode;
    case TG3_REG_GRC_MODE:               return s->regs.grc_mode;
    case TG3_REG_GRC_MISC_CFG:           return s->regs.grc_misc_cfg;
    case TG3_REG_GRC_LOCAL_CTRL:         return s->regs.grc_local_ctrl;
    case TG3_REG_TG3PCI_DMA_RW_CTRL:     return s->regs.dma_rwctrl;
    case TG3_REG_MSGINT_MODE:            return s->regs.msgint_mode;
    case TG3_REG_MSGINT_STATUS:          return s->regs.msgint_status;
    case TG3_REG_TG3PCI_PCISTATE:        return s->regs.pcistate;
    case TG3_REG_TG3PCI_MISC_HOST_CTRL:  return s->regs.misc_host_ctrl;
    case TG3_REG_TG3PCI_CLOCK_CTRL:      return s->regs.clock_ctrl;
    case TG3_REG_TG3PCI_PRODID_ASICREV:  return s->regs.pci_chip_rev_id;
    case TG3_REG_TG3PCI_MEM_WIN_BASE_ADDR: return s->regs.mem_win_base;
    case TG3_REG_TG3PCI_MEM_WIN_DATA:    return s->regs.mem_win_data;
    case TG3_REG_TG3PCI_REG_BASE_ADDR:   return s->regs.reg_base_addr;
    case TG3_REG_TG3PCI_REG_DATA:        return s->regs.reg_data;
    case TG3_REG_MAILBOX_INTERRUPT_0:    return s->regs.mailbox_int0;
    case TG3_REG_MAILBOX_RCVRET_CON_IDX_0: return s->regs.mailbox_rcvret_con_idx0;
    case TG3_REG_MAILBOX_SNDNIC_PROD_IDX_0: return s->regs.mailbox_sndnic_prod_idx0;
    default:
        /* For unimplemented registers, just return 0 */
        return 0;
    }
}

/* Helper: 32-bit register write */
static void pcibase_reg_writel(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case TG3_REG_MAC_MODE:
        s->regs.mac_mode = val;
        break;
    case TG3_REG_MAC_STATUS:
        /* many bits are W1C in real hw; we only clear those the driver may clear */
        s->regs.mac_status &= ~val;
        break;
    case TG3_REG_MAC_EVENT:
        s->regs.mac_event = val;
        break;
    case TG3_REG_MAC_LED_CTRL:
        s->regs.mac_led_ctrl = val;
        break;
    case TG3_REG_MAC_ADDR_0_HIGH:
        s->regs.mac_addr0_high = val;
        break;
    case TG3_REG_MAC_ADDR_0_LOW:
        s->regs.mac_addr0_low = val;
        break;
    case TG3_REG_MAC_ADDR_1_HIGH:
        s->regs.mac_addr1_high = val;
        break;
    case TG3_REG_MAC_ADDR_1_LOW:
        s->regs.mac_addr1_low = val;
        break;
    case TG3_REG_MAC_RX_MTU_SIZE:
        s->regs.mac_rx_mtu_size = val;
        break;
    case TG3_REG_MAC_MI_STAT:
        s->regs.mac_mi_stat = val;
        break;
    case TG3_REG_MAC_MI_COM:
        s->regs.mac_mi_com = val;
        break;
    case TG3_REG_MAC_MI_MODE:
        s->regs.mac_mi_mode = val;
        break;
    case TG3_REG_HOSTCC_MODE:
        s->regs.hostcc_mode = val;
        break;
    case TG3_REG_HOSTCC_FLOW_ATTN:
        /* W1C semantics for flow attention bits */
        s->regs.hostcc_flow_attn &= ~val;
        break;
    case TG3_REG_RX_MODE:
        s->regs.rx_mode = val;
        s->regs.mac_rx_mode = val;
        if (val & TG3_MAC_RX_MODE_ENABLE) {
            s->link_up = true;
        }
        break;
    case TG3_REG_TX_MODE:
        s->regs.tx_mode = val;
        s->regs.mac_tx_mode = val;
        break;
    case TG3_REG_GRC_MODE:
        s->regs.grc_mode = val;
        break;
    case TG3_REG_GRC_MISC_CFG:
        s->regs.grc_misc_cfg = val;
        break;
    case TG3_REG_GRC_LOCAL_CTRL:
        s->regs.grc_local_ctrl = val;
        break;
    case TG3_REG_TG3PCI_DMA_RW_CTRL:
        s->regs.dma_rwctrl = val;
        break;
    case TG3_REG_MSGINT_MODE:
        s->regs.msgint_mode = val;
        break;
    case TG3_REG_MSGINT_STATUS:
        s->regs.msgint_status &= ~val;
        break;
    case TG3_REG_TG3PCI_PCISTATE:
        s->regs.pcistate = val;
        break;
    case TG3_REG_TG3PCI_MISC_HOST_CTRL:
        s->regs.misc_host_ctrl = val;
        break;
    case TG3_REG_TG3PCI_CLOCK_CTRL:
        s->regs.clock_ctrl = val;
        break;
    case TG3_REG_TG3PCI_MEM_WIN_BASE_ADDR:
        s->regs.mem_win_base = val;
        break;
    case TG3_REG_TG3PCI_MEM_WIN_DATA:
        /* very simple window: base is offset within our BAR, data is not stored */
        s->regs.mem_win_data = val;
        break;
    case TG3_REG_TG3PCI_REG_BASE_ADDR:
        s->regs.reg_base_addr = val;
        break;
    case TG3_REG_TG3PCI_REG_DATA:
        s->regs.reg_data = val;
        break;
    case TG3_REG_MAILBOX_INTERRUPT_0:
        /* Writing non-zero engages 'in handler' coalescing; driver also uses it as W1C */
        s->regs.mailbox_int0 = val;
        break;
    case TG3_REG_MAILBOX_RCVRET_CON_IDX_0:
        s->regs.mailbox_rcvret_con_idx0 = val;
        break;
    case TG3_REG_MAILBOX_SNDNIC_PROD_IDX_0:
        s->regs.mailbox_sndnic_prod_idx0 = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4 && size != 2 && size != 1) {
        return 0;
    }

    /* We only implement 32-bit semantics, smaller sizes are little-endian slices */
    uint32_t full = pcibase_reg_readl(s, addr & ~3ULL);
    unsigned shift = (addr & 3ULL) * 8;

    if (size == 4) {
        val = full;
    } else if (size == 2) {
        val = (full >> shift) & 0xffffu;
    } else {
        val = (full >> shift) & 0xffu;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 && size != 2 && size != 1) {
        return;
    }

    uint32_t cur = pcibase_reg_readl(s, addr & ~3ULL);
    uint32_t mask;
    unsigned shift = (addr & 3ULL) * 8;

    if (size == 4) {
        cur = (uint32_t)val;
    } else if (size == 2) {
        mask = 0xffffu << shift;
        cur = (cur & ~mask) | (((uint32_t)val & 0xffffu) << shift);
    } else {
        mask = 0xffu << shift;
        cur = (cur & ~mask) | (((uint32_t)val & 0xffu) << shift);
    }

    pcibase_reg_writel(s, addr & ~3ULL, cur);

    /* after writes that may impact interrupt state, recompute */
    switch (addr & ~3ULL) {
    case TG3_REG_HOSTCC_FLOW_ATTN:
    case TG3_REG_MSGINT_STATUS:
    case TG3_REG_MAC_STATUS:
    case TG3_REG_MAILBOX_INTERRUPT_0:
    case TG3_REG_TG3PCI_MISC_HOST_CTRL:
        pcibase_update_irq(s);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
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

    memset(&s->regs, 0, sizeof(s->regs));

    /* Initialize some reset defaults that the driver depends on */
    s->regs.pci_chip_rev_id = 0x00000001; /* arbitrary but non-zero */
    s->regs.mac_status = 0;
    s->regs.misc_host_ctrl = 0;
    s->regs.grc_mode = 0;
    s->regs.msgint_mode = 0;
    s->regs.msgint_status = 0;
    s->regs.pcistate = 0;
    s->regs.clock_ctrl = 0;
    s->regs.hostcc_mode = 0;
    s->regs.hostcc_flow_attn = 0;
    s->regs.mailbox_int0 = 0;
    s->link_up = false;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  TG3_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  TG3_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, TG3_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = TG3_REG_TG3_REG_BLK_SIZE;
    s->bar_info[0].name  = "tg3-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    s->has_msix = true;

    memset(&s->regs, 0, sizeof(s->regs));

    s->regs.pci_chip_rev_id = 0x00000001;
    s->regs.mac_status = 0;
    s->regs.misc_host_ctrl = 0;
    s->regs.grc_mode = 0;
    s->regs.msgint_mode = 0;
    s->regs.msgint_status = 0;
    s->regs.pcistate = 0;
    s->regs.clock_ctrl = 0;
    s->regs.hostcc_mode = 0;
    s->regs.hostcc_flow_attn = 0;
    s->regs.mailbox_int0 = 0;
    s->regs.mac_rx_mtu_size = 1500 + 14 + 4; /* default */
    s->link_up = false;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "tg3_pci",
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
