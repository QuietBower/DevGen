/*
 * QEMU PCI device model for Intel SNB-EP uncore Home Agent (HA) performance monitor.
 * Generated from driver: arch/x86/events/intel/uncore_snbep.c
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

#define TYPE_PCIBASE_DEVICE "snr_uncore_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor/Device IDs */
#define SNB_UNCORE_HA_DEVICE_ID  0x3c46
#define SNB_UNCORE_HA_CLASS_ID   0x0880  /* System peripheral */

/* SNB-EP PCI uncore register offsets */
#define SNBEP_PCI_PMON_BOX_CTL            0xf4
#define SNBEP_PCI_PMON_CTL0                0xd8
#define SNBEP_PCI_PMON_CTR0                0xa0
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0   0x40
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1   0x44
#define SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH  0x48

/* Box control bits */
#define SNBEP_PMON_BOX_CTL_RST_CTRL  (1 << 0)
#define SNBEP_PMON_BOX_CTL_RST_CTRS  (1 << 1)
#define SNBEP_PMON_BOX_CTL_FRZ       (1 << 8)
#define SNBEP_PMON_BOX_CTL_FRZ_EN    (1 << 16)
#define SNBEP_PMON_BOX_CTL_INT       (SNBEP_PMON_BOX_CTL_RST_CTRL | \
                                      SNBEP_PMON_BOX_CTL_RST_CTRS | \
                                      SNBEP_PMON_BOX_CTL_FRZ_EN)

/* Event control bits */
#define SNBEP_PMON_CTL_EV_SEL_MASK   0x000000ff
#define SNBEP_PMON_CTL_UMASK_MASK    0x0000ff00
#define SNBEP_PMON_CTL_RST           (1 << 17)
#define SNBEP_PMON_CTL_EDGE_DET      (1 << 18)
#define SNBEP_PMON_CTL_EV_SEL_EXT    (1 << 21)
#define SNBEP_PMON_CTL_EN            (1 << 22)
#define SNBEP_PMON_CTL_INVERT        (1 << 23)
#define SNBEP_PMON_CTL_TRESH_MASK    0xff000000
#define SNBEP_PMON_RAW_EVENT_MASK     (SNBEP_PMON_CTL_EV_SEL_MASK | \
                                       SNBEP_PMON_CTL_UMASK_MASK | \
                                       SNBEP_PMON_CTL_EDGE_DET | \
                                       SNBEP_PMON_CTL_INVERT | \
                                       SNBEP_PMON_CTL_TRESH_MASK)

/* HA-specific raw event mask (same as common) */
#define SNBEP_HA_RAW_EVENT_MASK      SNBEP_PMON_RAW_EVENT_MASK

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
    uint32_t box_ctl;         /* Shadow of box control register */
    uint32_t event_ctl[4];    /* Shadow of event control registers (4 counters) */
    uint64_t counter[4];      /* Shadow of 48-bit counters */
    uint32_t addr_match0;     /* Address match 0 */
    uint32_t addr_match1;     /* Address match 1 */
    uint32_t opcode_match;    /* Opcode match */

    /* Operational status flags */
    uint32_t status;

    /* State used to handle reset sequences */
    bool reset_in_progress;

    /* Power management state (D0-D3) - minimal */
    uint8_t pm_state;
};

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (len != 4) {
        return pci_default_read_config(pdev, addr, len);
    }

    switch (addr) {
    case 0x40: return s->addr_match0;
    case 0x44: return s->addr_match1;
    case 0x48: return s->opcode_match;
    case 0xa0: return (uint32_t)(s->counter[0] & 0xffffffff);
    case 0xa4: return (uint32_t)(s->counter[0] >> 32);
    case 0xa8: return (uint32_t)(s->counter[1] & 0xffffffff);
    case 0xac: return (uint32_t)(s->counter[1] >> 32);
    case 0xb0: return (uint32_t)(s->counter[2] & 0xffffffff);
    case 0xb4: return (uint32_t)(s->counter[2] >> 32);
    case 0xb8: return (uint32_t)(s->counter[3] & 0xffffffff);
    case 0xbc: return (uint32_t)(s->counter[3] >> 32);
    case 0xd8: return s->event_ctl[0];
    case 0xdc: return s->event_ctl[1];
    case 0xe0: return s->event_ctl[2];
    case 0xe4: return s->event_ctl[3];
    case 0xf4: return s->box_ctl;
    default:
        return pci_default_read_config(pdev, addr, len);
    }
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (len != 4) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    switch (addr) {
    case 0x40: s->addr_match0 = val; break;
    case 0x44: s->addr_match1 = val; break;
    case 0x48: s->opcode_match = val; break;
    case 0xa0: s->counter[0] = (s->counter[0] & 0xffffffff00000000ULL) | val; break;
    case 0xa4: s->counter[0] = (s->counter[0] & 0xffffffff) | ((uint64_t)val << 32); break;
    case 0xa8: s->counter[1] = (s->counter[1] & 0xffffffff00000000ULL) | val; break;
    case 0xac: s->counter[1] = (s->counter[1] & 0xffffffff) | ((uint64_t)val << 32); break;
    case 0xb0: s->counter[2] = (s->counter[2] & 0xffffffff00000000ULL) | val; break;
    case 0xb4: s->counter[2] = (s->counter[2] & 0xffffffff) | ((uint64_t)val << 32); break;
    case 0xb8: s->counter[3] = (s->counter[3] & 0xffffffff00000000ULL) | val; break;
    case 0xbc: s->counter[3] = (s->counter[3] & 0xffffffff) | ((uint64_t)val << 32); break;
    case 0xd8: s->event_ctl[0] = val; break;
    case 0xdc: s->event_ctl[1] = val; break;
    case 0xe0: s->event_ctl[2] = val; break;
    case 0xe4: s->event_ctl[3] = val; break;
    case 0xf4: s->box_ctl = val; break;
    default:
        pci_default_write_config(pdev, addr, val, len);
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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
    s->box_ctl = 0;
    memset(s->event_ctl, 0, sizeof(s->event_ctl));
    memset(s->counter, 0, sizeof(s->counter));
    s->addr_match0 = 0;
    s->addr_match1 = 0;
    s->opcode_match = 0;
    s->status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SNB_UNCORE_HA_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SNB_UNCORE_HA_CLASS_ID );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "snb_uncore_ha.bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X init */
    /* No DMA config */
    /* No timer config */

    /* Final state initialization */
    s->box_ctl = 0;
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
    .name = "snr_uncore_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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