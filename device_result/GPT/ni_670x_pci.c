/*
 * QEMU 8.2.10 PCI device model for NI 670x (comedi ni_670x.c)
 * Phase 2: Functional behavior implementation.
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

#define TYPE_PCIBASE_DEVICE "ni_670x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define NI_670X_VENDOR_ID        0x1093
#define NI_670X_DEVICE_ID        0x1290
#define NI_670X_CLASS_ID         0xff

#define MITE_IODWBSR             0xc0
#define WENAB                    (1U << 7)

#define AO_VALUE_OFFSET          0x00
#define AO_CHAN_OFFSET           0x0c
#define AO_STATUS_OFFSET         0x10
#define AO_CONTROL_OFFSET        0x10
#define DIO_PORT0_DIR_OFFSET     0x20
#define DIO_PORT0_DATA_OFFSET    0x24
#define DIO_PORT1_DIR_OFFSET     0x28
#define DIO_PORT1_DATA_OFFSET    0x2c
#define MISC_STATUS_OFFSET       0x14
#define MISC_CONTROL_OFFSET      0x14

enum ni_670x_boardid {
    BOARD_PCI6703,
    BOARD_PXI6704,
    BOARD_PCI6704,
};

enum caldac_enum {
    caldac_none = 0,
    mb88341,
    dac8800,
    dac8043,
    ad8522,
    ad8804,
    ad8842,
    ad8804_debug
};

struct ni_670x_board {
    const char *name;
    unsigned short ao_chans;
};

struct ni_670x_private {
    int boardtype;
    int dio;
};

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
    struct {
        uint32_t ao_value;
        uint32_t ao_chan;
        uint32_t ao_status;
        uint32_t dio_port0_dir;
        uint32_t dio_port0_data;
        uint32_t dio_port1_dir;
        uint32_t dio_port1_data;
        uint32_t misc_status;
    } regs;

};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The provided driver never uses interrupts, so do nothing here. */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The ni_670x driver does not set up any DMA, so this is intentionally empty. */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Implement simple 32-bit register accesses based on offsets used by driver. */
    if (size != 4) {
        /* The driver always uses readl/writel (32-bit). For other sizes, return 0. */
        return 0;
    }

    switch (addr) {
    case AO_VALUE_OFFSET:
        /* No direct reads from AO_VALUE in driver, but provide shadow. */
        val = s->regs.ao_value;
        break;
    case AO_CHAN_OFFSET:
        val = s->regs.ao_chan;
        break;
    case AO_STATUS_OFFSET:
        /* AO_STATUS and AO_CONTROL share offset; return status shadow. */
        val = s->regs.ao_status;
        break;
    case DIO_PORT0_DIR_OFFSET:
        val = s->regs.dio_port0_dir;
        break;
    case DIO_PORT0_DATA_OFFSET:
        /* Readback of DIO data for port0. */
        val = s->regs.dio_port0_data;
        break;
    case DIO_PORT1_DIR_OFFSET:
        val = s->regs.dio_port1_dir;
        break;
    case DIO_PORT1_DATA_OFFSET:
        val = s->regs.dio_port1_data;
        break;
    case MISC_STATUS_OFFSET:
        /* MISC_STATUS and MISC_CONTROL share offset; return misc_status shadow. */
        val = s->regs.misc_status;
        break;
    default:
        /* For all other addresses, return 0 to emulate unused/undocumented regs. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        /* Driver uses writel (32-bit). Ignore other sizes. */
        return;
    }

    switch (addr) {
    case AO_CHAN_OFFSET:
        /* Driver writes encoded channel here before AO_VALUE. */
        s->regs.ao_chan = (uint32_t)val;
        break;
    case AO_VALUE_OFFSET:
        /* Store the DAC value; driver uses comedi readback, not hardware read. */
        s->regs.ao_value = (uint32_t)val;
        break;
    case AO_CONTROL_OFFSET:
        /* AO_CONTROL and AO_STATUS share offset; only initialization write seen. */
        s->regs.ao_status = (uint32_t)val;
        break;
    case DIO_PORT0_DIR_OFFSET:
        /* Direction bits for first DIO port. */
        s->regs.dio_port0_dir = (uint32_t)val;
        break;
    case DIO_PORT0_DATA_OFFSET:
        /* Data bits for first DIO port. */
        s->regs.dio_port0_data = (uint32_t)val;
        break;
    case DIO_PORT1_DIR_OFFSET:
        /* Not directly used by this driver, but keep shadow for completeness. */
        s->regs.dio_port1_dir = (uint32_t)val;
        break;
    case DIO_PORT1_DATA_OFFSET:
        /* Not directly used by this driver, but keep shadow. */
        s->regs.dio_port1_data = (uint32_t)val;
        break;
    case MISC_CONTROL_OFFSET:
        /* MISC_CONTROL and MISC_STATUS share offset; driver writes init value here. */
        s->regs.misc_status = (uint32_t)val;
        break;
    default:
        /* Ignore writes to unspecified addresses. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    /* The ni_670x driver never uses I/O port space; return 0 for all reads. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;

    /* The driver does not access any PIO BAR; ignore all writes. */
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

    /* Initialize register shadows to power-on defaults consistent with driver usage. */
    s->regs.ao_value = 0;
    s->regs.ao_chan = 0;
    s->regs.ao_status = 0;
    s->regs.dio_port0_dir = 0;
    s->regs.dio_port0_data = 0;
    s->regs.dio_port1_dir = 0;
    s->regs.dio_port1_data = 0;
    s->regs.misc_status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  NI_670X_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  NI_670X_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, NI_670X_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Enable PCIe capability for completeness, though the card may be PCI. */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The driver uses BAR 0 for temporary MITE window and BAR 1 for main regs.
     * We model both as MMIO regions with reasonable sizes covering used offsets.
     */
    s->num_bars = 2;

    /* BAR 0: MITE registers (only MITE_IODWBSR at 0xc0 is used). Use 4 KiB. */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "ni_670x_mite";

    /* BAR 1: main device registers (AO/DIO/MISC). Use 4 KiB. */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "ni_670x_regs";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal register state as on reset. */
    s->regs.ao_value = 0;
    s->regs.ao_chan = 0;
    s->regs.ao_status = 0;
    s->regs.dio_port0_dir = 0;
    s->regs.dio_port0_data = 0;
    s->regs.dio_port1_dir = 0;
    s->regs.dio_port1_data = 0;
    s->regs.misc_status = 0;
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
    .name = "ni_670x_pci",
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

