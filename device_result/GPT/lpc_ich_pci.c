/*
 * QEMU PCI device model for Intel LPC ICH (minimal emulation for lpc_ich driver)
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
#include "hw/sysbus.h"

#define TYPE_PCIBASE_DEVICE "lpc_ich_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define LPC_ICH_VENDOR_ID 0x8086
#define LPC_ICH_DEVICE_ID 0x0f1c
#define LPC_ICH_CLASS_ID  0x0601

#define ACPIBASE                0x40
#define ACPIBASE_GPE_OFF        0x28
#define ACPIBASE_GPE_END        0x2f
#define ACPIBASE_SMI_OFF        0x30
#define ACPIBASE_SMI_END        0x33
#define ACPIBASE_PMC_OFF        0x08
#define ACPIBASE_PMC_END        0x0c
#define ACPIBASE_TCO_OFF        0x60
#define ACPIBASE_TCO_END        0x7f
#define ACPICTRL_PMCBASE        0x44
#define ACPIBASE_GCS_OFF        0x3410
#define ACPIBASE_GCS_END        0x3414
#define SPIBASE_BYT             0x54
#define SPIBASE_BYT_SZ          512
#define SPIBASE_BYT_EN          (1U << 1)
#define BYT_BCR                 0xfc
#define BYT_BCR_WPD             (1U << 0)
#define SPIBASE_LPT             0x3800
#define SPIBASE_LPT_SZ          512
#define BCR                     0xdc
#define BCR_WPD                 (1U << 0)
#define GPIOBASE_ICH0           0x58
#define GPIOCTRL_ICH0           0x5C
#define GPIOBASE_ICH6           0x48
#define GPIOCTRL_ICH6           0x4C
#define RCBABASE                0xf0
#define INTEL_GPIO_RESOURCE_SIZE 0x1000
#define APL_GPIO_NORTH          0
#define APL_GPIO_NORTHWEST      1
#define APL_GPIO_WEST           2
#define APL_GPIO_SOUTHWEST      3
#define APL_GPIO_NR_DEVICES     4
#define APL_GPIO_NR_RESOURCES   4
#define APL_GPIO_IRQ            14
#define DNV_GPIO_NORTH          0
#define DNV_GPIO_SOUTH          1
#define DNV_GPIO_NR_DEVICES     1
#define DNV_GPIO_NR_RESOURCES   2
#define DNV_GPIO_IRQ            14

enum lpc_gpio_versions {
    ICH_I3100_GPIO,
    ICH_V5_GPIO,
    ICH_V6_GPIO,
    ICH_V7_GPIO,
    ICH_V9_GPIO,
    ICH_V10CORP_GPIO,
    ICH_V10CONS_GPIO,
    AVOTON_GPIO,
};

enum intel_spi_type {
    INTEL_SPI_BYT = 1,
    INTEL_SPI_LPT,
    INTEL_SPI_BXT,
    INTEL_SPI_CNL,
};

struct mfd_cell_acpi_match {
    const char *pnpid;
    const unsigned long long adr;
};

struct software_node {
    const char *name;
    const struct software_node *parent;
    const struct property_entry *properties;
};

/* Forward declaration to satisfy function pointer types */
struct platform_device;

struct mfd_cell {
    const char *name;
    int id;
    int level;

    int (*suspend)(struct platform_device *dev);
    int (*resume)(struct platform_device *dev);

    /* platform data passed to the sub devices drivers */
    const void *platform_data;
    size_t pdata_size;

    /* Matches ACPI */
    const struct mfd_cell_acpi_match *acpi_match;

    /* Software node for the device. */
    const struct software_node *swnode;

    /*
     * Device Tree compatible string
     * See: Documentation/devicetree/usage-model.rst Chapter 2.2 for details
     */
    const char *of_compatible;

    /*
     * Address as defined in Device Tree.  Used to complement 'of_compatible'
     * (above) when matching OF nodes with devices that have identical
     * compatible strings
     */
    uint64_t of_reg;

    /* Set to 'true' to use 'of_reg' (above) - allows for of_reg=0 */
    bool use_of_reg;

    /*
     * These resources can be specified relative to the parent device.
     * For accessing hardware you should use resources from the platform dev
     */
    int num_resources;
    const struct resource *resources;

    /* don't check for resource conflicts */
    bool ignore_resource_conflicts;

    /*
     * Disable runtime PM callbacks for this subdevice - see
     * pm_runtime_no_callbacks().
     */
    bool pm_runtime_no_callbacks;

    /* A list of regulator supplies that should be mapped to the MFD
     * device rather than the child device when requested
     */
    int num_parent_supplies;
    const char * const *parent_supplies;
};

typedef uint64_t resource_size_t;

struct resource {
    resource_size_t start;
    resource_size_t end;
    const char *name;
    unsigned long flags;
    unsigned long desc;
    struct resource *parent, *sibling, *child;
};

struct lpc_ich_gpio_info {
    const char *hid;
    const struct mfd_cell *devices;
    size_t nr_devices;
    struct resource **resources;
    size_t nr_resources;
    const resource_size_t *offsets;
};

/* Minimal definition to complete type; values not used in QEMU model */
enum lpc_chipsets {
    LPC_CHIPSET_DUMMY,
};

struct lpc_ich_info {
    char name[32];
    unsigned int iTCO_version;
    enum lpc_gpio_versions gpio_version;
    enum intel_spi_type spi_type;
    const struct lpc_ich_gpio_info *gpio_info;
    uint8_t use_gpio;
};

struct lpc_ich_priv {
    enum lpc_chipsets chipset;

    int abase;          /* ACPI base */
    int actrl_pbase;    /* ACPI control or PMC base */
    int gbase;          /* GPIO base */
    int gctrl;          /* GPIO control */

    int abase_save;         /* Cached ACPI base value */
    int actrl_pbase_save;   /* Cached ACPI control or PMC base value */
    int gctrl_save;         /* Cached GPIO control value */
};

struct intel_spi_boardinfo {
    enum intel_spi_type type;
    bool (*set_writeable)(void *base, void *data);
    void *data;
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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Simple config and MMIO shadow storage */
    uint8_t config_shadow[256];
    uint8_t *mmio_space; /* backing store for MMIO BAR0 if used */
    hwaddr mmio_size;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* lpc_ich.c does not use any device interrupts directly, so keep IRQ deasserted */
    pci_set_irq(pdev, 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The lpc_ich driver does not program any DMA engines in this PCI device.
     * No DMA emulation is required. */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (!s->mmio_space || addr + size > s->mmio_size) {
        return 0;
    }

    /* Provide a flat byte-addressable MMIO backing store. */
    switch (size) {
    case 1:
        val = s->mmio_space[addr];
        break;
    case 2:
        val = le16_to_cpu(*(uint16_t *)(s->mmio_space + addr));
        break;
    case 4:
        val = le32_to_cpu(*(uint32_t *)(s->mmio_space + addr));
        break;
    case 8:
        val = le64_to_cpu(*(uint64_t *)(s->mmio_space + addr));
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (!s->mmio_space || addr + size > s->mmio_size) {
        return;
    }

    switch (size) {
    case 1:
        s->mmio_space[addr] = (uint8_t)val;
        break;
    case 2:
        *(uint16_t *)(s->mmio_space + addr) = cpu_to_le16((uint16_t)val);
        break;
    case 4:
        *(uint32_t *)(s->mmio_space + addr) = cpu_to_le32((uint32_t)val);
        break;
    case 8:
        *(uint64_t *)(s->mmio_space + addr) = cpu_to_le64((uint64_t)val);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The lpc_ich driver does not perform port I/O via this PCI device. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The lpc_ich driver does not perform port I/O via this PCI device. */
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

    /* Mirror PCI config space into our shadow for easier access if needed. */
    memcpy(s->config_shadow, PCI_DEVICE(dev)->config, sizeof(s->config_shadow));
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
        s->mmio_size = aligned_size;
        if (!s->mmio_space) {
            s->mmio_space = g_malloc0(aligned_size);
        }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  LPC_ICH_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  LPC_ICH_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, LPC_ICH_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Mirror initial config into shadow */
    memset(s->config_shadow, 0, sizeof(s->config_shadow));
    memcpy(s->config_shadow, pci_conf, MIN(sizeof(s->config_shadow), (size_t)PCI_CONFIG_SPACE_SIZE));

    /* Enable PCIe capability and basic PM capability as done in template */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Define a single MMIO BAR large enough for potential subdevices.
     * The real hardware uses many legacy / fixed resources; the lpc_ich
     * driver only manipulates config-space base address registers and
     * passes derived port/memory ranges to child drivers. Emulate this
     * by exposing a dummy MMIO BAR 0, though the driver itself will not
     * access it directly with readl/writel. */
    s->num_bars = 0;
    s->mmio_space = NULL;
    s->mmio_size = 0;

    /* BAR0: 4 KiB MMIO */
    s->bar_info[s->num_bars].index = 0;
    s->bar_info[s->num_bars].type  = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size  = 0x1000;
    s->bar_info[s->num_bars].name  = "lpc_ich-mmio0";
    s->num_bars++;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X used by the lpc_ich driver itself. Leave disabled. */
    s->has_msi = false;
    s->has_msix = false;
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

    if (s->mmio_space) {
        g_free(s->mmio_space);
        s->mmio_space = NULL;
        s->mmio_size = 0;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "lpc_ich_pci",
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

