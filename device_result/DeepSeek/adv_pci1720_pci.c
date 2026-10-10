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
/* (none) */

#define TYPE_PCIBASE_DEVICE "adv_pci1720_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ADVANTECH    0x13FE
#define PCI1720_AO_LSB_REG(x)      (0x00 + ((x) * 2))
#define PCI1720_AO_MSB_REG(x)      (0x01 + ((x) * 2))
#define PCI1720_AO_RANGE_REG       0x08
#define PCI1720_AO_RANGE(c, r)     (((r) & 0x3) << ((c) * 2))
#define PCI1720_AO_RANGE_MASK(c)   PCI1720_AO_RANGE((c), 0x3)
#define PCI1720_SYNC_REG           0x09
#define PCI1720_SYNC_CTRL_REG      0x0f
#define PCI1720_SYNC_CTRL_SC0      BIT(0)
#define PCI1720_BOARDID_REG        0x14
#define PCI1720_BAR2_SIZE          0x20  /* covers all known registers (0x00-0x14), pow2ceil(0x15) */
#define BIT(n)                     (1U << (n))

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

    /* Device-specific registers */
    uint16_t ao_values[4];
    uint8_t range_reg;
    uint8_t sync_ctrl_reg;
    uint8_t board_id;
};

/* PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 1) {
        return ~0ULL;
    }

    switch (addr) {
    case 0x00: /* AO LSB ch0 */
    case 0x02: /* AO LSB ch1 */
    case 0x04: /* AO LSB ch2 */
    case 0x06: /* AO LSB ch3 */
        {
            int ch = (addr >> 1);
            val = s->ao_values[ch] & 0xFF;
        }
        break;
    case 0x01: /* AO MSB ch0 */
    case 0x03: /* AO MSB ch1 */
    case 0x05: /* AO MSB ch2 */
    case 0x07: /* AO MSB ch3 */
        {
            int ch = (addr >> 1);
            val = (s->ao_values[ch] >> 8) & 0xFF;
        }
        break;
    case 0x08: /* range reg */
        val = s->range_reg;
        break;
    case 0x0f: /* sync ctrl reg */
        val = s->sync_ctrl_reg;
        break;
    case 0x14: /* board id reg */
        val = s->board_id;
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

    if (size != 1) {
        return;
    }

    switch (addr) {
    case 0x00: case 0x02: case 0x04: case 0x06: /* LSB */
        {
            int ch = (addr >> 1);
            s->ao_values[ch] = (s->ao_values[ch] & 0xFF00) | (val & 0xFF);
        }
        break;
    case 0x01: case 0x03: case 0x05: case 0x07: /* MSB */
        {
            int ch = (addr >> 1);
            s->ao_values[ch] = (s->ao_values[ch] & 0x00FF) | ((val & 0xFF) << 8);
        }
        break;
    case 0x08: /* range reg */
        s->range_reg = val & 0xFF;
        break;
    case 0x0f: /* sync ctrl reg */
        s->sync_ctrl_reg = val & 0xFF;
        break;
    /* board id reg is read-only, ignore writes */
    default:
        break;
    }
}

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

    memset(s->ao_values, 0, sizeof(s->ao_values));
    s->range_reg = 0;
    s->sync_ctrl_reg = 0;
    /* board_id is a property, not reset to maintain user-set value */
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x13FE);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x1720);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xFF0000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = PCI1720_BAR2_SIZE;
    s->bar_info[0].name = "bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X init – driver does not request these */

    /* No DMA configuration */

    /* No internal hardware timers */

    /* Initial state: ao values zero, range zero, sync ctrl zero, board_id from property */
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

    /* No additional cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "adv_pci1720_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16_ARRAY(ao_values, PCIBaseState, 4),
        VMSTATE_UINT8(range_reg, PCIBaseState),
        VMSTATE_UINT8(sync_ctrl_reg, PCIBaseState),
        VMSTATE_UINT8(board_id, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static Property pcibase_properties[] = {
    DEFINE_PROP_UINT8("board_id", PCIBaseState, board_id, 0),
    DEFINE_PROP_END_OF_LIST(),
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
    device_class_set_props(dc, pcibase_properties);
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
