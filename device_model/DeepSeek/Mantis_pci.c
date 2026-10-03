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

#define TYPE_PCIBASE_DEVICE "Mantis_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID         0x1822
#define DEVICE_ID         0x4e35
#define CLASS_ID          0x0480

/* Register offsets */
#define MANTIS_INT_STAT         0x00
#define MANTIS_INT_MASK         0x04
#define MANTIS_DMA_CTL          0x08
#define MANTIS_RISC_START       0x10
#define MANTIS_I2CDATA_CTL      0x18
#define MANTIS_CONTROL          0x28
#define MANTIS_GPIF_STATUS      0x9c
#define MANTIS_GPIF_ADDR        0xb0
#define MANTIS_GPIF_DOUT        0xb4
#define MANTIS_UART_CTL         0xe0
#define MANTIS_UART_BAUD        0xec

/* Interrupt bits */
#define MANTIS_INT_RISCI        BIT(1)
#define MANTIS_INT_PCMCIA1      BIT(13)
#define MANTIS_INT_OCERR        BIT(8)
#define MANTIS_GPIF_OTHERR      BIT(4)
#define MANTIS_GPIF_EXTIRQ      BIT(2)
#define MANTIS_INT_PCMCIA2      BIT(14)
#define MANTIS_INT_I2CDONE      BIT(0)
#define MANTIS_INT_IRQ0         BIT(10)
#define MANTIS_INT_PABORT       BIT(7)
#define MANTIS_INT_PCMCIA5      BIT(17)
#define MANTIS_INT_FTRGT        BIT(3)
#define MANTIS_INT_PCMCIA3      BIT(15)
#define MANTIS_INT_IRQ1         BIT(11)
#define MANTIS_INT_PCMCIA4      BIT(16)
#define MANTIS_INT_PCMCIA6      BIT(18)
#define MANTIS_INT_PCMCIA0      BIT(12)
#define MANTIS_INT_PCMCIA7      BIT(19)
#define MANTIS_INT_I2CRACK      BIT(26)
#define MANTIS_INT_RISCEN       BIT(27)
#define MANTIS_INT_RISCSTAT     (0x0f << 28)
#define MANTIS_SBUF_WSTO        BIT(3)
#define MANTIS_INT_PPERR        BIT(5)
#define MANTIS_GPIF_WRACK       BIT(7)
#define MANTIS_INT_RIPERR       BIT(6)

/* Control register bits */
#define MANTIS_BYPASS           BIT(2)
#define MANTIS_DCAP_EN          BIT(1)
#define MANTIS_FIFO_EN          BIT(2)
#define MANTIS_RISC_EN          BIT(0)

/* UART bits */
#define MANTIS_UART_RXINT       (1 << 4)
#define MANTIS_UART_RXFLUSH     (1 << 2)

/* GPIF bits */
#define MANTIS_GPIF_HIFRDWRN    BIT(31)

/* I2C bits */
#define MANTIS_I2C_RATE_3       (0x02 << 6)
#define MANTIS_I2C_PGMODE       BIT(3)
#define MANTIS_I2C_STOP         BIT(5)

/* Enums */
enum mantis_slot_state {
    MODULE_INSERTED = 3,
    MODULE_XTRACTED = 4,
};

enum mantis_sbuf_status {
    MANTIS_SBUF_DATA_AVAIL = 1,
    MANTIS_SBUF_DATA_EMPTY = 2,
    MANTIS_SBUF_DATA_OVFLW = 3,
};

enum mantis_baud {
    MANTIS_BAUD_9600  = 0,
    MANTIS_BAUD_19200,
    MANTIS_BAUD_38400,
    MANTIS_BAUD_57600,
    MANTIS_BAUD_115200,
};

enum mantis_parity {
    MANTIS_PARITY_NONE = 0,
    MANTIS_PARITY_EVEN,
    MANTIS_PARITY_ODD,
};

enum mantis_i2c_mode {
    MANTIS_PAGE_MODE = 0,
    MANTIS_BYTE_MODE,
};

enum mantis_power {
    POWER_OFF = 0,
    POWER_ON  = 1,
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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    struct {
        uint32_t int_stat;
        uint32_t int_mask;
        uint32_t dma_ctl;
        uint32_t risc_start;
        uint32_t i2cdata_ctl;
        uint32_t control;
        uint32_t gpif_status;
        uint32_t gpif_addr;
        uint32_t gpif_dout;
        uint32_t uart_ctl;
        uint32_t uart_baud;
    } regs;

    /* DMA Context */
    

    /* Operational status flags */
    

    /* State used to handle reset sequences */
    

    /* Power management state */
    uint32_t power_state;

    
};



static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->regs.int_stat & s->regs.int_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    switch (addr) {
    case MANTIS_INT_STAT:
        val = s->regs.int_stat;
        break;
    case MANTIS_INT_MASK:
        val = s->regs.int_mask;
        break;
    case MANTIS_DMA_CTL:
        val = s->regs.dma_ctl;
        break;
    case MANTIS_RISC_START:
        val = s->regs.risc_start;
        break;
    case MANTIS_I2CDATA_CTL:
        val = s->regs.i2cdata_ctl;
        break;
    case MANTIS_CONTROL:
        val = s->regs.control;
        break;
    case MANTIS_GPIF_STATUS:
        val = s->regs.gpif_status;
        break;
    case MANTIS_GPIF_ADDR:
        val = s->regs.gpif_addr;
        break;
    case MANTIS_GPIF_DOUT:
        val = s->regs.gpif_dout;
        break;
    case MANTIS_UART_CTL:
        val = s->regs.uart_ctl;
        break;
    case MANTIS_UART_BAUD:
        val = s->regs.uart_baud;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case MANTIS_INT_STAT:
        /* W1C: clear bits set in val */
        s->regs.int_stat &= ~val;
        pcibase_update_irq(s);
        break;
    case MANTIS_INT_MASK:
        s->regs.int_mask = val;
        pcibase_update_irq(s);
        break;
    case MANTIS_DMA_CTL:
        s->regs.dma_ctl = val;
        break;
    case MANTIS_RISC_START:
        s->regs.risc_start = val;
        break;
    case MANTIS_I2CDATA_CTL:
        s->regs.i2cdata_ctl = val;
        break;
    case MANTIS_CONTROL:
        s->regs.control = val;
        break;
    case MANTIS_GPIF_STATUS:
        /* W1C */
        s->regs.gpif_status &= ~val;
        break;
    case MANTIS_GPIF_ADDR:
        s->regs.gpif_addr = val;
        break;
    case MANTIS_GPIF_DOUT:
        s->regs.gpif_dout = val;
        break;
    case MANTIS_UART_CTL:
        s->regs.uart_ctl = val;
        break;
    case MANTIS_UART_BAUD:
        s->regs.uart_baud = val;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->regs, 0, sizeof(s->regs));
    pcibase_update_irq(s);
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "mantis-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    memset(&s->regs, 0, sizeof(s->regs));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    
}

static const VMStateDescription vmstate_pcibase = {
    .name = "Mantis_pci",
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
