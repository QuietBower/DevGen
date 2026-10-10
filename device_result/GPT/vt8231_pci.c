/*
 * QEMU PCI device model for VIA VT8231 hardware monitor (hwmon/vt8231.c)
 * Generated to satisfy Linux driver probing and basic register access.
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
/* Removed linux/pci_ids.h which is not available in QEMU build environment */

#define TYPE_PCIBASE_DEVICE "vt8231_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VT8231_EXTENT      0x80
#define VT8231_BASE_REG    0x70
#define VT8231_ENABLE_REG  0x74
#define VT8231_REG_FAN_MIN(nr) (0x3b + (nr))
#define VT8231_REG_FAN(nr)     (0x29 + (nr))
#define VT8231_REG_TEMP_LOW01  0x49
#define VT8231_REG_TEMP_LOW25  0x4d
#define VT8231_REG_CONFIG      0x40
#define VT8231_REG_ALARM1      0x41
#define VT8231_REG_ALARM2      0x42
#define VT8231_REG_FANDIV      0x47
#define VT8231_REG_UCH_CONFIG  0x4a
#define VT8231_REG_TEMP1_CONFIG 0x4b
#define VT8231_REG_TEMP2_CONFIG 0x4c

/* Local copies of IDs normally provided by linux/pci_ids.h */
#define VT8231_VENDOR_ID   0x1106  /* PCI_VENDOR_ID_VIA */
#define VT8231_DEVICE_ID   0x8231  /* PCI_DEVICE_ID_VIA_8231_4 */
#define VT8231_CLASS_ID    0x0580  /* PCI_CLASS_MEMORY_OTHER */

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* We implement a flat 0x80-byte I/O space, as seen by the driver
     * via inb_p()/outb_p() on data->addr + reg.
     */
    uint8_t regs[VT8231_EXTENT];

    /* cached config words for BASE and ENABLE registers */
    uint16_t cfg_base;
    uint16_t cfg_enable;

    /* DMA Context */

};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* This device does not use MMIO; all accesses from the driver are I/O
     * port based. Return 0 for any unexpected MMIO access.
     */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO space is defined in the driver; ignore. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Emulate ISA-like 0x80-byte I/O region. Driver uses inb_p, so size
     * will be 1. For robustness, support 1-byte reads only; others return 0.
     */
    if (size != 1) {
        return 0xff;
    }

    if (addr < VT8231_EXTENT) {
        val = s->regs[addr];
    } else {
        val = 0xff;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Emulate outb_p() semantics (8-bit). Ignore wider writes. */
    if (size != 1) {
        return;
    }

    if (addr < VT8231_EXTENT) {
        s->regs[addr] = (uint8_t)val;
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
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize register defaults explicitly used by the driver. */
    for (i = 0; i < VT8231_EXTENT; ++i) {
        s->regs[i] = 0x00;
    }

    /* Alarms default to 0, fan dividers default to 0, volt/temp thresholds
     * default to 0, which the driver can interpret.
     */
    s->cfg_base = 0x0000;
    s->cfg_enable = 0x0000;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VT8231_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  VT8231_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, VT8231_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Implement an I/O BAR to represent the ISA-like VT8231_EXTENT region. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = VT8231_EXTENT;
    s->bar_info[0].name  = "vt8231-io";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize shadow config words for BASE and ENABLE. The Linux driver
     * may write these via pci_write_config_word(). We honor the BASE_REG
     * value only logically; we always map the BAR at the QEMU-assigned
     * I/O address. ENABLE_REG bit 0 is assumed set by default, matching
     * the "sensors enabled" state.
     */
    s->cfg_base = 0x0000;
    s->cfg_enable = 0x0001; /* sensors enabled */

    pci_set_word(pci_conf + VT8231_BASE_REG, s->cfg_base | 0x0001);
    pci_set_word(pci_conf + VT8231_ENABLE_REG, s->cfg_enable);
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "vt8231_pci",
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
