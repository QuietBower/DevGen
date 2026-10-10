/*
 * QEMU PCI device model for leds-ss4200 (Intel ICH7 LPC GPIO subset)
 *
 * Functional emulation limited strictly to what the driver uses:
 * - PCI config dwords PMBASE, GPIO_CTRL, GPIO_BASE
 * - An I/O space GPIO block accessed with inl()/outl()
 * - Simple shadow registers for GPIO_USE_SEL, GP_IO_SEL, GP_LVL,
 *   GPO_BLINK, GPI_INV and their *_2 variants.
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

#define TYPE_PCIBASE_DEVICE "leds_ss4200_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PMBASE              0x040
#define GPIO_BASE           0x048
#define GPIO_CTRL           0x04c
#define GPIO_EN             0x010
#define ICH7_GPIO_SIZE      64
#define GPIO_USE_SEL        0x000
#define GP_IO_SEL           0x004
#define GP_LVL              0x00c
#define GPO_BLINK           0x018
#define GPI_INV             0x030
#define GPIO_USE_SEL2       0x034
#define GP_IO_SEL2          0x038
#define GP_LVL2             0x03c
#define NAS_RECOVERY        0x00000400

/* Vendor/Device from first ich7_lpc_pci_id entry: PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_ICH7_0) */
#define SS4200_VENDOR_ID    0x8086
#define SS4200_DEVICE_ID    0x27b8
#define SS4200_CLASS_ID     0x0601

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
    /* PCI config shadow for PMBASE, GPIO_BASE, GPIO_CTRL */
    uint32_t pmbase_cfg;
    uint32_t gpio_base_cfg;
    uint32_t gpio_ctrl_cfg;

    /* GPIO I/O space shadow registers, 32-bit each */
    uint32_t gpio_use_sel;
    uint32_t gp_io_sel;
    uint32_t gp_lvl;
    uint32_t gpo_blink;
    uint32_t gpi_inv;
    uint32_t gpio_use_sel2;
    uint32_t gp_io_sel2;
    uint32_t gp_lvl2;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The leds-ss4200 driver does not use interrupts from this block. */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The driver does not use any DMA from this device. */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_gpio_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* The driver only issues 32-bit inl() reads. */
    if (size != 4) {
        return 0xffffffffULL;
    }

    switch (addr) {
    case GPIO_USE_SEL:
        val = s->gpio_use_sel;
        break;
    case GP_IO_SEL:
        val = s->gp_io_sel;
        break;
    case GP_LVL:
        val = s->gp_lvl;
        break;
    case GPO_BLINK:
        val = s->gpo_blink;
        break;
    case GPI_INV:
        val = s->gpi_inv;
        break;
    case GPIO_USE_SEL2:
        val = s->gpio_use_sel2;
        break;
    case GP_IO_SEL2:
        val = s->gp_io_sel2;
        break;
    case GP_LVL2:
        val = s->gp_lvl2;
        break;
    default:
        /* For addresses within the ICH7_GPIO_SIZE that the driver doesn't use,
         * just return 0.
         */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_gpio_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The driver only uses outl() (32-bit) writes. */
    if (size != 4) {
        return;
    }

    uint32_t v = (uint32_t)val;

    switch (addr) {
    case GPIO_USE_SEL:
        s->gpio_use_sel = v;
        break;
    case GP_IO_SEL:
        s->gp_io_sel = v;
        break;
    case GP_LVL:
        s->gp_lvl = v;
        break;
    case GPO_BLINK:
        s->gpo_blink = v;
        break;
    case GPI_INV:
        s->gpi_inv = v;
        break;
    case GPIO_USE_SEL2:
        s->gpio_use_sel2 = v;
        break;
    case GP_IO_SEL2:
        s->gp_io_sel2 = v;
        break;
    case GP_LVL2:
        s->gp_lvl2 = v;
        break;
    default:
        /* Silently ignore unknown offsets within the region. */
        break;
    }
}

static const MemoryRegionOps pcibase_gpio_io_ops = {
    .read = pcibase_gpio_io_read,
    .write = pcibase_gpio_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No memory-mapped registers are used by the driver; keep region empty. */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No MMIO usage in the driver. */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* This handler is not used for the GPIO block. All I/O emulation is in
     * pcibase_gpio_io_ops attached to the GPIO BAR.
     */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0xffffffffffffffffULL;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
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
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    /* Default PCI config values expected by the driver. These are reset
     * consistently so that pci_read_config_dword() returns deterministic
     * values:
     *  - PMBASE: ACPI PM base I/O address; driver masks bits with 0xff80.
     *  - GPIO_BASE: GPIO I/O base; driver masks with 0xffc0.
     *  - GPIO_CTRL: must have GPIO_EN bit set so probe() succeeds.
     */
    s->pmbase_cfg = 0x00001001U;      /* arbitrary aligned I/O base */
    s->gpio_base_cfg = 0x00002001U;   /* arbitrary aligned I/O base */
    s->gpio_ctrl_cfg = GPIO_EN;       /* only care that enable bit is set */

    pci_set_long(pdev->config + PMBASE, s->pmbase_cfg);
    pci_set_long(pdev->config + GPIO_BASE, s->gpio_base_cfg);
    pci_set_long(pdev->config + GPIO_CTRL, s->gpio_ctrl_cfg);

    /* Initialize GPIO I/O space registers to a benign state. The driver
     * reads them and then overwrites some fields.
     */
    s->gpio_use_sel = 0;
    s->gp_io_sel = 0;
    s->gp_lvl = 0;
    s->gpo_blink = 0;
    s->gpi_inv = 0;
    s->gpio_use_sel2 = 0;
    s->gp_io_sel2 = 0;
    s->gp_lvl2 = 0;
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
        /* For the GPIO BAR, we want to use the dedicated GPIO ops. */
        const MemoryRegionOps *ops = &pcibase_pio_ops;
        if (strcmp(bi->name, "gpio-io") == 0) {
            ops = &pcibase_gpio_io_ops;
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  SS4200_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SS4200_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SS4200_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 0;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    /* The GPIO block is accessed through I/O space with request_region(),
     * size ICH7_GPIO_SIZE. Model this as BAR 1 in I/O space.
     */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_NONE;
    s->bar_info[0].size = 0;
    s->bar_info[0].name = "unused-bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = ICH7_GPIO_SIZE;
    s->bar_info[1].name = "gpio-io";

    s->num_bars = 2;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize reset state, including PCI config dwords and GPIO defaults. */
    pcibase_reset(DEVICE(pdev));
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
    .name = "leds_ss4200_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(pmbase_cfg, PCIBaseState),
        VMSTATE_UINT32(gpio_base_cfg, PCIBaseState),
        VMSTATE_UINT32(gpio_ctrl_cfg, PCIBaseState),
        VMSTATE_UINT32(gpio_use_sel, PCIBaseState),
        VMSTATE_UINT32(gp_io_sel, PCIBaseState),
        VMSTATE_UINT32(gp_lvl, PCIBaseState),
        VMSTATE_UINT32(gpo_blink, PCIBaseState),
        VMSTATE_UINT32(gpi_inv, PCIBaseState),
        VMSTATE_UINT32(gpio_use_sel2, PCIBaseState),
        VMSTATE_UINT32(gp_io_sel2, PCIBaseState),
        VMSTATE_UINT32(gp_lvl2, PCIBaseState),
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
