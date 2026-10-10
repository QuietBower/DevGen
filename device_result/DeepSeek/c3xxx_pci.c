/*
 * Stage 2 Functional Implementation for QAT C3xxx virtual device.
 * Based on driver source at /home/eely/linux-7.1/drivers/crypto/intel/qat/qat_c3xxx/adf_drv.c.
 * Updated with MSI-X support, LEGFUSE register, using driver-confirmed BAR ids.
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

/* Additional includes from driver context */

#define TYPE_PCIBASE_DEVICE "c3xxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ADF_C3XXX_SOFTSTRAP_CSR_OFFSET 0x2EC
#define ADF_C3XXX_MAX_ACCELENGINES 6
#define ADF_C3XXX_ETR_MAX_BANKS 16
#define ADF_C3XXX_MAX_ACCELERATORS 3
#define ADF_C3XXX_ACCELERATORS_MASK 0x7
#define ADF_C3XXX_ACCELERATORS_REG_OFFSET 16
#define ADF_C3XXX_ACCELENGINES_MASK 0x3F
#define ADF_C3XXX_SRAM_BAR 0
#define ADF_C3XXX_AE2FUNC_MAP_GRP_B_NUM_REGS 6
#define ADF_C3XXX_AE2FUNC_MAP_GRP_A_NUM_REGS 48
#define ADF_C3XXX_PMISC_BAR 0
#define ADF_C3XXX_MIN_AE_FREQ (533 * HZ_PER_MHZ)
#define ADF_C3XXX_MAX_AE_FREQ (685 * HZ_PER_MHZ)
#define ADF_C3XXX_ETR_BAR 1

/* Guessed BAR sizes for compilation; exact values still needed from headers */
#define ADF_C3XXX_PMISC_BAR_SIZE (64 * KiB)
#define ADF_C3XXX_ETR_BAR_SIZE   (1 * MiB)

/* Device-specific config registers */
#define ADF_DEVICE_FUSECTL_OFFSET 0x40
#define ADF_DEVICE_LEGFUSE_OFFSET 0x4C

/* Number of MSI-X vectors derived from adf_isr_alloc_msix_vectors_data logic:
 * msix_num_entries = 1 + (vf_info ? 0 : hw_data->num_banks)
 * For C3xxx, num_banks is assumed to be ADF_C3XXX_ETR_MAX_BANKS = 16.
 * Thus total = 1 + 16 = 17. (Request actual definition via needed_sources).
 */
#define ADF_C3XXX_NUM_BANKS 16
#define MSIX_VECTOR_COUNT (1 + ADF_C3XXX_NUM_BANKS)
#define MSIX_BAR_IDX 2

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
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
    uint32_t softstrap;       /* ADF_C3XXX_SOFTSTRAP_CSR */
    uint32_t fusectl0;        /* ADF_DEVICE_FUSECTL0 */
    uint32_t legfuse;         /* ADF_DEVICE_LEGFUSE */

    /* DMA Context */

    /* MSI-X table BAR */
    MemoryRegion msix_bar;
};

/* Custom config space read to handle vendor-specific extended registers */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;

    /* Intercept vendor-specific extended config space accesses */
    if (addr == ADF_DEVICE_FUSECTL_OFFSET && len == 4) {
        val = s->fusectl0;
        return val;
    }
    if (addr == ADF_DEVICE_LEGFUSE_OFFSET && len == 4) {
        val = s->legfuse;
        return val;
    }
    if (addr == ADF_C3XXX_SOFTSTRAP_CSR_OFFSET && len == 4) {
        val = s->softstrap;
        return val;
    }
    /* Fall through to default for all other accesses */
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    /* No specific write handlers needed yet */
    pci_default_write_config(pdev, addr, val, len);
}

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* #IRQ_Update_Logic#: Not implemented yet – driver ISR details unknown. */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* #DMA_Transfer_Logic#: Not implemented – driver DMA descriptor format unknown. */
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* #MMIO_Read_Func#: Minimal dummy read returns zero */
    qemu_log_mask(LOG_UNIMP, "c3xxx_pci: MMIO read at addr 0x%" HWADDR_PRIx " size %u\n", addr, size);
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* #MMIO_Write_Func#: Minimal dummy write ignores data */
    qemu_log_mask(LOG_UNIMP, "c3xxx_pci: MMIO write at addr 0x%" HWADDR_PRIx " size %u val 0x%" PRIx64 "\n", addr, size, val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* #PIO_Read_Func#: Port I/O not used by driver, dummy implementation */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* #PIO_Write_Func#: Port I/O not used by driver, dummy implementation */
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

    /* Reset shadow registers to default values: zero to enable all capabilities */
    s->softstrap = 0;
    s->fusectl0 = 0;
    s->legfuse = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x19e2 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0b40 ); /* Co-processor */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0);  /* MSI-X does not use INTx pin, but keep 0 */

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* PM Capability */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Override config read/write to intercept vendor-specific extended space */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* MSI-X Initialization: driver expects MSI-X, vector count derived from driver's allocation logic.
     * EXACT count will be refined when full hw_data is available.
     */
    /* Allocate a dedicated BAR for MSI-X table and PBA */
    memory_region_init_ram(&s->msix_bar, OBJECT(s), "msix-bar", 0x1000, errp);
    pci_register_bar(pdev, MSIX_BAR_IDX, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_bar);
    if (msix_init(pdev, MSIX_VECTOR_COUNT, &s->msix_bar, MSIX_BAR_IDX, 0,
                  &s->msix_bar, MSIX_BAR_IDX, 0x800, 0, errp) != 0) {
        /* If MSI-X init fails, probe will fail – that's the expected behavior for now */
    }

    /* BAR Initialization */
    /* #BAR_CONFIG_INIT# */
    s->num_bars = 2;  /* Only PMISC and ETR are application MMIO; MSI-X bar is internal */
    s->bar_info[0] = (BARInfo) { .index = ADF_C3XXX_PMISC_BAR, .type = BAR_TYPE_MMIO, .size = ADF_C3XXX_PMISC_BAR_SIZE, .name = "pmisc" };
    s->bar_info[1] = (BARInfo) { .index = ADF_C3XXX_ETR_BAR,   .type = BAR_TYPE_MMIO, .size = ADF_C3XXX_ETR_BAR_SIZE,   .name = "etr" };
    /* SRAM bar shares index 0 with PMISC, already handled. */

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->msix_bar, &s->msix_bar);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "c3xxx_pci",
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
