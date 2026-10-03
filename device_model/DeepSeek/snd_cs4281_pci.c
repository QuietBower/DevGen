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

#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "snd_cs4281_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_CIRRUS 0x1013
#define CS4281_DEVICE_ID 0x6005
#define CS4281_CLASS_ID PCI_CLASS_MULTIMEDIA_AUDIO

#define CS4281_BA0_SIZE 0x1000
#define CS4281_BA1_SIZE 0x10000

/* Register offsets */
#define BA0_HISR         0x0000
#define BA0_HICR         0x0008
#define BA0_HIMR         0x000c
#define BA0_IIER         0x0010
#define BA0_HDSR0        0x00f0
#define BA0_HDSR1        0x00f4
#define BA0_HDSR2        0x00f8
#define BA0_HDSR3        0x00fc
#define BA0_DCA0         0x0110
#define BA0_DCC0         0x0114
#define BA0_DBA0         0x0118
#define BA0_DBC0         0x011c
#define BA0_DCA1         0x0120
#define BA0_DCC1         0x0124
#define BA0_DBA1         0x0128
#define BA0_DBC1         0x012c
#define BA0_DCA2         0x0130
#define BA0_DCC2         0x0134
#define BA0_DBA2         0x0138
#define BA0_DBC2         0x013c
#define BA0_DCA3         0x0140
#define BA0_DCC3         0x0144
#define BA0_DBA3         0x0148
#define BA0_DBC3         0x014c
#define BA0_DMR0         0x0150
#define BA0_DCR0         0x0154
#define BA0_DMR1         0x0158
#define BA0_DCR1         0x015c
#define BA0_DMR2         0x0160
#define BA0_DCR2         0x0164
#define BA0_DMR3         0x0168
#define BA0_DCR3         0x016c
#define BA0_FCR0         0x0180
#define BA0_FCR1         0x0184
#define BA0_FCR2         0x0188
#define BA0_FCR3         0x018c
#define BA0_FPDR0        0x0190
#define BA0_FPDR1        0x0194
#define BA0_FPDR2        0x0198
#define BA0_FPDR3        0x019c
#define BA0_FCHS         0x020c
#define BA0_FSIC0        0x0210
#define BA0_FSIC1        0x0214
#define BA0_FSIC2        0x0218
#define BA0_FSIC3        0x021c
#define BA0_PMCS         0x0344
#define BA0_CWPR         0x03e0
#define BA0_EPPMC        0x03e4
#define BA0_SPMC         0x03ec
#define BA0_CFLR         0x03f0
#define BA0_IISR         0x03f4
#define BA0_TMS          0x03f8
#define BA0_SSVID        0x03fc
#define BA0_GPIOR        0x03e8
#define BA0_CLKCR1       0x0400
#define BA0_FRR          0x0410
#define BA0_SLT12O       0x041c
#define BA0_SERMC        0x0420
#define BA0_SERC1        0x0428
#define BA0_SERC2        0x042c
#define BA0_ACCTL        0x0460
#define BA0_ACSTS        0x0464
#define BA0_ACOSV        0x0468
#define BA0_ACCAD        0x046c
#define BA0_ACCDA        0x0470
#define BA0_ACISV        0x0474
#define BA0_ACSAD        0x0478
#define BA0_ACSDA        0x047c
#define BA0_JSPT         0x0480
#define BA0_JSCTL        0x0484
#define BA0_JSC1         0x0488
#define BA0_JSC2         0x048c
#define BA0_MIDCR        0x0490
#define BA0_MIDSR        0x0494
#define BA0_MIDWP        0x0498
#define BA0_MIDRP        0x049c
#define BA0_JSIO         0x04a0
#define BA0_AODSD1       0x04a8
#define BA0_AODSD2       0x04ac
#define BA0_CFGI         0x04b0
#define BA0_SLT12M       0x045c
#define BA0_SLT12M2      0x04dc
#define BA0_ACSTS2       0x04e4
#define BA0_ACISV2       0x04f4
#define BA0_ACSAD2       0x04f8
#define BA0_ACSDA2       0x04fc
#define BA0_FMSR         0x0730
#define BA0_B0AP         0x0730  /* alias */
#define BA0_FMDP         0x0734
#define BA0_B1AP         0x0738
#define BA0_B1DP         0x073c
#define BA0_SSPM         0x0740
#define BA0_DACSR        0x0744
#define BA0_ADCSR        0x0748
#define BA0_SSCR         0x074c
#define BA0_FMLVC        0x0754
#define BA0_FMRVC        0x0758
#define BA0_SRCSA        0x075c
#define BA0_PPLVC        0x0760
#define BA0_PPRVC        0x0764
#define BA0_PASR         0x0768
#define BA0_CASR         0x076C

/* Bit definitions */
#define BA0_HISR_INTENA      (1U << 31)
#define BA0_HISR_MIDI        (1U << 22)
#define BA0_HISR_FIFOI       (1U << 20)
#define BA0_HISR_DMAI        (1U << 18)
#define BA0_HISR_FIFO(c)     (1U << (12 + (c)))
#define BA0_HISR_DMA(c)      (1U << (8 + (c)))
#define BA0_HISR_GPPI        (1U << 5)
#define BA0_HISR_GPSI        (1U << 4)
#define BA0_HISR_GP3I        (1U << 3)
#define BA0_HISR_GP1I        (1U << 2)
#define BA0_HISR_VUPI        (1U << 1)
#define BA0_HISR_VDNI        (1U << 0)

#define BA0_HICR_CHGM        (1U << 1)
#define BA0_HICR_IEV         (1U << 0)
#define BA0_HICR_EOI         (3U << 0)

#define BA0_HDSR_CH1P        (1U << 25)
#define BA0_HDSR_CH2P        (1U << 24)
#define BA0_HDSR_DHTC        (1U << 17)
#define BA0_HDSR_DTC         (1U << 16)
#define BA0_HDSR_DRUN        (1U << 15)
#define BA0_HDSR_RQ          (1U << 7)

#define BA0_DMR_DMA          (1U << 29)
#define BA0_DMR_POLL         (1U << 28)
#define BA0_DMR_TBC          (1U << 25)
#define BA0_DMR_CBC          (1U << 24)
#define BA0_DMR_SWAPC        (1U << 22)
#define BA0_DMR_SIZE20       (1U << 20)
#define BA0_DMR_USIGN        (1U << 19)
#define BA0_DMR_BEND         (1U << 18)
#define BA0_DMR_MONO         (1U << 17)
#define BA0_DMR_SIZE8        (1U << 16)
#define BA0_DMR_TYPE_DEMAND  (0U << 6)
#define BA0_DMR_TYPE_SINGLE  (1U << 6)
#define BA0_DMR_TYPE_BLOCK   (2U << 6)
#define BA0_DMR_TYPE_CASCADE (3U << 6)
#define BA0_DMR_DEC          (1U << 5)
#define BA0_DMR_AUTO         (1U << 4)
#define BA0_DMR_TR_VERIFY    (0U << 2)
#define BA0_DMR_TR_WRITE     (1U << 2)
#define BA0_DMR_TR_READ      (2U << 2)
#define BA0_DCR_HTCIE        (1U << 17)
#define BA0_DCR_TCIE         (1U << 16)
#define BA0_DCR_MSK          (1U << 0)

#define BA0_FCR_FEN          (1U << 31)
#define BA0_FCR_DACZ         (1U << 30)
#define BA0_FCR_PSH          (1U << 29)
#define BA0_FCR_RS(x)        (((x) & 0x1f) << 24)
#define BA0_FCR_LS(x)        (((x) & 0x1f) << 16)
#define BA0_FCR_SZ(x)        (((x) & 0x7f) << 8)
#define BA0_FCR_OF(x)        (((x) & 0x7f) << 0)

#define BA0_FCHS_RCO(x)      (1U << (7 + (((x) & 3) << 3)))
#define BA0_FCHS_LCO(x)      (1U << (6 + (((x) & 3) << 3)))
#define BA0_FCHS_MRP(x)      (1U << (5 + (((x) & 3) << 3)))
#define BA0_FCHS_FE(x)       (1U << (4 + (((x) & 3) << 3)))
#define BA0_FCHS_FF(x)       (1U << (3 + (((x) & 3) << 3)))
#define BA0_FCHS_IOR(x)      (1U << (2 + (((x) & 3) << 3)))
#define BA0_FCHS_RCI(x)      (1U << (1 + (((x) & 3) << 3)))
#define BA0_FCHS_LCI(x)      (1U << (0 + (((x) & 3) << 3)))

#define BA0_FSIC_FIC(x)      (((x) & 0x7f) << 24)
#define BA0_FSIC_FORIE       (1U << 23)
#define BA0_FSIC_FURIE       (1U << 22)
#define BA0_FSIC_FSCIE       (1U << 16)
#define BA0_FSIC_FSC(x)      (((x) & 0x7f) << 8)
#define BA0_FSIC_FOR         (1U << 7)
#define BA0_FSIC_FUR         (1U << 6)
#define BA0_FSIC_FSCR        (1U << 0)

#define BA0_EPPMC_FPDN       (1U << 14)

#define BA0_SPMC_GIPPEN      (1U << 15)
#define BA0_SPMC_GISPEN      (1U << 14)
#define BA0_SPMC_EESPD       (1U << 9)
#define BA0_SPMC_ASDI2E      (1U << 8)
#define BA0_SPMC_ASDO        (1U << 7)
#define BA0_SPMC_WUP2        (1U << 3)
#define BA0_SPMC_WUP1        (1U << 2)
#define BA0_SPMC_ASYNC       (1U << 1)
#define BA0_SPMC_RSTN        (1U << 0)

#define BA0_CFLR_DEFAULT     0x00000001

#define BA0_CLKCR1_CLKON     (1U << 25)
#define BA0_CLKCR1_DLLRDY    (1U << 24)
#define BA0_CLKCR1_DLLOS     (1U << 6)
#define BA0_CLKCR1_SWCE      (1U << 5)
#define BA0_CLKCR1_DLLP      (1U << 4)
#define BA0_CLKCR1_DLLSS(x)  (((x) & 3) << 3)

#define BA0_SERMC_FCRN       (1U << 27)
#define BA0_SERMC_ODSEN2     (1U << 25)
#define BA0_SERMC_ODSEN1     (1U << 24)
#define BA0_SERMC_SXLB       (1U << 21)
#define BA0_SERMC_SLB        (1U << 20)
#define BA0_SERMC_LOVF       (1U << 19)
#define BA0_SERMC_TCID(x)    (((x) & 3) << 16)
#define BA0_SERMC_PXLB       (5U << 1)
#define BA0_SERMC_PLB        (4U << 1)
#define BA0_SERMC_PTC        (7U << 1)
#define BA0_SERMC_PTC_AC97   (1U << 1)
#define BA0_SERMC_MSPE       (1U << 0)
#define BA0_SERC1_SO1F(x)    (((x) & 7) >> 1)
#define BA0_SERC1_AC97       (1U << 1)
#define BA0_SERC1_SO1EN      (1U << 0)
#define BA0_SERC2_SI1F(x)    (((x) & 7) >> 1)
#define BA0_SERC2_AC97       (1U << 1)
#define BA0_SERC2_SI1EN      (1U << 0)

#define BA0_ACCTL_TC         (1U << 6)
#define BA0_ACCTL_CRW        (1U << 4)
#define BA0_ACCTL_DCV        (1U << 3)
#define BA0_ACCTL_VFRM       (1U << 2)
#define BA0_ACCTL_ESYN       (1U << 1)
#define BA0_ACSTS_VSTS       (1U << 1)
#define BA0_ACSTS_CRDY       (1U << 0)
#define BA0_ACOSV_SLV(x)     (1U << ((x) - 3))
#define BA0_ACISV_SLV(x)     (1U << ((x) - 3))

#define BA0_MIDCR_MRST       (1U << 5)
#define BA0_MIDCR_MLB        (1U << 4)
#define BA0_MIDCR_TIE        (1U << 3)
#define BA0_MIDCR_RIE        (1U << 2)
#define BA0_MIDCR_RXE        (1U << 1)
#define BA0_MIDCR_TXE        (1U << 0)
#define BA0_MIDSR_RDA        (1U << 15)
#define BA0_MIDSR_TBE        (1U << 14)
#define BA0_MIDSR_RBE        (1U << 7)
#define BA0_MIDSR_TBF        (1U << 6)

#define BA0_AODSD1_NDS(x)    (1U << ((x) - 3))
#define BA0_AODSD2_NDS(x)    (1U << ((x) - 3))

#define BA0_SSPM_MIXEN       (1U << 6)
#define BA0_SSPM_CSRCEN      (1U << 5)
#define BA0_SSPM_PSRCEN      (1U << 4)
#define BA0_SSPM_JSEN        (1U << 3)
#define BA0_SSPM_ACLEN       (1U << 2)
#define BA0_SSPM_FMEN        (1U << 1)

#define BA0_SSCR_HVS1        (1U << 23)
#define BA0_SSCR_MVCS        (1U << 19)
#define BA0_SSCR_MVLD        (1U << 18)
#define BA0_SSCR_MVAD        (1U << 17)
#define BA0_SSCR_MVMD        (1U << 16)
#define BA0_SSCR_XLPSRC      (1U << 8)
#define BA0_SSCR_LPSRC       (1U << 7)
#define BA0_SSCR_CDTX        (1U << 5)
#define BA0_SSCR_HVC         (1U << 3)

#define JSPT_CAX             0x00000001
#define JSPT_CAY             0x00000002
#define JSPT_CBX             0x00000004
#define JSPT_CBY             0x00000008
#define JSPT_BA1             0x00000010
#define JSPT_BA2             0x00000020
#define JSPT_BB1             0x00000040
#define JSPT_BB2             0x00000080
#define JSCTL_SP_MASK        0x00000003
#define JSCTL_SP_SLOW        0x00000000
#define JSCTL_SP_MEDIUM_SLOW 0x00000001
#define JSCTL_SP_MEDIUM_FAST 0x00000002
#define JSCTL_SP_FAST        0x00000003
#define JSCTL_ARE            0x00000004
#define JSC1_Y1V_MASK        0x0000FFFF
#define JSC1_X1V_MASK        0xFFFF0000
#define JSC1_Y1V_SHIFT       0
#define JSC1_X1V_SHIFT       16
#define JSC2_Y2V_MASK        0x0000FFFF
#define JSC2_X2V_MASK        0xFFFF0000
#define JSC2_Y2V_SHIFT       0
#define JSC2_X2V_SHIFT       16
#define JSIO_DAX             0x00000001
#define JSIO_DAY             0x00000002
#define JSIO_DBX             0x00000004
#define JSIO_DBY             0x00000008
#define JSIO_AXOE            0x00000010
#define JSIO_AYOE            0x00000020
#define JSIO_BXOE            0x00000040
#define JSIO_BYOE            0x00000080

#define CS4281_FIFO_SIZE 32

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

    /* Hardware Register Shadows */
    uint32_t ba0_regs[CS4281_BA0_SIZE / 4];

    /* AC97 codec emulation */
    uint16_t ac97_primary[0x80];
    uint16_t ac97_secondary[0x80];
    bool dual_codec;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t hisr = s->ba0_regs[BA0_HISR >> 2];
    uint32_t himr = s->ba0_regs[BA0_HIMR >> 2];

    if (hisr & ~himr) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void ac97_write(PCIBaseState *s, int codec_num, uint8_t reg, uint16_t val)
{
    if (codec_num == 0) {
        s->ac97_primary[reg & 0x7f] = val;
    } else {
        s->ac97_secondary[reg & 0x7f] = val;
    }
}

static uint16_t ac97_read(PCIBaseState *s, int codec_num, uint8_t reg)
{
    if (codec_num == 0) {
        return s->ac97_primary[reg & 0x7f];
    } else {
        return s->ac97_secondary[reg & 0x7f];
    }
}

static void pcibase_ac97_command(PCIBaseState *s, uint32_t actl_val)
{
    uint32_t accad = s->ba0_regs[BA0_ACCAD >> 2];
    uint32_t accda = s->ba0_regs[BA0_ACCDA >> 2];
    uint8_t reg = accad & 0x7f;
    int codec = (actl_val & BA0_ACCTL_TC) ? 1 : 0;

    if (actl_val & BA0_ACCTL_CRW) {
        /* Read command */
        uint16_t result = ac97_read(s, codec, reg);
        s->ba0_regs[BA0_ACSDA >> 2] = result;
        s->ba0_regs[BA0_ACSTS >> 2] |= BA0_ACSTS_VSTS;
    } else {
        /* Write command */
        uint16_t data = accda & 0xffff;
        ac97_write(s, codec, reg, data);
    }
    /* Clear DCV to indicate completion */
    s->ba0_regs[BA0_ACCTL >> 2] &= ~BA0_ACCTL_DCV;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t reg_offset = addr & 0xffc;
    uint32_t idx = reg_offset >> 2;

    if (addr >= CS4281_BA0_SIZE) {
        return ~0ULL;
    }
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid mmio read size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return ~0ULL;
    }

    switch (reg_offset) {
    case BA0_ACCTL:
        val = s->ba0_regs[idx];
        break;
    case BA0_ACSTS:
        val = s->ba0_regs[idx];
        break;
    case BA0_ACSDA:
        val = s->ba0_regs[idx];
        break;
    case BA0_ACISV:
        val = s->ba0_regs[idx];
        break;
    case BA0_ACSTS2:
        val = s->ba0_regs[idx];
        break;
    case BA0_ACSDA2:
        val = s->ba0_regs[idx];
        break;
    case BA0_MIDSR:
        /* Return a status indicating TX not full, RX empty */
        val = 0x00000080;
        break;
    case BA0_HISR:
        val = s->ba0_regs[idx];
        break;
    case BA0_HDSR0:
    case BA0_HDSR1:
    case BA0_HDSR2:
    case BA0_HDSR3:
        val = s->ba0_regs[idx];
        /* Clear the corresponding HISR DMA bit by reading HDSR */
        {
            int chan = (reg_offset - BA0_HDSR0) / 4;
            s->ba0_regs[BA0_HISR >> 2] &= ~BA0_HISR_DMA(chan);
            pcibase_update_irq(s);
        }
        break;
    default:
        val = s->ba0_regs[idx];
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_offset = addr & 0xffc;
    uint32_t idx = reg_offset >> 2;

    if (addr >= CS4281_BA0_SIZE) {
        return;
    }
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid mmio write size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return;
    }

    switch (reg_offset) {
    case BA0_HICR:
        s->ba0_regs[idx] = val;
        if (val == BA0_HICR_EOI) {
            /* EOI clears interrupt pending state */
            pcibase_update_irq(s);
        }
        break;
    case BA0_HIMR:
        s->ba0_regs[idx] = val;
        pcibase_update_irq(s);
        break;
    case BA0_ACCTL:
        s->ba0_regs[idx] = val;
        if (val & BA0_ACCTL_DCV) {
            pcibase_ac97_command(s, val);
        } else if (val & BA0_ACCTL_VFRM) {
            /* When VFRM is asserted, mark input slots as valid */
            s->ba0_regs[BA0_ACISV >> 2] |= BA0_ACISV_SLV(3) | BA0_ACISV_SLV(4);
        }
        break;
    case BA0_CLKCR1:
        s->ba0_regs[idx] = val;
        if ((val & BA0_CLKCR1_SWCE) && (val & BA0_CLKCR1_DLLP)) {
            s->ba0_regs[idx] |= BA0_CLKCR1_DLLRDY;
        }
        break;
    case BA0_MIDCR:
        s->ba0_regs[idx] = val;
        if (val & BA0_MIDCR_MRST) {
            /* MIDI reset sets MIDSR to empty state */
            s->ba0_regs[BA0_MIDSR >> 2] = 0x00000080;
        }
        break;
    case BA0_SPMC:
        {
            uint32_t old = s->ba0_regs[idx];
            s->ba0_regs[idx] = val;
            if ((old ^ val) & BA0_SPMC_RSTN) {
                if (val & BA0_SPMC_RSTN) {
                    s->ba0_regs[BA0_ACSTS >> 2] |= BA0_ACSTS_CRDY;
                } else {
                    s->ba0_regs[BA0_ACSTS >> 2] &= ~BA0_ACSTS_CRDY;
                }
            }
        }
        break;
    case BA0_ACSTS:
    case BA0_ACSTS2:
        /* Read-only, ignore write */
        break;
    case BA0_ACSDA:
    case BA0_ACSDA2:
        /* Read-only, ignore write */
        break;
    case BA0_HISR:
        /* Read-only, ignore write */
        break;
    default:
        s->ba0_regs[idx] = val;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* No PIO registers for this device */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO registers for this device */
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

    memset(s->ba0_regs, 0, sizeof(s->ba0_regs));

    /* Set default register values */
    s->ba0_regs[BA0_CFLR >> 2] = BA0_CFLR_DEFAULT;
    s->ba0_regs[BA0_SERC1 >> 2] = BA0_SERC1_SO1EN | BA0_SERC1_AC97;
    s->ba0_regs[BA0_SERC2 >> 2] = BA0_SERC2_SI1EN | BA0_SERC2_AC97;
    s->ba0_regs[BA0_SPMC >> 2]  = BA0_SPMC_RSTN;   /* Ensure codec is out of reset */

    /* Set AC'97 status registers to indicate codec ready */
    s->ba0_regs[BA0_ACSTS >> 2]  = BA0_ACSTS_CRDY;
    s->ba0_regs[BA0_ACSTS2 >> 2] = BA0_ACSTS_CRDY;

    /* Initialize AC97 codec with minimal valid data */
    memset(s->ac97_primary, 0, sizeof(s->ac97_primary));
    memset(s->ac97_secondary, 0, sizeof(s->ac97_secondary));
    /* Vendor ID for Cirrus Logic CS4297A */
    s->ac97_primary[0x7c] = 0x4352;
    s->ac97_primary[0x7e] = 0x5937;
    /* Set some default mixer values */
    s->ac97_primary[0x02] = 0x0808;  /* Master volume, 0dB */
    s->ac97_primary[0x04] = 0x0808;  /* Headphone volume, 0dB */
    s->ac97_primary[0x0a] = 0x0000;  /* Mic volume, muted */
    s->ac97_primary[0x0e] = 0x8000;  /* Mic boost 20dB */
    s->ac97_primary[0x18] = 0x0808;  /* PCM volume, 0dB */
    s->ac97_primary[0x1a] = 0x0000;  /* Record select */
    s->ac97_primary[0x1c] = 0x0000;  /* Record gain */
    s->ac97_primary[0x20] = 0x8000;  /* General purpose */
    s->ac97_primary[0x26] = 0x0100;  /* Powerdown Ctrl/Stat, set REF bit to indicate ready */

    /* Set MIDI status to ready */
    s->ba0_regs[BA0_MIDSR >> 2] = 0x00000080;

    s->dual_codec = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1013 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x6005 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = CS4281_BA0_SIZE;
    s->bar_info[0].name = "cs4281-ba0";
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = CS4281_BA1_SIZE;
    s->bar_info[1].name = "cs4281-ba1";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Set initial state after reset */
    pcibase_reset(DEVICE(pdev));
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
    .name = "snd_cs4281_pci",
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
