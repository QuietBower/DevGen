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

#define TYPE_PCIBASE_DEVICE "addi_apci_3501_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ADDIDATA                 0x15B8
#define APCI3501_AO_CTRL_STATUS_REG		0x00
#define APCI3501_AO_CTRL_BIPOLAR		BIT(0)
#define APCI3501_AO_STATUS_READY		BIT(8)
#define APCI3501_AO_DATA_REG			0x04
#define APCI3501_AO_DATA_CHAN(x)		((x) << 0)
#define APCI3501_AO_DATA_VAL(x)			((x) << 8)
#define APCI3501_AO_DATA_BIPOLAR		BIT(31)
#define APCI3501_AO_TRIG_SCS_REG		0x08
#define APCI3501_TIMER_BASE			0x20
#define APCI3501_DO_REG				0x40
#define APCI3501_DI_REG				0x50
#define NVRAM_USER_DATA_START	0x100
#define NVCMD_BEGIN_READ	(0x7 << 5)
#define NVCMD_LOAD_LOW		(0x4 << 5)
#define NVCMD_LOAD_HIGH		(0x5 << 5)
#define EEPROM_DIGITALINPUT		0
#define EEPROM_DIGITALOUTPUT		1
#define EEPROM_ANALOGINPUT		2
#define EEPROM_ANALOGOUTPUT		3
#define EEPROM_TIMER			4
#define EEPROM_WATCHDOG			5
#define EEPROM_TIMER_WATCHDOG_COUNTER	10
#define AMCC_OP_REG_MCSR         0x3c
#define AMCC_OP_REG_MCSR_NVCMD  (AMCC_OP_REG_MCSR + 3)
#define AMCC_OP_REG_MCSR_NVDATA (AMCC_OP_REG_MCSR + 2)

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
    unsigned long amcc;
    unsigned char timer_mode;

    /* DMA Context */
    uint32_t ao_ctrl_status;
    uint32_t ao_data[8];
    uint32_t do_reg;
    uint32_t di_reg;
    
    uint8_t nvcmd;
    uint8_t nvdata;
    uint16_t nvaddr;
    uint8_t eeprom[1024];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 1) {
        switch (addr) {
        case AMCC_OP_REG_MCSR_NVCMD:
            val = 0; /* Ready */
            break;
        case AMCC_OP_REG_MCSR_NVDATA:
            val = s->nvdata;
            break;
        }
    } else if (size == 4) {
        switch (addr) {
        case APCI3501_AO_CTRL_STATUS_REG:
            val = s->ao_ctrl_status | APCI3501_AO_STATUS_READY;
            break;
        case APCI3501_DO_REG:
            val = s->do_reg;
            break;
        case APCI3501_DI_REG:
            val = s->di_reg;
            break;
        }
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    if (size == 1) {
        switch (addr) {
        case AMCC_OP_REG_MCSR_NVCMD:
            s->nvcmd = val;
            if (s->nvcmd == NVCMD_BEGIN_READ) {
                if (s->nvaddr < sizeof(s->eeprom)) {
                    s->nvdata = s->eeprom[s->nvaddr];
                } else {
                    s->nvdata = 0;
                }
            }
            break;
        case AMCC_OP_REG_MCSR_NVDATA:
            if (s->nvcmd == NVCMD_LOAD_LOW) {
                s->nvaddr = (s->nvaddr & 0xff00) | (val & 0xff);
            } else if (s->nvcmd == NVCMD_LOAD_HIGH) {
                s->nvaddr = (s->nvaddr & 0x00ff) | ((val & 0xff) << 8);
            }
            break;
        }
    } else if (size == 4) {
        switch (addr) {
        case APCI3501_AO_CTRL_STATUS_REG:
            s->ao_ctrl_status = val & APCI3501_AO_CTRL_BIPOLAR;
            break;
        case APCI3501_AO_DATA_REG:
            {
                uint32_t chan = val & 0xff;
                if (chan < 8) {
                    s->ao_data[chan] = val;
                }
            }
            break;
        case APCI3501_DO_REG:
            s->do_reg = val;
            break;
        }
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

    s->ao_ctrl_status = 0;
    memset(s->ao_data, 0, sizeof(s->ao_data));
    s->do_reg = 0;
    s->di_reg = 0;
    s->nvcmd = 0;
    s->nvdata = 0;
    s->nvaddr = 0;
    
    memset(s->eeprom, 0, sizeof(s->eeprom));
    /* Setup EEPROM to report 8 analog output channels */
    s->eeprom[NVRAM_USER_DATA_START + 10] = 1;
    s->eeprom[NVRAM_USER_DATA_START + 11] = 0;
    s->eeprom[NVRAM_USER_DATA_START + 12] = EEPROM_ANALOGOUTPUT;
    s->eeprom[NVRAM_USER_DATA_START + 13] = 0;
    s->eeprom[NVRAM_USER_DATA_START + 14] = 20;
    s->eeprom[NVRAM_USER_DATA_START + 15] = 0;
    s->eeprom[NVRAM_USER_DATA_START + 30] = (8 << 4) & 0xff;
    s->eeprom[NVRAM_USER_DATA_START + 31] = ((8 << 4) >> 8) & 0xff;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ADDIDATA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x3001 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x80, .name = "amcc" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 0x80, .name = "iobase" };

    /* BAR Initialization */
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
    .name = "addi_apci_3501_pci",
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
