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
#define PCI_VENDOR_ID_XILINX		0x10ee
#define PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP 0x3fc5

#define TYPE_PCIBASE_DEVICE "snd_hdsp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define HDSP_resetPointer               0
#define HDSP_freqReg			0
#define HDSP_outputBufferAddress	32
#define HDSP_inputBufferAddress		36
#define HDSP_controlRegister		64
#define HDSP_interruptConfirmation	96
#define HDSP_outputEnable	  	128
#define HDSP_control2Reg		256
#define HDSP_midiDataOut0  		352
#define HDSP_midiDataOut1  		356
#define HDSP_fifoData  			368
#define HDSP_inputEnable	 	384
#define HDSP_statusRegister    0
#define HDSP_timecode        128
#define HDSP_status2Register 192
#define HDSP_midiDataIn0     360
#define HDSP_midiDataIn1     364
#define HDSP_midiStatusOut0  384
#define HDSP_midiStatusOut1  388
#define HDSP_midiStatusIn0   392
#define HDSP_midiStatusIn1   396
#define HDSP_fifoStatus      400
#define HDSP_playbackPeakLevel  4096
#define HDSP_inputPeakLevel     4224
#define HDSP_outputPeakLevel    4352
#define HDSP_playbackRmsLevel   4612
#define HDSP_inputRmsLevel      4868
#define HDSP_9652_peakBase	7164
#define HDSP_9652_rmsBase	4096
#define HDSP_9632_metersBase	4096
#define HDSP_IO_EXTENT     7168

#define HDSP_audioIRQPending    (1<<0)
#define HDSP_midi0IRQPending    (1<<30)
#define HDSP_midi1IRQPending    (1<<31)

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

struct hdsp_9632_meters {
    uint32_t input_peak[16];
    uint32_t playback_peak[16];
    uint32_t output_peak[16];
    uint32_t xxx_peak[16];
    uint32_t padding[64];
    uint32_t input_rms_low[16];
    uint32_t playback_rms_low[16];
    uint32_t output_rms_low[16];
    uint32_t xxx_rms_low[16];
    uint32_t input_rms_high[16];
    uint32_t playback_rms_high[16];
    uint32_t output_rms_high[16];
    uint32_t xxx_rms_high[16];
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t interrupt_status;
    uint32_t interrupt_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t control_register;
    uint32_t control2_register;
    uint32_t status_register;
    uint32_t status2_register;

    /* DMA Context */
    uint32_t output_buffer_address;
    uint32_t input_buffer_address;

    struct hdsp_9632_meters meters;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_state = false;

    if (s->status_register & (HDSP_audioIRQPending | HDSP_midi0IRQPending | HDSP_midi1IRQPending)) {
        irq_state = true;
    }

    if (msi_enabled(pdev)) {
        if (irq_state) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, irq_state ? 1 : 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t buf[256] = {0};

    if (is_write && s->output_buffer_address) {
        pci_dma_write(pdev, s->output_buffer_address, buf, sizeof(buf));
    } else if (!is_write && s->input_buffer_address) {
        pci_dma_read(pdev, s->input_buffer_address, buf, sizeof(buf));
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case HDSP_statusRegister:
        val = s->status_register;
        break;
    case HDSP_status2Register:
        val = s->status2_register;
        break;
    case HDSP_fifoStatus:
        val = 0;
        break;
    case HDSP_midiStatusOut0:
    case HDSP_midiStatusOut1:
        val = 0;
        break;
    case HDSP_midiStatusIn0:
    case HDSP_midiStatusIn1:
        val = 0;
        break;
    case HDSP_midiDataIn0:
    case HDSP_midiDataIn1:
        val = 0;
        break;
    default:
        if (addr >= HDSP_9632_metersBase && addr < HDSP_IO_EXTENT) {
            uint32_t offset = addr - HDSP_9632_metersBase;
            if (offset + size <= sizeof(s->meters)) {
                memcpy(&val, (uint8_t *)&s->meters + offset, size);
            }
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HDSP_resetPointer:
        /* Also HDSP_freqReg */
        break;
    case HDSP_outputBufferAddress:
        s->output_buffer_address = val;
        break;
    case HDSP_inputBufferAddress:
        s->input_buffer_address = val;
        break;
    case HDSP_controlRegister:
        s->control_register = val;
        pcibase_update_irq(s);
        break;
    case HDSP_control2Reg:
        s->control2_register = val;
        break;
    case HDSP_interruptConfirmation:
        s->status_register &= ~(HDSP_audioIRQPending | HDSP_midi0IRQPending | HDSP_midi1IRQPending);
        pcibase_update_irq(s);
        break;
    case HDSP_fifoData:
        break;
    default:
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

    s->control_register = 0;
    s->control2_register = 0;
    s->status_register = 0;
    s->status2_register = 0;
    s->output_buffer_address = 0;
    s->input_buffer_address = 0;
    memset(&s->meters, 0, sizeof(s->meters));
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_XILINX_HAMMERFALL_DSP );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x64);
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
    s->bar_info[0].size = HDSP_IO_EXTENT;
    s->bar_info[0].name = "hdsp-mmio";
  
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_hdsp_pci",
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
