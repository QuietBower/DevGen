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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "hfi1_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* Base addresses */
#define CORE		0x000000000000
#define RXE		(CORE + 0x000001000000)
#define TXE		(CORE + 0x000001800000)
#define ASIC		(CORE + 0x000000400000)
#define MISC		(CORE + 0x000000500000)
#define DC_TOP_CSRS		(CORE + 0x000000600000)
#define DCC_CSRS		(DC_TOP_CSRS + 0x000000000000)
#define DC_8051_CSRS		(DC_TOP_CSRS + 0x000000002000)
#define DC_LCB_CSRS		(DC_TOP_CSRS + 0x000000001000)
#define PCIE		0

#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL0 0x24f0
#define PCI_DEVICE_ID_INTEL1 0x24f1

/* Register offsets and bitfields */

#define CCE_REVISION (CCE + 0x000000000000)
#define CCE_REVISION_SW_MASK 0xFFull
#define CCE_REVISION_SW_SHIFT 24
#define CCE_REVISION_CHIP_REV_MAJOR_SHIFT 8
#define CCE_REVISION_CHIP_REV_MAJOR_MASK 0xFFull
#define CCE_REVISION_CHIP_REV_MINOR_MASK 0xFFull
#define CCE_REVISION_CHIP_REV_MINOR_SHIFT 0
#define CCE_REVISION2 (CCE + 0x000000000008)
#define CCE_REVISION2_HFI_ID_MASK 0x1ull
#define CCE_REVISION2_HFI_ID_SHIFT 0
#define CCE_REVISION2_IMPL_CODE_SHIFT 8
#define CCE_REVISION2_IMPL_REVISION_SHIFT 16
#define CCE_REVISION2_IMPL_REVISION_MASK 0xFFull

#define CCE_DC_CTRL (CCE + 0x0000000000B8)
#define CCE_DC_CTRL_DC_RESET_SMASK 0x1ull
#define CCE_DC_CTRL_RESETCSR 0x0000000000000001ull

#define CCE_CTRL (CCE + 0x000000000010)
#define CCE_CTRL_SPC_UNFREEZE_SMASK 0x200ull
#define CCE_CTRL_SPC_FREEZE_SMASK 0x100ull
#define CCE_CTRL_TXE_RESUME_SMASK 0x2000ull
#define CCE_CTRL_RXE_RESUME_SMASK 0x800ull

#define CCE_STATUS (CCE + 0x000000000018)
#define CCE_STATUS_TXE_FROZE_SMASK 0x4ull
#define CCE_STATUS_SDMA_FROZE_SMASK 0x1ull
#define CCE_STATUS_RXE_FROZE_SMASK 0x2ull
#define CCE_STATUS_TXE_PIO_FROZE_SMASK 0x8ull
#define CCE_STATUS_RXE_PAUSED_SMASK 0x20ull
#define CCE_STATUS_SDMA_PAUSED_SMASK 0x10ull
#define CCE_STATUS_TXE_PAUSED_SMASK 0x40ull
#define CCE_STATUS_TXE_PIO_PAUSED_SMASK 0x80ull
#define CCE_STATUS_TIMEOUT 10

#define CCE_INT_STATUS (CCE + 0x000000110800)
#define CCE_INT_MASK (CCE + 0x000000110900)
#define CCE_INT_CLEAR (CCE + 0x000000110A00)
#define CCE_INT_FORCE (CCE + 0x000000110B00)
#define CCE_INT_BLOCKED (CCE + 0x000000110C00)
#define CCE_INT_MAP (CCE + 0x000000110500)
#define CCE_NUM_INT_CSRS 12
#define CCE_NUM_INT_MAP_CSRS 96
#define CCE_NUM_MSIX_VECTORS 256
#define CCE_NUM_MSIX_PBAS 4
#define CCE_MSIX_TABLE_LOWER (CCE + 0x000000100000)
#define CCE_MSIX_TABLE_UPPER (CCE + 0x000000100008)
#define CCE_MSIX_VEC_CLR_WITHOUT_INT (CCE + 0x000000110400)
#define CCE_MSIX_INT_GRANTED (CCE + 0x000000110200)

#define CCE_INT_COUNTER_ARRAY32 (CCE + 0x000000110D00)
#define CCE_COUNTER_ARRAY32 (CCE + 0x000000000060)
#define CCE_NUM_32_BIT_INT_COUNTERS 6
#define CCE_NUM_32_BIT_COUNTERS 3

#define CCE_ERR_MASK (CCE + 0x000000000048)
#define CCE_ERR_CLEAR (CCE + 0x000000000050)

#define CCE_SCRATCH (CCE + 0x000000000020)
#define CCE_NUM_SCRATCH 4

#define CCE_PCIE_CTRL (CCE + 0x0000000000C0)

#define RCV_CTRL (RXE + 0x000000000000)
#define RCV_CTRL_RCV_PORT_ENABLE_SMASK 0x1ull
#define RCV_CTRL_RCV_EXTENDED_PSN_ENABLE_SMASK 0x40ull
#define RCV_CTRL_RCV_PARTITION_KEY_ENABLE_SMASK 0x4ull
#define RCV_CTRL_RCV_RSM_ENABLE_SMASK 0x20ull
#define RCV_CTRL_RCV_QP_MAP_ENABLE_SMASK 0x2ull
#define RCV_CTRL_RCV_BYPASS_ENABLE_SMASK 0x10ull
#define RCV_CTRL_RX_RBUF_INIT_SMASK 0x200ull

#define RCV_STATUS (RXE + 0x000000000008)
#define RCV_STATUS_RX_RBUF_INIT_DONE_SMASK 0x200ull
#define RCV_STATUS_RX_RBUF_PKT_PENDING_SMASK 0x40ull
#define RCV_STATUS_RX_PKT_IN_PROGRESS_SMASK 0x1ull

#define RCV_CONTEXTS (RXE + 0x000000000010)

#define RCV_ARRAY_CNT (RXE + 0x000000000018)

#define RCV_BTH_QP (RXE + 0x000000000028)
#define RCV_BTH_QP_KDETH_QP_MASK 0xFFull
#define RCV_BTH_QP_KDETH_QP_SHIFT 16

#define RCV_MULTICAST (RXE + 0x000000000030)

#define RCV_BYPASS (RXE + 0x000000000038)
#define RCV_BYPASS_HDR_SIZE_SHIFT 16
#define RCV_BYPASS_HDR_SIZE_SMASK 0x1F0000ull
#define RCV_BYPASS_HDR_SIZE_MASK 0x1Full

#define RCV_VL15 (RXE + 0x000000000048)

#define RCV_ERR_INFO (RXE + 0x000000000050)
#define RCV_ERR_INFO_RCV_EXCESS_BUFFER_OVERRUN_SMASK 0x20ull

#define RCV_ERR_MASK (RXE + 0x000000000068)
#define RCV_ERR_CLEAR (RXE + 0x000000000070)

#define RCV_QP_MAP_TABLE (RXE + 0x000000000100)

#define RCV_PARTITION_KEY (RXE + 0x000000000200)
#define RCV_PARTITION_KEY_PARTITION_KEY_B_SHIFT 16
#define RCV_PARTITION_KEY_PARTITION_KEY_A_MASK 0xFFFFull

#define RCV_COUNTER_ARRAY32 (RXE + 0x000000000400)
#define RCV_COUNTER_ARRAY64 (RXE + 0x000000000500)

#define RCV_RSM_CFG (RXE + 0x000000000600)
#define RCV_RSM_MATCH (RXE + 0x000000000800)
#define RCV_RSM_SELECT (RXE + 0x000000000700)

#define RCV_RSM_MAP_TABLE (RXE + 0x000000000900)
#define RCV_RSM_MAP_TABLE_RCV_CONTEXT_A_MASK 0xFFull

#define RCV_CTXT_CTRL (RXE + 0x000000100000)
#define RCV_CTXT_CTRL_ENABLE_SMASK 0x1ull
#define RCV_CTXT_CTRL_ONE_PACKET_PER_EGR_BUFFER_SMASK 0x2ull
#define RCV_CTXT_CTRL_DONT_DROP_EGR_FULL_SMASK 0x4ull
#define RCV_CTXT_CTRL_DONT_DROP_RHQ_FULL_SMASK 0x8ull
#define RCV_CTXT_CTRL_TID_FLOW_ENABLE_SMASK 0x10ull
#define RCV_CTXT_CTRL_INTR_AVAIL_SMASK 0x20ull
#define RCV_CTXT_CTRL_TAIL_UPD_SMASK 0x40ull
#define RCV_CTXT_CTRL_EGR_BUF_SIZE_SHIFT 8
#define RCV_CTXT_CTRL_EGR_BUF_SIZE_MASK 0x7ull
#define RCV_CTXT_CTRL_EGR_BUF_SIZE_SMASK 0x700ull

#define RCV_CTXT_STATUS (RXE + 0x000000100008)

#define RCV_EGR_CTRL (RXE + 0x000000100010)
#define RCV_EGR_CTRL_EGR_BASE_INDEX_MASK 0x1FFFull
#define RCV_EGR_CTRL_EGR_BASE_INDEX_SHIFT 0
#define RCV_EGR_CTRL_EGR_CNT_MASK 0x1FFull
#define RCV_EGR_CTRL_EGR_CNT_SHIFT 32

#define RCV_TID_CTRL (RXE + 0x000000100018)
#define RCV_TID_CTRL_TID_BASE_INDEX_MASK 0x1FFFull
#define RCV_TID_CTRL_TID_BASE_INDEX_SHIFT 0
#define RCV_TID_CTRL_TID_PAIR_CNT_MASK 0x1FFull
#define RCV_TID_CTRL_TID_PAIR_CNT_SHIFT 32

#define RCV_KEY_CTRL (RXE + 0x000000100020)
#define RCV_KEY_CTRL_JOB_KEY_ENABLE_SMASK 0x200000000ull
#define RCV_KEY_CTRL_JOB_KEY_VALUE_MASK 0xFFFFull
#define RCV_KEY_CTRL_JOB_KEY_VALUE_SHIFT 0

#define RCV_HDR_ADDR (RXE + 0x000000100028)
#define RCV_HDR_CNT (RXE + 0x000000100030)
#define RCV_HDR_CNT_CNT_MASK 0x1FFull
#define RCV_HDR_CNT_CNT_SHIFT 0
#define RCV_HDR_ENT_SIZE (RXE + 0x000000100038)
#define RCV_HDR_ENT_SIZE_ENT_SIZE_MASK 0x7ull
#define RCV_HDR_ENT_SIZE_ENT_SIZE_SHIFT 0
#define RCV_HDR_SIZE (RXE + 0x000000100040)
#define RCV_HDR_SIZE_HDR_SIZE_MASK 0x1Full
#define RCV_HDR_SIZE_HDR_SIZE_SHIFT 0
#define RCV_HDR_TAIL_ADDR (RXE + 0x000000100048)
#define RCV_AVAIL_TIME_OUT (RXE + 0x000000100050)
#define RCV_AVAIL_TIME_OUT_TIME_OUT_RELOAD_MASK 0xFFull
#define RCV_AVAIL_TIME_OUT_TIME_OUT_RELOAD_SHIFT 0
#define RCV_HDR_OVFL_CNT (RXE + 0x000000100058)

#define RCV_HDR_HEAD (RXE + 0x000000300008)
#define RCV_HDR_HEAD_HEAD_MASK 0x7FFFFull
#define RCV_HDR_HEAD_HEAD_SHIFT 0
#define RCV_HDR_HEAD_COUNTER_MASK 0xFFull
#define RCV_HDR_HEAD_COUNTER_SHIFT 32

#define RCV_EGR_INDEX_HEAD (RXE + 0x000000300018)
#define RCV_EGR_INDEX_HEAD_HEAD_MASK 0x7FFull
#define RCV_EGR_INDEX_HEAD_HEAD_SHIFT 0

#define RCV_TID_FLOW_TABLE (RXE + 0x000000300800)

#define RCV_ARRAY (RXE + 0x000000200000)
#define RCV_ARRAY_RT_ADDR_SHIFT 0
#define RCV_ARRAY_RT_ADDR_MASK 0xFFFFFFFFFull
#define RCV_ARRAY_RT_BUF_SIZE_SHIFT 36
#define RCV_ARRAY_RT_WRITE_ENABLE_SMASK 0x8000000000000000ull

#define SEND_CTRL (TXE + 0x000000000000)
#define SEND_CTRL_SEND_ENABLE_SMASK 0x1ull
#define SEND_CTRL_VL_ARBITER_ENABLE_SMASK 0x2ull
#define SEND_CTRL_UNSUPPORTED_VL_MASK 0xFFull
#define SEND_CTRL_UNSUPPORTED_VL_SHIFT 3
#define SEND_CTRL_UNSUPPORTED_VL_SMASK (SEND_CTRL_UNSUPPORTED_VL_MASK << SEND_CTRL_UNSUPPORTED_VL_SHIFT)

#define SEND_CONTEXTS (TXE + 0x000000000010)

#define SEND_DMA_ENGINES (TXE + 0x000000000018)

#define SEND_PIO_MEM_SIZE (TXE + 0x000000000020)

#define SEND_HIGH_PRIORITY_LIMIT (TXE + 0x000000000030)
#define SEND_HIGH_PRIORITY_LIMIT_LIMIT_MASK 0x3FFFull
#define SEND_HIGH_PRIORITY_LIMIT_LIMIT_SHIFT 0

#define SEND_PIO_INIT_CTXT (TXE + 0x000000000038)
#define SEND_PIO_INIT_CTXT_PIO_CTXT_NUM_MASK 0xFFull
#define SEND_PIO_INIT_CTXT_PIO_CTXT_NUM_SHIFT 8
#define SEND_PIO_INIT_CTXT_PIO_SINGLE_CTXT_INIT_SMASK 0x2ull
#define SEND_PIO_INIT_CTXT_PIO_ALL_CTXT_INIT_SMASK 0x1ull
#define SEND_PIO_INIT_CTXT_PIO_INIT_IN_PROGRESS_SMASK 0x4ull
#define SEND_PIO_INIT_CTXT_PIO_INIT_ERR_SMASK 0x8ull

#define SEND_PIO_ERR_MASK (TXE + 0x000000000048)
#define SEND_PIO_ERR_CLEAR (TXE + 0x000000000050)

#define SEND_DMA_ERR_MASK (TXE + 0x000000000068)
#define SEND_DMA_ERR_CLEAR (TXE + 0x000000000070)

#define SEND_EGRESS_ERR_MASK (TXE + 0x000000000088)
#define SEND_EGRESS_ERR_CLEAR (TXE + 0x000000000090)

#define SEND_BTH_QP (TXE + 0x0000000000A0)
#define SEND_BTH_QP_KDETH_QP_MASK 0xFFull
#define SEND_BTH_QP_KDETH_QP_SHIFT 16

#define SEND_STATIC_RATE_CONTROL (TXE + 0x0000000000A8)
#define SEND_STATIC_RATE_CONTROL_CSR_SRC_RELOAD_SMASK 0xFFFFull
#define SEND_STATIC_RATE_CONTROL_CSR_SRC_RELOAD_SHIFT 0

#define SEND_SC2VLT0 (TXE + 0x0000000000B0)
#define SEND_SC2VLT1 (TXE + 0x0000000000B8)
#define SEND_SC2VLT2 (TXE + 0x0000000000C0)
#define SEND_SC2VLT3 (TXE + 0x0000000000C8)

#define SEND_LEN_CHECK0 (TXE + 0x0000000000D0)
#define SEND_LEN_CHECK0_LEN_VL0_MASK 0xFFFull
#define SEND_LEN_CHECK0_LEN_VL1_SHIFT 12

#define SEND_LEN_CHECK1 (TXE + 0x0000000000D8)
#define SEND_LEN_CHECK1_LEN_VL4_MASK 0xFFFull
#define SEND_LEN_CHECK1_LEN_VL5_SHIFT 12
#define SEND_LEN_CHECK1_LEN_VL15_SHIFT 48
#define SEND_LEN_CHECK1_LEN_VL15_MASK 0xFFFull

#define SEND_ERR_MASK (TXE + 0x0000000000E8)
#define SEND_ERR_CLEAR (TXE + 0x0000000000F0)

#define SEND_LOW_PRIORITY_LIST (TXE + 0x000000000100)
#define SEND_HIGH_PRIORITY_LIST (TXE + 0x000000000180)

#define SEND_CONTEXT_SET_CTRL (TXE + 0x000000000200)

#define SEND_COUNTER_ARRAY32 (TXE + 0x000000000300)
#define SEND_COUNTER_ARRAY64 (TXE + 0x000000000400)

#define SEND_CM_CTRL (TXE + 0x000000000500)
#define SEND_CM_CTRL_RESETCSR 0x0000000000000020ull

#define SEND_CM_GLOBAL_CREDIT (TXE + 0x000000000508)
#define SEND_CM_GLOBAL_CREDIT_RESETCSR 0x0000094000030000ull
#define SEND_CM_GLOBAL_CREDIT_AU_SMASK 0x70000ull
#define SEND_CM_GLOBAL_CREDIT_AU_SHIFT 16
#define SEND_CM_GLOBAL_CREDIT_SHARED_LIMIT_SMASK 0xFFFFull
#define SEND_CM_GLOBAL_CREDIT_TOTAL_CREDIT_LIMIT_SHIFT 32
#define SEND_CM_GLOBAL_CREDIT_TOTAL_CREDIT_LIMIT_SMASK 0xFFFF00000000ull

#define SEND_CM_TIMER_CTRL (TXE + 0x000000000518)

#define SEND_CM_LOCAL_AU_TABLE0_TO3 (TXE + 0x000000000520)
#define SEND_CM_LOCAL_AU_TABLE4_TO7 (TXE + 0x000000000528)

#define SEND_CM_REMOTE_AU_TABLE0_TO3 (TXE + 0x000000000530)
#define SEND_CM_REMOTE_AU_TABLE4_TO7 (TXE + 0x000000000538)

#define SEND_CM_CREDIT_VL (TXE + 0x000000000600)
#define SEND_CM_CREDIT_VL15 (TXE + 0x000000000678)
#define SEND_CM_CREDIT_VL15_DEDICATED_LIMIT_VL_SHIFT 0

#define SEND_EGRESS_CTXT_STATUS (TXE + 0x000000000800)
#define SEND_EGRESS_CTXT_STATUS_CTXT_EGRESS_PACKET_OCCUPANCY_SMASK 0x3FFFull
#define SEND_EGRESS_CTXT_STATUS_CTXT_EGRESS_PACKET_OCCUPANCY_SHIFT 0
#define SEND_EGRESS_CTXT_STATUS_CTXT_EGRESS_HALT_STATUS_SMASK 0x10000ull

#define SEND_EGRESS_ERR_INFO (TXE + 0x000000000F00)

#define SEND_EGRESS_SEND_DMA_STATUS (TXE + 0x000000000E00)
#define SEND_EGRESS_SEND_DMA_STATUS_SDMA_EGRESS_PACKET_OCCUPANCY_SHIFT 0
#define SEND_EGRESS_SEND_DMA_STATUS_SDMA_EGRESS_PACKET_OCCUPANCY_SMASK 0x3FFFull

#define SEND_CTXT_CTRL (TXE + 0x000000100000)
#define SEND_CTXT_CREDIT_CTRL (TXE + 0x000000100010)
#define SEND_CTXT_CREDIT_RETURN_ADDR (TXE + 0x000000100020)
#define SEND_CTXT_CREDIT_FORCE (TXE + 0x000000100028)
#define SEND_CTXT_ERR_MASK (TXE + 0x000000100048)
#define SEND_CTXT_ERR_CLEAR (TXE + 0x000000100050)

#define SEND_CTXT_CHECK_ENABLE (TXE + 0x000000100080)
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_PBC_STATIC_RATE_CONTROL_SMASK 0x100000ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_RAW_SMASK 0x100ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_RAW_IPV6_SMASK 0x200ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_SMALL_BYPASS_PACKETS_SMASK 0x8000ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_LONG_BYPASS_PACKETS_SMASK 0x80000ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_SMALL_IB_PACKETS_SMASK 0x4000ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_TOO_LONG_IB_PACKETS_SMASK 0x40000ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_BYPASS_BAD_PKT_LEN_SMASK 0x200000ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_BAD_PKT_LEN_SMASK 0x20000ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_PBC_TEST_SMASK 0x10000ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_KDETH_PACKETS_SMASK 0x1000ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_NON_KDETH_PACKETS_SMASK 0x2000ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_BYPASS_SMASK 0x800ull
#define SEND_CTXT_CHECK_ENABLE_DISALLOW_GRH_SMASK 0x400ull
#define SEND_CTXT_CHECK_ENABLE_CHECK_VL_MAPPING_SMASK 0x40ull
#define SEND_CTXT_CHECK_ENABLE_CHECK_OPCODE_SMASK 0x20ull
#define SEND_CTXT_CHECK_ENABLE_CHECK_ENABLE_SMASK 0x1ull
#define SEND_CTXT_CHECK_ENABLE_CHECK_SLID_SMASK 0x10ull
#define SEND_CTXT_CHECK_ENABLE_CHECK_BYPASS_VL_MAPPING_SMASK 0x80ull
#define SEND_CTXT_CHECK_ENABLE_CHECK_JOB_KEY_SMASK 0x4ull
#define SEND_CTXT_CHECK_ENABLE_CHECK_VL_SMASK 0x2ull

#define SEND_CTXT_CHECK_VL (TXE + 0x000000100088)
#define SEND_CTXT_CHECK_SLID (TXE + 0x0000001000A0)
#define SEND_CTXT_CHECK_OPCODE (TXE + 0x0000001000A8)
#define SEND_CTXT_CHECK_JOB_KEY (TXE + 0x000000100090)
#define SEND_CTXT_CHECK_JOB_KEY_VALUE_MASK 0xFFFFull
#define SEND_CTXT_CHECK_JOB_KEY_VALUE_SHIFT 0
#define SEND_CTXT_CHECK_JOB_KEY_ALLOW_PERMISSIVE_SMASK 0x100000000ull
#define SEND_CTXT_CHECK_JOB_KEY_MASK_SMASK 0xFFFF0000ull

#define SEND_CTXT_CHECK_OPCODE_MASK_SHIFT 8
#define SEND_CTXT_CHECK_OPCODE_VALUE_SHIFT 0
#define OPCODE_CHECK_MASK_DISABLED 0x0
#define USER_OPCODE_CHECK_VAL 0xC0
#define USER_OPCODE_CHECK_MASK 0xC0
#define OPCODE_CHECK_VAL_DISABLED 0x0

#define SEND_DMA_CTRL (TXE + 0x000000200000)
#define SEND_DMA_BASE_ADDR (TXE + 0x000000200010)
#define SEND_DMA_LEN_GEN (TXE + 0x000000200018)
#define SEND_DMA_TAIL (TXE + 0x000000200020)
#define SEND_DMA_HEAD_ADDR (TXE + 0x000000200030)
#define SEND_DMA_PRIORITY_THLD (TXE + 0x000000200038)
#define SEND_DMA_RELOAD_CNT (TXE + 0x000000200048)
#define SEND_DMA_DESC_CNT (TXE + 0x000000200050)
#define SEND_DMA_DESC_FETCHED_CNT (TXE + 0x000000200058)
#define SEND_DMA_ENG_ERR_MASK (TXE + 0x000000200068)
#define SEND_DMA_ENG_ERR_CLEAR (TXE + 0x000000200070)

#define SEND_DMA_CHECK_ENABLE (TXE + 0x000000200080)
#define SEND_DMA_CHECK_ENABLE_DISALLOW_BYPASS_BAD_PKT_LEN_SMASK 0x200000ull
#define SEND_DMA_CHECK_ENABLE_DISALLOW_TOO_LONG_BYPASS_PACKETS_SMASK 0x80000ull
#define SEND_DMA_CHECK_ENABLE_DISALLOW_TOO_SMALL_IB_PACKETS_SMASK 0x4000ull
#define SEND_DMA_CHECK_ENABLE_DISALLOW_TOO_LONG_IB_PACKETS_SMASK 0x40000ull
#define SEND_DMA_CHECK_ENABLE_DISALLOW_RAW_SMASK 0x100ull
#define SEND_DMA_CHECK_ENABLE_DISALLOW_RAW_IPV6_SMASK 0x200ull
#define SEND_DMA_CHECK_ENABLE_DISALLOW_TOO_SMALL_BYPASS_PACKETS_SMASK 0x8000ull
#define SEND_DMA_CHECK_ENABLE_DISALLOW_BAD_PKT_LEN_SMASK 0x20000ull
#define SEND_DMA_CHECK_ENABLE_DISALLOW_PBC_STATIC_RATE_CONTROL_SMASK 0x100000ull
#define SEND_DMA_CHECK_ENABLE_CHECK_SLID_SMASK 0x10ull
#define SEND_DMA_CHECK_ENABLE_CHECK_OPCODE_SMASK 0x20ull
#define SEND_DMA_CHECK_ENABLE_CHECK_JOB_KEY_SMASK 0x4ull
#define SEND_DMA_CHECK_ENABLE_CHECK_VL_SMASK 0x2ull
#define SEND_DMA_CHECK_ENABLE_CHECK_VL_MAPPING_SMASK 0x40ull
#define SEND_DMA_CHECK_ENABLE_CHECK_BYPASS_VL_MAPPING_SMASK 0x80ull
#define SEND_DMA_CHECK_ENABLE_CHECK_ENABLE_SMASK 0x1ull

#define SEND_DMA_CHECK_VL (TXE + 0x000000200088)
#define SEND_DMA_CHECK_SLID (TXE + 0x0000002000A0)
#define SEND_DMA_CHECK_OPCODE (TXE + 0x0000002000A8)
#define SEND_DMA_CHECK_JOB_KEY (TXE + 0x000000200090)
#define SEND_DMA_CHECK_PARTITION_KEY (TXE + 0x000000200098)

#define SEND_DMA_MEMORY (TXE + 0x0000002000B0)

#define TXE_PIO_SEND_OFFSET 0x0800000
#define TXE_PIO_SEND (TXE + TXE_PIO_SEND_OFFSET)
#define TXE_PIO_SIZE (32 * 0x100000)

#define DCC_CFG_RESET (DCC_CSRS + 0x000000000000)
#define DCC_CFG_RESET_RESET_LCB BIT_ULL(0)
#define DCC_CFG_RESET_RESET_RX_FPE BIT_ULL(2)

#define DCC_CFG_PORT_CONFIG (DCC_CSRS + 0x000000000008)
#define DCC_CFG_PORT_CONFIG_LINK_STATE_SHIFT 48
#define DCC_CFG_PORT_CONFIG_LINK_STATE_SMASK 0x7000000000000ull
#define DCC_CFG_PORT_CONFIG_LINK_STATE_MASK 0x7ull
#define DCC_CFG_PORT_CONFIG_MTU_CAP_SHIFT 32
#define DCC_CFG_PORT_CONFIG_MTU_CAP_SMASK 0x700000000ull
#define DCC_CFG_PORT_CONFIG_MTU_CAP_MASK 0x7ull

#define DCC_CFG_PORT_CONFIG1 (DCC_CSRS + 0x000000000010)
#define DCC_CFG_PORT_CONFIG1_TARGET_DLID_SMASK 0xFFFFull
#define DCC_CFG_PORT_CONFIG1_TARGET_DLID_SHIFT 0
#define DCC_CFG_PORT_CONFIG1_TARGET_DLID_MASK 0xFFFFull
#define DCC_CFG_PORT_CONFIG1_DLID_MASK_SHIFT 16
#define DCC_CFG_PORT_CONFIG1_DLID_MASK_MASK 0xFFFFull
#define DCC_CFG_PORT_CONFIG1_DLID_MASK_SMASK 0xFFFF0000ull

#define DCC_CFG_SC_VL_TABLE_15_0 (DCC_CSRS + 0x000000000028)
#define DCC_CFG_SC_VL_TABLE_31_16 (DCC_CSRS + 0x000000000030)

#define DCC_CFG_LED_CNTRL (DCC_CSRS + 0x000000000040)

#define DCC_ERR_FLG_EN (DCC_CSRS + 0x000000000058)
#define DCC_ERR_FLG_CLR (DCC_CSRS + 0x000000000060)

#define DCC_ERR_FMCONFIG_ERR_CNT (DCC_CSRS + 0x000000000110)
#define DCC_ERR_UNCORRECTABLE_CNT (DCC_CSRS + 0x000000000100)
#define DCC_ERR_DROPPED_PKT_CNT (DCC_CSRS + 0x000000000120)
#define DCC_ERR_RCVREMOTE_PHY_ERR_CNT (DCC_CSRS + 0x000000000118)
#define DCC_ERR_PORTRCV_ERR_CNT (DCC_CSRS + 0x000000000108)

#define DCC_PRF_PORT_RCV_FECN_CNT (DCC_CSRS + 0x000000000240)
#define DCC_PRF_PORT_VL_RCV_BUBBLE_CNT (DCC_CSRS + 0x0000000002E8)
#define DCC_PRF_PORT_VL_MARK_FECN_CNT (DCC_CSRS + 0x000000000338)
#define DCC_PRF_PORT_RCV_CORRECTABLE_CNT (DCC_CSRS + 0x000000000140)
#define DCC_PRF_PORT_RCV_PKTS_CNT (DCC_CSRS + 0x0000000001A8)
#define DCC_PRF_TX_FLOW_CRTL_CNT (DCC_CSRS + 0x000000000188)
#define DCC_PRF_PORT_XMIT_MULTICAST_CNT (DCC_CSRS + 0x000000000128)
#define DCC_PRF_PORT_VL_RCV_DATA_CNT (DCC_CSRS + 0x0000000001B0)
#define DCC_PRF_PORT_VL_RCV_BECN_CNT (DCC_CSRS + 0x000000000298)
#define DCC_PRF_PORT_RCV_DATA_CNT (DCC_CSRS + 0x000000000198)
#define DCC_PRF_PORT_RCV_MULTICAST_PKT_CNT (DCC_CSRS + 0x000000000130)
#define DCC_PRF_PORT_XMIT_DATA_CNT (DCC_CSRS + 0x000000000190)
#define DCC_PRF_PORT_VL_RCV_FECN_CNT (DCC_CSRS + 0x000000000248)
#define DCC_PRF_PORT_VL_RCV_PKTS_CNT (DCC_CSRS + 0x0000000001F8)
#define DCC_PRF_RX_FLOW_CRTL_CNT (DCC_CSRS + 0x000000000180)
#define DCC_PRF_PORT_MARK_FECN_CNT (DCC_CSRS + 0x000000000330)
#define DCC_PRF_PORT_RCV_BUBBLE_CNT (DCC_CSRS + 0x0000000002E0)
#define DCC_PRF_PORT_XMIT_CORRECTABLE_CNT (DCC_CSRS + 0x000000000138)
#define DCC_PRF_PORT_RCV_BECN_CNT (DCC_CSRS + 0x000000000290)
#define DCC_PRF_PORT_XMIT_PKTS_CNT (DCC_CSRS + 0x0000000001A0)

#define DC_LCB_CFG_RUN (DC_LCB_CSRS + 0x000000000000)
#define DC_LCB_CFG_RUN_EN_SHIFT 0

#define DC_LCB_CFG_TX_FIFOS_RESET (DC_LCB_CSRS + 0x000000000008)
#define DC_LCB_CFG_TX_FIFOS_RESET_VAL_SHIFT 0

#define DC_LCB_CFG_IGNORE_LOST_RCLK (DC_LCB_CSRS + 0x000000000020)
#define DC_LCB_CFG_IGNORE_LOST_RCLK_EN_SMASK 0x1ull

#define DC_LCB_CFG_REINIT_AS_SLAVE (DC_LCB_CSRS + 0x000000000030)

#define DC_LCB_CFG_CNT_FOR_SKIP_STALL (DC_LCB_CSRS + 0x000000000040)

#define DC_LCB_CFG_LANE_WIDTH (DC_LCB_CSRS + 0x000000000100)

#define DC_LCB_CFG_CLK_CNTR (DC_LCB_CSRS + 0x000000000110)

#define DC_LCB_CFG_ALLOW_LINK_UP (DC_LCB_CSRS + 0x000000000128)
#define DC_LCB_CFG_ALLOW_LINK_UP_VAL_SHIFT 0

#define DC_LCB_CFG_LOOPBACK (DC_LCB_CSRS + 0x0000000000F8)
#define DC_LCB_CFG_LOOPBACK_VAL_SHIFT 0

#define DC_LCB_ERR_CLR (DC_LCB_CSRS + 0x000000000308)
#define DC_LCB_ERR_EN (DC_LCB_CSRS + 0x000000000310)

#define DC_LCB_ERR_INFO_CRC_ERR_LN0 (DC_LCB_CSRS + 0x000000000328)
#define DC_LCB_ERR_INFO_CRC_ERR_LN1 (DC_LCB_CSRS + 0x000000000330)
#define DC_LCB_ERR_INFO_CRC_ERR_LN2 (DC_LCB_CSRS + 0x000000000338)
#define DC_LCB_ERR_INFO_CRC_ERR_LN3 (DC_LCB_CSRS + 0x000000000340)
#define DC_LCB_ERR_INFO_CRC_ERR_MULTI_LN (DC_LCB_CSRS + 0x000000000348)
#define DC_LCB_ERR_INFO_TOTAL_CRC_ERR (DC_LCB_CSRS + 0x000000000320)
#define DC_LCB_ERR_INFO_SEQ_CRC_CNT (DC_LCB_CSRS + 0x000000000360)
#define DC_LCB_ERR_INFO_RX_REPLAY_CNT (DC_LCB_CSRS + 0x000000000358)
#define DC_LCB_ERR_INFO_TX_REPLAY_CNT (DC_LCB_CSRS + 0x000000000350)
#define DC_LCB_ERR_INFO_ESCAPE_0_ONLY_CNT (DC_LCB_CSRS + 0x000000000368)
#define DC_LCB_ERR_INFO_ESCAPE_0_PLUS1_CNT (DC_LCB_CSRS + 0x000000000370)
#define DC_LCB_ERR_INFO_ESCAPE_0_PLUS2_CNT (DC_LCB_CSRS + 0x000000000378)
#define DC_LCB_ERR_INFO_REINIT_FROM_PEER_CNT (DC_LCB_CSRS + 0x000000000380)
#define DC_LCB_ERR_INFO_SBE_CNT (DC_LCB_CSRS + 0x000000000388)
#define DC_LCB_ERR_INFO_MISC_FLG_CNT (DC_LCB_CSRS + 0x000000000390)

#define DC_LCB_PRF_GOOD_LTP_CNT (DC_LCB_CSRS + 0x000000000400)
#define DC_LCB_PRF_ACCEPTED_LTP_CNT (DC_LCB_CSRS + 0x000000000408)
#define DC_LCB_PRF_RX_FLIT_CNT (DC_LCB_CSRS + 0x000000000410)
#define DC_LCB_PRF_TX_FLIT_CNT (DC_LCB_CSRS + 0x000000000418)
#define DC_LCB_PRF_CLK_CNTR (DC_LCB_CSRS + 0x000000000420)

#define DC_LCB_STS_LINK_TRANSFER_ACTIVE (DC_LCB_CSRS + 0x000000000468)

#define DC_LCB_PG_STS_TX_SBE_CNT (DC_LCB_CSRS + 0x000000000600)
#define DC_LCB_PG_STS_TX_MBE_CNT (DC_LCB_CSRS + 0x000000000608)
#define DC_LCB_PG_STS_PAUSE_COMPLETE_CNT (DC_LCB_CSRS + 0x0000000005F8)
#define DC_LCB_PG_DBG_FLIT_CRDTS_CNT (DC_LCB_CSRS + 0x000000000580)

#define DC_DC8051_CFG_RAM_ACCESS_SETUP (DC_8051_CSRS + 0x000000000000)
#define DC_DC8051_CFG_RAM_ACCESS_SETUP_AUTO_INCR_ADDR_SMASK 0x100ull
#define DC_DC8051_CFG_RAM_ACCESS_SETUP_RAM_SEL_SMASK 0x1ull

#define DC_DC8051_CFG_RAM_ACCESS_CTRL (DC_8051_CSRS + 0x000000000008)
#define DC_DC8051_CFG_RAM_ACCESS_CTRL_ADDRESS_SHIFT 0
#define DC_DC8051_CFG_RAM_ACCESS_CTRL_ADDRESS_MASK 0x7FFFull
#define DC_DC8051_CFG_RAM_ACCESS_CTRL_WRITE_ENA_SMASK 0x1000000ull

#define DC_DC8051_CFG_RAM_ACCESS_WR_DATA (DC_8051_CSRS + 0x000000000010)

#define DC_DC8051_CFG_RAM_ACCESS_STATUS (DC_8051_CSRS + 0x000000000018)
#define DC_DC8051_CFG_RAM_ACCESS_STATUS_ACCESS_COMPLETED_SMASK 0x10000ull

#define DC_DC8051_CFG_HOST_CMD_0 (DC_8051_CSRS + 0x000000000028)
#define DC_DC8051_CFG_HOST_CMD_0_REQ_NEW_SMASK 0x1ull
#define DC_DC8051_CFG_HOST_CMD_0_REQ_TYPE_SHIFT 8
#define DC_DC8051_CFG_HOST_CMD_0_REQ_TYPE_MASK 0xFFull
#define DC_DC8051_CFG_HOST_CMD_0_REQ_DATA_SHIFT 16
#define DC_DC8051_CFG_HOST_CMD_0_REQ_DATA_MASK 0xFFFFFFFFFFFFull

#define DC_DC8051_CFG_HOST_CMD_1 (DC_8051_CSRS + 0x000000000030)
#define DC_DC8051_CFG_HOST_CMD_1_COMPLETED_SMASK 0x1ull
#define DC_DC8051_CFG_HOST_CMD_1_RETURN_CODE_SHIFT 8
#define DC_DC8051_CFG_HOST_CMD_1_RETURN_CODE_MASK 0xFFull
#define DC_DC8051_CFG_HOST_CMD_1_RSP_DATA_SHIFT 16
#define DC_DC8051_CFG_HOST_CMD_1_RSP_DATA_MASK 0xFFFFFFFFFFFFull

#define DC_DC8051_CFG_LOCAL_GUID (DC_8051_CSRS + 0x000000000038)

#define DC_DC8051_STS_REMOTE_GUID (DC_8051_CSRS + 0x000000000040)

#define DC_DC8051_STS_REMOTE_NODE_TYPE (DC_8051_CSRS + 0x000000000048)
#define DC_DC8051_STS_REMOTE_NODE_TYPE_VAL_MASK 0x3ull

#define DC_DC8051_STS_REMOTE_FM_SECURITY (DC_8051_CSRS + 0x000000000058)
#define DC_DC8051_STS_LOCAL_FM_SECURITY_DISABLED_MASK 0x1ull

#define DC_DC8051_STS_CUR_STATE (DC_8051_CSRS + 0x000000000060)
#define DC_DC8051_STS_CUR_STATE_PORT_SHIFT 0
#define DC_DC8051_STS_CUR_STATE_PORT_MASK 0xFFull
#define DC_DC8051_STS_CUR_STATE_FIRMWARE_SHIFT 16
#define DC_DC8051_STS_CUR_STATE_FIRMWARE_MASK 0xFFull

#define DC_DC8051_CFG_RST (DC_8051_CSRS + 0x000000000068)
#define DC_DC8051_CFG_RST_M8051W_SMASK 0x1ull
#define DC_DC8051_CFG_RST_CRAM_SMASK 0x2ull
#define DC_DC8051_CFG_RST_DRAM_SMASK 0x4ull
#define DC_DC8051_CFG_RST_IRAM_SMASK 0x8ull
#define DC_DC8051_CFG_RST_SFR_SMASK 0x10ull

#define DC_DC8051_CFG_MODE (DC_8051_CSRS + 0x000000000070)

#define DC_DC8051_ERR_CLR (DC_8051_CSRS + 0x0000000000E8)

#define DC_DC8051_ERR_EN (DC_8051_CSRS + 0x0000000000F0)

#define DC_DC8051_CFG_CSR_ACCESS_SEL (DC_8051_CSRS + 0x000000000110)
#define DC_DC8051_CFG_CSR_ACCESS_SEL_DCC_SMASK 0x2ull
#define DC_DC8051_CFG_CSR_ACCESS_SEL_LCB_SMASK 0x1ull

#define DC_DC8051_CFG_EXT_DEV_0 (DC_8051_CSRS + 0x000000000118)
#define DC_DC8051_CFG_EXT_DEV_0_COMPLETED_SMASK 0x1ull
#define DC_DC8051_CFG_EXT_DEV_0_RETURN_CODE_SHIFT 8
#define DC_DC8051_CFG_EXT_DEV_0_RSP_DATA_SHIFT 16

#define DC_DC8051_CFG_EXT_DEV_1 (DC_8051_CSRS + 0x000000000120)
#define DC_DC8051_CFG_EXT_DEV_1_REQ_DATA_SHIFT 16
#define DC_DC8051_CFG_EXT_DEV_1_REQ_DATA_SMASK 0xFFFF0000ull

#define DC_DC8051_STS_REMOTE_PORT_NO (DC_8051_CSRS + 0x000000000130)
#define DC_DC8051_STS_REMOTE_PORT_NO_VAL_SMASK 0xFFull

#define MISC_CFG_FW_CTRL (MISC + 0x000000001000)
#define MISC_CFG_FW_CTRL_FW_8051_LOADED_SMASK 0x2ull
#define MISC_CFG_FW_CTRL_RSA_STATUS_SHIFT 2
#define MISC_CFG_FW_CTRL_RSA_STATUS_SMASK 0xCull

#define MISC_CFG_RSA_R2 (MISC + 0x000000000000)
#define MISC_CFG_RSA_SIGNATURE (MISC + 0x000000000200)
#define MISC_CFG_RSA_MODULUS (MISC + 0x000000000400)
#define MISC_CFG_SHA_PRELOAD (MISC + 0x000000000A00)
#define MISC_CFG_RSA_MU (MISC + 0x000000000A10)
#define MISC_CFG_RSA_CMD (MISC + 0x000000000A08)

#define MISC_ERR_STATUS (MISC + 0x000000002000)
#define MISC_ERR_STATUS_MISC_FW_AUTH_FAILED_ERR_SMASK 0x20ull
#define MISC_ERR_STATUS_MISC_KEY_MISMATCH_ERR_SMASK 0x10ull

#define MISC_ERR_MASK (MISC + 0x000000002008)
#define MISC_ERR_CLEAR (MISC + 0x000000002010)

#define ASIC_CFG_SBUS_REQUEST (ASIC + 0x000000000000)
#define ASIC_CFG_SBUS_REQUEST_DATA_IN_SHIFT 32
#define ASIC_CFG_SBUS_REQUEST_COMMAND_SHIFT 16
#define ASIC_CFG_SBUS_REQUEST_RECEIVER_ADDR_SHIFT 0
#define ASIC_CFG_SBUS_REQUEST_DATA_ADDR_SHIFT 8

#define ASIC_CFG_SBUS_EXECUTE (ASIC + 0x000000000008)
#define ASIC_CFG_SBUS_EXECUTE_EXECUTE_SMASK 0x1ull
#define ASIC_CFG_SBUS_EXECUTE_FAST_MODE_SMASK 0x2ull

#define ASIC_STS_SBUS_RESULT (ASIC + 0x000000000010)
#define ASIC_STS_SBUS_RESULT_DONE_SMASK 0x1ull
#define ASIC_STS_SBUS_RESULT_RCV_DATA_VALID_SMASK 0x2ull
#define ASIC_STS_SBUS_RESULT_RESULT_CODE_SHIFT 2
#define ASIC_STS_SBUS_RESULT_RESULT_CODE_MASK 0x7ull
#define ASIC_STS_SBUS_RESULT_DATA_OUT_SHIFT 32
#define ASIC_STS_SBUS_RESULT_DATA_OUT_MASK 0xFFFFFFFFull

#define ASIC_STS_SBUS_COUNTERS (ASIC + 0x000000000018)

#define ASIC_CFG_SCRATCH (ASIC + 0x000000000020)
#define ASIC_CFG_SCRATCH_1 (ASIC_CFG_SCRATCH + 0x08)
#define ASIC_CFG_SCRATCH_2 (ASIC_CFG_SCRATCH + 0x10)
#define ASIC_CFG_SCRATCH_3 (ASIC_CFG_SCRATCH + 0x18)

#define ASIC_CFG_MUTEX (ASIC + 0x000000000040)

#define ASIC_CFG_THERM_POLL_EN (ASIC + 0x000000000050)

#define ASIC_PCIE_SD_HOST_CMD (ASIC + 0x000000000100)
#define ASIC_PCIE_SD_HOST_CMD_SBR_MODE_SMASK 0x400ull
#define ASIC_PCIE_SD_HOST_CMD_TIMER_MASK 0xFFFFFull
#define ASIC_PCIE_SD_HOST_CMD_TIMER_SHIFT 12
#define ASIC_PCIE_SD_HOST_CMD_SBUS_RCVR_ADDR_SHIFT 2
#define ASIC_PCIE_SD_HOST_CMD_INTRPT_CMD_SHIFT 0

#define ASIC_PCIE_SD_HOST_STATUS (ASIC + 0x000000000108)
#define ASIC_PCIE_SD_HOST_STATUS_FW_DNLD_STS_SHIFT 0
#define ASIC_PCIE_SD_HOST_STATUS_FW_DNLD_STS_MASK 0x3ull
#define ASIC_PCIE_SD_HOST_STATUS_FW_DNLD_ERR_SHIFT 2
#define ASIC_PCIE_SD_HOST_STATUS_FW_DNLD_ERR_MASK 0x7ull

#define ASIC_PCIE_SD_INTRPT_LIST (ASIC + 0x000000000180)
#define ASIC_PCIE_SD_INTRPT_LIST_INTRPT_DATA_SHIFT 0
#define ASIC_PCIE_SD_INTRPT_LIST_INTRPT_CODE_SHIFT 16

#define ASIC_QSFP1_IN (ASIC + 0x000000000240)
#define ASIC_QSFP2_IN (ASIC + 0x000000000280)

#define ASIC_QSFP1_OE (ASIC + 0x000000000248)
#define ASIC_QSFP2_OE (ASIC + 0x000000000288)

#define ASIC_QSFP1_OUT (ASIC + 0x000000000258)
#define ASIC_QSFP2_OUT (ASIC + 0x000000000298)

#define ASIC_QSFP1_INVERT (ASIC + 0x000000000250)
#define ASIC_QSFP2_INVERT (ASIC + 0x000000000290)

#define ASIC_QSFP1_MASK (ASIC + 0x000000000260)
#define ASIC_QSFP2_MASK (ASIC + 0x0000000002A0)

#define ASIC_QSFP1_CLEAR (ASIC + 0x000000000270)
#define ASIC_QSFP2_CLEAR (ASIC + 0x0000000002B0)

#define ASIC_EEP_CTL_STAT (ASIC + 0x000000000300)
#define ASIC_EEP_CTL_STAT_EP_RESET_SMASK 0x4ull
#define ASIC_EEP_CTL_STAT_RATE_SPI_SHIFT 8

#define ASIC_EEP_ADDR_CMD (ASIC + 0x000000000308)

#define ASIC_EEP_DATA (ASIC + 0x000000000310)

#define PCIE_CFG_TPH2 (PCIE + 0x000000000180)
#define PCI_CFG_MSIX0 (PCIE + 0x0000000000B0)

#define PCIE_CFG_SPCIE2 (PCIE + 0x000000000150)

#define PCIE_CFG_REG_PL2 (PCIE + 0x000000000708)
#define PCIE_CFG_REG_PL2_LOW_PWR_ENT_CNT_SHIFT 24

#define PCIE_CFG_REG_PL3 (PCIE + 0x00000000070C)
#define PCIE_CFG_REG_PL3_L1_ENT_LATENCY_SHIFT 27
#define PCIE_CFG_REG_PL3_L1_ENT_LATENCY_SMASK 0x38000000

#define PCIE_CFG_REG_PL100 (PCIE + 0x000000000890)
#define PCIE_CFG_REG_PL100_EQ_EIEOS_CNT_SMASK 0x400ull

#define PCIE_CFG_REG_PL101 (PCIE + 0x000000000894)
#define PCIE_CFG_REG_PL101_GEN3_EQ_LOCAL_LF_SHIFT 0
#define PCIE_CFG_REG_PL101_GEN3_EQ_LOCAL_FS_SHIFT 6

#define PCIE_CFG_REG_PL102 (PCIE + 0x000000000898)
#define PCIE_CFG_REG_PL102_GEN3_EQ_POST_CURSOR_PSET_SHIFT 12
#define PCIE_CFG_REG_PL102_GEN3_EQ_PRE_CURSOR_PSET_SHIFT 0
#define PCIE_CFG_REG_PL102_GEN3_EQ_CURSOR_PSET_SHIFT 6

#define PCIE_CFG_REG_PL103 (PCIE + 0x00000000089C)

#define PCIE_CFG_REG_PL105 (PCIE + 0x0000000008A4)
#define PCIE_CFG_REG_PL105_GEN3_EQ_VIOLATE_COEF_RULES_SMASK 0x1ull

#define PCIE_CFG_REG_PL106 (PCIE + 0x0000000008A8)
#define PCIE_CFG_REG_PL106_GEN3_EQ_PHASE23_EXIT_MODE_SMASK 0x10ull
#define PCIE_CFG_REG_PL106_GEN3_EQ_EVAL2MS_DISABLE_SMASK 0x20ull
#define PCIE_CFG_REG_PL106_GEN3_EQ_PSET_REQ_VEC_SHIFT 8

/* Other driver constants */
#define HFI1_CHIP_VERS_MAJ 3U
#define HFI1_CHIP_VERS_MIN 0U
#define NUM_IB_PORTS 1
#define TXE_NUM_SDMA_ENGINES 16
#define RXE_NUM_DATA_VL 8
#define PER_VL_SEND_CONTEXTS 16
#define SC_MAX 4
#define HFI1_HAS_SEND_DMA 0x10
#define HFI1_HAS_SDMA_TIMEOUT 0x8
#define HFI1_CAP_LOCKED_SHIFT 63
#define HFI1_CAP_LOCKED_MASK 0x1ULL
#define HFI1_CAP_USER_SHIFT 24
#define HFI1_CAP_LOCKED_SMASK (HFI1_CAP_LOCKED_MASK << HFI1_CAP_LOCKED_SHIFT)
#define RCV_SHIFT 3
#define RCV_INCREMENT BIT(RCV_SHIFT)

#define BIT(nr) (1UL << (nr))

/* Template placeholders filled with structural definitions */

#define VENDOR_ID 0x8086
#define DEVICE_ID 0x24f0
#define CLASS_ID 0x0c06

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
    MemoryRegion msix_table;
    MemoryRegion msix_pba;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint64_t cce_revision;
    uint64_t cce_revision2;
    uint64_t cce_dc_ctrl;
    uint64_t cce_ctrl;
    uint64_t cce_status;
    uint64_t cce_int_status[CCE_NUM_INT_CSRS];
    uint64_t cce_int_mask[CCE_NUM_INT_CSRS];
    uint64_t cce_int_clear;
    uint64_t cce_int_force;
    uint64_t cce_int_blocked;
    uint64_t cce_int_map[CCE_NUM_INT_MAP_CSRS];
    uint64_t cce_err_mask;
    uint64_t cce_err_clear;
    uint64_t cce_scratch[CCE_NUM_SCRATCH];
    uint64_t cce_pcie_ctrl;
    uint64_t rcv_ctrl;
    uint64_t rcv_status;
    uint64_t rcv_contexts;
    uint64_t rcv_array_cnt;
    uint64_t rcv_bth_qp;
    uint64_t rcv_multicast;
    uint64_t rcv_bypass;
    uint64_t rcv_vl15;
    uint64_t rcv_err_info;
    uint64_t rcv_err_mask;
    uint64_t rcv_err_clear;
    uint64_t rcv_qp_map_table;
    uint64_t rcv_partition_key;
    uint64_t rcv_rsm_cfg;
    uint64_t rcv_rsm_match;
    uint64_t rcv_rsm_select;
    uint64_t rcv_rsm_map_table;
    uint64_t rcv_ctxt_ctrl;
    uint64_t rcv_ctxt_status;
    uint64_t rcv_egr_ctrl;
    uint64_t rcv_tid_ctrl;
    uint64_t rcv_key_ctrl;
    uint64_t rcv_hdr_addr;
    uint64_t rcv_hdr_cnt;
    uint64_t rcv_hdr_ent_size;
    uint64_t rcv_hdr_size;
    uint64_t rcv_hdr_tail_addr;
    uint64_t rcv_avail_time_out;
    uint64_t rcv_hdr_ovfl_cnt;
    uint64_t rcv_hdr_head;
    uint64_t rcv_egr_index_head;
    uint64_t rcv_tid_flow_table;
    uint64_t send_ctrl;
    uint64_t send_contexts;
    uint64_t send_dma_engines;
    uint64_t send_pio_mem_size;
    uint64_t send_high_priority_limit;
    uint64_t send_pio_init_ctxt;
    uint64_t send_pio_err_mask;
    uint64_t send_pio_err_clear;
    uint64_t send_dma_err_mask;
    uint64_t send_dma_err_clear;
    uint64_t send_egress_err_mask;
    uint64_t send_egress_err_clear;
    uint64_t send_bth_qp;
    uint64_t send_static_rate_control;
    uint64_t send_sc2vlt0;
    uint64_t send_sc2vlt1;
    uint64_t send_sc2vlt2;
    uint64_t send_sc2vlt3;
    uint64_t send_len_check0;
    uint64_t send_len_check1;
    uint64_t send_err_mask;
    uint64_t send_err_clear;
    uint64_t send_low_priority_list;
    uint64_t send_high_priority_list;
    uint64_t send_context_set_ctrl;
    uint64_t send_cm_ctrl;
    uint64_t send_cm_global_credit;
    uint64_t send_cm_timer_ctrl;
    uint64_t send_cm_local_au_table0_to3;
    uint64_t send_cm_local_au_table4_to7;
    uint64_t send_cm_remote_au_table0_to3;
    uint64_t send_cm_remote_au_table4_to7;
    uint64_t send_cm_credit_vl;
    uint64_t send_cm_credit_vl15;
    uint64_t send_egress_ctxt_status;
    uint64_t send_egress_err_info;
    uint64_t send_egress_send_dma_status;
    uint64_t send_ctxt_ctrl;
    uint64_t send_ctxt_credit_ctrl;
    uint64_t send_ctxt_credit_return_addr;
    uint64_t send_ctxt_credit_force;
    uint64_t send_ctxt_err_mask;
    uint64_t send_ctxt_err_clear;
    uint64_t send_ctxt_check_enable;
    uint64_t send_ctxt_check_vl;
    uint64_t send_ctxt_check_slid;
    uint64_t send_ctxt_check_opcode;
    uint64_t send_ctxt_check_job_key;
    uint64_t send_ctxt_check_partition_key;
    uint64_t send_dma_ctrl;
    uint64_t send_dma_base_addr;
    uint64_t send_dma_len_gen;
    uint64_t send_dma_tail;
    uint64_t send_dma_head_addr;
    uint64_t send_dma_priority_thld;
    uint64_t send_dma_reload_cnt;
    uint64_t send_dma_desc_cnt;
    uint64_t send_dma_desc_fetched_cnt;
    uint64_t send_dma_eng_err_mask;
    uint64_t send_dma_eng_err_clear;
    uint64_t send_dma_check_enable;
    uint64_t send_dma_check_vl;
    uint64_t send_dma_check_slid;
    uint64_t send_dma_check_opcode;
    uint64_t send_dma_check_job_key;
    uint64_t send_dma_memory;
    uint64_t dcc_cfg_reset;
    uint64_t dcc_cfg_port_config;
    uint64_t dcc_cfg_port_config1;
    uint64_t dcc_cfg_sc_vl_table_15_0;
    uint64_t dcc_cfg_sc_vl_table_31_16;
    uint64_t dcc_cfg_led_cntrl;
    uint64_t dcc_err_flg_en;
    uint64_t dcc_err_flg_clr;
    uint64_t dcc_err_fmconfig_err_cnt;
    uint64_t dcc_err_uncorrectable_cnt;
    uint64_t dcc_err_dropped_pkt_cnt;
    uint64_t dcc_err_rcvremote_phy_err_cnt;
    uint64_t dcc_err_portrcv_err_cnt;
    uint64_t dcc_prf_port_rcv_fecn_cnt;
    uint64_t dcc_prf_port_vl_rcv_bubble_cnt;
    uint64_t dcc_prf_port_vl_mark_fecn_cnt;
    uint64_t dcc_prf_port_rcv_correctable_cnt;
    uint64_t dcc_prf_port_rcv_pkts_cnt;
    uint64_t dcc_prf_tx_flow_crtl_cnt;
    uint64_t dcc_prf_port_xmit_multicast_cnt;
    uint64_t dcc_prf_port_vl_rcv_data_cnt;
    uint64_t dcc_prf_port_vl_rcv_becn_cnt;
    uint64_t dcc_prf_port_rcv_data_cnt;
    uint64_t dcc_prf_port_rcv_multicast_pkt_cnt;
    uint64_t dcc_prf_port_xmit_data_cnt;
    uint64_t dcc_prf_port_vl_rcv_fecn_cnt;
    uint64_t dcc_prf_port_vl_rcv_pkts_cnt;
    uint64_t dcc_prf_rx_flow_crtl_cnt;
    uint64_t dcc_prf_port_mark_fecn_cnt;
    uint64_t dcc_prf_port_rcv_bubble_cnt;
    uint64_t dcc_prf_port_xmit_correctable_cnt;
    uint64_t dcc_prf_port_rcv_becn_cnt;
    uint64_t dcc_prf_port_xmit_pkts_cnt;
    uint64_t dc_lcb_cfg_run;
    uint64_t dc_lcb_cfg_tx_fifos_reset;
    uint64_t dc_lcb_cfg_ignore_lost_rclk;
    uint64_t dc_lcb_cfg_reinit_as_slave;
    uint64_t dc_lcb_cfg_cnt_for_skip_stall;
    uint64_t dc_lcb_cfg_lane_width;
    uint64_t dc_lcb_cfg_clk_cntr;
    uint64_t dc_lcb_cfg_allow_link_up;
    uint64_t dc_lcb_cfg_loopback;
    uint64_t dc_lcb_err_clr;
    uint64_t dc_lcb_err_en;
    uint64_t dc_lcb_err_info_crc_err_ln0;
    uint64_t dc_lcb_err_info_crc_err_ln1;
    uint64_t dc_lcb_err_info_crc_err_ln2;
    uint64_t dc_lcb_err_info_crc_err_ln3;
    uint64_t dc_lcb_err_info_crc_err_multi_ln;
    uint64_t dc_lcb_err_info_total_crc_err;
    uint64_t dc_lcb_err_info_seq_crc_cnt;
    uint64_t dc_lcb_err_info_rx_replay_cnt;
    uint64_t dc_lcb_err_info_tx_replay_cnt;
    uint64_t dc_lcb_err_info_escape_0_only_cnt;
    uint64_t dc_lcb_err_info_escape_0_plus1_cnt;
    uint64_t dc_lcb_err_info_escape_0_plus2_cnt;
    uint64_t dc_lcb_err_info_reinit_from_peer_cnt;
    uint64_t dc_lcb_err_info_sbe_cnt;
    uint64_t dc_lcb_err_info_misc_flg_cnt;
    uint64_t dc_lcb_prf_good_ltp_cnt;
    uint64_t dc_lcb_prf_accepted_ltp_cnt;
    uint64_t dc_lcb_prf_rx_flit_cnt;
    uint64_t dc_lcb_prf_tx_flit_cnt;
    uint64_t dc_lcb_prf_clk_cntr;
    uint64_t dc_lcb_sts_link_transfer_active;
    uint64_t dc_lcb_pg_sts_tx_sbe_cnt;
    uint64_t dc_lcb_pg_sts_tx_mbe_cnt;
    uint64_t dc_lcb_pg_sts_pause_complete_cnt;
    uint64_t dc_lcb_pg_dbg_flit_crdts_cnt;
    uint64_t dc_dc8051_cfg_ram_access_setup;
    uint64_t dc_dc8051_cfg_ram_access_ctrl;
    uint64_t dc_dc8051_cfg_ram_access_wr_data;
    uint64_t dc_dc8051_cfg_ram_access_status;
    uint64_t dc_dc8051_cfg_host_cmd_0;
    uint64_t dc_dc8051_cfg_host_cmd_1;
    uint64_t dc_dc8051_cfg_local_guid;
    uint64_t dc_dc8051_sts_remote_guid;
    uint64_t dc_dc8051_sts_remote_node_type;
    uint64_t dc_dc8051_sts_remote_fm_security;
    uint64_t dc_dc8051_sts_cur_state;
    uint64_t dc_dc8051_cfg_rst;
    uint64_t dc_dc8051_cfg_mode;
    uint64_t dc_dc8051_err_clr;
    uint64_t dc_dc8051_err_en;
    uint64_t dc_dc8051_cfg_csr_access_sel;
    uint64_t dc_dc8051_cfg_ext_dev_0;
    uint64_t dc_dc8051_cfg_ext_dev_1;
    uint64_t dc_dc8051_sts_remote_port_no;
    uint64_t misc_cfg_fw_ctrl;
    uint64_t misc_cfg_rsa_r2;
    uint64_t misc_cfg_rsa_signature;
    uint64_t misc_cfg_rsa_modulus;
    uint64_t misc_cfg_sha_preload;
    uint64_t misc_cfg_rsa_mu;
    uint64_t misc_cfg_rsa_cmd;
    uint64_t misc_err_status;
    uint64_t misc_err_mask;
    uint64_t misc_err_clear;
    uint64_t asic_cfg_sbus_request;
    uint64_t asic_cfg_sbus_execute;
    uint64_t asic_sts_sbus_result;
    uint64_t asic_sts_sbus_counters;
    uint64_t asic_cfg_scratch[4];
    uint64_t asic_cfg_mutex;
    uint64_t asic_cfg_therm_poll_en;
    uint64_t asic_pcie_sd_host_cmd;
    uint64_t asic_pcie_sd_host_status;
    uint64_t asic_pcie_sd_intrpt_list;
    uint64_t asic_qsfp1_in;
    uint64_t asic_qsfp2_in;
    uint64_t asic_qsfp1_oe;
    uint64_t asic_qsfp2_oe;
    uint64_t asic_qsfp1_out;
    uint64_t asic_qsfp2_out;
    uint64_t asic_qsfp1_invert;
    uint64_t asic_qsfp2_invert;
    uint64_t asic_qsfp1_mask;
    uint64_t asic_qsfp2_mask;
    uint64_t asic_qsfp1_clear;
    uint64_t asic_qsfp2_clear;
    uint64_t asic_eep_ctl_stat;
    uint64_t asic_eep_addr_cmd;
    uint64_t asic_eep_data;
    uint64_t pcie_cfg_tph2;
    uint64_t pci_cfg_msix0;
    uint64_t pcie_cfg_spcie2;
    uint64_t pcie_cfg_reg_pl2;
    uint64_t pcie_cfg_reg_pl3;
    uint64_t pcie_cfg_reg_pl100;
    uint64_t pcie_cfg_reg_pl101;
    uint64_t pcie_cfg_reg_pl102;
    uint64_t pcie_cfg_reg_pl103;
    uint64_t pcie_cfg_reg_pl105;
    uint64_t pcie_cfg_reg_pl106;

    /* DMA Context */
    /* Pointers for DMA base addresses, count, and status */

    /* Operational status flags */

    /* State used to handle reset sequences */

    /* Power management state (D0-D3) */

    /* Other addition info */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t enabled_ints = 0;
    int i;

    for (i = 0; i < CCE_NUM_INT_CSRS; i++) {
        uint64_t status = s->cce_int_status[i];
        uint64_t mask = s->cce_int_mask[i];
        enabled_ints |= (status & mask);
    }

    if (enabled_ints && msix_enabled(pdev)) {
        msix_notify(pdev, 0);
    } else if (enabled_ints) {
        pci_set_irq(pdev, 1);
    } else {
        if (!msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* DMA transfer logic - not required for probe, left empty */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CCE_REVISION:
        val = s->cce_revision;
        break;
    case CCE_REVISION2:
        val = s->cce_revision2;
        break;
    case CCE_DC_CTRL:
        val = s->cce_dc_ctrl;
        break;
    case CCE_CTRL:
        val = s->cce_ctrl;
        break;
    case CCE_STATUS:
        val = s->cce_status;
        break;
    case CCE_ERR_MASK:
        val = s->cce_err_mask;
        break;
    case CCE_ERR_CLEAR:
        val = s->cce_err_clear;
        break;
    case CCE_SCRATCH:
    case CCE_SCRATCH + 0x08:
    case CCE_SCRATCH + 0x10:
    case CCE_SCRATCH + 0x18:
        {
            int index = (addr - CCE_SCRATCH) / 0x08;
            if (index < CCE_NUM_SCRATCH) {
                val = s->cce_scratch[index];
            }
        }
        break;
    case CCE_PCIE_CTRL:
        val = s->cce_pcie_ctrl;
        break;
    case CCE_INT_STATUS:
        val = s->cce_int_status[0];
        break;
    case CCE_INT_MASK:
        val = s->cce_int_mask[0];
        break;
    case CCE_INT_CLEAR:
        val = s->cce_int_clear;
        break;
    case CCE_INT_FORCE:
        val = s->cce_int_force;
        break;
    case CCE_INT_BLOCKED:
        val = s->cce_int_blocked;
        break;
    /* Handle array-based interrupt CSRs with stride */
    default:
        if (addr >= CCE_INT_STATUS && addr < CCE_INT_STATUS + 0x100 * CCE_NUM_INT_CSRS && (addr - CCE_INT_STATUS) % 0x100 == 0) {
            int idx = (addr - CCE_INT_STATUS) / 0x100;
            if (idx < CCE_NUM_INT_CSRS)
                val = s->cce_int_status[idx];
        } else if (addr >= CCE_INT_MASK && addr < CCE_INT_MASK + 0x100 * CCE_NUM_INT_CSRS && (addr - CCE_INT_MASK) % 0x100 == 0) {
            int idx = (addr - CCE_INT_MASK) / 0x100;
            if (idx < CCE_NUM_INT_CSRS)
                val = s->cce_int_mask[idx];
        } else if (addr >= CCE_INT_MAP && addr < CCE_INT_MAP + 0x100 * CCE_NUM_INT_MAP_CSRS && (addr - CCE_INT_MAP) % 0x100 == 0) {
            int idx = (addr - CCE_INT_MAP) / 0x100;
            if (idx < CCE_NUM_INT_MAP_CSRS)
                val = s->cce_int_map[idx];
        } else if (addr >= RCV_CTRL && addr <= RCV_VL15) {
            switch (addr) {
            case RCV_CTRL: val = s->rcv_ctrl; break;
            case RCV_STATUS: val = s->rcv_status; break;
            case RCV_CONTEXTS: val = s->rcv_contexts; break;
            case RCV_ARRAY_CNT: val = s->rcv_array_cnt; break;
            case RCV_BTH_QP: val = s->rcv_bth_qp; break;
            case RCV_MULTICAST: val = s->rcv_multicast; break;
            case RCV_BYPASS: val = s->rcv_bypass; break;
            case RCV_VL15: val = s->rcv_vl15; break;
            default: val = 0;
            }
        } else if (addr >= RCV_ERR_INFO && addr <= RCV_ERR_CLEAR) {
            switch (addr) {
            case RCV_ERR_INFO: val = s->rcv_err_info; break;
            case RCV_ERR_MASK: val = s->rcv_err_mask; break;
            case RCV_ERR_CLEAR: val = s->rcv_err_clear; break;
            default: val = 0;
            }
        } else if (addr == RCV_QP_MAP_TABLE) {
            val = s->rcv_qp_map_table;
        } else if (addr == RCV_PARTITION_KEY) {
            val = s->rcv_partition_key;
        } else if (addr >= RCV_COUNTER_ARRAY32 && addr <= RCV_COUNTER_ARRAY64) {
            val = 0; /* Not implemented */
        } else if (addr >= RCV_RSM_CFG && addr <= RCV_RSM_MAP_TABLE) {
            switch (addr) {
            case RCV_RSM_CFG: val = s->rcv_rsm_cfg; break;
            case RCV_RSM_MATCH: val = s->rcv_rsm_match; break;
            case RCV_RSM_SELECT: val = s->rcv_rsm_select; break;
            case RCV_RSM_MAP_TABLE: val = s->rcv_rsm_map_table; break;
            default: val = 0;
            }
        } else if (addr >= RCV_CTXT_CTRL && addr <= RCV_HDR_OVFL_CNT) {
            switch (addr) {
            case RCV_CTXT_CTRL: val = s->rcv_ctxt_ctrl; break;
            case RCV_CTXT_STATUS: val = s->rcv_ctxt_status; break;
            case RCV_EGR_CTRL: val = s->rcv_egr_ctrl; break;
            case RCV_TID_CTRL: val = s->rcv_tid_ctrl; break;
            case RCV_KEY_CTRL: val = s->rcv_key_ctrl; break;
            case RCV_HDR_ADDR: val = s->rcv_hdr_addr; break;
            case RCV_HDR_CNT: val = s->rcv_hdr_cnt; break;
            case RCV_HDR_ENT_SIZE: val = s->rcv_hdr_ent_size; break;
            case RCV_HDR_SIZE: val = s->rcv_hdr_size; break;
            case RCV_HDR_TAIL_ADDR: val = s->rcv_hdr_tail_addr; break;
            case RCV_AVAIL_TIME_OUT: val = s->rcv_avail_time_out; break;
            case RCV_HDR_OVFL_CNT: val = s->rcv_hdr_ovfl_cnt; break;
            default: val = 0;
            }
        } else if (addr >= RCV_HDR_HEAD && addr <= RCV_EGR_INDEX_HEAD) {
            switch (addr) {
            case RCV_HDR_HEAD: val = s->rcv_hdr_head; break;
            case RCV_EGR_INDEX_HEAD: val = s->rcv_egr_index_head; break;
            default: val = 0;
            }
        } else if (addr == RCV_TID_FLOW_TABLE) {
            val = s->rcv_tid_flow_table;
        } else if (addr == RCV_ARRAY) {
            /* RCV_ARRAY may be multiple entries; we just return 0 for simplicity */
            val = 0;
        } else if (addr >= SEND_CTRL && addr <= SEND_HIGH_PRIORITY_LIMIT) {
            switch (addr) {
            case SEND_CTRL: val = s->send_ctrl; break;
            case SEND_CONTEXTS: val = s->send_contexts; break;
            case SEND_DMA_ENGINES: val = s->send_dma_engines; break;
            case SEND_PIO_MEM_SIZE: val = s->send_pio_mem_size; break;
            case SEND_HIGH_PRIORITY_LIMIT: val = s->send_high_priority_limit; break;
            default: val = 0;
            }
        } else if (addr >= SEND_PIO_INIT_CTXT && addr <= SEND_PIO_ERR_CLEAR) {
            switch (addr) {
            case SEND_PIO_INIT_CTXT: val = s->send_pio_init_ctxt; break;
            case SEND_PIO_ERR_MASK: val = s->send_pio_err_mask; break;
            case SEND_PIO_ERR_CLEAR: val = s->send_pio_err_clear; break;
            default: val = 0;
            }
        } else if (addr >= SEND_DMA_ERR_MASK && addr <= SEND_DMA_ERR_CLEAR) {
            switch (addr) {
            case SEND_DMA_ERR_MASK: val = s->send_dma_err_mask; break;
            case SEND_DMA_ERR_CLEAR: val = s->send_dma_err_clear; break;
            default: val = 0;
            }
        } else if (addr >= SEND_EGRESS_ERR_MASK && addr <= SEND_EGRESS_ERR_CLEAR) {
            switch (addr) {
            case SEND_EGRESS_ERR_MASK: val = s->send_egress_err_mask; break;
            case SEND_EGRESS_ERR_CLEAR: val = s->send_egress_err_clear; break;
            default: val = 0;
            }
        } else if (addr == SEND_BTH_QP) {
            val = s->send_bth_qp;
        } else if (addr == SEND_STATIC_RATE_CONTROL) {
            val = s->send_static_rate_control;
        } else if (addr >= SEND_SC2VLT0 && addr <= SEND_SC2VLT3) {
            switch (addr) {
            case SEND_SC2VLT0: val = s->send_sc2vlt0; break;
            case SEND_SC2VLT1: val = s->send_sc2vlt1; break;
            case SEND_SC2VLT2: val = s->send_sc2vlt2; break;
            case SEND_SC2VLT3: val = s->send_sc2vlt3; break;
            default: val = 0;
            }
        } else if (addr >= SEND_LEN_CHECK0 && addr <= SEND_LEN_CHECK1) {
            switch (addr) {
            case SEND_LEN_CHECK0: val = s->send_len_check0; break;
            case SEND_LEN_CHECK1: val = s->send_len_check1; break;
            default: val = 0;
            }
        } else if (addr >= SEND_ERR_MASK && addr <= SEND_ERR_CLEAR) {
            switch (addr) {
            case SEND_ERR_MASK: val = s->send_err_mask; break;
            case SEND_ERR_CLEAR: val = s->send_err_clear; break;
            default: val = 0;
            }
        } else if (addr >= SEND_LOW_PRIORITY_LIST && addr <= SEND_HIGH_PRIORITY_LIST) {
            switch (addr) {
            case SEND_LOW_PRIORITY_LIST: val = s->send_low_priority_list; break;
            case SEND_HIGH_PRIORITY_LIST: val = s->send_high_priority_list; break;
            default: val = 0;
            }
        } else if (addr == SEND_CONTEXT_SET_CTRL) {
            val = s->send_context_set_ctrl;
        } else if (addr >= SEND_COUNTER_ARRAY32 && addr <= SEND_COUNTER_ARRAY64) {
            val = 0;
        } else if (addr >= SEND_CM_CTRL && addr <= SEND_CM_REMOTE_AU_TABLE4_TO7) {
            switch (addr) {
            case SEND_CM_CTRL: val = s->send_cm_ctrl; break;
            case SEND_CM_GLOBAL_CREDIT: val = s->send_cm_global_credit; break;
            case SEND_CM_TIMER_CTRL: val = s->send_cm_timer_ctrl; break;
            case SEND_CM_LOCAL_AU_TABLE0_TO3: val = s->send_cm_local_au_table0_to3; break;
            case SEND_CM_LOCAL_AU_TABLE4_TO7: val = s->send_cm_local_au_table4_to7; break;
            case SEND_CM_REMOTE_AU_TABLE0_TO3: val = s->send_cm_remote_au_table0_to3; break;
            case SEND_CM_REMOTE_AU_TABLE4_TO7: val = s->send_cm_remote_au_table4_to7; break;
            default: val = 0;
            }
        } else if (addr >= SEND_CM_CREDIT_VL && addr <= SEND_CM_CREDIT_VL15) {
            switch (addr) {
            case SEND_CM_CREDIT_VL: val = s->send_cm_credit_vl; break;
            case SEND_CM_CREDIT_VL15: val = s->send_cm_credit_vl15; break;
            default: val = 0;
            }
        } else if (addr >= SEND_EGRESS_CTXT_STATUS && addr <= SEND_EGRESS_ERR_INFO) {
            switch (addr) {
            case SEND_EGRESS_CTXT_STATUS: val = s->send_egress_ctxt_status; break;
            case SEND_EGRESS_ERR_INFO: val = s->send_egress_err_info; break;
            default: val = 0;
            }
        } else if (addr == SEND_EGRESS_SEND_DMA_STATUS) {
            val = s->send_egress_send_dma_status;
        } else if (addr >= SEND_CTXT_CTRL && addr <= SEND_CTXT_ERR_CLEAR) {
            switch (addr) {
            case SEND_CTXT_CTRL: val = s->send_ctxt_ctrl; break;
            case SEND_CTXT_CREDIT_CTRL: val = s->send_ctxt_credit_ctrl; break;
            case SEND_CTXT_CREDIT_RETURN_ADDR: val = s->send_ctxt_credit_return_addr; break;
            case SEND_CTXT_CREDIT_FORCE: val = s->send_ctxt_credit_force; break;
            case SEND_CTXT_ERR_MASK: val = s->send_ctxt_err_mask; break;
            case SEND_CTXT_ERR_CLEAR: val = s->send_ctxt_err_clear; break;
            default: val = 0;
            }
        } else if (addr >= SEND_CTXT_CHECK_ENABLE && addr <= SEND_CTXT_CHECK_PARTITION_KEY) {
            switch (addr) {
            case SEND_CTXT_CHECK_ENABLE: val = s->send_ctxt_check_enable; break;
            case SEND_CTXT_CHECK_VL: val = s->send_ctxt_check_vl; break;
            case SEND_CTXT_CHECK_SLID: val = s->send_ctxt_check_slid; break;
            case SEND_CTXT_CHECK_OPCODE: val = s->send_ctxt_check_opcode; break;
            case SEND_CTXT_CHECK_JOB_KEY: val = s->send_ctxt_check_job_key; break;
            default: val = 0;
            }
        } else if (addr >= SEND_DMA_CTRL && addr <= SEND_DMA_ENG_ERR_CLEAR) {
            switch (addr) {
            case SEND_DMA_CTRL: val = s->send_dma_ctrl; break;
            case SEND_DMA_BASE_ADDR: val = s->send_dma_base_addr; break;
            case SEND_DMA_LEN_GEN: val = s->send_dma_len_gen; break;
            case SEND_DMA_TAIL: val = s->send_dma_tail; break;
            case SEND_DMA_HEAD_ADDR: val = s->send_dma_head_addr; break;
            case SEND_DMA_PRIORITY_THLD: val = s->send_dma_priority_thld; break;
            case SEND_DMA_RELOAD_CNT: val = s->send_dma_reload_cnt; break;
            case SEND_DMA_DESC_CNT: val = s->send_dma_desc_cnt; break;
            case SEND_DMA_DESC_FETCHED_CNT: val = s->send_dma_desc_fetched_cnt; break;
            case SEND_DMA_ENG_ERR_MASK: val = s->send_dma_eng_err_mask; break;
            case SEND_DMA_ENG_ERR_CLEAR: val = s->send_dma_eng_err_clear; break;
            default: val = 0;
            }
        } else if (addr >= SEND_DMA_CHECK_ENABLE && addr <= SEND_DMA_MEMORY) {
            switch (addr) {
            case SEND_DMA_CHECK_ENABLE: val = s->send_dma_check_enable; break;
            case SEND_DMA_CHECK_VL: val = s->send_dma_check_vl; break;
            case SEND_DMA_CHECK_SLID: val = s->send_dma_check_slid; break;
            case SEND_DMA_CHECK_OPCODE: val = s->send_dma_check_opcode; break;
            case SEND_DMA_CHECK_JOB_KEY: val = s->send_dma_check_job_key; break;
            case SEND_DMA_MEMORY: val = s->send_dma_memory; break;
            default: val = 0;
            }
        } else if (addr >= DCC_CFG_RESET && addr <= DCC_ERR_FLG_CLR) {
            switch (addr) {
            case DCC_CFG_RESET: val = s->dcc_cfg_reset; break;
            case DCC_CFG_PORT_CONFIG: val = s->dcc_cfg_port_config; break;
            case DCC_CFG_PORT_CONFIG1: val = s->dcc_cfg_port_config1; break;
            case DCC_CFG_SC_VL_TABLE_15_0: val = s->dcc_cfg_sc_vl_table_15_0; break;
            case DCC_CFG_SC_VL_TABLE_31_16: val = s->dcc_cfg_sc_vl_table_31_16; break;
            case DCC_CFG_LED_CNTRL: val = s->dcc_cfg_led_cntrl; break;
            case DCC_ERR_FLG_EN: val = s->dcc_err_flg_en; break;
            case DCC_ERR_FLG_CLR: val = s->dcc_err_flg_clr; break;
            default: val = 0;
            }
        } else if (addr >= DCC_ERR_FMCONFIG_ERR_CNT && addr <= DCC_PRF_PORT_XMIT_PKTS_CNT) {
            /* Many counters, return 0 for simplicity */
            val = 0;
        } else if (addr >= DC_LCB_CFG_RUN && addr <= DC_LCB_PG_DBG_FLIT_CRDTS_CNT) {
            switch (addr) {
            case DC_LCB_CFG_RUN: val = s->dc_lcb_cfg_run; break;
            case DC_LCB_CFG_TX_FIFOS_RESET: val = s->dc_lcb_cfg_tx_fifos_reset; break;
            case DC_LCB_CFG_IGNORE_LOST_RCLK: val = s->dc_lcb_cfg_ignore_lost_rclk; break;
            case DC_LCB_CFG_REINIT_AS_SLAVE: val = s->dc_lcb_cfg_reinit_as_slave; break;
            case DC_LCB_CFG_CNT_FOR_SKIP_STALL: val = s->dc_lcb_cfg_cnt_for_skip_stall; break;
            case DC_LCB_CFG_LANE_WIDTH: val = s->dc_lcb_cfg_lane_width; break;
            case DC_LCB_CFG_CLK_CNTR: val = s->dc_lcb_cfg_clk_cntr; break;
            case DC_LCB_CFG_ALLOW_LINK_UP: val = s->dc_lcb_cfg_allow_link_up; break;
            case DC_LCB_CFG_LOOPBACK: val = s->dc_lcb_cfg_loopback; break;
            case DC_LCB_ERR_CLR: val = s->dc_lcb_err_clr; break;
            case DC_LCB_ERR_EN: val = s->dc_lcb_err_en; break;
            case DC_LCB_ERR_INFO_CRC_ERR_LN0: val = s->dc_lcb_err_info_crc_err_ln0; break;
            case DC_LCB_ERR_INFO_CRC_ERR_LN1: val = s->dc_lcb_err_info_crc_err_ln1; break;
            case DC_LCB_ERR_INFO_CRC_ERR_LN2: val = s->dc_lcb_err_info_crc_err_ln2; break;
            case DC_LCB_ERR_INFO_CRC_ERR_LN3: val = s->dc_lcb_err_info_crc_err_ln3; break;
            case DC_LCB_ERR_INFO_CRC_ERR_MULTI_LN: val = s->dc_lcb_err_info_crc_err_multi_ln; break;
            case DC_LCB_ERR_INFO_TOTAL_CRC_ERR: val = s->dc_lcb_err_info_total_crc_err; break;
            case DC_LCB_ERR_INFO_SEQ_CRC_CNT: val = s->dc_lcb_err_info_seq_crc_cnt; break;
            case DC_LCB_ERR_INFO_RX_REPLAY_CNT: val = s->dc_lcb_err_info_rx_replay_cnt; break;
            case DC_LCB_ERR_INFO_TX_REPLAY_CNT: val = s->dc_lcb_err_info_tx_replay_cnt; break;
            case DC_LCB_ERR_INFO_ESCAPE_0_ONLY_CNT: val = s->dc_lcb_err_info_escape_0_only_cnt; break;
            case DC_LCB_ERR_INFO_ESCAPE_0_PLUS1_CNT: val = s->dc_lcb_err_info_escape_0_plus1_cnt; break;
            case DC_LCB_ERR_INFO_ESCAPE_0_PLUS2_CNT: val = s->dc_lcb_err_info_escape_0_plus2_cnt; break;
            case DC_LCB_ERR_INFO_REINIT_FROM_PEER_CNT: val = s->dc_lcb_err_info_reinit_from_peer_cnt; break;
            case DC_LCB_ERR_INFO_SBE_CNT: val = s->dc_lcb_err_info_sbe_cnt; break;
            case DC_LCB_ERR_INFO_MISC_FLG_CNT: val = s->dc_lcb_err_info_misc_flg_cnt; break;
            case DC_LCB_PRF_GOOD_LTP_CNT: val = s->dc_lcb_prf_good_ltp_cnt; break;
            case DC_LCB_PRF_ACCEPTED_LTP_CNT: val = s->dc_lcb_prf_accepted_ltp_cnt; break;
            case DC_LCB_PRF_RX_FLIT_CNT: val = s->dc_lcb_prf_rx_flit_cnt; break;
            case DC_LCB_PRF_TX_FLIT_CNT: val = s->dc_lcb_prf_tx_flit_cnt; break;
            case DC_LCB_PRF_CLK_CNTR: val = s->dc_lcb_prf_clk_cntr; break;
            case DC_LCB_STS_LINK_TRANSFER_ACTIVE: val = s->dc_lcb_sts_link_transfer_active; break;
            case DC_LCB_PG_STS_TX_SBE_CNT: val = s->dc_lcb_pg_sts_tx_sbe_cnt; break;
            case DC_LCB_PG_STS_TX_MBE_CNT: val = s->dc_lcb_pg_sts_tx_mbe_cnt; break;
            case DC_LCB_PG_STS_PAUSE_COMPLETE_CNT: val = s->dc_lcb_pg_sts_pause_complete_cnt; break;
            case DC_LCB_PG_DBG_FLIT_CRDTS_CNT: val = s->dc_lcb_pg_dbg_flit_crdts_cnt; break;
            default: val = 0;
            }
        } else if (addr >= DC_DC8051_CFG_RAM_ACCESS_SETUP && addr <= DC_DC8051_STS_REMOTE_PORT_NO) {
            switch (addr) {
            case DC_DC8051_CFG_RAM_ACCESS_SETUP: val = s->dc_dc8051_cfg_ram_access_setup; break;
            case DC_DC8051_CFG_RAM_ACCESS_CTRL: val = s->dc_dc8051_cfg_ram_access_ctrl; break;
            case DC_DC8051_CFG_RAM_ACCESS_WR_DATA: val = s->dc_dc8051_cfg_ram_access_wr_data; break;
            case DC_DC8051_CFG_RAM_ACCESS_STATUS: val = s->dc_dc8051_cfg_ram_access_status; break;
            case DC_DC8051_CFG_HOST_CMD_0: val = s->dc_dc8051_cfg_host_cmd_0; break;
            case DC_DC8051_CFG_HOST_CMD_1: val = s->dc_dc8051_cfg_host_cmd_1; break;
            case DC_DC8051_CFG_LOCAL_GUID: val = s->dc_dc8051_cfg_local_guid; break;
            case DC_DC8051_STS_REMOTE_GUID: val = s->dc_dc8051_sts_remote_guid; break;
            case DC_DC8051_STS_REMOTE_NODE_TYPE: val = s->dc_dc8051_sts_remote_node_type; break;
            case DC_DC8051_STS_REMOTE_FM_SECURITY: val = s->dc_dc8051_sts_remote_fm_security; break;
            case DC_DC8051_STS_CUR_STATE: val = s->dc_dc8051_sts_cur_state; break;
            case DC_DC8051_CFG_RST: val = s->dc_dc8051_cfg_rst; break;
            case DC_DC8051_CFG_MODE: val = s->dc_dc8051_cfg_mode; break;
            case DC_DC8051_ERR_CLR: val = s->dc_dc8051_err_clr; break;
            case DC_DC8051_ERR_EN: val = s->dc_dc8051_err_en; break;
            case DC_DC8051_CFG_CSR_ACCESS_SEL: val = s->dc_dc8051_cfg_csr_access_sel; break;
            case DC_DC8051_CFG_EXT_DEV_0: val = s->dc_dc8051_cfg_ext_dev_0; break;
            case DC_DC8051_CFG_EXT_DEV_1: val = s->dc_dc8051_cfg_ext_dev_1; break;
            case DC_DC8051_STS_REMOTE_PORT_NO: val = s->dc_dc8051_sts_remote_port_no; break;
            default: val = 0;
            }
        } else if (addr >= MISC_CFG_FW_CTRL && addr <= MISC_ERR_CLEAR) {
            switch (addr) {
            case MISC_CFG_FW_CTRL: val = s->misc_cfg_fw_ctrl; break;
            case MISC_CFG_RSA_R2: val = s->misc_cfg_rsa_r2; break;
            case MISC_CFG_RSA_SIGNATURE: val = s->misc_cfg_rsa_signature; break;
            case MISC_CFG_RSA_MODULUS: val = s->misc_cfg_rsa_modulus; break;
            case MISC_CFG_SHA_PRELOAD: val = s->misc_cfg_sha_preload; break;
            case MISC_CFG_RSA_MU: val = s->misc_cfg_rsa_mu; break;
            case MISC_CFG_RSA_CMD: val = s->misc_cfg_rsa_cmd; break;
            case MISC_ERR_STATUS: val = s->misc_err_status; break;
            case MISC_ERR_MASK: val = s->misc_err_mask; break;
            case MISC_ERR_CLEAR: val = s->misc_err_clear; break;
            default: val = 0;
            }
        } else if (addr >= ASIC_CFG_SBUS_REQUEST && addr <= ASIC_EEP_DATA) {
            switch (addr) {
            case ASIC_CFG_SBUS_REQUEST: val = s->asic_cfg_sbus_request; break;
            case ASIC_CFG_SBUS_EXECUTE: val = s->asic_cfg_sbus_execute; break;
            case ASIC_STS_SBUS_RESULT: val = s->asic_sts_sbus_result; break;
            case ASIC_STS_SBUS_COUNTERS: val = s->asic_sts_sbus_counters; break;
            case ASIC_CFG_SCRATCH: val = s->asic_cfg_scratch[0]; break;
            case ASIC_CFG_SCRATCH_1: val = s->asic_cfg_scratch[1]; break;
            case ASIC_CFG_SCRATCH_2: val = s->asic_cfg_scratch[2]; break;
            case ASIC_CFG_SCRATCH_3: val = s->asic_cfg_scratch[3]; break;
            case ASIC_CFG_MUTEX: val = s->asic_cfg_mutex; break;
            case ASIC_CFG_THERM_POLL_EN: val = s->asic_cfg_therm_poll_en; break;
            case ASIC_PCIE_SD_HOST_CMD: val = s->asic_pcie_sd_host_cmd; break;
            case ASIC_PCIE_SD_HOST_STATUS: val = s->asic_pcie_sd_host_status; break;
            case ASIC_PCIE_SD_INTRPT_LIST: val = s->asic_pcie_sd_intrpt_list; break;
            case ASIC_QSFP1_IN: val = s->asic_qsfp1_in; break;
            case ASIC_QSFP2_IN: val = s->asic_qsfp2_in; break;
            case ASIC_QSFP1_OE: val = s->asic_qsfp1_oe; break;
            case ASIC_QSFP2_OE: val = s->asic_qsfp2_oe; break;
            case ASIC_QSFP1_OUT: val = s->asic_qsfp1_out; break;
            case ASIC_QSFP2_OUT: val = s->asic_qsfp2_out; break;
            case ASIC_QSFP1_INVERT: val = s->asic_qsfp1_invert; break;
            case ASIC_QSFP2_INVERT: val = s->asic_qsfp2_invert; break;
            case ASIC_QSFP1_MASK: val = s->asic_qsfp1_mask; break;
            case ASIC_QSFP2_MASK: val = s->asic_qsfp2_mask; break;
            case ASIC_QSFP1_CLEAR: val = s->asic_qsfp1_clear; break;
            case ASIC_QSFP2_CLEAR: val = s->asic_qsfp2_clear; break;
            case ASIC_EEP_CTL_STAT: val = s->asic_eep_ctl_stat; break;
            case ASIC_EEP_ADDR_CMD: val = s->asic_eep_addr_cmd; break;
            case ASIC_EEP_DATA: val = s->asic_eep_data; break;
            default: val = 0;
            }
        } else if (addr >= PCIE_CFG_TPH2 && addr <= PCIE_CFG_REG_PL106) {
            switch (addr) {
            case PCIE_CFG_TPH2: val = s->pcie_cfg_tph2; break;
            case PCI_CFG_MSIX0: val = s->pci_cfg_msix0; break;
            case PCIE_CFG_SPCIE2: val = s->pcie_cfg_spcie2; break;
            case PCIE_CFG_REG_PL2: val = s->pcie_cfg_reg_pl2; break;
            case PCIE_CFG_REG_PL3: val = s->pcie_cfg_reg_pl3; break;
            case PCIE_CFG_REG_PL100: val = s->pcie_cfg_reg_pl100; break;
            case PCIE_CFG_REG_PL101: val = s->pcie_cfg_reg_pl101; break;
            case PCIE_CFG_REG_PL102: val = s->pcie_cfg_reg_pl102; break;
            case PCIE_CFG_REG_PL103: val = s->pcie_cfg_reg_pl103; break;
            case PCIE_CFG_REG_PL105: val = s->pcie_cfg_reg_pl105; break;
            case PCIE_CFG_REG_PL106: val = s->pcie_cfg_reg_pl106; break;
            default: val = 0;
            }
        } else {
            val = 0; /* unhandled address */
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CCE_REVISION:
    case CCE_REVISION2:
    case CCE_STATUS:
        /* Read-only, ignore */
        break;
    case CCE_DC_CTRL:
        s->cce_dc_ctrl = val;
        /* Handling reset? Might need to reset device on certain writes */
        break;
    case CCE_CTRL:
        s->cce_ctrl = val;
        break;
    case CCE_ERR_MASK:
        s->cce_err_mask = val;
        break;
    case CCE_ERR_CLEAR:
        s->cce_err_clear = val;
        /* Clear error status bits if write-1-to-clear */
        break;
    case CCE_SCRATCH:
    case CCE_SCRATCH + 0x08:
    case CCE_SCRATCH + 0x10:
    case CCE_SCRATCH + 0x18:
        {
            int index = (addr - CCE_SCRATCH) / 0x08;
            if (index < CCE_NUM_SCRATCH) {
                s->cce_scratch[index] = val;
            }
        }
        break;
    case CCE_PCIE_CTRL:
        s->cce_pcie_ctrl = val;
        break;
    case CCE_INT_MASK:
        s->cce_int_mask[0] = val;
        pcibase_update_irq(s);
        break;
    case CCE_INT_FORCE:
        s->cce_int_force = val;
        break;
    case CCE_INT_CLEAR:
        /* Write-1-to-clear for interrupt status */
        {
            int i;
            for (i = 0; i < CCE_NUM_INT_CSRS; i++) {
                s->cce_int_status[i] &= ~val;
            }
            s->cce_int_clear = val;
        }
        pcibase_update_irq(s);
        break;
    default:
        if (addr >= CCE_INT_STATUS && addr < CCE_INT_STATUS + 0x100 * CCE_NUM_INT_CSRS && (addr - CCE_INT_STATUS) % 0x100 == 0) {
            /* Status is read-only */
        } else if (addr >= CCE_INT_MASK && addr < CCE_INT_MASK + 0x100 * CCE_NUM_INT_CSRS && (addr - CCE_INT_MASK) % 0x100 == 0) {
            int idx = (addr - CCE_INT_MASK) / 0x                  {
                s->cce_int_mask[idx] = val;
                pcibase_update_irq(s);
            }
        } else if (addr >= CCE_INT_MAP && addr < CCE_INT_MAP + 0x100 * CCE_NUM_INT_MAP_CSRS && (addr - CCE_INT_MAP) % 0x100 == 0) {
            int idx = (addr - CCE_INT_MAP) / 0x100;
            if (idx < CCE_NUM_INT_MAP_CSRS) {
                s->cce_int_map[idx] = val;
            }
        } else if (addr >= RCV_CTRL && addr <= RCV_VL15) {
            switch (addr) {
            case RCV_CTRL: s->rcv_ctrl = val; break;
            case RCV_BTH_QP: s->rcv_bth_qp = val; break;
            case RCV_MULTICAST: s->rcv_multicast = val; break;
            case RCV_BYPASS: s->rcv_bypass = val; break;
            case RCV_VL15: s->rcv_vl15 = val; break;
            default: break; /* RCV_STATUS, RCV_CONTEXTS, RCV_ARRAY_CNT read-only */
            }
        } else if (addr == RCV_ERR_MASK) {
            s->rcv_err_mask = val;
        } else if (addr == RCV_ERR_CLEAR) {
            s->rcv_err_clear = val; /* also clear error status? just store */
        } else if (addr == RCV_QP_MAP_TABLE) {
            s->rcv_qp_map_table = val;
        } else if (addr == RCV_PARTITION_KEY) {
            s->rcv_partition_key = val;
        } else if (addr >= RCV_RSM_CFG && addr <= RCV_RSM_MAP_TABLE) {
            switch (addr) {
            case RCV_RSM_CFG: s->rcv_rsm_cfg = val; break;
            case RCV_RSM_MATCH: s->rcv_rsm_match = val; break;
            case RCV_RSM_SELECT: s->rcv_rsm_select = val; break;
            case RCV_RSM_MAP_TABLE: s->rcv_rsm_map_table = val; break;
            default: break;
            }
        } else if (addr >= RCV_CTXT_CTRL && addr <= RCV_HDR_OVFL_CNT) {
            switch (addr) {
            case RCV_CTXT_CTRL: s->rcv_ctxt_ctrl = val; break;
            case RCV_EGR_CTRL: s->rcv_egr_ctrl = val; break;
            case RCV_TID_CTRL: s->rcv_tid_ctrl = val; break;
            case RCV_KEY_CTRL: s->rcv_key_ctrl = val; break;
            case RCV_HDR_ADDR: s->rcv_hdr_addr = val; break;
            case RCV_HDR_CNT: s->rcv_hdr_cnt = val; break;
            case RCV_HDR_ENT_SIZE: s->rcv_hdr_ent_size = val; break;
            case RCV_HDR_SIZE: s->rcv_hdr_size = val; break;
            case RCV_HDR_TAIL_ADDR: s->rcv_hdr_tail_addr = val; break;
            case RCV_AVAIL_TIME_OUT: s->rcv_avail_time_out = val; break;
            case RCV_HDR_OVFL_CNT: s->rcv_hdr_ovfl_cnt = val; break;
            default: break; /* RCV_CTXT_STATUS read-only */
            }
        } else if (addr == RCV_TID_FLOW_TABLE) {
            s->rcv_tid_flow_table = val;
        } else if (addr >= SEND_CTRL && addr <= SEND_HIGH_PRIORITY_LIMIT) {
            switch (addr) {
            case SEND_CTRL: s->send_ctrl = val; break;
            case SEND_HIGH_PRIORITY_LIMIT: s->send_high_priority_limit = val; break;
            default: break; /* SEND_CONTEXTS, SEND_DMA_ENGINES, SEND_PIO_MEM_SIZE read-only */
            }
        } else if (addr == SEND_PIO_INIT_CTXT) {
            s->send_pio_init_ctxt = val;
        } else if (addr == SEND_PIO_ERR_MASK) {
            s->send_pio_err_mask = val;
        } else if (addr == SEND_PIO_ERR_CLEAR) {
            s->send_pio_err_clear = val;
        } else if (addr == SEND_DMA_ERR_MASK) {
            s->send_dma_err_mask = val;
        } else if (addr == SEND_DMA_ERR_CLEAR) {
            s->send_dma_err_clear = val;
        } else if (addr == SEND_EGRESS_ERR_MASK) {
            s->send_egress_err_mask = val;
        } else if (addr == SEND_EGRESS_ERR_CLEAR) {
            s->send_egress_err_clear = val;
        } else if (addr == SEND_BTH_QP) {
            s->send_bth_qp = val;
        } else if (addr == SEND_STATIC_RATE_CONTROL) {
            s->send_static_rate_control = val;
        } else if (addr >= SEND_SC2VLT0 && addr <= SEND_SC2VLT3) {
            switch (addr) {
            case SEND_SC2VLT0: s->send_sc2vlt0 = val; break;
            case SEND_SC2VLT1: s->send_sc2vlt1 = val; break;
            case SEND_SC2VLT2: s->send_sc2vlt2 = val; break;
            case SEND_SC2VLT3: s->send_sc2vlt3 = val; break;
            }
        } else if (addr >= SEND_LEN_CHECK0 && addr <= SEND_LEN_CHECK1) {
            switch (addr) {
            case SEND_LEN_CHECK0: s->send_len_check0 = val; break;
            case SEND_LEN_CHECK1: s->send_len_check1 = val; break;
            }
        } else if (addr == SEND_ERR_MASK) {
            s->send_err_mask = val;
        } else if (addr == SEND_ERR_CLEAR) {
            s->send_err_clear = val;
        } else if (addr >= SEND_LOW_PRIORITY_LIST && addr <= SEND_HIGH_PRIORITY_LIST) {
            switch (addr) {
            case SEND_LOW_PRIORITY_LIST: s->send_low_priority_list = val; break;
            case SEND_HIGH_PRIORITY_LIST: s->send_high_priority_list = val; break;
            }
        } else if (addr == SEND_CONTEXT_SET_CTRL) {
            s->send_context_set_ctrl = val;
        } else if (addr >= SEND_CM_CTRL && addr <= SEND_CM_REMOTE_AU_TABLE4_TO7) {
            switch (addr) {
            case SEND_CM_CTRL: s->send_cm_ctrl = val; break;
            case SEND_CM_GLOBAL_CREDIT: s->send_cm_global_credit = val; break;
            case SEND_CM_TIMER_CTRL: s->send_cm_timer_ctrl = val; break;
            case SEND_CM_LOCAL_AU_TABLE0_TO3: s->send_cm_local_au_table0_to3 = val; break;
            case SEND_CM_LOCAL_AU_TABLE4_TO7: s->send_cm_local_au_table4_to7 = val; break;
            case SEND_CM_REMOTE_AU_TABLE0_TO3: s->send_cm_remote_au_table0_to3 = val; break;
            case SEND_CM_REMOTE_AU_TABLE4_TO7: s->send_cm_remote_au_table4_to7 = val; break;
            }
        } else if (addr >= SEND_CM_CREDIT_VL && addr <= SEND_CM_CREDIT_VL15) {
            switch (addr) {
            case SEND_CM_CREDIT_VL: s->send_cm_credit_vl = val; break;
            case SEND_CM_CREDIT_VL15: s->send_cm_credit_vl15 = val; break;
            }
        } else if (addr == SEND_EGRESS_CTXT_STATUS) {
            /* read-only, ignore write */;
        } else if (addr == SEND_EGRESS_ERR_INFO) {
            /* read-only, ignore */;
        } else if (addr == SEND_EGRESS_SEND_DMA_STATUS) {
            /* read-only */;
        } else if (addr >= SEND_CTXT_CTRL && addr <= SEND_CTXT_ERR_CLEAR) {
            switch (addr) {
            case SEND_CTXT_CTRL: s->send_ctxt_ctrl = val; break;
            case SEND_CTXT_CREDIT_CTRL: s->send_ctxt_credit_ctrl = val; break;
            case SEND_CTXT_CREDIT_RETURN_ADDR: s->send_ctxt_credit_return_addr = val; break;
            case SEND_CTXT_CREDIT_FORCE: s->send_ctxt_credit_force = val; break;
            case SEND_CTXT_ERR_MASK: s->send_ctxt_err_mask = val; break;
            case SEND_CTXT_ERR_CLEAR: s->send_ctxt_err_clear = val; break;
            }
        } else if (addr >= SEND_CTXT_CHECK_ENABLE && addr <= SEND_CTXT_CHECK_PARTITION_KEY) {
            switch (addr) {
            case SEND_CTXT_CHECK_ENABLE: s->send_ctxt_check_enable = val; break;
            case SEND_CTXT_CHECK_VL: s->send_ctxt_check_vl = val; break;
            case SEND_CTXT_CHECK_SLID: s->send_ctxt_check_slid = val; break;
            case SEND_CTXT_CHECK_OPCODE: s->send_ctxt_check_opcode = val; break;
            case SEND_CTXT_CHECK_JOB_KEY: s->send_ctxt_check_job_key = val; break;
            }
        } else if (addr >= SEND_DMA_CTRL && addr <= SEND_DMA_ENG_ERR_CLEAR) {
            switch (addr) {
            case SEND_DMA_CTRL: s->send_dma_ctrl = val; break;
            case SEND_DMA_BASE_ADDR: s->send_dma_base_addr = val; break;
            case SEND_DMA_LEN_GEN: s->send_dma_len_gen = val; break;
            case SEND_DMA_TAIL: s->send_dma_tail = val; break;
            case SEND_DMA_HEAD_ADDR: s->send_dma_head_addr = val; break;
            case SEND_DMA_PRIORITY_THLD: s->send_dma_priority_thld = val; break;
            case SEND_DMA_RELOAD_CNT: s->send_dma_reload_cnt = val; break;
            case SEND_DMA_DESC_CNT: s->send_dma_desc_cnt = val; break;
            case SEND_DMA_DESC_FETCHED_CNT: s->send_dma_desc_fetched_cnt = val; break;
            case SEND_DMA_ENG_ERR_MASK: s->send_dma_eng_err_mask = val; break;
            case SEND_DMA_ENG_ERR_CLEAR: s->send_dma_eng_err_clear = val; break;
            }
        } else if (addr >= SEND_DMA_CHECK_ENABLE && addr <= SEND_DMA_MEMORY) {
            switch (addr) {
            case SEND_DMA_CHECK_ENABLE: s->send_dma_check_enable = val; break;
            case SEND_DMA_CHECK_VL: s->send_dma_check_vl = val; break;
            case SEND_DMA_CHECK_SLID: s->send_dma_check_slid = val; break;
            case SEND_DMA_CHECK_OPCODE: s->send_dma_check_opcode = val; break;
            case SEND_DMA_CHECK_JOB_KEY: s->send_dma_check_job_key = val; break;
            case SEND_DMA_MEMORY: s->send_dma_memory = val; break;
            }
        } else if (addr == DCC_CFG_RESET) {
            s->dcc_cfg_reset = val;
        } else if (addr == DCC_CFG_PORT_CONFIG) {
            s->dcc_cfg_port_config = val;
        } else if (addr == DCC_CFG_PORT_CONFIG1) {
            s->dcc_cfg_port_config1 = val;
        } else if (addr == DCC_CFG_SC_VL_TABLE_15_0) {
            s->dcc_cfg_sc_vl_table_15_0 = val;
        } else if (addr == DCC_CFG_SC_VL_TABLE_31_16) {
            s->dcc_cfg_sc_vl_table_31_16 = val;
        } else if (addr == DCC_CFG_LED_CNTRL) {
            s->dcc_cfg_led_cntrl = val;
        } else if (addr == DCC_ERR_FLG_EN) {
            s->dcc_err_flg_en = val;
        } else if (addr == DCC_ERR_FLG_CLR) {
            s->dcc_err_flg_clr = val;
        } else if (addr >= DC_LCB_CFG_RUN && addr <= DC_LCB_PG_DBG_FLIT_CRDTS_CNT) {
            switch (addr) {
            case DC_LCB_CFG_RUN: s->dc_lcb_cfg_run = val; break;
            case DC_LCB_CFG_TX_FIFOS_RESET: s->dc_lcb_cfg_tx_fifos_reset = val; break;
            case DC_LCB_CFG_IGNORE_LOST_RCLK: s->dc_lcb_cfg_ignore_lost_rclk = val; break;
            case DC_LCB_CFG_REINIT_AS_SLAVE: s->dc_lcb_cfg_reinit_as_slave = val; break;
            case DC_LCB_CFG_CNT_FOR_SKIP_STALL: s->dc_lcb_cfg_cnt_for_skip_stall = val; break;
            case DC_LCB_CFG_LANE_WIDTH: s->dc_lcb_cfg_lane_width = val; break;
            case DC_LCB_CFG_CLK_CNTR: s->dc_lcb_cfg_clk_cntr = val; break;
            case DC_LCB_CFG_ALLOW_LINK_UP: s->dc_lcb_cfg_allow_link_up = val; break;
            case DC_LCB_CFG_LOOPBACK: s->dc_lcb_cfg_loopback = val; break;
            case DC_LCB_ERR_CLR: s->dc_lcb_err_clr = val; break;
            case DC_LCB_ERR_EN: s->dc_lcb_err_en = val; break;
            /* error info counters are read-only, ignore */
            default: if (addr >= DC_LCB_ERR_INFO_CRC_ERR_LN0 && addr <= DC_LCB_ERR_INFO_MISC_FLG_CNT) { /* read-only */ }
                     else if (addr >= DC_LCB_PRF_GOOD_LTP_CNT && addr <= DC_LCB_PRF_CLK_CNTR) { /* read-only */ }
                     else if (addr == DC_LCB_STS_LINK_TRANSFER_ACTIVE) { /* read-only */ }
                     else if (addr >= DC_LCB_PG_STS_TX_SBE_CNT && addr <= DC_LCB_PG_DBG_FLIT_CRDTS_CNT) { /* read-only */ }
            }
        } else if (addr >= DC_DC8051_CFG_RAM_ACCESS_SETUP && addr <= DC_DC8051_STS_REMOTE_PORT_NO) {
            switch (addr) {
            case DC_DC8051_CFG_RAM_ACCESS_SETUP: s->dc_dc8051_cfg_ram_access_setup = val; break;
            case DC_DC8051_CFG_RAM_ACCESS_CTRL: s->dc_dc8051_cfg_ram_access_ctrl = val; break;
            case DC_DC8051_CFG_RAM_ACCESS_WR_DATA: s->dc_dc8051_cfg_ram_access_wr_data = val; break;
            case DC_DC8051_CFG_RAM_ACCESS_STATUS: /* read-only */ break;
            case DC_DC8051_CFG_HOST_CMD_0: s->dc_dc8051_cfg_host_cmd_0 = val; break;
            case DC_DC8051_CFG_HOST_CMD_1: s->dc_dc8051_cfg_host_cmd_1 = val; break;
            case DC_DC8051_CFG_LOCAL_GUID: s->dc_dc8051_cfg_local_guid = val; break;
            case DC_DC8051_STS_REMOTE_GUID: /* read-only */ break;
            case DC_DC8051_STS_REMOTE_NODE_TYPE: /* read-only */ break;
            case DC_DC8051_STS_REMOTE_FM_SECURITY: /* read-only */ break;
            case DC_DC8051_STS_CUR_STATE: /* read-only */ break;
            case DC_DC8051_CFG_RST: s->dc_dc8051_cfg_rst = val; break;
            case DC_DC8051_CFG_MODE: s->dc_dc8051_cfg_mode = val; break;
            case DC_DC8051_ERR_CLR: s->dc_dc8051_err_clr = val; break;
            case DC_DC8051_ERR_EN: s->dc_dc8051_err_en = val; break;
            case DC_DC8051_CFG_CSR_ACCESS_SEL: s->dc_dc8051_cfg_csr_access_sel = val; break;
            case DC_DC8051_CFG_EXT_DEV_0: s->dc_dc8051_cfg_ext_dev_0 = val; break;
            case DC_DC8051_CFG_EXT_DEV_1: s->dc_dc8051_cfg_ext_dev_1 = val; break;
            case DC_DC8051_STS_REMOTE_PORT_NO: /* read-only */ break;
            }
        } else if (addr >= MISC_CFG_FW_CTRL && addr <= MISC_ERR_CLEAR) {
            switch (addr) {
            case MISC_CFG_FW_CTRL: s->misc_cfg_fw_ctrl = val; break;
            case MISC_CFG_RSA_R2: s->misc_cfg_rsa_r2 = val; break;
            case MISC_CFG_RSA_SIGNATURE: s->misc_cfg_rsa_signature = val; break;
            case MISC_CFG_RSA_MODULUS: s->misc_cfg_rsa_modulus = val; break;
            case MISC_CFG_SHA_PRELOAD: s->misc_cfg_sha_preload = val; break;
            case MISC_CFG_RSA_MU: s->misc_cfg_rsa_mu = val; break;
            case MISC_CFG_RSA_CMD: s->misc_cfg_rsa_cmd = val; break;
            case MISC_ERR_STATUS: /* read-only */ break;
            case MISC_ERR_MASK: s->misc_err_mask = val; break;
            case MISC_ERR_CLEAR: s->misc_err_clear = val; break;
            }
        } else if (addr >= ASIC_CFG_SBUS_REQUEST && addr <= ASIC_EEP_DATA) {
            switch (addr) {
            case ASIC_CFG_SBUS_REQUEST: s->asic_cfg_sbus_request = val; break;
            case ASIC_CFG_SBUS_EXECUTE: s->asic_cfg_sbus_execute = val; break;
            case ASIC_STS_SBUS_RESULT: /* read-only */ break;
            case ASIC_STS_SBUS_COUNTERS: /* read-only */ break;
            case ASIC_CFG_SCRATCH: s->asic_cfg_scratch[0] = val; break;
            case ASIC_CFG_SCRATCH_1: s->asic_cfg_scratch[1] = val; break;
            case ASIC_CFG_SCRATCH_2: s->asic_cfg_scratch[2] = val; break;
            case ASIC_CFG_SCRATCH_3: s->asic_cfg_scratch[3] = val; break;
            case ASIC_CFG_MUTEX: s->asic_cfg_mutex = val; break;
            case ASIC_CFG_THERM_POLL_EN: s->asic_cfg_therm_poll_en = val; break;
            case ASIC_PCIE_SD_HOST_CMD: s->asic_pcie_sd_host_cmd = val; break;
            case ASIC_PCIE_SD_HOST_STATUS: /* read-only */ break;
            case ASIC_PCIE_SD_INTRPT_LIST: s->asic_pcie_sd_intrpt_list = val; break;
            case ASIC_QSFP1_IN: s->asic_qsfp1_in = val; break;
            case ASIC_QSFP2_IN: s->asic_qsfp2_in = val; break;
            case ASIC_QSFP1_OE: s->asic_qsfp1_oe = val; break;
            case ASIC_QSFP2_OE: s->asic_qsfp2_oe = val; break;
            case ASIC_QSFP1_OUT: s->asic_qsfp1_out = val; break;
            case ASIC_QSFP2_OUT: s->asic_qsfp2_out = val; break;
            case ASIC_QSFP1_INVERT: s->asic_qsfp1_invert = val; break;
            case ASIC_QSFP2_INVERT: s->asic_qsfp2_invert = val; break;
            case ASIC_QSFP1_MASK: s->asic_qsfp1_mask = val; break;
            case ASIC_QSFP2_MASK: s->asic_qsfp2_mask = val; break;
            case ASIC_QSFP1_CLEAR: s->asic_qsfp1_clear = val; break;
            case ASIC_QSFP2_CLEAR: s->asic_qsfp2_clear = val; break;
            case ASIC_EEP_CTL_STAT: s->asic_eep_ctl_stat = val; break;
            case ASIC_EEP_ADDR_CMD: s->asic_eep_addr_cmd = val; break;
            case ASIC_EEP_DATA: s->asic_eep_data = val; break;
            }
        } else if (addr >= PCIE_CFG_TPH2 && addr <= PCIE_CFG_REG_PL106) {
            switch (addr) {
            case PCIE_CFG_TPH2: s->pcie_cfg_tph2 = val; break;
            case PCI_CFG_MSIX0: s->pci_cfg_msix0 = val; break;
            case PCIE_CFG_SPCIE2: s->pcie_cfg_spcie2 = val; break;
            case PCIE_CFG_REG_PL2: s->pcie_cfg_reg_pl2 = val; break;
            case PCIE_CFG_REG_PL3: s->pcie_cfg_reg_pl3 = val; break;
            case PCIE_CFG_REG_PL100: s->pcie_cfg_reg_pl100 = val; break;
            case PCIE_CFG_REG_PL101: s->pcie_cfg_reg_pl101 = val; break;
            case PCIE_CFG_REG_PL102: s->pcie_cfg_reg_pl102 = val; break;
            case PCIE_CFG_REG_PL103: s->pcie_cfg_reg_pl103 = val; break;
            case PCIE_CFG_REG_PL105: s->pcie_cfg_reg_pl105 = val; break;
            case PCIE_CFG_REG_PL106: s->pcie_cfg_reg_pl106 = val; break;
            }
        } else {
            /* Unknown address, ignore */
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO access expected, return 0 */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* ignore */
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

    memset(&s->cce_revision, 0, sizeof(PCIBaseState) - offsetof(PCIBaseState, cce_revision));

    /* Set initial register values that driver expects */
    s->cce_revision = (0x1ull << CCE_REVISION_SW_SHIFT) |
                      (HFI1_CHIP_VERS_MAJ << CCE_REVISION_CHIP_REV_MAJOR_SHIFT) |
                      (HFI1_CHIP_VERS_MIN << CCE_REVISION_CHIP_REV_MINOR_SHIFT);
    s->cce_revision2 = 0; /* HFI_ID=0, etc. */
    s->rcv_contexts = 16;
    s->rcv_array_cnt = 1024;
    s->send_contexts = 16;
    s->send_dma_engines = 16;
    s->send_pio_mem_size = TXE_PIO_SIZE;
    s->dcc_cfg_port_config = (1ULL << DCC_CFG_PORT_CONFIG_LINK_STATE_SHIFT); /* link state active? */
    s->dcc_cfg_port_config1 = 0;
    /* Other registers zero */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR sizes: BAR0 covers register space (at least 4MB to cover all registers), BAR1 covers PIO send area */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x400000, .name = "hfi1_bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = TXE_PIO_SIZE, .name = "hfi1_bar1" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    if (msix_enabled(pdev)) {
        uint32_t msix_table_offset = CCE_MSIX_TABLE_LOWER - CORE;
        uint32_t msix_pba_offset = CCE_MSIX_TABLE_UPPER - CORE;
        memory_region_init_ram(&s->msix_table, OBJECT(s), "hfi1-msix-table", 16 * CCE_NUM_MSIX_VECTORS, errp);
        memory_region_init_ram(&s->msix_pba, OBJECT(s), "hfi1-msix-pba", 8 * CCE_NUM_MSIX_PBAS, errp);
        msix_init(pdev, CCE_NUM_MSIX_VECTORS, &s->msix_table, 0, msix_table_offset, &s->msix_pba, 0, msix_pba_offset, errp);
        if (*errp) {
            return;
        }
    }

    /* Device reset to set initial values */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->msix_table, &s->msix_pba);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "hfi1_pci",
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
