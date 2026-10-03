/*
 * QEMU 8.2.10 virtual PCI device for Marvell OcteonTX2 MCS (MacSec)
 * Generated from driver: drivers/net/ethernet/marvell/octeontx2/af/mcs.c
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

#define GENMASK_ULL(h, l) (((~0ULL) << (l)) & (~0ULL >> (63 - (h))))

#define TYPE_PCIBASE_DEVICE "Marvell_MCS_Driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define PCI_DEVID_CN10K_MCS 0xA096
#define PCI_CLASS_NETWORK_OTHER 0x0280

/* BAR definitions */
#define PCI_CFG_REG_BAR_NUM 0
#define BAR0_SIZE (16 * 1024 * 1024)

/* Register offsets */
#define MCSX_CSE_TX_MEM_SLAVE_IFOUTCTLUCPKTSX(a) (0x1b440ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTOCTETSSECYENCRYPTEDX(a) (0x17c40ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_IFOUTCTLMCPKTSX(a) (0x1bc40ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSECYTOOLONGX(a) (0x1d440ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_IFOUTCTLBCPKTSX(a) (0x1c440ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSECYNOACTIVESAX(a) (0x1dc40ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_IFOUTUNCTLUCPKTSX(a) (0x19c40ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_IFOUTUNCTLBCPKTSX(a) (0x1ac40ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_IFOUTUNCTLMCPKTSX(a) (0x1a440ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_IFOUTUNCTLOCTETSX(a) (0x18c40ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_IFOUTCTLOCTETSX(a) (0x19440ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTOCTETSSECYPROTECTEDX(a) (0x17440ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSECYUNTAGGEDX(a) (0x1cc40ull + (a) * 0x8ull)

#define MCSX_CSE_RX_MEM_SLAVE_IFINUNCTLUCPKTSX(a) (0x7680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INOCTETSSECYDECRYPTEDX(a) (0x5e80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_IFINCTLUCPKTSX(a) (0x8e80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYBADTAGX(a) (0xae80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYNOSAERRORX(a) (0xce80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYTAGGEDCTLX(a) (0xbe80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_IFINCTLOCTETSX(a) (0x6e80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_IFINCTLMCPKTSX(a) (0x9680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_IFINUNCTLOCTETSX(a) (0x6680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYNOTAGX(a) (0xd218ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_IFINUNCTLBCPKTSX(a) (0x8680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INOCTETSSECYVALIDATEX(a) (0x5680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYCTLX(a) (0xb680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYUNTAGGEDX(a) (0xa680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSECYNOSAX(a) (0xc680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_IFINUNCTLMCPKTSX(a) (0x7e80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_IFINCTLBCPKTSX(a) (0x9e80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSCTRLPORTDISABLEDX(a) (0xd680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSFLOWIDTCAMHITX(a) (0x16a80ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSFLOWIDTCAMHITX(a) (0x23240ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSPARSEERRX(a) (0x16880ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSFLOWIDTCAMMISSX(a) (0x22c40ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSPARSEERRX(a) (0x22e40ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSECTAGINSERTIONERRX(a) (0x23040ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSFLOWIDTCAMMISSX(a) (0x16680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSEARLYPREEMPTERRX(a) (0xec58ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSAOKX(a) (0x11680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSAUNUSEDSAX(a) (0x14680ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSAENCRYPTEDX(a) (0x21c40ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSAINVALIDX(a) (0x12680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSANOTVALIDX(a) (0x13680ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSAPROTECTEDX(a) (0x20c40ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSANOTUSINGSAERRORX(a) (0x15680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCCAMHITX(a) (0xfe80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCDELAYEDX(a) (0xe618ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INOCTETSSCVALIDATEX(a) (0xde80ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTOCTETSSCENCRYPTEDX(a) (0x1f440ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCOKX(a) (0xea18ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTOCTETSSCPROTECTEDX(a) (0x1ec40ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCUNCHECKEDX(a) (0xee80ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCINVALIDX(a) (0x10680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCLATEORDELAYEDX(a) (0xf680ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INOCTETSSCDECRYPTEDX(a) (0xe680ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSCENCRYPTEDX(a) (0x20440ull + (a) * 0x8ull)
#define MCSX_CSE_TX_MEM_SLAVE_OUTPKTSSCPROTECTEDX(a) (0x1fc40ull + (a) * 0x8ull)
#define MCSX_CSE_RX_MEM_SLAVE_INPKTSSCNOTVALIDX(a) (0x10e80ull + (a) * 0x8ull)

#define MCSX_CSE_TX_SLAVE_CTRL 0x54a0ull
#define MCSX_CSE_RX_SLAVE_CTRL 0x52a0ull

#define MCSX_CPM_RX_SLAVE_SA_PN_TABLE_MEMX(a) (0x2f700ull + (a) * 0x8ull)
#define MCSX_CPM_TX_SLAVE_SA_PN_TABLE_MEMX(a) (0x50d10ull + (a) * 0x8ull)
#define MCSX_CPM_TX_SLAVE_SA_MAP_MEM_1X(a) (0x3fd18ull + (a) * 0x10ull)
#define MCSX_CPM_TX_SLAVE_SA_MAP_MEM_0X(a) (0x3fd10ull + (a) * 0x10ull)
#define MCSX_CPM_RX_SLAVE_SA_MAP_MEMX(a) (0x256e0ull + (a) * 0x8ull)
#define MCSX_CPM_RX_SLAVE_SA_PLCY_MEMX(a, b) (0x27700ull + (a) * 0x8ull + (b) * 0x40ull)
#define MCSX_CPM_TX_SLAVE_SA_PLCY_MEMX(a, b) (0x40d10ull + (a) * 0x8ull + (b) * 0x80ull)

#define MCSX_CPM_RX_SLAVE_SC_CAM_ENA(a) (0x38740ull + (a) * 0x8ull)
#define MCSX_CPM_RX_SLAVE_SC_CAMX(a, b) (0x38780ull + (a) * 0x8ull + (b) * 0x10ull)

#define MCSX_CPM_RX_SLAVE_SECY_PLCY_MEM_1X(a) (0x246e8ull + (a) * 0x10ull)
#define MCSX_CPM_RX_SLAVE_SECY_PLCY_MEM_0X(a) (0x246e0ull + (a) * 0x10ull)
#define MCSX_CPM_TX_SLAVE_SECY_PLCY_MEMX(a) (0x3ed08ull + (a) * 0x8ull)
#define MCSX_CPM_TX_SLAVE_SECY_MAP_MEM_0X(a) (0x3e508ull + (a) * 0x8ull)
#define MCSX_CPM_RX_SLAVE_SECY_MAP_MEMX(a) (0x23ee0ull + (a) * 0x8ull)

#define MCSX_CPM_RX_SLAVE_FLOWID_TCAM_ENA_1 0x30708ull
#define MCSX_CPM_TX_SLAVE_FLOWID_TCAM_ENA_1 0x51d18ull
#define MCSX_CPM_RX_SLAVE_FLOWID_TCAM_ENA_0 0x30700ull
#define MCSX_CPM_TX_SLAVE_FLOWID_TCAM_ENA_0 0x51d10ull

#define MCSX_CPM_TX_SLAVE_FLOWID_TCAM_MASKX(a, b) (0x55d50ull + (a) * 0x8ull + (b) * 0x20ull)
#define MCSX_CPM_TX_SLAVE_FLOWID_TCAM_DATAX(a, b) (0x51d50ull + (a) * 0x8ull + (b) * 0x20ull)
#define MCSX_CPM_RX_SLAVE_FLOWID_TCAM_DATAX(a, b) (0x30740ull + (a) * 0x8ull + (b) * 0x20ull)
#define MCSX_CPM_RX_SLAVE_FLOWID_TCAM_MASKX(a, b) (0x34740ull + (a) * 0x8ull + (b) * 0x20ull)

#define MCSX_PEX_RX_SLAVE_RULE_ENABLE 0x40e8ull
#define MCSX_PEX_TX_SLAVE_RULE_ENABLE 0x4c88ull
#define MCSX_PEX_TX_SLAVE_RULE_MAC 0x4c80ull
#define MCSX_PEX_RX_SLAVE_RULE_DAX(a) (0x4000ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_RULE_DA_RANGE_MAXX(a) (0x4be8ull + (a) * 0x8ull)
#define MCSX_PEX_RX_SLAVE_RULE_COMBO_ETX(a) (0x4090ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_RULE_COMBO_ETX(a) (0x4c30ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_RULE_COMBO_MINX(a) (0x4c20ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_RULE_DAX(a) (0x4ba0ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_RULE_DA_RANGE_MINX(a) (0x4be0ull + (a) * 0x8ull)
#define MCSX_PEX_RX_SLAVE_RULE_DA_RANGE_MINX(a) (0x4040ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_RULE_ETYPE_CFGX(a) (0x4b60ull + (a) * 0x8ull)
#define MCSX_PEX_RX_SLAVE_RULE_MAC 0x40e0ull
#define MCSX_PEX_RX_SLAVE_RULE_ETYPE_CFGX(a) (0x3fc0ull + (a) * 0x8ull)
#define MCSX_PEX_RX_SLAVE_RULE_COMBO_MAXX(a) (0x4088ull + (a) * 0x8ull)
#define MCSX_PEX_RX_SLAVE_RULE_DA_RANGE_MAXX(a) (0x4048ull + (a) * 0x8ull)
#define MCSX_PEX_RX_SLAVE_RULE_COMBO_MINX(a) (0x4080ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_RULE_COMBO_MAXX(a) (0x4c28ull + (a) * 0x8ull)

#define MCSX_BBE_RX_SLAVE_PLFIFO_OVERFLOW_0 0xe40ull
#define MCSX_BBE_TX_SLAVE_PLFIFO_OVERFLOW_0 0x12b8ull
#define MCSX_BBE_RX_SLAVE_DFIFO_OVERFLOW_0 0xe20ull
#define MCSX_BBE_TX_SLAVE_DFIFO_OVERFLOW_0 0x1298ull

#define MCSX_PAB_RX_SLAVE_PAB_INT 0x16f0ull
#define MCSX_BBE_TX_SLAVE_BBE_INT_INTR_RW 0x1278ull
#define MCSX_PAB_TX_SLAVE_PAB_INT 0x2908ull
#define MCSX_BBE_RX_SLAVE_BBE_INT_INTR_RW 0xe08ull
#define MCSX_CPM_TX_SLAVE_TX_INT 0x3d490ull
#define MCSX_BBE_TX_SLAVE_BBE_INT 0x1278ull
#define MCSX_PAB_TX_SLAVE_PAB_INT_INTR_RW 0x16f8ull
#define MCSX_IP_INT_ENA_W1S 0x80040ull
#define MCSX_IP_INT_ENA_W1C 0x80038ull
#define MCSX_BBE_RX_SLAVE_BBE_INT 0xe00ull
#define MCSX_PAB_RX_SLAVE_PAB_INT_INTR_RW 0x16f8ull
#define MCSX_IP_INT 0x80028ull
#define MCSX_CPM_RX_SLAVE_RX_INT 0x23c00ull
#define MCSX_TOP_SLAVE_INT_SUM 0xc20ull
#define MCSX_BBE_TX_SLAVE_BBE_INT_ENB 0x1280ull
#define MCSX_TOP_SLAVE_INT_SUM_ENB 0xc28ull
#define MCSX_PAB_TX_SLAVE_PAB_INT_ENB 0x2910ull
#define MCSX_PAB_RX_SLAVE_PAB_INT_ENB 0x16f8ull
#define MCSX_CPM_RX_SLAVE_RX_INT_ENB 0x23c08ull
#define MCSX_BBE_RX_SLAVE_BBE_INT_ENB 0xe08ull
#define MCSX_CPM_TX_SLAVE_TX_INT_ENB 0x3d498ull

#define MCSX_PEX_TX_SLAVE_CUSTOM_TAG_REL_MODE_SEL(a) (0x788ull + (a) * 0x8ull)
#define MCSX_PAB_RX_SLAVE_PORT_CFGX(a) (0x1718ull + (a) * 0x40ull)
#define MCSX_PEX_TX_SLAVE_PORT_CONFIG(a) (0x4738ull + (a) * 0x8ull)
#define MCSX_PEX_RX_SLAVE_PEX_CONFIGURATION 0x3b50ull
#define MCSX_PAB_RX_SLAVE_FIFO_SKID_CFGX(a) (0x290ull + (a) * 0x40ull)
#define MCSX_PEX_RX_SLAVE_CUSTOM_TAGX(a) (0x4c8ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_VLAN_CFGX(a) (0x46f8ull + (a) * 0x8ull)
#define MCSX_PEX_TX_SLAVE_ETYPE_ENABLE 0x968ull
#define MCSX_PEX_TX_SLAVE_CUSTOM_TAGX(a) (0x748ull + (a) * 0x8ull)
#define MCSX_PEX_RX_SLAVE_ETYPE_ENABLE 0x6e8ull
#define MCSX_PEX_RX_SLAVE_VLAN_CFGX(a) (0x3b58ull + (a) * 0x8ull)

#define MCSX_MCS_TOP_SLAVE_PORT_RESET(a) (0x408ull + (a) * 0x8ull)
#define MCSX_MCS_TOP_SLAVE_CHANNEL_CFG(a) (0x808ull + (a) * 0x8ull)

#define MCSX_CPM_RX_SLAVE_XPN_THRESHOLD 0x23e40ull
#define MCSX_CPM_TX_SLAVE_PN_THRESHOLD 0x3e4b8ull
#define MCSX_CPM_TX_SLAVE_XPN_THRESHOLD 0x3e4b0ull
#define MCSX_CPM_RX_SLAVE_PN_THRESHOLD 0x23e48ull

#define MCSX_PAB_TX_SLAVE_PORT_CFGX(a) (0x2930ull + (a) * 0x40ull)
#define MCSX_LINK_LMACX_CFG(a) (0x90000ull + (a) * 0x800ull)
#define MCSX_LINK_LMAC_BASE_MASK GENMASK_ULL(11, 0)
#define MCSX_LINK_LMAC_RANGE_MASK GENMASK_ULL(19, 16)

#define MCSX_MIL_GLOBAL 0x80000ull
#define MCSX_MIL_RX_GBL_STATUS 0x800c8ull
#define MCSX_BBE_RX_SLAVE_CAL_LEN 0x188ull
#define MCSX_IP_MODE 0x900c8ull
#define MCSX_CSE_RX_SLAVE_STATS_CLEAR 0x52b8ull
#define MCSX_BBE_RX_SLAVE_CAL_ENTRY 0x180ull
#define MCSX_CSE_TX_SLAVE_STATS_CLEAR 0x54b8ull

#define MCS_CN10KB_INT_VEC_IP 0x53

#define MCS_ID_MASK 0x7
#define PCI_SUBSYS_DEVID_CN10K_B 0xBD00
#define MCS_MAX_PFS 128
#define RVU_PFVF_FUNC_MASK 0x3FF

#define MCSX_CPM_TX_SLAVE_AUTO_REKEY_ENABLE_0 0x5500ull
#define MCSX_CPM_TX_SLAVE_TX_SA_ACTIVEX(a) (0x5b50 + (a) * 0x8ull)

/* Interrupt related register offsets and bit definitions */
#define MCS_CPM_RX_INT_SECTAG_V_EQ1 BIT_ULL(0)
#define MCS_CPM_RX_SECTAG_SL_GTE48_INT BIT_ULL(2)
#define MCS_CPM_RX_INT_ES_EQ1_SC_EQ1 BIT_ULL(3)
#define MCS_CPM_RX_INT_SL_GTE48 BIT_ULL(2)
#define MCS_CPM_RX_INT_SC_EQ1_SCB_EQ1 BIT_ULL(4)
#define MCS_CPM_RX_SECTAG_SC_EQ1_SCB_EQ1_INT BIT_ULL(4)
#define MCS_CPM_RX_PACKET_XPN_EQ0_INT BIT_ULL(5)
#define MCS_CPM_RX_SECTAG_E_EQ0_C_EQ1_INT BIT_ULL(1)
#define MCS_CPM_RX_INT_PACKET_XPN_EQ0 BIT_ULL(5)
#define MCS_CPM_RX_SECTAG_ES_EQ1_SC_EQ1_INT BIT_ULL(3)
#define MCS_CPM_RX_INT_SECTAG_E_EQ0_C_EQ1 BIT_ULL(1)
#define MCS_CPM_RX_SECTAG_V_EQ1_INT BIT_ULL(0)

#define MCS_CPM_TX_INT_SA_NOT_VALID BIT_ULL(2)
#define MCS_CPM_TX_SA_NOT_VALID_INT BIT_ULL(9)
#define MCS_PAB_TX_INT_ENA BIT_ULL(5)
#define MCS_PAB_RX_INT_ENA BIT_ULL(4)
#define MCS_CPM_RX_INT_ENA BIT_ULL(2)
#define MCS_BBE_RX_INT_ENA BIT_ULL(0)
#define MCS_CPM_TX_INT_PACKET_XPN_EQ0 BIT_ULL(0)
#define MCS_BBE_TX_INT_ENA BIT_ULL(1)
#define MCS_CPM_RX_INT_ALL (MCS_CPM_RX_INT_SECTAG_V_EQ1 | \
                         MCS_CPM_RX_INT_SECTAG_E_EQ0_C_EQ1 | \
                         MCS_CPM_RX_INT_SL_GTE48 | \
                         MCS_CPM_RX_INT_ES_EQ1_SC_EQ1 | \
                         MCS_CPM_RX_INT_SC_EQ1_SCB_EQ1 | \
                         MCS_CPM_RX_INT_PACKET_XPN_EQ0 | \
                         MCS_CPM_RX_INT_PN_THRESH_REACHED)
#define MCS_CPM_RX_INT_PN_THRESH_REACHED BIT_ULL(6)
#define MCS_CPM_TX_INT_ENA BIT_ULL(3)
#define MCS_CPM_TX_INT_PN_THRESH_REACHED BIT_ULL(1)
#define MCS_CPM_TX_PACKET_XPN_EQ0_INT BIT_ULL(7)
#define MCS_CPM_TX_PN_THRESH_REACHED_INT BIT_ULL(8)
#define MCS_CPM_RX_PN_THRESH_REACHED_INT BIT_ULL(6)

#define MCS_PORT_FIFO_SKID_MASK 0x3F
#define MCS_PORT_MODE_MASK 0x3
#define MCS_MAX_CUSTOM_TAGS 0x8

#define MCS_CTRLPKT_ETYPE_RULE_MAX 8
#define MCS_CTRLPKT_DA_RANGE_RULE_MAX 4
#define MCS_CTRLPKT_MAC_RULE_MAX 1
#define MCS_CTRLPKT_DA_RULE_MAX 8
#define MCS_CTRLPKT_COMBO_RULE_MAX 4
#define MCS_MAX_CTRLPKT_RULES (MCS_CTRLPKT_ETYPE_RULE_MAX + \
                            MCS_CTRLPKT_DA_RULE_MAX + \
                            MCS_CTRLPKT_DA_RANGE_RULE_MAX + \
                            MCS_CTRLPKT_COMBO_RULE_MAX + \
                            MCS_CTRLPKT_MAC_RULE_MAX)

/* Enums from driver */
enum mcs_direction {
    MCS_RX,
    MCS_TX,
};

/* BAR info types */
typedef enum { BAR_TYPE_NONE, BAR_TYPE_MMIO, BAR_TYPE_PIO, BAR_TYPE_RAM } BARType;

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
    uint64_t intr_status;    /* Cumulative interrupt status */
    uint64_t intr_enable;    /* Interrupt enable mask */

    /* Hardware Register Storage */
    uint64_t regs[BAR0_SIZE / 8]; /* Flat register file */

    /* Additional device-specific state */
    uint8_t mcs_blks;   /* Number of MCS blocks (default 1) */
    bool bypass;        /* Bypass mode flag */

    /* MSI-X support */
    MemoryRegion msix_table;
    int ip_vector;      /* Vector number for IP interrupt, 0x53 */

    /* Calibration state */
    uint64_t mil_rx_gbl_status; /* Cached status for MCSX_MIL_RX_GBL_STATUS */
};

/* Internal helper for interrupt updates */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t cpm_rx_status = s->regs[MCSX_CPM_RX_SLAVE_RX_INT / 8];
    uint64_t cpm_rx_ena = s->regs[MCSX_CPM_RX_SLAVE_RX_INT_ENB / 8];
    uint64_t cpm_tx_status = s->regs[MCSX_CPM_TX_SLAVE_TX_INT / 8];
    uint64_t cpm_tx_ena = s->regs[MCSX_CPM_TX_SLAVE_TX_INT_ENB / 8];
    uint64_t bbe_rx_status = s->regs[MCSX_BBE_RX_SLAVE_BBE_INT / 8];
    uint64_t bbe_rx_ena = s->regs[MCSX_BBE_RX_SLAVE_BBE_INT_ENB / 8];
    uint64_t bbe_tx_status = s->regs[MCSX_BBE_TX_SLAVE_BBE_INT / 8];
    uint64_t bbe_tx_ena = s->regs[MCSX_BBE_TX_SLAVE_BBE_INT_ENB / 8];
    uint64_t pab_rx_status = s->regs[MCSX_PAB_RX_SLAVE_PAB_INT / 8];
    uint64_t pab_rx_ena = s->regs[MCSX_PAB_RX_SLAVE_PAB_INT_ENB / 8];
    uint64_t pab_tx_status = s->regs[MCSX_PAB_TX_SLAVE_PAB_INT / 8];
    uint64_t pab_tx_ena = s->regs[MCSX_PAB_TX_SLAVE_PAB_INT_ENB / 8];
    uint64_t top_enb = s->regs[MCSX_TOP_SLAVE_INT_SUM_ENB / 8];
    uint64_t ip_ena = s->regs[MCSX_IP_INT_ENA_W1S / 8]; /* IP enable stored in W1S reg */

    uint64_t sum = 0;
    if (top_enb & MCS_CPM_RX_INT_ENA && (cpm_rx_status & cpm_rx_ena)) sum |= MCS_CPM_RX_INT_ENA;
    if (top_enb & MCS_CPM_TX_INT_ENA && (cpm_tx_status & cpm_tx_ena)) sum |= MCS_CPM_TX_INT_ENA;
    if (top_enb & MCS_BBE_RX_INT_ENA && (bbe_rx_status & bbe_rx_ena)) sum |= MCS_BBE_RX_INT_ENA;
    if (top_enb & MCS_BBE_TX_INT_ENA && (bbe_tx_status & bbe_tx_ena)) sum |= MCS_BBE_TX_INT_ENA;
    if (top_enb & MCS_PAB_RX_INT_ENA && (pab_rx_status & pab_rx_ena)) sum |= MCS_PAB_RX_INT_ENA;
    if (top_enb & MCS_PAB_TX_INT_ENA && (pab_tx_status & pab_tx_ena)) sum |= MCS_PAB_TX_INT_ENA;

    bool ip_raw = (sum != 0);
    uint64_t ip_int = ip_raw ? BIT_ULL(0) : 0;
    s->regs[MCSX_IP_INT / 8] = ip_int;

    /* Notify MSI-X vector if IP interrupt is raw and enabled */
    if (ip_raw && (ip_ena & BIT_ULL(0))) {
        msix_notify(pdev, s->ip_vector);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "MCS MMIO read out of bounds: addr=0x%" HWADDR_PRIx "\n", addr);
        return 0;
    }

    /* Special calibration status register */
    if (addr == MCSX_MIL_RX_GBL_STATUS) {
        return s->mil_rx_gbl_status;
    }

    /* Top-level summary is read-only, compute on the fly */
    if (addr == MCSX_TOP_SLAVE_INT_SUM) {
        uint64_t cpm_rx_status = s->regs[MCSX_CPM_RX_SLAVE_RX_INT / 8];
        uint64_t cpm_rx_ena = s->regs[MCSX_CPM_RX_SLAVE_RX_INT_ENB / 8];
        uint64_t cpm_tx_status = s->regs[MCSX_CPM_TX_SLAVE_TX_INT / 8];
        uint64_t cpm_tx_ena = s->regs[MCSX_CPM_TX_SLAVE_TX_INT_ENB / 8];
        uint64_t bbe_rx_status = s->regs[MCSX_BBE_RX_SLAVE_BBE_INT / 8];
        uint64_t bbe_rx_ena = s->regs[MCSX_BBE_RX_SLAVE_BBE_INT_ENB / 8];
        uint64_t bbe_tx_status = s->regs[MCSX_BBE_TX_SLAVE_BBE_INT / 8];
        uint64_t bbe_tx_ena = s->regs[MCSX_BBE_TX_SLAVE_BBE_INT_ENB / 8];
        uint64_t pab_rx_status = s->regs[MCSX_PAB_RX_SLAVE_PAB_INT / 8];
        uint64_t pab_rx_ena = s->regs[MCSX_PAB_RX_SLAVE_PAB_INT_ENB / 8];
        uint64_t pab_tx_status = s->regs[MCSX_PAB_TX_SLAVE_PAB_INT / 8];
        uint64_t pab_tx_ena = s->regs[MCSX_PAB_TX_SLAVE_PAB_INT_ENB / 8];
        uint64_t top_enb = s->regs[MCSX_TOP_SLAVE_INT_SUM_ENB / 8];

        uint64_t sum = 0;
        if (top_enb & MCS_CPM_RX_INT_ENA && (cpm_rx_status & cpm_rx_ena)) sum |= MCS_CPM_RX_INT_ENA;
        if (top_enb & MCS_CPM_TX_INT_ENA && (cpm_tx_status & cpm_tx_ena)) sum |= MCS_CPM_TX_INT_ENA;
        if (top_enb & MCS_BBE_RX_INT_ENA && (bbe_rx_status & bbe_rx_ena)) sum |= MCS_BBE_RX_INT_ENA;
        if (top_enb & MCS_BBE_TX_INT_ENA && (bbe_tx_status & bbe_tx_ena)) sum |= MCS_BBE_TX_INT_ENA;
        if (top_enb & MCS_PAB_RX_INT_ENA && (pab_rx_status & pab_rx_ena)) sum |= MCS_PAB_RX_INT_ENA;
        if (top_enb & MCS_PAB_TX_INT_ENA && (pab_tx_status & pab_tx_ena)) sum |= MCS_PAB_TX_INT_ENA;
        return sum;
    }

    /* Default: read from regs array, handle size */
    val = s->regs[addr / 8];
    /* For 8-byte reads, it's fine; for smaller, we need to extract */
    if (size < 8) {
        val = (val >> ((addr & 7) * 8)) & ((1ULL << (size * 8)) - 1);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "MCS MMIO write out of bounds: addr=0x%" HWADDR_PRIx " val=0x%" PRIx64 "\n", addr, val);
        return;
    }

    uint64_t mask;
    if (size < 8) {
        /* For sub-8 writes, only modify the appropriate bytes */
        uint64_t shift = (addr & 7) * 8;
        mask = ((1ULL << (size * 8)) - 1) << shift;
        val = (val << shift) & mask;
    } else {
        mask = ~0ULL;
    }

    /* Special registers */
    if (addr == MCSX_IP_INT) {
        /* Write-1-to-clear, only bit 0 matters */
        if (val & BIT_ULL(0)) {
            s->regs[addr / 8] &= ~BIT_ULL(0);
            pcibase_update_irq(s);
        }
        return;
    } else if (addr == MCSX_IP_INT_ENA_W1S) {
        /* Write-1-to-set IP enable */
        s->regs[addr / 8] |= val; /* Store raw enable in W1S reg (we'll use that for enable) */
        /* also store same in W1C for consistency if needed, but not required */
        s->regs[MCSX_IP_INT_ENA_W1C / 8] = s->regs[addr / 8];
        pcibase_update_irq(s);
        return;
    } else if (addr == MCSX_IP_INT_ENA_W1C) {
        /* Write-1-to-clear IP enable */
        s->regs[addr / 8] &= ~val;
        s->regs[MCSX_IP_INT_ENA_W1S / 8] = s->regs[addr / 8];
        pcibase_update_irq(s);
        return;
    } else if (addr == MCSX_MIL_GLOBAL) {
        /* Handle calibration trigger and bypass */
        uint64_t old = s->regs[addr / 8];
        s->regs[addr / 8] = (old & ~mask) | (val & mask);
        if (val & BIT_ULL(5)) {
            /* Start calibration: set gbl status bits immediately */
            s->mil_rx_gbl_status = BIT_ULL(0) | GENMASK_ULL(5, 1); /* bits 0..5 set */
        } else if (!(val & BIT_ULL(5)) && (old & BIT_ULL(5))) {
            /* Calibration finished, clear status bits */
            s->mil_rx_gbl_status = 0;
        }
        /* Bypass bit 6 is simply stored */
        return;
    } else if (addr == MCSX_CPM_RX_SLAVE_RX_INT ||
               addr == MCSX_CPM_TX_SLAVE_TX_INT ||
               addr == MCSX_BBE_RX_SLAVE_BBE_INT ||
               addr == MCSX_BBE_TX_SLAVE_BBE_INT ||
               addr == MCSX_PAB_RX_SLAVE_PAB_INT ||
               addr == MCSX_PAB_TX_SLAVE_PAB_INT) {
        /* Write-1-to-clear interrupt status */
        s->regs[addr / 8] &= ~val;
        pcibase_update_irq(s);
        return;
    } else if (addr == MCSX_TOP_SLAVE_INT_SUM_ENB ||
               addr == MCSX_CPM_RX_SLAVE_RX_INT_ENB ||
               addr == MCSX_CPM_TX_SLAVE_TX_INT_ENB ||
               addr == MCSX_BBE_RX_SLAVE_BBE_INT_ENB ||
               addr == MCSX_BBE_TX_SLAVE_BBE_INT_ENB ||
               addr == MCSX_PAB_RX_SLAVE_PAB_INT_ENB ||
               addr == MCSX_PAB_TX_SLAVE_PAB_INT_ENB) {
        /* Interrupt enable registers: plain store and update */
        uint64_t old = s->regs[addr / 8];
        s->regs[addr / 8] = (old & ~mask) | (val & mask);
        pcibase_update_irq(s);
        return;
    }

    /* Default write for other registers */
    uint64_t old = s->regs[addr / 8];
    s->regs[addr / 8] = (old & ~mask) | (val & mask);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_enable = 0;
    s->bypass = false;
    s->mcs_blks = 1;
    s->mil_rx_gbl_status = 0;
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVID_CN10K_MCS);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 0: MMIO register space */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "mcs-mmio";

    /* BAR 1: MSI-X table and PBA */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = 0x1000; /* 4KB, enough for 84 vectors */
    s->bar_info[1].name = "mcs-msix";
    s->num_bars = 2;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization: 84 vectors, table at BAR 1 offset 0, PBA at offset 0x800 */
    if (msix_init(pdev, 84, &s->bar_regions[1], 1, 0, &s->bar_regions[1], 1, 0x800, 0x80, errp)) {
        return;
    }
    s->ip_vector = MCS_CN10KB_INT_VEC_IP; /* 0x53 */

    s->mcs_blks = 1;
    s->bypass = false;
    memset(s->regs, 0, sizeof(s->regs));
    s->mil_rx_gbl_status = 0;
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
    .name = "Marvell_MCS_Driver_pci",
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
