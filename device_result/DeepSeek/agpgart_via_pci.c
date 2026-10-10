/* QEMU PCI device model for VIA AGP bridge based on via-agp.c driver.
 * Implements config space registers and aperture BAR for AGP.
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "agpgart_via_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets from driver */
#define VIA_GARTCTRL    0x80
#define VIA_APSIZE      0x84
#define VIA_ATTBASE     0x88
#define VIA_AGP3_GARTCTRL 0x90
#define VIA_AGP3_APSIZE  0x94
#define VIA_AGP3_ATTBASE 0x98
#define VIA_AGPSEL      0xfd
#define AGPCTRL         0x10
#define AGPSTAT         0x04
#define AGPCMD          0x08
#define AGPNISTAT       0x0c
#define AGPNICMD        0x20

/* Device ID for VIA VT82C597 (Apollo VP3) */
#define PCI_DEVICE_ID_VIA_82C597_0 0x0597

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

    /* Shadow registers for custom config space offsets */
    uint32_t gart_regs[8];  /* offsets 0x80-0x9F, dword granularity */
    uint8_t agpsel;         /* offset 0xFD */
};

/* Dummy MMIO/PIO handlers (not used, but required for potential registry) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "agpgart_via: unexpected MMIO read at 0x" HWADDR_FMT_plx " size %u\n", addr, size);
    return ~0ULL;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "agpgart_via: unexpected MMIO write at 0x" HWADDR_FMT_plx " size %u val 0x%" PRIx64 "\n", addr, size, val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "agpgart_via: unexpected PIO read at 0x" HWADDR_FMT_plx " size %u\n", addr, size);
    return ~0ULL;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "agpgart_via: unexpected PIO write at 0x" HWADDR_FMT_plx " size %u val 0x%" PRIx64 "\n", addr, size, val);
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

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);

    if (addr >= 0x80 && addr < 0xA0) {
        int index = (addr - 0x80) >> 2;
        uint32_t reg = s->gart_regs[index];
        int byte_offset = addr & 3;
        if (len == 1) {
            val = (reg >> (byte_offset * 8)) & 0xFF;
        } else if (len == 2) {
            val = (reg >> (byte_offset * 8)) & 0xFFFF;
        } else {
            val = reg;
        }
    } else if (addr == 0xFD) {
        if (len == 1) {
            val = s->agpsel;
        }
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    pci_default_write_config(pdev, addr, val, len);

    if (addr >= 0x80 && addr < 0xA0) {
        int index = (addr - 0x80) >> 2;
        int byte_offset = addr & 3;
        uint32_t mask;
        if (len == 1) {
            mask = 0xFF << (byte_offset * 8);
            s->gart_regs[index] = (s->gart_regs[index] & ~mask) | ((val & 0xFF) << (byte_offset * 8));
        } else if (len == 2) {
            mask = 0xFFFF << (byte_offset * 8);
            s->gart_regs[index] = (s->gart_regs[index] & ~mask) | ((val & 0xFFFF) << (byte_offset * 8));
        } else {
            s->gart_regs[index] = val;
        }
    } else if (addr == 0xFD) {
        if (len == 1) {
            s->agpsel = val & 0xFF;
        }
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->gart_regs, 0, sizeof(s->gart_regs));
    /* NOTE: VIA_APSIZE initial value must match kernel's aperture size table.
     * Setting to 0 will likely cause probe failure; valid value required.
     */
    s->agpsel = 0x02;  /* Bit 1 set => AGP 2.0 mode, prevents AGP3 selection */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_VIA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_VIA_82C597_0);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* AGP capability as required by driver */
    int agp_pos = pci_add_capability(pdev, PCI_CAP_ID_AGP, 0, 12, errp);
    if (agp_pos > 0) {
        /* Set initial AGP status: major=2, minor=0, rate 4x/2x/1x, FW, SBA, max requests 31, etc */
        pci_set_long(pci_conf + agp_pos + PCI_AGP_STATUS, 0x1f000217);
        /* AGP command initially zero */
        pci_set_long(pci_conf + agp_pos + PCI_AGP_COMMAND, 0x0);
    }

    /* Aperture BAR (index 0, RAM type) to satisfy pci_bus_address call */
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_RAM,
        .size = 256 * MiB,
        .name = "agp-aperture"
    };
    s->num_bars = 1;

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
    .name = "agpgart_via_pci",
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
    k->exit = pcibase_uninit;
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
    dc->reset = pcibase_reset;
    dc->vmsd = &vmstate_pcibase;
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

type_init(pcibase_register_types)
