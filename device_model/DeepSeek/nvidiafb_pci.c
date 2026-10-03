/*
 * QEMU NVIDIA TNT2-like framebuffer device model
 * Based on Linux nvidiafb driver source.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "nvidiafb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* BAR types */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_RAM,
} BARType;

typedef struct BARInfo {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
} BARInfo;

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10DE
#define DEVICE_ID 0x0020   /* GeForce2 MX/TNT2 – matches NV_ARCH_04 */
#define CLASS_ID  0x0300

#define NV_MMIO_SIZE (16 * MiB)
#define NV_FB_SIZE   (64 * MiB)

/* Driver architecture constants */
#define NV_ARCH_04 0x04
#define NV_ARCH_10 0x10
#define NV_ARCH_20 0x20
#define NV_ARCH_30 0x30
#define NV_ARCH_40 0x40

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* No MSI/MSI-X fields; driver uses legacy interrupts only. */
    /* No DMA engine present in this framebuffer device. */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t reg_0;   /* NV_PMC_BOOT_0 at offset 0x0000 */
    /* Additional register shadows can be added when needed */

    /* VGA emulation for single-head CRT detection */
    uint8_t crtc_index;          /* Index register for CRTC (0x3D4) */
    uint8_t crtc_regs[256];      /* CRTC data registers (0x3D5) */

    int open_count;
    int lockup;
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* IRQ logic not required for probe; can be extended later. */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x0000:
        val = s->reg_0;
        break;

    /* PCIO region (VGA CRTC @ 0x00601000 + 0x3D4/0x3D5) */
    case 0x006013D4:
        /* CRTC index port: some cards allow read-back of index */
        val = s->crtc_index;
        break;
    case 0x006013D5:
        /* CRTC data port: return according to current index */
        val = (val & ~0xFF) | s->crtc_regs[s->crtc_index];
        break;

    /* PVIO region: Misc Output Register at 0x000C03CC */
    case 0x000C03CC:
        val = 0x60;  /* EOAS=0 (3B0), color mode, etc. */
        break;

    /* Other accessed areas: return 0 for safety */
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x0000:
        s->reg_0 = (uint32_t)val;
        break;

    /* PCIO region */
    case 0x006013D4:
        s->crtc_index = (uint8_t)val;
        break;
    case 0x006013D5:
        /* CRTC data write */
        s->crtc_regs[s->crtc_index] = (uint8_t)val;
        /* If CRTC index increments, handle it (most VGA cards do) */
        /* Not emulating that for now */
        break;

    /* PVIO region: Misc Output write (0x000C03C2) */
    case 0x000C03C2:
        /* Accept write, but no emulation needed */
        break;

    /* All other writes ignored */
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset register defaults */
    s->reg_0 = 0x00200000;   /* TNT2 PMC_BOOT_0 signature */
    s->open_count = 0;
    s->lockup = 0;

    /* VGA CRTC default state: no slaved display, standard CRT */
    s->crtc_index = 0;
    memset(s->crtc_regs, 0, sizeof(s->crtc_regs));
    /* Set CRTC index 0x28 = 0x00 (no DFP slaved) */
    /* Set index 0x33 = 0x01 (TV not detected) only needed if index 0x28 has bit7 set,
       but we keep 0x28 clear, so not necessary. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* No PCIe capability; driver does not require it. */
    /* No power management capability. */

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = NV_MMIO_SIZE, .name = "nvidiafb-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM,  .size = NV_FB_SIZE,   .name = "nvidiafb-ram" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pci_set_byte(pci_conf + 0x09, 0x03); /* base class display */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No MSI/X to uninit. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "nvidiafb_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(reg_0, PCIBaseState),
        VMSTATE_INT32(open_count, PCIBaseState),
        VMSTATE_INT32(lockup, PCIBaseState),
        VMSTATE_UINT8(crtc_index, PCIBaseState),
        VMSTATE_UINT8_ARRAY(crtc_regs, PCIBaseState, 256),
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
