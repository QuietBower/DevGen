/* QEMU PCI device model for hisi_sas_v3_hw - Phase 4: Debug & Update */
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

#define TYPE_PCIBASE_DEVICE "hisi_sas_v3_hw_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_HUAWEI 0x19e5
#define PCI_DEVICE_ID_HISI_SAS_V3 0xa230
#define PCI_CLASS_STORAGE_SAS     0x0107
#define PCI_CAP_ID_SAS            0x12

/* Register offsets and bitfields from driver (abbreviated for brevity) */
#define DRV_NAME "hisi_sas_v3_hw"
#define PORT_BASE               (0x2000)
#define PHY_CTRL                (PORT_BASE + 0x14)
#define LUN_SIZE 8
#define DLVRY_QUEUE_ENABLE       0x0
#define IOST_BASE_ADDR_LO        0x8
#define IOST_BASE_ADDR_HI        0xc
#define ITCT_BASE_ADDR_LO        0x10
#define ITCT_BASE_ADDR_HI        0x14
#define IO_BROKEN_MSG_ADDR_LO    0x18
#define IO_BROKEN_MSG_ADDR_HI    0x1c
#define PHY_CONTEXT              0x20
#define PHY_STATE                0x24
#define PHY_PORT_NUM_MA          0x28
#define PHY_CONN_RATE            0x30
#define ITCT_CLR                 0x44
#define ITCT_CLR_EN_OFF          16
#define ITCT_CLR_EN_MSK          (0x1 << ITCT_CLR_EN_OFF)
#define ITCT_DEV_OFF             0
#define ITCT_DEV_MSK             (0x7ff << ITCT_DEV_OFF)
#define IO_SATA_BROKEN_MSG_ADDR_LO 0x58
#define IO_SATA_BROKEN_MSG_ADDR_HI 0x5c
#define SATA_INITI_D2H_STORE_ADDR_LO 0x60
#define SATA_INITI_D2H_STORE_ADDR_HI 0x64
#define HGC_SAS_TX_OPEN_FAIL_RETRY_CTRL 0x84
#define HGC_SAS_TXFAIL_RETRY_CTRL 0x88
#define HGC_GET_ITV_TIME         0x90
#define DEVICE_MSG_WORK_MODE     0x94
#define OPENA_WT_CONTI_TIME      0x9c
#define I_T_NEXUS_LOSS_TIME      0xa0
#define MAX_CON_TIME_LIMIT_TIME  0xa4
#define BUS_INACTIVE_LIMIT_TIME  0xa8
#define REJECT_TO_OPEN_LIMIT_TIME 0xac
#define CFG_AGING_TIME           0xbc
#define HGC_DFX_CFG2             0xc0
#define HGC_CQE_ECC_ADDR         0x13c
#define HGC_CQE_ECC_1B_ADDR_OFF  0
#define HGC_CQE_ECC_1B_ADDR_MSK  (0x3f << HGC_CQE_ECC_1B_ADDR_OFF)
#define HGC_CQE_ECC_MB_ADDR_OFF  8
#define HGC_CQE_ECC_MB_ADDR_MSK  (0x3f << HGC_CQE_ECC_MB_ADDR_OFF)
#define HGC_IOST_ECC_ADDR        0x140
#define HGC_IOST_ECC_1B_ADDR_OFF 0
#define HGC_IOST_ECC_1B_ADDR_MSK (0x3ff << HGC_IOST_ECC_1B_ADDR_OFF)
#define HGC_IOST_ECC_MB_ADDR_OFF 16
#define HGC_IOST_ECC_MB_ADDR_MSK (0x3ff << HGC_IOST_ECC_MB_ADDR_OFF)
#define HGC_DQE_ECC_ADDR         0x144
#define HGC_DQE_ECC_1B_ADDR_OFF  0
#define HGC_DQE_ECC_1B_ADDR_MSK  (0xfff << HGC_DQE_ECC_1B_ADDR_OFF)
#define HGC_DQE_ECC_MB_ADDR_OFF  16
#define HGC_DQE_ECC_MB_ADDR_MSK  (0xfff << HGC_DQE_ECC_MB_ADDR_OFF)
#define HGC_ITCT_ECC_ADDR        0x150
#define HGC_ITCT_ECC_1B_ADDR_OFF 0
#define HGC_ITCT_ECC_1B_ADDR_MSK (0x3ff << HGC_ITCT_ECC_1B_ADDR_OFF)
#define HGC_ITCT_ECC_MB_ADDR_OFF 16
#define HGC_ITCT_ECC_MB_ADDR_MSK (0x3ff << HGC_ITCT_ECC_MB_ADDR_OFF)
#define HGC_AXI_FIFO_ERR_INFO    0x154
#define AXI_ERR_INFO_OFF         0
#define AXI_ERR_INFO_MSK         (0xff << AXI_ERR_INFO_OFF)
#define FIFO_ERR_INFO_OFF        8
#define FIFO_ERR_INFO_MSK        (0xff << FIFO_ERR_INFO_OFF)
#define INT_COAL_EN              0x19c
#define OQ_INT_COAL_TIME         0x1a0
#define OQ_INT_COAL_CNT          0x1a4
#define ENT_INT_COAL_TIME        0x1a8
#define ENT_INT_COAL_CNT         0x1ac
#define OQ_INT_SRC               0x1b0
#define OQ_INT_SRC_MSK           0x1b4
#define ENT_INT_SRC1             0x1b8
#define ENT_INT_SRC2             0x1bc
#define ENT_INT_SRC3             0x1c0
#define ENT_INT_SRC3_WP_DEPTH_OFF 8
#define ENT_INT_SRC3_IPTT_SLOT_NOMATCH_OFF 9
#define ENT_INT_SRC3_RP_DEPTH_OFF 10
#define ENT_INT_SRC3_AXI_OFF     11
#define ENT_INT_SRC3_FIFO_OFF    12
#define ENT_INT_SRC3_LM_OFF      14
#define ENT_INT_SRC3_ITC_INT_OFF 15
#define ENT_INT_SRC3_ITC_INT_MSK (0x1 << ENT_INT_SRC3_ITC_INT_OFF)
#define ENT_INT_SRC3_ABT_OFF     16
#define ENT_INT_SRC3_DQE_POISON_OFF 18
#define ENT_INT_SRC3_IOST_POISON_OFF 19
#define ENT_INT_SRC3_ITCT_POISON_OFF 20
#define ENT_INT_SRC3_ITCT_NCQ_POISON_OFF 21
#define ENT_INT_SRC_MSK1         0x1c4
#define ENT_INT_SRC_MSK2         0x1c8
#define ENT_INT_SRC_MSK3         0x1cc
#define ENT_INT_SRC_MSK3_ENT95_MSK_OFF  31
#define ENT_INT_SRC_MSK3_ENT95_MSK_MSK  (0x1 << ENT_INT_SRC_MSK3_ENT95_MSK_OFF)
#define SAS_ECC_INTR             0x1e8
#define SAS_ECC_INTR_MSK         0x1ec
#define SAS_ECC_INTR_DQE_ECC_1B_OFF 0
#define SAS_ECC_INTR_DQE_ECC_MB_OFF 1
#define SAS_ECC_INTR_IOST_ECC_1B_OFF 2
#define SAS_ECC_INTR_IOST_ECC_MB_OFF 3
#define SAS_ECC_INTR_ITCT_ECC_MB_OFF 5
#define SAS_ECC_INTR_ITCT_ECC_1B_OFF 4
#define SAS_ECC_INTR_IOSTLIST_ECC_MB_OFF 9
#define SAS_ECC_INTR_IOSTLIST_ECC_1B_OFF 8
#define SAS_ECC_INTR_ITCTLIST_ECC_1B_OFF 6
#define SAS_ECC_INTR_ITCTLIST_ECC_MB_OFF 7
#define SAS_ECC_INTR_CQE_ECC_1B_OFF 10
#define SAS_ECC_INTR_CQE_ECC_MB_OFF 11
#define SAS_ECC_INTR_NCQ_MEM0_ECC_MB_OFF 13
#define SAS_ECC_INTR_NCQ_MEM0_ECC_1B_OFF 12
#define SAS_ECC_INTR_NCQ_MEM1_ECC_MB_OFF 15
#define SAS_ECC_INTR_NCQ_MEM1_ECC_1B_OFF 14
#define SAS_ECC_INTR_NCQ_MEM2_ECC_MB_OFF 17
#define SAS_ECC_INTR_NCQ_MEM2_ECC_1B_OFF 16
#define SAS_ECC_INTR_NCQ_MEM3_ECC_MB_OFF 19
#define SAS_ECC_INTR_NCQ_MEM3_ECC_1B_OFF 18
#define SAS_ECC_INTR_OOO_RAM_ECC_1B_OFF 20
#define SAS_ECC_INTR_OOO_RAM_ECC_MB_OFF 21
#define HGC_ERR_STAT_EN          0x238
#define CQE_SEND_CNT             0x248
#define DLVRY_Q_0_BASE_ADDR_LO  0x260
#define DLVRY_Q_0_BASE_ADDR_HI  0x264
#define DLVRY_Q_0_DEPTH         0x268
#define DLVRY_Q_0_WR_PTR        0x26c
#define DLVRY_Q_0_RD_PTR        0x270
#define COMPL_Q_0_BASE_ADDR_LO  0x4e0
#define COMPL_Q_0_BASE_ADDR_HI  0x4e4
#define COMPL_Q_0_DEPTH         0x4e8
#define COMPL_Q_0_WR_PTR        0x4ec
#define COMPL_Q_0_RD_PTR        0x4f0
#define HYPER_STREAM_ID_EN_CFG  0xc80
#define AWQOS_AWCACHE_CFG       0xc84
#define ARQOS_ARCACHE_CFG       0xc88
#define OQ0_INT_SRC_MSK         0xc90
/* Port-specific registers */
#define PHY_CFG                 (PORT_BASE + 0x0)
#define PHY_CFG_ENA_OFF         0
#define PHY_CFG_ENA_MSK         (0x1 << PHY_CFG_ENA_OFF)
#define PHY_CFG_DC_OPT_OFF      2
#define PHY_CFG_DC_OPT_MSK      (0x1 << PHY_CFG_DC_OPT_OFF)
#define PROG_PHY_LINK_RATE      (PORT_BASE + 0x8)
#define PHY_CTRL_RESET_OFF      0
#define PHY_CTRL_RESET_MSK      (0x1 << PHY_CTRL_RESET_OFF)
#define SL_CONTROL              (PORT_BASE + 0x94)
#define SL_CONTROL_NOTIFY_EN_OFF 0
#define SL_CONTROL_NOTIFY_EN_MSK (0x1 << SL_CONTROL_NOTIFY_EN_OFF)
#define TX_ID_DWORD0            (PORT_BASE + 0x9c)
#define TX_ID_DWORD1            (PORT_BASE + 0xa0)
#define TX_ID_DWORD2            (PORT_BASE + 0xa4)
#define TX_ID_DWORD3            (PORT_BASE + 0xa8)
#define TX_ID_DWORD4            (PORT_BASE + 0xaC)
#define TX_ID_DWORD5            (PORT_BASE + 0xb0)
#define TX_ID_DWORD6            (PORT_BASE + 0xb4)
#define RX_IDAF_DWORD0          (PORT_BASE + 0xc4)
#define RXOP_CHECK_CFG_H        (PORT_BASE + 0xfc)
#define CON_CFG_DRIVER          (PORT_BASE + 0x130)
#define CHL_INT0                (PORT_BASE + 0x1b4)
#define CHL_INT1                (PORT_BASE + 0x1b8)
#define CHL_INT2                (PORT_BASE + 0x1bc)
#define CHL_INT0_MSK            (PORT_BASE + 0x1c0)
#define CHL_INT1_MSK            (PORT_BASE + 0x1c4)
#define CHL_INT2_MSK            (PORT_BASE + 0x1c8)
#define CHL_INT_COAL_EN         (PORT_BASE + 0x1d0)
#define DMA_TX_STATUS           (PORT_BASE + 0x2d0)
#define DMA_TX_STATUS_BUSY_OFF  0
#define DMA_TX_STATUS_BUSY_MSK  (0x1 << DMA_TX_STATUS_BUSY_OFF)
#define DMA_RX_STATUS           (PORT_BASE + 0x2e8)
#define DMA_RX_STATUS_BUSY_OFF  0
#define DMA_RX_STATUS_BUSY_MSK  (0x1 << DMA_RX_STATUS_BUSY_OFF)
#define AXI_CFG                 (0x5100)
#define CMD_HDR_RESP_REPORT_OFF 5
#define CMD_HDR_RESP_REPORT_MSK (0x1 << CMD_HDR_RESP_REPORT_OFF)
#define CMD_HDR_TLR_CTRL_OFF    6
#define CMD_HDR_TLR_CTRL_MSK    (0x3 << CMD_HDR_TLR_CTRL_OFF)
#define CMD_HDR_PORT_OFF        18
#define CMD_HDR_PORT_MSK        (0xf << CMD_HDR_PORT_OFF)
#define CMD_HDR_PRIORITY_OFF    27
#define CMD_HDR_PRIORITY_MSK    (0x1 << CMD_HDR_PRIORITY_OFF)
#define CMD_HDR_CMD_OFF         29
#define CMD_HDR_CMD_MSK         (0x7 << CMD_HDR_CMD_OFF)
#define CMD_HDR_CFL_OFF         0
#define CMD_HDR_CFL_MSK         (0x1ff << CMD_HDR_CFL_OFF)
#define CMD_HDR_MRFL_OFF        15
#define CMD_HDR_MRFL_MSK        (0x1ff << CMD_HDR_MRFL_OFF)
#define CMD_HDR_IPTT_OFF        0
#define CMD_HDR_IPTT_MSK        (0xffff << CMD_HDR_IPTT_OFF)
#define CMD_HDR_DATA_SGL_LEN_OFF 16
#define CMD_HDR_DATA_SGL_LEN_MSK (0xffff << CMD_HDR_DATA_SGL_LEN_OFF)
#define CMPLT_HDR_IPTT_OFF      0
#define CMPLT_HDR_IPTT_MSK      (0xffff << CMPLT_HDR_IPTT_OFF)
#define CMPLT_HDR_RSPNS_XFRD_OFF 10
#define CMPLT_HDR_RSPNS_XFRD_MSK (0x1 << CMPLT_HDR_RSPNS_XFRD_OFF)
#define ITCT_HDR_DEV_TYPE_OFF   0
#define ITCT_HDR_DEV_TYPE_MSK   (0x3 << ITCT_HDR_DEV_TYPE_OFF)
#define ITCT_HDR_VALID_OFF      2
#define ITCT_HDR_VALID_MSK      (0x1 << ITCT_HDR_VALID_OFF)
#define ITCT_HDR_PORT_ID_OFF    28
#define ITCT_HDR_PORT_ID_MSK    (0xf << ITCT_HDR_PORT_ID_OFF)
#define ITCT_HDR_SMP_TIMEOUT_OFF 16
#define HGC_LM_DFX_STATUS2      0x128
#define HGC_LM_DFX_STATUS2_IOSTLIST_OFF 0
#define HGC_LM_DFX_STATUS2_IOSTLIST_MSK (0xfff << HGC_LM_DFX_STATUS2_IOSTLIST_OFF)
#define HGC_LM_DFX_STATUS2_ITCTLIST_OFF 12
#define HGC_LM_DFX_STATUS2_ITCTLIST_MSK (0x7ff << HGC_LM_DFX_STATUS2_ITCTLIST_OFF)
#define HGC_RXM_DFX_STATUS14    0xae8
#define HGC_RXM_DFX_STATUS14_MEM0_OFF 0
#define HGC_RXM_DFX_STATUS14_MEM0_MSK (0x1ff << HGC_RXM_DFX_STATUS14_MEM0_OFF)
#define HGC_RXM_DFX_STATUS14_MEM1_OFF 9
#define HGC_RXM_DFX_STATUS14_MEM1_MSK (0x1ff << HGC_RXM_DFX_STATUS14_MEM1_OFF)
#define HGC_RXM_DFX_STATUS14_MEM2_OFF 18
#define HGC_RXM_DFX_STATUS14_MEM2_MSK (0x1ff << HGC_RXM_DFX_STATUS14_MEM2_OFF)
#define HGC_RXM_DFX_STATUS15    0xaec
#define HGC_RXM_DFX_STATUS15_MEM3_OFF 0
#define HGC_RXM_DFX_STATUS15_MEM3_MSK (0x1ff << HGC_RXM_DFX_STATUS15_MEM3_OFF)
#define CFG_MAX_TAG             0x68
#define TRANS_LOCK_ICT_TIME     0x70
#define CQ_INT_CONVERGE_EN      0xb0
#define CFG_ICT_TIMER_STEP_TRSH 0xc8
#define CFG_ABT_SET_QUERY_IPTT  0xd4
#define CFG_SET_ABORTED_IPTT_OFF 0
#define CFG_SET_ABORTED_IPTT_MSK (0xfff << CFG_SET_ABORTED_IPTT_OFF)
#define CFG_SET_ABORTED_EN_OFF  12
#define CFG_ABT_SET_IPTT_DONE   0xd8
#define CFG_ABT_SET_IPTT_DONE_OFF 0
#define CHNL_INT_STATUS         0x148
#define TAB_DFX                 0x14c
#define TAB_RD_TYPE             0x15c
#define CHNL_PHYUPDOWN_INT_MSK  0x1d0
#define CHNL_ENT_INT_MSK        0x1d4
#define HGC_COM_INT_MSK         0x1d8
#define HILINK_ERR_DFX          0xe04
#define SAS_GPIO_CFG_0          0x1000
#define SAS_GPIO_CFG_1          0x1004
#define SAS_GPIO_TX_0_1         0x1040
#define SAS_CFG_DRIVE_VLD       0x1070
#define AM_CFG_MAX_TRANS         (0x5010)
#define AM_CFG_SINGLE_PORT_MAX_TRANS (0x5014)
#define AXI_MASTER_CFG_BASE      (0x5000)
#define AM_CTRL_GLOBAL           (0x0)
#define AM_CURR_TRANS_RETURN     (0x150)
#define CMD_HDR_ABORT_FLAG_OFF   0
#define CMD_HDR_ABORT_FLAG_MSK   (0x3 << CMD_HDR_ABORT_FLAG_OFF)
#define CMD_HDR_ABORT_DEVICE_TYPE_OFF 2
#define CMD_HDR_ABORT_DEVICE_TYPE_MSK (0x1 << CMD_HDR_ABORT_DEVICE_TYPE_OFF)
#define CMD_HDR_PHY_ID_OFF       8
#define CMD_HDR_PHY_ID_MSK       (0x1ff << CMD_HDR_PHY_ID_OFF)
#define CMD_HDR_FORCE_PHY_OFF    17
#define CMD_HDR_FORCE_PHY_MSK    (0x1U << CMD_HDR_FORCE_PHY_OFF)
#define CMD_HDR_DIR_OFF          5
#define CMD_HDR_DIR_MSK          (0x3 << CMD_HDR_DIR_OFF)
#define CMD_HDR_RESET_OFF        7
#define CMD_HDR_RESET_MSK        (0x1 << CMD_HDR_RESET_OFF)
#define CMD_HDR_VDTL_OFF         10
#define CMD_HDR_VDTL_MSK         (0x1 << CMD_HDR_VDTL_OFF)
#define CMD_HDR_FRAME_TYPE_OFF   11
#define CMD_HDR_FRAME_TYPE_MSK   (0x1f << CMD_HDR_FRAME_TYPE_OFF)
#define CMD_HDR_DEV_ID_OFF       16
#define CMD_HDR_DEV_ID_MSK       (0xffff << CMD_HDR_DEV_ID_OFF)
#define CMD_HDR_NCQ_TAG_OFF      10
#define CMD_HDR_NCQ_TAG_MSK      (0x1f << CMD_HDR_NCQ_TAG_OFF)
#define CMD_HDR_SG_MOD_OFF       24
#define CMD_HDR_SG_MOD_MSK       (0x3 << CMD_HDR_SG_MOD_OFF)
#define CMD_HDR_DIF_SGL_LEN_OFF  0
#define CMD_HDR_DIF_SGL_LEN_MSK  (0xffff << CMD_HDR_DIF_SGL_LEN_OFF)
#define CMD_HDR_ABORT_IPTT_OFF   16
#define CMD_HDR_ABORT_IPTT_MSK   (0xffff << CMD_HDR_ABORT_IPTT_OFF)
#define CMPLT_HDR_ERX_OFF        12
#define CMPLT_HDR_ERX_MSK        (0x1 << CMPLT_HDR_ERX_OFF)
#define CMPLT_HDR_ABORT_STAT_OFF 13
#define CMPLT_HDR_ABORT_STAT_MSK (0x7 << CMPLT_HDR_ABORT_STAT_OFF)
#define STAT_IO_NOT_VALID        0x1
#define STAT_IO_NO_DEVICE        0x2
#define STAT_IO_COMPLETE         0x3
#define STAT_IO_ABORTED          0x4
#define CMPLT_HDR_DEV_ID_OFF     16
#define CMPLT_HDR_DEV_ID_MSK     (0xffffU << CMPLT_HDR_DEV_ID_OFF)
#define ITCT_HDR_MCR_OFF         5
#define ITCT_HDR_MCR_MSK         (0xf << ITCT_HDR_MCR_OFF)
#define ITCT_HDR_VLN_OFF         9
#define ITCT_HDR_VLN_MSK         (0xf << ITCT_HDR_VLN_OFF)
#define ITCT_HDR_AWT_CONTINUE_OFF 25
#define ITCT_HDR_INLT_OFF        0
#define ITCT_HDR_INLT_MSK        (0xffffULL << ITCT_HDR_INLT_OFF)
#define ITCT_HDR_RTOLT_OFF       48
#define ITCT_HDR_RTOLT_MSK       (0xffffULL << ITCT_HDR_RTOLT_OFF)
#define DIR_NO_DATA 0
#define DIR_TO_INI 1
#define DIR_TO_DEVICE 2
#define DIR_RESERVED 3
#define SAS_AXI_USER3            0x50
#define DEFAULT_ITCT_HW          2048
#define AM_CTRL_SHUTDOWN_REQ_OFF 0
#define AM_CTRL_SHUTDOWN_REQ_MSK (0x1 << AM_CTRL_SHUTDOWN_REQ_OFF)
#define AM_ROB_ECC_ERR_ADDR      (0x510c)
#define AM_ROB_ECC_ERR_ADDR_OFF 0
#define AM_ROB_ECC_ERR_ADDR_MSK 0xffffffff
#define RAS_BASE                 (0x6000)
#define SAS_RAS_INTR0            (RAS_BASE)
#define SAS_RAS_INTR1            (RAS_BASE + 0x04)
#define SAS_RAS_INTR0_MASK       (RAS_BASE + 0x08)
#define SAS_RAS_INTR1_MASK       (RAS_BASE + 0x0c)
#define CFG_SAS_RAS_INTR_MASK    (RAS_BASE + 0x1c)
#define SAS_RAS_INTR2            (RAS_BASE + 0x20)
#define SAS_RAS_INTR2_MASK       (RAS_BASE + 0x24)
#define CMD_HDR_UNCON_CMD_OFF     3
#define CMD_HDR_ADDR_MODE_SEL_OFF 15
#define CMD_HDR_ADDR_MODE_SEL_MSK (1 << CMD_HDR_ADDR_MODE_SEL_OFF)
#define CMPLT_HDR_CMPLT_OFF      0
#define CMPLT_HDR_CMPLT_MSK      (0x3 << CMPLT_HDR_CMPLT_OFF)
#define CMPLT_HDR_ERROR_PHASE_OFF 2
#define CMPLT_HDR_ERROR_PHASE_MSK (0xff << CMPLT_HDR_ERROR_PHASE_OFF)
#define ERR_PHASE_RESPONSE_FRAME_REV_STAGE_OFF 8
#define ERR_PHASE_RESPONSE_FRAME_REV_STAGE_MSK  (0x1 << ERR_PHASE_RESPONSE_FRAME_REV_STAGE_OFF)
#define CMPLT_HDR_RSPNS_GOOD_OFF 11
#define CMPLT_HDR_RSPNS_GOOD_MSK (0x1 << CMPLT_HDR_RSPNS_GOOD_OFF)
#define SATA_DISK_IN_ERROR_STATUS_OFF 8
#define SATA_DISK_IN_ERROR_STATUS_MSK (0x1 << SATA_DISK_IN_ERROR_STATUS_OFF)
#define CMPLT_HDR_SATA_DISK_ERR_OFF 16
#define CMPLT_HDR_SATA_DISK_ERR_MSK (0x1 << CMPLT_HDR_SATA_DISK_ERR_OFF)
#define CMPLT_HDR_IO_IN_TARGET_OFF 17
#define CMPLT_HDR_IO_IN_TARGET_MSK (0x1 << CMPLT_HDR_IO_IN_TARGET_OFF)
#define FIS_ATA_STATUS_ERR_OFF   18
#define FIS_ATA_STATUS_ERR_MSK   (0x1 << FIS_ATA_STATUS_ERR_OFF)
#define FIS_TYPE_SDB_OFF         31
#define FIS_TYPE_SDB_MSK         (0x1U << FIS_TYPE_SDB_OFF)
#define RX_DATA_LEN_UNDERFLOW_OFF 6
#define RX_DATA_LEN_UNDERFLOW_MSK (1 << RX_DATA_LEN_UNDERFLOW_OFF)
#define RX_FIS_STATUS_ERR_OFF    0
#define RX_FIS_STATUS_ERR_MSK    (1 << RX_FIS_STATUS_ERR_OFF)
#define HISI_SAS_COMMAND_ENTRIES_V3_HW 4096
#define HISI_SAS_MSI_COUNT_V3_HW 32
#define T10_INSRT_EN_OFF         0
#define T10_INSRT_EN_MSK         (1 << T10_INSRT_EN_OFF)
#define T10_RMV_EN_OFF           1
#define T10_RMV_EN_MSK           (1 << T10_RMV_EN_OFF)
#define T10_RPLC_EN_OFF          2
#define T10_RPLC_EN_MSK          (1 << T10_RPLC_EN_OFF)
#define T10_CHK_EN_OFF           3
#define T10_CHK_EN_MSK           (1 << T10_CHK_EN_OFF)
#define INCR_LBRT_OFF            5
#define INCR_LBRT_MSK            (1 << INCR_LBRT_OFF)
#define USR_DATA_BLOCK_SZ_OFF    20
#define USR_DATA_BLOCK_SZ_MSK    (0x3 << USR_DATA_BLOCK_SZ_OFF)
#define T10_CHK_MSK_OFF          16
#define T10_CHK_REF_TAG_MSK      (0xf0 << T10_CHK_MSK_OFF)
#define T10_CHK_APP_TAG_MSK      (0xc << T10_CHK_MSK_OFF)
#define BASE_VECTORS_V3_HW       16
#define MIN_AFFINE_VECTORS_V3_HW (BASE_VECTORS_V3_HW + 1)
#define IRQ_PHY_UP_DOWN_INDEX 1
#define IRQ_CHL_INDEX 2
#define IRQ_AXI_INDEX 11
#define DELAY_FOR_RESET_HW 100
#define HDR_SG_MOD 0x2
#define ATTR_PRIO_REGION 9
#define CDB_REGION 12
#define PRIO_OFF 3
#define TMF_REGION 10
#define TAG_MSB 12
#define TAG_LSB 13
#define SMP_FRAME_TYPE 2
#define SMP_CRC_SIZE 4
#define HDR_TAG_OFF 3
#define HOST_NO_OFF 6
#define PHY_NO_OFF 7
#define IDENTIFY_REG_READ 6
#define LINK_RESET_TIMEOUT_OFF 4
#define DECIMALISM_FLAG 10
#define WAIT_RETRY 100
#define WAIT_TMROUT 5000
#define ID_DWORD0_INDEX 0
#define ID_DWORD1_INDEX 1
#define ID_DWORD2_INDEX 2
#define ID_DWORD3_INDEX 3
#define ID_DWORD4_INDEX 4
#define ID_DWORD5_INDEX 5
#define TICKS_BIT_INDEX 24
#define COUNT_BIT_INDEX 8
#define PORT_REG_LENGTH        0x100
#define GLOBAL_REG_LENGTH      0x800
#define AXI_REG_LENGTH         0x61
#define RAS_REG_LENGTH         0x10
#define CHNL_INT_STS_MSK       0xeeeeeeee
#define CHNL_INT_STS_PHY_MSK   0xe
#define CHNL_INT_STS_INT0_MSK BIT(1)
#define CHNL_INT_STS_INT1_MSK BIT(2)
#define CHNL_INT_STS_INT2_MSK BIT(3)
#define CHNL_WIDTH 4
#define BAR_NO_V3_HW           5
#define HISI_SAS_QUEUE_SLOTS   4096
#define HISI_SAS_CLEAR_ITCT_TIMEOUT (20 * HZ)
#define HISI_SAS_DELAY_FOR_PHY_DISABLE 100
#define HISI_SAS_REG_MEM_SIZE 4
#define HISI_SAS_MAX_SSP_RESP_SZ (sizeof(struct ssp_frame_hdr) + 1024)
#define HISI_SAS_MAX_SMP_RESP_SZ 1028
#define HISI_SAS_MAX_STP_RESP_SZ 28
#define HISI_SAS_REJECT_CMD_BIT 1
#define HISI_SAS_IOST_ITCT_CACHE_DW_SZ 10
#define HISI_SAS_IOST_ITCT_CACHE_NUM 64
#define HISI_SAS_PROT_MASK (HISI_SAS_DIF_PROT_MASK | HISI_SAS_DIX_PROT_MASK)
#define HISI_SAS_MAX_ITCT_ENTRIES 1024
#define HISI_SAS_MAX_COMMANDS (HISI_SAS_QUEUE_SLOTS)
#define HISI_SAS_MAX_CDB_LEN 16
#define HISI_SAS_MAX_DEVICES HISI_SAS_MAX_ITCT_ENTRIES
#define HISI_SAS_RESETTING_BIT 0
#define HISI_SAS_PM_BIT          2
#define HISI_SAS_SGE_PAGE_CNT (124)
#define HISI_SAS_MAX_PHYS    9
#define HISI_SAS_MAX_DEBUGFS_DUMP 50
#define HISI_SAS_MAX_QUEUES   32
#define FRAME_RCVD_BUF 32
#define SAS_PHY_RESV_SIZE 2
#define ITCT_RESV_DDW 12
#define HISI_SAS_SGE_DIF_PAGE_CNT   HISI_SAS_SGE_PAGE_CNT
#define HISI_SAS_SATA_PROTOCOL_FPDMA         0x8
#define HISI_SAS_SATA_PROTOCOL_DMA           0x4
#define HISI_SAS_SATA_PROTOCOL_PIO           0x2
#define HISI_SAS_SATA_PROTOCOL_NONDATA       0x1
#define FIS_RESV_DW 3
#define HISI_SAS_WAIT_PHYUP_TIMEOUT (30 * HZ)
#define ERROR_RECORD_BUF_DW 4
#define HISI_SAS_BLK_QUEUE_DEPTH 64
#define HISI_SAS_DIF_PROT_MASK (SHOST_DIF_TYPE1_PROTECTION | SHOST_DIF_TYPE2_PROTECTION | SHOST_DIF_TYPE3_PROTECTION)
#define HISI_SAS_RESERVED_IPTT  96
#define BREAKPOINT_DATA_SIZE 128
#define IU_BUF_SIZE 1024
#define BREAKPOINT_TAG_NUM 32
#define SMP_CMD_TABLE_SIZE 44
#define DUMMY_BUF_SIZE 12
#define PROT_BUF_SIZE 7
#define HISI_SAS_UNRESERVED_IPTT (HISI_SAS_MAX_COMMANDS - HISI_SAS_RESERVED_IPTT)
#define HISI_SAS_DIX_PROT_MASK (SHOST_DIX_TYPE1_PROTECTION | SHOST_DIX_TYPE2_PROTECTION | SHOST_DIX_TYPE3_PROTECTION)

/* End of driver-specific definitions */

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

#define REG_AREA_SIZE 0x8000
#define REG_ARRAY_SIZE (REG_AREA_SIZE / sizeof(uint32_t))

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Register shadow array */
    uint32_t regs[REG_ARRAY_SIZE];

    /* DMA context placeholder */
    void *dma_ptr;

    uint32_t status;      /* Operational status */
    bool in_reset;        /* Reset sequence state */
    uint8_t pm_state;     /* Power management state */
    /* Additional placeholder */
    uint32_t reserved;

    /* SAS address for property */
    uint64_t sas_addr;

    /* Additional firmware properties required by driver */
    uint32_t phy_count;
    uint32_t queue_count;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (addr < REG_AREA_SIZE) {
        val = s->regs[addr / 4];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out-of-bounds read at 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < REG_AREA_SIZE) {
        s->regs[addr / 4] = (uint32_t)val;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out-of-bounds write at 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all registers to 0, then set known reset values */
    memset(s->regs, 0, sizeof(s->regs));

    /* AXI bus idle */
    s->regs[AXI_CFG / 4] = 0;

    /* Do not reset sas_addr; it is a permanent property */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                  BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_HUAWEI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_HISI_SAS_V3);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SAS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0,
                                    PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Add SAS capability with the SAS address */
    int sas_cap = pci_add_capability(pdev, PCI_CAP_ID_SAS, 0, 10, errp);
    if (sas_cap < 0) {
        return;
    }
    pci_set_quad(pci_conf + sas_cap + 2, s->sas_addr);

    /* BAR Initialization: 6 BARs; BAR5 is register MMIO */
    s->num_bars = 6;
    /* BAR0-3: Dummy RAM regions */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM,
                                .size = 0x1000, .name = "bar0-dummy" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM,
                                .size = 0x1000, .name = "bar1-dummy" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM,
                                .size = 0x1000, .name = "bar2-dummy" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_RAM,
                                .size = 0x1000, .name = "bar3-dummy" };
    /* BAR4: MSI-X table */
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_RAM,
                                .size = 0x1000, .name = "bar4-msix" };
    /* BAR5: Main MMIO register space */
    s->bar_info[5] = (BARInfo){ .index = 5, .type = BAR_TYPE_MMIO,
                                .size = REG_AREA_SIZE, .name = "bar5-regs" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    if (msix_init_exclusive_bar(pdev, HISI_SAS_MSI_COUNT_V3_HW, 4, errp)) {
        return;
    }
    s->has_msix = true;

    /* Reset registers to known state */
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[AXI_CFG / 4] = 0;  /* Ensure AXI bus idle */

    /* Additional field initialization */
    s->dma_ptr = NULL;
    s->status = 0;
    s->in_reset = false;
    s->pm_state = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* No additional uninit required */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "hisi_sas_v3_hw_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(regs, PCIBaseState, REG_ARRAY_SIZE),
        VMSTATE_UINT32(status, PCIBaseState),
        VMSTATE_BOOL(in_reset, PCIBaseState),
        VMSTATE_UINT8(pm_state, PCIBaseState),
        VMSTATE_UINT64(sas_addr, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_instance_init(Object *obj)
{
    PCIBaseState *s = PCIBASE_DEVICE(obj);

    /* Set default firmware properties; can be overridden by user */
    s->sas_addr = 0x5700000000000001ULL;
    s->phy_count = 8;  /* Default to 8 phys, driver expects at most 9 */
    s->queue_count = 16;  /* Reasonable default number of queues */

    object_property_add_uint64_ptr(obj, "sas-addr", &s->sas_addr,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "phy-count", &s->phy_count,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint32_ptr(obj, "queue-count", &s->queue_count,
                                   OBJ_PROP_FLAG_READWRITE);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
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
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);