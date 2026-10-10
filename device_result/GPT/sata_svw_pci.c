/*
 * QEMU PCI device model for ServerWorks K2 SATA (sata_svw)
 * Behavioral emulation sufficient for Linux sata_svw driver probe/bind.
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

#define TYPE_PCIBASE_DEVICE "sata_svw_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x1166 /* PCI_VENDOR_ID_SERVERWORKS */
#define PCIBASE_DEVICE_ID 0x0240
#define PCIBASE_CLASS_ID  PCI_CLASS_STORAGE_SATA

/*
 * The sata_svw driver uses a single MMIO BAR (either BAR 3 or 5) and
 * lays out per-port and global registers using fixed offsets. We only
 * implement the registers that the driver actually touches.
 */

/* Port and register offsets (not given in driver; use minimal dummy values).
 * To avoid inventing undocumented layout, we only provide storage for the
 * regions actually accessed by the driver and ignore the exact numeric
 * offsets. The driver uses base + K2_SATA_PORT_OFFSET * i + various offsets.
 * Here we simply map the MMIO BAR as a flat region and treat accesses
 * anywhere within a generous size as generic registers.
 */

/* Simple BAR description */
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

    /* Simple MMIO backing storage: emulate a single MMIO BAR (index 5) */
    uint8_t *mmio_data;
    hwaddr mmio_size;

    /* PIO is not used by this driver, but keep structure symmetry */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The sata_svw driver uses ata_bmdma_interrupt(), which will read
     * BMDMA status and device status, then clear interrupt sources by
     * writing status. Since the driver code we have does not touch any
     * explicit interrupt status bits in MMIO, we keep this as a no-op.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The sata_svw driver programs the standard BMDMA engine via
     * ATA_DMA_TABLE_OFS, ATA_DMA_CMD etc. It does not parse any
     * device-specific descriptor format here, and the actual ATA data
     * DMA path goes through libata core using ide/ata helpers that
     * expect a real device. Emulating actual disk I/O is out of scope
     * for this minimal model, and no explicit pci_dma_* accesses are
     * driven by this driver code alone. So leave DMA as a no-op.
     */
    (void)s;
    (void)is_write;
}

/*
 * MMIO/PIO Handlers
 *
 * The driver uses readl/writel/readb/writeb/readw/writew on the BAR
 * mapped at bar_pos (3 or 5). It does not rely on specific values other
 * than that reads and writes are consistent. So we back the BAR with a
 * simple byte array and support 8/16/32-bit accesses.
 */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (!s->mmio_data || addr + size > s->mmio_size) {
        /* Out of range; return 0 */
        return 0;
    }

    switch (size) {
    case 1:
        val = s->mmio_data[addr];
        break;
    case 2:
        val = le16_to_cpu(*(uint16_t *)(s->mmio_data + addr));
        break;
    case 4:
        val = le32_to_cpu(*(uint32_t *)(s->mmio_data + addr));
        break;
    case 8:
        val = le64_to_cpu(*(uint64_t *)(s->mmio_data + addr));
        break;
    default:
        /* Unsupported size, return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (!s->mmio_data || addr + size > s->mmio_size) {
        return;
    }

    switch (size) {
    case 1:
        s->mmio_data[addr] = (uint8_t)val;
        break;
    case 2:
        *(uint16_t *)(s->mmio_data + addr) = cpu_to_le16((uint16_t)val);
        break;
    case 4:
        *(uint32_t *)(s->mmio_data + addr) = cpu_to_le32((uint32_t)val);
        break;
    case 8:
        *(uint64_t *)(s->mmio_data + addr) = cpu_to_le64((uint64_t)val);
        break;
    default:
        break;
    }

    /* Potentially update IRQ state if DMA command or status registers
     * are written, but since we have no specification and the driver
     * code provided never explicitly manipulates interrupt status in
     * MMIO, we leave this as a no-op.
     */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used by sata_svw; provide dummy implementation */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used by sata_svw; provide dummy implementation */
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

    /* Clear MMIO backing store on reset to a known state. */
    if (s->mmio_data && s->mmio_size) {
        memset(s->mmio_data, 0, s->mmio_size);
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

    /* Initialize BAR layout used by the sata_svw driver. It probes either
     * BAR 3 or BAR 5, but for simplicity we only model BAR 5 with a
     * reasonably large MMIO space. The driver checks pci_resource_len()
     * and then pcim_iomap_regions() with mask 1 << bar_pos, so having a
     * non-zero size BAR 5 is sufficient for it to succeed.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 5;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    /* Provide 64 KiB for MMIO; more than enough for the driver offsets */
    s->bar_info[0].size  = 64 * KiB;
    s->bar_info[0].name  = "k2-sata-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Track MMIO backing region size and allocate storage for it. */
    s->mmio_size = s->bar_info[0].size ? pow2ceil(s->bar_info[0].size) : 0;
    if (s->mmio_size) {
        s->mmio_data = g_malloc0(s->mmio_size);
    } else {
        s->mmio_data = NULL;
    }

    /* Enable bus mastering as the driver will call pci_set_master() */
    pdev->config[PCI_COMMAND] |= PCI_COMMAND_MASTER;
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

    if (s->mmio_data) {
        g_free(s->mmio_data);
        s->mmio_data = NULL;
        s->mmio_size = 0;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sata_svw_pci",
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
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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
