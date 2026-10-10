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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "labpc_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1093
#define DEVICE_ID 0x0161
#define CLASS_ID PCI_CLASS_OTHERS

#define MITE_IODWBSR 0xc0
#define WENAB BIT(7)

#define CMD1_REG 0x00
#define CMD2_REG 0x01
#define CMD3_REG 0x02
#define CMD4_REG 0x0f
#define CMD5_REG 0x1c
#define CMD6_REG 0x0e

#define STAT1_REG 0x00
#define STAT2_REG 0x1d

#define DIO_BASE_REG 0x10
#define COUNTER_A_BASE_REG 0x14
#define COUNTER_B_BASE_REG 0x18

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

typedef struct {
    PCIBaseState *pci_state;
    int bar_index;
} MMIOState;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t cmd1;
    uint32_t cmd2;
    uint32_t cmd3;
    uint32_t cmd4;
    uint32_t cmd5;
    uint32_t cmd6;
    uint32_t stat1;
    uint32_t stat2;
    uint32_t dio;
    uint32_t counter_a;
    uint32_t counter_b;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    MMIOState *ms = (MMIOState *)opaque;
    PCIBaseState *s = ms->pci_state;
    int bar = ms->bar_index;
    uint64_t val = 0;

    if (bar == 0) {
        /* BAR0: MITE registers */
        if (addr == MITE_IODWBSR && size == 4) {
            val = 0;
        }
    } else if (bar == 1) {
        /* BAR1: Main device registers */
        switch (addr) {
        case STAT1_REG:
            val = s->stat1;
            break;
        case CMD2_REG:
            val = s->cmd2;
            break;
        case CMD3_REG:
            val = s->cmd3;
            break;
        case CMD4_REG:
            val = s->cmd4;
            break;
        case CMD5_REG:
            val = s->cmd5;
            break;
        case CMD6_REG:
            val = s->cmd6;
            break;
        case STAT2_REG:
            val = s->stat2;
            break;
        case DIO_BASE_REG:
            val = s->dio;
            break;
        case COUNTER_A_BASE_REG:
            val = s->counter_a;
            break;
        case COUNTER_B_BASE_REG:
            val = s->counter_b;
            break;
        default:
            /* Unknown register: return 0 */
            val = 0;
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MMIOState *ms = (MMIOState *)opaque;
    PCIBaseState *s = ms->pci_state;
    int bar = ms->bar_index;

    if (bar == 0) {
        /* BAR0: MITE registers */
        if (addr == MITE_IODWBSR && size == 4) {
            /* Driver writes main_phys_addr | WENAB to set window.
             * We simply acknowledge the write; no windowing emulation needed
             * since BAR1 is directly accessible. */
        }
    } else if (bar == 1) {
        /* BAR1: Main device registers */
        switch (addr) {
        case CMD1_REG:
            s->cmd1 = val & 0xff;
            break;
        case CMD2_REG:
            s->cmd2 = val & 0xff;
            break;
        case CMD3_REG:
            s->cmd3 = val & 0xff;
            break;
        case CMD4_REG:
            s->cmd4 = val & 0xff;
            break;
        case CMD5_REG:
            s->cmd5 = val & 0xff;
            break;
        case CMD6_REG:
            s->cmd6 = val & 0xff;
            break;
        case DIO_BASE_REG:
            s->dio = val & 0xff;
            break;
        case COUNTER_A_BASE_REG:
            s->counter_a = val & 0xff;
            break;
        case COUNTER_B_BASE_REG:
            s->counter_b = val & 0xff;
            break;
        default:
            /* Other registers (8254/8255/calibration) not yet implemented */
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used; PIO BARs not implemented */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used; PIO BARs not implemented */
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

    s->cmd1 = s->cmd2 = s->cmd3 = s->cmd4 = s->cmd5 = s->cmd6 = 0;
    s->stat1 = s->stat2 = 0;
    s->dio = s->counter_a = s->counter_b = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1093);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0161);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "labpc_mite" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "labpc_main" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Set per-BAR opaque to enable dispatching in the shared MMIO ops */
    for (int i = 0; i < s->num_bars; i++) {
        MMIOState *ms = g_new0(MMIOState, 1);
        ms->pci_state = s;
        ms->bar_index = i;
        s->bar_regions[i].opaque = ms;
    }

    s->cmd1 = s->cmd2 = s->cmd3 = s->cmd4 = s->cmd5 = s->cmd6 = 0;
    s->stat1 = s->stat2 = 0;
    s->dio = s->counter_a = s->counter_b = 0;
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

    /* Free per-BAR opaques */
    for (int i = 0; i < s->num_bars; i++) {
        g_free(s->bar_regions[i].opaque);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "labpc_pci_pci",
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
