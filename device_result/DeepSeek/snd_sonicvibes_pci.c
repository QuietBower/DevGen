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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

// Additional include files retrieved from driver context

#define TYPE_PCIBASE_DEVICE "snd_sonicvibes_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

// Register Layout and Hardware Identifiers extracted from driver source
// PCI Identification
#define PCI_VENDOR_ID_S3           0x5333
#define SONICVIBES_VENDOR_ID        PCI_VENDOR_ID_S3
#define SONICVIBES_DEVICE_ID        0xca00
#define SONICVIBES_CLASS_ID         0x040100   // PCI_CLASS_MULTIMEDIA_AUDIO

// Register Offsets from enhanced port (enh_port)
#define SV_REG_CONTROL      0x00
#define SV_REG_IRQMASK      0x01
#define SV_REG_STATUS       0x02
#define SV_REG_INDEX        0x04
#define SV_REG_DATA         0x05

// Control Register Bits
#define SV_ENHANCED         0x01
#define SV_TEST             0x02
#define SV_REVERB           0x04
#define SV_WAVETABLE        0x08
#define SV_INTA             0x20
#define SV_RESET            0x80

// IRQ Mask Bits
#define SV_DMAA_MASK        0x01
#define SV_DMAC_MASK        0x04
#define SV_SPEC_MASK        0x08
#define SV_UD_MASK          0x40
#define SV_MIDI_MASK        0x80

// Status Register Bits (IRQ sources)
#define SV_DMAA_IRQ         0x01
#define SV_DMAC_IRQ         0x04
#define SV_SPEC_IRQ         0x08
#define SV_UD_IRQ           0x40
#define SV_MIDI_IRQ         0x80

// Indirect Register Index (Data Port 0x05)
#define SV_IREG_LEFT_ADC            0x00
#define SV_IREG_RIGHT_ADC           0x01
#define SV_IREG_LEFT_AUX1           0x02
#define SV_IREG_RIGHT_AUX1          0x03
#define SV_IREG_LEFT_CD             0x04
#define SV_IREG_RIGHT_CD            0x05
#define SV_IREG_LEFT_LINE           0x06
#define SV_IREG_RIGHT_LINE          0x07
#define SV_IREG_MIC                 0x08
#define SV_IREG_GAME_PORT           0x09
#define SV_IREG_LEFT_SYNTH          0x0a
#define SV_IREG_RIGHT_SYNTH         0x0b
#define SV_IREG_LEFT_AUX2           0x0c
#define SV_IREG_RIGHT_AUX2          0x0d
#define SV_IREG_LEFT_ANALOG         0x0e
#define SV_IREG_RIGHT_ANALOG        0x0f
#define SV_IREG_LEFT_PCM            0x10
#define SV_IREG_RIGHT_PCM           0x11
#define SV_IREG_DMA_DATA_FMT        0x12
#define SV_IREG_PC_ENABLE           0x13
#define SV_IREG_UD_BUTTON           0x14
#define SV_IREG_REVISION            0x15
#define SV_IREG_ADC_OUTPUT_CTRL     0x16
#define SV_IREG_DMA_A_UPPER         0x18
#define SV_IREG_DMA_A_LOWER         0x19
#define SV_IREG_DMA_C_UPPER         0x1c
#define SV_IREG_DMA_C_LOWER         0x1d
#define SV_IREG_PCM_RATE_LOW        0x1e
#define SV_IREG_PCM_RATE_HIGH       0x1f
#define SV_IREG_SYNTH_RATE_LOW      0x20
#define SV_IREG_SYNTH_RATE_HIGH     0x21
#define SV_IREG_ADC_CLOCK           0x22
#define SV_IREG_ADC_ALT_RATE        0x23
#define SV_IREG_ADC_PLL_M           0x24
#define SV_IREG_ADC_PLL_N           0x25
#define SV_IREG_SYNTH_PLL_M         0x26
#define SV_IREG_SYNTH_PLL_N         0x27
#define SV_IREG_MPU401              0x2a
#define SV_IREG_DRIVE_CTRL          0x2b
#define SV_IREG_SRS_SPACE           0x2c
#define SV_IREG_SRS_CENTER          0x2d
#define SV_IREG_WAVE_SOURCE         0x2e
#define SV_IREG_ANALOG_POWER        0x30
#define SV_IREG_DIGITAL_POWER       0x31

// DMA I/O Port Offsets (for DMA controller)
#define SV_DMA_ADDR0            0x00
#define SV_DMA_ADDR1            0x01
#define SV_DMA_ADDR2            0x02
#define SV_DMA_ADDR3            0x03
#define SV_DMA_COUNT0           0x04
#define SV_DMA_COUNT1           0x05
#define SV_DMA_COUNT2           0x06
#define SV_DMA_MODE             0x0b
#define SV_DMA_RESET            0x0d
#define SV_DMA_MASK             0x0f

// PCI Config Space Offsets for DDMA Base Addresses
#define SV_PCI_DMA_A            0x40
#define SV_PCI_DMA_C            0x48

// Constants
#define SV_FULLRATE             48000
#define SV_REFFREQUENCY         24576000
#define SV_ADCMULT              512
#define SV_MODE_PLAY            1
#define SV_MODE_CAPTURE         2

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    // Hardware Register Shadows
    uint8_t sv_control;
    uint8_t sv_irqmask;
    uint8_t sv_status;
    uint8_t sv_index;
    uint8_t sv_data;
    uint8_t sv_indirect[0x40];
    unsigned char enable;
    unsigned char revision;
    unsigned char format;
    unsigned char srs_space;
    unsigned char srs_center;
    unsigned char mpu_switch;
    unsigned char wave_source;
    unsigned int mode;
    unsigned int p_dma_size;
    unsigned int c_dma_size;

    // DMA Context
    uint32_t dma_addr[2];
    uint32_t dma_count[2];
    uint8_t dma_mode[2];
    uint8_t dma_mask[2];

    uint32_t irq_status;

    uint32_t dmaa_port;
    uint32_t dmac_port;

    // Memory regions for DDMA I/O
    MemoryRegion ddma_a_region;
    MemoryRegion ddma_c_region;
};

// Forward declarations
static void pcibase_reset(DeviceState *dev);

// ---- MMIO/PIO handlers ----

// Enhanced registers PIO (BAR1)
static uint64_t pcibase_pio_enh_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case SV_REG_CONTROL:
        val = s->sv_control;
        break;
    case SV_REG_IRQMASK:
        val = s->sv_irqmask;
        break;
    case SV_REG_STATUS:
        val = s->sv_status;
        s->sv_status = 0; // Reading clears status
        break;
    case SV_REG_INDEX:
        val = s->sv_index;
        break;
    case SV_REG_DATA:
        val = s->sv_indirect[s->sv_index & 0x3f];
        break;
    default:
        val = 0xff;
        break;
    }
    return val;
}

static void pcibase_pio_enh_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SV_REG_CONTROL:
        s->sv_control = val;
        // Handle reset pulse? We'll ignore for now, driver does explicit reset sequence.
        break;
    case SV_REG_IRQMASK:
        s->sv_irqmask = val;
        break;
    case SV_REG_INDEX:
        s->sv_index = val;
        break;
    case SV_REG_DATA:
        s->sv_indirect[s->sv_index & 0x3f] = val;
        break;
    case SV_REG_STATUS: // not writable
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_enh_ops = {
    .read = pcibase_pio_enh_read,
    .write = pcibase_pio_enh_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

// Simple pass-through PIO (for BAR0, BAR2, BAR3, BAR4)
static uint64_t pcibase_pio_simple_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0x00;
}

static void pcibase_pio_simple_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_pio_simple_ops = {
    .read = pcibase_pio_simple_read,
    .write = pcibase_pio_simple_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

// DDMA PIO handlers
static uint64_t ddma_a_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case SV_DMA_ADDR0:
        val = s->dma_addr[0];
        break;
    case SV_DMA_COUNT0:
        val = s->dma_count[0] & 0xffffff;
        break;
    case SV_DMA_MODE:
        val = s->dma_mode[0];
        break;
    case SV_DMA_RESET:
        val = 0;
        break;
    case SV_DMA_MASK:
        val = s->dma_mask[0];
        break;
    default:
        val = 0xff;
        break;
    }
    return val;
}

static void ddma_a_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SV_DMA_ADDR0:
        s->dma_addr[0] = val;
        break;
    case SV_DMA_COUNT0:
        s->dma_count[0] = val;
        break;
    case SV_DMA_MODE:
        s->dma_mode[0] = val;
        break;
    case SV_DMA_RESET:
        // ignore
        break;
    case SV_DMA_MASK:
        s->dma_mask[0] = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps ddma_a_ops = {
    .read = ddma_a_read,
    .write = ddma_a_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static uint64_t ddma_c_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case SV_DMA_ADDR0:
        val = s->dma_addr[1];
        break;
    case SV_DMA_COUNT0:
        val = s->dma_count[1] & 0xffffff;
        break;
    case SV_DMA_MODE:
        val = s->dma_mode[1];
        break;
    case SV_DMA_RESET:
        val = 0;
        break;
    case SV_DMA_MASK:
        val = s->dma_mask[1];
        break;
    default:
        val = 0xff;
        break;
    }
    return val;
}

static void ddma_c_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SV_DMA_ADDR0:
        s->dma_addr[1] = val;
        break;
    case SV_DMA_COUNT0:
        s->dma_count[1] = val;
        break;
    case SV_DMA_MODE:
        s->dma_mode[1] = val;
        break;
    case SV_DMA_RESET:
        // ignore
        break;
    case SV_DMA_MASK:
        s->dma_mask[1] = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps ddma_c_ops = {
    .read = ddma_c_read,
    .write = ddma_c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

// ---- MMIO/PIO stubs (unused) ----
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

// ---- Reset ----
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->sv_control = 0;
    s->sv_irqmask = 0;
    s->sv_status = 0;
    s->sv_index = 0;
    s->sv_data = 0;
    memset(s->sv_indirect, 0, sizeof(s->sv_indirect));
    s->sv_indirect[SV_IREG_REVISION] = 0x02;
    s->enable = 0;
    s->revision = 0x02;
    s->format = 0;
    s->srs_space = 0;
    s->srs_center = 0;
    s->mpu_switch = 0;
    s->wave_source = 0;
    s->mode = 0;
    s->p_dma_size = 0;
    s->c_dma_size = 0;
    for (int i = 0; i < 2; i++) {
        s->dma_addr[i] = 0;
        s->dma_count[i] = 0;
        s->dma_mode[i] = 0;
        s->dma_mask[i] = 0;
    }
    s->irq_status = 0;
    s->dmaa_port = 0;
    s->dmac_port = 0;
}

// ---- Realize ----
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  SONICVIBES_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SONICVIBES_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SONICVIBES_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    // DDMA base addresses in PCI config (vendor-specific)
    pci_set_long(pci_conf + SV_PCI_DMA_A, 0x300);
    pci_set_long(pci_conf + SV_PCI_DMA_C, 0x310);

    // BAR 0: sb_port (Sound Blaster emulation) - simple pass-through
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_pio_simple_ops, s,
                          "snd_sonicvibes_sb", 0x10);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[0]);

    // BAR 1: enh_port (Enhanced registers) - real enhanced interface
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_pio_enh_ops, s,
                          "snd_sonicvibes_enh", 0x10);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[1]);

    // BAR 2: synth_port (OPL3) - simple pass-through
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_pio_simple_ops, s,
                          "snd_sonicvibes_synth", 0x10);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[2]);

    // BAR 3: midi_port (MPU-401) - simple pass-through
    memory_region_init_io(&s->bar_regions[3], OBJECT(s), &pcibase_pio_simple_ops, s,
                          "snd_sonicvibes_midi", 0x10);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[3]);

    // BAR 4: game_port - simple pass-through
    memory_region_init_io(&s->bar_regions[4], OBJECT(s), &pcibase_pio_simple_ops, s,
                          "snd_sonicvibes_game", 0x10);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[4]);

    // DDMA I/O regions
    memory_region_init_io(&s->ddma_a_region, OBJECT(s), &ddma_a_ops, s,
                          "snd_sonicvibes_ddma_a", 0x10);
    memory_region_add_subregion(pci_address_space_io(pdev), 0x300, &s->ddma_a_region);

    memory_region_init_io(&s->ddma_c_region, OBJECT(s), &ddma_c_ops, s,
                          "snd_sonicvibes_ddma_c", 0x10);
    memory_region_add_subregion(pci_address_space_io(pdev), 0x310, &s->ddma_c_region);

    // Set default register values after reset
    pcibase_reset(DEVICE(s));
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

    memory_region_del_subregion(pci_address_space_io(pdev), &s->ddma_a_region);
    memory_region_del_subregion(pci_address_space_io(pdev), &s->ddma_c_region);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_sonicvibes_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(sv_control, PCIBaseState),
        VMSTATE_UINT8(sv_irqmask, PCIBaseState),
        VMSTATE_UINT8(sv_status, PCIBaseState),
        VMSTATE_UINT8(sv_index, PCIBaseState),
        VMSTATE_UINT8(sv_data, PCIBaseState),
        VMSTATE_UINT8_ARRAY(sv_indirect, PCIBaseState, 0x40),
        VMSTATE_UINT8(enable, PCIBaseState),
        VMSTATE_UINT8(revision, PCIBaseState),
        VMSTATE_UINT8(format, PCIBaseState),
        VMSTATE_UINT8(srs_space, PCIBaseState),
        VMSTATE_UINT8(srs_center, PCIBaseState),
        VMSTATE_UINT8(mpu_switch, PCIBaseState),
        VMSTATE_UINT8(wave_source, PCIBaseState),
        VMSTATE_UINT32(mode, PCIBaseState),
        VMSTATE_UINT32(p_dma_size, PCIBaseState),
        VMSTATE_UINT32(c_dma_size, PCIBaseState),
        VMSTATE_UINT32_ARRAY(dma_addr, PCIBaseState, 2),
        VMSTATE_UINT32_ARRAY(dma_count, PCIBaseState, 2),
        VMSTATE_UINT8_ARRAY(dma_mode, PCIBaseState, 2),
        VMSTATE_UINT8_ARRAY(dma_mask, PCIBaseState, 2),
        VMSTATE_UINT32(irq_status, PCIBaseState),
        VMSTATE_UINT32(dmaa_port, PCIBaseState),
        VMSTATE_UINT32(dmac_port, PCIBaseState),
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