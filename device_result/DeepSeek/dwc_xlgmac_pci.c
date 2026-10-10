/* This template provides a robust skeleton for hardware emulation.
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

#define TYPE_PCIBASE_DEVICE "dwc_xlgmac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define XLGMAC_DRV_NAME			"dwc-xlgmac"
#define XLGMAC_MAX_DMA_CHANNELS		16
#define MAC_RSSCR_UDP4TE_POS		3
#define MAC_RSSCR_IP2TE_LEN		1
#define XLGMAC_RSS_MAX_TABLE_SIZE	256
#define MAC_RSSDR_DMCH_LEN		4
#define XLGMAC_INIT_DMA_TX_USECS	1000
#define MAC_RSSCR_TCP4TE_POS		2
#define XLGMAC_INIT_DMA_TX_FRAMES	25
#define MAC_RSSCR_UDP4TE_LEN		1
#define XLGMAC_RX_DESC_CNT		1024
#define XLGMAC_INIT_DMA_RX_USECS	30
#define XLGMAC_TX_DESC_CNT		1024
#define MAC_RSSDR_DMCH_POS		0
#define XLGMAC_INIT_DMA_RX_FRAMES	25
#define MAC_RSSCR_IP2TE_POS		1
#define XLGMAC_RSS_HASH_KEY_SIZE	40
#define MAC_HWF1R_RSSEN_LEN		1
#define MAC_HWF1R_DCBEN_POS		16
#define MAC_HWF1R_ADDR64_LEN		2
#define MAC_HWF0R_TSSTSSEL_POS		25
#define MAC_HWF0R_SMASEL_LEN		1
#define MAC_HWF2R_TXQCNT_LEN		4
#define MAC_HWF2R_RXCHCNT_LEN		4
#define MAC_HWF1R_DBGMEMA_POS		19
#define MAC_HWF1R_L3L4FNUM_LEN		4
#define MAC_HWF0R_PHYIFSEL_POS		1
#define MAC_HWF0R_ADDMACADRSEL_POS	18
#define MAC_HWF0R_EEESEL_POS		13
#define MAC_HWF2R_RXQCNT_LEN		4
#define MAC_HWF0R_MMCSEL_POS		8
#define MAC_HWF0R_RWKSEL_LEN		1
#define MAC_HWF0R_SMASEL_POS		5
#define MAC_VR				0x0110
#define MAC_HWF2R_TXCHCNT_LEN		4
#define MAC_HWF1R_RSSEN_POS		20
#define MAC_HWF1R_SPHEN_POS		17
#define MAC_HWF0R_ADDMACADRSEL_LEN	5
#define MAC_HWF1R_TXFIFOSIZE_LEN	5
#define MAC_HWF1R_RXFIFOSIZE_POS	0
#define MAC_HWF1R_L3L4FNUM_POS		27
#define MAC_HWF0R_RXCOESEL_POS		16
#define MAC_HWF1R_ADDR64_POS		14
#define MAC_HWF1R_SPHEN_LEN		1
#define MAC_HWF2R_PPSOUTNUM_POS		24
#define MAC_HWF2R_RXCHCNT_POS		12
#define MAC_HWF0R_MMCSEL_LEN		1
#define MAC_HWF0R_MGKSEL_POS		7
#define MAC_HWF1R_TSOEN_POS		18
#define MAC_HWF2R_TXQCNT_POS		6
#define MAC_HWF2R				0x0124
#define MAC_HWF1R_TXFIFOSIZE_POS	6
#define MAC_HWF2R_TXCHCNT_POS		18
#define MAC_HWF1R_DCBEN_LEN		1
#define MAC_HWF0R_EEESEL_LEN		1
#define MAC_HWF0R_SAVLANINS_POS		27
#define MAC_HWF1R_DBGMEMA_LEN		1
#define MAC_HWF0R_VLHASH_LEN		1
#define MAC_HWF1R_NUMTC_LEN		3
#define MAC_HWF1R_TSOEN_LEN		1
#define MAC_HWF0R_PHYIFSEL_LEN		2
#define MAC_HWF0R_MGKSEL_LEN		1
#define MAC_HWF2R_AUXSNAPNUM_POS	28
#define MAC_HWF0R_VLHASH_POS		4
#define MAC_HWF0R_RXCOESEL_LEN		1
#define MAC_HWF0R				0x011c
#define MAC_HWF1R				0x0120
#define MAC_HWF1R_RXFIFOSIZE_LEN	5
#define MAC_HWF1R_HASHTBLSZ_LEN		3
#define MAC_HWF2R_PPSOUTNUM_LEN		3
#define MAC_HWF0R_TSSEL_POS		12
#define MAC_HWF1R_NUMTC_POS		21
#define MAC_HWF0R_RWKSEL_POS		6
#define MAC_HWF0R_ARPOFFSEL_POS		9
#define MAC_HWF0R_ARPOFFSEL_LEN		1
#define MAC_HWF0R_TSSTSSEL_LEN		2
#define MAC_HWF0R_TXCOESEL_LEN		1
#define MAC_HWF1R_HASHTBLSZ_POS		24
#define MAC_HWF2R_AUXSNAPNUM_LEN	3
#define MAC_HWF2R_RXQCNT_POS		0
#define MAC_HWF0R_SAVLANINS_LEN		1
#define MAC_HWF0R_TXCOESEL_POS		14
#define MAC_HWF1R_ADVTHWORD_POS		13
#define MAC_HWF1R_ADVTHWORD_LEN		1
#define MAC_HWF0R_TSSEL_LEN		1
#define MTL_RX_THRESHOLD_128		0x03
#define XLGMAC_SYSCLOCK			125000000
#define MTL_TSF_ENABLE			0x01
#define DMA_PBL_X8_ENABLE		0x01
#define DMA_PBL_32				32
#define DMA_OSP_ENABLE			0x01
#define XLGMAC_DRV_VERSION		"1.0.0"
#define MTL_RSF_DISABLE			0x00
#define MTL_TX_THRESHOLD_128		0x03
#define MTL_Q_RQOMR				0x40
#define MTL_Q_RQOMR_RSF_POS		5
#define MTL_Q_RQOMR_RSF_LEN		1
#define DMA_CH_CR_PBLX8_POS		16
#define DMA_CH_CR				0x00
#define DMA_CH_CR_PBLX8_LEN		1
#define RX_NORMAL_DESC3_INTE_LEN	1
#define RX_NORMAL_DESC3_OWN_LEN		1
#define RX_NORMAL_DESC3_INTE_POS	30
#define RX_NORMAL_DESC3_OWN_POS		31
#define DMA_CH_RCR				0x08
#define DMA_CH_RCR_PBL_POS		16
#define DMA_CH_RCR_PBL_LEN		6
#define RX_DESC3_L34T_IPV6_UDP		10
#define RX_NORMAL_DESC3_PL_POS		0
#define RX_PACKET_ATTRIBUTES_INCOMPLETE_LEN	1
#define RX_PACKET_ATTRIBUTES_VLAN_CTAG_LEN	1
#define RX_PACKET_ATTRIBUTES_INCOMPLETE_POS	2
#define RX_NORMAL_DESC3_FD_LEN		1
#define RX_DESC3_L34T_IPV6_TCP		9
#define RX_NORMAL_DESC3_L34T_LEN	4
#define RX_DESC3_L34T_IPV4_TCP		1
#define RX_PACKET_ATTRIBUTES_CSUM_DONE_LEN	1
#define RX_PACKET_ERRORS_FRAME_LEN	1
#define RX_NORMAL_DESC3_CTXT_POS	30
#define RX_NORMAL_DESC3_ES_POS		15
#define RX_NORMAL_DESC3_CDA_LEN		1
#define RX_PACKET_ATTRIBUTES_CONTEXT_NEXT_POS	3
#define RX_PACKET_ATTRIBUTES_CSUM_DONE_POS	0
#define RX_NORMAL_DESC3_RSV_LEN		1
#define RX_NORMAL_DESC3_ETLT_POS	16
#define RX_PACKET_ATTRIBUTES_CONTEXT_LEN	1
#define RX_NORMAL_DESC0_OVT_POS		0
#define RX_PACKET_ATTRIBUTES_CONTEXT_NEXT_LEN	1
#define RX_NORMAL_DESC0_OVT_LEN		16
#define RX_PACKET_ATTRIBUTES_CONTEXT_POS	4
#define RX_DESC3_L34T_IPV4_UDP		2
#define RX_NORMAL_DESC3_PL_LEN		14
#define RX_NORMAL_DESC3_FD_POS		29
#define RX_NORMAL_DESC3_L34T_POS	20
#define RX_NORMAL_DESC3_CDA_POS		27
#define RX_NORMAL_DESC2_HL_LEN		10
#define RX_NORMAL_DESC3_CTXT_LEN	1
#define RX_NORMAL_DESC3_ES_LEN		1
#define RX_NORMAL_DESC3_LD_POS		28
#define RX_PACKET_ATTRIBUTES_RSS_HASH_LEN	1
#define RX_PACKET_ATTRIBUTES_VLAN_CTAG_POS	1
#define RX_NORMAL_DESC2_HL_POS		0
#define RX_PACKET_ATTRIBUTES_RSS_HASH_POS	6
#define RX_NORMAL_DESC3_LD_LEN		1
#define RX_NORMAL_DESC3_RSV_POS		26
#define RX_NORMAL_DESC3_ETLT_LEN	4
#define DMA_CH_TDTR_LO				0x24
#define MMC_RXOUTOFRANGETYPE_LO		0x0980
#define MMC_RX512TO1023OCTETS_GB_LO	0x0960
#define MMC_RXLENGTHERROR_LO		0x0978
#define MMC_RXJABBERERROR			0x0934
#define MMC_TXOCTETCOUNT_GB_LO		0x0814
#define MMC_CR_MCF_LEN			1
#define MMC_RXOCTETCOUNT_G_LO		0x0910
#define MMC_RX256TO511OCTETS_GB_LO	0x0958
#define MMC_RXBROADCASTFRAMES_G_LO	0x0918
#define MMC_RXOVERSIZE_G			0x093c
#define MMC_TX512TO1023OCTETS_GB_LO	0x0854
#define MMC_RX1024TOMAXOCTETS_GB_LO	0x0968
#define MMC_RXWATCHDOGERROR		0x09a0
#define MMC_RXUNICASTFRAMES_G_LO	0x0970
#define MMC_TXFRAMECOUNT_G_LO		0x088c
#define MMC_RX65TO127OCTETS_GB_LO	0x0948
#define MMC_RXRUNTERROR			0x0930
#define MMC_TXBROADCASTFRAMES_GB_LO	0x0874
#define MMC_CR_MCF_POS			3
#define MMC_TXMULTICASTFRAMES_GB_LO	0x086c
#define MMC_TX128TO255OCTETS_GB_LO	0x0844
#define MMC_RXFIFOOVERFLOW_LO		0x0990
#define MMC_RX64OCTETS_GB_LO		0x0940
#define MMC_RXVLANFRAMES_GB_LO		0x0998
#define MMC_TXUNDERFLOWERROR_LO		0x087c
#define MMC_TXVLANFRAMES_G_LO		0x089c
#define MMC_TXMULTICASTFRAMES_G_LO	0x082c
#define MMC_RXMULTICASTFRAMES_G_LO	0x0920
#define MMC_RXOCTETCOUNT_GB_LO		0x0908
#define MMC_RXUNDERSIZE_G		0x0938
#define MMC_TX256TO511OCTETS_GB_LO	0x084c
#define MMC_TXPAUSEFRAMES_LO		0x0894
#define MMC_TXUNICASTFRAMES_GB_LO	0x0864
#define MMC_TXFRAMECOUNT_GB_LO		0x081c
#define MMC_TX64OCTETS_GB_LO		0x0834
#define MMC_CR				0x0800
#define MMC_RXFRAMECOUNT_GB_LO		0x0900
#define MMC_RXCRCERROR_LO		0x0928
#define MMC_RXPAUSEFRAMES_LO		0x0988
#define MMC_TXBROADCASTFRAMES_G_LO	0x0824
#define MMC_TXOCTETCOUNT_G_LO		0x0884
#define MMC_TX1024TOMAXOCTETS_GB_LO	0x085c
#define MMC_TX65TO127OCTETS_GB_LO	0x083c
#define MAC_RCR				0x0004
#define MAC_RCR_IPC_LEN			1
#define MAC_RCR_IPC_POS			9
#define MAC_VLANHTR_VLHT_LEN		16
#define MAC_VLANHTR			0x0058
#define MAC_VLANHTR_VLHT_POS		0
#define DMA_CH_TCR				0x04
#define DMA_CH_TCR_PBL_LEN		6
#define DMA_CH_TCR_PBL_POS		16
#define MAC_TCR_SS_POS			28
#define MAC_TCR				0x0000
#define MAC_TCR_SS_LEN			3
#define MAC_RCR_DCRCC_LEN		1
#define MAC_RCR_CST_LEN			1
#define MAC_RQC0R				0x00a0
#define MAC_RCR_RE_POS			0
#define DMA_CH_RCR_SR_POS		0
#define MAC_RCR_CST_POS			2
#define MAC_RCR_ACS_POS			1
#define MAC_RCR_RE_LEN			1
#define MAC_RCR_ACS_LEN			1
#define MAC_RCR_DCRCC_POS		3
#define DMA_CH_RCR_SR_LEN		1
#define TX_NORMAL_DESC3_LD_POS		28
#define TX_NORMAL_DESC3_LD_LEN		1
#define MTL_Q_RQOMR_RTC_POS		0
#define MTL_Q_RQOMR_RTC_LEN		2
#define MAC_TCR_TE_LEN			1
#define MTL_Q_TQOMR_TXQEN_POS		2
#define MTL_Q_TQOMR_TXQEN_LEN		2
#define DMA_CH_TCR_ST_POS		0
#define MTL_Q_TQOMR				0x00
#define DMA_CH_TCR_ST_LEN		1
#define MAC_TCR_TE_POS			0
#define DMA_CH_TCR_OSP_LEN		1
#define DMA_CH_TCR_OSP_POS		4
#define MMC_RISR_RXPAUSEFRAMES_LEN	1
#define MMC_RISR_RXOVERSIZE_G_LEN	1
#define MMC_RISR_RXLENGTHERROR_LEN	1
#define MMC_RISR_RX512TO1023OCTETS_GB_POS	14
#define MMC_RISR_RXUNICASTFRAMES_G_POS	16
#define MMC_RISR_RXBROADCASTFRAMES_G_LEN	1
#define MMC_RISR_RXOCTETCOUNT_GB_POS	1
#define MMC_RISR_RX1024TOMAXOCTETS_GB_LEN	1
#define MMC_RISR_RXOCTETCOUNT_G_POS	2
#define MMC_RISR_RXFIFOOVERFLOW_LEN	1
#define MMC_RISR_RX128TO255OCTETS_GB_POS	12
#define MMC_RISR_RXOCTETCOUNT_G_LEN	1
#define MMC_RISR_RX65TO127OCTETS_GB_LEN	1
#define MMC_RISR_RXFRAMECOUNT_GB_LEN	1
#define MMC_RISR_RXFIFOOVERFLOW_POS	20
#define MMC_RISR_RX64OCTETS_GB_LEN	1
#define MMC_RISR_RXUNDERSIZE_G_POS	8
#define MMC_RISR_RXOCTETCOUNT_GB_LEN	1
#define MMC_RISR_RXFRAMECOUNT_GB_POS	0
#define MMC_RISR_RXOUTOFRANGETYPE_LEN	1
#define MMC_RISR_RXWATCHDOGERROR_POS	22
#define MMC_RISR_RXMULTICASTFRAMES_G_POS	4
#define MMC_RISR_RXUNDERSIZE_G_LEN	1
#define MMC_RISR_RX128TO255OCTETS_GB_LEN	1
#define MMC_RISR_RXWATCHDOGERROR_LEN	1
#define MMC_RISR_RXJABBERERROR_LEN	1
#define MMC_RISR_RXPAUSEFRAMES_POS	19
#define MMC_RISR_RXLENGTHERROR_POS	17
#define MMC_RISR_RXRUNTERROR_LEN	1
#define MMC_RISR_RXVLANFRAMES_GB_POS	21
#define MMC_RISR_RX256TO511OCTETS_GB_POS	13
#define MMC_RISR_RX512TO1023OCTETS_GB_LEN	1
#define MMC_RISR_RXVLANFRAMES_GB_LEN	1
#define MMC_RISR_RX256TO511OCTETS_GB_LEN	1
#define MMC_RISR_RXMULTICASTFRAMES_G_LEN	1
#define MMC_RISR_RXOUTOFRANGETYPE_POS	18
#define MMC_RISR_RXRUNTERROR_POS	6
#define MMC_RISR_RX1024TOMAXOCTETS_GB_POS	15
#define MMC_RISR_RXJABBERERROR_POS	7
#define MMC_RISR_RXOVERSIZE_G_POS	9
#define MMC_RISR_RXCRCERROR_POS		5
#define MMC_RISR_RXUNICASTFRAMES_G_LEN	1
#define MMC_RISR_RXBROADCASTFRAMES_G_POS	3
#define MMC_RISR_RX64OCTETS_GB_POS	10
#define MMC_RISR				0x0804
#define MMC_RISR_RXCRCERROR_LEN		1
#define MMC_RISR_RX65TO127OCTETS_GB_POS	11
#define MTL_Q_TQOMR_TTC_POS		4
#define MTL_Q_TQOMR_TTC_LEN		3
#define MAC_VLANTR_EVLS_LEN		2
#define MAC_VLANTR_EVLS_POS		21
#define MAC_VLANTR				0x0050
#define MTL_Q_ENABLED			0x02
#define DMA_CH_IER				0x38
#define DMA_CH_IER_RSE_POS		8
#define DMA_CH_IER_TXSE_LEN		1
#define DMA_CH_IER_RBUE_POS		7
#define DMA_CH_IER_FBEE_LEN		1
#define DMA_CH_IER_TXSE_POS		1
#define DMA_CH_IER_TBUE_LEN		1
#define DMA_CH_IER_RSE_LEN		1
#define DMA_CH_IER_TBUE_POS		2
#define DMA_CH_IER_TIE_POS		0
#define DMA_CH_IER_RIE_POS		6
#define DMA_CH_IER_FBEE_POS		12
#define DMA_CH_IER_TIE_LEN		1
#define DMA_CH_IER_RIE_LEN		1
#define DMA_CH_IER_RBUE_LEN		1
#define TX_NORMAL_DESC3_OWN_POS		31
#define TX_NORMAL_DESC3_OWN_LEN		1
#define MAC_VLANTR_DOVLTC_POS		20
#define MAC_VLANTR_ESVL_POS		18
#define MAC_VLANTR_EVLRXS_LEN		1
#define MAC_VLANTR_ERSVLM_LEN		1
#define MAC_VLANTR_ERSVLM_POS		19
#define MAC_VLANTR_EVLRXS_POS		24
#define MAC_VLANTR_ESVL_LEN		1
#define MAC_VLANTR_DOVLTC_LEN		1
#define DMA_MR_SWR_POS			0
#define DMA_MR				0x3000
#define DMA_MR_SWR_LEN			1
#define XLGMAC_DMA_INTERRUPT_MASK	0x31c7
#define MTL_Q_TQOMR_TSF_POS		1
#define MTL_Q_TQOMR_TSF_LEN		1
#define MMC_TISR_TXOCTETCOUNT_GB_LEN	1
#define MMC_TISR_TXBROADCASTFRAMES_GB_POS	12
#define MMC_TISR_TX512TO1023OCTETS_GB_LEN	1
#define MMC_TISR_TXBROADCASTFRAMES_G_LEN	1
#define MMC_TISR_TXUNICASTFRAMES_GB_LEN	1
#define MMC_TISR_TXPAUSEFRAMES_POS	16
#define MMC_TISR_TX128TO255OCTETS_GB_POS	6
#define MMC_TISR_TXOCTETCOUNT_GB_POS	0
#define MMC_TISR_TX64OCTETS_GB_POS	4
#define MMC_TISR_TX1024TOMAXOCTETS_GB_LEN	1
#define MMC_TISR_TXFRAMECOUNT_GB_POS	1
#define MMC_TISR_TXFRAMECOUNT_GB_LEN	1
#define MMC_TISR_TX65TO127OCTETS_GB_POS	5
#define MMC_TISR_TXUNICASTFRAMES_GB_POS	10
#define MMC_TISR_TXPAUSEFRAMES_LEN	1
#define MMC_TISR_TXOCTETCOUNT_G_POS	14
#define MMC_TISR_TXMULTICASTFRAMES_GB_LEN	1
#define MMC_TISR_TXMULTICASTFRAMES_G_POS	3
#define MMC_TISR_TXFRAMECOUNT_G_POS	15
#define MMC_TISR_TXVLANFRAMES_G_POS	17
#define MMC_TISR_TX65TO127OCTETS_GB_LEN	1
#define MMC_TISR				0x0808
#define MMC_TISR_TX256TO511OCTETS_GB_POS	7
#define MMC_TISR_TXMULTICASTFRAMES_GB_POS	11
#define MMC_TISR_TXVLANFRAMES_G_LEN	1
#define MMC_TISR_TXFRAMECOUNT_G_LEN	1
#define MMC_TISR_TXBROADCASTFRAMES_GB_LEN	1
#define MMC_TISR_TXUNDERFLOWERROR_POS	13
#define MMC_TISR_TX1024TOMAXOCTETS_GB_POS	9
#define MMC_TISR_TXUNDERFLOWERROR_LEN	1
#define MMC_TISR_TXOCTETCOUNT_G_LEN	1
#define MMC_TISR_TXBROADCASTFRAMES_G_POS	2
#define MMC_TISR_TX512TO1023OCTETS_GB_POS	8
#define MMC_TISR_TX128TO255OCTETS_GB_LEN	1
#define MMC_TISR_TX64OCTETS_GB_LEN	1
#define MMC_TISR_TXMULTICASTFRAMES_G_LEN	1
#define MMC_TISR_TX256TO511OCTETS_GB_LEN	1
#define DMA_CH_RIWT_RWT_POS		0
#define DMA_CH_RIWT_RWT_LEN		8
#define DMA_CH_RIWT				0x3c
#define TX_NORMAL_DESC3_CTXT_POS	30
#define TX_NORMAL_DESC3_CTXT_LEN	1
#define MAC_RSSCR_RSSE_LEN		1
#define MAC_RSSCR_RSSE_POS		0
#define MAC_RSSCR				0x0c80
#define MAC_PFR_VTFE_LEN		1
#define MAC_PFR_VTFE_POS		16
#define MAC_PFR				0x0008
#define TX_PACKET_ATTRIBUTES_VLAN_CTAG_POS	2
#define TX_CONTEXT_DESC3_VT_POS		0
#define TX_NORMAL_DESC2_VTIR_POS	14
#define TX_PACKET_ATTRIBUTES_CSUM_ENABLE_LEN	1
#define TX_PACKET_ATTRIBUTES_PTP_LEN	1
#define TX_NORMAL_DESC3_TCPHDRLEN_POS	19
#define TX_NORMAL_DESC3_TSE_LEN		1
#define TX_NORMAL_DESC3_FL_LEN		15
#define TX_NORMAL_DESC3_TCPPL_POS	0
#define TX_CONTEXT_DESC3_CTXT_POS	30
#define TX_NORMAL_DESC3_CPC_LEN		2
#define TX_NORMAL_DESC3_FL_POS		0
#define TX_NORMAL_DESC3_CPC_POS		26
#define TX_PACKET_ATTRIBUTES_PTP_POS	3
#define TX_NORMAL_DESC2_IC_LEN		1
#define TX_CONTEXT_DESC3_VLTV_POS	16
#define TX_NORMAL_DESC2_IC_POS		31
#define TX_NORMAL_DESC3_CIC_LEN		2
#define TX_NORMAL_DESC3_FD_LEN		1
#define TX_PACKET_ATTRIBUTES_CSUM_ENABLE_POS	0
#define TX_NORMAL_DESC2_VLAN_INSERT	0x2
#define TX_CONTEXT_DESC2_MSS_POS	0
#define TX_PACKET_ATTRIBUTES_TSO_ENABLE_POS	1
#define TX_NORMAL_DESC3_TSE_POS		18
#define TX_NORMAL_DESC3_TCPPL_LEN	18
#define TX_NORMAL_DESC2_HL_B1L_POS	0
#define TX_CONTEXT_DESC3_TCMSSV_LEN	1
#define TX_CONTEXT_DESC2_MSS_LEN	15
#define TX_NORMAL_DESC2_VTIR_LEN	2
#define TX_PACKET_ATTRIBUTES_TSO_ENABLE_LEN	1
#define TX_NORMAL_DESC2_TTSE_LEN	1
#define TX_NORMAL_DESC3_TCPHDRLEN_LEN	4
#define TX_CONTEXT_DESC3_VLTV_LEN	1
#define TX_NORMAL_DESC3_FD_POS		29
#define TX_PACKET_ATTRIBUTES_VLAN_CTAG_LEN	1
#define TX_NORMAL_DESC2_HL_B1L_LEN	14
#define TX_NORMAL_DESC2_TTSE_POS	30
#define TX_CONTEXT_DESC3_CTXT_LEN	1
#define TX_NORMAL_DESC3_CIC_POS		16
#define TX_CONTEXT_DESC3_TCMSSV_POS	26
#define MAC_VLANTR_ETV_LEN		1
#define MAC_VLANTR_VTIM_LEN		1
#define MAC_VLANTR_VTIM_POS		17
#define MAC_VLANTR_VL_POS		0
#define MAC_VLANTR_ETV_POS		16
#define MAC_VLANTR_VTHM_POS		25
#define MAC_VLANTR_VL_LEN		16
#define MAC_VLANTR_VTHM_LEN		1
#define XLGMAC_TX_MAX_BUF_SIZE	(0x3fff & ~(64 - 1))
#define XLGMAC_SKB_ALLOC_SIZE		512
#define MTL_Q_BASE				0x1100
#define MTL_Q_INC				0x80
#define RX_CONTEXT_DESC3_TSA_LEN	1
#define RX_CONTEXT_DESC3_TSD_LEN	1
#define RX_CONTEXT_DESC3_TSA_POS	4
#define RX_CONTEXT_DESC3_TSD_POS	6
#define RX_PACKET_ATTRIBUTES_RX_TSTAMP_LEN	1
#define RX_PACKET_ATTRIBUTES_RX_TSTAMP_POS	5
#define MTL_Q_RQDR_PRXQ_POS		16
#define XLGMAC_DMA_STOP_TIMEOUT		5
#define MTL_Q_RQDR				0x48
#define MTL_Q_RQDR_RXQSTS_POS		4
#define MTL_Q_RQDR_RXQSTS_LEN		2
#define MTL_Q_RQDR_PRXQ_LEN		14
#define XLGMAC_RSS_LOOKUP_TABLE_TYPE	0
#define MAC_RFCR_RFE_POS		0
#define MAC_RFCR				0x0090
#define MAC_RFCR_RFE_LEN		1
#define XLGMAC_RSS_HASH_KEY_TYPE	1
#define MAC_PFR_PR_LEN			1
#define MAC_PFR_PR_POS			0
#define MAC_PFR_PM_LEN			1
#define MAC_PFR_PM_POS			4
#define DMA_DSR_TPS_LEN			4
#define DMA_DSRX_TPS_START		4
#define DMA_TPS_STOPPED			0x00
#define DMA_DSRX_FIRST_QUEUE		3
#define DMA_DSR_Q_LEN			(DMA_DSR_RPS_LEN + DMA_DSR_TPS_LEN)
#define DMA_DSRX_QPR			4
#define DMA_DSR0				0x3020
#define DMA_DSR1				0x3024
#define DMA_DSR0_TPS_START		12
#define DMA_TPS_SUSPENDED		0x06
#define DMA_DSRX_INC			4
#define MAC_QTFCR_INC			4
#define MAC_Q0TFCR				0x0070
#define MAC_Q0TFCR_TFE_POS		1
#define MTL_Q_RQOMR_EHFC_POS		7
#define MAC_Q0TFCR_TFE_LEN		1
#define XLGMAC_MAX_FLOW_CONTROL_QUEUES	8
#define MAC_Q0TFCR_PT_LEN		16
#define MAC_Q0TFCR_PT_POS		16
#define MTL_Q_RQOMR_EHFC_LEN		1
#define DMA_CH_RCR_RBSZ_LEN		14
#define DMA_CH_RCR_RBSZ_POS		1
#define MTL_Q_IER				0x70
#define MTL_Q_ISR				0x74
#define MTL_Q_RQOMR_FEP_LEN		1
#define MTL_Q_RQOMR_FEP_POS		4
#define MTL_TC_ETSCR			0x10
#define MTL_TC_ETSCR_TSA_LEN		2
#define MTL_RAA_SP				0x00
#define MTL_ETSALG_WRR			0x00
#define MTL_TC_QWR_QW_LEN		21
#define MTL_OMR_RAA_LEN			1
#define MTL_OMR_ETSALG_POS		5
#define MTL_OMR_ETSALG_LEN		2
#define MTL_TSA_ETS				0x02
#define MTL_OMR_RAA_POS			2
#define MTL_TC_ETSCR_TSA_POS		0
#define MTL_TC_QWR				0x18
#define MTL_OMR					0x1000
#define MTL_TC_QWR_QW_POS		0
#define MAC_RCR_JE_POS			8
#define MAC_RCR_JE_LEN			1
#define XLGMAC_STD_PACKET_MTU		1500
#define MAC_PFR_HUC_POS			1
#define MAC_PFR_HMC_LEN			1
#define MAC_PFR_HUC_LEN			1
#define MAC_PFR_HPF_LEN			1
#define MAC_PFR_HMC_POS			2
#define MAC_PFR_HPF_POS			10
#define MTL_RQDCM0R_Q0MDMACH		0x00000000
#define MAC_RQC2R				0x00a8
#define MTL_RQDCM2R_Q8MDMACH		0x00000008
#define MAC_RQC2_INC			4
#define MTL_RQDCM1R_Q5MDMACH		0x00000500
#define MTL_RQDCM1R_Q4MDMACH		0x00000004
#define MTL_RQDCM2R_Q11MDMACH		0x0B000000
#define MTL_RQDCM0R_Q3MDMACH		0x03000000
#define MTL_RQDCM1R_Q7MDMACH		0x07000000
#define MTL_RQDCM0R_Q2MDMACH		0x00020000
#define MTL_Q_TQOMR_Q2TCMAP_LEN		3
#define MTL_RQDCM_INC			4
#define MTL_RQDCM0R_Q1MDMACH		0x00000100
#define MTL_Q_TQOMR_Q2TCMAP_POS		8
#define MTL_RQDCM0R				0x1030
#define MTL_RQDCM1R_Q6MDMACH		0x00060000
#define MAC_RQC2_Q_PER_REG		4
#define MTL_RQDCM2R_Q10MDMACH		0x000A0000
#define MTL_RQDCM2R_Q9MDMACH		0x00000900
#define DMA_CH_IER_AIE_POS		15
#define DMA_CH_IER_NIE_LEN		1
#define DMA_CH_SR				0x60
#define DMA_CH_IER_NIE_POS		16
#define DMA_CH_IER_AIE_LEN		1
#define XLGMAC_SPH_HDSMS_SIZE		3
#define MAC_RCR_HDSMS_LEN		3
#define MAC_RCR_HDSMS_POS		12
#define DMA_CH_CR_SPH_LEN		1
#define DMA_CH_CR_SPH_POS		24
#define MAC_IER					0x00b4
#define MMC_RIER_ALL_INTERRUPTS_POS	0
#define MAC_IER_TSIE_POS		12
#define MAC_IER_TSIE_LEN		1
#define MMC_TIER_ALL_INTERRUPTS_LEN	18
#define MMC_RIER_ALL_INTERRUPTS_LEN	23
#define MMC_TIER				0x0810
#define MMC_RIER				0x080c
#define MMC_TIER_ALL_INTERRUPTS_POS	0
#define MTL_Q_TQOMR_FTQ_LEN		1
#define MTL_Q_TQOMR_FTQ_POS		0
#define DMA_CH_TCR_TSE_POS		12
#define DMA_CH_TCR_TSE_LEN		1
#define MTL_Q_RQOMR_RQS_POS		16
#define MTL_Q_RQOMR_RQS_LEN		9
#define MTL_Q_RQFCR_RFD_POS		17
#define MTL_Q_RQFCR_RFA_POS		1
#define MTL_Q_RQFCR_RFA_LEN		6
#define MTL_Q_RQFCR_RFD_LEN		6
#define MTL_Q_RQFCR				0x50
#define MTL_Q_RQOMR_FUP_LEN		1
#define MTL_Q_RQOMR_FUP_POS		3
#define DMA_SBMR_UNDEF_LEN		1
#define DMA_SBMR_UNDEF_POS		0
#define DMA_SBMR_EAME_LEN		1
#define DMA_SBMR_EAME_POS		11
#define DMA_SBMR_BLEN_256_LEN		1
#define DMA_SBMR_BLEN_256_POS		7
#define DMA_SBMR				0x3004
#define MMC_CR_ROR_LEN			1
#define MMC_CR_CR_POS			0
#define MMC_CR_ROR_POS			2
#define MMC_CR_CR_LEN			1
#define MAC_VLANIR_CSVL_POS		19
#define MAC_VLANIR_CSVL_LEN		1
#define MAC_VLANIR_VLTI_POS		20
#define MAC_VLANIR				0x0060
#define MAC_VLANIR_VLTI_LEN		1
#define MTL_Q_TQOMR_TQS_POS		16
#define MTL_Q_TQOMR_TQS_LEN		10
#define DMA_CH_INC				0x80
#define DMA_CH_BASE				0x3100
#define MAC_RSSAR_CT_LEN		1
#define MAC_RSSAR				0x0c88
#define MAC_RSSAR_OB_POS		0
#define MAC_RSSAR_OB_LEN		1
#define MAC_RSSDR				0x0c8c
#define MAC_RSSAR_ADDRT_LEN		1
#define MAC_RSSDR				0x0c8c
#define MAC_RSSAR_ADDRT_POS		2
#define MAC_RSSAR_CT_POS		1
#define MAC_RSSAR_RSSIA_POS		8
#define MAC_RSSAR_RSSIA_LEN		8
#define MAC_MACA1HR				0x0308
#define XLGMAC_MAC_HASH_TABLE_SIZE	8
#define MAC_HTR0				0x0010
#define MAC_HTR_INC				4
#define DMA_DSR_RPS_LEN			4
#define XLGMAC_MAX_FIFO			81920
#define MAC_MACA_INC			4
#define MAC_MACA1HR_AE_POS		31
#define MAC_MACA1HR_AE_LEN		1

/* Additional register offsets needed for MAC address read */
#define MAC_MACA0HR  0x0300
#define MAC_MACA0LR  0x0304

typedef enum {
	XLGMAC_INT_DMA_CH_SR_TI,
	XLGMAC_INT_DMA_CH_SR_TPS,
	XLGMAC_INT_DMA_CH_SR_TBU,
	XLGMAC_INT_DMA_CH_SR_RI,
	XLGMAC_INT_DMA_CH_SR_RBU,
	XLGMAC_INT_DMA_CH_SR_RPS,
	XLGMAC_INT_DMA_CH_SR_TI_RI,
	XLGMAC_INT_DMA_CH_SR_FBE,
	XLGMAC_INT_DMA_ALL,
} xlgmac_int;

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
    uint32_t version;
    /* Additional register shadows for behavioral modeling */
    uint32_t mac_tcr;
    uint32_t mac_rcr;
    uint32_t dma_mr;
};

struct xlgmac_dma_desc {
    uint32_t desc0;
    uint32_t desc1;
    uint32_t desc2;
    uint32_t desc3;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* TODO: Implement IRQ signaling if needed */
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* TODO: Implement DMA if needed */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case MAC_VR:
        val = 0x00010000; /* Version major.minor */
        break;
    case MAC_HWF0R:
        /* Compose feature register 0 value */
        val = (1 << MAC_HWF0R_RXCOESEL_POS) |
              (1 << MAC_HWF0R_TXCOESEL_POS) |
              (1 << MAC_HWF0R_MMCSEL_POS) |
              (0 << MAC_HWF0R_PHYIFSEL_POS) |
              (1 << MAC_HWF0R_VLHASH_POS) |
              (1 << MAC_HWF0R_SMASEL_POS) |
              (1 << MAC_HWF0R_RWKSEL_POS) |
              (1 << MAC_HWF0R_MGKSEL_POS) |
              (1 << MAC_HWF0R_ARPOFFSEL_POS) |
              (1 << MAC_HWF0R_TSSEL_POS) |
              (1 << MAC_HWF0R_EEESEL_POS) |
              (1 << MAC_HWF0R_ADDMACADRSEL_POS) |
              (1 << MAC_HWF0R_SAVLANINS_POS);
        break;
    case MAC_HWF1R:
        /* Compose feature register 1 value */
        val = (0x1F << MAC_HWF1R_RXFIFOSIZE_POS) |
              (0x1F << MAC_HWF1R_TXFIFOSIZE_POS) |
              (1 << MAC_HWF1R_ADVTHWORD_POS) |
              (2 << MAC_HWF1R_ADDR64_POS) |   /* 64-bit DMA */
              (0 << MAC_HWF1R_DCBEN_POS) |
              (1 << MAC_HWF1R_SPHEN_POS) |
              (1 << MAC_HWF1R_TSOEN_POS) |
              (0 << MAC_HWF1R_DBGMEMA_POS) |
              (1 << MAC_HWF1R_RSSEN_POS) |
              (0 << MAC_HWF1R_NUMTC_POS) |   /* 1 TC */
              (0 << MAC_HWF1R_HASHTBLSZ_POS) |
              (0 << MAC_HWF1R_L3L4FNUM_POS);
        break;
    case MAC_HWF2R:
        /* Set minimal single-queue, single-channel configuration */
        val = (0 << MAC_HWF2R_RXQCNT_POS) |   /* rx_q_cnt = 0 -> 1 queue */
              (0 << MAC_HWF2R_TXQCNT_POS) |   /* tx_q_cnt = 0 -> 1 queue */
              (0 << MAC_HWF2R_RXCHCNT_POS) |  /* rx_ch_cnt = 0 -> 1 channel */
              (0 << MAC_HWF2R_TXCHCNT_POS) |  /* tx_ch_cnt = 0 -> 1 channel */
              (0 << MAC_HWF2R_PPSOUTNUM_POS) |
              (0 << MAC_HWF2R_AUXSNAPNUM_POS);
        break;
    case DMA_MR:
        val = s->dma_mr;
        break;
    case MAC_TCR:
        val = s->mac_tcr;
        break;
    case MAC_RCR:
        val = s->mac_rcr;
        break;
    case MAC_MACA0HR:
        /* Return a valid MAC address: 02:00:00:00:00:01 with AE bit set */
        val = (1U << 31) | 0x0200;
        break;
    case MAC_MACA0LR:
        val = 0x00000001;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented MMIO read addr=0x%04" HWADDR_PRIx " size=%u\n", __func__, addr, size);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case DMA_MR:
        s->dma_mr = val;
        /* If software reset requested, clear it immediately */
        if (val & (1 << DMA_MR_SWR_POS)) {
            s->dma_mr &= ~(1 << DMA_MR_SWR_POS);
        }
        break;
    case MAC_TCR:
        s->mac_tcr = val;
        break;
    case MAC_RCR:
        s->mac_rcr = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented MMIO write addr=0x%04" HWADDR_PRIx " val=0x%08" PRIx64 " size=%u\n", __func__, addr, val, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    qemu_log_mask(LOG_UNIMP, "%s: PIO read addr=0x%04" HWADDR_PRIx " size=%u\n", __func__, addr, size);
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "%s: PIO write addr=0x%04" HWADDR_PRIx " val=0x%08" PRIx64 " size=%u\n", __func__, addr, val, size);
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
    s->dma_mr = 0;
    s->mac_tcr = 0;
    s->mac_rcr = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SYNOPSYS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x7302 );
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
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "dwc-xlgmac-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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

static const VMStateDescription vmstate_pcibase = {
    .name = "dwc_xlgmac_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(mac_tcr, PCIBaseState),
        VMSTATE_UINT32(mac_rcr, PCIBaseState),
        VMSTATE_UINT32(dma_mr, PCIBaseState),
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
