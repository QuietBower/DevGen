/*
 * QEMU PCI device model for Alcor Micro PCI card reader (skeleton behavior).
 *
 * Generated to satisfy Linux driver probe in drivers/misc/cardreader/alcor_pci.c
 * using only behavior explicitly visible in the provided source.
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
/* #include "alcor.h" */

#define TYPE_PCIBASE_DEVICE "alcor_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ALCOR_VENDOR_ID   0x1AEA
#define ALCOR_DEVICE_ID   0x6601
#define ALCOR_CLASS_ID    PCI_CLASS_OTHERS

/*
 * The Linux driver uses regmap-style accessors alcor_write32(), alcor_read32(),
 * etc. to MMIO registers based on symbolic offsets such as AU6601_REG_INT_ENABLE
 * and AU6601_MS_INT_ENABLE which are defined in alcor.h. We do not know the
 * complete register map, but we must at least provide storage for any offsets
 * that might be touched. To keep mapping simple and deterministic, we back the
 * entire BAR0 with a flat byte array and perform width-aware accesses.
 */

#define PCIBASE_BAR0_SIZE   (64 * KiB)

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

    /* Simple generic MMIO backing storage for BAR0 */
    uint8_t *mmio0_buf;
    size_t mmio0_size;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The provided driver snippet only disables interrupts during probe and
     * never requests an IRQ or handles interrupts. Therefore, we do not
     * implement any interrupt signaling here.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The driver only calls dma_set_mask_and_coherent() and never programs
     * DMA descriptors or engine registers in the provided code. There is no
     * explicit DMA behavior to emulate, so this is intentionally empty.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (!s->mmio0_buf || addr >= s->mmio0_size) {
        return 0;
    }

    /* All registers are little-endian in the driver interface except for
     * explicit big-endian helpers alcor_read32be()/write32be(), which use
     * ioread32be()/iowrite32be(). Those helpers expect big-endian order in
     * memory, but since we have no semantic requirements from the snippet,
     * we simply serve the underlying bytes as stored.
     */

    switch (size) {
    case 1:
        val = s->mmio0_buf[addr];
        break;
    case 2:
        if (addr + 1 < s->mmio0_size) {
            val = s->mmio0_buf[addr] |
                  ((uint16_t)s->mmio0_buf[addr + 1] << 8);
        }
        break;
    case 4:
        if (addr + 3 < s->mmio0_size) {
            val = (uint32_t)s->mmio0_buf[addr] |
                  ((uint32_t)s->mmio0_buf[addr + 1] << 8) |
                  ((uint32_t)s->mmio0_buf[addr + 2] << 16) |
                  ((uint32_t)s->mmio0_buf[addr + 3] << 24);
        }
        break;
    case 8:
        if (addr + 7 < s->mmio0_size) {
            val = (uint64_t)s->mmio0_buf[addr] |
                  ((uint64_t)s->mmio0_buf[addr + 1] << 8) |
                  ((uint64_t)s->mmio0_buf[addr + 2] << 16) |
                  ((uint64_t)s->mmio0_buf[addr + 3] << 24) |
                  ((uint64_t)s->mmio0_buf[addr + 4] << 32) |
                  ((uint64_t)s->mmio0_buf[addr + 5] << 40) |
                  ((uint64_t)s->mmio0_buf[addr + 6] << 48) |
                  ((uint64_t)s->mmio0_buf[addr + 7] << 56);
        }
        break;
    default:
        /* Unsupported access size; return 0 */
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (!s->mmio0_buf || addr >= s->mmio0_size) {
        return;
    }

    switch (size) {
    case 1:
        s->mmio0_buf[addr] = (uint8_t)val;
        break;
    case 2:
        if (addr + 1 < s->mmio0_size) {
            s->mmio0_buf[addr]     = (uint8_t)(val & 0xff);
            s->mmio0_buf[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        }
        break;
    case 4:
        if (addr + 3 < s->mmio0_size) {
            s->mmio0_buf[addr]     = (uint8_t)(val & 0xff);
            s->mmio0_buf[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->mmio0_buf[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->mmio0_buf[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        }
        break;
    case 8:
        if (addr + 7 < s->mmio0_size) {
            s->mmio0_buf[addr]     = (uint8_t)(val & 0xff);
            s->mmio0_buf[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->mmio0_buf[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->mmio0_buf[addr + 3] = (uint8_t)((val >> 24) & 0xff);
            s->mmio0_buf[addr + 4] = (uint8_t)((val >> 32) & 0xff);
            s->mmio0_buf[addr + 5] = (uint8_t)((val >> 40) & 0xff);
            s->mmio0_buf[addr + 6] = (uint8_t)((val >> 48) & 0xff);
            s->mmio0_buf[addr + 7] = (uint8_t)((val >> 56) & 0xff);
        }
        break;
    default:
        /* Ignore unsupported access sizes */
        break;
    }

    /* The driver uses alcor_write32(priv, 0, AU6601_REG_INT_ENABLE) and
     * alcor_write32(priv, 0, AU6601_MS_INT_ENABLE) during probe to disable
     * interrupts. No further behavior is required for successful probe.
     */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The provided driver snippet never uses port I/O; all accesses go
     * through MMIO. To respect the "no unused placeholders" rule, PIO can
     * simply return 0 and keep minimal logic.
     */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Unused by the driver snippet. */
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

    /* Clear MMIO backing store on reset for determinism. */
    if (s->mmio0_buf && s->mmio0_size) {
        memset(s->mmio0_buf, 0, s->mmio0_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ALCOR_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ALCOR_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ALCOR_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Provide PCIe capability, as real hardware is PCIe-based. */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: the driver uses BAR 0 as MMIO. Size is not
     * specified in the snippet, so we choose a conventional 64 KiB region
     * to cover all relevant offsets without relying on undocumented layout.
     */
    s->num_bars = 1;

    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = PCIBASE_BAR0_SIZE;
    s->bar_info[0].name  = "alcor-bar0";

    /* Allocate generic MMIO backing memory for BAR0 */
    s->mmio0_size = PCIBASE_BAR0_SIZE;
    s->mmio0_buf = g_malloc0(s->mmio0_size);

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X usage is not evident from the provided snippet. Keep disabled. */
    s->has_msi = false;
    s->has_msix = false;
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

    if (s->mmio0_buf) {
        g_free(s->mmio0_buf);
        s->mmio0_buf = NULL;
        s->mmio0_size = 0;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "alcor_pci_pci",
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
