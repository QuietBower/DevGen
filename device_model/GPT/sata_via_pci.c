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

/* Removed linux/pci_ids.h include since it is not available in QEMU build environment */

#define TYPE_PCIBASE_DEVICE "sata_via_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x1106
#define PCIBASE_DEVICE_ID 0x5337
#define PCIBASE_CLASS_ID  0x0106

#define PCIBASE_NUM_BARS 6

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
      

    /* DMA Context */
     

          
          
};

 

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The VIA SATA driver uses standard ATA PCI BMDMA interrupt handlers
     * (ata_bmdma_interrupt / ata_bmdma_irq_clear) and does not access any
     * device-specific interrupt status registers in MMIO/PIO space that are
     * visible in this file. The interrupt is generated purely based on the
     * PCI IRQ line and BMDMA engine, whose detailed behavior is not
     * specified in the provided driver snippet. To avoid inventing
     * undocumented hardware behavior, we leave this helper empty.
     *
     * Interrupt routing is still configured via pci_config_set_interrupt_pin
     * in pcibase_realize(), and QEMU will convey the line-based IRQ when
     * raised via pci_set_irq()/msi_notify()/msix_notify() if later needed.
     */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The driver relies on the generic libata BMDMA helpers
     * (ata_bmdma_start, ata_bmdma_interrupt, ata_bmdma_irq_clear, etc.)
     * and does not expose controller-specific DMA descriptor formats or
     * MMIO/PIO registers that control bus mastering beyond standard
     * ATA PCI BMDMA semantics. Those standard registers (command, status,
     * PRD table address) reside in PCI BARs but their exact layout and
     * access patterns are not detailed in the provided snippet.
     *
     * As we must not guess DMA formats or invisible register layouts, we
     * refrain from emulating active bus-master transfers here. The device
     * model will still provide I/O regions so that the driver can map them
     * successfully and complete probe(), but no real data movement is
     * performed.
     */
    (void)s;
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The sata_via driver uses ioread32/iowrite32 on SCR regions mapped
     * from BAR5 (and possibly other control regions). However, the driver
     * treats them as simple memory-mapped registers without inspecting
     * any specific reset values beyond generic SATA SCR semantics.
     *
     * Since the exact register map for BAR4/BAR5 is not specified in the
     * provided code and must not be invented, we simply return the current
     * (zero-initialized) shadow state. This is sufficient for the driver
     * paths that only validate the presence and size of the BARs and then
     * use SCRs mainly for link management and error checking. Hotplug/error
     * conditions are not triggered by this synthetic device unless further
     * information is supplied.
     */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* See comment in pcibase_mmio_read(). The driver performs iowrite32 to
     * SCR and control registers but does not read back device-specific
     * state beyond standard SCR fields. Absent a precise register
     * specification, we accept and ignore writes to avoid undefined
     * behavior.
     */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Logic for Port I/O (Legacy support)
     *
     * The driver uses the generic ATA SFF helpers to access legacy task
     * file registers via PIO (ata_sff_std_ports/ata_sff_tf_load/etc.), but
     * the exact mapping of ATA registers within BAR0-3 is standard for PCI
     * IDE/SATA controllers and not explicitly described in sata_via.c.
     * In line with the no-hallucination rule, we do not emulate task-file
     * protocol here and simply return zero.
     */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Logic for Port I/O (Legacy support)
     * See pcibase_pio_read() comment. We accept and ignore writes.
     */
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

    /* Revert registers to power-on defaults
     *
     * The provided driver code does not document controller-specific
     * reset values for MMIO/PIO registers beyond generic PCI config space,
     * which is reset via pci_device_reset(). Therefore we leave internal
     * shadow state at its zero-initialized defaults.
     */
    (void)s;
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

    /* BAR Initialization */
    s->num_bars = PCIBASE_NUM_BARS;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 8;
    s->bar_info[0].name  = "sata_via-bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_PIO;
    s->bar_info[1].size  = 4;
    s->bar_info[1].name  = "sata_via-bar1";

    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_PIO;
    s->bar_info[2].size  = 8;
    s->bar_info[2].name  = "sata_via-bar2";

    s->bar_info[3].index = 3;
    s->bar_info[3].type  = BAR_TYPE_PIO;
    s->bar_info[3].size  = 4;
    s->bar_info[3].name  = "sata_via-bar3";

    s->bar_info[4].index = 4;
    s->bar_info[4].type  = BAR_TYPE_MMIO;
    s->bar_info[4].size  = 16;
    s->bar_info[4].name  = "sata_via-bar4";

    s->bar_info[5].index = 5;
    s->bar_info[5].type  = BAR_TYPE_MMIO;
    s->bar_info[5].size  = 256;
    s->bar_info[5].name  = "sata_via-bar5";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* The driver may optionally enable MSI/MSI-X via PCI config space
     * or kernel infrastructure, but sata_via.c itself does not directly
     * manipulate MSI/MSI-X capabilities for these controllers. To avoid
     * creating unsupported capabilities, we leave MSI/MSI-X disabled
     * here. If future driver snippets show explicit use, they can be
     * wired in iteratively.
     */

    /* No explicit DMA mask handling here; sata_via.c uses
     * dma_set_mask_and_coherent() which operates on the Linux device
     * layer, not on QEMU's internal PCI DMA APIs. QEMU's PCI core will
     * accept 32-bit DMA by default.
     */
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

    /* Free buffers, stop timers, etc.
     *
     * No additional dynamically allocated resources or timers are created
     * in this model, so there is nothing further to tear down.
     */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sata_via_pci",
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
