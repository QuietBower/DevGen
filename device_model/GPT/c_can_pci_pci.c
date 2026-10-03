/*
 * QEMU PCI device model for Linux c_can_pci driver (behavioral implementation)
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
/* Removed non-existent include hw/net/c_can_regs.h to fix compilation */

#define TYPE_PCIBASE_DEVICE "c_can_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID  0x104A
#define PCIBASE_DEVICE_ID  0xCC11
/* Use a generic communications controller class ID since CANBUS is not defined */
#define PCIBASE_CLASS_ID   PCI_CLASS_COMMUNICATION_OTHER

/*
 * We do not have the actual Bosch C_CAN register layout here; however,
 * the Linux driver accesses registers via an enum 'reg' and an array
 * 'priv->regs = reg_map_c_can/reg_map_d_can', using readw/writew/ioread32.
 *
 * For probe() and basic register access to succeed, we only need a
 * contiguous MMIO region that accepts 16/32-bit accesses. The driver
 * does not verify register reset values during probe, so we can keep
 * them at 0. We just implement a simple shadow register array and
 * return/set values from that array.
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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    void *regs_shadow;  /* base of shadow register storage */
};

/*
 * Internal helper for status-triggered signaling.
 * The c_can_pci driver enables MSI and uses a single interrupt.
 * We implement a simple level-triggered interrupt source that can be
 * extended later if more detailed IRQ behavior is required.
 */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The provided driver snippets so far do not expose ISR or specific
     * interrupt enable/clear registers. Therefore, keep IRQs inactive
     * until more core c_can code is available to model them precisely.
     */
    (void)pdev;
}

/* Simple shadow register access helpers */
#define PCIBASE_REG_SPACE_SIZE  0x1000

static inline uint16_t pcibase_reg_read16(PCIBaseState *s, hwaddr addr)
{
    if (!s->regs_shadow) {
        return 0;
    }
    if (addr + sizeof(uint16_t) > PCIBASE_REG_SPACE_SIZE) {
        return 0;
    }
    uint16_t *p = (uint16_t *)((uint8_t *)s->regs_shadow + addr);
    return le16_to_cpu(*p);
}

static inline void pcibase_reg_write16(PCIBaseState *s, hwaddr addr, uint16_t val)
{
    if (!s->regs_shadow) {
        return;
    }
    if (addr + sizeof(uint16_t) > PCIBASE_REG_SPACE_SIZE) {
        return;
    }
    uint16_t *p = (uint16_t *)((uint8_t *)s->regs_shadow + addr);
    *p = cpu_to_le16(val);
}

static inline uint32_t pcibase_reg_read32(PCIBaseState *s, hwaddr addr)
{
    if (!s->regs_shadow) {
        return 0;
    }
    if (addr + sizeof(uint32_t) > PCIBASE_REG_SPACE_SIZE) {
        return 0;
    }
    uint32_t *p = (uint32_t *)((uint8_t *)s->regs_shadow + addr);
    return le32_to_cpu(*p);
}

static inline void pcibase_reg_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (!s->regs_shadow) {
        return;
    }
    if (addr + sizeof(uint32_t) > PCIBASE_REG_SPACE_SIZE) {
        return;
    }
    uint32_t *p = (uint32_t *)((uint8_t *)s->regs_shadow + addr);
    *p = cpu_to_le32(val);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver only uses readw (16-bit) and ioread32 (32-bit). */
    switch (size) {
    case 2:
        val = pcibase_reg_read16(s, addr);
        break;
    case 4:
        val = pcibase_reg_read32(s, addr);
        break;
    default:
        /* Unsupported size for this device; return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The driver only uses writew (16-bit) and iowrite32 (32-bit). */
    switch (size) {
    case 2:
        pcibase_reg_write16(s, addr, (uint16_t)val);
        break;
    case 4:
        pcibase_reg_write32(s, addr, (uint32_t)val);
        break;
    default:
        /* Ignore unsupported access sizes */
        break;
    }

    /* Update interrupt status if required in future extensions */
    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No PIO accesses are used by the driver. Keep this as a stub
     * backed by the same shadow storage for safety.
     */
    switch (size) {
    case 1:
        if (s->regs_shadow && addr < PCIBASE_REG_SPACE_SIZE) {
            uint8_t *p = (uint8_t *)s->regs_shadow + addr;
            val = *p;
        }
        break;
    case 2:
        val = pcibase_reg_read16(s, addr);
        break;
    case 4:
        val = pcibase_reg_read32(s, addr);
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Not used by driver; mirror writes into shadow area anyway. */
    switch (size) {
    case 1:
        if (s->regs_shadow && addr < PCIBASE_REG_SPACE_SIZE) {
            uint8_t *p = (uint8_t *)s->regs_shadow + addr;
            *p = (uint8_t)val;
        }
        break;
    case 2:
        pcibase_reg_write16(s, addr, (uint16_t)val);
        break;
    case 4:
        pcibase_reg_write32(s, addr, (uint32_t)val);
        break;
    default:
        break;
    }

    pcibase_update_irq(s);
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

    /* Reset PCI core state */
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear register shadow space */
    if (s->regs_shadow) {
        memset(s->regs_shadow, 0, PCIBASE_REG_SPACE_SIZE);
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

    /* Allocate shadow register space */
    s->regs_shadow = g_malloc0(PCIBASE_REG_SPACE_SIZE);

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Enable MSI capability to match driver's pci_enable_msi() */
    if (!msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
    }

    /* Expose as PCIe endpoint as in structure phase */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* Use BAR0 as MMIO with conservative size; driver-specific bars:
     * STA2x11 uses bar = 0, PCH uses bar = 1. Only static structure here;
     * behavior will be refined later.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = PCIBASE_REG_SPACE_SIZE; /* generic MMIO window */
    s->bar_info[0].name  = "c_can_pci-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No DMA configuration in driver snippet; none here. */
    /* No timer configuration required for probe. */
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

    if (s->regs_shadow) {
        g_free(s->regs_shadow);
        s->regs_shadow = NULL;
    }

    /* No other dynamic allocations to free. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "c_can_pci_pci",
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
