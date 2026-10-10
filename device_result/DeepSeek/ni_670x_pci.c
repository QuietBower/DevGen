/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

#define TYPE_PCIBASE_DEVICE "ni_670x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* ===== Driver-specific definitions ===== */

#define PCI_VENDOR_ID_NI          0x1093
#define PCI_DEVICE_ID_NI_6704     0x1290

#define MITE_IODWBSR   0xc0
#define WENAB          BIT(7)
#define AO_VALUE_OFFSET        0x00
#define AO_CHAN_OFFSET         0x0c
#define AO_STATUS_OFFSET       0x10
#define AO_CONTROL_OFFSET      0x10
#define DIO_PORT0_DIR_OFFSET   0x20
#define DIO_PORT0_DATA_OFFSET  0x24
#define DIO_PORT1_DIR_OFFSET   0x28
#define DIO_PORT1_DATA_OFFSET  0x2c
#define MISC_STATUS_OFFSET     0x14
#define MISC_CONTROL_OFFSET    0x14

enum ni_670x_boardid {
    BOARD_PCI6703,
    BOARD_PXI6704,
    BOARD_PCI6704,
};

struct ni_670x_board {
    const char *name;
    unsigned short ao_chans;
};

static const struct ni_670x_board ni_670x_boards[] = {
    [BOARD_PCI6703] = {
        .name       = "PCI-6703",
        .ao_chans   = 16,
    },
    [BOARD_PXI6704] = {
        .name       = "PXI-6704",
        .ao_chans   = 32,
    },
    [BOARD_PCI6704] = {
        .name       = "PCI-6704",
        .ao_chans   = 32,
    },
};

/* End of driver-specific definitions */

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t ao_value;
    uint32_t ao_chan;
    uint32_t ao_status;
    uint32_t ao_control;
    uint32_t dio_dir[2];
    uint32_t dio_data[2];
    uint32_t misc_status;
    uint32_t misc_control;
    uint32_t mite_iodwbsr;

    /* DMA Context */
    /* Pointers for DMA base addresses, count, and status */

    /* Operational status flags */
    uint32_t status_flags;

    /* State used to handle reset sequences */
    uint32_t reset_state;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* Additional info (board type, etc.) */
    enum ni_670x_boardid board_type;
};

/* MITE BAR (BAR0) MMIO handlers */
static uint64_t mite_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MITE_IODWBSR:
        val = s->mite_iodwbsr;
        break;
    default:
        /* undefined, return 0 */
        break;
    }
    return val;
}

static void mite_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MITE_IODWBSR:
        s->mite_iodwbsr = val;
        break;
    default:
        /* undefined, ignore */
        break;
    }
}

static const MemoryRegionOps mite_ops = {
    .read = mite_mmio_read,
    .write = mite_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* Main register BAR (BAR1) MMIO handlers */
static uint64_t main_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case AO_VALUE_OFFSET:
        val = s->ao_value;
        break;
    case AO_CHAN_OFFSET:
        val = s->ao_chan;
        break;
    case AO_CONTROL_OFFSET:
        val = s->ao_control;
        break;
    case DIO_PORT0_DIR_OFFSET:
        val = s->dio_dir[0];
        break;
    case DIO_PORT0_DATA_OFFSET:
        val = s->dio_data[0];
        break;
    case DIO_PORT1_DIR_OFFSET:
        val = s->dio_dir[1];
        break;
    case DIO_PORT1_DATA_OFFSET:
        val = s->dio_data[1];
        break;
    case MISC_CONTROL_OFFSET:
        val = s->misc_control;
        break;
    default:
        /* undefined, return 0 */
        break;
    }
    return val;
}

static void main_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case AO_VALUE_OFFSET:
        s->ao_value = val;
        break;
    case AO_CHAN_OFFSET:
        s->ao_chan = val;
        break;
    case AO_CONTROL_OFFSET:
        s->ao_control = val;
        break;
    case DIO_PORT0_DIR_OFFSET:
        s->dio_dir[0] = val;
        break;
    case DIO_PORT0_DATA_OFFSET:
        s->dio_data[0] = val;
        break;
    case DIO_PORT1_DIR_OFFSET:
        s->dio_dir[1] = val;
        break;
    case DIO_PORT1_DATA_OFFSET:
        s->dio_data[1] = val;
        break;
    case MISC_CONTROL_OFFSET:
        s->misc_control = val;
        break;
    default:
        /* undefined, ignore */
        break;
    }
}

static const MemoryRegionOps main_ops = {
    .read = main_mmio_read,
    .write = main_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->ao_value = 0;
    s->ao_chan = 0;
    s->ao_status = 0;
    s->ao_control = 0;
    s->dio_dir[0] = 0;
    s->dio_dir[1] = 0;
    s->dio_data[0] = 0;
    s->dio_data[1] = 0;
    s->misc_status = 0;
    s->misc_control = 0;
    s->mite_iodwbsr = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1093 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1290 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xFF00 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR0: MITE registers */
    hwaddr bar0_size = pow2ceil(0x1000);
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &mite_ops, s, "ni_670x_mite", bar0_size);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR1: Main registers */
    hwaddr bar1_size = pow2ceil(0x100);
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &main_ops, s, "ni_670x_main", bar1_size);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    s->num_bars = 2;

    s->board_type = BOARD_PCI6704;
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
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ni_670x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(ao_value, PCIBaseState),
        VMSTATE_UINT32(ao_chan, PCIBaseState),
        VMSTATE_UINT32(ao_status, PCIBaseState),
        VMSTATE_UINT32(ao_control, PCIBaseState),
        VMSTATE_UINT32_ARRAY(dio_dir, PCIBaseState, 2),
        VMSTATE_UINT32_ARRAY(dio_data, PCIBaseState, 2),
        VMSTATE_UINT32(misc_status, PCIBaseState),
        VMSTATE_UINT32(misc_control, PCIBaseState),
        VMSTATE_UINT32(mite_iodwbsr, PCIBaseState),
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
