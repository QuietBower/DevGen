/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Filled with static definitions from Habanalabs driver.
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

#define TYPE_PCIBASE_DEVICE "habanalabs_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID  0x1da3
#define DEVICE_ID  0x0001
#define CLASS_ID   0x1200

/* BAR definitions from driver */
#define SRAM_CFG_BAR_ID       0
#define MSIX_BAR_ID           2
#define DDR_BAR_ID            4
#define MSIX_BAR_SIZE         0x4000ull

/* Placeholder BAR sizes: actual values should come from ASIC-specific header */
#define SRAM_CFG_BAR_SIZE     0x200000ull
#define DDR_BAR_SIZE          0x10000000ull

/* Register offsets placeholder: will be filled when ASIC-specific header is provided */
typedef enum {
    REG_DUMMY = 0x0000,  /* Placeholder register */
} PCIBaseReg;

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
    uint32_t intr_status; uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Placeholder register structure: will be expanded from ASIC-specific header */
    struct {
        uint32_t reg_dummy;
    } regs;

    /* DMA Context (deleted - no device-initiated DMA evident) */

    /* Operational status flags (deleted) */
    /* State used to handle reset sequences (deleted) */
    /* Power management state (D0-D3) (deleted) */
    /* Other additions (deleted) */
};

/* Other definitions (deleted) */

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* #IRQ_Update_Logic#: Placeholder - to be implemented when interrupt status/mask registers are known */
}

/* Device-initiated DMA logic based on driver access patterns (deleted) */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_DUMMY:
        val = s->regs.reg_dummy;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected read at 0x%"PRIx64" size %u\n",
                      __func__, addr, size);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_DUMMY:
        s->regs.reg_dummy = (uint32_t)val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected write at 0x%"PRIx64" size %u val 0x%"PRIx64"\n",
                      __func__, addr, size, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* #PIO_Read_Func# (deleted) */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* #PIO_Write_Func# (deleted) */
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

    /* #Reset_Func#: Placeholder - set power-on defaults when register layout is known */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Configuration: extracted from goya_pci_bars_map */
    {
        s->bar_info[SRAM_CFG_BAR_ID].index = SRAM_CFG_BAR_ID;
        s->bar_info[SRAM_CFG_BAR_ID].type = BAR_TYPE_MMIO;
        s->bar_info[SRAM_CFG_BAR_ID].size = SRAM_CFG_BAR_SIZE;
        s->bar_info[SRAM_CFG_BAR_ID].name = "SRAM_CFG";

        s->bar_info[MSIX_BAR_ID].index = MSIX_BAR_ID;
        s->bar_info[MSIX_BAR_ID].type = BAR_TYPE_MMIO; /* MSI-X tables are typically MMIO */
        s->bar_info[MSIX_BAR_ID].size = MSIX_BAR_SIZE;
        s->bar_info[MSIX_BAR_ID].name = "MSIX";

        s->bar_info[DDR_BAR_ID].index = DDR_BAR_ID;
        s->bar_info[DDR_BAR_ID].type = BAR_TYPE_RAM;
        s->bar_info[DDR_BAR_ID].size = DDR_BAR_SIZE;
        s->bar_info[DDR_BAR_ID].name = "DDR";

        s->num_bars = 3;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI-X capability: driver expects MSI-X based on MSIX_BAR definition */
    int msix_cap = pci_add_capability(pdev, PCI_CAP_ID_MSIX, 0, 12, errp);
    if (msix_cap < 0) {
        return;
    }
    /* Fix MSI-X table and PBA overlap by setting distinct offsets */
    if (msix_init(pdev, 1, &s->bar_regions[MSIX_BAR_ID], MSIX_BAR_ID, 0,
                  &s->bar_regions[MSIX_BAR_ID], MSIX_BAR_ID, 0x1000,
                  msix_cap, errp)) {
        return;
    }

    /* #DMA_Config_Real# (deleted) */
    /* #Timer_Config_Real# (deleted) */
    /* #Field_Init_Real# (placeholder: final state init) */
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

    /* #Uninit_Func# (placeholder: free buffers, stop timers) */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "habanalabs_pci",
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
