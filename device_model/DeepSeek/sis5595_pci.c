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

#define TYPE_PCIBASE_DEVICE "sis5595_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI Vendor and Device IDs */
#define PCI_VENDOR_ID_SI 0x1039
#define PCI_DEVICE_ID_SI_503 0x0008
/* Hardware register offsets from driver */
#define SIS5595_EXTENT 8
#define SIS5595_ENABLE_REG 0x7B
#define DIV_FROM_REG(val) (1 << (val))
#define SIS5595_BASE_REG 0x68
#define SIS5595_PIN_REG 0x7A
#define SIS5595_ADDR_REG_OFFSET 5
#define SIS5595_DATA_REG_OFFSET 6
#define SIS5595_REG_IN_MAX(nr) (0x2b + (nr) * 2)
#define SIS5595_REG_IN_MIN(nr) (0x2c + (nr) * 2)
#define SIS5595_REG_IN(nr) (0x20 + (nr))
#define SIS5595_REG_FAN_MIN(nr) (0x3b + (nr))
#define SIS5595_REG_FAN(nr) (0x28 + (nr))
#define REV2MIN 0xb0
#define SIS5595_REG_TEMP (((data->revision) >= REV2MIN) ? \
					SIS5595_REG_IN(4) : 0x27)
#define SIS5595_REG_TEMP_OVER (((data->revision) >= REV2MIN) ? \
					SIS5595_REG_IN_MAX(4) : 0x39)
#define SIS5595_REG_TEMP_HYST (((data->revision) >= REV2MIN) ? \
					SIS5595_REG_IN_MIN(4) : 0x3a)
#define SIS5595_REG_CONFIG 0x40
#define SIS5595_REG_ALARM1 0x41
#define SIS5595_REG_ALARM2 0x42
#define SIS5595_REG_FANDIV 0x47

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
    uint8_t index_reg;   /* index register at IO offset 5 */
    uint8_t data_reg;    /* data register at IO offset 6 */
    uint8_t regs[256];   /* internal registers accessed via index/data */
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;


    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{


}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == SIS5595_ADDR_REG_OFFSET) {
        val = s->index_reg;
    } else if (addr == SIS5595_DATA_REG_OFFSET) {
        val = s->regs[s->index_reg];
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    if (addr == SIS5595_ADDR_REG_OFFSET) {
        s->index_reg = val & 0xFF;
    } else if (addr == SIS5595_DATA_REG_OFFSET) {
        s->regs[s->index_reg] = val & 0xFF;
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

    memset(s->regs, 0, sizeof(s->regs));
    s->index_reg = 0;
    s->data_reg = 0;
    /* Initialize registers to default states as expected by the driver */
    s->regs[SIS5595_REG_CONFIG] = 0x01; /* set bit 0 to start monitoring */
    s->regs[SIS5595_REG_ALARM1] = 0;
    s->regs[SIS5595_REG_ALARM2] = 0;
    s->regs[SIS5595_REG_FANDIV] = 0; /* fans divisor 1 */
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    uint32_t val = pci_default_read_config(pdev, address, len);
    /*
     * The driver reads the base address from SIS5595_BASE_REG (0x68).
     * Return the current BAR0 address to satisfy the driver's expectation.
     */
    if (address <= SIS5595_BASE_REG + 3 && address + len - 1 >= SIS5595_BASE_REG) {
        uint32_t bar0 = pci_get_long(pdev->config + PCI_BASE_ADDRESS_0);
        bar0 &= PCI_BASE_ADDRESS_IO_MASK;
        /* Overwrite the appropriate bytes in the returned value */
        for (int i = 0; i < len; i++) {
            int cfg_addr = address + i;
            if (cfg_addr >= SIS5595_BASE_REG && cfg_addr < SIS5595_BASE_REG + 4) {
                int shift = (cfg_addr - SIS5595_BASE_REG) * 8;
                val = deposit32(val, i * 8, 8, (bar0 >> shift) & 0xff);
            }
        }
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    pci_default_write_config(pdev, address, val, len);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1039 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0008 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0880 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0);

    /* Set default enable register value (enabled) */
    pci_set_byte(pci_conf + SIS5595_ENABLE_REG, 0x80);

    /* Set pin register default (bit 7=0 -> 5 voltages if revision>=REV2MIN) */
    pci_set_byte(pci_conf + SIS5595_PIN_REG, 0x00);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "sis5595-pio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{


}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sis5595_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
