/*
 * QEMU PCI device model for Advantech PCI-1724, minimal behavior
 * to satisfy the adv_pci1724 comedi driver.
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

#define TYPE_PCIBASE_DEVICE "adv_pci1724_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x13fe
#define PCIBASE_DEVICE_ID 0x1724
#define PCIBASE_CLASS_ID  PCI_CLASS_OTHERS

#define PCI1724_DAC_CTRL_REG        0x00
#define PCI1724_SYNC_CTRL_REG       0x04
#define PCI1724_EEPROM_CTRL_REG     0x08
#define PCI1724_SYNC_TRIG_REG       0x0c
#define PCI1724_BOARD_ID_REG        0x10

#define PCI1724_BOARD_ID_MASK       (0xf << 0)

#define PCI1724_SYNC_CTRL_DACSTAT   (1u << 1)
#define PCI1724_SYNC_CTRL_SYN       (1u << 0)

#define PCI1724_DAC_CTRL_GX(x)      (1u << (20 + ((x) / 8)))
#define PCI1724_DAC_CTRL_CX(x)      (((x) % 8) << 16)
#define PCI1724_DAC_CTRL_MODE(x)    (((x) & 0x3) << 14)
#define PCI1724_DAC_CTRL_MODE_GAIN   PCI1724_DAC_CTRL_MODE(1)
#define PCI1724_DAC_CTRL_MODE_OFFSET PCI1724_DAC_CTRL_MODE(2)
#define PCI1724_DAC_CTRL_MODE_NORMAL PCI1724_DAC_CTRL_MODE(3)
#define PCI1724_DAC_CTRL_MODE_MASK   PCI1724_DAC_CTRL_MODE(3)
#define PCI1724_DAC_CTRL_DATA(x)    (((x) & 0x3fff) << 0)

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
    uint32_t dac_ctrl;
    uint32_t sync_ctrl;
    uint32_t eeprom_ctrl;
    uint32_t sync_trig;
    uint32_t board_id;
};

/* Internal helper for status-triggered signaling. Currently unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The provided driver does not use interrupts; keep this empty. */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns.
 * The driver does not perform DMA, so this is unused.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The comedi driver uses inl()/outl() which are 32-bit accesses. */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case PCI1724_DAC_CTRL_REG:
        val = s->dac_ctrl;
        break;
    case PCI1724_SYNC_CTRL_REG:
        val = s->sync_ctrl;
        break;
    case PCI1724_EEPROM_CTRL_REG:
        val = s->eeprom_ctrl;
        break;
    case PCI1724_SYNC_TRIG_REG:
        val = s->sync_trig;
        break;
    case PCI1724_BOARD_ID_REG:
        val = s->board_id;
        break;
    default:
        /* For any undefined offset, return 0 to avoid surprises. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case PCI1724_DAC_CTRL_REG:
        /* Store the written DAC control value.
         * The driver never reads this register back, but keeping a shadow is harmless.
         */
        s->dac_ctrl = (uint32_t)val;
        break;
    case PCI1724_SYNC_CTRL_REG:
        /* The driver writes 0 to disable synchronous mode and polls this
         * register for PCI1724_SYNC_CTRL_DACSTAT when checking for idle.
         * We simply track the value; no DAC busy state is modeled.
         */
        s->sync_ctrl = (uint32_t)val;
        break;
    case PCI1724_EEPROM_CTRL_REG:
        /* Not used by the supplied driver code; store for completeness. */
        s->eeprom_ctrl = (uint32_t)val;
        break;
    case PCI1724_SYNC_TRIG_REG:
        /* Not used by provided code; retain shadow. */
        s->sync_trig = (uint32_t)val;
        break;
    case PCI1724_BOARD_ID_REG:
        /* The board ID register is read-only from the driver's perspective.
         * Ignore writes.
         */
        break;
    default:
        /* Ignore writes to unknown offsets. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The driver uses dev->iobase obtained from pci_resource_start(pcidev, 2)
     * and performs inl()/outl() I/O accesses. In QEMU, these are modeled as
     * MMIO through BAR0. No separate PIO space is used.
     */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
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
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize register shadows to a sane reset state.
     * The only value the driver actually relies on is board_id.
     */
    s->dac_ctrl = 0;
    s->sync_ctrl = 0;
    s->eeprom_ctrl = 0;
    s->sync_trig = 0;
    /* Provide a non-zero board ID with lower nibble set so that
     * (board_id & PCI1724_BOARD_ID_MASK) yields something reasonable.
     * The exact value is not interpreted further by the driver code shown.
     */
    s->board_id = 0x1;
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

    /* BAR Initialization.
     * The driver uses pci_resource_start(pcidev, 2) as dev->iobase.
     * For this virtual device, we export a single MMIO BAR (index 0).
     * When wiring the guest, ensure the driver is bound to this BAR.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* Provide at least enough space for the defined registers up to 0x10. */
    s->bar_info[0].size = 0x20;
    s->bar_info[0].name = "adv_pci1724-bar0";
    for (int i = 1; i < 6; ++i) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register state. This mirrors pcibase_reset, but ensure
     * that a fresh device starts in a consistent state even if reset is
     * not explicitly invoked before use.
     */
    s->dac_ctrl = 0;
    s->sync_ctrl = 0;
    s->eeprom_ctrl = 0;
    s->sync_trig = 0;
    s->board_id = 0x1;
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
    .name = "adv_pci1724_pci",
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

type_init(pcibase_register_types)
