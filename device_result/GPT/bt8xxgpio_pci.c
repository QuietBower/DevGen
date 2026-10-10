/*
 * QEMU PCI device model for bt8xxgpio
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
/* Removed: #include "hw/gpio/gpio.h" (file not found, not used) */

#define TYPE_PCIBASE_DEVICE "bt8xxgpio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define BT8XXGPIO_NR_GPIOS 24
#define BT848_GPIO_OUT_EN  0x118
#define BT848_GPIO_DATA    0x200
#define BT848_GPIO_DMA_CTL 0x10C
#define BT848_INT_MASK     0x104
#define BT848_GPIO_REG_INP 0x11C
#define BT848_INT_STAT     0x100
#define PCI_DEVICE_ID_BT848 0x350
#define PCI_DEVICE_ID_BT878 0x36e
#define PCI_DEVICE_ID_BT879 0x36f
#define PCI_DEVICE_ID_BT849 0x351

/* Define vendor ID locally since PCI_VENDOR_ID_BROOKTREE is not available */
#define BT8XXGPIO_PCI_VENDOR_ID 0x109e
#define BT8XXGPIO_PCI_DEVICE_ID PCI_DEVICE_ID_BT848
#define BT8XXGPIO_PCI_CLASS_ID  PCI_CLASS_OTHERS

#define BT8XXGPIO_MMIO_BAR_INDEX 0
#define BT8XXGPIO_MMIO_BAR_SIZE  0x1000

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
    uint32_t gpio_out_en;
    uint32_t gpio_data;
    uint32_t gpio_dma_ctl;
    uint32_t int_mask;
    uint32_t gpio_reg_inp;
    uint32_t int_stat;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending;

    /* Interrupts are enabled when int_mask is non-zero and there is a pending
     * status bit in int_stat. The driver only ever writes zero to int_mask in
     * the provided code, but we still implement the generic logic.
     */
    pending = s->int_stat & s->int_mask;

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns
 *
 * The bt8xxgpio driver only writes zero to BT848_GPIO_DMA_CTL and does not
 * perform any DMA setup or transfers. Implement a no-op helper to keep the
 * model consistent without inventing behavior.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver uses bgread()/bgwrite() which are typically 32-bit accesses
     * (readl/writel). We provide 32-bit semantics on the known registers and
     * return zero for others.
     */

    switch (addr) {
    case BT848_GPIO_OUT_EN:
        val = s->gpio_out_en;
        break;
    case BT848_GPIO_DATA:
        val = s->gpio_data;
        break;
    case BT848_GPIO_DMA_CTL:
        val = s->gpio_dma_ctl;
        break;
    case BT848_INT_MASK:
        val = s->int_mask;
        break;
    case BT848_GPIO_REG_INP:
        val = s->gpio_reg_inp;
        break;
    case BT848_INT_STAT:
        val = s->int_stat;
        break;
    default:
        /* Unimplemented / unused region returns 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Implement only the behavior visible in the bt8xxgpio driver. Other
     * registers are left as simple read/write shadows or ignored.
     */

    switch (addr) {
    case BT848_GPIO_OUT_EN:
        /* Direction control for GPIO pins. The driver uses bitwise OR/AND on
         * this register, so we simply store the written value.
         */
        s->gpio_out_en = (uint32_t)val;
        break;
    case BT848_GPIO_DATA:
        /* Data register for GPIO pins. Only bit operations based on nr are
         * done by the driver; we mirror the value.
         */
        s->gpio_data = (uint32_t)val;
        break;
    case BT848_GPIO_DMA_CTL:
        /* The driver writes 0 during init/resume. No DMA behavior required. */
        s->gpio_dma_ctl = (uint32_t)val;
        break;
    case BT848_INT_MASK:
        /* Interrupt enable/disable. The driver writes 0 in probe, remove,
         * suspend and resume paths to disable interrupts.
         */
        s->int_mask = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case BT848_GPIO_REG_INP:
        /* The driver writes 0 in probe/resume. No further semantics given. */
        s->gpio_reg_inp = (uint32_t)val;
        break;
    case BT848_INT_STAT:
        /* In remove() and suspend() the driver uses:
         *   bgwrite(~0x0, BT848_INT_STAT);
         * which indicates write-1-to-clear semantics (W1C) on all bits.
         * Implement W1C behavior here.
         */
        s->int_stat &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    default:
        /* Ignore writes to unknown offsets within BAR */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

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

    s->gpio_out_en = 0;
    s->gpio_data = 0;
    s->gpio_dma_ctl = 0;
    s->int_mask = 0;
    s->gpio_reg_inp = 0;
    s->int_stat = 0;

    /* Ensure interrupt line is deasserted on reset */
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  BT8XXGPIO_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  BT8XXGPIO_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, BT8XXGPIO_PCI_CLASS_ID );
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
    s->bar_info[0].index = BT8XXGPIO_MMIO_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BT8XXGPIO_MMIO_BAR_SIZE;
    s->bar_info[0].name = "bt8xxgpio-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->gpio_out_en = 0;
    s->gpio_data = 0;
    s->gpio_dma_ctl = 0;
    s->int_mask = 0;
    s->gpio_reg_inp = 0;
    s->int_stat = 0;
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
    .name = "bt8xxgpio_pci",
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
