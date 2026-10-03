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

#define TYPE_PCIBASE_DEVICE "rtsx_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Direct MMIO register offsets for RTS5209-like devices */
#define RTSX_BIPR       0x00
#define RTSX_BIER       0x04
#define RTSX_HAIMR      0x10
#define RTSX_HCBAR      0x20
#define RTSX_HCBCTLR    0x24
#define RTSX_HDBAR      0x28
#define RTSX_HDBCTLR    0x2C

/* Interrupt bit definitions */
#define SD_EXIST         0x01
#define MS_EXIST         0x02
#define TRANS_OK_INT     0x04
#define TRANS_FAIL_INT   0x08
#define DELINK_INT       0x10
#define SD_OC_INT        0x20
#define SD_OVP_INT       0x40
#define SD_INT           0x80
#define MS_INT           0x100
#define NEED_COMPLETE_INT (TRANS_OK_INT | TRANS_FAIL_INT)
#define CARD_EXIST       (SD_EXIST | MS_EXIST)

/* Command types for HAIMR-based indirect access */
#define READ_REG_CMD    0
#define WRITE_REG_CMD   1
#define CHECK_REG_CMD   2

/* Buffer sizes mirroring driver allocations */
#define HOST_CMDS_BUF_LEN   1024
#define RTSX_RESV_BUF_LEN   4096

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10EC
#define DEVICE_ID 0x5209
#define CLASS_ID PCI_CLASS_OTHERS

#define MASK_8_BIT_DEF          0xFF
#define MAX_DIV_N_PCR           208
#define MIN_DIV_N_PCR           80
#define SSC_CLOCK_STABLE_WAIT   130
#define RTS5261_FW_STATUS       0xFF56
#define RTS5261_EXPRESS_LINK_FAIL_MASK  (0x01<<7)
#define RTS5261_SSC_DEPTH_2M    0x03
#define RTS5228_LDO1_SR_TIME_MASK       (0x03<<6)
#define RTS5228_LDO1_SR_0_5             (0x02<<6)
#define RTS5264_SSC_DEPTH_2M    0x03
#define RTS5261_REG_FPDCTL      0xFF60
#define RTS5228_LDO1_CFG1               0xFF73
#define RTS5228_SSC_DEPTH_2M    0x03
#define RTS5264_IC_VER_A        0
#define RTS5228_SSC_DEPTH_512K  0x05
#define RTS5228_SSC_DEPTH_4M    0x02
#define RTS5228_SSC_DEPTH_1M    0x04
#define RTS5228_SSC_DEPTH_8M    0x01
#define RTS5264_SSC_DEPTH_4M    0x02
#define RTS5264_SSC_DEPTH_512K  0x05
#define RTS5264_REG_BIG_KVCO    0x04
#define RTS5264_REG_BIG_KVCO_A  0x20
#define RTS5264_CARD_CLK_SRC2   0xFC2F
#define RTS5264_SYS_DUMMY_1     0xFC35
#define RTS5264_SSC_DEPTH_1M    0x04
#define RTS5264_SSC_DEPTH_8M    0x01
#define RTS5261_SSC_DEPTH_4M    0x02
#define RTS5261_SSC_DEPTH_512K  0x05
#define RTS5261_SSC_DEPTH_1M    0x04
#define RTS5261_SSC_DEPTH_8M    0x01
#define L1_SNOOZE_DELAY_DEF     1
#define LTR_ACTIVE_LATENCY_DEF  0x883C
#define LTR_L1OFF_LATENCY_DEF   0x9003
#define LTR_L1OFF_SNOOZE_SSPWRGATE_5250_DEF 0xF8
#define RTS524A_PM_CTRL3        0xFF7E
#define LTR_L1OFF_SSPWRGATE_5250_DEF         0xFF
#define LTR_IDLE_LATENCY_DEF    0x892C
#define RTS5228_AUTOLOAD_CFG3           0xFF7E
#define RTS5228_LDO1_OCP_THD_930        (0x03<<5)
#define RTS5264_LDO1_OCP_THD_1150       (0x04<<5)
#define RTS5264_AUTOLOAD_CFG3   0xFF7E
#define RTS5264_FW_STATUS       0xFF56
#define RTS5264_EXPRESS_LINK_FAIL_MASK  (0x01<<7)
#define RTS525A_OCP_THD_800     0x05
#define RTS524A_OCP_THD_800     0x04
#define set_pull_ctrl_tables(pcr, __device)                             \
do {                                                                    \
        pcr->sd_pull_ctl_enable_tbl  = __device##_sd_pull_ctl_enable_tbl;  \
        pcr->sd_pull_ctl_disable_tbl = __device##_sd_pull_ctl_disable_tbl; \
        pcr->ms_pull_ctl_enable_tbl  = __device##_ms_pull_ctl_enable_tbl;  \
        pcr->ms_pull_ctl_disable_tbl = __device##_ms_pull_ctl_disable_tbl; \
} while (0)
#define RTS522A_OCP_THD_800     0x06
#define RTS522A_PM_CTRL3        0xFF7E
#define RTS5261_LDO1_OCP_THD_1040       (0x05<<5)
#define RTS5261_AUTOLOAD_CFG3   0xFF7E
#define LTR_L1OFF_SSPWRGATE_5249_DEF            0xAF
#define LTR_L1OFF_SNOOZE_SSPWRGATE_5249_DEF     0xAC
#define rtsx_reg_to_rtd3_uhsii(reg)             ((reg) & 0x04)
#define rtsx_reg_to_sd30_drive_sel_1v8(reg)     (((reg) >> 26) & 0x03)
#define rtsx_vendor_setting_valid(reg)          (!((reg) & 0x1000000))
#define rtsx_reg_to_aspm(reg)                   (((reg) >> 28) & 0x03)
#define rtsx_reg_check_reverse_socket(reg)      ((reg) & 0x4000)
#define rtsx_reg_check_wp_reverse(reg)          ((reg) & 0x400000)
#define rtsx_reg_to_sd30_drive_sel_3v3(reg)     (((reg) >> 5) & 0x03)
#define rtsx_check_mmc_support(reg)             ((reg) & 0x10)
#define rtsx_reg_check_cd_reverse(reg)          ((reg) & 0x800000)
#define rtsx_reg_to_card_drive_sel(reg)         ((((reg) >> 25) & 0x01) << 6)
#define RTS5228_LDO1_OCP_LMT_EN                 (0x01<<1)
#define RTS5228_LDO1_OCP_EN                     (0x01<<4)
#define RTS5228_LDO1_CFG0                       0xFF72
#define RTS5228_LDO1_LMT_THD_1500               (0x02<<2)
#define RTS5228_LDO1_OCP_THD_MASK               (0x07<<5)
#define RTS5228_LDO1_OCP_LMT_THD_MASK           (0x03<<2)
#define RTS5228_LDO1233318_POW_CTL              0xFF70
#define RTS5228_LDO_POWERON_MASK                (0x0F<<0)
#define RTS5228_LDO1_SOFTSTART                  (0x01<<0)
#define RTS5228_LDO1_33                         (0x07<<1)
#define RTS5228_LDO1_TUNE_MASK                  (0x07<<1)
#define RTS5228_LDO3318_POWERON                 (0x01<<3)
#define RTS5228_LDO1_FULLON                     (0x03<<0)
#define RTS5228_LDO1_POWERON_MASK               (0x03<<0)
#define FORCE_PM_VALUE                          0x10
#define FORCE_PM_CONTROL                        0x20
#define RTS5228_REG_PME_FORCE_CTL               0xFF78
#define RTS5228_AUTOLOAD_CFG1                   0xFF7C
#define RTS5228_PUPDC                           (0x01<<5)
#define RTS5228_DV3318_CFG                      0xFF71
#define RTS5228_DV3318_18                       (0x02<<4)
#define RTS5228_CARD_PWR_CTL                    0xFD50
#define RTS5228_DV3318_33                       (0x07<<4)
#define RTS5228_DV3318_TUNE_MASK                (0x07<<4)
#define rtsx_reg_to_rtd3(reg)                   ((reg) & 0x02)
#define SD_VDD3_OCP_INT_CLR                     0x02
#define RTS5264_OVP_INT_CLR                     0x02
#define RTS5264_OCP_VDD3_CTL                    0xFD89
#define SD_VDD3_OC_CLR                          0x01
#define RTS5264_OVP_CTL                         0xFD8D
#define RTS5264_OVP_CLR                         0x01
#define RTS5264_REG_LDO12_CFG                   0xFF6E
#define RTS5264_POW_CKMUX                       0x40
#define RTS5264_AUX_CLK_16M_EN                  (1 << 5)
#define RTS5264_PWR_CUT                         0xFF81
#define RTS5264_F_HIGH_RC_400K                  (0 << 4)
#define RTS5264_DAT_OE_EARLY_CYCLE_MASK         0x06
#define RTS5264_AUTOLOAD_CFG4                   0xFF7F
#define RTS5264_AUTOLOAD_CFG1                   0xFF7C
#define RTS5264_DAT_OE_EARLY_EN                 0x01
#define RTS5264_CMD_OE_EARLY_EN                 0x01
#define RTS5264_F_HIGH_RC_MASK                  (1 << 4)
#define RTS5264_DAT_OE_START_EARLY              0xFDCC
#define RTS5264_FW_CTL                          0xFF5F
#define RTS5264_CMD_OE_START_EARLY              0xFDCB
#define RTS5264_LDO12_SR_0_0_MS                 (0x00<<6)
#define RTS5264_CMD_OE_EARLY_CYCLE_MASK         0x06
#define RTS5264_INFORM_RTD3_COLD                (0x01<<5)
#define REG_EFUSE_POWEROFF                      0x00
#define REG_EFUSE_POWER_MASK                    0x03
#define RTS5264_REG_PME_FORCE_CTL               0xFF78
#define RTS5264_CFG_MEM_PD                      0xF0
#define RTS5264_LDO12_SR_MASK                   (0x03<<6)
#define RTS5264_FORCE_PRSNT_LOW                 (1 << 6)
#define RTS5264_CKMUX_MBIAS_PWR                 0xFF8B
#define SD_VDD3_OC_EVER                         0x02
#define RTS5264_OVP_NOW                         0x04
#define SD_VDD3_OC_NOW                          0x04
#define RTS5264_OVP_EVER                        0x02
#define SD_VDD3_DETECT_EN                       0x08
#define RTS5264_OVP_DETECT_EN                   0x08
#define RTS5264_LDO3_OCP_LMT_EN                 (0x01<<1)
#define RTS5264_LDO2_OCP_LMT_EN                 (0x01<<1)
#define RTS5264_LDO1_CFG0                       0xFF72
#define SD_VDD3_OCP_INT_EN                      0x04
#define RTS5264_OVP_DET                         0xFF8A
#define RTS5264_LDO3_OCP_EN                     (0x01<<4)
#define RTS5264_LDO2_CFG0                       0xFF74
#define RTS5264_LDO3_CFG0                       0xFF76
#define RTS5264_POW_VDET                        0x04
#define RTS5264_OVP_INT_EN                      0x04
#define RTS5264_LDO1_OCP_LMT_EN                 (0x01 << 1)
#define RTS5264_LDO2_OCP_EN                     (0x01<<4)
#define RTS5264_LDO1_OCP_EN                     (0x01 << 4)
#define RTS5264_LDO1_POWERON                    (0x01<<0)
#define RTS5264_LDO1_33                         (0x07<<1)
#define RTS5264_LDO1_CFG1                       0xFF73
#define RTS5264_LDO3318_POWERON                 (0x01<<3)
#define RTS5264_LDO1_TUNE_MASK                  (0x07<<1)
#define RTS5264_LDO1233318_POW_CTL              0xFF70
#define RTS5264_LDO_POWERON_MASK                (0x0F<<0)
#define RTS5264_REG_FPDCTL                      0xFF60
#define RTS5264_LDO2_OCP_LMT_THD_MASK           (0x03<<2)
#define RTS5264_LDO1_OCP_LMT_THD_MASK           (0x03 << 2)
#define RTS5264_LDO2_LMT_THD_2000               (0x03<<2)
#define RTS5264_LDO3_OCP_THD_MASK               (0x07<<5)
#define RTS5264_LDO2_OCP_THD_950                (0x03<<5)
#define RTS5264_LDO2_OCP_THD_MASK               (0x07<<5)
#define RTS5264_LDO1_OCP_THD_MASK               (0x07 << 5)
#define RTS5264_LDO1_LMT_THD_2000               (0x03<<2)
#define RTS5264_LDO3_LMT_THD_1500               (0x03<<2)
#define RTS5264_LDO3_OCP_THD_710                (0x03<<5)
#define RTS5264_TUNE_VROV_1V6                   0x01
#define RTS5264_TUNE_VROV_MASK                  0x03
#define RTS5264_LDO3_OCP_LMT_THD_MASK           (0x03<<2)
#define RTS5264_IC_VER_B                        2
#define RTS5264_DV3318_18                       (0x02<<4)
#define RTS5264_PUPDC                           (0x01<<5)
#define RTS5264_CARD_PWR_CTL                    0xFD50
#define RTS5264_TUNE_REF_LDO3318_DFT            (0x02<<6)
#define RTS5264_DV3318_CFG                      0xFF71
#define RTS5264_DV3318_TUNE_MASK                (0x07<<4)
#define RTS5264_DV3318_33                       (0x07<<4)
#define RTS5264_TUNE_REF_LDO3318                (0x03<<6)
#define RTS525A_CFG_MEM_PD                      0xF0
#define RTS5250_CLK_CFG3                        0xFF79
#define RTS524A_AUTOLOAD_CFG1                   0xFF7C
#define RTS524A_PME_FORCE_CTL                   0xFF78
#define rtl8411_reg_to_sd30_drive_sel_3v3(reg)  (((reg) >> 5) & 0x07)
#define RTS522A_AUTOLOAD_CFG1                   0xFF7C
#define RTS522A_PME_FORCE_CTL                   0xFF78
#define rtl8411b_reg_to_sd30_drive_sel_3v3(reg) ((reg) & 0x03)
#define RTS5261_LDO1_TUNE_MASK                  (0x07<<1)
#define RTS5261_LDO1_CFG1                       0xFF73
#define RTS5261_LDO1233318_POW_CTL              0xFF70
#define RTS5261_LDO3318_POWERON                 (0x01<<3)
#define RTS5261_LDO1_POWERON                    (0x01<<0)
#define RTS5261_LDO1_33                         (0x07<<1)
#define RTS5261_LDO_POWERON_MASK                (0x0F<<0)
#define RTS5261_FORCE_PRSNT_LOW                 (1 << 6)
#define RTS5261_AUTOLOAD_CFG4                   0xFF7F
#define RTS5261_AUTOLOAD_CFG1                   0xFF7C
#define RTS5261_REG_PME_FORCE_CTL               0xFF78
#define RTS5261_INFORM_RTD3_COLD                (0x01<<5)
#define RTS5261_AUX_CLK_16M_EN                  (1 << 5)
#define RTS5261_FW_CTL                          0xFF5F
#define RTS5261_DV3318_TUNE_MASK                (0x07<<4)
#define RTS5261_DV3318_33                       (0x07<<4)
#define RTS5261_PUPDC                           (0x01<<5)
#define RTS5261_DV3318_18                       (0x02<<4)
#define RTS5261_DV3318_CFG                      0xFF71
#define RTS5261_CARD_PWR_CTL                    0xFD50
#define RTS5261_LDO1_LMT_THD_2000               (0x03<<2)
#define CMD_TIMEOUT_DEF                         100
#define rts5209_reg_to_card_drive_sel(reg)      ((reg) >> 8)
#define rts5209_reg_check_ms_pmos(reg)          (!((reg) & 0x08))
#define rts5209_vendor_setting1_valid(reg)      (!((reg) & 0x80))
#define rts5209_reg_to_sd30_drive_sel_3v3(reg)  ((reg) & 0x07)
#define rts5209_vendor_setting2_valid(reg)      ((reg) & 0x80)
#define rts5209_reg_to_sd30_drive_sel_1v8(reg)  (((reg) >> 3) & 0x07)
#define rts5209_reg_to_aspm(reg)                (((reg) >> 5) & 0x03)
#define RTS5264_EFUSE_READ_DATA                 0xFC34
#define REG_EFUSE_POWERON                       0x03
#define RTS5264_EFUSE_MODE_MASK                 0x40
#define REG_EFUSE_POR                           0x04
#define RTS5264_FW_CFG_INFO2                    0xFF52
#define RTS5264_EFUSE_ENABLE                    0x80
#define RTS5264_EFUSE_CTL                       0xFC30
#define RTS5264_EFUSE_ADDR                      0xFC31
#define RTS5264_EFUSE_ADDR_MASK                 0x3F
#define RTS5264_OVP_STS                         0xFD8E
#define RTS5264_OCP_VDD3_STS                    0xFD8A
#define RTS5261_EFUSE_ADDR_MASK                 0x3F
#define RTS5261_EFUSE_ENABLE                    0x80
#define RTS5261_EFUSE_ADDR                      0xFC31
#define RTS5261_EFUSE_READ_DATA                 0xFC34
#define RTS5261_EFUSE_MODE_MASK                 0x40
#define RTS5261_EFUSE_CTL                       0xFC30
#define RTS525A_CLEAR_BIOS_FLAG                 0x00
#define RTS525A_BIOS_CFG                        0xFF2D
#define RTS525A_LOAD_BIOS_FLAG                  0x01
#define REG_EFUSE_ADD_MASK                      0x3F
#define RTS525A_EFUSE_CTL                       0xFC32
#define REG_EFUSE_BYPASS                        0x08
#define RTS525A_EFUSE_ADD                       0xFC33
#define REG_EFUSE_ENABLE                        0x80
#define REG_EFUSE_MODE                          0x40
#define RTS525A_EFUSE_DATA                      0xFC35

/* Additional definitions from struct rtsx_pcr */
#define EXTRA_CAPS_SD_SDR50             (1 << 0)
#define EXTRA_CAPS_SD_SDR104            (1 << 1)
#define EXTRA_CAPS_SD_DDR50             (1 << 2)
#define EXTRA_CAPS_MMC_HSDDR            (1 << 3)
#define EXTRA_CAPS_MMC_HS200            (1 << 4)
#define EXTRA_CAPS_MMC_8BIT             (1 << 5)
#define EXTRA_CAPS_NO_MMC               (1 << 7)
#define EXTRA_CAPS_SD_EXPRESS           (1 << 8)
#define IC_VER_A                        0
#define IC_VER_B                        1
#define IC_VER_C                        2
#define IC_VER_D                        3
#define ASPM_L1_EN                      0x02
#define PCR_MS_PMOS                     (1 << 0)
#define PCR_REVERSE_SOCKET              (1 << 1)

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef enum {
    ASPM_MODE_CFG,
    ASPM_MODE_REG
} ASPM_MODE;

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
    uint32_t intr_status;   /* Interrupt status (BIPR) */
    uint32_t intr_mask;     /* Interrupt mask (BIER) */

    /* Hardware Register Shadows */
    uint8_t regs[0x10000];  /* Internal indirect registers */
    uint32_t haimr_shadow;
    uint64_t hcbar;
    uint32_t hcbctlr;
    uint64_t hdbar;
    uint32_t hdbctlr;

    /* DMA Context (unused for basic probe, kept for expansion) */
    struct {
        dma_addr_t base;
        uint32_t cnt;
        uint32_t status;
    } dma;

    uint32_t status;        /* Operational status flags */
    int reset_step;         /* State used to handle reset sequences */
    uint8_t pm_state;       /* Power management state (D0-D3) */
};

static inline uint8_t rtsx_internal_read(PCIBaseState *s, uint16_t addr)
{
    if (addr < 0x10000) {
        return s->regs[addr];
    }
    return 0;
}

static inline void rtsx_internal_write(PCIBaseState *s, uint16_t addr, uint8_t data)
{
    if (addr < 0x10000) {
        s->regs[addr] = data;
    }
}

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_mask;
    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static void haimr_process(PCIBaseState *s, uint32_t val)
{
    if (val & (1u << 31)) {
        bool is_read = (val & (1u << 30)) != 0;
        uint16_t addr = (val >> 16) & 0xFFFF;
        uint8_t mask = (val >> 8) & 0xFF;
        uint8_t data = val & 0xFF;
        if (is_read) {
            uint8_t read_val = rtsx_internal_read(s, addr);
            s->haimr_shadow = (val & 0xFFFFFF00) | read_val;
            s->haimr_shadow &= ~(1u << 31);
        } else {
            uint8_t old = rtsx_internal_read(s, addr);
            uint8_t new_val = (old & ~mask) | (data & mask);
            rtsx_internal_write(s, addr, new_val);
            s->haimr_shadow = (val & 0xFFFFFF00) | new_val;
            s->haimr_shadow &= ~(1u << 31);
        }
    } else {
        s->haimr_shadow = val;
    }
}

static void process_commands(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    dma_addr_t addr = s->hcbar;
    uint32_t count = (s->hcbctlr & 0x00FFFFFF) / 4;
    uint32_t cmd, data;

    for (uint32_t i = 0; i < count; i++) {
        dma_addr_t cmd_addr = addr + i * 4;
        pci_dma_read(pdev, cmd_addr, &cmd, 4);
        uint8_t cmd_type = (cmd >> 30) & 0x03;
        uint16_t reg_addr = (cmd >> 16) & 0xFFFF;
        uint8_t mask = (cmd >> 8) & 0xFF;
        uint8_t val = cmd & 0xFF;
        if (cmd_type == READ_REG_CMD) {
            data = rtsx_internal_read(s, reg_addr);
            cmd = (cmd & 0xFFFFFF00) | data;
            pci_dma_write(pdev, cmd_addr, &cmd, 4);
        } else if (cmd_type == WRITE_REG_CMD) {
            uint8_t old = rtsx_internal_read(s, reg_addr);
            uint8_t new_val = (old & ~mask) | (val & mask);
            rtsx_internal_write(s, reg_addr, new_val);
        }
    }

    s->intr_status |= TRANS_OK_INT;
    pcibase_update_irq(s);
    s->hcbctlr &= ~(1u << 31);
}

static void process_dma(PCIBaseState *s)
{
    s->intr_status |= TRANS_OK_INT;
    pcibase_update_irq(s);
    s->hdbctlr &= ~(1u << 31);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case RTSX_BIPR:
        val = s->intr_status;
        break;
    case RTSX_BIER:
        val = s->intr_mask;
        break;
    case RTSX_HAIMR:
        val = s->haimr_shadow;
        break;
    case RTSX_HCBAR:
        val = s->hcbar;
        break;
    case RTSX_HCBCTLR:
        val = s->hcbctlr;
        break;
    case RTSX_HDBAR:
        val = s->hdbar;
        break;
    case RTSX_HDBCTLR:
        val = s->hdbctlr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "rtsx_pci: unsupported MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case RTSX_BIPR:
        s->intr_status &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case RTSX_BIER:
        s->intr_mask = val & 0x7FFFFF;
        pcibase_update_irq(s);
        break;
    case RTSX_HAIMR:
        haimr_process(s, (uint32_t)val);
        break;
    case RTSX_HCBAR:
        s->hcbar = val;
        break;
    case RTSX_HCBCTLR:
        s->hcbctlr = val & 0xFFFFFFFF;
        if (val & (1u << 31)) {
            process_commands(s);
        }
        break;
    case RTSX_HDBAR:
        s->hdbar = val;
        break;
    case RTSX_HDBCTLR:
        s->hdbctlr = val & 0xFFFFFFFF;
        if (val & (1u << 31)) {
            process_dma(s);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "rtsx_pci: unsupported MMIO write at 0x%" HWADDR_PRIx "\n", addr);
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

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->haimr_shadow = 0;
    s->hcbar = 0;
    s->hcbctlr = 0;
    s->hdbar = 0;
    s->hdbctlr = 0;
    s->status = 0;
    s->reset_step = 0;
    s->pm_state = 0;
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: BAR0, MMIO, size 0x100 */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x100,
        .name = "bar0"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;
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
    .name = TYPE_PCIBASE_DEVICE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
        VMSTATE_UINT32(haimr_shadow, PCIBaseState),
        VMSTATE_UINT64(hcbar, PCIBaseState),
        VMSTATE_UINT32(hcbctlr, PCIBaseState),
        VMSTATE_UINT64(hdbar, PCIBaseState),
        VMSTATE_UINT32(hdbctlr, PCIBaseState),
        VMSTATE_BUFFER(regs, PCIBaseState),
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
