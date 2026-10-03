#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "snd_cs5530_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1078
#define DEVICE_ID 0x0103
#define CLASS_ID  0x0401

#define CS5530_BAR0_SIZE 0x100

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

    /* CS5530 MMIO register space */
    uint8_t mmio_regs[CS5530_BAR0_SIZE];

    /* SoundBlaster DSP emulation */
    uint8_t dsp_reset_state;
    uint8_t dsp_response[16];
    int dsp_response_len;
    int dsp_response_pos;
    bool dsp_data_ready;

    /* Mixer emulation (indices 0x00-0xFF) */
    uint8_t mixer_index;
    uint8_t mixer_regs[256];
    MemoryRegion mixer_io;

    uint16_t sb_base;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= CS5530_BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return ~0ULL;
    }

    /* Little-endian read from mmio_regs */
    for (unsigned i = 0; i < size; i++) {
        val |= (uint64_t)(s->mmio_regs[addr + i]) << (i * 8);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= CS5530_BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: 0x%" HWADDR_PRIx " size %u val 0x%" PRIx64 "\n",
                      __func__, addr, size, val);
        return;
    }

    /* Little-endian write to mmio_regs */
    for (unsigned i = 0; i < size; i++) {
        s->mmio_regs[addr + i] = (val >> (i * 8)) & 0xFF;
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
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t cs5530_mixer_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x4:
        /* Mixer address register, write-only, return 0 */
        break;
    case 0x5:
        val = s->mixer_regs[s->mixer_index];
        break;
    case 0x6:
        if (s->dsp_reset_state == 2) {
            val = 0xAA;
        } else if (s->dsp_reset_state == 1) {
            val = 0x01;      /* after write 1, reflect 0x01 */
        }
        break;
    case 0xA:
        if (s->dsp_data_ready && s->dsp_response_pos < s->dsp_response_len) {
            val = s->dsp_response[s->dsp_response_pos++];
            if (s->dsp_response_pos >= s->dsp_response_len) {
                s->dsp_data_ready = false;
            }
        }
        break;
    case 0xC:
        /* Write buffer status: ready for command */
        val = 0x7F;
        break;
    case 0xE:
        /* Read buffer status: bit7 indicates data ready */
        val = s->dsp_data_ready ? 0x80 : 0x00;
        break;
    default:
        break;
    }
    return val;
}

static void cs5530_mixer_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x4:
        s->mixer_index = val & 0xff;
        break;
    case 0x5:
        s->mixer_regs[s->mixer_index] = val & 0xff;
        break;
    case 0x6:
        if ((val & 0xff) == 1) {
            s->dsp_reset_state = 1;
        } else if ((val & 0xff) == 0 && s->dsp_reset_state == 1) {
            s->dsp_reset_state = 2;
            s->dsp_response[0] = 0xAA;
            s->dsp_response_len = 1;
            s->dsp_response_pos = 0;
            s->dsp_data_ready = true;
        }
        break;
    case 0xC:
        if ((val & 0xff) == 0xE1) {
            /* DSP Get version command: report SB16 version 4.13 */
            s->dsp_response[0] = 0x04;
            s->dsp_response[1] = 0x0D;
            s->dsp_response_len = 2;
            s->dsp_response_pos = 0;
            s->dsp_data_ready = true;
        }
        /* Other commands ignored */
        break;
    default:
        break;
    }
}

static const MemoryRegionOps cs5530_mixer_io_ops = {
    .read = cs5530_mixer_io_read,
    .write = cs5530_mixer_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* reset MMIO state to defaults */
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    /* Audio Map Base (0x00–0x03): set to 0 (unused) */
    /* Audio Control at 0x08: enable SB (bit0) */
    s->mmio_regs[0x08] = 0x01;
    /* Audio Status at 0x0C: IRQ 9, DMA8 0, DMA16 5 */
    s->mmio_regs[0x0C] = 0x09;  /* IRQ */
    s->mmio_regs[0x0D] = 0x00;  /* DMA8 */
    s->mmio_regs[0x0E] = 0x05;  /* DMA16 */
    /* Map register at 0x18: SB base select (0x0004 -> 0x220) */
    s->mmio_regs[0x18] = 0x04;
    s->mmio_regs[0x19] = 0x00;

    s->mixer_index = 0;
    memset(s->mixer_regs, 0, sizeof(s->mixer_regs));
    s->mixer_regs[0x80] = 0x01;   /* Enable SB */
    s->mixer_regs[0x81] = 0x21;   /* IRQ=9, DMA=0/5 */
    s->dsp_reset_state = 0;
    s->dsp_response_len = 0;
    s->dsp_response_pos = 0;
    s->dsp_data_ready = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Derive initial SB base from map register (default 0x0004 -> 0x220) */
    uint16_t map_val = s->mmio_regs[0x18] | (s->mmio_regs[0x19] << 8);
    s->sb_base = 0x220 + 0x20 * (map_val & 3);

    /* Register fixed I/O region for SoundBlaster (includes mixer and DSP) */
    memory_region_init_io(&s->mixer_io, OBJECT(s), &cs5530_mixer_io_ops, s,
                         "cs5530-sb", 0x10);
    memory_region_add_subregion(get_system_io(), s->sb_base, &s->mixer_io);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    memory_region_del_subregion(get_system_io(), &s->mixer_io);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static void pcibase_instance_init(Object *obj)
{
    PCIBaseState *s = PCIBASE_DEVICE(obj);
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = CS5530_BAR0_SIZE,
        .name = "cs5530-mmio",
    };

    /* Initialize MMIO defaults */
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    s->mmio_regs[0x08] = 0x01;       /* Audio Control: enable SB */
    s->mmio_regs[0x0C] = 0x09;       /* IRQ 9 */
    s->mmio_regs[0x0D] = 0x00;       /* DMA8 0 */
    s->mmio_regs[0x0E] = 0x05;       /* DMA16 5 */
    s->mmio_regs[0x18] = 0x04;       /* Map register LSB (base select) */
    s->mmio_regs[0x19] = 0x00;

    s->mixer_index = 0;
    memset(s->mixer_regs, 0, sizeof(s->mixer_regs));
    s->mixer_regs[0x80] = 0x01;
    s->mixer_regs[0x81] = 0x21;
    s->dsp_reset_state = 0;
    s->dsp_response_len = 0;
    s->dsp_response_pos = 0;
    s->dsp_data_ready = false;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_cs5530_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(mmio_regs, PCIBaseState, CS5530_BAR0_SIZE),
        VMSTATE_UINT8(mixer_index, PCIBaseState),
        VMSTATE_UINT8_ARRAY(mixer_regs, PCIBaseState, 256),
        VMSTATE_UINT8(dsp_reset_state, PCIBaseState),
        VMSTATE_UINT8_ARRAY(dsp_response, PCIBaseState, 16),
        VMSTATE_INT32(dsp_response_len, PCIBaseState),
        VMSTATE_INT32(dsp_response_pos, PCIBaseState),
        VMSTATE_BOOL(dsp_data_ready, PCIBaseState),
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
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
