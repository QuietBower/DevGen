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

#define TYPE_PCIBASE_DEVICE "cb_pcimdas_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs */
#define PCI_VENDOR_ID_CB 0x1307
#define PCI_DEVICE_ID_PCIMDAS 0x0056
#define PCI_CLASS_ID 0x00ff

/* Register offsets and masks from driver */
#define PCIMDAS_AI_REG			0x00
#define PCIMDAS_AI_SOFTTRIG_REG		0x00
#define PCIMDAS_AO_REG(x)		(0x02 + ((x) * 2))
#define PCIMDAS_MUX_REG			0x00
#define PCIMDAS_MUX(_lo, _hi)		((_lo) | ((_hi) << 4))
#define PCIMDAS_DI_DO_REG		0x01
#define PCIMDAS_STATUS_REG		0x02
#define PCIMDAS_STATUS_EOC		BIT(7)
#define PCIMDAS_STATUS_UB		BIT(6)
#define PCIMDAS_STATUS_MUX		BIT(5)
#define PCIMDAS_STATUS_CLK		BIT(4)
#define PCIMDAS_STATUS_TO_CURR_MUX(x)	((x) & 0xf)
#define PCIMDAS_CONV_STATUS_REG		0x03
#define PCIMDAS_CONV_STATUS_EOC		BIT(7)
#define PCIMDAS_CONV_STATUS_EOB		BIT(6)
#define PCIMDAS_CONV_STATUS_EOA		BIT(5)
#define PCIMDAS_CONV_STATUS_FNE		BIT(4)
#define PCIMDAS_CONV_STATUS_FHF		BIT(3)
#define PCIMDAS_CONV_STATUS_OVERRUN	BIT(2)
#define PCIMDAS_IRQ_REG			0x04
#define PCIMDAS_IRQ_INTE		BIT(7)
#define PCIMDAS_IRQ_INT			BIT(6)
#define PCIMDAS_IRQ_OVERRUN		BIT(4)
#define PCIMDAS_IRQ_EOA			BIT(3)
#define PCIMDAS_IRQ_EOA_INT_SEL		BIT(2)
#define PCIMDAS_IRQ_INTSEL(x)		((x) << 0)
#define PCIMDAS_IRQ_INTSEL_EOC		PCIMDAS_IRQ_INTSEL(0)
#define PCIMDAS_IRQ_INTSEL_FNE		PCIMDAS_IRQ_INTSEL(1)
#define PCIMDAS_IRQ_INTSEL_EOB		PCIMDAS_IRQ_INTSEL(2)
#define PCIMDAS_IRQ_INTSEL_FHF_EOA	PCIMDAS_IRQ_INTSEL(3)
#define PCIMDAS_PACER_REG		0x05
#define PCIMDAS_PACER_GATE_STATUS	BIT(6)
#define PCIMDAS_PACER_GATE_POL		BIT(5)
#define PCIMDAS_PACER_GATE_LATCH	BIT(4)
#define PCIMDAS_PACER_GATE_EN		BIT(3)
#define PCIMDAS_PACER_EXT_PACER_POL	BIT(2)
#define PCIMDAS_PACER_SRC(x)		((x) << 0)
#define PCIMDAS_PACER_SRC_POLLED	PCIMDAS_PACER_SRC(0)
#define PCIMDAS_PACER_SRC_EXT		PCIMDAS_PACER_SRC(2)
#define PCIMDAS_PACER_SRC_INT		PCIMDAS_PACER_SRC(3)
#define PCIMDAS_PACER_SRC_MASK		(3 << 0)
#define PCIMDAS_BURST_REG		0x06
#define PCIMDAS_BURST_BME		BIT(1)
#define PCIMDAS_BURST_CONV_EN		BIT(0)
#define PCIMDAS_GAIN_REG		0x07
#define PCIMDAS_8254_BASE		0x08
#define PCIMDAS_USER_CNTR_REG		0x0c
#define PCIMDAS_USER_CNTR_CTR1_CLK_SEL	BIT(0)
#define PCIMDAS_RESIDUE_MSB_REG		0x0d
#define PCIMDAS_RESIDUE_LSB_REG		0x0e
#define PCIMDAS_8255_BASE		0x00

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
    uint8_t irq_reg;       /* IRQ register at offset 0x04 */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t ai_data;
    uint16_t ao[2];
    uint8_t badr3_regs[16];     /* 8254/8255 I/O region (BAR3) */
    uint8_t iobase_regs[16];    /* Additional I/O region (BAR4) */
    uint8_t last_mux;           /* Store last MUX value for AI trigger */
};

/* DAQIO PIO handlers (BAR2) */
static uint64_t pcibase_pio_read_daqio(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 2) {
        return ~0ULL;
    }
    switch (addr) {
    case 0x00:
        val = s->ai_data;
        break;
    case 0x02:
        val = s->ao[0] & 0xFFF;
        break;
    case 0x04:
        val = s->ao[1] & 0xFFF;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_pio_write_daqio(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 2) {
        return;
    }
    switch (addr) {
    case 0x00:
        /* Soft trigger: start conversion, set EOC and update data */
        s->badr3_regs[PCIMDAS_STATUS_REG] |= PCIMDAS_STATUS_EOC;
        /* Produce arbitrary data based on last_mux */
        s->ai_data = s->last_mux << 8;
        break;
    case 0x02:
        s->ao[0] = val & 0xFFF;
        break;
    case 0x04:
        s->ao[1] = val & 0xFFF;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops_daqio = {
    .read = pcibase_pio_read_daqio,
    .write = pcibase_pio_write_daqio,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 2, .max_access_size = 2 },
    .impl  = { .min_access_size = 2, .max_access_size = 2 },
};

/* BADDR3 PIO handlers (BAR3) */
static uint64_t pcibase_pio_read_badr3(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (size != 1) {
        return val;
    }
    switch (addr) {
    case 0x00:
        val = s->badr3_regs[0x00];
        break;
    case 0x01:
        val = s->badr3_regs[0x01] & 0x0f; /* DI_DO_REG lower nibble */
        break;
    case 0x02:
        val = s->badr3_regs[0x02];
        break;
    case 0x03:
        val = 0; /* CONV_STATUS_REG, not used */
        break;
    case 0x04:
        val = s->badr3_regs[0x04];
        break;
    case 0x05:
        val = s->badr3_regs[0x05];
        break;
    case 0x06:
        val = s->badr3_regs[0x06];
        break;
    case 0x07:
        val = s->badr3_regs[0x07];
        break;
    case 0x08:
    case 0x09:
    case 0x0a:
    case 0x0b:
        val = 0; /* 8254 dummy */
        break;
    case 0x0c:
        val = s->badr3_regs[0x0c];
        break;
    case 0x0d:
        val = 0;
        break;
    case 0x0e:
        val = 0;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_pio_write_badr3(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }
    switch (addr) {
    case 0x00:
        s->badr3_regs[0x00] = (uint8_t)val;
        s->last_mux = (uint8_t)val;
        break;
    case 0x01:
        s->badr3_regs[0x01] = (uint8_t)val;
        break;
    case 0x02:
        /* STATUS_REG is read-only, ignore writes */
        break;
    case 0x03:
        break;
    case 0x04:
        s->badr3_regs[0x04] = (uint8_t)val;
        break;
    case 0x05:
        s->badr3_regs[0x05] = (uint8_t)val;
        break;
    case 0x06:
        s->badr3_regs[0x06] = (uint8_t)val;
        break;
    case 0x07:
        s->badr3_regs[0x07] = (uint8_t)val;
        break;
    case 0x08:
    case 0x09:
    case 0x0a:
    case 0x0b:
        /* 8254 dummy, ignore */
        break;
    case 0x0c:
        s->badr3_regs[0x0c] = (uint8_t)val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops_badr3 = {
    .read = pcibase_pio_read_badr3,
    .write = pcibase_pio_write_badr3,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* IOBASE PIO handlers (BAR4) - 8255 emulation */
static uint64_t pcibase_pio_read_iobase(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (size != 1) {
        return val;
    }
    if (addr < 4) {
        val = s->iobase_regs[addr];
    } else {
        val = 0;
    }
    return val;
}

static void pcibase_pio_write_iobase(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }
    if (addr < 4) {
        s->iobase_regs[addr] = (uint8_t)val;
    }
}

static const MemoryRegionOps pcibase_pio_ops_iobase = {
    .read = pcibase_pio_read_iobase,
    .write = pcibase_pio_write_iobase,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->ai_data = 0;
    s->ao[0] = s->ao[1] = 0;
    s->last_mux = 0;
    memset(s->badr3_regs, 0, sizeof(s->badr3_regs));
    memset(s->iobase_regs, 0, sizeof(s->iobase_regs));
    /* Set default status bits: bipolar, differential, 1 MHz clock, EOC low */
    s->badr3_regs[PCIMDAS_STATUS_REG] = 0;
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
        memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        const MemoryRegionOps *ops = NULL;
        switch (bi->index) {
        case 2:
            ops = &pcibase_pio_ops_daqio;
            break;
        case 3:
            ops = &pcibase_pio_ops_badr3;
            break;
        case 4:
            ops = &pcibase_pio_ops_iobase;
            break;
        default:
            g_assert_not_reached();
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CB );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PCIMDAS );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 16, .name = "daqio" };
    s->bar_info[1] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 16, .name = "badr3" };
    s->bar_info[2] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 16, .name = "iobase" };
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
    .name = "cb_pcimdas_pci",
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
