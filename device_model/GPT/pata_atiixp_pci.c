/*
 * QEMU PCI device model for pata_atiixp
 * Phase 2: Minimal behavioral logic sufficient for libata/pata_atiixp
 * to probe and bind.
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

#define TYPE_PCIBASE_DEVICE "pata_atiixp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID   0x1002
#define PCIBASE_DEVICE_ID   0x4349
#define PCIBASE_CLASS_ID    0x0101

/*
 * The Linux pata_atiixp driver uses only PCI config space for this
 * controller and standard IDE legacy/BMDMA I/O BARs managed by libata
 * core helpers. No controller-specific MMIO register map is defined in
 * the driver body we were given.
 *
 * To allow generic IDE/libata probing, we expose the standard IDE BAR
 * layout:
 *  BAR0/1 : primary cmd/ctl
 *  BAR2/3 : secondary cmd/ctl
 *  BAR4   : BMDMA
 *  BAR5   : unused
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

    /* No controller-specific interrupt state required; libata uses
     * standard PCI INTx line and BMDMA status which we don't emulate
     * functionally here.
     */
};

/*
 * The provided driver never touches device-specific interrupt
 * registers; it only requests the shared PCI IRQ and relies on
 * the generic ata_sff/ata_bmdma interrupt handlers that read
 * standard status/altstatus from I/O space.  We don't emulate an
 * actual ATA device, so we don't assert interrupts.
 */

/*
 * The driver programs the BMDMA engine (BAR4) and expects the
 * real hardware to perform bus-master DMA via standard IDE PRD
 * tables. The format and behavior are not defined in the driver,
 * so we do not implement any DMA side effects.
 */

/* MMIO/PIO handlers: the driver does not access any controller-
 * specific MMIO region. All ATA register access is done via I/O BARs
 * that are mapped by libata using pcim_iomap_regions(), and real data
 * transfer goes to a real disk behind the controller in a physical
 * machine. Since we only need the controller to exist and have valid
 * resources, we provide dummy handlers that don't crash and always
 * return 0 / ignore writes.
 */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    /* Returning 0xff for byte reads would look more like a floating
     * legacy port, but the driver never directly pokes our emulated
     * I/O regions (it uses the host's pci_resource_start addresses),
     * so this value is irrelevant. Keep it simple.
     */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    PCIBaseState *s = opaque;
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
    (void)s;
    pci_device_reset(PCI_DEVICE(dev));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                 BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size ? bi->size : 1);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    switch (bi->type) {
    case BAR_TYPE_MMIO:
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
        break;
    case BAR_TYPE_PIO:
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
        break;
    case BAR_TYPE_RAM:
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
        break;
    default:
        break;
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);

    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8F);

    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1); /* INTA# */

    /* Expose as legacy IDE, not PCIe; pata_atiixp binds to a
     * conventional PCI device of class STORAGE_IDE.
     */
    pdev->cap_present &= ~QEMU_PCI_CAP_EXPRESS;

    /* Initialize BARs to look like a normal PCI IDE controller.
     * Sizes follow the standard IDE mapping used by libata's
     * ata_pci_sff_init_host() expectations: 8 bytes for cmd, 4 for ctl,
     * 16 bytes for BMDMA.
     */
    s->num_bars = 5;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 8;
    s->bar_info[0].name  = "pata-atiixp-prim-cmd";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_PIO;
    s->bar_info[1].size  = 4;
    s->bar_info[1].name  = "pata-atiixp-prim-ctl";

    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_PIO;
    s->bar_info[2].size  = 8;
    s->bar_info[2].name  = "pata-atiixp-sec-cmd";

    s->bar_info[3].index = 3;
    s->bar_info[3].type  = BAR_TYPE_PIO;
    s->bar_info[3].size  = 4;
    s->bar_info[3].name  = "pata-atiixp-sec-ctl";

    s->bar_info[4].index = 4;
    s->bar_info[4].type  = BAR_TYPE_PIO;
    s->bar_info[4].size  = 16;
    s->bar_info[4].name  = "pata-atiixp-bmdma";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "pata_atiixp_pci",
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

    /* Report as standard PCI IDE controller */
    k->vendor_id = PCIBASE_VENDOR_ID;
    k->device_id = PCIBASE_DEVICE_ID;
    k->class_id  = PCIBASE_CLASS_ID;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_class_init,
        .interfaces    = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)

