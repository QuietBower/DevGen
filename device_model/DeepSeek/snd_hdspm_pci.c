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
/* No extra includes needed */

#define TYPE_PCIBASE_DEVICE "snd_hdspm_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_XILINX                    0x10ee
#define PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP_MADI 0x3fc6
#define PCI_CLASS_ID 0x040100 /* Multimedia audio controller */
#define BAR0_SIZE 0x10000 /* 64KB */
#define HDSPM_MAX_CHANNELS 64

/* Registers */
#define HDSPM_WR_SETTINGS             0
#define HDSPM_outputBufferAddress    32
#define HDSPM_inputBufferAddress     36
#define HDSPM_controlRegister	     64
#define HDSPM_interruptConfirmation  96
#define HDSPM_control2Reg	     256
#define HDSPM_freqReg                256
#define HDSPM_midiDataOut0	     352
#define HDSPM_midiDataOut1	     356
#define HDSPM_eeprom_wr		     384
#define HDSPM_outputEnableBase       512
#define HDSPM_inputEnableBase        768
#define HDSPM_pageAddressBufferOut       8192
#define HDSPM_pageAddressBufferIn        (HDSPM_pageAddressBufferOut+64*16*4)
#define HDSPM_MADI_mixerBase    32768
#define HDSPM_MATRIX_MIXER_SIZE  8192
#define HDSPM_statusRegister    0
#define HDSPM_statusRegister2  192
#define HDSPM_timecodeRegister 128
#define HDSPM_RD_STATUS_0 0
#define HDSPM_RD_STATUS_1 64
#define HDSPM_RD_STATUS_2 128
#define HDSPM_RD_STATUS_3 192
#define HDSPM_RD_PLL_FREQ      512
#define HDSPM_WR_TCO           128
#define HDSPM_RD_TCO           256

/* Status bits */
#define HDSPM_audioIRQPending    (1<<0)
#define HDSPM_RX_64ch            (1<<1)
#define HDSPM_AB_int             (1<<2)
#define HDSPM_madiLock           (1<<3)
#define HDSPM_madiSync          (1<<18)
#define HDSPM_tcoLockMadi    0x00000020
#define HDSPM_tcoSync    0x10000000
#define HDSPM_syncInLock 0x00010000
#define HDSPM_syncInSync 0x00020000
#define HDSPM_BufferPositionMask 0x000FFC0
#define HDSPM_DoubleSpeedStatus (1<<19)
#define HDSPM_madiFreq0         (1<<22)
#define HDSPM_madiFreq1         (1<<23)
#define HDSPM_madiFreq2         (1<<24)
#define HDSPM_madiFreq3         (1<<25)
#define HDSPM_BufferID          (1<<26)
#define HDSPM_tco_detect         0x08000000
#define HDSPM_tcoLockAes         0x20000000
#define HDSPM_s2_tco_detect      0x00000040
#define HDSPM_s2_AEBO_D          0x00000080
#define HDSPM_s2_AEBI_D          0x00000100
#define HDSPM_midi0IRQPending    0x40000000
#define HDSPM_midi1IRQPending    0x80000000
#define HDSPM_midi2IRQPending    0x20000000
#define HDSPM_midi2IRQPendingAES 0x00000020
#define HDSPM_midi3IRQPending    0x00200000
#define HDSPM_madiFreqMask  (HDSPM_madiFreq0|HDSPM_madiFreq1|\
                             HDSPM_madiFreq2|HDSPM_madiFreq3)
#define HDSPM_madiFreq32    (HDSPM_madiFreq0)
#define HDSPM_madiFreq44_1  (HDSPM_madiFreq1)
#define HDSPM_madiFreq48    (HDSPM_madiFreq0|HDSPM_madiFreq1)
#define HDSPM_madiFreq64    (HDSPM_madiFreq2)
#define HDSPM_madiFreq88_2  (HDSPM_madiFreq0|HDSPM_madiFreq2)
#define HDSPM_madiFreq96    (HDSPM_madiFreq1|HDSPM_madiFreq2)
#define HDSPM_madiFreq128   (HDSPM_madiFreq0|HDSPM_madiFreq1|HDSPM_madiFreq2)
#define HDSPM_madiFreq176_4 (HDSPM_madiFreq3)
#define HDSPM_madiFreq192   (HDSPM_madiFreq3|HDSPM_madiFreq0)

#define HDSPM_version0 (1<<0)
#define HDSPM_version1 (1<<1)
#define HDSPM_version2 (1<<2)
#define HDSPM_wcLock (1<<3)
#define HDSPM_wcSync (1<<4)
#define HDSPM_wc_freq0 (1<<5)
#define HDSPM_wc_freq1 (1<<6)
#define HDSPM_wc_freq2 (1<<7)
#define HDSPM_wc_freq3 0x800
#define HDSPM_SyncRef0 0x10000
#define HDSPM_SyncRef1 0x20000
#define HDSPM_SelSyncRef0 (1<<8)
#define HDSPM_SelSyncRef1 (1<<9)
#define HDSPM_SelSyncRef2 (1<<10)
#define HDSPM_wcFreqMask  (HDSPM_wc_freq0|HDSPM_wc_freq1|HDSPM_wc_freq2|\
                            HDSPM_wc_freq3)
#define HDSPM_wcFreq32    (HDSPM_wc_freq0)
#define HDSPM_wcFreq44_1  (HDSPM_wc_freq1)
#define HDSPM_wcFreq48    (HDSPM_wc_freq0|HDSPM_wc_freq1)
#define HDSPM_wcFreq64    (HDSPM_wc_freq2)
#define HDSPM_wcFreq88_2  (HDSPM_wc_freq0|HDSPM_wc_freq2)
#define HDSPM_wcFreq96    (HDSPM_wc_freq1|HDSPM_wc_freq2)
#define HDSPM_wcFreq128   (HDSPM_wc_freq0|HDSPM_wc_freq1|HDSPM_wc_freq2)
#define HDSPM_wcFreq176_4 (HDSPM_wc_freq3)
#define HDSPM_wcFreq192   (HDSPM_wc_freq0|HDSPM_wc_freq3)

#define HDSPM_status1_F_0 0x0400000
#define HDSPM_status1_F_1 0x0800000
#define HDSPM_status1_F_2 0x1000000
#define HDSPM_status1_F_3 0x2000000
#define HDSPM_status1_freqMask (HDSPM_status1_F_0|HDSPM_status1_F_1|HDSPM_status1_F_2|HDSPM_status1_F_3)
#define HDSPM_SelSyncRefMask       (HDSPM_SelSyncRef0|HDSPM_SelSyncRef1|\
                                    HDSPM_SelSyncRef2)

#define HDSPM_Start                (1<<0)
#define HDSPM_Latency0             (1<<1)
#define HDSPM_Latency1             (1<<2)
#define HDSPM_Latency2             (1<<3)
#define HDSPM_ClockModeMaster      (1<<4)
#define HDSPM_c0Master		0x1
#define HDSPM_AudioInterruptEnable (1<<5)
#define HDSPM_Frequency0  (1<<6)
#define HDSPM_Frequency1  (1<<7)
#define HDSPM_DoubleSpeed (1<<8)
#define HDSPM_QuadSpeed   (1<<31)
#define HDSPM_Professional (1<<9)
#define HDSPM_TX_64ch     (1<<10)
#define HDSPM_Emphasis    (1<<10)
#define HDSPM_AutoInp     (1<<11)
#define HDSPM_Dolby       (1<<11)
#define HDSPM_InputSelect0 (1<<14)
#define HDSPM_InputSelect1 (1<<15)
#define HDSPM_SyncRef2     (1<<13)
#define HDSPM_SyncRef3     (1<<25)
#define HDSPM_SMUX         (1<<18)
#define HDSPM_clr_tms      (1<<19)
#define HDSPM_taxi_reset   (1<<20)
#define HDSPM_WCK48        (1<<20)
#define HDSPM_Midi0InterruptEnable 0x0400000
#define HDSPM_Midi1InterruptEnable 0x0800000
#define HDSPM_Midi2InterruptEnable 0x0200000
#define HDSPM_Midi3InterruptEnable 0x4000000
#define HDSPM_LineOut (1<<24)
#define HDSPe_FLOAT_FORMAT         0x2000000
#define HDSPM_DS_DoubleWire (1<<26)
#define HDSPM_QS_DoubleWire (1<<27)
#define HDSPM_QS_QuadWire   (1<<28)
#define HDSPM_wclk_sel (1<<30)
#define HDSPM_c0_Wck48			0x20
#define HDSPM_c0_Input0			0x1000
#define HDSPM_c0_Input1			0x2000
#define HDSPM_c0_Spdif_Opt			0x4000
#define HDSPM_c0_Pro				0x8000
#define HDSPM_c0_clr_tms			0x10000
#define HDSPM_c0_AEB1				0x20000
#define HDSPM_c0_AEB2				0x40000
#define HDSPM_c0_LineOut			0x80000
#define HDSPM_c0_AD_GAIN0			0x100000
#define HDSPM_c0_AD_GAIN1			0x200000
#define HDSPM_c0_DA_GAIN0			0x400000
#define HDSPM_c0_DA_GAIN1			0x800000
#define HDSPM_c0_PH_GAIN0			0x1000000
#define HDSPM_c0_PH_GAIN1			0x2000000
#define HDSPM_c0_Sym6db				0x4000000

/* Other constants */
#define MADI_SS_CHANNELS       64
#define MADI_DS_CHANNELS       32
#define MADI_QS_CHANNELS       16
#define RAYDAT_SS_CHANNELS     36
#define RAYDAT_DS_CHANNELS     20
#define RAYDAT_QS_CHANNELS     12
#define AIO_IN_SS_CHANNELS        14
#define AIO_IN_DS_CHANNELS        10
#define AIO_IN_QS_CHANNELS        8
#define AIO_OUT_SS_CHANNELS        16
#define AIO_OUT_DS_CHANNELS        12
#define AIO_OUT_QS_CHANNELS        10
#define AES32_CHANNELS		16

#define HDSPM_CHANNEL_BUFFER_SAMPLES  (16*1024)
#define HDSPM_CHANNEL_BUFFER_BYTES    (4*HDSPM_CHANNEL_BUFFER_SAMPLES)
#define HDSPM_DMA_AREA_BYTES (HDSPM_MAX_CHANNELS * HDSPM_CHANNEL_BUFFER_BYTES)
#define HDSPM_DMA_AREA_KILOBYTES (HDSPM_DMA_AREA_BYTES/1024)

#define HDSPM_RAYDAT_REV	211
#define HDSPM_AIO_REV		212
#define HDSPM_MADIFACE_REV	213

#define HDSPM_SPEED_SINGLE 0
#define HDSPM_SPEED_DOUBLE 1
#define HDSPM_SPEED_QUAD   2

#define HDSPM_MADI_INPUT_PEAK		4096
#define HDSPM_MADI_PLAYBACK_PEAK	4352
#define HDSPM_MADI_OUTPUT_PEAK		4608
#define HDSPM_MADI_INPUT_RMS_L		6144
#define HDSPM_MADI_PLAYBACK_RMS_L	6400
#define HDSPM_MADI_OUTPUT_RMS_L		6656
#define HDSPM_MADI_INPUT_RMS_H		7168
#define HDSPM_MADI_PLAYBACK_RMS_H	7424
#define HDSPM_MADI_OUTPUT_RMS_H		7680

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
    uint32_t mmio_regs[BAR0_SIZE / 4];

    /* DMA Context not needed, removed */
    /* Operational status flags */
    uint8_t operational_status;

    /* State used to handle reset sequences */
    int reset_state;

    /* Power management state (D0-D3) */
    uint8_t power_state;

    /* Other additions */
    /* MIDI state, TCO state, etc. */
};

/* Other additional definitions */

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    uint32_t idx = addr >> 2;

    switch (addr) {
    case HDSPM_statusRegister: /* 0 */
        /* Report status bits that make driver think MADI is locked and synced at 48kHz */
        val = 0;
        if (s->intr_status & HDSPM_audioIRQPending) {
            val |= HDSPM_audioIRQPending;
        }
        val |= HDSPM_madiLock | HDSPM_madiSync;
        val |= HDSPM_madiFreq48; /* indicate 48kHz */
        val |= (0x00000000); /* buffer position 0 */
        break;

    case HDSPM_statusRegister2: /* 192 */
        /* Provide version bits and pretend word clock is also present */
        val = HDSPM_version0 | HDSPM_version1 | HDSPM_version2;
        val |= HDSPM_wcLock | HDSPM_wcSync;
        val |= HDSPM_wcFreq48; /* word clock 48kHz */
        break;

    case HDSPM_RD_STATUS_1:  /* 64 */
    case HDSPM_RD_STATUS_2:  /* 128 */
        /* Currently returning 0, can be extended if needed */
        val = 0;
        break;

    default:
        if (idx < (BAR0_SIZE / 4)) {
            val = s->mmio_regs[idx];
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    uint32_t idx = addr >> 2;

    switch (addr) {
    case HDSPM_controlRegister: /* 64 */
        s->mmio_regs[idx] = val;
        /* If driver sets AudioInterruptEnable and Start, we could later raise interrupts */
        break;

    case HDSPM_interruptConfirmation: /* 96 */
        /* Writing 0 acknowledges and clears all pending interrupts */
        s->intr_status = 0;
        pcibase_update_irq(s);
        break;

    /* Read-only status registers are ignored on write */
    case HDSPM_statusRegister:
    case HDSPM_statusRegister2:
    case HDSPM_RD_STATUS_2:
        break;

    default:
        if (idx < (BAR0_SIZE / 4)) {
            s->mmio_regs[idx] = val;
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Legacy PIO not supported */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Legacy PIO not supported */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    /* Reset all internal state */
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    s->intr_status = 0;
    s->operational_status = 0;
    s->reset_state = 0;
    s->power_state = 0; /* D0 */

    /* Note: status registers are computed dynamically in reads, no need to preset them here */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_XILINX );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP_MADI );
    pci_config_set_class(pci_conf, PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0xd2); /* MADI revision */
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "BAR0" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI or MSI-X initialization */

    /* DMA configuration not needed */

    /* Timer initialization not needed */

    /* Final state initialization: ensure reset initial state */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No DMA or timers to clean up */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_hdspm_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(mmio_regs, PCIBaseState, BAR0_SIZE / 4),
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