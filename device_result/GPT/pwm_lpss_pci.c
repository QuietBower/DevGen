/*
 * QEMU PCI device model for pwm-lpss-pci skeleton
 * Generated based strictly on provided pwm-lpss-pci.c probe/remove logic.
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "pwm_lpss_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID  PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID  0x0ac8
#define PCIBASE_CLASS_ID   PCI_CLASS_OTHERS

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
     * The LPSS PWM core driver programs registers via pwm_lpss_read()/write(),
     * but their exact offsets and bit definitions are not provided in the
     * snippets. To obey the no‑invention rule, we only provide a flat register
     * window backing the BAR and keep all state in an opaque byte array.
     * This lets the core driver perform read/modify/write cycles without
     * us having to guess any semantics.
     */
    uint8_t regs[0x1000];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The provided pwm-lpss-pci and pwm-lpss core snippets do not request
     * or use an IRQ, so there is no observable interrupt behavior to model.
     */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The visible driver code does not set up DMA or touch any
     * bus-mastering control. Leave this empty per the
     * no‑undocumented‑behavior rule.
     */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Bound accesses to the implemented register window. */
    if (addr >= sizeof(s->regs)) {
        return 0;
    }

    /*
     * The pwm-lpss core uses pwm_lpss_read(), which is a simple read of a
     * 32-bit register. We still allow generic sizes but do not infer
     * semantics; we just return the stored bytes.
     */
    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        if (addr + 1 < sizeof(s->regs)) {
            val = s->regs[addr] |
                  ((uint16_t)s->regs[addr + 1] << 8);
        }
        break;
    case 4:
        if (addr + 3 < sizeof(s->regs)) {
            val = s->regs[addr] |
                  ((uint32_t)s->regs[addr + 1] << 8) |
                  ((uint32_t)s->regs[addr + 2] << 16) |
                  ((uint32_t)s->regs[addr + 3] << 24);
        }
        break;
    case 8:
        if (addr + 7 < sizeof(s->regs)) {
            val = (uint64_t)s->regs[addr] |
                  ((uint64_t)s->regs[addr + 1] << 8) |
                  ((uint64_t)s->regs[addr + 2] << 16) |
                  ((uint64_t)s->regs[addr + 3] << 24) |
                  ((uint64_t)s->regs[addr + 4] << 32) |
                  ((uint64_t)s->regs[addr + 5] << 40) |
                  ((uint64_t)s->regs[addr + 6] << 48) |
                  ((uint64_t)s->regs[addr + 7] << 56);
        }
        break;
    default:
        /* Unsupported access size, return 0 without side effects. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Bound accesses to the implemented register window. */
    if (addr >= sizeof(s->regs)) {
        return;
    }

    /*
     * The LPSS PWM core uses read-modify-write on registers via
     * pwm_lpss_read()/write() but no specific register layout is given.
     * We therefore simply store written bytes into an opaque array so that
     * subsequent reads return the last written values.
     */
    switch (size) {
    case 1:
        s->regs[addr] = (uint8_t)val;
        break;
    case 2:
        if (addr + 1 < sizeof(s->regs)) {
            s->regs[addr]     = (uint8_t)(val & 0xff);
            s->regs[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        }
        break;
    case 4:
        if (addr + 3 < sizeof(s->regs)) {
            s->regs[addr]     = (uint8_t)(val & 0xff);
            s->regs[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->regs[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->regs[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        }
        break;
    case 8:
        if (addr + 7 < sizeof(s->regs)) {
            s->regs[addr]     = (uint8_t)(val & 0xff);
            s->regs[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->regs[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->regs[addr + 3] = (uint8_t)((val >> 24) & 0xff);
            s->regs[addr + 4] = (uint8_t)((val >> 32) & 0xff);
            s->regs[addr + 5] = (uint8_t)((val >> 40) & 0xff);
            s->regs[addr + 6] = (uint8_t)((val >> 48) & 0xff);
            s->regs[addr + 7] = (uint8_t)((val >> 56) & 0xff);
        }
        break;
    default:
        /* Ignore unsupported access sizes. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No PIO is used by the provided driver snippet. */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Clear register storage so that each reset starts from a clean state. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = sizeof(s->regs);
    s->bar_info[0].name = "pwm-lpss-bar0";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* The driver uses pcim_enable_device() and pcim_iomap_region() only.
     * No MSI/MSI-X or legacy IRQ usage is visible in the provided code,
     * so we do not enable MSI/MSI-X here.
     */
    s->has_msi = false;
    s->has_msix = false;

    /* Ensure registers start from a known state. */
    memset(s->regs, 0, sizeof(s->regs));
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
    .name = "pwm_lpss_pci",
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
