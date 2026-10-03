/*
 * QEMU PCI device model for Chelsio csiostor driver (minimal probe support)
 *
 * This model only provides the minimal MMIO/BAR/PCI identity needed for
 * the Linux csiostor driver to successfully probe and bind. The actual
 * hardware logic is implemented elsewhere in the driver stack and is
 * not visible in the provided source, so we intentionally keep device
 * behavior minimal and side-effect free.
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
#include "hw/hw.h"

#define TYPE_PCIBASE_DEVICE "csiostor_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CSIO_VENDOR_ID 0x1425
#define CSIO_PCI_DEVICE_ID 0x5600
#define CSIO_PCI_CLASS_ID PCI_CLASS_STORAGE_SCSI

/*
 * The Linux driver uses a CH_PCI_DEVICE_ID_TABLE macro which expands
 * CSIO SCSI/FCoE related device IDs. Additional concrete device IDs
 * may exist but are not required here; we always use the first entry
 * from the pci_device_id table.
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

/*
 * We only know from csio_hw_alloc() that BAR 0 is used and mapped, but the
 * driver does not reference any specific MMIO register offsets in the
 * provided source. To allow ioremap()/access without faults, we simply
 * expose a reasonably sized MMIO BAR and back it with a zero-initialized
 * array of bytes.
 */

#define CSIO_MMIO_BAR_INDEX 0
#define CSIO_MMIO_BAR_SIZE  (64 * 1024)  /* 64 KiB generic register space */

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
    uint8_t mmio_regs[CSIO_MMIO_BAR_SIZE];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * No interrupt logic is used in the provided csio_init.c. All interrupt
     * management is encapsulated in helper functions (csio_intr_enable(),
     * csio_request_irqs(), etc.) whose MMIO behavior is not visible here.
     * Therefore, we intentionally provide no interrupt signaling.
     */

    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The provided csio_init.c only shows creation of DMA pools and generic
     * queue allocation, but no explicit pci_dma_read()/write() pattern or
     * descriptor ring MMIO programming, so we cannot model any concrete DMA
     * engine. This stub exists only to show where such logic would live.
     */

    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Bound check: silently return 0 for out-of-range access */
    if (addr >= CSIO_MMIO_BAR_SIZE || size == 0 || size > 8) {
        return 0;
    }

    /* Implement simple little-endian register array */
    switch (size) {
    case 1:
        val = s->mmio_regs[addr];
        break;
    case 2:
        val = le16_to_cpu(*(uint16_t *)&s->mmio_regs[addr]);
        break;
    case 4:
        val = le32_to_cpu(*(uint32_t *)&s->mmio_regs[addr]);
        break;
    case 8:
        val = le64_to_cpu(*(uint64_t *)&s->mmio_regs[addr]);
        break;
    default:
        /* Unsupported size; return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= CSIO_MMIO_BAR_SIZE || size == 0 || size > 8) {
        return;
    }

    /* Store into the shadow register array in little-endian order */
    switch (size) {
    case 1:
        s->mmio_regs[addr] = (uint8_t)val;
        break;
    case 2:
        *(uint16_t *)&s->mmio_regs[addr] = cpu_to_le16((uint16_t)val);
        break;
    case 4:
        *(uint32_t *)&s->mmio_regs[addr] = cpu_to_le32((uint32_t)val);
        break;
    case 8:
        *(uint64_t *)&s->mmio_regs[addr] = cpu_to_le64((uint64_t)val);
        break;
    default:
        break;
    }

    /*
     * We intentionally do not implement any side effects because the
     * provided source does not expose any specific register semantics.
     */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver never uses I/O port BARs in the provided source. */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The driver never uses I/O port BARs in the provided source. */
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

    /* Clear MMIO shadow registers on reset */
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CSIO_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CSIO_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CSIO_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Expose PCIe capability to satisfy generic PCIe helper usage. */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: only BAR 0 is used by the driver for MMIO */
    s->num_bars = 1;
    s->bar_info[0].index = CSIO_MMIO_BAR_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = CSIO_MMIO_BAR_SIZE;
    s->bar_info[0].name  = "csiostor-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X, DMA, or timer configuration is explicitly required. */
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

    /* No dynamic resources to free beyond generic BAR memory regions. */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "csiostor_pci",
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
