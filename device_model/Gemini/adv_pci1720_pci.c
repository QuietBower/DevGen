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

/* Additional include files retrieved from driver context */
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "adv_pci1720_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_ADVANTECH     0x13fe

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI1720_AO_LSB_REG(x)       (0x00 + ((x) * 2))
#define PCI1720_AO_MSB_REG(x)       (0x01 + ((x) * 2))
#define PCI1720_AO_RANGE_REG        0x08
#define PCI1720_AO_RANGE(c, r)      (((r) & 0x3) << ((c) * 2))
#define PCI1720_AO_RANGE_MASK(c)    PCI1720_AO_RANGE((c), 0x3)
#define PCI1720_SYNC_REG            0x09
#define PCI1720_SYNC_CTRL_REG       0x0f
#define PCI1720_SYNC_CTRL_SC0       BIT(0)
#define PCI1720_BOARDID_REG         0x14

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t ao_lsb[4];
    uint8_t ao_msb[4];
    uint8_t ao_range;
    uint8_t sync;
    uint8_t sync_ctrl;
    uint8_t boardid;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PCI1720_AO_LSB_REG(0): val = s->ao_lsb[0]; break;
    case PCI1720_AO_MSB_REG(0): val = s->ao_msb[0]; break;
    case PCI1720_AO_LSB_REG(1): val = s->ao_lsb[1]; break;
    case PCI1720_AO_MSB_REG(1): val = s->ao_msb[1]; break;
    case PCI1720_AO_LSB_REG(2): val = s->ao_lsb[2]; break;
    case PCI1720_AO_MSB_REG(2): val = s->ao_msb[2]; break;
    case PCI1720_AO_LSB_REG(3): val = s->ao_lsb[3]; break;
    case PCI1720_AO_MSB_REG(3): val = s->ao_msb[3]; break;
    case PCI1720_AO_RANGE_REG:  val = s->ao_range; break;
    case PCI1720_SYNC_REG:      val = s->sync; break;
    case PCI1720_SYNC_CTRL_REG: val = s->sync_ctrl; break;
    case PCI1720_BOARDID_REG:   val = s->boardid; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "adv_pci1720: read from invalid offset 0x%" HWADDR_PRIx "\n", addr);
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PCI1720_AO_LSB_REG(0): s->ao_lsb[0] = val & 0xff; break;
    case PCI1720_AO_MSB_REG(0): s->ao_msb[0] = val & 0xff; break;
    case PCI1720_AO_LSB_REG(1): s->ao_lsb[1] = val & 0xff; break;
    case PCI1720_AO_MSB_REG(1): s->ao_msb[1] = val & 0xff; break;
    case PCI1720_AO_LSB_REG(2): s->ao_lsb[2] = val & 0xff; break;
    case PCI1720_AO_MSB_REG(2): s->ao_msb[2] = val & 0xff; break;
    case PCI1720_AO_LSB_REG(3): s->ao_lsb[3] = val & 0xff; break;
    case PCI1720_AO_MSB_REG(3): s->ao_msb[3] = val & 0xff; break;
    case PCI1720_AO_RANGE_REG:  s->ao_range = val & 0xff; break;
    case PCI1720_SYNC_REG:      s->sync = val & 0xff; break;
    case PCI1720_SYNC_CTRL_REG: s->sync_ctrl = val & 0xff; break;
    case PCI1720_BOARDID_REG:   s->boardid = val & 0xff; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "adv_pci1720: write to invalid offset 0x%" HWADDR_PRIx "\n", addr);
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

    s->ao_range = 0;
    s->sync = 0;
    s->sync_ctrl = 0;
    s->boardid = 0;
    for (int i = 0; i < 4; i++) {
        s->ao_lsb[i] = 0;
        s->ao_msb[i] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ADVANTECH );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1720 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x20;
    s->bar_info[0].name = "adv_pci1720_io";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "adv_pci1720_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(ao_lsb, PCIBaseState, 4),
        VMSTATE_UINT8_ARRAY(ao_msb, PCIBaseState, 4),
        VMSTATE_UINT8(ao_range, PCIBaseState),
        VMSTATE_UINT8(sync, PCIBaseState),
        VMSTATE_UINT8(sync_ctrl, PCIBaseState),
        VMSTATE_UINT8(boardid, PCIBaseState),
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
