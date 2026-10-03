/*
 * QEMU PCI device model for mf624 (uio_mf624.c)
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

#define TYPE_PCIBASE_DEVICE "mf624_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define MF624_PCI_VENDOR_ID        0x186c
#define MF624_PCI_DEVICE_ID        0x0624
#define MF624_PCI_CLASS_ID         PCI_CLASS_OTHERS

#define MF624_INTCSR               0x4C
#define MF624_INTCSR_ADINT_ENABLE  (1 << 0)
#define MF624_INTCSR_CTR4INT_ENABLE (1 << 3)
#define MF624_INTCSR_PCIINT_ENABLE (1 << 6)
#define MF624_INTCSR_ADINT_STATUS  (1 << 2)
#define MF624_INTCSR_CTR4INT_STATUS (1 << 5)

#define MF624_INT_DISABLE          0x0
#define MF624_MAILBOX              0x0F
#define MF624_INT_ENABLE_ADDR      0xFC
#define MF624_INT_MASK_ADDR        0x2E

/* Mirror enum names used in the driver */
typedef enum mf624_interrupt_source {
    MF624_INT_SRC_ADC = 0,
    MF624_INT_SRC_CTR4,
    MF624_INT_SRC_ALL,
} mf624_interrupt_source_t;


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

    /* Hardware Register Shadows */
    /* BAR0 register space we actually model: */
    uint32_t intcsr_reg;      /* at offset MF624_INTCSR (0x4C) */
    uint8_t  int_mask_reg;    /* at MF624_INT_MASK_ADDR (0x2E) */
    uint32_t int_enable_reg;  /* at MF624_INT_ENABLE_ADDR (0xFC) */
    uint8_t  mailbox_reg;     /* at MF624_MAILBOX (0x0F) */

    /* Latched interrupt pending flag so that a status bit can be seen */
    bool adint_pending;
    bool ctr4int_pending;
};


/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The driver only checks the shared legacy INTx line; no MSI/MSI-X used. */

    /* We assert INTx when PCIINT_ENABLE is set and any enabled source is
     * pending (status bit set with enable bit set). The driver will then
     * read INTCSR and, if it sees both enable+status for a source, it calls
     * mf624_disable_interrupt(), which clears the enable bits and thus
     * makes us deassert.
     */
    bool pci_en = (s->intcsr_reg & MF624_INTCSR_PCIINT_ENABLE) != 0;
    bool ad_en  = (s->intcsr_reg & MF624_INTCSR_ADINT_ENABLE) != 0;
    bool ad_st  = (s->intcsr_reg & MF624_INTCSR_ADINT_STATUS) != 0;
    bool c4_en  = (s->intcsr_reg & MF624_INTCSR_CTR4INT_ENABLE) != 0;
    bool c4_st  = (s->intcsr_reg & MF624_INTCSR_CTR4INT_STATUS) != 0;

    bool any_irq = pci_en && ((ad_en && ad_st) || (c4_en && c4_st));

    pci_set_irq(pdev, any_irq ? 1 : 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The provided driver does not program any DMA engine, so nothing here. */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* We only emulate specific registers that the driver touches. */
    switch (addr) {
    case MF624_INTCSR:
        /* Return whole 32-bit INTCSR register. */
        val = s->intcsr_reg;
        break;
    case MF624_INT_MASK_ADDR:
        /* 8-bit interrupt mask register used in generic remove() path. */
        val = s->int_mask_reg;
        break;
    case MF624_INT_ENABLE_ADDR:
        /* 32-bit interrupt enable register used in generic remove() path. */
        val = s->int_enable_reg;
        break;
    case MF624_MAILBOX:
        /* MAILBOX read is used to "ensure board drops irq" in generic
         * remove(). Any value is fine; keep shadow.
         */
        val = s->mailbox_reg;
        break;
    default:
        /* Unused / unmapped area: leave as 0. */
        val = 0;
        break;
    }

    /* Respect access size: mask result accordingly. */
    switch (size) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
        val &= 0xFFFFFFFFU;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Normalize value to 32-bit for register fields. */
    uint32_t v32 = (uint32_t)val;
    uint8_t  v8  = (uint8_t)val;

    switch (addr) {
    case MF624_INTCSR:
        /* The driver always reads-modifies-writes INTCSR. We treat it as a
         * plain 32-bit read/write register containing enable and status bits.
         */
        s->intcsr_reg = v32;
        break;

    case MF624_INT_MASK_ADDR:
        /* 8-bit mask register, only used in generic remove() to disable. */
        s->int_mask_reg = v8;
        break;

    case MF624_INT_ENABLE_ADDR:
        /* 32-bit interrupt enable register, disabled in generic remove(). */
        s->int_enable_reg = v32;
        break;

    case MF624_MAILBOX:
        /* Write to mailbox; not used by driver other than read to flush. */
        s->mailbox_reg = v8;
        break;

    default:
        /* Writes to unmapped / unmodeled offsets are ignored. */
        break;
    }

    /* Any write that may change enable bits or status bits can affect IRQ. */
    pcibase_update_irq(s);
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

    /* Initialize shadow registers to a sane reset state: interrupts disabled
     * and no status bits asserted.
     */
    s->intcsr_reg     = 0;
    s->int_mask_reg   = MF624_INT_DISABLE;
    s->int_enable_reg = MF624_INT_DISABLE;
    s->mailbox_reg    = 0;
    s->adint_pending  = false;
    s->ctr4int_pending = false;

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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  MF624_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  MF624_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, MF624_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The driver uses BAR0, BAR2, and BAR4 with ioremap and UIO mappings.
     * Their exact sizes are not specified; we choose a small, non-zero size
     * that is still power-of-two after pow2ceil.
     */
    s->num_bars = 3;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    /* BAR0: chipset, interrupts, status */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* minimal page-aligned space */
    s->bar_info[0].name = "mf624-bar0";

    /* BAR2: ADC, DAC, DIO (not functionally modeled, but mappable) */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x1000;
    s->bar_info[2].name = "mf624-bar2";

    /* BAR4: Counter/timer chip (not functionally modeled) */
    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_MMIO;
    s->bar_info[4].size = 0x1000;
    s->bar_info[4].name = "mf624-bar4";

    /* Register active BARs */
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
    pcibase_register_bar(pdev, s, &s->bar_info[2], errp);
    pcibase_register_bar(pdev, s, &s->bar_info[4], errp);

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize internal state */
    s->intcsr_reg     = 0;
    s->int_mask_reg   = MF624_INT_DISABLE;
    s->int_enable_reg = MF624_INT_DISABLE;
    s->mailbox_reg    = 0;
    s->adint_pending  = false;
    s->ctr4int_pending = false;

    pcibase_update_irq(s);
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
    .name = "mf624_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intcsr_reg, PCIBaseState),
        VMSTATE_UINT8(int_mask_reg, PCIBaseState),
        VMSTATE_UINT32(int_enable_reg, PCIBaseState),
        VMSTATE_UINT8(mailbox_reg, PCIBaseState),
        VMSTATE_BOOL(adint_pending, PCIBaseState),
        VMSTATE_BOOL(ctr4int_pending, PCIBaseState),
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

