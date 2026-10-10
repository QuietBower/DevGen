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

#define TYPE_PCIBASE_DEVICE "cb_pcidas_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

#define PCI_VENDOR_ID_CB		0x1307

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

enum cb_pcidas_boardid {
	BOARD_PCIDAS1602_16,
	BOARD_PCIDAS1200,
	BOARD_PCIDAS1602_12,
	BOARD_PCIDAS1200_JR,
	BOARD_PCIDAS1602_16_JR,
	BOARD_PCIDAS1000,
	BOARD_PCIDAS1001,
	BOARD_PCIDAS1002,
};

struct cb_pcidas_board {
	const char *name;
	int ai_speed;
	int ao_scan_speed;
	int fifo_size;
	unsigned int is_16bit;
	unsigned int use_alt_range:1;
	unsigned int has_ao:1;
	unsigned int has_ao_fifo:1;
	unsigned int has_ad8402:1;
	unsigned int has_dac08:1;
	unsigned int is_1602:1;
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t amcc_intcsr;
    uint32_t ctrl;
    uint32_t ao_ctrl;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t ai_buffer[1024];
    uint16_t ao_buffer[1024];
    uint32_t calib_src;

    struct cb_pcidas_board board;
};

#define AI_BUFFER_SIZE		1024
#define AO_BUFFER_SIZE		1024
#define PCIDAS_CTRL_REG		0x00
#define PCIDAS_CTRL_INT(x)	(((x) & 0x3) << 0)
#define PCIDAS_CTRL_INT_NONE	PCIDAS_CTRL_INT(0)
#define PCIDAS_CTRL_INT_EOS	PCIDAS_CTRL_INT(1)
#define PCIDAS_CTRL_INT_FHF	PCIDAS_CTRL_INT(2)
#define PCIDAS_CTRL_INT_FNE	PCIDAS_CTRL_INT(3)
#define PCIDAS_CTRL_INT_MASK	PCIDAS_CTRL_INT(3)
#define PCIDAS_CTRL_INTE	BIT(2)
#define PCIDAS_CTRL_DAHFIE	BIT(3)
#define PCIDAS_CTRL_EOAIE	BIT(4)
#define PCIDAS_CTRL_DAHFI	BIT(5)
#define PCIDAS_CTRL_EOAI	BIT(6)
#define PCIDAS_CTRL_INT_CLR	BIT(7)
#define PCIDAS_CTRL_EOBI	BIT(9)
#define PCIDAS_CTRL_ADHFI	BIT(10)
#define PCIDAS_CTRL_ADNEI	BIT(11)
#define PCIDAS_CTRL_ADNE	BIT(12)
#define PCIDAS_CTRL_DAEMIE	BIT(12)
#define PCIDAS_CTRL_LADFUL	BIT(13)
#define PCIDAS_CTRL_DAEMI	BIT(14)
#define PCIDAS_CTRL_AI_INT	(PCIDAS_CTRL_EOAI | PCIDAS_CTRL_EOBI |   \
				 PCIDAS_CTRL_ADHFI | PCIDAS_CTRL_ADNEI | \
				 PCIDAS_CTRL_LADFUL)
#define PCIDAS_CTRL_AO_INT	(PCIDAS_CTRL_DAHFI | PCIDAS_CTRL_DAEMI)
#define PCIDAS_AI_REG		0x02
#define PCIDAS_AI_FIRST(x)	((x) & 0xf)
#define PCIDAS_AI_LAST(x)	(((x) & 0xf) << 4)
#define PCIDAS_AI_CHAN(x)	(PCIDAS_AI_FIRST(x) | PCIDAS_AI_LAST(x))
#define PCIDAS_AI_GAIN(x)	(((x) & 0x3) << 8)
#define PCIDAS_AI_SE		BIT(10)
#define PCIDAS_AI_UNIP		BIT(11)
#define PCIDAS_AI_PACER(x)	(((x) & 0x3) << 12)
#define PCIDAS_AI_PACER_SW	PCIDAS_AI_PACER(0)
#define PCIDAS_AI_PACER_INT	PCIDAS_AI_PACER(1)
#define PCIDAS_AI_PACER_EXTN	PCIDAS_AI_PACER(2)
#define PCIDAS_AI_PACER_EXTP	PCIDAS_AI_PACER(3)
#define PCIDAS_AI_PACER_MASK	PCIDAS_AI_PACER(3)
#define PCIDAS_AI_EOC		BIT(14)
#define PCIDAS_TRIG_REG		0x04
#define PCIDAS_TRIG_SEL(x)	(((x) & 0x3) << 0)
#define PCIDAS_TRIG_SEL_NONE	PCIDAS_TRIG_SEL(0)
#define PCIDAS_TRIG_SEL_SW	PCIDAS_TRIG_SEL(1)
#define PCIDAS_TRIG_SEL_EXT	PCIDAS_TRIG_SEL(2)
#define PCIDAS_TRIG_SEL_ANALOG	PCIDAS_TRIG_SEL(3)
#define PCIDAS_TRIG_SEL_MASK	PCIDAS_TRIG_SEL(3)
#define PCIDAS_TRIG_POL		BIT(2)
#define PCIDAS_TRIG_MODE	BIT(3)
#define PCIDAS_TRIG_EN		BIT(4)
#define PCIDAS_TRIG_BURSTE	BIT(5)
#define PCIDAS_TRIG_CLR		BIT(7)
#define PCIDAS_CALIB_REG	0x06
#define PCIDAS_CALIB_8800_SEL	BIT(8)
#define PCIDAS_CALIB_TRIM_SEL	BIT(9)
#define PCIDAS_CALIB_DAC08_SEL	BIT(10)
#define PCIDAS_CALIB_SRC(x)	(((x) & 0x7) << 11)
#define PCIDAS_CALIB_EN		BIT(14)
#define PCIDAS_CALIB_DATA	BIT(15)
#define PCIDAS_AO_REG		0x08
#define PCIDAS_AO_EMPTY		BIT(0)
#define PCIDAS_AO_DACEN		BIT(1)
#define PCIDAS_AO_START		BIT(2)
#define PCIDAS_AO_PACER(x)	(((x) & 0x3) << 3)
#define PCIDAS_AO_PACER_SW	PCIDAS_AO_PACER(0)
#define PCIDAS_AO_PACER_INT	PCIDAS_AO_PACER(1)
#define PCIDAS_AO_PACER_EXTN	PCIDAS_AO_PACER(2)
#define PCIDAS_AO_PACER_EXTP	PCIDAS_AO_PACER(3)
#define PCIDAS_AO_PACER_MASK	PCIDAS_AO_PACER(3)
#define PCIDAS_AO_CHAN_EN(c)	BIT(5 + ((c) & 0x1))
#define PCIDAS_AO_CHAN_MASK	(PCIDAS_AO_CHAN_EN(0) | PCIDAS_AO_CHAN_EN(1))
#define PCIDAS_AO_UPDATE_BOTH	BIT(7)
#define PCIDAS_AO_RANGE(c, r)	(((r) & 0x3) << (8 + 2 * ((c) & 0x1)))
#define PCIDAS_AO_RANGE_MASK(c)	PCIDAS_AO_RANGE((c), 0x3)
#define PCIDAS_AI_DATA_REG	0x00
#define PCIDAS_AI_FIFO_CLR_REG	0x02
#define PCIDAS_AI_8254_BASE	0x00
#define PCIDAS_8255_BASE	0x04
#define PCIDAS_AO_8254_BASE	0x08
#define PCIDAS_AO_DATA_REG(x)	(0x00 + ((x) * 2))
#define PCIDAS_AO_FIFO_REG	0x00
#define PCIDAS_AO_FIFO_CLR_REG	0x02
#define MCSR_NV_BUSY	MCSR_NV_ENABLE
#define AMCC_OP_REG_MCSR_NVCMD  (AMCC_OP_REG_MCSR + 3)
#define AMCC_OP_REG_MCSR_NVDATA (AMCC_OP_REG_MCSR + 2)
#define MCSR_NV_READ	0x60
#define MCSR_NV_LOAD_LOW_ADDR	0x0
#define MCSR_NV_LOAD_HIGH_ADDR	0x20
#define MCSR_NV_ENABLE	0x80
#define INTCSR_INBOX_INTR_STATUS	0x20000
#define AMCC_OP_REG_IMB4         0x1c
#define AMCC_OP_REG_INTCSR       0x38
#define INTCSR_INTR_ASSERTED	0x800000
#define INTCSR_INBOX_FULL_INT	0x1000
#define INTCSR_INBOX_BYTE(x)	(((x) & 0x3) << 8)
#define INTCSR_INBOX_SELECT(x)	(((x) & 0x3) << 10)
#define AMCC_OP_REG_MCSR         0x3c

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = false;

    if (s->ctrl & (PCIDAS_CTRL_AI_INT | PCIDAS_CTRL_AO_INT)) {
        s->amcc_intcsr |= INTCSR_INTR_ASSERTED;
        irq_active = true;
    } else {
        s->amcc_intcsr &= ~INTCSR_INTR_ASSERTED;
    }

    pci_set_irq(pdev, irq_active ? 1 : 0);
}

static uint64_t pcibar1_read(void *opaque, hwaddr addr, unsigned size) {
    PCIBaseState *s = opaque;
    switch (addr) {
        case PCIDAS_CTRL_REG: {
            uint16_t ret = s->ctrl & ~PCIDAS_CTRL_DAEMIE;
            if (s->ai_buffer[0] > 0) {
                ret |= PCIDAS_CTRL_ADNE;
                s->ai_buffer[0]--;
            }
            return ret;
        }
        case PCIDAS_AI_REG: return PCIDAS_AI_EOC;
        case PCIDAS_AO_REG: return s->ao_ctrl | PCIDAS_AO_EMPTY;
    }
    return 0;
}

static void pcibar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
    PCIBaseState *s = opaque;
    switch (addr) {
        case PCIDAS_CTRL_REG:
            s->ctrl = val & ~(PCIDAS_CTRL_INT_CLR | PCIDAS_CTRL_DAEMI | PCIDAS_CTRL_DAHFI);
            if (val & PCIDAS_CTRL_INT_CLR) {
                s->ctrl &= ~(PCIDAS_CTRL_ADHFI | PCIDAS_CTRL_ADNEI | PCIDAS_CTRL_EOBI | PCIDAS_CTRL_LADFUL | PCIDAS_CTRL_EOAI);
                s->ai_buffer[0] = 0;
            }
            if (val & PCIDAS_CTRL_DAEMI) s->ctrl &= ~PCIDAS_CTRL_DAEMI;
            if (val & PCIDAS_CTRL_DAHFI) s->ctrl &= ~PCIDAS_CTRL_DAHFI;
            pcibase_update_irq(s);
            break;
        case PCIDAS_TRIG_REG:
            if (s->ctrl & PCIDAS_CTRL_INTE) {
                s->ai_buffer[0] = 4;
                s->ctrl |= PCIDAS_CTRL_ADNEI | PCIDAS_CTRL_ADHFI;
                pcibase_update_irq(s);
            }
            break;
        case PCIDAS_AO_REG:
            s->ao_ctrl = val;
            if (val & PCIDAS_AO_START) {
                s->ctrl |= PCIDAS_CTRL_DAEMI | PCIDAS_CTRL_DAHFI;
                pcibase_update_irq(s);
            }
            break;
    }
}

static const MemoryRegionOps pcibar1_ops = {
    .read = pcibar1_read, .write = pcibar1_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 }
};

static uint64_t pcibar2_read(void *opaque, hwaddr addr, unsigned size) {
    return 0x8000;
}

static void pcibar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
}

static const MemoryRegionOps pcibar2_ops = {
    .read = pcibar2_read, .write = pcibar2_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 }
};

static uint64_t pcibar4_read(void *opaque, hwaddr addr, unsigned size) {
    return 0;
}

static void pcibar4_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
}

static const MemoryRegionOps pcibar4_ops = {
    .read = pcibar4_read, .write = pcibar4_write, .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 }
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
        case AMCC_OP_REG_INTCSR:
            val = s->amcc_intcsr;
            break;
        case AMCC_OP_REG_IMB4:
            val = 0;
            break;
        case AMCC_OP_REG_MCSR_NVCMD:
            val = 0;
            break;
        case AMCC_OP_REG_MCSR_NVDATA:
            val = 0xAA;
            break;
    }
    
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
        case AMCC_OP_REG_INTCSR:
            s->amcc_intcsr = val & ~INTCSR_INBOX_INTR_STATUS;
            if (val & INTCSR_INBOX_INTR_STATUS) {
                s->amcc_intcsr &= ~INTCSR_INTR_ASSERTED;
            }
            pcibase_update_irq(s);
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

    s->ctrl = 0;
    s->ao_ctrl = 0;
    s->amcc_intcsr = 0;
    s->ai_buffer[0] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CB );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0001 );
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
    s->num_bars = 5;  
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 0x40, "amcc"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_NONE, 0, "pcibar1"};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_NONE, 0, "pcibar2"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_PIO, 0x10, "iobase"};
    s->bar_info[4] = (BARInfo){4, BAR_TYPE_NONE, 0, "pcibar4"};
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibar1_ops, s, "pcibar1", 0x80);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[1]);

    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibar2_ops, s, "pcibar2", 0x80);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[2]);

    memory_region_init_io(&s->bar_regions[4], OBJECT(s), &pcibar4_ops, s, "pcibar4", 0x80);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[4]);
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
    .name = "cb_pcidas_pci",
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
