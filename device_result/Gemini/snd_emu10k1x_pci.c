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


#define TYPE_PCIBASE_DEVICE "snd_emu10k1x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PTR			0x00
#define IPR			0x08
#define MPU401_RESET		0xff
#define MPU401_ENTER_UART	0x3f
#define MPU401_ACK		0xfe
#define DATA			0x04
#define HCFG_AUDIOENABLE	0x00000001
#define HCFG_LOCKSOUNDCACHE	0x00000008
#define PLAYBACK_LIST_ADDR	0x00
#define PLAYBACK_LIST_SIZE	0x01
#define PLAYBACK_LIST_PTR	0x02
#define PLAYBACK_DMA_ADDR	0x04
#define PLAYBACK_PERIOD_SIZE	0x05
#define PLAYBACK_POINTER	0x06
#define PLAYBACK_UNKNOWN1       0x07
#define PLAYBACK_UNKNOWN2       0x08
#define CAPTURE_DMA_ADDR	0x10
#define CAPTURE_BUFFER_SIZE	0x11
#define CAPTURE_POINTER		0x12
#define CAPTURE_UNKNOWN         0x13
#define SPCS0			0x42
#define SPCS1			0x43
#define SPCS2			0x44
#define SPCS_CLKACCY_1000PPM	0x00000000
#define SPCS_SAMPLERATE_48	0x02000000
#define SPCS_CHANNELNUM_LEFT	0x00100000
#define SPCS_SOURCENUM_UNSPEC	0x00000000
#define SPCS_GENERATIONSTATUS	0x00008000
#define SPCS_EMPHASIS_NONE	0x00000000
#define SPCS_COPYRIGHT		0x00000004
#define INTE			0x0c
#define HCFG			0x14
#define MUDATA			0x47
#define MUCMD			0x48
#define MUSTAT		MUCMD
#define AC97DATA		0x1c
#define AC97ADDRESS		0x1e
#define TRIGGER_CHANNEL         0x40
#define ROUTING                 0x41
#define SPDIF_SELECT		0x45

#define INTE_CH_0_LOOP          0x00000800
#define INTE_CH_0_HALF_LOOP     0x00000100
#define TRIGGER_CHANNEL_0       0x00000001
#define TRIGGER_CAPTURE         0x00000100
#define INTE_CAP_0_LOOP         0x00080000
#define INTE_CAP_0_HALF_LOOP    0x00010000
#define IPR_CAP_0_LOOP          0x00080000
#define IPR_CAP_0_HALF_LOOP     0x00010000
#define IPR_CH_0_LOOP           0x00000800
#define IPR_CH_0_HALF_LOOP      0x00000100
#define IPR_MIDITRANSBUFEMPTY   0x00000001
#define IPR_MIDIRECVBUFEMPTY    0x00000002
#define INTE_MIDITXENABLE       0x00000001
#define INTE_MIDIRXENABLE       0x00000002

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
    uint32_t ipr;
    uint32_t inte; 

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t ptr;
    uint32_t data;
    uint32_t hcfg;
    uint32_t spcs0;
    uint32_t spcs1;
    uint32_t spcs2;
    uint32_t trigger_channel;
    uint32_t routing;
    uint32_t spdif_select;  
    uint32_t gpio;

    /* DMA Context */
    uint32_t playback_list_addr;
    uint32_t playback_list_size;
    uint32_t playback_list_ptr;
    uint32_t playback_dma_addr;
    uint32_t playback_period_size;
    uint32_t playback_pointer;
    uint32_t playback_unknown1;
    uint32_t playback_unknown2;
    uint32_t capture_dma_addr;
    uint32_t capture_buffer_size;
    uint32_t capture_pointer; 
    uint32_t capture_unknown;

    uint32_t status;      
    uint16_t ac97data;
    uint16_t ac97address;
    uint8_t mudata;
    uint8_t mucmd;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->ipr & s->inte) != 0;
    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PTR:
        val = s->ptr;
        break;
    case DATA: {
        uint32_t reg = s->ptr >> 16;
        switch (reg) {
        case PLAYBACK_LIST_ADDR: val = s->playback_list_addr; break;
        case PLAYBACK_LIST_SIZE: val = s->playback_list_size; break;
        case PLAYBACK_LIST_PTR: val = s->playback_list_ptr; break;
        case PLAYBACK_DMA_ADDR: val = s->playback_dma_addr; break;
        case PLAYBACK_PERIOD_SIZE: val = s->playback_period_size; break;
        case PLAYBACK_POINTER: val = s->playback_pointer; break;
        case PLAYBACK_UNKNOWN1: val = s->playback_unknown1; break;
        case PLAYBACK_UNKNOWN2: val = s->playback_unknown2; break;
        case CAPTURE_DMA_ADDR: val = s->capture_dma_addr; break;
        case CAPTURE_BUFFER_SIZE: val = s->capture_buffer_size; break;
        case CAPTURE_POINTER: val = s->capture_pointer; break;
        case CAPTURE_UNKNOWN: val = s->capture_unknown; break;
        case SPCS0: val = s->spcs0; break;
        case SPCS1: val = s->spcs1; break;
        case SPCS2: val = s->spcs2; break;
        case TRIGGER_CHANNEL: val = s->trigger_channel; break;
        case ROUTING: val = s->routing; break;
        case SPDIF_SELECT: val = s->spdif_select; break;
        default: val = 0; break;
        }
        break;
    }
    case IPR:
        val = s->ipr;
        break;
    case INTE:
        val = s->inte;
        break;
    case HCFG:
        val = s->hcfg;
        break;
    case AC97DATA:
        val = s->ac97data;
        break;
    case AC97ADDRESS:
        val = s->ac97address;
        break;
    case MUDATA:
        val = s->mudata;
        break;
    case MUCMD:
        val = s->mucmd;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PTR:
        s->ptr = val;
        break;
    case DATA: {
        uint32_t reg = s->ptr >> 16;
        switch (reg) {
        case PLAYBACK_LIST_ADDR: s->playback_list_addr = val; break;
        case PLAYBACK_LIST_SIZE: s->playback_list_size = val; break;
        case PLAYBACK_LIST_PTR: s->playback_list_ptr = val; break;
        case PLAYBACK_DMA_ADDR: s->playback_dma_addr = val; break;
        case PLAYBACK_PERIOD_SIZE: s->playback_period_size = val; break;
        case PLAYBACK_POINTER: s->playback_pointer = val; break;
        case PLAYBACK_UNKNOWN1: s->playback_unknown1 = val; break;
        case PLAYBACK_UNKNOWN2: s->playback_unknown2 = val; break;
        case CAPTURE_DMA_ADDR: s->capture_dma_addr = val; break;
        case CAPTURE_BUFFER_SIZE: s->capture_buffer_size = val; break;
        case CAPTURE_POINTER: s->capture_pointer = val; break;
        case CAPTURE_UNKNOWN: s->capture_unknown = val; break;
        case SPCS0: s->spcs0 = val; break;
        case SPCS1: s->spcs1 = val; break;
        case SPCS2: s->spcs2 = val; break;
        case TRIGGER_CHANNEL: s->trigger_channel = val; break;
        case ROUTING: s->routing = val; break;
        case SPDIF_SELECT: s->spdif_select = val; break;
        default: break;
        }
        break;
    }
    case IPR:
        s->ipr &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case INTE:
        s->inte = val;
        pcibase_update_irq(s);
        break;
    case HCFG:
        s->hcfg = val;
        break;
    case AC97DATA:
        s->ac97data = val;
        break;
    case AC97ADDRESS:
        s->ac97address = val;
        break;
    case MUDATA:
        s->mudata = val;
        break;
    case MUCMD:
        s->mucmd = val;
        break;
    default:
        break;
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
    
    s->ptr = 0;
    s->data = 0;
    s->ipr = 0;
    s->inte = 0;
    s->hcfg = 0;
    s->spcs0 = 0;
    s->spcs1 = 0;
    s->spcs2 = 0;
    s->trigger_channel = 0;
    s->routing = 0;
    s->spdif_select = 0;
    s->gpio = 0;
    s->playback_list_addr = 0;
    s->playback_list_size = 0;
    s->playback_list_ptr = 0;
    s->playback_dma_addr = 0;
    s->playback_period_size = 0;
    s->playback_pointer = 0;
    s->playback_unknown1 = 0;
    s->playback_unknown2 = 0;
    s->capture_dma_addr = 0;
    s->capture_buffer_size = 0;
    s->capture_pointer = 0;
    s->capture_unknown = 0;
    s->status = 0;
    s->ac97data = 0;
    s->ac97address = 0;
    s->mudata = 0;
    s->mucmd = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1102 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0006 );
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
    s->num_bars = 1;  
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x100, .name = "emu10k1x-pio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_emu10k1x_pci",
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
