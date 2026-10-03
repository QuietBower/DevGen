/*
 * QEMU PCI device model for dwc3_pci (Phase 2: Functional behavior)
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

#define TYPE_PCIBASE_DEVICE "dwc3_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID_INTEL 0x8086
#define PCIBASE_DEVICE_ID_INTEL_CMLLP 0x02ee
#define PCIBASE_PCI_CLASS_SERIAL_USB 0x0c03

#define GP_RWBAR                        1
#define GP_RWREG1                       0xa0
#define GP_RWREG1_ULPI_REFCLK_DISABLE   (1 << 17)

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
    /* We only model what the driver touches: GP_RWBAR / GP_RWREG1 in BAR1 */
    uint32_t gp_rwreg1;

    /* DMA Context */

};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The dwc3_pci driver never programs or expects IRQs directly from this
     * PCI function, it simply forwards the legacy INTx/MSI line as a
     * resource to the child dwc3 platform device. Therefore we do not
     * implement any interrupt status or generation logic here.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The dwc3_pci glue driver does not program any DMA engine in this PCI
     * function. All DMA is handled by the dwc3 core once instantiated as a
     * platform device, accessing BAR0 directly. As such, there is no
     * device-initiated DMA behavior to model on this wrapper PCI device.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The only register the dwc3_pci glue driver reads is GP_RWREG1 in BAR1.
     * It is accessed with readl(), i.e. 32-bit little-endian.
     */
    if (addr == GP_RWREG1 && size == 4) {
        val = s->gp_rwreg1;
    } else {
        /* For all other addresses / sizes we simply return 0. The core dwc3
         * driver bound to BAR0 will have its own separate device model and is
         * outside the scope of this PCI wrapper emulation.
         */
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The glue driver writes GP_RWREG1 after masking out the
     * GP_RWREG1_ULPI_REFCLK_DISABLE bit. It uses writel(), i.e. 32-bit.
     */
    if (addr == GP_RWREG1 && size == 4) {
        s->gp_rwreg1 = (uint32_t)val;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    /* Logic for Port I/O (Legacy support)
     * The dwc3_pci driver never uses I/O port resources, so we return 0.
     */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* Logic for Port I/O (Legacy support)
     * Not used by dwc3_pci, so nothing to do.
     */
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

    /* Revert registers to power-on defaults.
     * The only modeled register is GP_RWREG1; the BYT quirk checks bit
     * GP_RWREG1_ULPI_REFCLK_DISABLE and clears it when set. To allow the
     * quirk to perform a write, we initialize this bit as set (disabled),
     * matching the scenario where firmware has not yet enabled the refclock.
     */
    s->gp_rwreg1 = GP_RWREG1_ULPI_REFCLK_DISABLE;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID_INTEL_CMLLP );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_PCI_CLASS_SERIAL_USB );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The dwc3_pci driver uses pci_resource_start(pci, 0) and
     * pcim_iomap(pci, GP_RWBAR, 0). We therefore provide a minimal layout:
     *   - BAR0: MMIO region acting as the parent for the dwc3 core resource.
     *           The actual dwc3 core implementation is outside this model,
     *           but the resource must exist.
     *   - BAR1: MMIO region containing GP_RWREG1 at offset 0xa0 used by
     *           dwc3_byt_enable_ulpi_refclock().
     */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    /* Provide a reasonably sized window; exact size is not dictated by the
     * glue driver, which only passes the resource through. 64 KiB is enough.
     */
    s->bar_info[0].size  = 64 * KiB;
    s->bar_info[0].name  = "dwc3-pci-bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    /* Need at least enough size to cover GP_RWREG1 offset (0xa0). 4 KiB is
     * sufficient and naturally aligned.
     */
    s->bar_info[1].size  = 4 * KiB;
    s->bar_info[1].name  = "dwc3-pci-bar1";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* The dwc3_pci glue does not explicitly enable MSI/MSI-X. We keep
     * legacy INTx behavior and do not call msi_init()/msix_init() here.
     * DMA masks or timers are likewise unused by this wrapper.
     */

    /* Initialize internal register state for power-on. */
    s->gp_rwreg1 = GP_RWREG1_ULPI_REFCLK_DISABLE;
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

    /* Free buffers, stop timers, etc. */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "dwc3_pci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(gp_rwreg1, PCIBaseState),
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
