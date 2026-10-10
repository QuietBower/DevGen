/*
 * QEMU PCI device model for ipmi_si_pci
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
/* linux/pci_ids.h removed for QEMU build environment; vendor/device IDs are set below */

#define TYPE_PCIBASE_DEVICE "ipmi_si_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x103c
#define PCIBASE_DEVICE_ID 0x121a
#define PCIBASE_CLASS_ID  0x0c07

/* The ipmi_si_pci driver only uses BAR0 and treats it as either IO or MEM.
 * It does not access any device-specific registers directly here: all
 * register access is abstracted through ipmi_si_port_setup/ipmi_si_mem_setup
 * and the si_sm implementations. Therefore, this QEMU device only needs to
 * expose a BAR0 of a reasonable size and provide basic read/write storage.
 */

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

    /* Simple backing storage for BAR0 when used as MMIO or PIO */
    uint8_t bar0_storage[256];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /*
     * The ipmi_si_pci driver only passes the PCI IRQ number to the generic
     * SI layer (io.irq = pdev->irq) and sets io.irq_setup = ipmi_std_irq_setup
     * if non-zero, but this pci glue driver itself never programs any
     * interrupt-related device registers. The SI implementation (KCS/SMIC/BT)
     * may later request IRQs on that line, but we have no visibility here
     * into how interrupts are generated. Therefore, we leave this as a
     * no-op; if later logs show that an interrupt must be raised for probe
     * completion, this function can be extended in a repair phase.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The ipmi_si_pci driver does not directly perform any DMA setup or
     * descriptor programming on this PCI function; everything goes through
     * IO or MMIO accesses via si_sm. Therefore, there is no device-initiated
     * DMA behavior to emulate here.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The generic SI layer will perform byte/word/dword accesses through
     * readb/readw/readl (or ioread8/16/32). We simply return the contents
     * of our bar0_storage backing array for addresses within range.
     */

    if (addr >= sizeof(s->bar0_storage)) {
        return 0;
    }

    switch (size) {
    case 1:
        val = s->bar0_storage[addr];
        break;
    case 2:
        if (addr + 1 < sizeof(s->bar0_storage)) {
            val = s->bar0_storage[addr] |
                  ((uint16_t)s->bar0_storage[addr + 1] << 8);
        }
        break;
    case 4:
        if (addr + 3 < sizeof(s->bar0_storage)) {
            val = s->bar0_storage[addr] |
                  ((uint32_t)s->bar0_storage[addr + 1] << 8) |
                  ((uint32_t)s->bar0_storage[addr + 2] << 16) |
                  ((uint32_t)s->bar0_storage[addr + 3] << 24);
        }
        break;
    case 8:
        if (addr + 7 < sizeof(s->bar0_storage)) {
            val = (uint64_t)s->bar0_storage[addr] |
                  ((uint64_t)s->bar0_storage[addr + 1] << 8) |
                  ((uint64_t)s->bar0_storage[addr + 2] << 16) |
                  ((uint64_t)s->bar0_storage[addr + 3] << 24) |
                  ((uint64_t)s->bar0_storage[addr + 4] << 32) |
                  ((uint64_t)s->bar0_storage[addr + 5] << 40) |
                  ((uint64_t)s->bar0_storage[addr + 6] << 48) |
                  ((uint64_t)s->bar0_storage[addr + 7] << 56);
        }
        break;
    default:
        /* unsupported size: return 0 */
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->bar0_storage)) {
        return;
    }

    switch (size) {
    case 1:
        s->bar0_storage[addr] = (uint8_t)val;
        break;
    case 2:
        if (addr + 1 < sizeof(s->bar0_storage)) {
            s->bar0_storage[addr]     = (uint8_t)(val & 0xff);
            s->bar0_storage[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        }
        break;
    case 4:
        if (addr + 3 < sizeof(s->bar0_storage)) {
            s->bar0_storage[addr]     = (uint8_t)(val & 0xff);
            s->bar0_storage[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->bar0_storage[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->bar0_storage[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        }
        break;
    case 8:
        if (addr + 7 < sizeof(s->bar0_storage)) {
            s->bar0_storage[addr]     = (uint8_t)(val & 0xff);
            s->bar0_storage[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->bar0_storage[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->bar0_storage[addr + 3] = (uint8_t)((val >> 24) & 0xff);
            s->bar0_storage[addr + 4] = (uint8_t)((val >> 32) & 0xff);
            s->bar0_storage[addr + 5] = (uint8_t)((val >> 40) & 0xff);
            s->bar0_storage[addr + 6] = (uint8_t)((val >> 48) & 0xff);
            s->bar0_storage[addr + 7] = (uint8_t)((val >> 56) & 0xff);
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Same behavior as MMIO over the same backing array, but accessed via
     * IO port instructions in the guest.
     */

    if (addr >= sizeof(s->bar0_storage)) {
        return 0;
    }

    switch (size) {
    case 1:
        val = s->bar0_storage[addr];
        break;
    case 2:
        if (addr + 1 < sizeof(s->bar0_storage)) {
            val = s->bar0_storage[addr] |
                  ((uint16_t)s->bar0_storage[addr + 1] << 8);
        }
        break;
    case 4:
        if (addr + 3 < sizeof(s->bar0_storage)) {
            val = s->bar0_storage[addr] |
                  ((uint32_t)s->bar0_storage[addr + 1] << 8) |
                  ((uint32_t)s->bar0_storage[addr + 2] << 16) |
                  ((uint32_t)s->bar0_storage[addr + 3] << 24);
        }
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->bar0_storage)) {
        return;
    }

    switch (size) {
    case 1:
        s->bar0_storage[addr] = (uint8_t)val;
        break;
    case 2:
        if (addr + 1 < sizeof(s->bar0_storage)) {
            s->bar0_storage[addr]     = (uint8_t)(val & 0xff);
            s->bar0_storage[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        }
        break;
    case 4:
        if (addr + 3 < sizeof(s->bar0_storage)) {
            s->bar0_storage[addr]     = (uint8_t)(val & 0xff);
            s->bar0_storage[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->bar0_storage[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->bar0_storage[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        }
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
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    /* Clear backing storage to a known state so that the SI detection logic
     * (which writes a bogus KCS command then reads status) does not see
     * stale data across resets.
     */
    for (i = 0; i < sizeof(s->bar0_storage); i++) {
        s->bar0_storage[i] = 0;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Simple interrupt line; the driver just uses pdev->irq if non-zero. */
    pci_conf[PCI_INTERRUPT_LINE] = 10; /* arbitrary non-zero IRQ */

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The pci glue driver always uses BAR 0. It checks
     *   pci_resource_flags(pdev, 0) & IORESOURCE_IO
     * to decide between IO and MEM address spaces. We choose to expose BAR0
     * as IO space, which matches the common KCS-on-PCI implementations, but
     * the driver can still work with MEM too. To support both paths in a
     * generic fashion, we define BAR0 as IO and provide equivalent MMIO
     * ops if the guest reconfigures it as memory.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = sizeof(s->bar0_storage);
    s->bar_info[0].name  = "ipmi-si-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X advertised; the glue driver doesn't require them. */
    s->has_msi = false;
    s->has_msix = false;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ipmi_si_pci",
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
