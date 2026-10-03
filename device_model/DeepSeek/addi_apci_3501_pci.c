/*
 * QEMU Virtual Device Model for addi_apci_3501
 * Based on Linux driver addi_apci_3501.c
 * Implements PCI PIO device with AMCC EEPROM and APCI3501 registers.
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

#define TYPE_PCIBASE_DEVICE "addi_apci_3501_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ADDIDATA 0x15b8
#define DEVICE_ID 0x3001
#define CLASS_ID PCI_CLASS_OTHERS

#define APCI3501_AO_CTRL_STATUS_REG  0x00
#define APCI3501_AO_CTRL_BIPOLAR     BIT(0)
#define APCI3501_AO_STATUS_READY     BIT(8)
#define APCI3501_AO_DATA_REG         0x04
#define APCI3501_AO_DATA_CHAN(x)     ((x) << 0)
#define APCI3501_AO_DATA_VAL(x)      ((x) << 8)
#define APCI3501_AO_DATA_BIPOLAR     BIT(31)
#define APCI3501_AO_TRIG_SCS_REG     0x08
#define APCI3501_TIMER_BASE          0x20
#define APCI3501_DO_REG              0x40
#define APCI3501_DI_REG              0x50
#define NVRAM_USER_DATA_START        0x100
#define NVCMD_BEGIN_READ             (0x7 << 5)
#define NVCMD_LOAD_LOW               (0x4 << 5)
#define NVCMD_LOAD_HIGH              (0x5 << 5)
#define EEPROM_DIGITALINPUT          0
#define EEPROM_DIGITALOUTPUT         1
#define EEPROM_ANALOGINPUT           2
#define EEPROM_ANALOGOUTPUT          3
#define EEPROM_TIMER                 4
#define EEPROM_WATCHDOG              5
#define EEPROM_TIMER_WATCHDOG_COUNTER 10
#define AMCC_OP_REG_MCSR             0x3c
#define AMCC_OP_REG_MCSR_NVCMD       (AMCC_OP_REG_MCSR + 3)
#define AMCC_OP_REG_MCSR_NVDATA      (AMCC_OP_REG_MCSR + 2)

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
    const MemoryRegionOps *ops;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Hardware Register Shadows */
    uint32_t ao_ctrl_status;
    uint32_t ao_data;
    uint32_t ao_trig_scs;
    uint32_t timer_base; /* Placeholder: exact layout unknown */
    uint32_t do_reg;
    uint32_t di_reg;
    /* EEPROM state */
    uint8_t eeprom_data[0x400];
    uint8_t nvcmd;
    uint8_t nvdata;
    uint8_t eeprom_state; /* 0=idle,1=low_addr,2=high_addr,3=read */
    uint8_t eeprom_addr_low;
    uint8_t eeprom_addr_high;
};

/* PIO handlers for BAR0 (AMCC registers) */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr == 0x3e) { /* NVDATA */
        if (s->eeprom_state == 3) {
            uint16_t eeprom_addr = (s->eeprom_addr_high << 8) | s->eeprom_addr_low;
            val = s->eeprom_data[eeprom_addr];
            s->eeprom_state = 0;
        } else {
            val = s->nvdata;
        }
    } else if (addr == 0x3f) { /* NVCMD */
        val = 0; /* always ready */
    }
    return val;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == 0x3e) { /* NVDATA */
        s->nvdata = val;
        if (s->eeprom_state == 1) {
            s->eeprom_addr_low = val;
            s->eeprom_state = 0;
        } else if (s->eeprom_state == 2) {
            s->eeprom_addr_high = val;
            s->eeprom_state = 0;
        }
    } else if (addr == 0x3f) { /* NVCMD */
        s->nvcmd = val;
        if (val == NVCMD_LOAD_LOW) {
            s->eeprom_state = 1;
        } else if (val == NVCMD_LOAD_HIGH) {
            s->eeprom_state = 2;
        } else if (val == NVCMD_BEGIN_READ) {
            s->eeprom_state = 3;
        }
    }
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* PIO handlers for BAR1 (APCI3501 registers) */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case APCI3501_AO_CTRL_STATUS_REG:
        val = (s->ao_ctrl_status & APCI3501_AO_CTRL_BIPOLAR) | APCI3501_AO_STATUS_READY;
        break;
    case APCI3501_DO_REG:
        val = s->do_reg;
        break;
    case APCI3501_DI_REG:
        val = s->di_reg;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case APCI3501_AO_CTRL_STATUS_REG:
        s->ao_ctrl_status = val & APCI3501_AO_CTRL_BIPOLAR;
        break;
    case APCI3501_AO_DATA_REG:
        /* Data written to DAC, no effect on status */
        break;
    case APCI3501_DO_REG:
        s->do_reg = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize registers to power-on defaults */
    s->ao_ctrl_status = 0;
    s->ao_data = 0;
    s->ao_trig_scs = 0;
    s->timer_base = 0;
    s->do_reg = 0;
    s->di_reg = 0;

    /* Initialize EEPROM state machine */
    s->nvcmd = 0;
    s->nvdata = 0;
    s->eeprom_state = 0;
    s->eeprom_addr_low = 0;
    s->eeprom_addr_high = 0;

    /* Pre-fill EEPROM data to satisfy driver probing */
    memset(s->eeprom_data, 0xff, sizeof(s->eeprom_data));
    /* nfuncs = 1 at byte offset 10 (word address 0x10a) */
    s->eeprom_data[0x10a] = 0x01;
    s->eeprom_data[0x10b] = 0x00;
    /* func = EEPROM_ANALOGOUTPUT at byte offset 12 (word address 0x10c) */
    s->eeprom_data[0x10c] = 0x03;
    s->eeprom_data[0x10d] = 0x00;
    /* addr = 20 at byte offset 14 (word address 0x10e) */
    s->eeprom_data[0x10e] = 0x14;
    s->eeprom_data[0x10f] = 0x00;
    /* config at addr+10 = 30 (word address 0x11e): ao_n_chan = 8 */
    s->eeprom_data[0x11e] = 0x80;
    s->eeprom_data[0x11f] = 0x00;
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
        memory_region_init_io(mr, OBJECT(s), bi->ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), bi->ops, s, bi->name, aligned_size);
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
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x80, .name = "amcc", .ops = &pcibase_bar0_ops };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 0x80, .name = "apci3501-regs", .ops = &pcibase_bar1_ops };
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
