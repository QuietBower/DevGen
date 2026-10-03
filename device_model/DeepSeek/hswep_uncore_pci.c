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
/* No additional headers needed beyond standard QEMU PCI headers */

#define TYPE_PCIBASE_DEVICE "hswep_uncore_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_HWEP 0x8086
#define PCI_DEVICE_ID_HWEP_HA0 0x2f30
#define PCI_CLASS_HWEP_UNCORE 0x0880

/* Register offsets from driver (HA unit) */
#define SNBEP_PCI_PMON_BOX_CTL           0xf4
#define SNBEP_PCI_PMON_CTL0               0xd8
#define SNBEP_PCI_PMON_CTR0               0xa0
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0  0x40
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1  0x44
#define SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH 0x48

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
    /* No interrupt structure needed; driver does not use interrupts */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Placeholder for register storage; size depends on BAR0_SIZE */
    uint8_t regs[256]; /* Dummy size, will be adjusted when BAR0_SIZE known */

    /* DMA Context */
    /* No DMA used by driver */

    /* Operational status flags */
    /* No status flags used by driver */

    /* State used to handle reset sequences */
    /* No probe/reset state needed */

    /* Power management state (D0-D3) */
    /* No explicit power management state */

    /* No additional info structures */
};

/* No additional definitions */

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No IRQ logic implemented; driver does not use interrupts */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No DMA logic; driver does not use DMA */
}

/* Custom PCI config space read/write handlers */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = 0;

    /* For simplicity, we handle only dword accesses, as the driver uses pci_read_config_dword. */
    if (len != 4) {
        return pci_default_read_config(pdev, addr, len);
    }

    switch (addr) {
    case SNBEP_PCI_PMON_CTR0:
    case SNBEP_PCI_PMON_CTR0 + 4:
    case SNBEP_PCI_PMON_CTR0 + 8:
    case SNBEP_PCI_PMON_CTR0 + 12:
    case SNBEP_PCI_PMON_CTR0 + 16:
    case SNBEP_PCI_PMON_CTR0 + 20:
    case SNBEP_PCI_PMON_CTR0 + 24:
    case SNBEP_PCI_PMON_CTR0 + 28:
        /* Return counter value (64-bit counter, return appropriate dword).
           We just return 0 for simplicity. */
        val = 0;
        break;
    case SNBEP_PCI_PMON_CTL0:
    case SNBEP_PCI_PMON_CTL0 + 4:
    case SNBEP_PCI_PMON_CTL0 + 8:
    case SNBEP_PCI_PMON_CTL0 + 12:
        /* Return control register value */
        val = *(uint32_t *)(s->regs + addr);
        break;
    case SNBEP_PCI_PMON_BOX_CTL:
        val = *(uint32_t *)(s->regs + addr);
        break;
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0:
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1:
    case SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH:
        val = *(uint32_t *)(s->regs + addr);
        break;
    default:
        return pci_default_read_config(pdev, addr, len);
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (len != 4) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    switch (addr) {
    case SNBEP_PCI_PMON_CTL0:
    case SNBEP_PCI_PMON_CTL0 + 4:
    case SNBEP_PCI_PMON_CTL0 + 8:
    case SNBEP_PCI_PMON_CTL0 + 12:
        *(uint32_t *)(s->regs + addr) = val;
        break;
    case SNBEP_PCI_PMON_BOX_CTL:
        *(uint32_t *)(s->regs + addr) = val;
        break;
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0:
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1:
    case SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH:
        *(uint32_t *)(s->regs + addr) = val;
        break;
    default:
        pci_default_write_config(pdev, addr, val, len);
        break;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* MMIO unused; return 0 */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* MMIO unused; do nothing */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO unused; return 0 */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* PIO unused; do nothing */
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

    /* Reset custom register storage to zeros */
    memset(s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_HWEP);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_HWEP_HA0);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_HWEP_UNCORE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0); /* No interrupt pin */

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    /* Power management capability not explicitly used, but added for completeness */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* BAR0 is MMIO, size unknown; need HSWEP_UNCORE_BAR0_SIZE definition */
    s->num_bars = 0; /* Commented out until size known */
    /* Placeholder: add bar_info[0] = { .index = 0, .type = BAR_TYPE_MMIO, .size = HSWEP_UNCORE_BAR0_SIZE, .name = "bar0" }; */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X Initialization */
    /* No MSI/MSI-X used */
    /* DMA Config Realize */
    /* No DMA */
    /* Timer Config Realize */
    /* No timers */
    /* Field Init Realize */
    /* No additional initialization */
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

    /* No additional uninit needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "hswep_uncore_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
