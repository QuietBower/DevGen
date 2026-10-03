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
#include "qemu/cutils.h"

#define TYPE_PCIBASE_DEVICE "CNN55XX_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define NITROX_VENDOR_ID 0x177d
#define NITROX_DEVICE_ID 0x12
#define NITROX_PCI_CLASS 0x108000

#define DRIVER_VERSION "1.2"
#define FW_DIR "cavium/"
#define CNN55XX_DEV_ID	0x12
#define UCODE_HLEN 48
#define DEFAULT_SE_GROUP 0
#define DEFAULT_AE_GROUP 0
#define CNN55XX_UCD_BLOCK_SIZE 32768
#define CNN55XX_MAX_UCODE_SIZE (CNN55XX_UCD_BLOCK_SIZE * 2)
#define SE_FW	FW_DIR "cnn55xx_se.fw"
#define AE_FW	FW_DIR "cnn55xx_ae.fw"
#define UCD_UCODE_LOAD_BLOCK_NUM	0x12C0010
#define UCD_UCODE_LOAD_IDX_DATAX(_i)	(0x12C0018 + ((_i) * 0x20))
#define DEV(ndev) ((struct device *)(&(ndev)->pdev->dev))
#define POM_GRP_EXECMASKX(_i)	(0x11C1100 | ((_i) * 8))
#define UCD_AE_EID_UCODE_BLOCK_NUMX(_i)	(0x12C0008 + ((_i) * 0x800))
#define UCD_SE_EID_UCODE_BLOCK_NUMX(_i)	(0x12C0000 + ((_i) * 0x1000))
#define AQM_GRP_EXECMSK_LOX(x)          (0x1300C00 + ((x) * 0x10))
#define AQM_GRP_EXECMSK_HIX(x)          (0x1300C08 + ((x) * 0x10))
#define VERSION_LEN 32
#define PEM_BIST_STATUSX(_i)	(0x1080468 | ((_i) << 18))
#define EFL_TOP_BIST_STAT	0x1241090
#define LBC_BIST_STATUS		0x1200020
#define UCD_BIST_STATUS		0x12C0070
#define EMU_BIST_STATUSX(_i)	(0x1402700 + ((_i) * 0x40000))
#define POM_BIST_REG		0x11C0100
#define EFL_CORE_BIST_REGX(_i)	(0x1240100 + ((_i) * 0x400))
#define NPS_CORE_BIST_REG	0x10000E8
#define BMI_BIST_REG		0x1140080
#define NR_CLUSTERS		4
#define BMO_BIST_REG		0x1180080
#define NPS_CORE_NPC_BIST_REG	0x1000128
#define NPS_PKT_SLC_BIST_REG	0x1040088
#define NPS_PKT_IN_BIST_REG	0x1040100
#define MAX_PF_QUEUES	64
#define CMD_TIMEOUT 2000
#define MAX_DEV_QUEUES (MAX_PF_QUEUES)
#define IRQ_NAMESZ	32
#define BMI_INT_ENA_W1S	0x1140018
#define BMI_CTL		0x1140020
#define LBC_ELM_VF65_128_INT_ENA_W1S	0x120F000
#define LBC_PLM_VF1_64_INT_ENA_W1S	0x1205008
#define LBC_PLM_VF65_128_INT_ENA_W1S	0x1209008
#define LBC_ELM_VF1_64_INT_ENA_W1S	0x120B000
#define LBC_INT_ENA_W1S		0x1203000
#define EFL_CORE_INT_ENA_W1SX(_i)		(0x1240018 + ((_i) * 0x400))
#define EFL_CORE_VF_ERR_INT0_ENA_W1SX(_i)	(0x1240068 + ((_i) * 0x400))
#define EFL_CORE_VF_ERR_INT1_ENA_W1SX(_i)	(0x1240088 + ((_i) * 0x400))
#define EMU_WD_INT_ENA_W1SX(_i)	(0x1402318 + ((_i) * 0x40000))
#define EMU_GE_INT_ENA_W1SX(_i)	(0x1402518 + ((_i) * 0x40000))
#define EMU_FUSE_MAPX(_i)	(0x1402708 + ((_i) * 0x40000))
#define FUS_DAT1	0x10C1408
#define SE_CORES_PER_CLUSTER	16
#define AE_CORES_PER_CLUSTER	20
#define RST_BOOT	0x10C1600
#define ZIP_MAX_CORES	5
#define BMO_CTL2		0x1180028
#define POM_PERF_CTL	0x11CC400
#define POM_INT_ENA_W1S		0x11C0018
#define EFL_RNM_CTL_STATUS			0x1241800
#define NPS_CORE_GBL_VFCFG	0x1000000
#define NPS_CORE_CONTROL	0x1000008
#define CNN55XX_MAX_UCD_BLOCKS	8
#define AQMQ_CMD_CNTX(x)                (0x20020 + ((x) * 0x40000))
#define NITROX_CSR_ADDR(ndev, offset) \
	((ndev)->bar_addr + (offset))
#define AQMQ_DRBLX(x)                   (0x20000 + ((x) * 0x40000))
#define NPS_PKT_IN_INSTR_BAOFF_DBELLX(_i)	(0x10078 + ((_i) * 0x40000))
#define NPS_PKT_SLC_CNTSX(_i)		(0x10008 + ((_i) * 0x40000))
#define LBC_INVAL_STATUS	0x1202010
#define LBC_INVAL_CTL		0x1201010
#define EMU_AE_ENABLEX(_i)	(0x1400008 + ((_i) * 0x40000))
#define EMU_SE_ENABLEX(_i)	(0x1400000 + ((_i) * 0x40000))
#define AE_MAX_CORES	(AE_CORES_PER_CLUSTER * NR_CLUSTERS)
#define SE_MAX_CORES	(SE_CORES_PER_CLUSTER * NR_CLUSTERS)
#define AQM_DBELL_OVF_LO_ENA_W1S        0x1300030
#define AQM_DMA_RD_ERR_LO_ENA_W1S       0x1300070
#define AQM_DBELL_OVF_HI_ENA_W1S        0x1300048
#define AQM_EXEC_NA_LO_ENA_W1S          0x13000B0
#define AQM_EXEC_ERR_HI_ENA_W1S         0x1300108
#define AQM_EXEC_NA_HI_ENA_W1S          0x13000C8
#define AQM_DMA_RD_ERR_HI_ENA_W1S       0x1300088
#define AQM_EXEC_ERR_LO_ENA_W1S         0x13000F0
#define AQMQ_NXT_CMDX(x)                (0x20018 + ((x) * 0x40000))
#define AQMQ_QSZX(x)                    (0x20008 + ((x) * 0x40000))
#define AQMQ_BADRX(x)                   (0x20010 + ((x) * 0x40000))
#define AQMQ_CMP_THRX(x)                (0x20028 + ((x) * 0x40000))
#define NPS_PKT_IN_INSTR_RSIZEX(_i)	(0x10070 + ((_i) * 0x40000))
#define NPS_PKT_IN_INT_LEVELSX(_i)		(0x10088 + ((_i) * 0x40000))
#define NPS_PKT_IN_INSTR_BADDRX(_i)	(0x10068 + ((_i) * 0x40000))
#define NPS_PKT_IN_RERR_LO_ENA_W1S	0x1040140
#define NPS_PKT_IN_RERR_HI_ENA_W1S	0x1040120
#define NPS_PKT_IN_ERR_TYPE_ENA_W1S	0x1040160
#define NPS_PKT_SLC_RERR_HI_ENA_W1S	0x1040220
#define NPS_PKT_SLC_ERR_TYPE_ENA_W1S	0x1040260
#define NPS_PKT_SLC_RERR_LO_ENA_W1S	0x1040240
#define NPS_CORE_INT_ENA_W1S	0x10000B8
#define AQMQ_ENX(x)                     (0x20048 + ((x) * 0x40000))
#define AQMQ_CMP_CNTX(x)                (0x20030 + ((x) * 0x40000))
#define AQMQ_ACTIVITY_STATX(x)          (0x20050 + ((x) * 0x40000))
#define NPS_PKT_SLC_INT_LEVELSX(_i)	(0x10010 + ((_i) * 0x40000))
#define NPS_PKT_IN_INSTR_CTLX(_i)	(0x10060 + ((_i) * 0x40000))
#define NPS_PKT_IN_DONE_CNTSX(_i)	(0x10080 + ((_i) * 0x40000))
#define PRIO 4001
#define NPS_PKT_SLC_CTLX(_i)		(0x10000 + ((_i) * 0x40000))
#define DECRYPT 1
#define ENCRYPT	0
#define IV_FROM_DPTR	1
#define FLEXI_CRYPTO_ENCRYPT_HMAC	0x33
#define NPS_PKT_MBOX_INT_HI_ENA_W1S	0x1040058
#define NPS_PKT_MBOX_INT_LO_ENA_W1S	0x1040038
#define NPS_PKT_MBOX_INT_LO_ENA_W1C	0x1040030
#define NPS_PKT_MBOX_INT_HI_ENA_W1C	0x1040050
#define ORH_HLEN	8
#define COMP_HLEN	8
#define PENDING_SIG	0xFFFFFFFFFFFFFFFFUL
#define BAR0_SIZE (64 * 1024 * 1024)  /* 64 MiB to accommodate all registers and MSI-X */
#define AQM_GRP_COUNT 1

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

/* Additional typedefs for union types used in NITROXRegs */
typedef union {
	uint64_t value;
} lbc_elm_vf65_128_int_ena_w1s_t;
typedef union {
	uint64_t value;
} lbc_plm_vf1_64_int_ena_w1s_t;
typedef union {
	uint64_t value;
} lbc_plm_vf65_128_int_ena_w1s_t;
typedef union {
	uint64_t value;
} lbc_elm_vf1_64_int_ena_w1s_t;
typedef union {
	uint64_t value;
} efl_core_vf_err_int0_ena_w1s_t;
typedef union {
	uint64_t value;
} efl_core_vf_err_int1_ena_w1s_t;
typedef union {
	uint64_t value;
} aqmq_badr_t;
typedef union {
	uint64_t value;
} aqmq_nxt_cmd_t;

typedef union {
	uint64_t value;
} nps_pkt_in_int_levels_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t max_len_err_nps : 1;
		uint64_t max_len_err_ilk : 1;
		uint64_t pkt_rcv_err_nps : 1;
		uint64_t pkt_rcv_err_ilk : 1;
		uint64_t sop_err_nps	: 1;
		uint64_t sop_err_ilk	: 1;
		uint64_t eop_err_nps	: 1;
		uint64_t eop_err_ilk	: 1;
		uint64_t fpf_undrrn	: 1;
		uint64_t raz_9 : 1;
		uint64_t raz_10 : 1;
		uint64_t nps_req_oflw : 1;
		uint64_t ilk_req_oflw : 1;
		uint64_t raz_13_63 : 51;
	} s;
} bmi_int_ena_w1s_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t max_pkt_len : 8;
		uint64_t totl_free_thrsh : 8;
		uint64_t nps_free_thrsh : 8;
		uint64_t ilk_free_thrsh : 8;
		uint64_t totl_hdrq_thrsh : 8;
		uint64_t nps_hdrq_thrsh : 8;
		uint64_t ilk_hdrq_thrsh : 8;
		uint64_t raz_56_63 : 8;
	} s;
} bmi_ctl_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t dma_rd_err : 1;
		uint64_t cam_soft_err : 1;
		uint64_t raz_2_5 : 4;
		uint64_t cache_line_to_err : 1;
		uint64_t over_fetch_err : 1;
		uint64_t cam_inval_abort : 1;
		uint64_t cam_hard_err : 1;
		uint64_t raz_10_63 : 54;
	} s;
} lbc_int_ena_w1s_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t len_ovr : 1;
		uint64_t d_left : 1;
		uint64_t raz_2_5 : 4;
		uint64_t epci_decode_err : 1;
		uint64_t raz_7_63 : 57;
	} s;
} efl_core_int_ena_w1s_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t se_wd : 16;
		uint64_t raz1 : 16;
		uint64_t ae_wd : 20;
		uint64_t raz2 : 12;
	} s;
} emu_wd_int_ena_w1s_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t se_ge : 16;
		uint64_t raz_16_31: 16;
		uint64_t ae_ge : 20;
		uint64_t raz_52_63 : 12;
	} s;
} emu_ge_int_ena_w1s_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t rboot_pin : 1;
		uint64_t rboot : 1;
		uint64_t lboot : 10;
		uint64_t raz_12_23 : 12;
		uint64_t pnr_mul : 6;
		uint64_t raz_30_36 : 7;
		uint64_t io_supply : 3;
		uint64_t raz_40_57 : 18;
		uint64_t jt_tst_mode : 1;
		uint64_t raz_59_61 : 3;
		uint64_t jtcsrdis : 1;
		uint64_t raz_63 : 1;
	} s;
} rst_boot_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t chip_id : 8;
		uint64_t pll_bwadj_denom : 2;
		uint64_t pll_alt_matrix : 1;
		uint64_t raz_11_17 : 7;
		uint64_t nozip : 1;
		uint64_t efus_ign : 1;
		uint64_t bar2_sz_conf : 1;
		uint64_t zip_info : 5;
		uint64_t raz_26_39 : 14;
		uint64_t efus_lck : 3;
		uint64_t raz_43_52 : 10;
		uint64_t pll_half_dis : 1;
		uint64_t pll_mul : 3;
		uint64_t raz_57_63 : 7;
	} s;
} fus_dat1_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t se_fuse : 16;
		uint64_t raz_16_31 : 16;
		uint64_t ae_fuse : 20;
		uint64_t raz_52_62 : 11;
		uint64_t valid : 1;
	} s;
} emu_fuse_map_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t totl_buf_thrsh : 8;
		uint64_t nps_uns_buf_thrsh : 8;
		uint64_t nps_slc_buf_thrsh : 8;
		uint64_t ilk_buf_thrsh : 8;
		uint64_t raz_32_62 : 31;
		uint64_t arb_sel : 1;
	} s;
} bmo_ctl2_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t raz0 : 1;
		uint64_t raz1 : 1;
		uint64_t illegal_dport : 1;
		uint64_t illegal_intf : 1;
		uint64_t raz2 : 60;
	} s;
} pom_int_ena_w1s_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t ent_en : 1;
		uint64_t rng_en : 1;
		uint64_t rnm_rst : 1;
		uint64_t rng_rst : 1;
		uint64_t exp_ent : 1;
		uint64_t ent_sel : 4;
		uint64_t raz_9_63 : 55;
	} s;
} efl_rnm_ctl_status_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t cfg : 3;
		uint64_t seaf : 1;
		uint64_t aeaf : 1;
		uint64_t zaf : 1;
		uint64_t ibaf : 1;
		uint64_t obaf : 1;
		uint64_t ilk_disable : 1;
		uint64_t raz : 55;
	} s;
} nps_core_gbl_vfcfg_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t done : 1;
		uint64_t cam_rst_rdy : 1;
		uint64_t cam_inval_abort : 1;
		uint64_t raz0 : 5;
		uint64_t cam_inval_state : 3;
		uint64_t raz1 : 5;
		uint64_t cam_clean_entry_cnt : 9;
		uint64_t raz2 : 7;
		uint64_t cam_clean_entry_complete_cnt : 9;
		uint64_t raz3 : 23;
	} s;
} lbc_inval_status_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t raz0 : 1;
		uint64_t cam_inval_start : 1;
		uint64_t raz1 : 6;
		uint64_t wait_timer : 8;
		uint64_t raz2 : 48;
	} s;
} lbc_inval_ctl_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t enable : 20;
		uint64_t raz : 44;
	} s;
} emu_ae_enable_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t enable : 16;
		uint64_t raz : 48;
	} s;
} emu_se_enable_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t commands_completed_threshold : 32;
		uint64_t raz_32_63 : 32;
	} s;
} aqmq_cmp_thr_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t host_queue_size : 32;
		uint64_t raz_32_63 : 32;
	} s;
} aqmq_qsz_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t dbell_count : 32;
		uint64_t raz_32_63 : 32;
	} s;
} aqmq_drbl_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t rsize : 32;
		uint64_t raz : 32;
	} s;
} nps_pkt_in_instr_rsize_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t dbell : 32;
		uint64_t aoff : 32;
	} s;
} nps_pkt_in_instr_baoff_dbell_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t raz0 : 1;
		uint64_t raz1 : 1;
		uint64_t raz2 : 1;
		uint64_t raz3 : 1;
		uint64_t host_wr_err : 1;
		uint64_t host_wr_timeout : 1;
		uint64_t exec_wr_timeout : 1;
		uint64_t npco_dma_malform : 1;
		uint64_t host_nps_wr_err : 1;
		uint64_t raz4 : 55;
	} s;
} nps_core_int_ena_w1s_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t queue_enable : 1;
		uint64_t raz_1_63 : 63;
	} s;
} aqmq_en_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t commands_completed_count : 32;
		uint64_t completion_status : 1;
		uint64_t resend : 1;
		uint64_t raz_34_63 : 30;
	} s;
} aqmq_cmp_cnt_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t queue_active : 1;
		uint64_t raz_1_63 : 63;
	} s;
} aqmq_activity_stat_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t cnt : 32;
		uint64_t timet : 22;
		uint64_t raz : 9;
		uint64_t bmode : 1;
	} s;
} nps_pkt_slc_int_levels_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t enb : 1;
		uint64_t is64b : 1;
		uint64_t raz : 62;
	} s;
} nps_pkt_in_instr_ctl_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t cnt : 32;
		uint64_t raz : 27;
		uint64_t resend : 1;
		uint64_t mbox_int : 1;
		uint64_t in_int : 1;
		uint64_t uns_int : 1;
		uint64_t slc_int : 1;
	} s;
} nps_pkt_in_done_cnts_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t enb : 1;
		uint64_t z : 1;
		uint64_t rh : 1;
		uint64_t raz : 61;
	} s;
} nps_pkt_slc_ctl_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t cnt : 32;
		uint64_t timer : 22;
		uint64_t raz : 5;
		uint64_t resend : 1;
		uint64_t mbox_int : 1;
		uint64_t in_int : 1;
		uint64_t uns_int : 1;
		uint64_t slc_int : 1;
	} s;
} nps_pkt_slc_cnts_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t exec_0_to_39 : 40;
		uint64_t raz_40_63 : 24;
	} s;
} aqm_grp_execmsk_lo_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t exec_40_to_79 : 40;
		uint64_t raz_40_63 : 24;
	} s;
} aqm_grp_execmsk_hi_t;

typedef union {
	uint64_t value;
	struct {
		uint64_t ucode_blk : 3;
		uint64_t ucode_len : 1;
		uint64_t raz_4_63 : 60;
	} s;
} ucd_core_eid_ucode_block_num_t;

typedef union {
	uint64_t value;
} nps_pkt_in_instr_baddr_t;

/* NITROXRegs struct packs all hardware registers */
typedef struct {
	/* NPS Core */
	nps_core_gbl_vfcfg_t nps_core_gbl_vfcfg;
	nps_core_int_ena_w1s_t nps_core_int_ena_w1s;
	nps_pkt_in_instr_ctl_t nps_pkt_in_instr_ctl[MAX_PF_QUEUES];
	nps_pkt_in_instr_baddr_t nps_pkt_in_instr_baddr[MAX_PF_QUEUES];
	nps_pkt_in_instr_rsize_t nps_pkt_in_instr_rsize[MAX_PF_QUEUES];
	nps_pkt_in_instr_baoff_dbell_t nps_pkt_in_instr_baoff_dbell[MAX_PF_QUEUES];
	nps_pkt_in_done_cnts_t nps_pkt_in_done_cnts[MAX_PF_QUEUES];
	nps_pkt_in_int_levels_t nps_pkt_in_int_levels[MAX_PF_QUEUES];
	nps_pkt_slc_ctl_t nps_pkt_slc_ctl[MAX_PF_QUEUES];
	nps_pkt_slc_cnts_t nps_pkt_slc_cnts[MAX_PF_QUEUES];
	nps_pkt_slc_int_levels_t nps_pkt_slc_int_levels[MAX_PF_QUEUES];
	/* NPS Packet */
	uint64_t nps_pkt_in_rerr_lo_ena_w1s;
	uint64_t nps_pkt_in_rerr_hi_ena_w1s;
	uint64_t nps_pkt_in_err_type_ena_w1s;
	uint64_t nps_pkt_slc_rerr_hi_ena_w1s;
	uint64_t nps_pkt_slc_err_type_ena_w1s;
	uint64_t nps_pkt_slc_rerr_lo_ena_w1s;
	uint64_t nps_pkt_mbox_int_hi_ena_w1s;
	uint64_t nps_pkt_mbox_int_lo_ena_w1s;
	uint64_t nps_pkt_mbox_int_lo_ena_w1c;
	uint64_t nps_pkt_mbox_int_hi_ena_w1c;
	/* BMI */
	bmi_int_ena_w1s_t bmi_int_ena_w1s;
	bmi_ctl_t bmi_ctl;
	/* LBC */
	lbc_int_ena_w1s_t lbc_int_ena_w1s;
	lbc_elm_vf65_128_int_ena_w1s_t lbc_elm_vf65_128_int_ena_w1s;
	lbc_plm_vf1_64_int_ena_w1s_t lbc_plm_vf1_64_int_ena_w1s;
	lbc_plm_vf65_128_int_ena_w1s_t lbc_plm_vf65_128_int_ena_w1s;
	lbc_elm_vf1_64_int_ena_w1s_t lbc_elm_vf1_64_int_ena_w1s;
	lbc_inval_status_t lbc_inval_status;
	lbc_inval_ctl_t lbc_inval_ctl;
	/* EFL */
	efl_core_int_ena_w1s_t efl_core_int_ena_w1s[NR_CLUSTERS];
	efl_core_vf_err_int0_ena_w1s_t efl_core_vf_err_int0_ena_w1s[NR_CLUSTERS];
	efl_core_vf_err_int1_ena_w1s_t efl_core_vf_err_int1_ena_w1s[NR_CLUSTERS];
	efl_rnm_ctl_status_t efl_rnm_ctl_status;
	/* EMU */
	emu_wd_int_ena_w1s_t emu_wd_int_ena_w1s[AE_MAX_CORES];
	emu_ge_int_ena_w1s_t emu_ge_int_ena_w1s[AE_MAX_CORES];
	emu_fuse_map_t emu_fuse_map[AE_MAX_CORES];
	emu_ae_enable_t emu_ae_enable[AE_MAX_CORES];
	emu_se_enable_t emu_se_enable[SE_MAX_CORES];
	/* UCD */
	ucd_core_eid_ucode_block_num_t ucd_se_eid_ucode_block_num[SE_MAX_CORES];
	ucd_core_eid_ucode_block_num_t ucd_ae_eid_ucode_block_num[AE_MAX_CORES];
	uint64_t ucd_ucode_load_block_num;
	uint64_t ucd_ucode_load_idx_data[CNN55XX_MAX_UCD_BLOCKS];
	/* BMO */
	bmo_ctl2_t bmo_ctl2;
	/* POM */
	pom_int_ena_w1s_t pom_int_ena_w1s;
	uint64_t pom_grp_execmask[NR_CLUSTERS];
	/* AQM */
	aqm_grp_execmsk_lo_t aqm_grp_execmsk_lo[AQM_GRP_COUNT];
	aqm_grp_execmsk_hi_t aqm_grp_execmsk_hi[AQM_GRP_COUNT];
	uint64_t aqm_dbell_ovf_lo_ena_w1s;
	uint64_t aqm_dma_rd_err_lo_ena_w1s;
	uint64_t aqm_dbell_ovf_hi_ena_w1s;
	uint64_t aqm_exec_na_lo_ena_w1s;
	uint64_t aqm_exec_err_hi_ena_w1s;
	uint64_t aqm_exec_na_hi_ena_w1s;
	uint64_t aqm_dma_rd_err_hi_ena_w1s;
	uint64_t aqm_exec_err_lo_ena_w1s;
	/* AQMQ */
	aqmq_en_t aqmq_en[MAX_DEV_QUEUES];
	aqmq_qsz_t aqmq_qsz[MAX_DEV_QUEUES];
	aqmq_badr_t aqmq_badr[MAX_DEV_QUEUES];
	aqmq_cmp_thr_t aqmq_cmp_thr[MAX_DEV_QUEUES];
	aqmq_cmp_cnt_t aqmq_cmp_cnt[MAX_DEV_QUEUES];
	aqmq_activity_stat_t aqmq_activity_stat[MAX_DEV_QUEUES];
	/* Identification */
	fus_dat1_t fus_dat1;
	rst_boot_t rst_boot;
} NITROXRegs;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar0_container;  /* Container for BAR0 (MMIO + MSI-X) */
    MemoryRegion bar0_mmio;       /* MMIO region for registers */
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    NITROXRegs regs;

    /* DMA Context */
    dma_addr_t dma_addr;

    uint32_t status;     /* Operational status flags */
    bool reset_active;   /* State used to handle reset sequences */
    uint8_t pm_state;    /* Power management state (D0-D3) */
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (msix_enabled(pdev)) {
        if (s->intr_status & s->intr_mask) {
            msix_notify(pdev, 0);
        }
    } else if (msi_enabled(pdev)) {
        if (s->intr_status & s->intr_mask) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, (s->intr_status & s->intr_mask) ? 1 : 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    NITROXRegs *regs = &s->regs;

    if (size != 8) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    switch (addr) {
    case NPS_CORE_GBL_VFCFG:
        val = regs->nps_core_gbl_vfcfg.value;
        break;
    case NPS_CORE_CONTROL:
        /* Not stored, return 0 */
        break;
    case NPS_CORE_INT_ENA_W1S:
        val = regs->nps_core_int_ena_w1s.value;
        break;
    case NPS_PKT_IN_RERR_LO_ENA_W1S:
        val = regs->nps_pkt_in_rerr_lo_ena_w1s;
        break;
    case NPS_PKT_IN_RERR_HI_ENA_W1S:
        val = regs->nps_pkt_in_rerr_hi_ena_w1s;
        break;
    case NPS_PKT_IN_ERR_TYPE_ENA_W1S:
        val = regs->nps_pkt_in_err_type_ena_w1s;
        break;
    case NPS_PKT_SLC_RERR_HI_ENA_W1S:
        val = regs->nps_pkt_slc_rerr_hi_ena_w1s;
        break;
    case NPS_PKT_SLC_ERR_TYPE_ENA_W1S:
        val = regs->nps_pkt_slc_err_type_ena_w1s;
        break;
    case NPS_PKT_SLC_RERR_LO_ENA_W1S:
        val = regs->nps_pkt_slc_rerr_lo_ena_w1s;
        break;
    case NPS_PKT_MBOX_INT_HI_ENA_W1S:
        val = regs->nps_pkt_mbox_int_hi_ena_w1s;
        break;
    case NPS_PKT_MBOX_INT_LO_ENA_W1S:
        val = regs->nps_pkt_mbox_int_lo_ena_w1s;
        break;
    case NPS_PKT_MBOX_INT_LO_ENA_W1C:
        val = regs->nps_pkt_mbox_int_lo_ena_w1c;
        break;
    case NPS_PKT_MBOX_INT_HI_ENA_W1C:
        val = regs->nps_pkt_mbox_int_hi_ena_w1c;
        break;
    case BMI_INT_ENA_W1S:
        val = regs->bmi_int_ena_w1s.value;
        break;
    case BMI_CTL:
        val = regs->bmi_ctl.value;
        break;
    case LBC_ELM_VF65_128_INT_ENA_W1S:
        val = regs->lbc_elm_vf65_128_int_ena_w1s.value;
        break;
    case LBC_PLM_VF1_64_INT_ENA_W1S:
        val = regs->lbc_plm_vf1_64_int_ena_w1s.value;
        break;
    case LBC_PLM_VF65_128_INT_ENA_W1S:
        val = regs->lbc_plm_vf65_128_int_ena_w1s.value;
        break;
    case LBC_ELM_VF1_64_INT_ENA_W1S:
        val = regs->lbc_elm_vf1_64_int_ena_w1s.value;
        break;
    case LBC_INT_ENA_W1S:
        val = regs->lbc_int_ena_w1s.value;
        break;
    case LBC_INVAL_STATUS:
        val = regs->lbc_inval_status.value;
        break;
    case LBC_INVAL_CTL:
        val = regs->lbc_inval_ctl.value;
        break;
    case EFL_RNM_CTL_STATUS:
        val = regs->efl_rnm_ctl_status.value;
        break;
    case POM_INT_ENA_W1S:
        val = regs->pom_int_ena_w1s.value;
        break;
    case BMO_CTL2:
        val = regs->bmo_ctl2.value;
        break;
    case FUS_DAT1:
        val = regs->fus_dat1.value;
        break;
    case RST_BOOT:
        val = regs->rst_boot.value;
        break;
    case UCD_UCODE_LOAD_BLOCK_NUM:
        val = regs->ucd_ucode_load_block_num;
        break;
    case AQM_DBELL_OVF_LO_ENA_W1S:
        val = regs->aqm_dbell_ovf_lo_ena_w1s;
        break;
    case AQM_DMA_RD_ERR_LO_ENA_W1S:
        val = regs->aqm_dma_rd_err_lo_ena_w1s;
        break;
    case AQM_DBELL_OVF_HI_ENA_W1S:
        val = regs->aqm_dbell_ovf_hi_ena_w1s;
        break;
    case AQM_EXEC_NA_LO_ENA_W1S:
        val = regs->aqm_exec_na_lo_ena_w1s;
        break;
    case AQM_EXEC_ERR_HI_ENA_W1S:
        val = regs->aqm_exec_err_hi_ena_w1s;
        break;
    case AQM_EXEC_NA_HI_ENA_W1S:
        val = regs->aqm_exec_na_hi_ena_w1s;
        break;
    case AQM_DMA_RD_ERR_HI_ENA_W1S:
        val = regs->aqm_dma_rd_err_hi_ena_w1s;
        break;
    case AQM_EXEC_ERR_LO_ENA_W1S:
        val = regs->aqm_exec_err_lo_ena_w1s;
        break;
    default:
        /* Handle indexed registers */
        if (addr >= 0x1000000 && addr < 0x1000000 + (MAX_PF_QUEUES * 0x40000)) {
            uint32_t idx = (addr - 0x1000000) / 0x40000;
            uint32_t offset = (addr - 0x1000000) % 0x40000;
            if (idx < MAX_PF_QUEUES) {
                switch (offset) {
                case 0x00: /* NPS_PKT_SLC_CTLX(_i) */
                    val = regs->nps_pkt_slc_ctl[idx].value;
                    break;
                case 0x08: /* NPS_PKT_SLC_CNTSX(_i) */
                    val = regs->nps_pkt_slc_cnts[idx].value;
                    break;
                case 0x10: /* NPS_PKT_SLC_INT_LEVELSX(_i) */
                    val = regs->nps_pkt_slc_int_levels[idx].value;
                    break;
                case 0x60: /* NPS_PKT_IN_INSTR_CTLX(_i) */
                    val = regs->nps_pkt_in_instr_ctl[idx].value;
                    break;
                case 0x68: /* NPS_PKT_IN_INSTR_BADDRX(_i) */
                    val = regs->nps_pkt_in_instr_baddr[idx].value;
                    break;
                case 0x70: /* NPS_PKT_IN_INSTR_RSIZEX(_i) */
                    val = regs->nps_pkt_in_instr_rsize[idx].value;
                    break;
                case 0x78: /* NPS_PKT_IN_INSTR_BAOFF_DBELLX(_i) */
                    val = regs->nps_pkt_in_instr_baoff_dbell[idx].value;
                    break;
                case 0x80: /* NPS_PKT_IN_DONE_CNTSX(_i) */
                    val = regs->nps_pkt_in_done_cnts[idx].value;
                    break;
                case 0x88: /* NPS_PKT_IN_INT_LEVELSX(_i) */
                    val = regs->nps_pkt_in_int_levels[idx].value;
                    break;
                default:
                    break;
                }
            }
        } else if (addr >= 0x11C1100 && addr < 0x11C1100 + (NR_CLUSTERS * 8)) {
            uint32_t idx = (addr - 0x11C1100) / 8;
            if (idx < NR_CLUSTERS) {
                val = regs->pom_grp_execmask[idx];
            }
        } else if (addr >= 0x12C0000 && addr < 0x12C0000 + (SE_MAX_CORES * 0x1000)) {
            uint32_t idx = (addr - 0x12C0000) / 0x1000;
            if (idx < SE_MAX_CORES) {
                val = regs->ucd_se_eid_ucode_block_num[idx].value;
            }
        } else if (addr >= 0x12C0008 && addr < 0x12C0008 + (AE_MAX_CORES * 0x800)) {
            uint32_t idx = (addr - 0x12C0008) / 0x800;
            if (idx < AE_MAX_CORES) {
                val = regs->ucd_ae_eid_ucode_block_num[idx].value;
            }
        } else if (addr >= 0x12C0018 && addr < 0x12C0018 + (CNN55XX_MAX_UCD_BLOCKS * 0x20)) {
            uint32_t idx = (addr - 0x12C0018) / 0x20;
            if (idx < CNN55XX_MAX_UCD_BLOCKS) {
                val = regs->ucd_ucode_load_idx_data[idx];
            }
        } else if (addr >= 0x1240018 && addr < 0x1240018 + (NR_CLUSTERS * 0x400)) {
            uint32_t idx = (addr - 0x1240018) / 0x400;
            if (idx < NR_CLUSTERS) {
                if ((addr % 0x400) == 0) {
                    val = regs->efl_core_int_ena_w1s[idx].value;
                } else if ((addr % 0x400) == 0x50) {
                    val = regs->efl_core_vf_err_int0_ena_w1s[idx].value;
                } else if ((addr % 0x400) == 0x70) {
                    val = regs->efl_core_vf_err_int1_ena_w1s[idx].value;
                }
            }
        } else if (addr >= 0x1300C00 && addr < 0x1300C00 + (AQM_GRP_COUNT * 0x10)) {
            uint32_t idx = (addr - 0x1300C00) / 0x10;
            if (idx < AQM_GRP_COUNT) {
                if ((addr % 0x10) == 0) {
                    val = regs->aqm_grp_execmsk_lo[idx].value;
                } else if ((addr % 0x10) == 8) {
                    val = regs->aqm_grp_execmsk_hi[idx].value;
                }
            }
        } else if (addr >= 0x1400000 && addr < 0x1400000 + (SE_MAX_CORES * 0x40000)) {
            uint32_t idx = (addr - 0x1400000) / 0x40000;
            if (idx < SE_MAX_CORES) {
                uint32_t off = (addr - 0x1400000) % 0x40000;
                if (off == 0) {
                    val = regs->emu_se_enable[idx].value;
                }
            }
        } else if (addr >= 0x1400008 && addr < 0x1400008 + (AE_MAX_CORES * 0x40000)) {
            uint32_t idx = (addr - 0x1400008) / 0x40000;
            if (idx < AE_MAX_CORES) {
                uint32_t off = (addr - 0x1400008) % 0x40000;
                if (off == 0) {
                    val = regs->emu_ae_enable[idx].value;
                }
            }
        } else if (addr >= 0x1402318 && addr < 0x1402318 + (AE_MAX_CORES * 0x40000)) {
            uint32_t idx = (addr - 0x1402318) / 0x40000;
            if (idx < AE_MAX_CORES) {
                val = regs->emu_wd_int_ena_w1s[idx].value;
            }
        } else if (addr >= 0x1402518 && addr < 0x1402518 + (AE_MAX_CORES * 0x40000)) {
            uint32_t idx = (addr - 0x1402518) / 0x40000;
            if (idx < AE_MAX_CORES) {
                val = regs->emu_ge_int_ena_w1s[idx].value;
            }
        } else if (addr >= 0x1402708 && addr < 0x1402708 + (AE_MAX_CORES * 0x40000)) {
            uint32_t idx = (addr - 0x1402708) / 0x40000;
            if (idx < AE_MAX_CORES) {
                val = regs->emu_fuse_map[idx].value;
            }
        } else if (addr >= 0x20000 && addr < 0x20000 + (MAX_DEV_QUEUES * 0x40000)) {
            uint32_t idx = (addr - 0x20000) / 0x40000;
            if (idx < MAX_DEV_QUEUES) {
                uint32_t off = (addr - 0x20000) % 0x40000;
                switch (off) {
                case 0x00: /* AQMQ_DRBLX(x) */
                    val = regs->aqmq_en[idx].value;
                    break;
                case 0x08: /* AQMQ_QSZX(x) */
                    val = regs->aqmq_qsz[idx].value;
                    break;
                case 0x10: /* AQMQ_BADRX(x) */
                    val = regs->aqmq_badr[idx].value;
                    break;
                case 0x18: /* AQMQ_NXT_CMDX(x) */
                    /* Not stored */
                    break;
                case 0x20: /* AQMQ_CMD_CNTX(x) */
                    /* Not stored */
                    break;
                case 0x28: /* AQMQ_CMP_THRX(x) */
                    val = regs->aqmq_cmp_thr[idx].value;
                    break;
                case 0x30: /* AQMQ_CMP_CNTX(x) */
                    val = regs->aqmq_cmp_cnt[idx].value;
                    break;
                case 0x48: /* AQMQ_ENX(x) */
                    val = regs->aqmq_en[idx].value;
                    break;
                case 0x50: /* AQMQ_ACTIVITY_STATX(x) */
                    val = regs->aqmq_activity_stat[idx].value;
                    break;
                default:
                    break;
                }
            }
        } else if (addr == NPS_CORE_BIST_REG ||
                   addr == NPS_CORE_NPC_BIST_REG ||
                   addr == NPS_PKT_SLC_BIST_REG ||
                   addr == NPS_PKT_IN_BIST_REG ||
                   addr == POM_BIST_REG ||
                   addr == BMI_BIST_REG ||
                   addr == EFL_TOP_BIST_STAT ||
                   addr == BMO_BIST_REG ||
                   addr == LBC_BIST_STATUS ||
                   addr == UCD_BIST_STATUS ||
                   addr == PEM_BIST_STATUSX(0) ||
                   (addr >= 0x1402700 && addr < 0x1402700 + (NR_CLUSTERS * 0x40000) &&
                    ((addr - 0x1402700) % 0x40000 == 0)) || /* EMU_BIST_STATUSX */
                   (addr >= 0x1240100 && addr < 0x1240100 + (NR_CLUSTERS * 0x400) &&
                    ((addr - 0x1240100) % 0x400 == 0))) {  /* EFL_CORE_BIST_REGX */
            /* BIST registers return 0 */
            val = 0;
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    NITROXRegs *regs = &s->regs;

    if (size != 8) {
        return;
    }

    switch (addr) {
    case NPS_CORE_CONTROL:
        /* Not stored */
        break;
    case NPS_CORE_GBL_VFCFG:
        regs->nps_core_gbl_vfcfg.value = val;
        break;
    case NPS_CORE_INT_ENA_W1S:
        regs->nps_core_int_ena_w1s.value = val;
        break;
    case NPS_PKT_IN_RERR_LO_ENA_W1S:
        regs->nps_pkt_in_rerr_lo_ena_w1s = val;
        break;
    case NPS_PKT_IN_RERR_HI_ENA_W1S:
        regs->nps_pkt_in_rerr_hi_ena_w1s = val;
        break;
    case NPS_PKT_IN_ERR_TYPE_ENA_W1S:
        regs->nps_pkt_in_err_type_ena_w1s = val;
        break;
    case NPS_PKT_SLC_RERR_HI_ENA_W1S:
        regs->nps_pkt_slc_rerr_hi_ena_w1s = val;
        break;
    case NPS_PKT_SLC_ERR_TYPE_ENA_W1S:
        regs->nps_pkt_slc_err_type_ena_w1s = val;
        break;
    case NPS_PKT_SLC_RERR_LO_ENA_W1S:
        regs->nps_pkt_slc_rerr_lo_ena_w1s = val;
        break;
    case NPS_PKT_MBOX_INT_HI_ENA_W1S:
        regs->nps_pkt_mbox_int_hi_ena_w1s = val;
        break;
    case NPS_PKT_MBOX_INT_LO_ENA_W1S:
        regs->nps_pkt_mbox_int_lo_ena_w1s = val;
        break;
    case NPS_PKT_MBOX_INT_LO_ENA_W1C:
        regs->nps_pkt_mbox_int_lo_ena_w1c = val;
        break;
    case NPS_PKT_MBOX_INT_HI_ENA_W1C:
        regs->nps_pkt_mbox_int_hi_ena_w1c = val;
        break;
    case BMI_INT_ENA_W1S:
        regs->bmi_int_ena_w1s.value = val;
        break;
    case BMI_CTL:
        regs->bmi_ctl.value = val;
        break;
    case LBC_ELM_VF65_128_INT_ENA_W1S:
        regs->lbc_elm_vf65_128_int_ena_w1s.value = val;
        break;
    case LBC_PLM_VF1_64_INT_ENA_W1S:
        regs->lbc_plm_vf1_64_int_ena_w1s.value = val;
        break;
    case LBC_PLM_VF65_128_INT_ENA_W1S:
        regs->lbc_plm_vf65_128_int_ena_w1s.value = val;
        break;
    case LBC_ELM_VF1_64_INT_ENA_W1S:
        regs->lbc_elm_vf1_64_int_ena_w1s.value = val;
        break;
    case LBC_INT_ENA_W1S:
        regs->lbc_int_ena_w1s.value = val;
        break;
    case LBC_INVAL_STATUS:
        regs->lbc_inval_status.value = val;
        break;
    case LBC_INVAL_CTL:
        regs->lbc_inval_ctl.value = val;
        break;
    case EFL_RNM_CTL_STATUS:
        regs->efl_rnm_ctl_status.value = val;
        break;
    case POM_INT_ENA_W1S:
        regs->pom_int_ena_w1s.value = val;
        break;
    case BMO_CTL2:
        regs->bmo_ctl2.value = val;
        break;
    case FUS_DAT1:
        regs->fus_dat1.value = val;
        break;
    case RST_BOOT:
        regs->rst_boot.value = val;
        break;
    case UCD_UCODE_LOAD_BLOCK_NUM:
        regs->ucd_ucode_load_block_num = val;
        break;
    case AQM_DBELL_OVF_LO_ENA_W1S:
        regs->aqm_dbell_ovf_lo_ena_w1s = val;
        break;
    case AQM_DMA_RD_ERR_LO_ENA_W1S:
        regs->aqm_dma_rd_err_lo_ena_w1s = val;
        break;
    case AQM_DBELL_OVF_HI_ENA_W1S:
        regs->aqm_dbell_ovf_hi_ena_w1s = val;
        break;
    case AQM_EXEC_NA_LO_ENA_W1S:
        regs->aqm_exec_na_lo_ena_w1s = val;
        break;
    case AQM_EXEC_ERR_HI_ENA_W1S:
        regs->aqm_exec_err_hi_ena_w1s = val;
        break;
    case AQM_EXEC_NA_HI_ENA_W1S:
        regs->aqm_exec_na_hi_ena_w1s = val;
        break;
    case AQM_DMA_RD_ERR_HI_ENA_W1S:
        regs->aqm_dma_rd_err_hi_ena_w1s = val;
        break;
    case AQM_EXEC_ERR_LO_ENA_W1S:
        regs->aqm_exec_err_lo_ena_w1s = val;
        break;
    default:
        /* Handle indexed registers */
        if (addr >= 0x1000000 && addr < 0x1000000 + (MAX_PF_QUEUES * 0x40000)) {
            uint32_t idx = (addr - 0x1000000) / 0x40000;
            uint32_t offset = (addr - 0x1000000) % 0x40000;
            if (idx < MAX_PF_QUEUES) {
                switch (offset) {
                case 0x00:
                    regs->nps_pkt_slc_ctl[idx].value = val;
                    break;
                case 0x08:
                    regs->nps_pkt_slc_cnts[idx].value = val;
                    break;
                case 0x10:
                    regs->nps_pkt_slc_int_levels[idx].value = val;
                    break;
                case 0x60:
                    regs->nps_pkt_in_instr_ctl[idx].value = val;
                    break;
                case 0x68:
                    regs->nps_pkt_in_instr_baddr[idx].value = val;
                    break;
                case 0x70:
                    regs->nps_pkt_in_instr_rsize[idx].value = val;
                    break;
                case 0x78:
                    regs->nps_pkt_in_instr_baoff_dbell[idx].value = val;
                    break;
                case 0x80:
                    regs->nps_pkt_in_done_cnts[idx].value = val;
                    break;
                case 0x88:
                    regs->nps_pkt_in_int_levels[idx].value = val;
                    break;
                default:
                    break;
                }
            }
        } else if (addr >= 0x11C1100 && addr < 0x11C1100 + (NR_CLUSTERS * 8)) {
            uint32_t idx = (addr - 0x11C1100) / 8;
            if (idx < NR_CLUSTERS) {
                regs->pom_grp_execmask[idx] = val;
            }
        } else if (addr >= 0x12C0000 && addr < 0x12C0000 + (SE_MAX_CORES * 0x1000)) {
            uint32_t idx = (addr - 0x12C0000) / 0x1000;
            if (idx < SE_MAX_CORES) {
                regs->ucd_se_eid_ucode_block_num[idx].value = val;
            }
        } else if (addr >= 0x12C0008 && addr < 0x12C0008 + (AE_MAX_CORES * 0x800)) {
            uint32_t idx = (addr - 0x12C0008) / 0x800;
            if (idx < AE_MAX_CORES) {
                regs->ucd_ae_eid_ucode_block_num[idx].value = val;
            }
        } else if (addr >= 0x12C0018 && addr < 0x12C0018 + (CNN55XX_MAX_UCD_BLOCKS * 0x20)) {
            uint32_t idx = (addr - 0x12C0018) / 0x20;
            if (idx < CNN55XX_MAX_UCD_BLOCKS) {
                regs->ucd_ucode_load_idx_data[idx] = val;
            }
        } else if (addr >= 0x1240018 && addr < 0x1240018 + (NR_CLUSTERS * 0x400)) {
            uint32_t idx = (addr - 0x1240018) / 0x400;
            if (idx < NR_CLUSTERS) {
                uint32_t off = (addr - 0x1240018) % 0x400;
                if (off == 0) {
                    regs->efl_core_int_ena_w1s[idx].value = val;
                } else if (off == 0x50) {
                    regs->efl_core_vf_err_int0_ena_w1s[idx].value = val;
                } else if (off == 0x70) {
                    regs->efl_core_vf_err_int1_ena_w1s[idx].value = val;
                }
            }
        } else if (addr >= 0x1300C00 && addr < 0x1300C00 + (AQM_GRP_COUNT * 0x10)) {
            uint32_t idx = (addr - 0x1300C00) / 0x10;
            if (idx < AQM_GRP_COUNT) {
                uint32_t off = (addr - 0x1300C00) % 0x10;
                if (off == 0) {
                    regs->aqm_grp_execmsk_lo[idx].value = val;
                } else if (off == 8) {
                    regs->aqm_grp_execmsk_hi[idx].value = val;
                }
            }
        } else if (addr >= 0x1400000 && addr < 0x1400000 + (SE_MAX_CORES * 0x40000)) {
            uint32_t idx = (addr - 0x1400000) / 0x40000;
            if (idx < SE_MAX_CORES) {
                uint32_t off = (addr - 0x1400000) % 0x40000;
                if (off == 0) {
                    regs->emu_se_enable[idx].value = val;
                }
            }
        } else if (addr >= 0x1400008 && addr < 0x1400008 + (AE_MAX_CORES * 0x40000)) {
            uint32_t idx = (addr - 0x1400008) / 0x40000;
            if (idx < AE_MAX_CORES) {
                uint32_t off = (addr - 0x1400008) % 0x40000;
                if (off == 0) {
                    regs->emu_ae_enable[idx].value = val;
                }
            }
        } else if (addr >= 0x1402318 && addr < 0x1402318 + (AE_MAX_CORES * 0x40000)) {
            uint32_t idx = (addr - 0x1402318) / 0x40000;
            if (idx < AE_MAX_CORES) {
                regs->emu_wd_int_ena_w1s[idx].value = val;
            }
        } else if (addr >= 0x1402518 && addr < 0x1402518 + (AE_MAX_CORES * 0x40000)) {
            uint32_t idx = (addr - 0x1402518) / 0x40000;
            if (idx < AE_MAX_CORES) {
                regs->emu_ge_int_ena_w1s[idx].value = val;
            }
        } else if (addr >= 0x1402708 && addr < 0x1402708 + (AE_MAX_CORES * 0x40000)) {
            uint32_t idx = (addr - 0x1402708) / 0x40000;
            if (idx < AE_MAX_CORES) {
                regs->emu_fuse_map[idx].value = val;
            }
        } else if (addr >= 0x20000 && addr < 0x20000 + (MAX_DEV_QUEUES * 0x40000)) {
            uint32_t idx = (addr - 0x20000) / 0x40000;
            if (idx < MAX_DEV_QUEUES) {
                uint32_t off = (addr - 0x20000) % 0x40000;
                switch (off) {
                case 0x00:
                    regs->aqmq_en[idx].value = val;
                    break;
                case 0x08:
                    regs->aqmq_qsz[idx].value = val;
                    break;
                case 0x10:
                    regs->aqmq_badr[idx].value = val;
                    break;
                case 0x18:
                    /* Not stored */
                    break;
                case 0x20:
                    /* Not stored */
                    break;
                case 0x28:
                    regs->aqmq_cmp_thr[idx].value = val;
                    break;
                case 0x30:
                    regs->aqmq_cmp_cnt[idx].value = val;
                    break;
                case 0x48:
                    regs->aqmq_en[idx].value = val;
                    break;
                case 0x50:
                    regs->aqmq_activity_stat[idx].value = val;
                    break;
                default:
                    break;
                }
            }
        } else if (addr == NPS_CORE_BIST_REG ||
                   addr == NPS_CORE_NPC_BIST_REG ||
                   addr == NPS_PKT_SLC_BIST_REG ||
                   addr == NPS_PKT_IN_BIST_REG ||
                   addr == POM_BIST_REG ||
                   addr == BMI_BIST_REG ||
                   addr == EFL_TOP_BIST_STAT ||
                   addr == BMO_BIST_REG ||
                   addr == LBC_BIST_STATUS ||
                   addr == UCD_BIST_STATUS ||
                   addr == PEM_BIST_STATUSX(0) ||
                   (addr >= 0x1402700 && addr < 0x1402700 + (NR_CLUSTERS * 0x40000) &&
                    ((addr - 0x1402700) % 0x40000 == 0)) ||
                   (addr >= 0x1240100 && addr < 0x1240100 + (NR_CLUSTERS * 0x400) &&
                    ((addr - 0x1240100) % 0x400 == 0))) {
            /* BIST registers: ignore writes */
        }
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

    /* Reset register values */
    memset(&s->regs, 0, sizeof(s->regs));
    /* Set default identification values */
    s->regs.fus_dat1.s.chip_id = 1;    /* Assign a valid chip_id */
    s->regs.fus_dat1.s.nozip = 0;
    s->regs.fus_dat1.s.bar2_sz_conf = 0;
    s->regs.fus_dat1.s.pll_bwadj_denom = 0;
    s->regs.fus_dat1.s.pll_alt_matrix = 0;
    s->regs.fus_dat1.s.efus_ign = 0;
    s->regs.fus_dat1.s.zip_info = 0;
    s->regs.fus_dat1.s.efus_lck = 0;
    s->regs.fus_dat1.s.pll_half_dis = 0;
    s->regs.fus_dat1.s.pll_mul = 0;
    /* Other regs remain zero as reset */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status = 0;
    s->reset_active = false;
    s->pm_state = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, NITROX_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, NITROX_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, (NITROX_PCI_CLASS >> 8) & 0xFFFF);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR0 Configuration: container with MMIO region and MSI-X overlay */
    memory_region_init(&s->bar0_container, OBJECT(s), "bar0-container", BAR0_SIZE);
    memory_region_init_io(&s->bar0_mmio, OBJECT(s), &pcibase_mmio_ops, s, "cnn55xx-mmio", BAR0_SIZE);
    memory_region_add_subregion(&s->bar0_container, 0, &s->bar0_mmio);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0_container);

    /* MSI-X initialization: table at offset 0x3FE0000, PBA at 0x3FF0000, within BAR0 */
    s->has_msix = false;
    if (msix_init(pdev, 256, &s->bar0_container, 0, 0x3FE0000,
                  &s->bar0_container, 0, 0x3FF0000, 0x60, errp) < 0) {
        return;
    }
    s->has_msix = true;

    /* Final state initialization */
    s->status = 0;
    s->reset_active = false;
    s->pm_state = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar0_container, &s->bar0_container);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "CNN55XX_pci",
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
