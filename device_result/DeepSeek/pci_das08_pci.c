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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

/* Additional include files retrieved from driver context */
/* none */

#define TYPE_PCIBASE_DEVICE "pci_das08_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Vendor and Device IDs from das08_pci_table[0] */
#define PCI_VENDOR_ID_CB 0x1307
#define PCI_DEVICE_ID_DAS08 0x0029
#define PCI_CLASS_ID PCI_CLASS_OTHERS /* temporary, will be refined */

/* Register offsets provided by driver */
#define DAS08_AI_LSB_REG       0x00
#define DAS08_AI_MSB_REG       0x01
#define DAS08_AI_TRIG_REG      0x01
#define DAS08_CONTROL_REG      0x02
#define DAS08_STATUS_REG       0x02
#define DAS08_GAIN_REG         0x03

#define DAS08_CONTROL_MUX_MASK 0x7
#define DAS08_CONTROL_MUX(x)   ((x) & DAS08_CONTROL_MUX_MASK)
#define DAS08_CONTROL_DO_MASK  0xf0
#define DAS08_CONTROL_DO(x)    (((x) << 4) & DAS08_CONTROL_DO_MASK)
#define DAS08_STATUS_DI(x)     (((x) & 0x70) >> 4)

/* Board-specific static structures from driver */
enum das08_ai_encoding { das08_encode12, das08_encode16, das08_pcm_encode12 };
enum das08_lrange {
    das08_pg_none, das08_bipolar5, das08_pgh, das08_pgl, das08_pgm
};
struct das08_private_struct {
    /* bits for do/mux register on boards without separate do register */
    unsigned int do_mux_bits;
    const unsigned int *pg_gainlist;
};
struct das08_board_struct {
    const char *name;
    bool is_jr;             /* true for 'JR' boards */
    unsigned int ai_nbits;
    enum das08_lrange ai_pg;
    enum das08_ai_encoding ai_encoding;
    unsigned int ao_nbits;
    unsigned int di_nchan;
    unsigned int do_nchan;
    unsigned int i8255_offset;
    unsigned int i8254_offset;
    unsigned int iosize;    /* number of ioports used */
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

    /* PIO region data */
    uint8_t pio_data[8];

    /* Control and status registers (separate from pio_data to reflect
     * read/write semantics of offset 0x02) */
    uint8_t control;
    uint8_t status;
};

/* Additional global definitions */
static const struct das08_board_struct das08_pci_boards[] = {
    {
        .name           = "pci-das08",
        .ai_nbits       = 12,
        .ai_pg          = das08_bipolar5,
        .ai_encoding    = das08_encode12,
        .di_nchan       = 3,
        .do_nchan       = 4,
        .i8254_offset   = 4,
        .iosize         = 8,
    },
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* MMIO read not yet implemented */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* MMIO write not yet implemented */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 8) {
        switch (addr) {
        case DAS08_STATUS_REG: /* 0x02 */
            /* Return digital input bits (uppernibble) */
            val = s->status & 0x70;
            break;
        case DAS08_AI_LSB_REG: /* 0x00 */
        case DAS08_AI_MSB_REG: /* 0x01 */
        default:
            /* Default flat PIO read */
            switch (size) {
            case 1:
                val = s->pio_data[addr];
                break;
            case 2:
                val = lduw_le_p(&s->pio_data[addr]);
                break;
            case 4:
                val = ldl_le_p(&s->pio_data[addr]);
                break;
            default:
                break;
            }
            break;
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 8) {
        switch (addr) {
        case DAS08_CONTROL_REG: /* 0x02 */
            /* Control register: MUX and DO fields, clear unused bit 3 */
            s->control = (uint8_t)(val & 0xF7);
            break;
        case DAS08_GAIN_REG: /* 0x03 */
            /* Gain write-only */
            s->pio_data[3] = (uint8_t)val;
            break;
        case DAS08_AI_TRIG_REG: /* 0x01 */
            /* AI Trigger write: store value */
            s->pio_data[1] = (uint8_t)val;
            break;
        default:
            /* Write to other PIO offsets */
            switch (size) {
            case 1:
                s->pio_data[addr] = (uint8_t)val;
                break;
            case 2:
                stw_le_p(&s->pio_data[addr], (uint16_t)val);
                break;
            case 4:
                stl_le_p(&s->pio_data[addr], (uint32_t)val);
                break;
            default:
                break;
            }
            break;
        }
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

    /* Reset PIO data to zero */
    memset(s->pio_data, 0, sizeof(s->pio_data));
    s->control = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CB);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_DAS08);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 8;
    s->bar_info[2].name = "das08-io";
    pcibase_register_bar(pdev, s, &s->bar_info[2], errp);

    /* Initialize PIO data */
    memset(s->pio_data, 0, sizeof(s->pio_data));
    s->control = 0;
    s->status = 0;
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
    .name = "pci_das08_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(pio_data, PCIBaseState, 8),
        VMSTATE_UINT8(control, PCIBaseState),
        VMSTATE_UINT8(status, PCIBaseState),
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
