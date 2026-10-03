/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and PCIe driver probing.
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

#define TYPE_PCIBASE_DEVICE "addi_apci_3xxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_ADDIDATA
#define PCI_VENDOR_ID_ADDIDATA 0x15b8
#endif

#define APCI3XXX_MMIO_CHAN_CTRL  0x00
#define APCI3XXX_MMIO_DELAY_MODE 0x04
#define APCI3XXX_MMIO_START_CMD  0x08
#define APCI3XXX_MMIO_FIFO_CTRL  0x0C
#define APCI3XXX_MMIO_INT_FLAGS  0x10
#define APCI3XXX_MMIO_EOS        0x14
#define APCI3XXX_MMIO_FIFO       0x1C
#define APCI3XXX_MMIO_SEQ_COUNT  0x30
#define APCI3XXX_MMIO_AO_RANGE   0x60
#define APCI3XXX_MMIO_AO_DATA    0x64

#define APCI3XXX_IO_DI           0x20
#define APCI3XXX_IO_DO           0x30

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

    /* MMIO Registers */
    uint32_t mmio_chan_ctrl;
    uint32_t mmio_delay_mode;
    uint32_t mmio_start_cmd;
    uint32_t mmio_int_flags;
    uint32_t mmio_eos;
    uint32_t mmio_fifo;
    uint32_t mmio_timer;
    uint32_t mmio_time_base;
    uint32_t mmio_seq_count;
    uint32_t mmio_ao_range;
    uint32_t mmio_ao_data;

    /* PIO Registers */
    uint32_t pio_di;
    uint32_t pio_do;
    uint32_t pio_dio_64;
    uint32_t pio_dio_80;
    uint32_t pio_dio_96;
    uint32_t pio_dio_112;
    uint32_t pio_dio_224;
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = (s->mmio_int_flags & 0x2) != 0;
    pci_set_irq(pdev, pending ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x04:
        val = s->mmio_delay_mode;
        break;
    case 0x08:
        val = s->mmio_start_cmd;
        break;
    case 0x10:
        val = s->mmio_int_flags;
        break;
    case 0x14:
        val = s->mmio_eos;
        break;
    case 0x1C:
        val = s->mmio_fifo;
        break;
    case 0x60:
        val = s->mmio_ao_range;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00:
        s->mmio_chan_ctrl = val;
        break;
    case 0x04:
        s->mmio_delay_mode = val;
        break;
    case 0x08:
        s->mmio_start_cmd = val;
        if (val == 0x80000 || val == 0x180000) {
            s->mmio_eos |= 0x1; /* Signal AI EOC */
        }
        break;
    case 0x0C:
        if (val == 0x10000) {
            s->mmio_fifo = 0; /* Clear FIFO */
        }
        break;
    case 0x10:
        s->mmio_int_flags &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case 0x20:
        s->mmio_timer = val;
        break;
    case 0x24:
        s->mmio_time_base = val;
        break;
    case 0x30:
        s->mmio_seq_count = val;
        break;
    case 0x60:
        s->mmio_ao_range = val;
        break;
    case 0x64:
        s->mmio_ao_data = val;
        s->mmio_ao_range |= 0x100; /* Signal AO EOC */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 32:
        val = s->pio_di;
        break;
    case 48:
        val = s->pio_do;
        break;
    case 64:
        val = s->pio_dio_64;
        break;
    case 80:
        val = s->pio_dio_80;
        break;
    case 96:
        val = s->pio_dio_96;
        break;
    case 112:
        val = s->pio_dio_112;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 48:
        s->pio_do = val;
        break;
    case 80:
        s->pio_dio_80 = val;
        break;
    case 112:
        s->pio_dio_112 = val;
        break;
    case 224:
        s->pio_dio_224 = val;
        break;
    default:
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

    s->mmio_chan_ctrl = 0;
    s->mmio_delay_mode = 0;
    s->mmio_start_cmd = 0;
    s->mmio_int_flags = 0;
    s->mmio_eos = 0x1; /* Ready state for AI EOC */
    s->mmio_fifo = 0;
    s->mmio_timer = 0;
    s->mmio_time_base = 0;
    s->mmio_seq_count = 0;
    s->mmio_ao_range = 0x100; /* Ready state for AO EOC */
    s->mmio_ao_data = 0;

    s->pio_di = 0;
    s->pio_do = 0;
    s->pio_dio_64 = 0;
    s->pio_dio_80 = 0;
    s->pio_dio_96 = 0;
    s->pio_dio_112 = 0;
    s->pio_dio_224 = 0;

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ADDIDATA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x3010);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 4;
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 256, .name = "apci3xxx_io" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_MMIO, .size = 4096, .name = "apci3xxx_mmio" };
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "addi_apci_3xxx_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(mmio_chan_ctrl, PCIBaseState),
        VMSTATE_UINT32(mmio_delay_mode, PCIBaseState),
        VMSTATE_UINT32(mmio_start_cmd, PCIBaseState),
        VMSTATE_UINT32(mmio_int_flags, PCIBaseState),
        VMSTATE_UINT32(mmio_eos, PCIBaseState),
        VMSTATE_UINT32(mmio_fifo, PCIBaseState),
        VMSTATE_UINT32(mmio_timer, PCIBaseState),
        VMSTATE_UINT32(mmio_time_base, PCIBaseState),
        VMSTATE_UINT32(mmio_seq_count, PCIBaseState),
        VMSTATE_UINT32(mmio_ao_range, PCIBaseState),
        VMSTATE_UINT32(mmio_ao_data, PCIBaseState),
        VMSTATE_UINT32(pio_di, PCIBaseState),
        VMSTATE_UINT32(pio_do, PCIBaseState),
        VMSTATE_UINT32(pio_dio_64, PCIBaseState),
        VMSTATE_UINT32(pio_dio_80, PCIBaseState),
        VMSTATE_UINT32(pio_dio_96, PCIBaseState),
        VMSTATE_UINT32(pio_dio_112, PCIBaseState),
        VMSTATE_UINT32(pio_dio_224, PCIBaseState),
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
