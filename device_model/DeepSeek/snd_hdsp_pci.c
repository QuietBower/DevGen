/*
 * QEMU device model for RME Hammerfall DSP (HDSP) - 9652 variant
 * Based on Linux driver sound/pci/rme9652/hdsp.c
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
#include "hw/pci/pci_ids.h"

/* PCI IDs not in standard QEMU headers */
#ifndef PCI_VENDOR_ID_XILINX
#define PCI_VENDOR_ID_XILINX 0x10ee
#endif
#ifndef PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP
#define PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP 0x3fc5
#endif

#define TYPE_PCIBASE_DEVICE "snd_hdsp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI IDs */
#define VENDOR_ID PCI_VENDOR_ID_XILINX
#define DEVICE_ID PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP
#define CLASS_ID  PCI_CLASS_MULTIMEDIA_AUDIO

/* Register offsets */
#define HDSP_resetPointer               0
#define HDSP_freqReg                    0
#define HDSP_outputBufferAddress        32
#define HDSP_inputBufferAddress         36
#define HDSP_controlRegister            64
#define HDSP_interruptConfirmation      96
#define HDSP_outputEnable               128
#define HDSP_control2Reg                256
#define HDSP_midiDataOut0               352
#define HDSP_midiDataOut1               356
#define HDSP_fifoData                   368
#define HDSP_inputEnable                384
#define HDSP_statusRegister             0
#define HDSP_timecode                   128
#define HDSP_status2Register            192
#define HDSP_midiDataIn0                360
#define HDSP_midiDataIn1                364
#define HDSP_midiStatusOut0             384
#define HDSP_midiStatusOut1             388
#define HDSP_midiStatusIn0              392
#define HDSP_midiStatusIn1              396
#define HDSP_fifoStatus                 400
#define HDSP_playbackPeakLevel          4096
#define HDSP_inputPeakLevel             4224
#define HDSP_outputPeakLevel            4352
#define HDSP_playbackRmsLevel           4612
#define HDSP_inputRmsLevel              4868
#define HDSP_9652_peakBase              7164
#define HDSP_9652_rmsBase               4096
#define HDSP_9632_metersBase            4096
#define HDSP_IO_EXTENT                  7168

#define HDSP_NUM_REGS (HDSP_IO_EXTENT / sizeof(uint32_t))

/* Control bits */
#define HDSP_TMS                0x01
#define HDSP_TCK                0x02
#define HDSP_TDI                0x04
#define HDSP_JTAG               0x08
#define HDSP_PWDN               0x10
#define HDSP_PROGRAM            0x020
#define HDSP_CONFIG_MODE_0      0x040
#define HDSP_CONFIG_MODE_1      0x080
#define HDSP_VERSION_BIT        (0x100 | HDSP_S_LOAD)
#define HDSP_BIGENDIAN_MODE     0x200
#define HDSP_RD_MULTIPLE        0x400
#define HDSP_9652_ENABLE_MIXER  0x800
#define HDSP_S200               0x800
#define HDSP_S300               (0x100 | HDSP_S200)
#define HDSP_CYCLIC_MODE        0x1000
#define HDSP_TDO                0x10000000
#define HDSP_S_PROGRAM          (HDSP_CYCLIC_MODE|HDSP_PROGRAM|HDSP_CONFIG_MODE_0)
#define HDSP_S_LOAD             (HDSP_CYCLIC_MODE|HDSP_PROGRAM|HDSP_CONFIG_MODE_1)

#define HDSP_Start                (1<<0)
#define HDSP_Latency0             (1<<1)
#define HDSP_Latency1             (1<<2)
#define HDSP_Latency2             (1<<3)
#define HDSP_ClockModeMaster      (1<<4)
#define HDSP_AudioInterruptEnable (1<<5)
#define HDSP_Frequency0           (1<<6)
#define HDSP_Frequency1           (1<<7)
#define HDSP_DoubleSpeed          (1<<8)
#define HDSP_SPDIFProfessional    (1<<9)
#define HDSP_SPDIFEmphasis        (1<<10)
#define HDSP_SPDIFNonAudio        (1<<11)
#define HDSP_SPDIFOpticalOut      (1<<12)
#define HDSP_SyncRef2             (1<<13)
#define HDSP_SPDIFInputSelect0    (1<<14)
#define HDSP_SPDIFInputSelect1    (1<<15)
#define HDSP_SyncRef0             (1<<16)
#define HDSP_SyncRef1             (1<<17)
#define HDSP_AnalogExtensionBoard (1<<18)
#define HDSP_XLRBreakoutCable     (1<<20)
#define HDSP_Midi0InterruptEnable (1<<22)
#define HDSP_Midi1InterruptEnable (1<<23)
#define HDSP_LineOut              (1<<24)
#define HDSP_ADGain0              (1<<25)
#define HDSP_ADGain1              (1<<26)
#define HDSP_DAGain0              (1<<27)
#define HDSP_DAGain1              (1<<28)
#define HDSP_PhoneGain0           (1<<29)
#define HDSP_PhoneGain1           (1<<30)
#define HDSP_QuadSpeed            (1<<31)

/* Status bits */
#define HDSP_audioIRQPending    (1<<0)
#define HDSP_Lock2              (1<<1)
#define HDSP_spdifFrequency3    HDSP_Lock2
#define HDSP_Lock1              (1<<2)
#define HDSP_Lock0              (1<<3)
#define HDSP_SPDIFSync          (1<<4)
#define HDSP_TimecodeLock       (1<<5)
#define HDSP_BufferPositionMask 0x000FFC0
#define HDSP_Sync2              (1<<16)
#define HDSP_Sync1              (1<<17)
#define HDSP_Sync0              (1<<18)
#define HDSP_DoubleSpeedStatus  (1<<19)
#define HDSP_ConfigError        (1<<20)
#define HDSP_DllError           (1<<21)
#define HDSP_spdifFrequency0    (1<<22)
#define HDSP_spdifFrequency1    (1<<23)
#define HDSP_spdifFrequency2    (1<<24)
#define HDSP_SPDIFErrorFlag     (1<<25)
#define HDSP_BufferID           (1<<26)
#define HDSP_TimecodeSync       (1<<27)
#define HDSP_AEBO               (1<<28)
#define HDSP_AEBI               (1<<29)
#define HDSP_midi0IRQPending    (1<<30)
#define HDSP_midi1IRQPending    (1<<31)

#define HDSP_version0     (1<<0)
#define HDSP_version1     (1<<1)
#define HDSP_version2     (1<<2)
#define HDSP_wc_lock      (1<<3)
#define HDSP_wc_sync      (1<<4)

/* Mixer memory for 9652 (addressable via offset 4096) */
#define HDSP_9652_MIXER_ENTRIES 676

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware registers */
    uint32_t control_register;     /* offset 64 */
    uint32_t control2_register;    /* offset 256 */
    uint32_t status_register;      /* offset 0, read-only */
    uint32_t status2_register;     /* offset 192, read-only */
    uint32_t dma_output_addr;      /* offset 32 */
    uint32_t dma_input_addr;       /* offset 36 */
    uint32_t midi_data_in[2];      /* offsets 360, 364 */
    uint32_t midi_status_out[2];   /* offsets 384, 388 */
    uint32_t midi_status_in[2];    /* offsets 392, 396 */
    uint32_t fifo_status;          /* offset 400 */
    uint32_t mixer_ram[HDSP_9652_MIXER_ENTRIES]; /* offset 4096+ */
};

/* MMIO handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case HDSP_statusRegister:          /* 0 */
        val = s->status_register;
        break;
    case HDSP_controlRegister:         /* 64 */
        val = s->control_register;
        break;
    case HDSP_status2Register:         /* 192 */
        val = s->status2_register;
        break;
    case HDSP_control2Reg:             /* 256 */
        val = s->control2_register;
        break;
    case HDSP_midiDataIn0:             /* 360 */
        val = s->midi_data_in[0];
        break;
    case HDSP_midiDataIn1:             /* 364 */
        val = s->midi_data_in[1];
        break;
    case HDSP_midiStatusOut0:          /* 384 */
        val = s->midi_status_out[0];
        break;
    case HDSP_midiStatusOut1:          /* 388 */
        val = s->midi_status_out[1];
        break;
    case HDSP_midiStatusIn0:           /* 392 */
        val = s->midi_status_in[0];
        break;
    case HDSP_midiStatusIn1:           /* 396 */
        val = s->midi_status_in[1];
        break;
    case HDSP_fifoStatus:              /* 400 */
        val = s->fifo_status;
        break;
    default:
        if (addr >= 4096 && addr + 4 <= HDSP_IO_EXTENT) {
            uint32_t idx = (addr - 4096) / 4;
            if (idx < HDSP_9652_MIXER_ENTRIES) {
                val = s->mixer_ram[idx];
            }
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

    switch (addr) {
    case HDSP_resetPointer:            /* 0 (also freqReg) */
        /* Driver writes DDS value or 0; no functional effect needed */
        break;
    case HDSP_outputBufferAddress:     /* 32 */
        s->dma_output_addr = (uint32_t)val;
        break;
    case HDSP_inputBufferAddress:      /* 36 */
        s->dma_input_addr = (uint32_t)val;
        break;
    case HDSP_controlRegister:         /* 64 */
        s->control_register = (uint32_t)val;
        break;
    case HDSP_interruptConfirmation:   /* 96 */
        if (val == 0) {
            /* Clear interrupt pending flags on write of 0 */
            s->status_register &= ~(HDSP_audioIRQPending |
                                    HDSP_midi0IRQPending |
                                    HDSP_midi1IRQPending);
        }
        break;
    case HDSP_outputEnable:            /* 128 */
        /* Ignore: driver enables outputs */
        break;
    case HDSP_control2Reg:             /* 256 */
        s->control2_register = (uint32_t)val;
        break;
    case HDSP_midiDataOut0:           /* 352 */
        break;
    case HDSP_midiDataOut1:           /* 356 */
        break;
    case HDSP_fifoData:                /* 368 */
        /* Firmware loading, not used for 9652 */
        break;
    case HDSP_inputEnable:             /* 384 */
        /* Ignore: driver enables inputs */
        break;
    case HDSP_inputEnable + 4:        /* 388 */
    case HDSP_inputEnable + 8:        /* 392 */
    case HDSP_inputEnable + 12:       /* 396 */
        /* Additional channels, ignore */
        break;
    default:
        if (addr >= 4096 && addr + 4 <= HDSP_IO_EXTENT) {
            uint32_t idx = (addr - 4096) / 4;
            if (idx < HDSP_9652_MIXER_ENTRIES) {
                s->mixer_ram[idx] = (uint32_t)val;
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used */
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

    /* Set initial register values */
    s->control_register = 0;
    s->control2_register = 0;
    s->status_register = 0;
    s->status2_register = 0;
    s->dma_output_addr = 0;
    s->dma_input_addr = 0;
    memset(s->midi_data_in, 0, sizeof(s->midi_data_in));
    memset(s->midi_status_out, 0, sizeof(s->midi_status_out));
    memset(s->midi_status_in, 0, sizeof(s->midi_status_in));
    s->fifo_status = 0;
    memset(s->mixer_ram, 0, sizeof(s->mixer_ram));
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
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x64); /* Firmware rev for 9652 */
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = HDSP_IO_EXTENT, .name = "hdsp-mmio" };
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
    .name = "snd_hdsp_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(control_register, PCIBaseState),
        VMSTATE_UINT32(control2_register, PCIBaseState),
        VMSTATE_UINT32(status_register, PCIBaseState),
        VMSTATE_UINT32(status2_register, PCIBaseState),
        VMSTATE_UINT32(dma_output_addr, PCIBaseState),
        VMSTATE_UINT32(dma_input_addr, PCIBaseState),
        VMSTATE_UINT32_ARRAY(midi_data_in, PCIBaseState, 2),
        VMSTATE_UINT32_ARRAY(midi_status_out, PCIBaseState, 2),
        VMSTATE_UINT32_ARRAY(midi_status_in, PCIBaseState, 2),
        VMSTATE_UINT32(fifo_status, PCIBaseState),
        VMSTATE_UINT32_ARRAY(mixer_ram, PCIBaseState, HDSP_9652_MIXER_ENTRIES),
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
