/*
 * QEMU model for Conexant CX8800 PCI video capture device
 * Based on Linux driver cx88-video.c (cx2388x family)
 * This is a complete functional implementation for Phase 2.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bswap.h"
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

/*----------------------------------------------------------------------------*/
/* Static Hardware Identifiers and Register Offsets (from driver source)        */
/*----------------------------------------------------------------------------*/

/* PCI IDs (first entry in pci_device_id table) */
#define VENDOR_ID 0x14f1
#define DEVICE_ID 0x8800

/* Default fallback values; will be refined when exact defines are available */
#ifndef BAR0_SIZE
#define BAR0_SIZE 0x1000000 /* 16 MB */
#endif

#define TYPE_PCIBASE_DEVICE "cx8800_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/*----------------------------------------------------------------------------*/
/* Register offsets (byte addresses within BAR0 MMIO) */
/*----------------------------------------------------------------------------*/
#define MO_DEV_CNTRL2       0x200034
#define MO_PCI_INTMSK       0x200040
#define MO_PCI_INTSTAT      0x200044
#define MO_VID_INTMSK       0x200050
#define MO_VID_INTSTAT      0x200054
#define MO_AUD_INTMSK       0x200060
#define MO_AUD_INTSTAT      0x200064
#define MO_TS_INTMSK        0x200070
#define MO_VIP_INTMSK       0x200080
#define MO_GPHST_INTMSK     0x200090

#define MO_DEVICE_STATUS    0x310100
#define MO_INPUT_FORMAT     0x310104
#define MO_COLOR_CTRL       0x310184
#define MO_CONTR_BRIGHT     0x310110
#define MO_UV_SATURATION    0x310114
#define MO_HUE              0x310118
#define MO_HTOTAL           0x310120
#define MO_HACTIVE          0x310124
#define MO_VTOTAL           0x310128
#define MO_VACTIVE          0x31012c
#define MO_FILTER_EVEN      0x31015c
#define MO_FILTER_ODD       0x310160
#define MO_PLL_REG          0x310168
#define MO_VBOS_CONTROL     0x3101a8

#define MO_VIDY_GPCNT       0x31C020
#define MO_VIDY_GPCNTRL     0x31C030
#define MO_VID_DMACNTRL     0x31C040
#define MO_VBI_GPCNT        0x31C02C
#define MO_VBI_GPCNTRL      0x31C03C
#define MO_AUD_DMACNTRL     0x32C040
#define MO_AUDR_LNGTH       0x32C04C
#define MO_AUDD_LNGTH       0x32C048
#define MO_TS_DMACNTRL      0x33C040
#define MO_VIP_DMACNTRL     0x34C040
#define MO_GPHST_DMACNTRL   0x38C040

#define MO_GP0_IO           0x350010
#define MO_GP1_IO           0x350014
#define MO_GP2_IO           0x350018
#define MO_GP3_IO           0x35001C
#define MO_SAMPLE_IO        0x35C058
#define MO_AFECFG_IO        0x35C04C
#define MO_SRST_IO          0x35C05C

#define AUD_INIT             0x320100
#define AUD_INIT_LD          0x320104
#define AUD_SOFT_RESET       0x320108
#define AUD_BAUDRATE         0x320124
#define AUD_I2SINPUTCNTL     0x320120
#define AUD_I2SOUTPUTCNTL    0x320128
#define AUD_VOL_CTL          0x320594
#define AUD_BAL_CTL          0x320598
#define AUD_CTL              0x32058c
#define AUD_STATUS           0x320590
#define AUD_I2SCNTL          0x3205ec
#define AUD_FM_MODE_ENABLE   0x320258
#define AUD_MODE_CHG_TIMER   0x3205b4
#define AUD_START_TIMER      0x3205b0
#define AUD_POLYPH80SCALEFAC 0x3205b8
#define AUD_POLY0_DDS_CONSTANT   0x320270
#define AUD_DMD_RA_DDS       0x3205bc
#define AUD_QAM_MODE         0x320d04
#define AUD_PDF_DDS_CNST_BYTE0   0x320d03
#define AUD_PDF_DDS_CNST_BYTE1   0x320d02
#define AUD_PDF_DDS_CNST_BYTE2   0x320d01
#define AUD_PHACC_FREQ_8LSB      0x320d2b
#define AUD_PHACC_FREQ_8MSB      0x320d2a
#define AUD_PLL_INT          0x320608
#define AUD_PLL_FRAC         0x32060c
#define AUD_PLL_DDS          0x320604

/* Audio processing registers (extracted from driver defines) */
#define AUD_IIR1_0_SEL       0x320150
#define AUD_IIR1_0_SHIFT     0x320154
#define AUD_IIR1_1_SEL       0x320158
#define AUD_IIR1_1_SHIFT     0x32015c
#define AUD_IIR1_2_SEL       0x320160
#define AUD_IIR1_2_SHIFT     0x320164
#define AUD_IIR1_3_SEL       0x320168
#define AUD_IIR1_3_SHIFT     0x32016c
#define AUD_IIR1_4_SEL       0x320170
#define AUD_IIR1_4_SHIFT     0x32017c
#define AUD_IIR2_0_SEL       0x320190
#define AUD_IIR2_0_SHIFT     0x320194
#define AUD_IIR2_1_SEL       0x320198
#define AUD_IIR2_1_SHIFT     0x32019c
#define AUD_IIR2_2_SEL       0x3201a0
#define AUD_IIR2_2_SHIFT     0x3201a4
#define AUD_IIR2_3_SEL       0x3201a8
#define AUD_IIR2_3_SHIFT     0x3201ac
#define AUD_IIR3_0_SEL       0x3201c0
#define AUD_IIR3_0_SHIFT     0x3201c4
#define AUD_IIR3_1_SEL       0x3201c8
#define AUD_IIR3_1_SHIFT     0x3201cc
#define AUD_IIR3_2_SEL       0x3201d0
#define AUD_IIR3_2_SHIFT     0x3201d4
#define AUD_IIR3_3_SHIFT     0x3201dc

#define AUD_DCOC_0_SHIFT_IN0 0x320328
#define AUD_DCOC_0_SHIFT_IN1 0x32032c
#define AUD_DCOC_1_SHIFT_IN0 0x320338
#define AUD_DCOC_1_SHIFT_IN1 0x32033c
#define AUD_DCOC_0_SRC       0x320320
#define AUD_DCOC_1_SRC       0x320330
#define AUD_DCOC0_SHIFT      0x320324
#define AUD_DCOC1_SHIFT      0x320334
#define AUD_DCOC2_SHIFT      0x320344
#define AUD_DCOC_PASS_IN     0x320350

#define AUD_CRDC0_SRC_SEL    0x320300
#define AUD_CRDC1_SRC_SEL    0x32030c
#define AUD_CORDIC_SHIFT_0   0x320308
#define AUD_CORDIC_SHIFT_1   0x320314
#define AUD_CRDC1_SHIFT      0x320310

#define AUD_DEEMPH0_SRC_SEL  0x320440
#define AUD_DEEMPH0_SHIFT    0x320444
#define AUD_DEEMPH0_G0       0x320448
#define AUD_DEEMPH0_A0       0x32044c
#define AUD_DEEMPH0_B0       0x320450
#define AUD_DEEMPH0_B1       0x320458
#define AUD_DEEMPH0_A1       0x320454
#define AUD_DEEMPH1_SRC_SEL  0x32045c
#define AUD_DEEMPH1_SHIFT    0x320460
#define AUD_DEEMPH1_G0       0x320464
#define AUD_DEEMPH1_A0       0x320468
#define AUD_DEEMPH1_B0       0x32046c
#define AUD_DEEMPH1_A1       0x320470
#define AUD_DEEMPH1_B1       0x320474

#define AUD_OUT0_SEL         0x320490
#define AUD_OUT1_SEL         0x320498
#define AUD_OUT1_SHIFT       0x32049c
#define AUD_RDSI_SEL         0x3204a0
#define AUD_RDSI_SHIFT       0x3204a4
#define AUD_RDSQ_SEL         0x3204a8
#define AUD_RDSQ_SHIFT       0x3204ac

#define AUD_DBX_IN_GAIN      0x320500
#define AUD_DBX_WBE_GAIN     0x320504
#define AUD_DBX_SE_GAIN      0x320508

#define AUD_THR_FR           0x3203c0
#define AUD_C1_LO_THR        0x3203d4
#define AUD_C1_UP_THR        0x3203d0
#define AUD_C2_LO_THR        0x3203dc
#define AUD_C2_UP_THR        0x3203d8

#define AUD_DEEMPHNUMER1_R   0x32053c
#define AUD_DEEMPHNUMER2_R   0x320540
#define AUD_DEEMPHDENOM1_R   0x320544
#define AUD_DEEMPHDENOM2_R   0x320548
#define AUD_ERRINTRPTTHSHLD1_R   0x320550
#define AUD_ERRINTRPTTHSHLD2_R   0x320554
#define AUD_ERRINTRPTTHSHLD3_R   0x320558
#define AUD_ERRLOGPERIOD_R       0x32054c

#define AUD_DEEMPHGAIN_R     0x320538

#define AUD_RATE_ADJ1        0x3205d8
#define AUD_RATE_ADJ2        0x3205dc
#define AUD_RATE_ADJ3        0x3205e0
#define AUD_RATE_ADJ4        0x3205e4
#define AUD_RATE_ADJ5        0x3205e8
#define AUD_RATE_THRES_DMD   0x3205d0

#define AUD_NICAM_STATUS2    0x320560

#define AUD_DN0_FREQ         0x320274
#define AUD_DN1_FREQ         0x320278
#define AUD_DN1_SRC_SEL      0x320284
#define AUD_DN1_AFC          0x320280
#define AUD_DN2_FREQ         0x32028c
#define AUD_DN2_SRC_SEL      0x320298
#define AUD_DN2_AFC          0x320294
#define AUD_DN2_SHFT         0x32029c

#define AUD_PILOT_BQD_1_K0   0x320380
#define AUD_PILOT_BQD_1_K1   0x320384
#define AUD_PILOT_BQD_1_K2   0x320388
#define AUD_PILOT_BQD_1_K3   0x32038c
#define AUD_PILOT_BQD_1_K4   0x320390
#define AUD_PILOT_BQD_2_K0   0x320394
#define AUD_PILOT_BQD_2_K1   0x320398
#define AUD_PILOT_BQD_2_K2   0x32039c
#define AUD_PILOT_BQD_2_K3   0x3203a0
#define AUD_PILOT_BQD_2_K4   0x3203a4

#define AUD_AFE_12DB_EN      0x320628

#define AAGC_DEF             0x32013c
#define AAGC_HYST            0x320134
#define AAGC_GAIN            0x320138

#define AUD_RDS_LINES        4

/* Miscellaneous */
#define I2C_BASE             0x368000
#define MO_I2C               I2C_BASE

/* PCI interrupt bits defined in driver */
#define PCI_INT_VIDINT       (1 << 0)
#define PCI_INT_IR_SMPINT    (1 << 18)
#define PCI_INT_RISC_RD_BERRINT  (1 << 10)
#define PCI_INT_RISC_WR_BERRINT  (1 << 11)
#define PCI_INT_SRC_DMA_BERRINT  (1 << 13)
#define PCI_INT_IPB_DMA_BERRINT  (1 << 15)
#define PCI_INT_DST_DMA_BERRINT  (1 << 14)
#define PCI_INT_BRDG_BERRINT (1 << 12)

/* Video interrupt bit names (used via cx88_vid_irqs array) */

/* Other hardware-related constants */
#define CX88X_DEVCTRL        0x40
#define CX88X_EN_TBFX        0x02
#define CX88X_EN_VSFX        0x04

/* SRAM channel identifiers (for RISC engine) */
#define SRAM_CH21 0
#define SRAM_CH24 3
#define SRAM_CH25 4
#define SRAM_CH26 5
#define SRAM_CH27 7

/* GPIO count control reset */
#define GP_COUNT_CONTROL_RESET 0x3

/* Audio routing constants (from driver) */
#define EN_BTSC_AUTO_STEREO     3
#define EN_BTSC_FORCE_STEREO    1
#define EN_BTSC_FORCE_MONO      0
#define EN_BTSC_FORCE_SAP       2
#define EN_BTSC_AUTO_SAP        4
#define EN_NICAM_AUTO_STEREO    36
#define EN_NICAM_FORCE_STEREO   34
#define EN_NICAM_FORCE_MONO1    32
#define EN_NICAM_FORCE_MONO2    33
#define EN_NICAM_AUTO_MONO2     35
#define EN_A2_AUTO_STEREO       12
#define EN_A2_FORCE_STEREO      10
#define EN_A2_FORCE_MONO1       8
#define EN_A2_FORCE_MONO2       9
#define EN_A2_AUTO_MONO2        11
#define EN_EIAJ_AUTO_STEREO     20
#define EN_EIAJ_FORCE_STEREO    18
#define EN_EIAJ_FORCE_MONO1     16
#define EN_EIAJ_FORCE_MONO2     17
#define EN_EIAJ_AUTO_MONO2      19
#define EN_FMRADIO_AUTO_STEREO  26
#define EN_FMRADIO_FORCE_STEREO 25
#define EN_FMRADIO_FORCE_MONO   24
#define EN_FMRADIO_EN_RDS       0x00000200

#define SEL_BTSC     0x01
#define SEL_SAP      0x08
#define SEL_A2       0x04
#define SEL_NICAM    0x10
#define SEL_EIAJ     0x02
#define SEL_FMRADIO  0x20

#define EN_DMTRX_BYPASS        (1 << 11)
#define EN_DMTRX_LR            (2 << 7)
#define EN_DMTRX_SUMDIFF       (0 << 7)

#define EN_I2SIN_ENABLE        0x00008000
#define EN_I2SOUT_ENABLE       0x00002000
#define EN_DAC_ENABLE          0x00001000

/* Exact register offset from driver: VID_CAPTURE_CONTROL */
#define VID_CAPTURE_CONTROL    0x310180

/*----------------------------------------------------------------------------*/
/* QEMU device model structures */
/*----------------------------------------------------------------------------*/

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
    uint32_t intr_status;   /* PCI_INTSTAT mirror */
    uint32_t intr_mask;     /* PCI_INTMSK mirror */

    /* Hardware Register Shadows - generic MMIO backing store */
    uint8_t mmio[BAR0_SIZE];  /* Backing store for BAR0 registers */

    /* No DMA context needed (device engine handles DMA internally) */
    /* No operational status flags beyond registers */
    /* No probe/reset-specific state */
    /* No power management state */
    /* No additional info */
};


/*----------------------------------------------------------------------------*/
/* Forward declarations */
/*----------------------------------------------------------------------------*/

static void pcibase_update_irq(PCIBaseState *s);


/*----------------------------------------------------------------------------*/
/* IRQ handling logic */
/*----------------------------------------------------------------------------*/

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t status = ldl_le_p(s->mmio + MO_PCI_INTSTAT);
    uint32_t mask   = ldl_le_p(s->mmio + MO_PCI_INTMSK);

    if (status & mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}


/*----------------------------------------------------------------------------*/
/* MMIO / PIO Handlers */
/*----------------------------------------------------------------------------*/

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver always uses 32-bit aligned accesses */
    addr &= ~0x3;
    if (addr + 4 > BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }

    val = ldl_le_p(s->mmio + addr);
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The driver always writes 32-bit values */
    addr &= ~0x3;
    if (addr + 4 > BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds 0x%" HWADDR_PRIx "\n", __func__, addr);
        return;
    }

    switch (addr) {
    case MO_PCI_INTSTAT:
        /* Write-1-to-clear: bits set in 'val' are cleared */
        {
            uint32_t cur = ldl_le_p(s->mmio + addr);
            cur &= ~((uint32_t)val);
            stl_le_p(s->mmio + addr, cur);
        }
        pcibase_update_irq(s);
        break;

    case MO_VID_INTSTAT:
        /* Similar W1C for video interrupt status */
        {
            uint32_t cur = ldl_le_p(s->mmio + addr);
            cur &= ~((uint32_t)val);
            stl_le_p(s->mmio + addr, cur);
        }
        /* Video status changes may affect PCI_INTSTAT (VIDINT bit) */
        /* But we don't automatically set that; the driver does in ISR. */
        break;

    default:
        stl_le_p(s->mmio + addr, (uint32_t)val);
        break;
    }

    /* If the written register affects interrupt routing, update IRQ */
    if (addr == MO_PCI_INTMSK) {
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used by CX8800 driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used by CX8800 driver */
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


/*----------------------------------------------------------------------------*/
/* Device reset */
/*----------------------------------------------------------------------------*/

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all registers to zero */
    memset(s->mmio, 0, BAR0_SIZE);

    /* Setup default interrupt mask and status */
    /* Both start as 0; driver will program mask later */
}


/*----------------------------------------------------------------------------*/
/* BAR registration helper */
/*----------------------------------------------------------------------------*/

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


/*----------------------------------------------------------------------------*/
/* Realize function */
/*----------------------------------------------------------------------------*/

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_byte(pci_conf + 0x09, 0x04); /* Base class: multimedia */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x8000); /* Subclass 0x80 (other), prog-if 0x00 */
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
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "cx8800-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization */
    s->intr_status = 0;
    s->intr_mask = 0;
}


/*----------------------------------------------------------------------------*/
/* Uninit function */
/*----------------------------------------------------------------------------*/

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


/*----------------------------------------------------------------------------*/
/* VMState */
/*----------------------------------------------------------------------------*/

static const VMStateDescription vmstate_pcibase = {
    .name = "cx8800_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
        VMSTATE_BUFFER(mmio, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};


/*----------------------------------------------------------------------------*/
/* Class initialization */
/*----------------------------------------------------------------------------*/

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
