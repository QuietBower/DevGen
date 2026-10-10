/*
 * QEMU PCI device model for MOXA Smartio/Industio serial board (mxser)
 * This model provides minimal emulation to allow the mxser driver to probe and bind.
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

#define TYPE_PCIBASE_DEVICE "mxser_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1393
#define DEVICE_ID 0x1680
#define CLASS_ID 0x0702

#define MXSER_PORTS_PER_BOARD 8
#define MXSER_IO_BAR_SIZE      64
#define MXSER_VECTOR_BAR_SIZE  16

#define UART_LCR_DLAB  0x80
#define UART_LSR_THRE  0x20
#define UART_LSR_TEMT  0x40

enum mxser_must_hwid {
    MOXA_OTHER_UART     = 0x00,
    MOXA_MUST_MU150_HWID = 0x01,
    MOXA_MUST_MU860_HWID = 0x02,
};

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

static const BARInfo mxser_bars[] = {
    {
        .index = 2,
        .type = BAR_TYPE_PIO,
        .size = MXSER_IO_BAR_SIZE,
        .name = "mxser(IO)",
    },
    {
        .index = 3,
        .type = BAR_TYPE_PIO,
        .size = MXSER_VECTOR_BAR_SIZE,
        .name = "mxser(vector)",
    },
};

static const unsigned int mxser_num_bars = ARRAY_SIZE(mxser_bars);

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    
    uint32_t intr_status;
    uint32_t intr_mask;
    uint8_t irq_pin;

    struct {
        uint8_t hwid;
    } board_regs;

    uint32_t status;

    /* Per-port UART state */
    struct {
        uint8_t lcr;
        uint8_t ier;
        uint8_t fcr;
        uint8_t mcr;
        uint8_t lsr;
        uint8_t msr;
        uint8_t dll;
        uint8_t dlm;
    } port[MXSER_PORTS_PER_BOARD];

    /* Vector region state */
    uint8_t vector[MXSER_VECTOR_BAR_SIZE];
};

static uint64_t pcibase_uart_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    int port_idx = addr >> 3;
    int reg = addr & 7;

    if (port_idx >= MXSER_PORTS_PER_BOARD) {
        return ~0ULL;
    }

    uint8_t val = 0;

    switch (reg) {
    case 0: /* RBR or DLL */
        if (s->port[port_idx].lcr & UART_LCR_DLAB) {
            val = s->port[port_idx].dll;
        } else {
            val = 0; /* no received data */
        }
        break;
    case 1: /* IER or DLM */
        if (s->port[port_idx].lcr & UART_LCR_DLAB) {
            val = s->port[port_idx].dlm;
        } else {
            val = s->port[port_idx].ier;
        }
        break;
    case 2: /* IIR */
        val = 0x01; /* no interrupt pending */
        break;
    case 3: /* LCR */
        val = s->port[port_idx].lcr;
        break;
    case 4: /* MCR */
        val = s->port[port_idx].mcr;
        break;
    case 5: /* LSR */
        val = s->port[port_idx].lsr;
        break;
    case 6: /* MSR */
        val = s->port[port_idx].msr;
        break;
    case 7: /* SCR */
        val = 0;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_uart_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port_idx = addr >> 3;
    int reg = addr & 7;

    if (port_idx >= MXSER_PORTS_PER_BOARD) {
        return;
    }

    uint8_t v = (uint8_t)val;

    switch (reg) {
    case 0: /* THR or DLL */
        if (s->port[port_idx].lcr & UART_LCR_DLAB) {
            s->port[port_idx].dll = v;
        } else {
            /* THR write: transmit data, ignored */
        }
        break;
    case 1: /* IER or DLM */
        if (s->port[port_idx].lcr & UART_LCR_DLAB) {
            s->port[port_idx].dlm = v;
        } else {
            s->port[port_idx].ier = v;
        }
        break;
    case 2: /* FCR */
        s->port[port_idx].fcr = v;
        break;
    case 3: /* LCR */
        s->port[port_idx].lcr = v;
        break;
    case 4: /* MCR */
        s->port[port_idx].mcr = v;
        break;
    case 5: /* LSR (read-only, ignore) */
        break;
    case 6: /* MSR (read-only, ignore) */
        break;
    case 7: /* SCR */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_vector_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == 0) {
        return 0xFF; /* no interrupt pending */
    }
    return 0;
}

static void pcibase_vector_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < MXSER_VECTOR_BAR_SIZE) {
        s->vector[addr] = (uint8_t)val;
    }
}

static const MemoryRegionOps pcibase_uart_pio_ops = {
    .read = pcibase_uart_pio_read,
    .write = pcibase_uart_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_vector_pio_ops = {
    .read = pcibase_vector_pio_read,
    .write = pcibase_vector_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Unused handlers remain to satisfy template structure */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize per-port UART state to power-on defaults */
    for (int i = 0; i < MXSER_PORTS_PER_BOARD; i++) {
        s->port[i].lcr = 0x00;
        s->port[i].ier = 0x00;
        s->port[i].fcr = 0x00;
        s->port[i].mcr = 0x00;
        s->port[i].lsr = UART_LSR_THRE | UART_LSR_TEMT; /* 0x60 */
        s->port[i].msr = 0x00;
        s->port[i].dll = 0x00;
        s->port[i].dlm = 0x00;
    }

    /* Vector region: all zeros except offset 0 set to 0xFF for no interrupts */
    memset(s->vector, 0, MXSER_VECTOR_BAR_SIZE);
    s->vector[0] = 0xFF;

    s->board_regs.hwid = MOXA_OTHER_UART;
    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* Select the appropriate PIO ops based on bar index */
        const MemoryRegionOps *ops;
        if (bi->index == 2) {
            ops = &pcibase_uart_pio_ops;
        } else if (bi->index == 3) {
            ops = &pcibase_vector_pio_ops;
        } else {
            ops = &pcibase_uart_pio_ops; /* fallback */
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->has_msi = false;
    s->has_msix = false;

    s->num_bars = mxser_num_bars;
    for (int i = 0; i < s->num_bars; i++) {
        s->bar_info[i] = mxser_bars[i];
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize port states with default values */
    pcibase_reset(DEVICE(s));
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

static const VMStateDescription vmstate_pcibase = {
    .name = "mxser_pci",
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
