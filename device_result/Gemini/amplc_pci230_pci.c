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
#include "qemu/bitops.h"
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

#define TYPE_PCIBASE_DEVICE "amplc_pci230_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_AMPLICON 0x14dc
#define PCI_DEVICE_ID_PCI230 0x0000
#define PCI_DEVICE_ID_PCI260 0x0006

#define PCI230_PPI_X_BASE	0x00
#define PCI230_PPI_X_A		0x00
#define PCI230_PPI_X_B		0x01
#define PCI230_PPI_X_C		0x02
#define PCI230_PPI_X_CMD	0x03
#define PCI230_Z2_CT_BASE	0x14
#define PCI230_ZCLK_SCE		0x1A
#define PCI230_ZGAT_SCE		0x1D
#define PCI230_INT_SCE		0x1E
#define PCI230_INT_STAT		0x1E

#define PCI230_DACCON		0x00
#define PCI230_DACOUT1		0x02
#define PCI230_DACOUT2		0x04
#define PCI230_ADCDATA		0x08
#define PCI230_ADCSWTRIG	0x08
#define PCI230_ADCCON		0x0A
#define PCI230_ADCEN		0x0C
#define PCI230_ADCG		0x0E
#define PCI230P_ADCTRIG		0x10
#define PCI230P_ADCTH		0x12
#define PCI230P_ADCFFTH		0x14
#define PCI230P_ADCFFLEV	0x16
#define PCI230P_ADCPTSC		0x18
#define PCI230P_ADCHYST		0x1A
#define PCI230P_EXTFUNC		0x1C
#define PCI230P_HWVER		0x1E

#define PCI230P2_DACDATA	0x02
#define PCI230P2_DACSWTRIG	0x02
#define PCI230P2_DACEN		0x06

#define PCI230_ADC_TRIG(x)		(((x) & 0x7) << 0)
#define PCI230_ADC_IM(x)		(((x) & 0x1) << 4)
#define PCI230_ADC_IR(x)		(((x) & 0x1) << 3)
#define PCI230P2_DAC_INT_FIFO(x)	(((x) & 7) << 9)
#define PCI230P2_DAC_TRIG(x)		(((x) & 0x7) << 2)
#define PCI230_DAC_OR(x)		(((x) & 0x1) << 0)
#define PCI230_ADC_INT_FIFO(x)		(((x) & 0x7) << 9)
#define PCI230P2_DAC_FIFOLEVEL_FULL	1024
#define PCI230P2_DAC_FIFOLEVEL_HALF	512

#define PCI230_DAC_OR_MASK		PCI230_DAC_OR(1)
#define PCI230P_EXTFUNC_GAT_EXTTRIG	BIT(0)
#define PCI230P2_EXTFUNC_DACFIFO	BIT(1)
#define PCI230P2_DAC_FIFO_EN		BIT(8)
#define PCI230P2_DAC_FIFO_RESET		BIT(12)
#define PCI230_ADC_TRIG_NONE		PCI230_ADC_TRIG(0)
#define PCI230_ADC_IM_SE		PCI230_ADC_IM(0)
#define PCI230_ADC_IR_BIP		PCI230_ADC_IR(1)
#define PCI230_ADC_FIFO_RESET		BIT(12)

#define PCI230_ADC_FIFO_EMPTY		BIT(12)
#define PCI230_ADC_TRIG_Z2CT2		PCI230_ADC_TRIG(6)
#define PCI230_ADC_FIFO_EN		BIT(8)
#define PCI230_ADC_IM_DIF		PCI230_ADC_IM(1)
#define PCI230_ADC_IR_UNI		PCI230_ADC_IR(0)
#define PCI230_INT_ZCLK_CT1		BIT(5)
#define PCI230P2_INT_DAC		BIT(4)
#define PCI230P2_DAC_FIFO_UNDERRUN_CLEAR	BIT(5)
#define PCI230P2_DAC_FIFO_UNDERRUN_LATCHED	BIT(5)
#define PCI230P2_DAC_FIFO_HALF		BIT(15)
#define PCI230P2_DAC_FIFO_FULL		BIT(14)
#define PCI230P2_DAC_FIFO_EMPTY		BIT(13)
#define PCI230P2_DAC_FIFOROOM_FULL		0
#define PCI230P2_DAC_FIFOROOM_HALFTOFULL	1
#define PCI230P2_DAC_FIFOROOM_EMPTY		PCI230P2_DAC_FIFOLEVEL_FULL
#define PCI230P2_DAC_FIFOROOM_ONETOHALF		\
	(PCI230P2_DAC_FIFOLEVEL_FULL - PCI230P2_DAC_FIFOLEVEL_HALF)
#define PCI230P2_DAC_INT_FIFO_MASK	PCI230P2_DAC_INT_FIFO(7)
#define PCI230P2_DAC_INT_FIFO_EMPTY	PCI230P2_DAC_INT_FIFO(0)
#define PCI230P2_DAC_TRIG_Z2CT1		PCI230P2_DAC_TRIG(5)
#define PCI230P2_DAC_TRIG_EXTP		PCI230P2_DAC_TRIG(2)
#define PCI230P2_DAC_TRIG_EXTN		PCI230P2_DAC_TRIG(3)
#define PCI230P2_DAC_TRIG_SW		PCI230P2_DAC_TRIG(1)
#define PCI230P2_DAC_TRIG_NONE		PCI230P2_DAC_TRIG(0)
#define PCI230P2_DAC_TRIG_MASK		PCI230P2_DAC_TRIG(7)
#define PCI230_INT_ADC			BIT(2)
#define PCI230_DAC_OR_BIP		PCI230_DAC_OR(1)
#define PCI230_DAC_OR_UNI		PCI230_DAC_OR(0)
#define PCI230P2_DAC_INT_FIFO_NHALF	PCI230P2_DAC_INT_FIFO(2)
#define PCI230_ADC_FIFOLEVEL_HALFFULL	2049
#define PCI230_ADC_INT_FIFO_HALF	PCI230_ADC_INT_FIFO(3)
#define PCI230P_ADC_INT_FIFO_THRESH	PCI230_ADC_INT_FIFO(7)
#define PCI230_ADC_INT_FIFO_NEMPTY	PCI230_ADC_INT_FIFO(1)
#define PCI230_ADC_INT_FIFO_MASK	PCI230_ADC_INT_FIFO(7)
#define PCI230_ADC_TRIG_EXTP		PCI230_ADC_TRIG(2)
#define PCI230_ADC_TRIG_EXTN		PCI230_ADC_TRIG(3)
#define PCI230_ADC_TRIG_MASK		PCI230_ADC_TRIG(7)
#define PCI230_ADC_FIFO_FULL_LATCHED	BIT(5)
#define PCI230_ADC_FIFO_HALF		BIT(14)
#define PCI230_ADC_IR_MASK		PCI230_ADC_IR(1)
#define PCI230_ADC_IM_MASK		PCI230_ADC_IM(1)
#define PCI230_INT_DISABLE		0

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

    uint16_t reg_adccon;
    uint16_t reg_daccon;
    uint16_t reg_adcfifothresh;
    uint16_t reg_adcg;
    uint8_t  reg_ier;
    uint16_t reg_extfunc;
    uint16_t reg_hwver;

    uint8_t ai_bipolar;
    uint8_t ao_bipolar;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intr_status & s->reg_ier) != 0;
    pci_set_irq(pdev, level ? 1 : 0);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_iobase_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
        case PCI230_INT_STAT:
            val = s->intr_status;
            break;
        default:
            break;
    }
    return val;
}

static void pcibase_iobase_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
        case PCI230_INT_SCE:
            s->reg_ier = val;
            pcibase_update_irq(s);
            break;
        default:
            break;
    }
}

static uint64_t pcibase_daqio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
        case PCI230_DACCON:
            val = s->reg_daccon;
            break;
        case PCI230_ADCDATA:
            val = 0x8000; /* Dummy ADC data */
            break;
        case PCI230_ADCCON:
            val = s->reg_adccon;
            break;
        case PCI230P_ADCFFLEV:
            val = 0;
            break;
        case PCI230P_HWVER:
            val = s->reg_hwver;
            break;
        default:
            break;
    }
    return val;
}

static void pcibase_daqio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
        case PCI230_DACCON:
            s->reg_daccon = val;
            break;
        case PCI230_ADCCON:
            s->reg_adccon = val;
            break;
        case PCI230_ADCG:
            s->reg_adcg = val;
            break;
        case PCI230P_ADCFFTH:
            s->reg_adcfifothresh = val;
            break;
        case PCI230P_EXTFUNC:
            s->reg_extfunc = val;
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

static const MemoryRegionOps pcibase_iobase_ops = {
    .read = pcibase_iobase_read,
    .write = pcibase_iobase_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_daqio_ops = {
    .read = pcibase_daqio_read,
    .write = pcibase_daqio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->reg_hwver = 2;
    s->reg_adccon = PCI230_ADC_TRIG_NONE | PCI230_ADC_IM_SE | PCI230_ADC_IR_BIP;
    s->reg_daccon = 0;
    s->reg_adcg = 0;
    s->reg_ier = 0;
    s->intr_status = 0;
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
        const MemoryRegionOps *ops = (bi->index == 2) ? &pcibase_iobase_ops : &pcibase_daqio_ops;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMPLICON );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PCI230 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 4;
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 32, .name = "pci230-iobase" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 32, .name = "pci230-daqio" };
      
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

static const VMStateDescription vmstate_pcibase = {
    .name = "amplc_pci230_pci",
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
