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


#define TYPE_PCIBASE_DEVICE "me_daq_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_MEILHAUS
#define PCI_VENDOR_ID_MEILHAUS 0x1402
#endif

#define ME2600_FIRMWARE		"me2600_firmware.bin"
#define XILINX_DOWNLOAD_RESET	0x42
#define ME_CTRL1_REG			0x00
#define ME_CTRL1_INT_ENA		BIT(15)
#define ME_CTRL1_COUNTER_B_IRQ	BIT(12)
#define ME_CTRL1_COUNTER_A_IRQ	BIT(11)
#define ME_CTRL1_CHANLIST_READY_IRQ	BIT(10)
#define ME_CTRL1_EXT_IRQ		BIT(9)
#define ME_CTRL1_ADFIFO_HALFFULL_IRQ	BIT(8)
#define ME_CTRL1_SCAN_COUNT_ENA	BIT(5)
#define ME_CTRL1_SIMULTANEOUS_ENA	BIT(4)
#define ME_CTRL1_TRIGGER_FALLING_EDGE	BIT(3)
#define ME_CTRL1_CONTINUOUS_MODE	BIT(2)
#define ME_CTRL1_ADC_MODE(x)		(((x) & 0x3) << 0)
#define ME_CTRL1_ADC_MODE_DISABLE	ME_CTRL1_ADC_MODE(0)
#define ME_CTRL1_ADC_MODE_SOFT_TRIG	ME_CTRL1_ADC_MODE(1)
#define ME_CTRL1_ADC_MODE_SCAN_TRIG	ME_CTRL1_ADC_MODE(2)
#define ME_CTRL1_ADC_MODE_EXT_TRIG	ME_CTRL1_ADC_MODE(3)
#define ME_CTRL1_ADC_MODE_MASK	ME_CTRL1_ADC_MODE(3)
#define ME_CTRL2_REG			0x02
#define ME_CTRL2_ADFIFO_ENA		BIT(10)
#define ME_CTRL2_CHANLIST_ENA		BIT(9)
#define ME_CTRL2_PORT_B_ENA		BIT(7)
#define ME_CTRL2_PORT_A_ENA		BIT(6)
#define ME_CTRL2_COUNTER_B_ENA	BIT(4)
#define ME_CTRL2_COUNTER_A_ENA	BIT(3)
#define ME_CTRL2_DAC_ENA		BIT(1)
#define ME_CTRL2_BUFFERED_DAC		BIT(0)
#define ME_STATUS_REG			0x04
#define ME_STATUS_COUNTER_B_IRQ	BIT(12)
#define ME_STATUS_COUNTER_A_IRQ	BIT(11)
#define ME_STATUS_CHANLIST_READY_IRQ	BIT(10)
#define ME_STATUS_EXT_IRQ		BIT(9)
#define ME_STATUS_ADFIFO_HALFFULL_IRQ	BIT(8)
#define ME_STATUS_ADFIFO_FULL		BIT(4)
#define ME_STATUS_ADFIFO_HALFFULL	BIT(3)
#define ME_STATUS_ADFIFO_EMPTY	BIT(2)
#define ME_STATUS_CHANLIST_FULL	BIT(1)
#define ME_STATUS_FST_ACTIVE		BIT(0)
#define ME_DIO_PORT_A_REG		0x06
#define ME_DIO_PORT_B_REG		0x08
#define ME_TIMER_DATA_REG(x)		(0x0a + ((x) * 2))
#define ME_AI_FIFO_REG			0x10
#define ME_AI_FIFO_CHANLIST_DIFF	BIT(7)
#define ME_AI_FIFO_CHANLIST_UNIPOLAR	BIT(6)
#define ME_AI_FIFO_CHANLIST_GAIN(x)	(((x) & 0x3) << 4)
#define ME_AI_FIFO_CHANLIST_CHAN(x)	(((x) & 0xf) << 0)
#define ME_DAC_CTRL_REG			0x12
#define ME_DAC_CTRL_BIPOLAR(x)	BIT(7 - ((x) & 0x3))
#define ME_DAC_CTRL_GAIN(x)		BIT(11 - ((x) & 0x3))
#define ME_DAC_CTRL_MASK(x)		(ME_DAC_CTRL_BIPOLAR(x) | \
					 ME_DAC_CTRL_GAIN(x))
#define ME_AO_DATA_REG(x)		(0x14 + ((x) * 2))
#define ME_COUNTER_ENDDATA_REG(x)	(0x1c + ((x) * 2))
#define ME_COUNTER_STARTDATA_REG(x)	(0x20 + ((x) * 2))
#define ME_COUNTER_VALUE_REG(x)		(0x20 + ((x) * 2))
#define PLX9052_INTCSR_PCIENAB		BIT(6)
#define PLX9052_INTCSR			0x4c
#define PLX9052_INTCSR_LI2STAT		BIT(5)
#define PLX9052_INTCSR_LI1ENAB		BIT(0)
#define PLX9052_INTCSR_LI1POL		BIT(1)

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t ctrl1;
    uint16_t ctrl2;
    uint16_t dac_ctrl;
    uint32_t plx_intcsr;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ME_CTRL1_REG:
        val = s->ctrl1;
        break;
    case ME_CTRL2_REG:
        val = s->ctrl2;
        break;
    case ME_STATUS_REG:
        /* Return 0 to indicate ADFIFO is not empty (bit 2 is 0) */
        val = 0;
        break;
    case ME_DAC_CTRL_REG:
        val = s->dac_ctrl;
        break;
    case XILINX_DOWNLOAD_RESET:
        val = 0;
        break;
    case PLX9052_INTCSR:
        val = s->plx_intcsr;
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

    switch (addr) {
    case ME_CTRL1_REG:
        if (size == 1) {
            s->ctrl1 = (s->ctrl1 & 0xff00) | (val & 0xff);
        } else {
            s->ctrl1 = val;
        }
        break;
    case ME_CTRL2_REG:
        s->ctrl2 = val;
        break;
    case ME_STATUS_REG:
        /* w1c register, writing 0 does nothing */
        break;
    case ME_DAC_CTRL_REG:
        s->dac_ctrl = val;
        break;
    case PLX9052_INTCSR:
        s->plx_intcsr = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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

    s->ctrl1 = 0;
    s->ctrl2 = 0;
    s->dac_ctrl = 0;
    s->plx_intcsr = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE || bi->size == 0) {
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MEILHAUS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x2000 );
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
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x80, .name = "plx_regbase" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x100, .name = "mmio" };
      
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
    .name = "me_daq_pci",
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
