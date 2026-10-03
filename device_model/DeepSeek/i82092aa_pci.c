/*
 * QEMU PCI device model for Intel i82092AA PCMCIA controller
 * Behavioral implementation for linux-6.18/drivers/pcmcia/i82092.c
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

#define TYPE_PCIBASE_DEVICE "i82092aa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

#define PCIBASE_VENDOR_ID   PCI_VENDOR_ID_INTEL
/* Local definition to satisfy compilation when PCI_DEVICE_ID_INTEL_82092AA_0 is missing */
#ifndef PCI_DEVICE_ID_INTEL_82092AA_0
#define PCI_DEVICE_ID_INTEL_82092AA_0 0x1221
#endif
#define PCIBASE_DEVICE_ID   PCI_DEVICE_ID_INTEL_82092AA_0
#define PCIBASE_CLASS_ID    PCI_CLASS_BRIDGE_PCMCIA

/* PCMCIA controller register offsets and bit definitions (from i82092.c) */
#define I365_STATUS         0x01
#define I365_INTCTL         0x03
#define I365_CSC            0x04
#define I365_CSC_READY      0x04
#define I365_CSC_STSCHG     0x01
#define I365_CSC_BVD1       0x01
#define I365_CSC_BVD2       0x02
#define I365_CSC_DETECT     0x08
#define I365_POWER          0x02
#define I365_PC_IOCARD      0x20
#define I365_PC_RESET       0x40
#define I365_PWR_AUTO       0x20
#define I365_PWR_NORESET    0x40
#define I365_PWR_OUT        0x80
#define I365_VCC_5V         0x10
#define I365_VPP1_5V        0x01
#define I365_VPP1_12V       0x02
#define I365_VPP2_5V        0x04
#define I365_VPP2_12V       0x08
#define I365_GENCTL         0x16
#define I365_GBLCTL         0x1E
#define I365_CS_BVD1        0x01
#define I365_CS_BVD2        0x02
#define I365_CS_STSCHG      0x01
#define I365_CS_DETECT      0x0C
#define I365_CS_WRPROT      0x10
#define I365_CS_READY       0x20
#define I365_CS_POWERON     0x40
#define I365_CSCINT         0x05
#define I365_IOCTL          0x07
#define I365_ADDRWIN        0x06
#define I365_W_START        0
#define I365_W_STOP         2
#define I365_W_OFF          4
#define I365_MEM_0WS        0x4000
#define I365_MEM_WRPROT     0x8000
#define I365_MEM_16BIT      0x8000
#define I365_MEM_WS0        0x4000
#define I365_MEM_WS1        0x8000
#define I365_MEM_REG        0x4000
#define I365_ENA_MEM(map)   (0x01 << (map))
#define I365_ENA_IO(map)    (0x40 << (map))
#define I365_IOCTL_16BIT(map)   (0x01 << ((map) << 2))
#define I365_IOCTL_MASK(map)    (0x0F << ((map) << 2))
#define I365_IO(map)        (0x08 + ((map) << 2))
#define I365_MEM(map)       (0x10 + ((map) << 3))

#define MAX_SOCKETS         4

/* The Linux driver uses an I/O port region of 0x0000-0x0fff via struct resource res */
#define I82092AA_IO_SIZE    0x1000


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
    /* Minimal placeholder for indirect I/O register space */
    uint8_t regs[I82092AA_IO_SIZE];
};


/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The real hardware would assert PCI IRQ for card status change events
     * based on CSC/CSCINT etc. The Linux driver, however, never clears CSC
     * bits through writes, and only polls them inside the ISR until they
     * read as zero. To avoid creating an interrupt storm without a clear
     * model of how bits are cleared, this model does not generate
     * interrupts autonomously. The guest will still be able to request
     * and use the IRQ number, but it will remain quiescent.
     */
    (void)pdev;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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
    uint64_t val = 0;

    /*
     * The driver uses an "indirect" register addressing scheme:
     *   outb(reg + socket*0x40, base);
     *   inb(base+1);
     *
     * 'addr' here is the offset from the PCI I/O BAR base.
     * offset 0: index register
     * offset 1: data register
     */

    if (size != 1) {
        return 0xff;
    }

    if (addr >= I82092AA_IO_SIZE) {
        return 0xff;
    }

    switch (addr) {
    case 0: /* index port read is not used by the driver; return 0 */
        val = 0x00;
        break;
    case 1: {
        /* data port */
        uint8_t index = s->regs[0];
        /* Indexing into per-socket register space is done in the driver.
         * The PCI device only sees the raw index value. We use a flat
         * 4KB array and let the driver provide the full index value,
         * including socket offset. */
        val = s->regs[index];
        break;
    }
    default:
        /* Only first two ports are used by the driver */
        val = 0xff;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }

    if (addr >= I82092AA_IO_SIZE) {
        return;
    }

    switch (addr) {
    case 0:
        /* index port: store selected register index */
        s->regs[0] = (uint8_t)val;
        break;
    case 1: {
        /* data port: write to currently indexed register */
        uint8_t index = s->regs[0];
        s->regs[index] = (uint8_t)val;
        break;
    }
    default:
        /* not used by this driver */
        break;
    }
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

    /* Reset register shadow space to power-on defaults (all zeros). */
    memset(s->regs, 0, sizeof(s->regs));
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

    /* BAR Initialization: the driver uses only an I/O port resource of 4KB. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = I82092AA_IO_SIZE;
    s->bar_info[0].name  = "i82092aa-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X are not used by the i82092 driver. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadow space */
    memset(s->regs, 0, sizeof(s->regs));
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i82092aa_pci",
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
