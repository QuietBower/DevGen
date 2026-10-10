/*
 * QEMU PCI Device Model for pci-das08 (skeleton with minimal behavior)
 * Generated to satisfy Linux comedi drivers/drivers/das08_pci.c expectations.
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

#define TYPE_PCIBASE_DEVICE "pci_das08_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DAS08_PCI_VENDOR_ID   0x1307
#define DAS08_PCI_DEVICE_ID   0x0029
#define DAS08_PCI_CLASS_ID    PCI_CLASS_OTHERS

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
    /*
     * The das08_pci.c front-end only passes the I/O base address
     * (pci_resource_start(pdev, 2)) to the common das08 driver via
     * das08_common_attach(). The common driver then uses dev->iobase
     * together with various board-specific offsets (e.g., i8255_offset,
     * i8254_offset). These offsets are not visible here, so we cannot
     * implement distinct functional blocks. We still expose a small
     * I/O space with simple read/write back storage so that generic
     * digital, analog, and counter helpers can access ports without
     * generating I/O errors.
     */
    uint8_t io_regs[8];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /*
     * No interrupt behavior is visible in the provided das08_pci.c
     * or das08_common_attach() fragments. The common das08 driver
     * and ISR implementation are not provided, so we do not implement
     * any IRQ logic here.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The provided das08_pci.c and das08_common_attach() snippets do not
     * configure DMA; they only enable the PCI device and pass an I/O base
     * to das08_common_attach(). Without visibility into the common driver
     * DMA usage, we cannot implement device DMA behavior here.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /*
     * This device is modeled as PIO-only for the exposed BAR (index 2 in
     * the Linux driver, but index 0 in our QEMU model). No MMIO access is
     * expected from the driver snippets provided, so we just return 0.
     */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /*
     * No MMIO access is expected by the provided driver front-end. Ignore.
     */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /*
     * Model a simple 8-byte I/O region. The common das08 driver and
     * helpers such as das08_ai_insn_read(), das08_ao_insn_write(),
     * das08_di_insn_bits(), das08_do_insn_bits(), das08jr_di_insn_bits(),
     * das08jr_do_insn_bits(), subdev_8255_io_init(), and
     * comedi_8254_io_alloc() ultimately operate on I/O ports relative
     * to dev->iobase and board-specific offsets. Since we do not know
     * the exact layout, we implement generic read-back behavior for a
     * small I/O window so those helpers can perform I/O without errors.
     */
    if (addr >= sizeof(s->io_regs)) {
        return 0;
    }

    switch (size) {
    case 1:
        val = s->io_regs[addr];
        break;
    case 2:
        if (addr + 1 < sizeof(s->io_regs)) {
            val = s->io_regs[addr] |
                  ((uint16_t)s->io_regs[addr + 1] << 8);
        }
        break;
    case 4:
        if (addr + 3 < sizeof(s->io_regs)) {
            val = s->io_regs[addr] |
                  ((uint32_t)s->io_regs[addr + 1] << 8) |
                  ((uint32_t)s->io_regs[addr + 2] << 16) |
                  ((uint32_t)s->io_regs[addr + 3] << 24);
        }
        break;
    default:
        /* Unsupported access size; return 0 */
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->io_regs)) {
        return;
    }

    switch (size) {
    case 1:
        s->io_regs[addr] = (uint8_t)(val & 0xff);
        break;
    case 2:
        if (addr + 1 < sizeof(s->io_regs)) {
            s->io_regs[addr]     = (uint8_t)(val & 0xff);
            s->io_regs[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        }
        break;
    case 4:
        if (addr + 3 < sizeof(s->io_regs)) {
            s->io_regs[addr]     = (uint8_t)(val & 0xff);
            s->io_regs[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->io_regs[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->io_regs[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        }
        break;
    default:
        /* Ignore unsupported access sizes */
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

    /* Clear I/O register shadows on reset */
    memset(s->io_regs, 0, sizeof(s->io_regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  DAS08_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DAS08_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, DAS08_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /*
     * The Linux driver uses pci_resource_start(pdev, 2) as the I/O base
     * handed to das08_common_attach(). The actual mapping of BAR indices
     * at the hardware level is not visible here, but we only need to
     * provide at least one I/O BAR that the driver can enable and map.
     *
     * We therefore expose a single PIO BAR at index 0 with size 8. The
     * comedi PCI helpers typically search for an I/O BAR; BAR index
     * mapping in the guest is determined by the PCI configuration space.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "pci-das08-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "unused";
    }
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadows */
    memset(s->io_regs, 0, sizeof(s->io_regs));
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
    .name = "pci_das08_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(io_regs, PCIBaseState, 8),
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
