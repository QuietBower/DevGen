/*
 * QEMU PCI device model for linux ata_generic PCI IDE controller
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "ata_generic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_PCTECH
#define PCI_VENDOR_ID_PCTECH 0x1042
#endif
#ifndef PCI_DEVICE_ID_PCTECH_SAMURAI_IDE
#define PCI_DEVICE_ID_PCTECH_SAMURAI_IDE 0x1002
#endif

#define PCIBASE_VENDOR_ID PCI_VENDOR_ID_PCTECH
#define PCIBASE_DEVICE_ID PCI_DEVICE_ID_PCTECH_SAMURAI_IDE
#define PCIBASE_CLASS_ID  PCI_CLASS_STORAGE_IDE

/* The Linux driver's pci_device_id table is not needed in QEMU model.
 * Remove incomplete struct reference that caused compile errors.
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
    
    /* For simplicity, emulate only legacy IDE BARs with fixed IO space.
     * We keep a tiny shadow area for possible future extensions, but
     * ata_generic/ata_pci_sff_init_host only depends on IO BAR presence
     * and does not access MMIO specific registers here.
     */
    uint8_t pio_space[0x40];

    /* DMA Context */
    

};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* ata_generic uses shared PCI IRQ only for legacy IDE interrupt
     * delivery from actual drive hardware. This virtual controller
     * does not implement a backing drive, and libata core will work
     * in polling mode for generic PCI IDE without any device
     * asserting interrupts. Thus we leave this as a no-op.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* generic PCI IDE (ata_generic) does not program any controller
     * specific DMA engine registers itself; instead it relies on the
     * standard Bus-Master IDE register block described in PCI IDE
     * spec, which is handled by ata_bmdma_* helpers in the kernel.
     * Because we do not emulate a backing disk or PRD processing,
     * leave this function empty. The BMDMA register block still
     * exists in IO space, but does not perform real DMA.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* ata_generic never ioremaps MMIO for this PCI ID; all accesses
     * go through IO BARs using inb/outb on cmd/ctl and BMDMA ports.
     * Keep MMIO region returning zero.
     */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO semantics required for ata_generic. Ignore writes. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Logic for Port I/O (Legacy support).
     *
     * ata_pci_sff_init_host() only checks that the IO regions are
     * present and then passes the addresses to libata, which will
     * talk to an attached drive. Since we do not emulate any drive
     * level registers here, return 0xff on 8-bit reads and 0xffff
     * on 16-bit reads. This is equivalent to an absent device and
     * is sufficient for the generic PCI IDE controller to probe
     * and create the host/ports.
     */

    (void)s;

    switch (size) {
    case 1:
        val = 0xff;
        break;
    case 2:
        val = 0xffff;
        break;
    case 4:
        val = 0xffffffffu;
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
    
    /* Logic for Port I/O (Legacy support)
     * The kernel driver will use outb/outw/outl to access standard
     * IDE taskfile registers and BMDMA registers. Since we do not
     * implement a backing disk or real DMA engine, safely ignore
     * all writes.
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

    /* Revert registers to power-on defaults.
     * Our minimal emulation does not keep per-register state,
     * so nothing beyond standard PCI reset is required.
     */
    memset(s->pio_space, 0, sizeof(s->pio_space));
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

    // 【新增这一行修复代码】 
    // 设置 Prog-If 寄存器。0x8F (二进制 10001111) 的含义：
    // Bit 7 (1): 支持 Bus Master (DMA)
    // Bit 3 & 2 (11): Secondary 通道支持 Native 模式
    // Bit 1 & 0 (11): Primary 通道支持 Native 模式
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8F);

    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);


    /* PCI IDE devices traditionally use legacy INTx, pin A */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Expose as conventional PCI (not real PCIe), but we keep the
     * existing pcie_endpoint_cap_init as per template; the kernel
     * driver only checks class code, not PCIe capabilities.
     */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     *
     * libata's ata_pci_sff_init_host() for PCI IDE controllers assumes
     * two IO port BAR pairs for primary and secondary channels and an
     * optional BMDMA BAR at index 4. In this generic model, we present
     * minimal IO BARs large enough for standard IDE register layout.
     *
     * BAR0: primary cmd (8 bytes)
     * BAR1: primary ctl (4 bytes)
     * BAR2: secondary cmd (8 bytes)
     * BAR3: secondary ctl (4 bytes)
     * BAR4: BMDMA (16 bytes)
     */

    s->num_bars = 5;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 8;
    s->bar_info[0].name  = "ata-generic-prim-cmd";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_PIO;
    s->bar_info[1].size  = 4;
    s->bar_info[1].name  = "ata-generic-prim-ctl";

    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_PIO;
    s->bar_info[2].size  = 8;
    s->bar_info[2].name  = "ata-generic-sec-cmd";

    s->bar_info[3].index = 3;
    s->bar_info[3].type  = BAR_TYPE_PIO;
    s->bar_info[3].size  = 4;
    s->bar_info[3].name  = "ata-generic-sec-ctl";

    s->bar_info[4].index = 4;
    s->bar_info[4].type  = BAR_TYPE_PIO;
    s->bar_info[4].size  = 16;
    s->bar_info[4].name  = "ata-generic-bmdma";

    /* BAR5 unused */
    s->bar_info[5].index = 5;
    s->bar_info[5].type  = BAR_TYPE_NONE;
    s->bar_info[5].size  = 0;
    s->bar_info[5].name  = "unused";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* We intentionally do not enable MSI/MSI-X since the legacy IDE
     * controller uses traditional INTx and the driver does not set up
     * MSI/MSI-X for this generic PCI ID.
     * Likewise, there is no need for DMA mask configuration or
     * internal timers here.
     */

    memset(s->pio_space, 0, sizeof(s->pio_space));
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
    .name = "ata_generic_pci",
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

