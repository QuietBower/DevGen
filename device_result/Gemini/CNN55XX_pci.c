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
#include "hw/pci/pci_device.h"

#define TYPE_PCIBASE_DEVICE "CNN55XX_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define CNN55XX_DEV_ID 0x12
#define UCD_UCODE_LOAD_BLOCK_NUM 0x12C0010
#define UCD_UCODE_LOAD_IDX_DATAX(_i) (0x12C0018 + ((_i) * 0x20))
#define POM_GRP_EXECMASKX(_i) (0x11C1100 | ((_i) * 8))
#define UCD_AE_EID_UCODE_BLOCK_NUMX(_i) (0x12C0008 + ((_i) * 0x800))
#define UCD_SE_EID_UCODE_BLOCK_NUMX(_i) (0x12C0000 + ((_i) * 0x1000))
#define AQM_GRP_EXECMSK_LOX(x) (0x1300C00 + ((x) * 0x10))
#define AQM_GRP_EXECMSK_HIX(x) (0x1300C08 + ((x) * 0x10))
#define PEM_BIST_STATUSX(_i) (0x1080468 | ((_i) << 18))
#define EFL_TOP_BIST_STAT 0x1241090
#define LBC_BIST_STATUS 0x1200020
#define UCD_BIST_STATUS 0x12C0070
#define EMU_BIST_STATUSX(_i) (0x1402700 + ((_i) * 0x40000))
#define POM_BIST_REG 0x11C0100
#define EFL_CORE_BIST_REGX(_i) (0x1240100 + ((_i) * 0x400))
#define NPS_CORE_BIST_REG 0x10000E8
#define BMI_BIST_REG 0x1140080
#define BMO_BIST_REG 0x1180080
#define NPS_CORE_NPC_BIST_REG 0x1000128
#define NPS_PKT_SLC_BIST_REG 0x1040088
#define NPS_PKT_IN_BIST_REG 0x1040100
#define BMI_INT_ENA_W1S 0x1140018
#define BMI_CTL 0x1140020
#define LBC_ELM_VF65_128_INT_ENA_W1S 0x120F000
#define LBC_PLM_VF1_64_INT_ENA_W1S 0x1205008
#define LBC_PLM_VF65_128_INT_ENA_W1S 0x1209008
#define LBC_ELM_VF1_64_INT_ENA_W1S 0x120B000
#define LBC_INT_ENA_W1S 0x1203000
#define EFL_CORE_INT_ENA_W1SX(_i) (0x1240018 + ((_i) * 0x400))
#define EFL_CORE_VF_ERR_INT0_ENA_W1SX(_i) (0x1240068 + ((_i) * 0x400))
#define EFL_CORE_VF_ERR_INT1_ENA_W1SX(_i) (0x1240088 + ((_i) * 0x400))
#define EMU_WD_INT_ENA_W1SX(_i) (0x1402318 + ((_i) * 0x40000))
#define EMU_GE_INT_ENA_W1SX(_i) (0x1402518 + ((_i) * 0x40000))
#define EMU_FUSE_MAPX(_i) (0x1402708 + ((_i) * 0x40000))
#define FUS_DAT1 0x10C1408
#define RST_BOOT 0x10C1600
#define BMO_CTL2 0x1180028
#define POM_PERF_CTL 0x11CC400
#define POM_INT_ENA_W1S 0x11C0018
#define EFL_RNM_CTL_STATUS 0x1241800
#define NPS_CORE_GBL_VFCFG 0x1000000
#define NPS_CORE_CONTROL 0x1000008
#define AQMQ_CMD_CNTX(x) (0x20020 + ((x) * 0x40000))
#define AQMQ_DRBLX(x) (0x20000 + ((x) * 0x40000))
#define NPS_PKT_IN_INSTR_BAOFF_DBELLX(_i) (0x10078 + ((_i) * 0x40000))
#define NPS_PKT_SLC_CNTSX(_i) (0x10008 + ((_i) * 0x40000))
#define LBC_INVAL_STATUS 0x1202010
#define LBC_INVAL_CTL 0x1201010
#define EMU_AE_ENABLEX(_i) (0x1400008 + ((_i) * 0x40000))
#define EMU_SE_ENABLEX(_i) (0x1400000 + ((_i) * 0x40000))
#define AQM_DBELL_OVF_LO_ENA_W1S 0x1300030
#define AQM_DMA_RD_ERR_LO_ENA_W1S 0x1300070
#define AQM_DBELL_OVF_HI_ENA_W1S 0x1300048
#define AQM_EXEC_NA_LO_ENA_W1S 0x13000B0
#define AQM_EXEC_ERR_HI_ENA_W1S 0x1300108
#define AQM_EXEC_NA_HI_ENA_W1S 0x13000C8
#define AQM_DMA_RD_ERR_HI_ENA_W1S 0x1300088
#define AQM_EXEC_ERR_LO_ENA_W1S 0x13000F0
#define AQMQ_NXT_CMDX(x) (0x20018 + ((x) * 0x40000))
#define AQMQ_QSZX(x) (0x20008 + ((x) * 0x40000))
#define AQMQ_BADRX(x) (0x20010 + ((x) * 0x40000))
#define AQMQ_CMP_THRX(x) (0x20028 + ((x) * 0x40000))
#define NPS_PKT_IN_INSTR_RSIZEX(_i) (0x10070 + ((_i) * 0x40000))
#define NPS_PKT_IN_INT_LEVELSX(_i) (0x10088 + ((_i) * 0x40000))
#define NPS_PKT_IN_INSTR_BADDRX(_i) (0x10068 + ((_i) * 0x40000))
#define NPS_PKT_IN_RERR_LO_ENA_W1S 0x1040140
#define NPS_PKT_IN_RERR_HI_ENA_W1S 0x1040120
#define NPS_PKT_IN_ERR_TYPE_ENA_W1S 0x1040160
#define NPS_PKT_SLC_RERR_HI_ENA_W1S 0x1040220
#define NPS_PKT_SLC_ERR_TYPE_ENA_W1S 0x1040260
#define NPS_PKT_SLC_RERR_LO_ENA_W1S 0x1040240
#define NPS_CORE_INT_ENA_W1S 0x10000B8
#define AQMQ_ENX(x) (0x20048 + ((x) * 0x40000))
#define AQMQ_CMP_CNTX(x) (0x20030 + ((x) * 0x40000))
#define AQMQ_ACTIVITY_STATX(x) (0x20050 + ((x) * 0x40000))
#define NPS_PKT_SLC_INT_LEVELSX(_i) (0x10010 + ((_i) * 0x40000))
#define NPS_PKT_IN_INSTR_CTLX(_i) (0x10060 + ((_i) * 0x40000))
#define NPS_PKT_IN_DONE_CNTSX(_i) (0x10080 + ((_i) * 0x40000))
#define NPS_PKT_SLC_CTLX(_i) (0x10000 + ((_i) * 0x40000))
#define NPS_PKT_MBOX_INT_HI_ENA_W1S 0x1040058
#define NPS_PKT_MBOX_INT_LO_ENA_W1S 0x1040038
#define NPS_PKT_MBOX_INT_LO_ENA_W1C 0x1040030
#define NPS_PKT_MBOX_INT_HI_ENA_W1C 0x1040050
#define NPS_CORE_INT_ACTIVE 0x1000080

#define NR_CLUSTERS 4
#define PLL_REF_CLK 50
#define AE_CORES_PER_CLUSTER 20
#define SE_CORES_PER_CLUSTER 16
#define ZIP_MAX_CORES 5
#define CNN55XX_MAX_UCODE_SIZE (CNN55XX_UCD_BLOCK_SIZE * 2)
#define VERSION_LEN 32
#define DEFAULT_SE_GROUP 0
#define CNN55XX_UCD_BLOCK_SIZE 32768
#define DEFAULT_AE_GROUP 0

union emu_fuse_map {
    uint64_t value;
    struct {
        uint64_t se_fuse : 16;
        uint64_t raz_16_31 : 16;
        uint64_t ae_fuse : 20;
        uint64_t raz_52_62 : 11;
        uint64_t valid : 1;
    } s;
};

union rst_boot {
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
    };
};

union fus_dat1 {
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
    };
};

union nps_core_gbl_vfcfg {
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
};

union pom_int_ena_w1s {
    uint64_t value;
    struct {
        uint64_t raz0 : 1;
        uint64_t raz1 : 1;
        uint64_t illegal_dport : 1;
        uint64_t illegal_intf : 1;
        uint64_t raz2 : 60;
    } s;
};

union efl_core_int_ena_w1s {
    uint64_t value;
    struct {
        uint64_t len_ovr : 1;
        uint64_t d_left : 1;
        uint64_t raz_2_5 : 4;
        uint64_t epci_decode_err : 1;
        uint64_t raz_7_63 : 57;
    } s;
};

union bmi_ctl {
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
};

union bmi_int_ena_w1s {
    uint64_t value;
    struct {
        uint64_t max_len_err_nps : 1;
        uint64_t max_len_err_ilk : 1;
        uint64_t pkt_rcv_err_nps : 1;
        uint64_t pkt_rcv_err_ilk : 1;
        uint64_t sop_err_nps : 1;
        uint64_t sop_err_ilk : 1;
        uint64_t eop_err_nps : 1;
        uint64_t eop_err_ilk : 1;
        uint64_t fpf_undrrn : 1;
        uint64_t raz_9 : 1;
        uint64_t raz_10 : 1;
        uint64_t nps_req_oflw : 1;
        uint64_t ilk_req_oflw : 1;
        uint64_t raz_13_63 : 51;
    } s;
};

union bmo_ctl2 {
    uint64_t value;
    struct {
        uint64_t totl_buf_thrsh : 8;
        uint64_t nps_uns_buf_thrsh : 8;
        uint64_t nps_slc_buf_thrsh : 8;
        uint64_t ilk_buf_thrsh : 8;
        uint64_t raz_32_62 : 31;
        uint64_t arb_sel : 1;
    } s;
};

union lbc_int_ena_w1s {
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
};

union efl_rnm_ctl_status {
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
};

struct ucode {
    uint8_t id;
    char version[VERSION_LEN - 1];
    uint32_t code_size;
    uint8_t raz[12];
    uint64_t code[];
};

union ucd_core_eid_ucode_block_num {
    uint64_t value;
    struct {
        uint64_t ucode_blk : 3;
        uint64_t ucode_len : 1;
        uint64_t raz_4_63 : 60;
    };
};

union aqm_grp_execmsk_lo {
    uint64_t value;
    struct {
        uint64_t exec_0_to_39 : 40;
        uint64_t raz_40_63 : 24;
    };
};

union aqm_grp_execmsk_hi {
    uint64_t value;
    struct {
        uint64_t exec_40_to_79 : 40;
        uint64_t raz_40_63 : 24;
    };
};

union emu_wd_int_ena_w1s {
    uint64_t value;
    struct {
        uint64_t se_wd : 16;
        uint64_t raz1 : 16;
        uint64_t ae_wd : 20;
        uint64_t raz2 : 12;
    } s;
};

union emu_ge_int_ena_w1s {
    uint64_t value;
    struct {
        uint64_t se_ge : 16;
        uint64_t raz_16_31: 16;
        uint64_t ae_ge : 20;
        uint64_t raz_52_63 : 12;
    } s;
};

union nps_pkt_slc_cnts {
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
};

union nps_core_int_active {
    uint64_t value;
    struct {
        uint64_t nps_core : 1;
        uint64_t nps_pkt : 1;
        uint64_t lbm : 1;
        uint64_t zctl: 1;
        uint64_t ucd : 1;
        uint64_t pom : 1;
        uint64_t pem : 1;
        uint64_t lbc : 1;
        uint64_t ilk : 1;
        uint64_t efl : 1;
        uint64_t zqm : 1;
        uint64_t aqm : 1;
        uint64_t bmi : 1;
        uint64_t bmo : 1;
        uint64_t emu : 4;
        uint64_t mbox : 1;
        uint64_t ocla : 1;
        uint64_t raz : 43;
        uint64_t resend : 1;
    } s;
};

union nps_core_int_ena_w1s {
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
};

union aqmq_drbl {
    uint64_t value;
    struct {
        uint64_t dbell_count : 32;
        uint64_t raz_32_63 : 32;
    };
};

union aqmq_qsz {
    uint64_t value;
    struct {
        uint64_t host_queue_size : 32;
        uint64_t raz_32_63 : 32;
    };
};

union aqmq_cmp_thr {
    uint64_t value;
    struct {
        uint64_t commands_completed_threshold : 32;
        uint64_t raz_32_63 : 32;
    };
};

union nps_pkt_in_instr_rsize {
    uint64_t value;
    struct {
        uint64_t rsize : 32;
        uint64_t raz : 32;
    } s;
};

union nps_pkt_in_instr_baoff_dbell {
    uint64_t value;
    struct {
        uint64_t dbell : 32;
        uint64_t aoff : 32;
    } s;
};

union lbc_inval_ctl {
    uint64_t value;
    struct {
        uint64_t raz0 : 1;
        uint64_t cam_inval_start : 1;
        uint64_t raz1 : 6;
        uint64_t wait_timer : 8;
        uint64_t raz2 : 48;
    } s;
};

union lbc_inval_status {
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
};

union emu_se_enable {
    uint64_t value;
    struct {
        uint64_t enable : 16;
        uint64_t raz : 48;
    } s;
};

union emu_ae_enable {
    uint64_t value;
    struct {
        uint64_t enable : 20;
        uint64_t raz : 44;
    } s;
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
    uint64_t regs[0x800000];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if ((addr / 8) < ARRAY_SIZE(s->regs)) {
        val = s->regs[addr / 8];
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if ((addr / 8) < ARRAY_SIZE(s->regs)) {
        bool is_w1s = false;
        
        switch (addr) {
            case POM_INT_ENA_W1S:
            case BMI_INT_ENA_W1S:
            case LBC_INT_ENA_W1S:
            case LBC_PLM_VF1_64_INT_ENA_W1S:
            case LBC_PLM_VF65_128_INT_ENA_W1S:
            case LBC_ELM_VF1_64_INT_ENA_W1S:
            case LBC_ELM_VF65_128_INT_ENA_W1S:
            case NPS_CORE_INT_ENA_W1S:
            case AQM_DBELL_OVF_LO_ENA_W1S:
            case AQM_DBELL_OVF_HI_ENA_W1S:
            case AQM_DMA_RD_ERR_LO_ENA_W1S:
            case AQM_DMA_RD_ERR_HI_ENA_W1S:
            case AQM_EXEC_NA_LO_ENA_W1S:
            case AQM_EXEC_NA_HI_ENA_W1S:
            case AQM_EXEC_ERR_LO_ENA_W1S:
            case AQM_EXEC_ERR_HI_ENA_W1S:
            case NPS_PKT_IN_RERR_LO_ENA_W1S:
            case NPS_PKT_IN_RERR_HI_ENA_W1S:
            case NPS_PKT_IN_ERR_TYPE_ENA_W1S:
            case NPS_PKT_SLC_RERR_HI_ENA_W1S:
            case NPS_PKT_SLC_RERR_LO_ENA_W1S:
            case NPS_PKT_SLC_ERR_TYPE_ENA_W1S:
                is_w1s = true;
                break;
            default:
                for (int i = 0; i < NR_CLUSTERS; i++) {
                    if (addr == EFL_CORE_INT_ENA_W1SX(i) ||
                        addr == EFL_CORE_VF_ERR_INT0_ENA_W1SX(i) ||
                        addr == EFL_CORE_VF_ERR_INT1_ENA_W1SX(i) ||
                        addr == EMU_WD_INT_ENA_W1SX(i) ||
                        addr == EMU_GE_INT_ENA_W1SX(i)) {
                        is_w1s = true;
                        break;
                    }
                }
                break;
        }

        if (is_w1s) {
            s->regs[addr / 8] |= val;
        } else {
            s->regs[addr / 8] = val;
        }

        /* Specific register side-effects */
        if (addr == LBC_INVAL_CTL) {
            union lbc_inval_ctl ctl;
            ctl.value = val;
            if (ctl.s.cam_inval_start) {
                union lbc_inval_status stat;
                stat.value = s->regs[LBC_INVAL_STATUS / 8];
                stat.s.done = 1;
                s->regs[LBC_INVAL_STATUS / 8] = stat.value;
            }
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

    /* Initialize hardware info registers read by nitrox_get_hwinfo */
    union rst_boot rst_boot = {0};
    rst_boot.pnr_mul = 7; /* (7 + 3) * 50 = 500 MHz */
    s->regs[RST_BOOT / 8] = rst_boot.value;

    union fus_dat1 fus_dat1 = {0};
    fus_dat1.nozip = 0;
    fus_dat1.zip_info = 0; /* 0 dead cores -> 5 zip cores */
    s->regs[FUS_DAT1 / 8] = fus_dat1.value;

    for (int i = 0; i < NR_CLUSTERS; i++) {
        union emu_fuse_map emu_fuse = {0};
        emu_fuse.s.valid = 1;
        emu_fuse.s.ae_fuse = 0; /* 0 dead cores -> 20 ae cores */
        emu_fuse.s.se_fuse = 0; /* 0 dead cores -> 16 se cores */
        s->regs[EMU_FUSE_MAPX(i) / 8] = emu_fuse.value;
    }

    /* Initialize CAM invalidation status to ready */
    union lbc_inval_status lbc_stat = {0};
    lbc_stat.s.done = 1;
    lbc_stat.s.cam_rst_rdy = 1;
    s->regs[LBC_INVAL_STATUS / 8] = lbc_stat.value;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, CNN55XX_DEV_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x4000000, .name = "bar0" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X Initialization */
    /* Driver expects up to 193 vectors (Entry 192 is NPS_CORE_INT_ACTIVE) */
    if (msix_init(pdev, 193, &s->bar_regions[0], 0, 0x3F00000, &s->bar_regions[0], 0, 0x3F10000, 0, errp) == 0) {
        s->has_msix = true;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
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
