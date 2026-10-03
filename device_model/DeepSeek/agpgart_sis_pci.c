/* QEMU 8.2.10 device model for SiS AGP bridge (driver: sis-agp.c) */

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

#define TYPE_PCIBASE_DEVICE "agpgart_sis_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_SI         0x1039
#define PCI_DEVICE_ID_SI_5591    0x5591
#define VENDOR_ID                PCI_VENDOR_ID_SI
#define DEVICE_ID                PCI_DEVICE_ID_SI_5591
#define CLASS_ID                 0x0600  /* PCI_CLASS_BRIDGE_HOST */

/* Register offsets (in PCI configuration space) */
#define AGPSTAT       0x04
#define AGPCMD        0x08
#define AGPNISTAT     0x0c
#define AGPCTRL       0x10
#define AGPNICMD      0x20
#define SIS_ATTBASE   0x90
#define SIS_APSIZE    0x94
#define SIS_TLBCNTRL  0x97
#define SIS_TLBFLUSH  0x98

/* Shadow registers for PCI config space accesses */
struct SiSAgpConfigRegs {
    uint32_t agpstat;    /* offset 0x04 */
    uint32_t agpcmd;     /* offset 0x08 */
    uint32_t agpnistat;  /* offset 0x0c */
    uint32_t agpctrl;    /* offset 0x10 */
    uint32_t agpncmd;    /* offset 0x20 */
    uint8_t attbase;     /* offset 0x90 */
    uint8_t apsize;      /* offset 0x94 */
    uint8_t tlbctrl;     /* offset 0x97 */
    uint8_t tlbflush;    /* offset 0x98 */
};

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    struct SiSAgpConfigRegs sis_regs;
    uint8_t agp_cap_offset;  /* offset of AGP capability in config space */
};

/* MMIO/PIO handlers not used (driver accesses config space directly) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr == SIS_ATTBASE && len == 4) {
        return s->sis_regs.attbase;
    }
    if (addr == SIS_APSIZE && len == 1) {
        return s->sis_regs.apsize;
    }
    if (addr == SIS_TLBCNTRL && len == 1) {
        return s->sis_regs.tlbctrl;
    }
    if (addr == SIS_TLBFLUSH && len == 1) {
        return s->sis_regs.tlbflush;
    }

    if (s->agp_cap_offset && addr >= s->agp_cap_offset &&
        addr < s->agp_cap_offset + PCI_AGP_SIZEOF) {
        uint32_t off = addr - s->agp_cap_offset;
        if (off == PCI_AGP_STATUS && len == 4) {
            return s->sis_regs.agpstat;
        }
        if (off == PCI_AGP_COMMAND && len == 4) {
            return s->sis_regs.agpcmd;
        }
    }

    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr == SIS_ATTBASE && len == 4) {
        s->sis_regs.attbase = val;
        return;
    }
    if (addr == SIS_APSIZE && len == 1) {
        s->sis_regs.apsize = val;
        return;
    }
    if (addr == SIS_TLBCNTRL && len == 1) {
        s->sis_regs.tlbctrl = val;
        return;
    }
    if (addr == SIS_TLBFLUSH && len == 1) {
        s->sis_regs.tlbflush = val;
        return;
    }

    if (s->agp_cap_offset && addr >= s->agp_cap_offset &&
        addr < s->agp_cap_offset + PCI_AGP_SIZEOF) {
        uint32_t off = addr - s->agp_cap_offset;
        if (off == PCI_AGP_COMMAND && len == 4) {
            s->sis_regs.agpcmd = val;
            return;
        }
        /* PCI_AGP_STATUS is read-only; ignore writes */
    }

    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(&s->sis_regs, 0, sizeof(s->sis_regs));
    /* Reinit AGP status to capabilities (same as realize) */
    if (s->agp_cap_offset) {
        uint8_t *pci_conf = PCI_DEVICE(s)->config;
        pci_set_long(pci_conf + s->agp_cap_offset + PCI_AGP_STATUS, 0x00020007);
        s->sis_regs.agpstat = 0x00020007;
    }
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Add AGP capability (required by driver probe) */
    s->agp_cap_offset = pci_add_capability(pdev, PCI_CAP_ID_AGP, 0, PCI_AGP_SIZEOF, errp);
    if (!s->agp_cap_offset) {
        error_setg(errp, "Failed to add AGP capability");
        return;
    }

    /* Initialize AGP status: major=2, minor=0, rates 1x|2x|4x */
    pci_set_long(pci_conf + s->agp_cap_offset + PCI_AGP_STATUS, 0x00020007);
    s->sis_regs.agpstat = 0x00020007;
    s->sis_regs.agpcmd = 0;

    /* BAR0: AGP aperture (256MB RAM region) */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_RAM,
        .size = 256 * 1024 * 1024,
        .name = "agp-aperture"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSI-X to clean */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "agpgart_sis_pci",
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
    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
