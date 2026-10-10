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

#define TYPE_PCIBASE_DEVICE "cb_pcidda_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID  0x1307
#define DEVICE_ID  0x0020
#define CLASS_ID   0x00000000  /* Not specified in driver; using 0 */

#define EEPROM_SIZE	128
#define MAX_AO_CHANNELS 8
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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t dac_cal1_bits;  /* shadow of DACALIBRATION1 register */

    /* DMA Context */

    /* Operational status flags */

    /* State used to handle reset sequences */

    /* Power management state (D0-D3) */
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No MMIO used by driver */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO used by driver */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Generic PIO handler not used; BARs have dedicated handlers */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Generic PIO handler not used; BARs have dedicated handlers */
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

/* BAR 3 analog output PIO handlers */
static uint64_t pcibase_ao_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CB_DDA_DA_CTRL_REG:
        /* control register not read by driver */
        break;
    case DACALIBRATION1:
        /* return dac_cal1_bits but with SERIAL_OUT_BIT = 0 */
        val = s->dac_cal1_bits & ~SERIAL_OUT_BIT;
        break;
    case DACALIBRATION2:
        /* not read by driver */
        break;
    default:
        if (addr >= CB_DDA_DA_DATA_REG(0) && addr < CB_DDA_DA_DATA_REG(MAX_AO_CHANNELS)) {
            /* data registers not read */
        }
        break;
    }
    return val;
}

static void pcibase_ao_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CB_DDA_DA_CTRL_REG:
        /* store control word if needed; driver writes but never reads back */
        break;
    case DACALIBRATION1:
        s->dac_cal1_bits = val & 0xFFFF;
        break;
    case DACALIBRATION2:
        /* calibration latch; driver writes but never reads */
        break;
    default:
        if (addr >= CB_DDA_DA_DATA_REG(0) && addr < CB_DDA_DA_DATA_REG(MAX_AO_CHANNELS)) {
            /* analog output data writes; driver writes but never reads */
        }
        break;
    }
}

static const MemoryRegionOps pcibase_ao_pio_ops = {
    .read = pcibase_ao_pio_read,
    .write = pcibase_ao_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

/* BAR 2 digital I/O (8255) PIO handlers */
static uint64_t pcibase_dio_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* Dummy read for 8255 subdevices */
    return 0;
}

static void pcibase_dio_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* Dummy write for 8255 subdevices */
}

static const MemoryRegionOps pcibase_dio_pio_ops = {
    .read = pcibase_dio_pio_read,
    .write = pcibase_dio_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp, const MemoryRegionOps *ops)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
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

    /* BAR 2: 8255 DIO */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 8;
    s->bar_info[2].name = "cb_pcidda_dio";
    pcibase_register_bar(pdev, s, &s->bar_info[2], errp, &pcibase_dio_pio_ops);

    /* BAR 3: Analog output and calibration */
    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = 256;
    s->bar_info[3].name = "cb_pcidda_ao";
    pcibase_register_bar(pdev, s, &s->bar_info[3], errp, &pcibase_ao_pio_ops);

    s->num_bars = 2;

    /* Final state initialization before the device is 'live' */
    s->dac_cal1_bits = 0;
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
    .name = "cb_pcidda_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset register shadows to power-on defaults */
    s->dac_cal1_bits = 0;
}

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