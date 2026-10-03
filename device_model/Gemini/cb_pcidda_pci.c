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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "cb_pcidda_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef PCI_VENDOR_ID_CB
#define PCI_VENDOR_ID_CB 0x1307
#endif

#define EEPROM_SIZE	128
#define MAX_AO_CHANNELS 8
#define I8255_SIZE		0x04

#define CB_DDA_DIO0_8255_BASE		0x00
#define CB_DDA_DIO1_8255_BASE		0x04
#define CB_DDA_DA_CTRL_REG		0x00
#define CB_DDA_DA_CTRL_SU		BIT(0)
#define CB_DDA_DA_CTRL_EN		BIT(1)
#define CB_DDA_DA_CTRL_DAC(x)		((x) << 2)
#define CB_DDA_DA_CTRL_RANGE2V5		(0 << 6)
#define CB_DDA_DA_CTRL_RANGE5V		(2 << 6)
#define CB_DDA_DA_CTRL_RANGE10V		(3 << 6)
#define CB_DDA_DA_CTRL_UNIP		BIT(8)

#define DACALIBRATION1	4
#define SERIAL_IN_BIT   0x1
#define CAL_CHANNEL_MASK	(0x7 << 1)
#define CAL_CHANNEL_BITS(channel)	(((channel) << 1) & CAL_CHANNEL_MASK)
#define CAL_COUNTER_MASK	0x1f
#define CAL_COUNTER_OVERFLOW_BIT        0x20
#define AO_BELOW_REF_BIT        0x40
#define SERIAL_OUT_BIT	0x80

#define DACALIBRATION2	6
#define SELECT_EEPROM_BIT	0x1
#define DESELECT_REF_DAC_BIT    0x2
#define DESELECT_CALDAC_BIT(n)  (0x4 << (n))
#define DUMMY_BIT       0x40

#define CB_DDA_DA_DATA_REG(x)		(0x08 + ((x) * 2))

#define CB_DDA_CALDAC_FINE_GAIN		0
#define CB_DDA_CALDAC_COURSE_GAIN	1
#define CB_DDA_CALDAC_COURSE_OFFSET	2
#define CB_DDA_CALDAC_FINE_OFFSET	3

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Hardware Register Shadows */
    uint16_t da_ctrl_reg;
    uint16_t dacalibration1;
    uint16_t dacalibration2;
    uint16_t da_data_reg[MAX_AO_CHANNELS];
    uint16_t eeprom_data[EEPROM_SIZE];

    /* Added for behavior */
    uint8_t iobase_regs[0x10];
    uint32_t serial_in_shift_reg;
    uint32_t serial_in_bit_count;
    uint16_t serial_out_shift_reg;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* No IRQ logic visible in driver */
}

static uint64_t iobase_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 0x10) {
        return s->iobase_regs[addr];
    }
    return 0;
}

static void iobase_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 0x10) {
        s->iobase_regs[addr] = val;
    }
}

static const MemoryRegionOps iobase_ops = {
    .read = iobase_read,
    .write = iobase_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static uint64_t daqio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
        case CB_DDA_DA_CTRL_REG:
            return s->da_ctrl_reg;
        case DACALIBRATION1:
            if (s->dacalibration2 & SELECT_EEPROM_BIT) {
                uint16_t ret = 0;
                if (s->serial_out_shift_reg & 0x8000) {
                    ret |= SERIAL_OUT_BIT;
                }
                s->serial_out_shift_reg <<= 1;
                return ret;
            }
            return s->dacalibration1;
        case DACALIBRATION2:
            return s->dacalibration2;
        default:
            if (addr >= 0x08 && addr < 0x08 + (MAX_AO_CHANNELS * 2)) {
                return s->da_data_reg[(addr - 0x08) / 2];
            }
            return 0;
    }
}

static void daqio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
        case CB_DDA_DA_CTRL_REG:
            s->da_ctrl_reg = val;
            break;
        case DACALIBRATION1:
            s->dacalibration1 = val;
            if (s->dacalibration2 & SELECT_EEPROM_BIT) {
                s->serial_in_shift_reg = (s->serial_in_shift_reg << 1) | (val & SERIAL_IN_BIT);
                s->serial_in_bit_count++;
                if (s->serial_in_bit_count == 11) {
                    uint8_t eeprom_addr = s->serial_in_shift_reg & 0xFF;
                    if (eeprom_addr < EEPROM_SIZE) {
                        s->serial_out_shift_reg = s->eeprom_data[eeprom_addr];
                    } else {
                        s->serial_out_shift_reg = 0;
                    }
                }
            }
            break;
        case DACALIBRATION2:
            if ((val & SELECT_EEPROM_BIT) && !(s->dacalibration2 & SELECT_EEPROM_BIT)) {
                s->serial_in_shift_reg = 0;
                s->serial_in_bit_count = 0;
            }
            s->dacalibration2 = val;
            break;
        default:
            if (addr >= 0x08 && addr < 0x08 + (MAX_AO_CHANNELS * 2)) {
                s->da_data_reg[(addr - 0x08) / 2] = val;
            }
            break;
    }
}

static const MemoryRegionOps daqio_ops = {
    .read = daqio_read,
    .write = daqio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->da_ctrl_reg = 0;
    s->dacalibration1 = 0;
    s->dacalibration2 = 0;
    memset(s->da_data_reg, 0, sizeof(s->da_data_reg));
    memset(s->iobase_regs, 0, sizeof(s->iobase_regs));
    
    s->serial_in_shift_reg = 0;
    s->serial_in_bit_count = 0;
    s->serial_out_shift_reg = 0;

    for (int i = 0; i < EEPROM_SIZE; i++) {
        s->eeprom_data[i] = 0x8000; /* Mid-scale dummy calibration data */
    }
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        if (bi->index == 2) {
            memory_region_init_io(mr, OBJECT(s), &iobase_ops, s, bi->name, aligned_size);
        } else if (bi->index == 3) {
            memory_region_init_io(mr, OBJECT(s), &daqio_ops, s, bi->name, aligned_size);
        }
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0020 );
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
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_NONE, 0, NULL};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_NONE, 0, NULL};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_PIO, 0x10, "iobase"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_PIO, 0x20, "daqio"};
      
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
    .name = "cb_pcidda_pci",
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
