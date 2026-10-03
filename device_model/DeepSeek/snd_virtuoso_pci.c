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

#define TYPE_PCIBASE_DEVICE "snd_virtuoso_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_CMEDIA 0x13F6
#define DEVICE_ID 0x8788
#define CLASS_ID 0x0401

#define OXYGEN_IO_SIZE 0x100
#define OXYGEN_GPIO_CONTROL 0xa8
#define OXYGEN_GPIO_DATA 0xa6
#define OXYGEN_FUNCTION_SPI 0x00
#define OXYGEN_FUNCTION_2WIRE 0x40
#define OXYGEN_FUNCTION_RESET_CODEC 0x02
#define OXYGEN_FUNCTION 0x50
#define OXYGEN_FUNCTION_ENABLE_SPI_4_5 0x80
#define OXYGEN_INTERRUPT_MASK 0x44
#define OXYGEN_DMA_STATUS 0x40
#define OXYGEN_GPI_DATA 0xa4
#define OXYGEN_GPI_INTERRUPT_MASK 0xa5
#define OXYGEN_GPIO_INTERRUPT_MASK 0xaa
#define OXYGEN_PLAY_ROUTING 0xc0
#define OXYGEN_PLAY_DAC0_SOURCE_SHIFT 8
#define OXYGEN_PLAY_DAC0_SOURCE_MASK 0x0300
#define OXYGEN_PLAY_DAC1_SOURCE_SHIFT 10
#define OXYGEN_PLAY_DAC1_SOURCE_MASK 0x0c00
#define OXYGEN_PLAY_DAC2_SOURCE_SHIFT 12
#define OXYGEN_PLAY_DAC2_SOURCE_MASK 0x3000
#define OXYGEN_PLAY_DAC3_SOURCE_SHIFT 14
#define OXYGEN_PLAY_DAC3_SOURCE_MASK 0xc000
#define OXYGEN_PLAY_CHANNELS 0x43
#define OXYGEN_PLAY_CHANNELS_2 0x00
#define OXYGEN_PLAY_CHANNELS_8 0x03
#define OXYGEN_PLAY_CHANNELS_MASK 0x03
#define OXYGEN_I2S_A_FORMAT 0x62
#define OXYGEN_I2S_FORMAT_I2S 0x0000
#define OXYGEN_I2S_FORMAT_LJUST 0x0008
#define OXYGEN_I2S_MASTER 0x0100
#define OXYGEN_I2S_BITS_16 0x0000
#define OXYGEN_I2S_BCLK_64 0x0000
#define OXYGEN_I2S_MCLK_SHIFT 4
#define OXYGEN_I2S_MCLK_MASK 0x0030
#define OXYGEN_I2S_MCLK(f) (((f) & 3) << OXYGEN_I2S_MCLK_SHIFT)
#define MCLK_256 1
#define MCLK_512 2
#define OXYGEN_RATE_32000 0x0000
#define OXYGEN_RATE_44100 0x0001
#define OXYGEN_RATE_48000 0x0002
#define OXYGEN_I2S_RATE_MASK 0x0007
#define OXYGEN_SPI_CONTROL 0x98
#define OXYGEN_SPI_DATA1 0x99
#define OXYGEN_SPI_DATA2 0x9a
#define OXYGEN_SPI_DATA3 0x9b
#define OXYGEN_SPI_TRIGGER 0x01
#define OXYGEN_SPI_CEN_LATCH_CLOCK_LO 0x00
#define OXYGEN_SPI_CEN_LATCH_CLOCK_HI 0x80
#define OXYGEN_SPI_DATA_LENGTH_2 0x00
#define OXYGEN_SPI_DATA_LENGTH_3 0x02
#define OXYGEN_SPI_CLOCK_160 0x00
#define OXYGEN_SPI_CODEC_SHIFT 4
#define OXYGEN_SPI_BUSY 0x01
#define OXYGEN_2WIRE_DATA 0x92
#define OXYGEN_2WIRE_CONTROL 0x90
#define OXYGEN_2WIRE_MAP 0x91
#define OXYGEN_2WIRE_DIR_WRITE 0x00
#define OXYGEN_2WIRE_LENGTH_8 0x0000
#define OXYGEN_2WIRE_SPEED_STANDARD 0x0000
#define OXYGEN_2WIRE_SPEED_FAST 0x0100
#define OXYGEN_2WIRE_BUS_STATUS 0x94
#define OXYGEN_2WIRE_INTERRUPT_MASK 0x0040
#define OXYGEN_AC97_REGS 0xdc
#define OXYGEN_AC97_INT_READ_DONE 0x01
#define OXYGEN_AC97_INT_WRITE_DONE 0x02
#define OXYGEN_AC97_REG_ADDR_SHIFT 16
#define OXYGEN_AC97_REG_CODEC_SHIFT 24
#define OXYGEN_AC97_REG_DIR_READ 0x00800000
#define OXYGEN_AC97_REG_DIR_WRITE 0x00000000
#define OXYGEN_AC97_INTERRUPT_STATUS 0xd3
#define OXYGEN_INT_GPIO 0x0800
#define OXYGEN_MISC_MIDI 0x40

#define PCM_COUNT 6
#define CONTROL_COUNT 0

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

    uint8_t saved_registers[OXYGEN_IO_SIZE];

    uint8_t pm_state;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < OXYGEN_IO_SIZE) {
        for (unsigned i = 0; i < size && (addr + i) < OXYGEN_IO_SIZE; i++) {
            val |= (uint64_t)(s->saved_registers[addr + i]) << (i * 8);
        }
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < OXYGEN_IO_SIZE) {
        for (unsigned i = 0; i < size && (addr + i) < OXYGEN_IO_SIZE; i++) {
            s->saved_registers[addr + i] = (val >> (i * 8)) & 0xff;
        }
    }
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
    memset(s->saved_registers, 0, OXYGEN_IO_SIZE);
    s->intr_status = 0;
    s->intr_mask = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x13F6);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x8788);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0401);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x13F6);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x8269);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = OXYGEN_IO_SIZE;
    s->bar_info[0].name = "oxygen-io";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    memset(s->saved_registers, 0, OXYGEN_IO_SIZE);
    s->intr_status = 0;
    s->intr_mask = 0;
    s->pm_state = 0;
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
    .name = "snd_virtuoso_pci",
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