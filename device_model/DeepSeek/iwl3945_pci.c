/*
 * QEMU 8.2.10 PCI device model for Intel PRO/Wireless 3945ABG/BG (iwlegacy)
 * Derived from Linux driver at drivers/net/wireless/intel/iwlegacy/3945-mac.c
 * Phase 4: Debug & Update - Fixed CSR_GP_CNTRL handshake and reset default
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

#define TYPE_PCIBASE_DEVICE "iwl3945_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL  0x8086
#define PCI_DEVICE_ID_3945ABG 0x4222
#define CLASS_ID PCI_CLASS_NETWORK_OTHER

/* Base addresses */
#define CSR_BASE    (0x000)
#define HBUS_BASE   (0x400)
#define PRPH_BASE   (0x00000)
#define APMG_BASE   (PRPH_BASE + 0x3000)
#define BSM_BASE    (PRPH_BASE + 0x3400)
#define ALM_SCD_BASE (PRPH_BASE + 0x2E00)
#define FH39_MEM_LOWER_BOUND (0x0800)
#define FH39_RCSR_TBL   (FH39_MEM_LOWER_BOUND + 0x400)
#define FH39_RSSR_TBL   (FH39_MEM_LOWER_BOUND + 0x4c0)
#define FH39_TCSR_TBL   (FH39_MEM_LOWER_BOUND + 0x500)
#define FH39_TSSR_TBL   (FH39_MEM_LOWER_BOUND + 0x680)
#define FH39_CBCC_TBL   (FH39_MEM_LOWER_BOUND + 0x140)

/* CSR registers */
#define CSR_HW_IF_CONFIG_REG    (CSR_BASE+0x000)
#define CSR_INT                 (CSR_BASE+0x008)
#define CSR_INT_MASK            (CSR_BASE+0x00c)
#define CSR_FH_INT_STATUS       (CSR_BASE+0x010)
#define CSR_RESET               (CSR_BASE+0x020)
#define CSR_GP_CNTRL            (CSR_BASE+0x024)
#define CSR_EEPROM_REG          (CSR_BASE+0x02c)
#define CSR_EEPROM_GP           (CSR_BASE+0x030)
#define CSR_GIO_REG             (CSR_BASE+0x03C)
#define CSR_UCODE_DRV_GP1       (CSR_BASE+0x054)
#define CSR_UCODE_DRV_GP1_SET   (CSR_BASE+0x058)
#define CSR_UCODE_DRV_GP1_CLR   (CSR_BASE+0x05c)
#define CSR_UCODE_DRV_GP2       (CSR_BASE+0x060)
#define CSR_GPIO_IN             (CSR_BASE+0x018)
#define CSR_GIO_CHICKEN_BITS    (CSR_BASE+0x100)
#define CSR_DBG_HPET_MEM_REG    (CSR_BASE+0x240)
#define CSR_ANA_PLL_CFG         (CSR_BASE+0x20c)

/* HBUS registers */
#define HBUS_TARG_MEM_RADDR     (HBUS_BASE+0x00c)
#define HBUS_TARG_MEM_WADDR     (HBUS_BASE+0x010)
#define HBUS_TARG_MEM_WDAT      (HBUS_BASE+0x018)
#define HBUS_TARG_MEM_RDAT      (HBUS_BASE+0x01c)
#define HBUS_TARG_WRPTR         (HBUS_BASE+0x060)

/* APMG registers */
#define APMG_CLK_EN_REG         (APMG_BASE + 0x0004)
#define APMG_CLK_DIS_REG        (APMG_BASE + 0x0008)
#define APMG_PS_CTRL_REG        (APMG_BASE + 0x000c)
#define APMG_PCIDEV_STT_REG     (APMG_BASE + 0x0010)
#define APMG_RFKILL_REG         (APMG_BASE + 0x0014)
#define APMG_RTC_INT_STT_REG    (APMG_BASE + 0x001c)
#define APMG_RTC_INT_MSK_REG    (APMG_BASE + 0x0020)

/* BSM registers */
#define BSM_WR_CTRL_REG             (BSM_BASE + 0x000)
#define BSM_WR_MEM_SRC_REG          (BSM_BASE + 0x004)
#define BSM_WR_MEM_DST_REG          (BSM_BASE + 0x008)
#define BSM_WR_DWCOUNT_REG          (BSM_BASE + 0x00C)
#define BSM_DRAM_INST_PTR_REG       (BSM_BASE + 0x090)
#define BSM_DRAM_INST_BYTECOUNT_REG (BSM_BASE + 0x094)
#define BSM_DRAM_DATA_PTR_REG       (BSM_BASE + 0x098)
#define BSM_DRAM_DATA_BYTECOUNT_REG (BSM_BASE + 0x09C)

/* ALM_SCD registers */
#define ALM_SCD_MODE_REG            (ALM_SCD_BASE + 0x000)
#define ALM_SCD_ARASTAT_REG         (ALM_SCD_BASE + 0x004)
#define ALM_SCD_TXFACT_REG          (ALM_SCD_BASE + 0x010)
#define ALM_SCD_TXF4MF_REG          (ALM_SCD_BASE + 0x014)
#define ALM_SCD_TXF5MF_REG          (ALM_SCD_BASE + 0x020)
#define ALM_SCD_SBYP_MODE_1_REG     (ALM_SCD_BASE + 0x02C)
#define ALM_SCD_SBYP_MODE_2_REG     (ALM_SCD_BASE + 0x030)

/* FH registers (RCSR, RSSR, TCSR, TSSR, CBCC) */
#define FH39_RCSR_CONFIG(_ch)       (FH39_RCSR(_ch) + 0x00)
#define FH39_RCSR_RBD_BASE(_ch)     (FH39_RCSR(_ch) + 0x04)
#define FH39_RCSR_WPTR(_ch)         (FH39_RCSR(_ch) + 0x20)
#define FH39_RCSR_RPTR_ADDR(_ch)    (FH39_RCSR(_ch) + 0x24)
#define FH39_RSSR_CTRL              (FH39_RSSR_TBL + 0x000)
#define FH39_RSSR_STATUS            (FH39_RSSR_TBL + 0x004)
#define FH39_TCSR_CONFIG(_ch)       (FH39_TCSR(_ch) + 0x00)
#define FH39_TCSR_CREDIT(_ch)       (FH39_TCSR(_ch) + 0x04)
#define FH39_TSSR_CBB_BASE          (FH39_TSSR_TBL + 0x000)
#define FH39_TSSR_MSG_CONFIG        (FH39_TSSR_TBL + 0x008)
#define FH39_TSSR_TX_STATUS         (FH39_TSSR_TBL + 0x010)
#define FH39_CBCC_CTRL(_ch)         (FH39_CBCC(_ch) + 0x00)
#define FH39_CBCC_BASE(_ch)         (FH39_CBCC(_ch) + 0x04)

/* Helper macros for channel indexing */
#define FH39_RCSR(_ch)              (FH39_RCSR_TBL + (_ch) * 0x40)
#define FH39_TCSR(_ch)              (FH39_TCSR_TBL + (_ch) * 0x20)
#define FH39_CBCC(_ch)              (FH39_CBCC_TBL + (_ch) * 0x8)

/* Bit definitions for key registers (subset) */
#define CSR_INT_BIT_ALIVE        (1 << 0)
#define CSR_INT_BIT_WAKEUP       (1 << 1)
#define CSR_INT_BIT_SW_RX        (1 << 3)
#define CSR_INT_BIT_RF_KILL      (1 << 7)
#define CSR_INT_BIT_SW_ERR       (1 << 25)
#define CSR_INT_BIT_SCD          (1 << 26)
#define CSR_INT_BIT_FH_TX        (1 << 27)
#define CSR_INT_BIT_HW_ERR       (1 << 29)
#define CSR_INT_BIT_FH_RX        (1 << 31)
#define CSR_FH_INT_BIT_TX_CHNL0  (1 << 0)
#define CSR_FH_INT_BIT_TX_CHNL1  (1 << 1)
#define CSR_FH_INT_BIT_RX_CHNL0  (1 << 16)
#define CSR_FH_INT_BIT_RX_CHNL1  (1 << 17)
#define CSR_FH_INT_BIT_HI_PRIOR  (1 << 30)
#define CSR39_FH_INT_BIT_TX_CHNL6  (1 << 6)
#define CSR39_FH_INT_BIT_RX_CHNL2  (1 << 18)
#define CSR_INI_SET_MASK \
    (CSR_INT_BIT_FH_RX   | \
     CSR_INT_BIT_HW_ERR  | \
     CSR_INT_BIT_FH_TX   | \
     CSR_INT_BIT_SW_ERR  | \
     CSR_INT_BIT_RF_KILL | \
     CSR_INT_BIT_SW_RX   | \
     CSR_INT_BIT_WAKEUP  | \
     CSR_INT_BIT_ALIVE)
#define CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY        (0x00000001)
#define CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP         (0x00000002)
#define CSR_GP_CNTRL_REG_FLAG_INIT_DONE              (0x00000004)
#define CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ         (0x00000008)
#define CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW          (0x08000000)
#define CSR_RESET_REG_FLAG_NEVO_RESET                (0x00000001)
#define CSR_RESET_REG_FLAG_FORCE_NMI                 (0x00000002)
#define CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP              (0x00000001)
#define CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED            (0x00000004)
#define CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER  (0x20000000)
#define CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX   (0x00800000)
#define CSR_EEPROM_REG_READ_VALID_MSK                (0x00000001)
#define CSR_EEPROM_REG_MSK_ADDR                      (0x0000FFFC)
#define CSR_DBG_HPET_MEM_REG_VAL                     (0xFFFF0000)
#define CSR_GIO_REG_VAL_L0S_ENABLED                  (0x00000002)
#define CSR_GPIO_IN_VAL_VMAIN_PWR_SRC                (0x00000200)
#define CSR_GPIO_IN_BIT_AUX_POWER                    (0x00000200)
#define CSR_UCODE_SW_BIT_RFKILL                      (0x00000002)
#define APMG_PCIDEV_STT_VAL_L1_ACT_DIS               (0x00000800)
#define APMG_CLK_VAL_DMA_CLK_RQT                     (0x00000200)
#define APMG_CLK_VAL_BSM_CLK_RQT                     (0x00000800)
#define APMG_PS_CTRL_VAL_PWR_SRC_VMAIN               (0x00000000)
#define APMG_PS_CTRL_MSK_PWR_SRC                     (0x03000000)
#define APMG_PS_CTRL_VAL_RESET_REQ                   (0x04000000)
#define BSM_WR_CTRL_REG_BIT_START                    (0x80000000)
#define BSM_WR_CTRL_REG_BIT_START_EN                 (0x40000000)
#define FH39_TSSR_TX_STATUS_REG_BIT_NO_PEND_REQ(_ch) (BIT(_ch) << 16)
#define FH39_TSSR_TX_STATUS_REG_BIT_BUFS_EMPTY(_ch)  (BIT(_ch) << 24)
#define FH39_RSSR_CHNL0_RX_STATUS_CHNL_IDLE          (0x01000000)
#define FH39_RSCSR_CHNL0_WPTR                        (FH39_RCSR_WPTR(0))
#define FH39_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE  (0x80000000)
#define FH39_TCSR_TX_CONFIG_REG_VAL_MSG_MODE_TXF     (0x00000000)
#define FH39_TCSR_TX_CONFIG_REG_VAL_CIRQ_HOST_IFTFD  (0x00200000)
#define FH39_TCSR_TX_CONFIG_REG_VAL_CIRQ_RTC_NOINT   (0x00000000)
#define FH39_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE_VAL (0x00000008)
#define FH39_RCSR_RX_CONFIG_REG_VAL_RDRBD_EN_ENABLE  (0x20000000)
#define FH39_RCSR_RX_CONFIG_REG_VAL_DMA_CHNL_EN_ENABLE (0x80000000)
#define FH39_RCSR_RX_CONFIG_REG_VAL_MSG_MODE_FH      (0x00000000)
#define FH39_RCSR_RX_CONFIG_REG_VAL_IRQ_DEST_INT_HOST (0x00001000)
#define FH39_RCSR_RX_CONFIG_REG_VAL_MAX_FRAG_SIZE_128 (0x01000000)
#define FH39_RCSR_RX_CONFIG_REG_BIT_WR_STTS_EN       (0x08000000)
#define FH39_RCSR_RX_CONFIG_REG_POS_RBDC_SIZE        (20)
#define FH39_RCSR_RX_CONFIG_REG_POS_IRQ_RBTH         (4)

#define CSR39_HW_IF_CONFIG_REG_BITS_SILICON_TYPE_A    (0x00000000)
#define CSR39_HW_IF_CONFIG_REG_BITS_SILICON_TYPE_B    (0x00001000)
#define CSR39_HW_IF_CONFIG_REG_BIT_3945_MM            (0x00000200)
#define CSR39_HW_IF_CONFIG_REG_BIT_3945_MB            (0x00000100)
#define CSR39_HW_IF_CONFIG_REG_BIT_BOARD_TYPE         (0x00000800)
#define CSR39_HW_IF_CONFIG_REG_BIT_SKU_MRC            (0x00000400)
#define CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A         (0x00080000)
#define PCI_CFG_REV_ID_BIT_RTP                        (0x80)
#define PCI_CFG_REV_ID_BIT_BASIC_SKU                  (0x40)

/* Bit macros */
#define BIT(nr) (1UL << (nr))

/* EEPROM signature macros */
#define CSR_EEPROM_GP_VALID_MSK                          (0x00000007)
#define CSR_EEPROM_GP_GOOD_SIG_EEP_LESS_THAN_4K          (0x00000002)
#define CSR_EEPROM_GP_GOOD_SIG_EEP_MORE_THAN_4K          (0x00000004)

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
    /* Interrupt status and mask */
    uint32_t intr_status;       /* CSR_INT */
    uint32_t intr_mask;         /* CSR_INT_MASK */
    uint32_t intr_fh_status;    /* CSR_FH_INT_STATUS */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t *mmio;  /* Pointer to allocated MMIO space */

    /* DMA Context */
    /* DMA state not needed for probe; driver uses mmio array to store DMA regs */

    /* #Status_Stru# */
    uint32_t device_status;  /* S_INIT, S_ALIVE, etc. */

    /* #Probe_Reset_Stru# */
    bool reset_in_progress;

    /* #Pow_Man_Stru# */
    bool power_mgmt;

    /* EEPROM simulation */
    uint8_t *eeprom;
    uint32_t eeprom_size;
    bool eeprom_valid;
    uint32_t eeprom_latch_addr;
    uint16_t eeprom_data;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_mask;

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA not required for probe; no-op */
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > 0x4000) {
        return ~0ULL;
    }

    if (size != 4) {
        return ~0ULL;
    }

    if ((addr & 3) == 0) {
        switch (addr) {
        case CSR_EEPROM_REG:
            if (s->eeprom_valid) {
                val = ((uint32_t)s->eeprom_data << 16) | CSR_EEPROM_REG_READ_VALID_MSK;
                s->eeprom_valid = false;
            } else {
                val = 0;
            }
            break;
        default:
            val = s->mmio[addr / 4];
            break;
        }
    }

    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > 0x4000) {
        return;
    }

    if (size != 4) {
        return;
    }

    if ((addr & 3) == 0) {
        uint32_t idx = addr / 4;
        uint32_t new_val;
        uint32_t current;

        switch (addr) {
        case CSR_RESET:
            if (val & (CSR_RESET_REG_FLAG_NEVO_RESET | CSR_RESET_REG_FLAG_FORCE_NMI)) {
                /* Reset the device: clear all registers and set defaults */
                memset(s->mmio, 0, 0x4000);
                s->intr_status = 0;
                s->intr_mask = 0;
                s->intr_fh_status = 0;
                /* Set power-on defaults */
                s->mmio[CSR_HW_IF_CONFIG_REG / 4] =
                    CSR39_HW_IF_CONFIG_REG_BIT_3945_MM |
                    CSR39_HW_IF_CONFIG_REG_BIT_BOARD_TYPE;
                s->mmio[CSR_GP_CNTRL / 4] = CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW |
                                           CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP;
                s->mmio[CSR_GIO_CHICKEN_BITS / 4] =
                    CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER |
                    CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX;
                s->mmio[CSR_DBG_HPET_MEM_REG / 4] = CSR_DBG_HPET_MEM_REG_VAL;
                s->mmio[CSR_ANA_PLL_CFG / 4] = 0x00000000;
                s->eeprom_valid = false;
                s->mmio[CSR_EEPROM_GP / 4] = CSR_EEPROM_GP_GOOD_SIG_EEP_MORE_THAN_4K;
            } else {
                s->mmio[idx] = val;
            }
            break;
        case CSR_GP_CNTRL:
        {
            uint32_t new = val; /* driver's desired value */
            if (val & CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ) {
                new |= CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY;
                new &= ~CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP;
            } else {
                new &= ~CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY;
                new |= CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP;
            }
            s->mmio[idx] = new;
            break;
        }
        case CSR_INT:
            s->intr_status &= ~val; /* W1C */
            pcibase_update_irq(s);
            break;
        case CSR_FH_INT_STATUS:
            s->intr_fh_status &= ~val; /* W1C */
            pcibase_update_irq(s);
            break;
        case CSR_INT_MASK:
            s->intr_mask = val;
            pcibase_update_irq(s);
            break;
        case CSR_EEPROM_REG:
            {
                uint32_t byte_addr = val & CSR_EEPROM_REG_MSK_ADDR;
                byte_addr &= 0x3FFF;
                if (byte_addr + 1 < s->eeprom_size) {
                    s->eeprom_data = (uint16_t)s->eeprom[byte_addr] |
                                     ((uint16_t)s->eeprom[byte_addr + 1] << 8);
                    s->eeprom_valid = true;
                } else {
                    s->eeprom_valid = false;
                }
                s->mmio[idx] = val;
            }
            break;
        default:
            s->mmio[idx] = val;
            break;
        }
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

    memset(s->mmio, 0, 0x4000);
    s->intr_status = 0;
    s->intr_mask = 0;
    s->intr_fh_status = 0;
    s->mmio[CSR_HW_IF_CONFIG_REG / 4] =
        CSR39_HW_IF_CONFIG_REG_BIT_3945_MM |
        CSR39_HW_IF_CONFIG_REG_BIT_BOARD_TYPE;
    s->mmio[CSR_GP_CNTRL / 4] = CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW |
                               CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP;
    s->mmio[CSR_GIO_CHICKEN_BITS / 4] =
        CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER |
        CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX;
    s->mmio[CSR_DBG_HPET_MEM_REG / 4] = CSR_DBG_HPET_MEM_REG_VAL;
    s->mmio[CSR_ANA_PLL_CFG / 4] = 0x00000000;
    s->mmio[CSR_EEPROM_GP / 4] = CSR_EEPROM_GP_GOOD_SIG_EEP_MORE_THAN_4K;
    s->eeprom_valid = false;
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
        /* Not used */
    } else if (bi->type == BAR_TYPE_RAM) {
        /* Not used */
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_3945ABG);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
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
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "iwl3945-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);

    s->mmio = g_malloc0(0x4000);
    s->mmio[CSR_HW_IF_CONFIG_REG / 4] =
        CSR39_HW_IF_CONFIG_REG_BIT_3945_MM |
        CSR39_HW_IF_CONFIG_REG_BIT_BOARD_TYPE;
    s->mmio[CSR_GP_CNTRL / 4] = CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW |
                               CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP;
    s->mmio[CSR_GIO_CHICKEN_BITS / 4] =
        CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER |
        CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX;
    s->mmio[CSR_DBG_HPET_MEM_REG / 4] = CSR_DBG_HPET_MEM_REG_VAL;
    s->mmio[CSR_ANA_PLL_CFG / 4] = 0x00000000;

    s->eeprom_size = 4096;
    s->eeprom = g_malloc0(s->eeprom_size);
    /* Pre-fill EEPROM with valid version and MAC address */
    s->eeprom[42] = 0x00; s->eeprom[43] = 0x11;
    s->eeprom[44] = 0x22; s->eeprom[45] = 0x33;
    s->eeprom[46] = 0x44; s->eeprom[47] = 0x55;
    s->eeprom[136] = 0x01; s->eeprom[137] = 0x00; /* version = 0x0001 little-endian */
    s->mmio[CSR_EEPROM_GP / 4] = CSR_EEPROM_GP_GOOD_SIG_EEP_MORE_THAN_4K;
    s->eeprom_valid = false;
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

    g_free(s->mmio);
    s->mmio = NULL;
    g_free(s->eeprom);
    s->eeprom = NULL;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "iwl3945_pci",
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
