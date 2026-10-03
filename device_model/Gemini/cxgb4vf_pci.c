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


#define TYPE_PCIBASE_DEVICE "cxgb4vf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define T4VF_SGE_BASE_ADDR	0x0000
#define T4VF_MPS_BASE_ADDR	0x0100
#define T4VF_PL_BASE_ADDR	0x0200
#define T4VF_MBDATA_BASE_ADDR	0x0240
#define T6VF_MBDATA_BASE_ADDR	0x0280
#define T4VF_CIM_BASE_ADDR	0x0300
#define T4VF_REGMAP_START	0x0000
#define T4VF_REGMAP_SIZE	0x0400

#define PL_VF_WHOAMI_A 0x0
#define PL_VF_REVISION_A 0x8
#define SGE_CONTROL_A	0x1008
#define SGE_HOST_PAGE_SIZE_A 0x100c
#define SGE_EGRESS_QUEUES_PER_PAGE_VF_A 0x1014
#define SGE_FL_BUFFER_SIZE0_A 0x1044
#define SGE_FL_BUFFER_SIZE1_A 0x1048
#define SGE_CONM_CTRL_A 0x1094
#define SGE_INGRESS_RX_THRESHOLD_A 0x10a0
#define SGE_TIMER_VALUE_0_AND_1_A 0x10b8
#define SGE_TIMER_VALUE_2_AND_3_A 0x10bc
#define SGE_TIMER_VALUE_4_AND_5_A 0x10c0
#define SGE_INGRESS_QUEUES_PER_PAGE_VF_A 0x10f8
#define SGE_CONTROL2_A	0x1124

#define CHELSIO_PCI_ID_VER(__DeviceID)  ((__DeviceID) >> 12)
#define CHELSIO_T4		0x4
#define CHELSIO_T5		0x5
#define CHELSIO_T6		0x6
#define PL_VF_REV_A 0x4

#define MBOX_OWNER_NONE			0x00
#define FW_CMD_MAX_TIMEOUT 10000
#define FW_RSS_GLB_CONFIG_CMD_MODE_BASICVIRTUAL	1
#define RXPKTCPLMODE_SPLIT_X		1
#define FW_VI_RXMODE_CMD_MTU_M		0xffff
#define FW_VI_RXMODE_CMD_PROMISCEN_M	0x3
#define FW_VI_RXMODE_CMD_ALLMULTIEN_M		0x3
#define FW_VI_RXMODE_CMD_BROADCASTEN_M		0x3
#define FW_VI_RXMODE_CMD_VLANEXEN_M	0x3
#define FW_VI_MAC_ADD_PERSIST_MAC	0x3FE
#define FW_VI_MAC_ADD_MAC		0x3FF
#define FETCHBURSTMIN_128B_X		3
#define FETCHBURSTMIN_64B_T6_X		0
#define FETCHBURSTMAX_512B_X		3
#define FETCHBURSTMAX_256B_X		2
#define FETCHBURSTMIN_64B_X		2
#define CIDXFLUSHTHRESH_32_X		5
#define FW_VI_MAC_MAC_BASED_FREE	0x3FD
#define FW_RESET_CMD                 0xDF
#define NUM_CIM_VF_MAILBOX_DATA_INSTANCES 16

#define MBOWNER_S    0
#define MBOWNER_M    0x3U
#define MBMSGVALID_V(x) ((x) << MBMSGVALID_S)
#define FW_CMD_OP_S             24
#define FW_CMD_OP_M             0xff
#define FW_CMD_REQUEST_V(x)     ((x) << FW_CMD_REQUEST_S)
#define FW_CMD_RETVAL_S         8
#define FW_CMD_RETVAL_M         0xff
#define REV_S    0
#define REV_M    0xfU
#define FW_PARAMS_MNEM_S	24
#define FW_PARAMS_PARAM_X_S     16
#define FW_PARAMS_PARAM_XYZ_S		0
#define FW_RSS_GLB_CONFIG_CMD_MODE_S	28
#define FW_RSS_GLB_CONFIG_CMD_MODE_M	0xf
#define FW_RSS_GLB_CONFIG_CMD_SYNMAPEN_V(x)	\
	((x) << FW_RSS_GLB_CONFIG_CMD_SYNMAPEN_S)
#define FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6_V(x)	\
	((x) << FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV6_S)
#define FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6_V(x)	\
	((x) << FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV6_S)
#define FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4_V(x)	\
	((x) << FW_RSS_GLB_CONFIG_CMD_SYN4TUPENIPV4_S)
#define FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4_V(x)	\
	((x) << FW_RSS_GLB_CONFIG_CMD_SYN2TUPENIPV4_S)
#define FW_RSS_GLB_CONFIG_CMD_OFDMAPEN_V(x)	\
	((x) << FW_RSS_GLB_CONFIG_CMD_OFDMAPEN_S)
#define FW_RSS_GLB_CONFIG_CMD_TNLMAPEN_V(x)	\
	((x) << FW_RSS_GLB_CONFIG_CMD_TNLMAPEN_S)
#define FW_RSS_GLB_CONFIG_CMD_TNLALLLKP_V(x)	\
	((x) << FW_RSS_GLB_CONFIG_CMD_TNLALLLKP_S)
#define FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ_V(x)	\
	((x) << FW_RSS_GLB_CONFIG_CMD_HASHTOEPLITZ_S)
#define RXPKTCPLMODE_S    18
#define EGRSTATUSPAGESIZE_V(x) ((x) << EGRSTATUSPAGESIZE_S)
#define PKTSHIFT_S    10
#define PKTSHIFT_M    0x7U
#define EGRTHRESHOLD_S    8
#define EGRTHRESHOLD_M    0x3fU
#define EGRTHRESHOLDPACKING_S    14
#define EGRTHRESHOLDPACKING_M    0x3fU
#define T6_EGRTHRESHOLDPACKING_S    16
#define T6_EGRTHRESHOLDPACKING_M    0xffU
#define FW_PFVF_CMD_NIQFLINT_S          20
#define FW_PFVF_CMD_NIQFLINT_M          0xfff
#define FW_PFVF_CMD_NIQ_S       0
#define FW_PFVF_CMD_NIQ_M       0xfffff
#define FW_PFVF_CMD_NEQ_S       0
#define FW_PFVF_CMD_NEQ_M       0xfffff
#define FW_PFVF_CMD_PMASK_S     20
#define FW_PFVF_CMD_PMASK_M	0xf
#define FW_PFVF_CMD_TC_S        24
#define FW_PFVF_CMD_TC_M        0xff
#define FW_PFVF_CMD_NVI_S       16
#define FW_PFVF_CMD_NVI_M       0xff
#define FW_PFVF_CMD_NEXACTF_S           0
#define FW_PFVF_CMD_NEXACTF_M           0xffff
#define FW_PFVF_CMD_R_CAPS_S    24
#define FW_PFVF_CMD_R_CAPS_M    0xff
#define FW_PFVF_CMD_WX_CAPS_S           16
#define FW_PFVF_CMD_WX_CAPS_M           0xff
#define FW_PFVF_CMD_NETHCTRL_S          0
#define FW_PFVF_CMD_NETHCTRL_M          0xffff
#define FW_VI_CMD_VIID_S	0
#define FW_VI_CMD_VIID_M	0xfff
#define FW_VI_CMD_PORTID_S	4
#define FW_VI_CMD_PORTID_M	0xf
#define FW_VI_CMD_RSSSIZE_S	0
#define FW_VI_CMD_RSSSIZE_M	0x7ff
#define FW_PORT_CMD_PORTID_S	0
#define FW_PORT_CMD_PORTID_M	0xf
#define FW_PORT_CMD_ACTION_S	16
#define FW_PORT_CMD_ACTION_M	0xffff
#define FW_PORT_CMD_PTYPE_S	8
#define FW_PORT_CMD_PTYPE_M	0x1f
#define FW_PORT_CMD_MDIOCAP_V(x)	((x) << FW_PORT_CMD_MDIOCAP_S)
#define FW_PORT_CMD_MDIOADDR_S		16
#define FW_PORT_CMD_MDIOADDR_M		0x1f
#define FW_PORT_CMD_PORTTYPE32_S	13
#define FW_PORT_CMD_PORTTYPE32_M	0xff
#define FW_PORT_CMD_MDIOCAP32_V(x)	((x) << FW_PORT_CMD_MDIOCAP32_S)
#define FW_PORT_CMD_MDIOADDR32_S	21
#define FW_PORT_CMD_MDIOADDR32_M	0x1f
#define FW_VI_CMD_ALLOC_V(x)	((x) << FW_VI_CMD_ALLOC_S)
#define FW_VI_RXMODE_CMD_VIID_S		0
#define FW_VI_RXMODE_CMD_MTU_S		16
#define FW_VI_RXMODE_CMD_PROMISCEN_S	14
#define FW_VI_RXMODE_CMD_ALLMULTIEN_S		12
#define FW_VI_RXMODE_CMD_BROADCASTEN_S		10
#define FW_VI_RXMODE_CMD_VLANEXEN_S	8
#define FW_VI_MAC_CMD_VIID_S	0
#define FW_CMD_LEN16_S          0
#define FW_VI_MAC_CMD_VALID_V(x)	((x) << FW_VI_MAC_CMD_VALID_S)
#define FW_VI_MAC_CMD_IDX_S	0
#define FW_VI_MAC_CMD_IDX_M	0x3ff
#define FW_IQ_CMD_ALLOC_V(x)	((x) << FW_IQ_CMD_ALLOC_S)
#define FW_IQ_CMD_IQSTART_V(x)	((x) << FW_IQ_CMD_IQSTART_S)
#define FW_IQ_CMD_TYPE_S	29
#define FW_IQ_CMD_IQASYNCH_S	28
#define FW_IQ_CMD_VIID_S	16
#define FW_IQ_CMD_IQANDST_S	15
#define FW_IQ_CMD_IQANUS_S	14
#define FW_IQ_CMD_IQANUD_S	12
#define FW_IQ_CMD_IQANDSTINDEX_S	0
#define FW_IQ_CMD_IQPCIECH_S	12
#define FW_IQ_CMD_IQGTSMODE_V(x)	((x) << FW_IQ_CMD_IQGTSMODE_S)
#define FW_IQ_CMD_IQINTCNTTHRESH_S	4
#define FW_IQ_CMD_IQESIZE_S	0
#define FW_IQ_CMD_FL0HOSTFCMODE_S	4
#define FW_IQ_CMD_FL0PACKEN_V(x)	((x) << FW_IQ_CMD_FL0PACKEN_S)
#define FW_IQ_CMD_FL0FETCHRO_S		6
#define FW_IQ_CMD_FL0DATARO_S		12
#define FW_IQ_CMD_FL0PADEN_V(x)	((x) << FW_IQ_CMD_FL0PADEN_S)
#define FW_IQ_CMD_FL0FBMIN_S	7
#define FW_IQ_CMD_FL0FBMAX_S	4
#define FW_EQ_ETH_CMD_ALLOC_V(x)	((x) << FW_EQ_ETH_CMD_ALLOC_S)
#define FW_EQ_ETH_CMD_EQSTART_V(x)	((x) << FW_EQ_ETH_CMD_EQSTART_S)
#define FW_EQ_ETH_CMD_AUTOEQUEQE_V(x)	((x) << FW_EQ_ETH_CMD_AUTOEQUEQE_S)
#define FW_EQ_ETH_CMD_VIID_S	16
#define FW_EQ_ETH_CMD_HOSTFCMODE_S	20
#define FW_EQ_ETH_CMD_PCIECHN_S		16
#define FW_EQ_ETH_CMD_IQID_S	0
#define FW_EQ_ETH_CMD_FBMIN_S		23
#define FW_EQ_ETH_CMD_FBMAX_S		20
#define FW_EQ_ETH_CMD_CIDXFTHRESH_S	16
#define FW_EQ_ETH_CMD_EQSIZE_S		0
#define FW_EQ_ETH_CMD_EQID_S	0
#define FW_EQ_ETH_CMD_EQID_M	0xfffff
#define FW_EQ_ETH_CMD_PHYSEQID_S	0
#define FW_EQ_ETH_CMD_PHYSEQID_M	0xfffff
#define FW_VI_ENABLE_CMD_VIID_S         0
#define FW_VI_ENABLE_CMD_LED_V(x)	((x) << FW_VI_ENABLE_CMD_LED_S)
#define FW_VI_STATS_CMD_VIID_S		0
#define FW_VI_STATS_CMD_IX_S	0
#define FW_VI_STATS_CMD_NSTATS_S	12
#define FW_VI_MAC_CMD_FREEMACS_S	31
#define FW_VI_MAC_CMD_HASHVECEN_V(x)	((x) << FW_VI_MAC_CMD_HASHVECEN_S)
#define FW_VI_MAC_CMD_HASHUNIEN_S	22
#define FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN_V(x)	\
	((x) << FW_RSS_VI_CONFIG_CMD_IP6FOURTUPEN_S)
#define FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN_V(x)	\
	((x) << FW_RSS_VI_CONFIG_CMD_IP6TWOTUPEN_S)
#define FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN_V(x)	\
	((x) << FW_RSS_VI_CONFIG_CMD_IP4FOURTUPEN_S)
#define FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN_V(x)	\
	((x) << FW_RSS_VI_CONFIG_CMD_IP4TWOTUPEN_S)
#define FW_RSS_VI_CONFIG_CMD_UDPEN_V(x)	((x) << FW_RSS_VI_CONFIG_CMD_UDPEN_S)
#define FW_RSS_VI_CONFIG_CMD_DEFAULTQ_S		16
#define FW_RSS_VI_CONFIG_CMD_DEFAULTQ_M		0x3ff
#define FW_RSS_IND_TBL_CMD_VIID_S	0
#define FW_RSS_IND_TBL_CMD_IQ0_S	20
#define FW_RSS_IND_TBL_CMD_IQ1_S	10
#define FW_RSS_IND_TBL_CMD_IQ2_S	0
#define FW_CMD_READ_V(x)        ((x) << FW_CMD_READ_S)
#define FW_CMD_WRITE_V(x)       ((x) << FW_CMD_WRITE_S)
#define FW_CMD_EXEC_V(x)        ((x) << FW_CMD_EXEC_S)

struct fw_vi_cmd {
	uint32_t op_to_vfn;
	uint32_t alloc_to_len16;
	uint16_t type_viid;
	uint8_t mac[6];
	uint8_t portid_pkd;
	uint8_t nmac;
	uint8_t nmac0[6];
	uint16_t rsssize_pkd;
	uint8_t nmac1[6];
	uint16_t idsiiq_pkd;
	uint8_t nmac2[6];
	uint16_t idseiq_pkd;
	uint8_t nmac3[6];
	uint64_t r9;
	uint64_t r10;
};

struct fw_port_cmd {
	uint32_t op_to_portid;
	uint32_t action_to_len16;
	union fw_port {
		struct fw_port_l1cfg {
			uint32_t rcap;
			uint32_t r;
		} l1cfg;
		struct fw_port_l2cfg {
			uint8_t   ctlbf;
			uint8_t   ovlan3_to_ivlan0;
			uint16_t ivlantype;
			uint16_t txipg_force_pinfo;
			uint16_t mtu;
			uint16_t ovlan0mask;
			uint16_t ovlan0type;
			uint16_t ovlan1mask;
			uint16_t ovlan1type;
			uint16_t ovlan2mask;
			uint16_t ovlan2type;
			uint16_t ovlan3mask;
			uint16_t ovlan3type;
		} l2cfg;
		struct fw_port_info {
			uint32_t lstatus_to_modtype;
			uint16_t pcap;
			uint16_t acap;
			uint16_t mtu;
			uint8_t   cbllen;
			uint8_t   auxlinfo;
			uint8_t   dcbxdis_pkd;
			uint8_t   r8_lo;
			uint16_t lpacap;
			uint64_t r9;
		} info;
		struct fw_port_diags {
			uint8_t   diagop;
			uint8_t   r[3];
			uint32_t diagval;
		} diags;
		union fw_port_dcb {
			struct fw_port_dcb_pgid {
				uint8_t   type;
				uint8_t   apply_pkd;
				uint8_t   r10_lo[2];
				uint32_t pgid;
				uint64_t r11;
			} pgid;
			struct fw_port_dcb_pgrate {
				uint8_t   type;
				uint8_t   apply_pkd;
				uint8_t   r10_lo[5];
				uint8_t   num_tcs_supported;
				uint8_t   pgrate[8];
				uint8_t   tsa[8];
			} pgrate;
			struct fw_port_dcb_priorate {
				uint8_t   type;
				uint8_t   apply_pkd;
				uint8_t   r10_lo[6];
				uint8_t   strict_priorate[8];
			} priorate;
			struct fw_port_dcb_pfc {
				uint8_t   type;
				uint8_t   pfcen;
				uint8_t   r10[5];
				uint8_t   max_pfc_tcs;
				uint64_t r11;
			} pfc;
			struct fw_port_app_priority {
				uint8_t   type;
				uint8_t   r10[2];
				uint8_t   idx;
				uint8_t   user_prio_map;
				uint8_t   sel_field;
				uint16_t protocolid;
				uint64_t r12;
			} app_priority;
			struct fw_port_dcb_control {
				uint8_t   type;
				uint8_t   all_syncd_pkd;
				uint16_t dcb_version_to_app_state;
				uint32_t r11;
				uint64_t r12;
			} control;
		} dcb;
		struct fw_port_l1cfg32 {
			uint32_t rcap32;
			uint32_t r;
		} l1cfg32;
		struct fw_port_info32 {
			uint32_t lstatus32_to_cbllen32;
			uint32_t auxlinfo32_mtu32;
			uint32_t linkattr32;
			uint32_t pcaps32;
			uint32_t acaps32;
			uint32_t lpacaps32;
		} info32;
	} u;
};

struct fw_cmd_hdr {
	uint32_t hi;
	uint32_t lo;
};

struct fw_rss_glb_config_cmd {
	uint32_t op_to_write;
	uint32_t retval_len16;
	union fw_rss_glb_config {
		struct fw_rss_glb_config_manual {
			uint32_t mode_pkd;
			uint32_t r3;
			uint64_t r4;
			uint64_t r5;
		} manual;
		struct fw_rss_glb_config_basicvirtual {
			uint32_t mode_pkd;
			uint32_t synmapen_to_hashtoeplitz;
			uint64_t r8;
			uint64_t r9;
		} basicvirtual;
	} u;
};

struct fw_pfvf_cmd {
	uint32_t op_to_vfn;
	uint32_t retval_len16;
	uint32_t niqflint_niq;
	uint32_t type_to_neq;
	uint32_t tc_to_nexactf;
	uint32_t r_caps_to_nethctrl;
	uint16_t nricq;
	uint16_t nriqp;
	uint32_t r4;
};

struct fw_acl_mac_cmd {
	uint32_t op_to_vfn;
	uint32_t en_to_len16;
	uint8_t nmac;
	uint8_t r3[7];
	uint16_t r4;
	uint8_t macaddr0[6];
	uint16_t r5;
	uint8_t macaddr1[6];
	uint16_t r6;
	uint8_t macaddr2[6];
	uint16_t r7;
	uint8_t macaddr3[6];
};

struct fw_vi_rxmode_cmd {
	uint32_t op_to_viid;
	uint32_t retval_len16;
	uint32_t mtu_to_vlanexen;
	uint32_t r4_lo;
};

struct fw_vi_mac_cmd {
	uint32_t op_to_viid;
	uint32_t freemacs_to_len16;
	union fw_vi_mac {
		struct fw_vi_mac_exact {
			uint16_t valid_to_idx;
			uint8_t macaddr[6];
		} exact[7];
		struct fw_vi_mac_hash {
			uint64_t hashvec;
		} hash;
		struct fw_vi_mac_raw {
			uint32_t raw_idx_pkd;
			uint32_t data0_pkd;
			uint32_t data1[2];
			uint64_t data0m_pkd;
			uint32_t data1m[2];
		} raw;
		struct fw_vi_mac_vni {
			uint16_t valid_to_idx;
			uint8_t macaddr[6];
			uint16_t r7;
			uint8_t macaddr_mask[6];
			uint32_t lookup_type_to_vni;
			uint32_t vni_mask_pkd;
		} exact_vni[2];
	} u;
};

struct fw_iq_cmd {
	uint32_t op_to_vfn;
	uint32_t alloc_to_len16;
	uint16_t physiqid;
	uint16_t iqid;
	uint16_t fl0id;
	uint16_t fl1id;
	uint32_t type_to_iqandstindex;
	uint16_t iqdroprss_to_iqesize;
	uint16_t iqsize;
	uint64_t iqaddr;
	uint32_t iqns_to_fl0congen;
	uint16_t fl0dcaen_to_fl0cidxfthresh;
	uint16_t fl0size;
	uint64_t fl0addr;
	uint32_t fl1cngchmap_to_fl1congen;
	uint16_t fl1dcaen_to_fl1cidxfthresh;
	uint16_t fl1size;
	uint64_t fl1addr;
};

struct fw_eq_eth_cmd {
	uint32_t op_to_vfn;
	uint32_t alloc_to_len16;
	uint32_t eqid_pkd;
	uint32_t physeqid_pkd;
	uint32_t fetchszm_to_iqid;
	uint32_t dcaen_to_eqsize;
	uint64_t eqaddr;
	uint32_t autoequiqe_to_viid;
	uint32_t timeren_timerix;
	uint64_t r9;
};

struct fw_reset_cmd {
	uint32_t op_to_write;
	uint32_t retval_len16;
	uint32_t val;
	uint32_t halt_pkd;
};

struct fw_params_cmd {
	uint32_t op_to_vfn;
	uint32_t retval_len16;
	struct fw_params_param {
		uint32_t mnem;
		uint32_t val;
	} param[7];
};

struct fw_vi_enable_cmd {
	uint32_t op_to_viid;
	uint32_t ien_to_len16;
	uint16_t blinkdur;
	uint16_t r3;
	uint32_t r4;
};

struct fw_vi_stats_cmd {
	uint32_t op_to_viid;
	uint32_t retval_len16;
	union fw_vi_stats {
		struct fw_vi_stats_ctl {
			uint16_t nstats_ix;
			uint16_t r6;
			uint32_t r7;
			uint64_t stat0;
			uint64_t stat1;
			uint64_t stat2;
			uint64_t stat3;
			uint64_t stat4;
			uint64_t stat5;
		} ctl;
		struct fw_vi_stats_pf {
			uint64_t tx_bcast_bytes;
			uint64_t tx_bcast_frames;
			uint64_t tx_mcast_bytes;
			uint64_t tx_mcast_frames;
			uint64_t tx_ucast_bytes;
			uint64_t tx_ucast_frames;
			uint64_t tx_offload_bytes;
			uint64_t tx_offload_frames;
			uint64_t rx_pf_bytes;
			uint64_t rx_pf_frames;
			uint64_t rx_bcast_bytes;
			uint64_t rx_bcast_frames;
			uint64_t rx_mcast_bytes;
			uint64_t rx_mcast_frames;
			uint64_t rx_ucast_bytes;
			uint64_t rx_ucast_frames;
			uint64_t rx_err_frames;
		} pf;
		struct fw_vi_stats_vf {
			uint64_t tx_bcast_bytes;
			uint64_t tx_bcast_frames;
			uint64_t tx_mcast_bytes;
			uint64_t tx_mcast_frames;
			uint64_t tx_ucast_bytes;
			uint64_t tx_ucast_frames;
			uint64_t tx_drop_frames;
			uint64_t tx_offload_bytes;
			uint64_t tx_offload_frames;
			uint64_t rx_bcast_bytes;
			uint64_t rx_bcast_frames;
			uint64_t rx_mcast_bytes;
			uint64_t rx_mcast_frames;
			uint64_t rx_ucast_bytes;
			uint64_t rx_ucast_frames;
			uint64_t rx_err_frames;
		} vf;
	} u;
};

struct fw_rss_vi_config_cmd {
	uint32_t op_to_viid;
	uint32_t retval_len16;
	union fw_rss_vi_config {
		struct fw_rss_vi_config_manual {
			uint64_t r3;
			uint64_t r4;
			uint64_t r5;
		} manual;
		struct fw_rss_vi_config_basicvirtual {
			uint32_t r6;
			uint32_t defaultq_to_udpen;
			uint64_t r9;
			uint64_t r10;
		} basicvirtual;
	} u;
};

struct fw_rss_ind_tbl_cmd {
	uint32_t op_to_viid;
	uint32_t retval_len16;
	uint16_t niqid;
	uint16_t startidx;
	uint32_t r3;
	uint32_t iq0_to_iq2;
	uint32_t iq3_to_iq5;
	uint32_t iq6_to_iq8;
	uint32_t iq9_to_iq11;
	uint32_t iq12_to_iq14;
	uint32_t iq15_to_iq17;
	uint32_t iq18_to_iq20;
	uint32_t iq21_to_iq23;
	uint32_t iq24_to_iq26;
	uint32_t iq27_to_iq29;
	uint32_t iq30_iq31;
	uint32_t r15_lo;
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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x2000 / 4];

    /* DMA Context */
    
    uint32_t status;
    uint32_t reset_state;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        if (size == 4) {
            val = s->regs[addr / 4];
        } else if (size == 8) {
            val = s->regs[addr / 4] | ((uint64_t)s->regs[(addr / 4) + 1] << 32);
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->regs)) {
        if (size == 4) {
            s->regs[addr / 4] = val;
        } else if (size == 8) {
            s->regs[addr / 4] = val & 0xFFFFFFFF;
            s->regs[(addr / 4) + 1] = val >> 32;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
    memset(s->regs, 0, sizeof(s->regs));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    if (aligned_size == 0) {
        aligned_size = 4096; /* Fallback to prevent crash if size is 0 */
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1425 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x4000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000; /* To be determined */
    s->bar_info[0].name = "cxgb4vf-bar0";

    s->bar_info[1].type = BAR_TYPE_NONE;

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x1000; /* To be determined */
    s->bar_info[2].name = "cxgb4vf-bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    s->has_msix = true;

    if (s->has_msix) {
        msix_init(pdev, 16, &s->bar_regions[0], 0, 0x1000, &s->bar_regions[0], 0, 0x1800, 0, errp);
    }
    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
    }
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
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cxgb4vf_pci",
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
