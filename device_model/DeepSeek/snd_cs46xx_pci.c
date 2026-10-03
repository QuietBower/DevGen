/*
 * QEMU device model for Cirrus Logic CS46xx (CS4280) audio controller
 * Based on Linux driver /home/eely/linux-7.1/sound/pci/cs46xx/cs46xx.c
 *
 * Phase 2: Incremental behavior implementation based on driver source.
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

/* Vendor and Device IDs from first entry in pci_device_id table */
#define PCI_VENDOR_ID_CIRRUS 0x1013
#define PCI_DEVICE_ID_CS4280 0x6001

/* PCI Class: Audio device (04h), subclass 01h (Audio multimedia) */
#define PCI_CLASS_CS46XX 0x0401

/* BAR sizes (derived from maximum observed register offset) */
#define BAR0_SIZE 0x800   /* BA0 space: registers from 0x000 to 0x4E4 */
#define BAR1_SIZE 0x40000 /* BA1 space: up to 0x3000C, including DSP memory */

/* Register offsets for BA0 (Control/Status) */
#define BA0_HICR                                0x00000008
#define BA0_PLLCC                               0x0000040C
#define BA0_PLLM                                0x00000408
#define BA0_CLKCR1                              0x00000400
#define BA0_CLKCR2                              0x00000404
#define BA0_SERMC1                              0x00000420
#define BA0_SERC1                               0x00000428
#define BA0_SERC2                               0x0000042C
#define BA0_SERC3                               0x00000430
#define BA0_SERC4                               0x00000434
#define BA0_SERC5                               0x00000438
#define BA0_SERC6                               0x000004D0
#define BA0_SERC7                               0x000004D4
#define BA0_SERACC                              0x000004D8
#define BA0_SERBAD                              0x00000448
#define BA0_SERBCF                              0x0000044C
#define BA0_SERBCM                              0x00000444
#define BA0_SERBST                              0x00000440
#define BA0_SERBWP                              0x00000450
#define BA0_ACCTL                               0x00000460
#define BA0_ACCTL2                              0x000004E0
#define BA0_ACSTS                               0x00000464
#define BA0_ACSTS2                              0x000004E4
#define BA0_ACOSV                               0x00000468
#define BA0_ACCDA                               0x00000470
#define BA0_ACCAD                               0x0000046C
#define BA0_ACSDA                               0x0000047C
#define BA0_ACISV                               0x00000474
#define BA0_ASER_MASTER                         0x000004A4
#define BA0_ASER_FADDR                          0x00000458
#define BA0_JSIO                                0x000004A0
#define BA0_JSPT                                0x00000480
#define BA0_JSC1                                0x00000488
#define BA0_JSC2                                0x0000048C
#define BA0_EGPIODR                             0x000004BC
#define BA0_EGPIOPTR                            0x000004C0

/* BA0 bit definitions */
#define HICR_IEV                                0x00000001
#define HICR_CHGM                               0x00000002
#define CLKCR1_SWCE                             0x00000020
#define CLKCR1_PLLP                             0x00000010
#define CLKCR2_PDIVS_8                          0x00000008
#define PLLCC_CDR_73_104_MHZ                    0x00000006
#define PLLCC_LPF_1050_2780_KHZ                 0x00000078
#define SERMC1_MSPE                             0x00000001
#define SERMC1_PTC_AC97                         0x00000002
#define SERC1_SO1EN                             0x00000001
#define SERC1_SO1F_AC97                         0x00000002
#define SERC2_SI1F_AC97                         0x00000002
#define SERC7_ASDI2EN                           0x00000001
#define SERACC_CHIP_TYPE_1_03                  0x00000000
#define SERACC_CHIP_TYPE_2_0                   0x00000001
#define SERACC_HSP                             0x00000008
#define SERACC_TWO_CODECS                      0x00000002
#define SERBCF_HBP                              0x00000001
#define SERBCM_WRC                              0x00000002
#define SERBST_WBSY                             0x00000002
#define ACCTL_RSTN                              0x00000001
#define ACCTL_ESYN                              0x00000002
#define ACCTL_VFRM                              0x00000004
#define ACCTL_DCV                               0x00000008
#define ACCTL_CRW                               0x00000010
#define ACCTL_TC                                0x00000040
#define ACSTS_CRDY                              0x00000001
#define ACSTS_VSTS                              0x00000002
#define ACOSV_SLV3                              0x00000001
#define ACOSV_SLV4                              0x00000002
#define ACOSV_SLV5                              0x00000004
#define ACOSV_SLV6                              0x00000008
#define ACOSV_SLV7                              0x00000010
#define ACOSV_SLV8                              0x00000020
#define ACOSV_SLV9                              0x00000040
#define ACOSV_SLV11                             0x00000100
#define ACISV_ISV3                              0x00000001
#define ACISV_ISV4                              0x00000002
#define EGPIODR_GPOE0                           0x00000001
#define EGPIODR_GPOE2                           0x00000004
#define EGPIOPTR_GPPT0                          0x00000001
#define EGPIOPTR_GPPT2                          0x00000004
#define JSC1_Y1V_MASK                           0x0000FFFF
#define JSC1_X1V_MASK                           0xFFFF0000
#define JSC1_Y1V_SHIFT                          0
#define JSC1_X1V_SHIFT                          16
#define JSC2_Y2V_MASK                           0x0000FFFF
#define JSC2_X2V_MASK                           0xFFFF0000
#define JSC2_Y2V_SHIFT                          0
#define JSC2_X2V_SHIFT                          16

/* Register offsets for BA1 (DSP memory and control) */
#define BA1_SP_DMEM0                            0x00000000
#define BA1_SP_DMEM1                            0x00010000
#define BA1_SP_PMEM                             0x00020000
#define BA1_SP_REG                              0x00030000
#define BA1_CCTL                                0x064
#define BA1_CVOL                                0x2f8
#define BA1_PVOL                                0x0f8
#define BA1_PCTL                                0x2a4
#define BA1_CFG1                                0x134
#define BA1_CFG2                                0x138
#define BA1_CCST                                0x13c
#define BA1_CIE                                 0x104
#define BA1_PFIE                                0x0c4
#define BA1_CPI                                 0x2f4
#define BA1_CCI                                 0x2d8
#define BA1_CD                                  0x2e0
#define BA1_CSRC                                0x2c8
#define BA1_CSPB                                0x340
#define BA1_PSRC                                0x288
#define BA1_PPI                                 0x2b4
#define BA1_SPCR                                0x00030000
#define BA1_DREG                                0x00030004
#define BA1_TWPR                                0x0003000C
#define BA1_FRMT                                0x00030030
#define BA1_VARIDEC_BUF_1                       0x000

/* BA1 bit definitions */
#define SPCR_RUN                                0x00000001
#define SPCR_RUNFR                              0x00000004
#define SPCR_DRQEN                              0x00000020
#define SPCR_RSTSP                              0x00000040
#define DREG_REGID_TRAP_SELECT                  0x00000500

typedef enum {
    BAR_TYPE_NONE,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    size_t size;
    const char *name;
} BARInfo;

#define TYPE_PCIBASE_DEVICE "snd_cs46xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ---------------------------------------------------
 * Internal device state
 * --------------------------------------------------- */
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

    /* BAR0 and BAR1 memory banks */
    uint32_t *ba0;
    uint32_t *ba1;
    size_t ba0_size, ba1_size;

    /* AC97 codec emulation */
    uint16_t ac97_codec_regs[256];

    /* DMA Context */
    dma_addr_t dma_addr;
    uint32_t dma_count;

    /* Operational status */
    uint32_t status;

    /* Reset sequence */
    bool in_reset;

    /* Power management state (D0-D3) */
    int pm_state;
};

/* ---------------------------------------------------
 * BAR0 (Control/Status) MMIO handlers
 * --------------------------------------------------- */
static uint64_t cs46xx_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;
    uint32_t val = 0;

    if (addr >= BAR0_SIZE) {
        return ~0ULL;
    }
    /* AC97 status read */
    if (addr == BA0_ACSDA) {
        /* Return last read data */
        return s->ac97_codec_regs[(s->ba0[BA0_ACCAD >> 2]) & 0xFF];
    }
    /* Simple array read for general registers */
    val = s->ba0[addr >> 2];
    return val;
}

static void cs46xx_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;

    if (addr >= BAR0_SIZE) {
        return;
    }

    /* AC97 control write */
    if (addr == BA0_ACCTL) {
        /* Store the written value */
        s->ba0[addr >> 2] = val;

        /* When ESYN and RSTN are both set, the codec is ready */
        if ((val & ACCTL_ESYN) && (val & ACCTL_RSTN)) {
            s->ba0[BA0_ACSTS >> 2] |= ACSTS_CRDY;
        }

        /* When VFRM is asserted, input slot valid bits become active */
        if (val & ACCTL_VFRM) {
            s->ba0[BA0_ACISV >> 2] |= (ACISV_ISV3 | ACISV_ISV4);
        }

        /* Existing AC97 cycle handling: initiate read/write if VFRM is set */
        if (val & ACCTL_VFRM) {
            uint32_t reg_index = s->ba0[BA0_ACCAD >> 2] & 0xFF;
            if (val & ACCTL_CRW) {
                /* Read cycle */
                if (reg_index < 0x80) {
                    s->ba0[BA0_ACSDA >> 2] = s->ac97_codec_regs[reg_index];
                    s->ba0[BA0_ACSTS >> 2] |= ACSTS_VSTS;
                }
            } else {
                /* Write cycle */
                if (reg_index < 0x80) {
                    s->ac97_codec_regs[reg_index] = (uint16_t)val;
                }
            }
        }
        return;
    }

    /* Secondary AC97 control write (ACCTL2) */
    if (addr == BA0_ACCTL2) {
        s->ba0[addr >> 2] = val;

        /* Mirror behavior for secondary codec ready */
        if ((val & ACCTL_ESYN) && (val & ACCTL_RSTN)) {
            s->ba0[BA0_ACSTS2 >> 2] |= ACSTS_CRDY;
        }
        /* For now, no AC97 cycle handling for secondary codec */
        return;
    }

    /* AC97 address/data registers (side-effect: just store) */
    s->ba0[addr >> 2] = val;
}

static const MemoryRegionOps cs46xx_bar0_ops = {
    .read = cs46xx_bar0_read,
    .write = cs46xx_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* ---------------------------------------------------
 * BAR1 (DSP memory/control) MMIO handlers
 * --------------------------------------------------- */
static uint64_t cs46xx_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;

    if (addr >= BAR1_SIZE) {
        return ~0ULL;
    }
    return s->ba1[addr >> 2];
}

static void cs46xx_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;

    if (addr >= BAR1_SIZE) {
        return;
    }
    s->ba1[addr >> 2] = val;
}

static const MemoryRegionOps cs46xx_bar1_ops = {
    .read = cs46xx_bar1_read,
    .write = cs46xx_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* ---------------------------------------------------
 * Helper to assign MMIO ops based on BAR index
 * --------------------------------------------------- */
static const MemoryRegionOps *cs46xx_get_bar_ops(int index)
{
    if (index == 0) {
        return &cs46xx_bar0_ops;
    } else {
        return &cs46xx_bar1_ops;
    }
}

/* ---------------------------------------------------
 * BAR registration (modified to use dedicated ops)
 * --------------------------------------------------- */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, int bar_index, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bar_index];
    const MemoryRegionOps *ops = cs46xx_get_bar_ops(bar_index);

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bar_index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* PIO not used in this device */
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bar_index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        /* RAM not used */
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bar_index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ---------------------------------------------------
 * Device reset
 * --------------------------------------------------- */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Clear BAR0/BAR1 memory */
    memset(s->ba0, 0, BAR0_SIZE);
    memset(s->ba1, 0, BAR1_SIZE);
    /* Initialize AC97 codec registers (primary codec CS4294) */
    memset(s->ac97_codec_regs, 0, sizeof(s->ac97_codec_regs));
    s->ac97_codec_regs[0x7C] = 0x4352;  /* Vendor ID1 */
    s->ac97_codec_regs[0x7E] = 0x5900;  /* Vendor ID2 (CS4294 ID) */
    s->in_reset = false;
    s->pm_state = 0;
}

/* ---------------------------------------------------
 * Realize: perform common initialization
 * --------------------------------------------------- */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CIRRUS);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_CS4280);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_CS46XX);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Express capability (not strictly required but harmless) */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Set subsystem IDs (optional, zero for generic) */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x1013);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x6001);

    /* BAR Initialization */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "cs46xx-bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = BAR1_SIZE;
    s->bar_info[1].name = "cs46xx-bar1";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, i, &s->bar_info[i], errp);
    }

    /* Allocate backing memory for registers */
    s->ba0 = g_malloc0(BAR0_SIZE);
    s->ba0_size = BAR0_SIZE;
    s->ba1 = g_malloc0(BAR1_SIZE);
    s->ba1_size = BAR1_SIZE;

    /* Initialize AC97 codec regs (done in reset as well) */
    memset(s->ac97_codec_regs, 0, sizeof(s->ac97_codec_regs));
    s->ac97_codec_regs[0x7C] = 0x4352;
    s->ac97_codec_regs[0x7E] = 0x5900;

    /* DMA configuration */
    s->dma_addr = 0;
    s->dma_count = 0;

    /* Other initializations */
    s->status = 0;
    s->in_reset = false;
    s->pm_state = 0;
}

/* ---------------------------------------------------
 * Cleanup
 * --------------------------------------------------- */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    g_free(s->ba0);
    g_free(s->ba1);

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_cs46xx_pci",
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
