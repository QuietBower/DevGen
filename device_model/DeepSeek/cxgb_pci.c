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

#define TYPE_PCIBASE_DEVICE "cxgb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CHELSIO 0x1425
#define PCI_DEVICE_ID_CHELSIO_T1 0x0001   /* Placeholder; actual ID to be filled from t1_pci_tbl */
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Register offsets from cxgb2.c */
#define A_MC4_INT_CAUSE 0x1fc
#define A_CSPI_INTR_ENABLE 0x84c
#define A_RAT_INTR_CAUSE 0x594
#define A_RAT_ROUTE_CONTROL 0x580
#define A_CSPI_RX_AE_WM 0x810
#define A_ESPI_GOSTAT 0x8fc
#define A_PL_CAUSE 0xa04
#define A_MC5_MASK_WRITE_CMD 0xcfc
#define A_TPI_PAR 0x29c
#define A_TP_TX_DROP_COUNT 0x4bc
#define A_MC3_CFG 0x100
#define A_TPI_ADDR 0x280
#define A_ESPI_SCH_TOKEN0 0x880
#define A_PL_ENABLE 0xa00
#define A_SG_RESPACCUTIMER 0xc0
#define A_ULP_PIO_CTRL 0x998
#define A_MC5_CONFIG 0xc04
#define A_ULP_ULIMIT 0x980
#define A_TP_IN_CONFIG 0x300
#define A_ELMER0_GPO		0x100018
#define A_PCICFG_PM_CSR 0x44
#define A_SG_CONTROL 0x0
#define A_PCICFG_INTR_ENABLE 0xf4
#define A_PCICFG_INTR_CAUSE 0xf8
#define A_MC4_CFG 0x180
#define A_SG_INTRTIMER 0x4c
#define A_PCICFG_VPD_DATA 0x4c
#define A_PCICFG_VPD_ADDR 0x4a
#define A_SG_SLEEPING 0x48
#define A_TP_PC_CONFIG 0x348
#define A_TP_GLOBAL_CONFIG 0x308
#define A_SG_DOORBELL 0x4
#define A_TP_INT_ENABLE 0x470
#define A_SG_INT_ENABLE 0xb8
#define A_ESPI_INTR_ENABLE 0x8cc
#define A_ESPI_INTR_STATUS 0x8c8
#define A_ESPI_DIP2_ERR_COUNT 0x8f4
#define A_TP_INT_CAUSE 0x474
#define A_SG_INT_CAUSE 0xbc
#define A_TP_RESET 0x44c
#define A_ESPI_TRAIN 0x8ac
#define A_ESPI_FIFO_STATUS_ENABLE 0x8a0
#define A_ESPI_MISC_CONTROL 0x8f0
#define A_ESPI_MAXBURST1_MAXBURST2 0x8a8
#define A_PCICFG_MODE 0xfc
#define A_SG_RSPQUEUECREDIT 0x40
#define A_SG_FLTHRESHOLD 0x3c
#define A_SG_FL1BASELWR 0x20
#define A_SG_RSPBASEUPR 0x38
#define A_SG_CMD0BASEUPR 0xc
#define A_SG_CMD1BASELWR 0x10
#define A_SG_RSPBASELWR 0x34
#define A_SG_RSPSIZE 0x30
#define A_SG_FL0BASEUPR 0x1c
#define A_SG_CMD0BASELWR 0x8
#define A_SG_FL1BASEUPR 0x24
#define A_SG_CMD0SIZE 0x28
#define A_SG_FL1SIZE 0xb4
#define A_SG_CMD1BASEUPR 0x14
#define A_SG_FL0SIZE 0x2c
#define A_SG_FL0BASELWR 0x18
#define A_SG_CMD1SIZE 0xb0
#define A_TP_TX_DROP_CONFIG 0x4b8
#define A_TP_OUT_CONFIG 0x304
#define A_ESPI_RX_FIFO_ALMOST_FULL_WATERMARK 0x894
#define A_ESPI_RX_FIFO_ALMOST_EMPTY_WATERMARK 0x890
#define A_ESPI_CALENDAR_LENGTH 0x898
#define A_PORT_CONFIG 0x89c
#define A_ESPI_SCH_TOKEN2 0x888
#define A_ESPI_SCH_TOKEN1 0x884
#define A_ESPI_SCH_TOKEN3 0x88c
#define A_ESPI_RX_RESET 0x8ec
#define A_ELMER0_GPI_STAT	0x100014
#define A_ELMER0_PORT0_MI1_ADDR 0x400004
#define A_ELMER0_PORT0_MI1_OP 0x40000c
#define A_ELMER0_PORT0_MI1_DATA 0x400008
#define A_ELMER0_PORT0_MI1_CFG	0x400000
#define A_ELMER0_INT_ENABLE	0x100008
#define A_ELMER0_INT_CAUSE	0x10000c

/* Bitfield macros (simplified) */
#define S_IP_CSUM    13
#define S_TCP_CSUM    11
#define S_FL1_ENABLE    3
#define S_FL0_ENABLE    2
#define S_PL_INTR_SGE_ERR    0
#define S_PL_INTR_TP    6
#define S_PL_INTR_PCIX    10
#define S_PL_INTR_ESPI    8
#define S_PL_INTR_EXT    11
#define S_MC4_SLOW    25
#define S_M_BUS_ENABLE    5
#define S_READY    1
#define S_TCAM_RESET    1
#define S_VLAN_XTRACT    18
#define S_VPD_OP_FLAG    15
#define S_PL_INTR_SGE_DATA    1
#define S_TP_RESET    0
#define S_TX_NPORTS    8
#define S_RX_NPORTS    0
#define S_RXSTATUSENABLE    0
#define S_ESPI_RX_LNK_RST    0
#define S_ESPI_RX_CORE_RST    1
#define S_RX_CLK_STATUS    2
#define S_WRITE_DATA    0
#define S_MODULE_ADDR    16
#define S_ESPI_CMD_BUSY    8
#define S_REGISTER_OFFSET    8
#define S_BUNDLE_ADDR    20
#define S_SPI4_COMMAND    24
#define S_CHANNEL_ADDR    12
#define S_MI1_REG_ADDR    0
#define S_MI1_PHY_ADDR    5
#define S_MI1_MDI_INVERT    1
#define S_MI1_SOF    3
#define S_MI1_CLK_DIV    5
#define S_MI1_MDI_ENABLE    0
#define S_MI1_PREAMBLE_ENABLE    2
#define S_MI1_OP_BUSY    31
#define S_TPIPAR    0
#define S_PCI_MODE_64BIT    0
#define S_PCI_MODE_PCIX    5
#define S_CMDQ1_ENABLE    1
#define S_CMDQ0_ENABLE    0
#define S_RESPQ_OVERFLOW    1
#define S_FL_EXHAUSTED    2
#define S_PACKET_TOO_BIG    3
#define S_PACKET_MISMATCH    4
#define S_RESPQ_EXHAUSTED    0
#define S_RXOVERFLOW    3
#define S_DIP2PARITYERR    5
#define S_RXDROP    1
#define S_DIP4ERR    0
#define S_TXDROP    2
#define S_RAMPARITYERR    4
#define S_RX_PKT_OFFSET    15
#define S_ISCSI_COALESCE    14
#define S_RESPONSE_QUEUE_ENABLE    5
#define S_ENABLE_BIG_ENDIAN    12
#define S_CPL_ENABLE    4
#define S_DISABLE_CMDQ1_GTS    9
#define S_CMDQ_PRIORITY    6
#define S_ENABLE_TX_DROP    31
#define S_TP_IN_CSPI_CHECK_IP_CSUM    5
#define S_TP_IN_ESPI_CHECK_IP_CSUM    12
#define S_TP_IN_CSPI_CHECK_TCP_CSUM    6
#define S_TP_OUT_ESPI_GENERATE_TCP_CSUM    11
#define S_TP_OUT_ESPI_GENERATE_IP_CSUM    10
#define S_IP_TTL    0
#define S_TP_IN_CSPI_CPL    3
#define S_TP_IN_ESPI_ETHERNET    8
#define S_OFFLOAD_DISABLE    14
#define S_TP_OUT_CSPI_CPL    2
#define S_DROP_TICKS_CNT    4
#define S_NUM_PKTS_DROPPED    0
#define S_ENABLE_TX_ERROR    30
#define S_PATH_MTU    15
#define S_TP_IN_ESPI_CHECK_TCP_CSUM    13
#define S_TP_OUT_ESPI_ETHERNET    6
#define S_MONITORED_PORT_NUM    25
#define S_MONITORED_DIRECTION    27
#define S_MONITORED_INTERFACE    28
#define S_INTEL1010MODE    4
#define S_DIP4_THRES    9
#define S_DIP2_PARITY_ERR_THRES    5
#define S_OUT_OF_SYNC_COUNT    0
#define S_SYN_COOKIE_PARAMETER    26
#define S_5TUPLE_LOOKUP    17

/* Convenience bit macros */
#define V_IP_CSUM(x) ((x) << S_IP_CSUM)
#define V_TCP_CSUM(x) ((x) << S_TCP_CSUM)
#define V_FL1_ENABLE(x) ((x) << S_FL1_ENABLE)
#define V_FL0_ENABLE(x) ((x) << S_FL0_ENABLE)
#define V_PL_INTR_SGE_ERR(x) ((x) << S_PL_INTR_SGE_ERR)
#define V_PL_INTR_TP(x) ((x) << S_PL_INTR_TP)
#define V_PL_INTR_PCIX(x) ((x) << S_PL_INTR_PCIX)
#define V_PL_INTR_ESPI(x) ((x) << S_PL_INTR_ESPI)
#define V_PL_INTR_EXT(x) ((x) << S_PL_INTR_EXT)
#define V_MC4_SLOW(x) ((x) << S_MC4_SLOW)
#define V_M_BUS_ENABLE(x) ((x) << S_M_BUS_ENABLE)
#define V_READY(x) ((x) << S_READY)
#define V_TCAM_RESET(x) ((x) << S_TCAM_RESET)
#define V_VLAN_XTRACT(x) ((x) << S_VLAN_XTRACT)
#define V_VPD_OP_FLAG(x) ((x) << S_VPD_OP_FLAG)
#define V_PL_INTR_SGE_DATA(x) ((x) << S_PL_INTR_SGE_DATA)
#define V_TP_RESET(x) ((x) << S_TP_RESET)
#define V_TX_NPORTS(x) ((x) << S_TX_NPORTS)
#define V_RX_NPORTS(x) ((x) << S_RX_NPORTS)
#define V_RXSTATUSENABLE(x) ((x) << S_RXSTATUSENABLE)
#define V_ESPI_RX_LNK_RST(x) ((x) << S_ESPI_RX_LNK_RST)
#define V_ESPI_RX_CORE_RST(x) ((x) << S_ESPI_RX_CORE_RST)
#define V_RX_CLK_STATUS(x) ((x) << S_RX_CLK_STATUS)
#define V_WRITE_DATA(x) ((x) << S_WRITE_DATA)
#define V_MODULE_ADDR(x) ((x) << S_MODULE_ADDR)
#define V_ESPI_CMD_BUSY(x) ((x) << S_ESPI_CMD_BUSY)
#define V_REGISTER_OFFSET(x) ((x) << S_REGISTER_OFFSET)
#define V_BUNDLE_ADDR(x) ((x) << S_BUNDLE_ADDR)
#define V_SPI4_COMMAND(x) ((x) << S_SPI4_COMMAND)
#define V_CHANNEL_ADDR(x) ((x) << S_CHANNEL_ADDR)
#define V_MI1_REG_ADDR(x) ((x) << S_MI1_REG_ADDR)
#define V_MI1_PHY_ADDR(x) ((x) << S_MI1_PHY_ADDR)
#define V_MI1_MDI_INVERT(x) ((x) << S_MI1_MDI_INVERT)
#define V_MI1_SOF(x) ((x) << S_MI1_SOF)
#define V_MI1_CLK_DIV(x) ((x) << S_MI1_CLK_DIV)
#define V_MI1_MDI_ENABLE(x) ((x) << S_MI1_MDI_ENABLE)
#define V_MI1_PREAMBLE_ENABLE(x) ((x) << S_MI1_PREAMBLE_ENABLE)
#define V_MI1_OP_BUSY(x) ((x) << S_MI1_OP_BUSY)
#define V_TPIPAR(x) ((x) << S_TPIPAR)
#define V_PCI_MODE_64BIT(x) ((x) << S_PCI_MODE_64BIT)
#define V_PCI_MODE_PCIX(x) ((x) << S_PCI_MODE_PCIX)
#define V_CMDQ1_ENABLE(x) ((x) << S_CMDQ1_ENABLE)
#define V_CMDQ0_ENABLE(x) ((x) << S_CMDQ0_ENABLE)
#define V_RESPQ_OVERFLOW(x) ((x) << S_RESPQ_OVERFLOW)
#define V_FL_EXHAUSTED(x) ((x) << S_FL_EXHAUSTED)
#define V_PACKET_TOO_BIG(x) ((x) << S_PACKET_TOO_BIG)
#define V_PACKET_MISMATCH(x) ((x) << S_PACKET_MISMATCH)
#define V_RESPQ_EXHAUSTED(x) ((x) << S_RESPQ_EXHAUSTED)
#define V_RXOVERFLOW(x) ((x) << S_RXOVERFLOW)
#define V_DIP2PARITYERR(x) ((x) << S_DIP2PARITYERR)
#define V_RXDROP(x) ((x) << S_RXDROP)
#define V_DIP4ERR(x) ((x) << S_DIP4ERR)
#define V_TXDROP(x) ((x) << S_TXDROP)
#define V_RAMPARITYERR(x) ((x) << S_RAMPARITYERR)
#define V_RX_PKT_OFFSET(x) ((x) << S_RX_PKT_OFFSET)
#define V_ISCSI_COALESCE(x) ((x) << S_ISCSI_COALESCE)
#define V_RESPONSE_QUEUE_ENABLE(x) ((x) << S_RESPONSE_QUEUE_ENABLE)
#define V_ENABLE_BIG_ENDIAN(x) ((x) << S_ENABLE_BIG_ENDIAN)
#define V_CPL_ENABLE(x) ((x) << S_CPL_ENABLE)
#define V_DISABLE_CMDQ1_GTS(x) ((x) << S_DISABLE_CMDQ1_GTS)
#define V_CMDQ_PRIORITY(x) ((x) << S_CMDQ_PRIORITY)
#define V_ENABLE_TX_DROP(x) ((x) << S_ENABLE_TX_DROP)
#define V_TP_IN_CSPI_CHECK_IP_CSUM(x) ((x) << S_TP_IN_CSPI_CHECK_IP_CSUM)
#define V_TP_IN_ESPI_CHECK_IP_CSUM(x) ((x) << S_TP_IN_ESPI_CHECK_IP_CSUM)
#define V_TP_IN_CSPI_CHECK_TCP_CSUM(x) ((x) << S_TP_IN_CSPI_CHECK_TCP_CSUM)
#define V_SYN_COOKIE_PARAMETER(x) ((x) << S_SYN_COOKIE_PARAMETER)
#define V_5TUPLE_LOOKUP(x) ((x) << S_5TUPLE_LOOKUP)
#define V_TP_OUT_ESPI_GENERATE_TCP_CSUM(x) ((x) << S_TP_OUT_ESPI_GENERATE_TCP_CSUM)
#define V_TP_OUT_ESPI_GENERATE_IP_CSUM(x) ((x) << S_TP_OUT_ESPI_GENERATE_IP_CSUM)
#define V_IP_TTL(x) ((x) << S_IP_TTL)
#define V_TP_IN_CSPI_CPL(x) ((x) << S_TP_IN_CSPI_CPL)
#define V_TP_IN_ESPI_ETHERNET(x) ((x) << S_TP_IN_ESPI_ETHERNET)
#define V_OFFLOAD_DISABLE(x) ((x) << S_OFFLOAD_DISABLE)
#define V_TP_OUT_CSPI_CPL(x) ((x) << S_TP_OUT_CSPI_CPL)
#define V_DROP_TICKS_CNT(x) ((x) << S_DROP_TICKS_CNT)
#define V_NUM_PKTS_DROPPED(x) ((x) << S_NUM_PKTS_DROPPED)
#define V_ENABLE_TX_ERROR(x) ((x) << S_ENABLE_TX_ERROR)
#define V_PATH_MTU(x) ((x) << S_PATH_MTU)
#define V_TP_IN_ESPI_CHECK_TCP_CSUM(x) ((x) << S_TP_IN_ESPI_CHECK_TCP_CSUM)
#define V_TP_OUT_ESPI_ETHERNET(x) ((x) << S_TP_OUT_ESPI_ETHERNET)
#define V_MONITORED_PORT_NUM(x) ((x) << S_MONITORED_PORT_NUM)
#define V_MONITORED_DIRECTION(x) ((x) << S_MONITORED_DIRECTION)
#define V_MONITORED_INTERFACE(x) ((x) << S_MONITORED_INTERFACE)
#define V_INTEL1010MODE(x) ((x) << S_INTEL1010MODE)
#define F_IP_CSUM    V_IP_CSUM(1U)
#define F_TCP_CSUM    V_TCP_CSUM(1U)
#define F_FL1_ENABLE    V_FL1_ENABLE(1U)
#define F_FL0_ENABLE    V_FL0_ENABLE(1U)
#define F_PL_INTR_SGE_ERR    V_PL_INTR_SGE_ERR(1U)
#define F_PL_INTR_TP    V_PL_INTR_TP(1U)
#define F_PL_INTR_PCIX    V_PL_INTR_PCIX(1U)
#define F_PL_INTR_ESPI    V_PL_INTR_ESPI(1U)
#define F_PL_INTR_EXT    V_PL_INTR_EXT(1U)
#define F_MC4_SLOW    V_MC4_SLOW(1U)
#define F_M_BUS_ENABLE    V_M_BUS_ENABLE(1U)
#define F_READY    V_READY(1U)
#define F_TCAM_RESET    V_TCAM_RESET(1U)
#define F_VLAN_XTRACT    V_VLAN_XTRACT(1U)
#define F_VPD_OP_FLAG    V_VPD_OP_FLAG(1U)
#define F_PL_INTR_SGE_DATA    V_PL_INTR_SGE_DATA(1U)
#define F_PACKET_TOO_BIG    V_PACKET_TOO_BIG(1U)
#define F_RESPQ_OVERFLOW    V_RESPQ_OVERFLOW(1U)
#define F_FL_EXHAUSTED    V_FL_EXHAUSTED(1U)
#define F_PACKET_MISMATCH    V_PACKET_MISMATCH(1U)
#define F_RESPQ_EXHAUSTED    V_RESPQ_EXHAUSTED(1U)
#define F_RXOVERFLOW    V_RXOVERFLOW(1U)
#define F_DIP2PARITYERR    V_DIP2PARITYERR(1U)
#define F_RXDROP    V_RXDROP(1U)
#define F_DIP4ERR    V_DIP4ERR(1U)
#define F_TXDROP    V_TXDROP(1U)
#define F_RAMPARITYERR    V_RAMPARITYERR(1U)
#define F_ISCSI_COALESCE    V_ISCSI_COALESCE(1U)
#define F_RESPONSE_QUEUE_ENABLE    V_RESPONSE_QUEUE_ENABLE(1U)
#define F_ENABLE_BIG_ENDIAN    V_ENABLE_BIG_ENDIAN(1U)
#define F_CPL_ENABLE    V_CPL_ENABLE(1U)
#define F_DISABLE_CMDQ1_GTS    V_DISABLE_CMDQ1_GTS(1U)
#define F_ENABLE_TX_DROP    V_ENABLE_TX_DROP(1U)
#define F_TP_IN_CSPI_CHECK_IP_CSUM    V_TP_IN_CSPI_CHECK_IP_CSUM(1U)
#define F_TP_IN_ESPI_CHECK_IP_CSUM    V_TP_IN_ESPI_CHECK_IP_CSUM(1U)
#define F_TP_IN_CSPI_CHECK_TCP_CSUM    V_TP_IN_CSPI_CHECK_TCP_CSUM(1U)
#define F_TP_OUT_ESPI_GENERATE_TCP_CSUM    V_TP_OUT_ESPI_GENERATE_TCP_CSUM(1U)
#define F_TP_OUT_ESPI_GENERATE_IP_CSUM    V_TP_OUT_ESPI_GENERATE_IP_CSUM(1U)
#define F_TP_IN_CSPI_CPL    V_TP_IN_CSPI_CPL(1U)
#define F_TP_IN_ESPI_ETHERNET    V_TP_IN_ESPI_ETHERNET(1U)
#define F_OFFLOAD_DISABLE    V_OFFLOAD_DISABLE(1U)
#define F_TP_OUT_CSPI_CPL    V_TP_OUT_CSPI_CPL(1U)
#define F_TP_RESET    V_TP_RESET(1U)
#define F_RXSTATUSENABLE    V_RXSTATUSENABLE(1U)
#define F_MONITORED_DIRECTION    V_MONITORED_DIRECTION(1U)
#define F_MONITORED_INTERFACE    V_MONITORED_INTERFACE(1U)
#define F_INTEL1010MODE    V_INTEL1010MODE(1U)
#define F_ESPI_RX_LNK_RST    V_ESPI_RX_LNK_RST(1U)
#define F_ESPI_RX_CORE_RST    V_ESPI_RX_CORE_RST(1U)
#define F_RX_CLK_STATUS    V_RX_CLK_STATUS(1U)
#define F_MI1_OP_BUSY    V_MI1_OP_BUSY(1U)
#define F_MI1_PREAMBLE_ENABLE    V_MI1_PREAMBLE_ENABLE(1U)
#define F_ESPI_CMD_BUSY    V_ESPI_CMD_BUSY(1U)
#define F_PCI_MODE_64BIT    V_PCI_MODE_64BIT(1U)
#define F_PCI_MODE_PCIX    V_PCI_MODE_PCIX(1U)
#define F_CMDQ1_ENABLE    V_CMDQ1_ENABLE(1U)
#define F_CMDQ0_ENABLE    V_CMDQ0_ENABLE(1U)

/* Max number of ports */
#define MAX_NPORTS 4

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint16_t pmcsr;   /* Power management control/status register shadow */

    /* Register file for MMIO emulation */
    uint32_t *regs;
};

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr & 3) {
        qemu_log_mask(LOG_UNIMP, "cxgb: unaligned MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        return 0;
    }
    if ((addr / 4) < (0x800000 / 4)) {
        val = s->regs[addr / 4];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "cxgb: MMIO read out of range: 0x%" HWADDR_PRIx "\n", addr);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr & 3) {
        qemu_log_mask(LOG_UNIMP, "cxgb: unaligned MMIO write at 0x%" HWADDR_PRIx "\n", addr);
        return;
    }
    if ((addr / 4) < (0x800000 / 4)) {
        s->regs[addr / 4] = val;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "cxgb: MMIO write out of range: 0x%" HWADDR_PRIx "\n", addr);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "cxgb: PIO read not implemented\n");
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "cxgb: PIO write not implemented\n");
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
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

    /* Initialize register file with zeros, then set required default values */
    memset(s->regs, 0, 0x800000);

    /* A_PCICFG_MODE: indicate 64-bit PCI-X capable */
    s->regs[A_PCICFG_MODE / 4] = F_PCI_MODE_64BIT | F_PCI_MODE_PCIX;

    /* A_PL_ENABLE: set ready bit to indicate hardware is operational */
    s->regs[A_PL_ENABLE / 4] = F_READY;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CHELSIO );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_CHELSIO_T1 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* MSI initialization */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        error_report("Failed to initialize MSI");
        return;
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x800000;   /* 8MB to cover known register map up to 0x40000c */
    s->bar_info[0].name = "cxgb-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Allocate register file */
    s->regs = g_new0(uint32_t, 0x800000 / 4);
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

    g_free(s->regs);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cxgb_pci",
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
