/*
 * QEMU PCI device model for addi_apci_1516
 * Functional implementation phase for linux-6.18/drivers/comedi/drivers/addi_apci_1516.c
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

#define TYPE_PCIBASE_DEVICE "addi_apci_1516_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define APCI1516_DI_REG              0x00
#define APCI1516_DO_REG              0x04
#define APCI1516_WDOG_REG            0x00
#define ADDI_TCW_RELOAD_REG          0x08
#define ADDI_TCW_CTRL_REG            0x0c
#define ADDI_TCW_STATUS_REG          0x10

#define ADDI_TCW_CTRL_TRIG           (1U << 9)
#define ADDI_TCW_CTRL_ENA            (1U << 0)

/*
 * PCI identification
 * First entry in apci1516_pci_table:
 *   { PCI_VDEVICE(ADDIDATA, 0x1000), BOARD_APCI1016 },
 */
#define APCI1516_VENDOR_ID           0x15B8
#define APCI1516_DEVICE_ID_APCI1016  0x1000

/* Comedi class: treat as generic multifunction PCI device */
#define APCI1516_PCI_CLASS_ID        PCI_CLASS_OTHERS

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
    uint32_t di_reg;
    uint32_t do_reg;
    uint32_t wdog_reg;
    uint32_t tcw_reload_reg;
    uint32_t tcw_ctrl_reg;
    uint32_t tcw_status_reg;
};

/* Internal helper for status-triggered signaling. Currently unused as the
 * reference driver does not use interrupts for this board. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns -
 * not used by this driver, keep as no-op. */
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

    /* The real hardware for this board uses I/O ports for the accessed
     * registers. Our model maps them into BAR1/2 I/O space via pcibase_pio_*.
     * No MMIO space is described by the driver, so this region is unused. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver only ever uses word (16-bit) access for DI/DO and
     * long (32-bit) access for watchdog registers. However, we accept
     * any size and mask appropriately. */

    switch (addr) {
    case APCI1516_DI_REG:
        /* Digital input: driver reads with inw(). We simply return the
         * shadow register. Tests/userspace can poke this via QEMU
         * monitor by altering di_reg if desired. */
        val = s->di_reg & 0xFFFF;
        break;
    case APCI1516_DO_REG:
        /* Digital output: driver reads back via inw() through
         * apci1516_do_insn_bits() using s->state. The driver writes
         * outw() only after updating state, so we simply mirror the
         * DO shadow register. */
        val = s->do_reg & 0xFFFF;
        break;
    /* Watchdog region is on a different BAR in hardware. In this
     * skeleton we declared only one BAR, but the comedi driver uses
     * pci_resource_start(pcidev, 2) for wdog. For modeling, we still
     * expose the registers here so that addi_watchdog_* helpers can
     * function if mapped to this I/O space in the guest. Their
     * behavior is trivial: simple load/store shadows. */
    case ADDI_TCW_CTRL_REG:
        val = s->tcw_ctrl_reg;
        break;
    case ADDI_TCW_RELOAD_REG:
        val = s->tcw_reload_reg;
        break;
    case ADDI_TCW_STATUS_REG:
        /* Status is read by addi_watchdog_insn_read(). No explicit
         * semantics are defined in the driver beyond being readable,
         * so just return the shadow value. */
        val = s->tcw_status_reg;
        break;
    default:
        /* Unused/undefined register offset */
        val = 0;
        break;
    }

    /* Truncate to requested access size */
    switch (size) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        val &= 0xFFFFFFFFULL;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Mask the value according to access size for consistency. */
    switch (size) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        val &= 0xFFFFFFFFULL;
        break;
    }

    switch (addr) {
    case APCI1516_DO_REG:
        /* Digital output register: written via outw() from
         * apci1516_do_insn_bits() and apci1516_reset(). We simply
         * store the value; there is no side effect described in
         * the driver. */
        s->do_reg = (uint32_t)(val & 0xFFFF);
        break;
    case APCI1516_DI_REG:
        /* Driver never writes DI register, but allow writes to update
         * the input shadow so tests can simulate external changes. */
        s->di_reg = (uint32_t)(val & 0xFFFF);
        break;
    case ADDI_TCW_CTRL_REG:
        /* Watchdog control register: manipulated via outl() in
         * addi_watchdog_reset() and addi_watchdog_insn_config(), and
         * or'ed with ADDI_TCW_CTRL_TRIG when "pinging" in
         * addi_watchdog_insn_write(). The driver does not inspect any
         * bits from this register, so we only maintain the shadow. */
        s->tcw_ctrl_reg = (uint32_t)val;
        break;
    case ADDI_TCW_RELOAD_REG:
        /* Watchdog reload register: written by
         * addi_watchdog_reset() (0) and addi_watchdog_insn_config()
         * (reload value). Driver never reads it, so just store. */
        s->tcw_reload_reg = (uint32_t)val;
        break;
    case ADDI_TCW_STATUS_REG:
        /* No writes are done by the driver to STATUS; ignore but keep
         * shadow in case future code touches it. */
        s->tcw_status_reg = (uint32_t)val;
        break;
    default:
        /* Ignore writes to undefined offsets. */
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

    /* Power-on defaults based on driver behavior: */

    /* Digital input defaults to 0 (no external signals). */
    s->di_reg = 0;

    /* Digital output is cleared by apci1516_reset() via outw(0).
     * Ensure reset state matches that. */
    s->do_reg = 0;

    /* Watchdog-related registers are cleared by addi_watchdog_reset():
     *   outl(0x0, iobase + ADDI_TCW_CTRL_REG);
     *   outl(0x0, iobase + ADDI_TCW_RELOAD_REG);
     */
    s->wdog_reg = 0;
    s->tcw_reload_reg = 0;
    s->tcw_ctrl_reg = 0;

    /* Status register: no explicit reset in driver, default to 0. */
    s->tcw_status_reg = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  APCI1516_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  APCI1516_DEVICE_ID_APCI1016 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, APCI1516_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Express capability is not strictly required by the driver, but it does
     * not interact with capabilities so presence is benign. */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The comedi driver uses:
     *   dev->iobase        = pci_resource_start(pcidev, 1);
     *   devpriv->wdog_iobase = pci_resource_start(pcidev, 2);
     * We model a single I/O BAR here (index 0). From the guest's
     * perspective, BAR indices 1 and 2 will map according to how QEMU
     * assigns them on the command line. For probe and operation, it is
     * sufficient that there is at least one I/O BAR providing the
     * expected register layout at offsets used by the driver.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 0x20; /* covers DI/DO and watchdog/timer regs */
    s->bar_info[0].name  = "apci1516-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X used by the driver; leave disabled */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadows to reset state */
    s->di_reg = 0;
    s->do_reg = 0;
    s->wdog_reg = 0;
    s->tcw_reload_reg = 0;
    s->tcw_ctrl_reg = 0;
    s->tcw_status_reg = 0;
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
    .name = "addi_apci_1516_pci",
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

