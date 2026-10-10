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


#define TYPE_PCIBASE_DEVICE "i2c_eg20t_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_PCH_I2C	0x8817

#define PCH_I2CSADR	0x00
#define PCH_I2CCTL	0x04
#define PCH_I2CSR	0x08
#define PCH_I2CDR	0x0C
#define PCH_I2CMON	0x10
#define PCH_I2CBC	0x14
#define PCH_I2CMOD	0x18
#define PCH_I2CBUFSLV	0x1C
#define PCH_I2CBUFSUB	0x20
#define PCH_I2CBUFFOR	0x24
#define PCH_I2CBUFCTL	0x28
#define PCH_I2CBUFMSK	0x2C
#define PCH_I2CBUFSTA	0x30
#define PCH_I2CBUFLEV	0x34
#define PCH_I2CESRFOR	0x38
#define PCH_I2CESRCTL	0x3C
#define PCH_I2CESRMSK	0x40
#define PCH_I2CESRSTA	0x44
#define PCH_I2CTMR	0x48
#define PCH_I2CNF	0xF8
#define PCH_I2CSRST	0xFC

#define PCH_I2CCTL_I2CMEN	0x0080
#define FAST_MODE_CLK		400
#define FAST_MODE_EN		0x0001
#define PCH_MAX_CLK		100000
#define NORMAL_INTR_ENBL	0x0300
#define BUFFER_MODE		0x1
#define EEPROM_SR_MODE		0x2
#define NORMAL_MODE		0x0
#define I2CMAL_BIT		0x0010
#define I2CMCF_BIT		0x0080
#define I2CMIF_BIT		0x0002

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
    uint32_t pch_event_flag;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t i2csadr;
    uint32_t i2cctl;
    uint32_t i2csr;
    uint32_t i2cdr;
    uint32_t i2cmon;
    uint32_t i2cbc;
    uint32_t i2cmod;
    uint32_t i2cbufslv;
    uint32_t i2cbufsub;
    uint32_t i2cbuffor;
    uint32_t i2cbufctl;
    uint32_t i2cbufmsk;
    uint32_t i2cbufsta;
    uint32_t i2cbuflev;
    uint32_t i2cesrfor;
    uint32_t i2cesrctl;
    uint32_t i2cesrmsk;
    uint32_t i2cesrsta;
    uint32_t i2ctmr;
    uint32_t i2cnf;
    uint32_t i2csrst;

    int pch_buff_mode_en;
    bool pch_i2c_xfer_in_progress;
    bool pch_i2c_suspended;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_status = false;

    if ((s->i2cctl & NORMAL_INTR_ENBL) &&
        (s->i2csr & (I2CMAL_BIT | I2CMCF_BIT | I2CMIF_BIT))) {
        irq_status = true;
    }

    pci_set_irq(pdev, irq_status ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    hwaddr offset = addr & 0xFF;

    switch (offset) {
    case PCH_I2CSADR: val = s->i2csadr; break;
    case PCH_I2CCTL: val = s->i2cctl; break;
    case PCH_I2CSR: val = s->i2csr; break;
    case PCH_I2CDR:
        val = s->i2cdr;
        /* Reading data register triggers next byte / completion in driver */
        s->i2csr |= (I2CMCF_BIT | I2CMIF_BIT);
        pcibase_update_irq(s);
        break;
    case PCH_I2CMON: val = s->i2cmon; break;
    case PCH_I2CBC: val = s->i2cbc; break;
    case PCH_I2CMOD: val = s->i2cmod; break;
    case PCH_I2CBUFSLV: val = s->i2cbufslv; break;
    case PCH_I2CBUFSUB: val = s->i2cbufsub; break;
    case PCH_I2CBUFFOR: val = s->i2cbuffor; break;
    case PCH_I2CBUFCTL: val = s->i2cbufctl; break;
    case PCH_I2CBUFMSK: val = s->i2cbufmsk; break;
    case PCH_I2CBUFSTA: val = s->i2cbufsta; break;
    case PCH_I2CBUFLEV: val = s->i2cbuflev; break;
    case PCH_I2CESRFOR: val = s->i2cesrfor; break;
    case PCH_I2CESRCTL: val = s->i2cesrctl; break;
    case PCH_I2CESRMSK: val = s->i2cesrmsk; break;
    case PCH_I2CESRSTA: val = s->i2cesrsta; break;
    case PCH_I2CTMR: val = s->i2ctmr; break;
    case PCH_I2CNF: val = s->i2cnf; break;
    case PCH_I2CSRST: val = s->i2csrst; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase_mmio_read: Bad offset 0x%" HWADDR_PRIx "\n", addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr offset = addr & 0xFF;

    switch (offset) {
    case PCH_I2CSADR: s->i2csadr = val; break;
    case PCH_I2CCTL:
        s->i2cctl = val;
        pcibase_update_irq(s);
        break;
    case PCH_I2CSR:
        s->i2csr = val;
        pcibase_update_irq(s);
        break;
    case PCH_I2CDR:
        s->i2cdr = val;
        /* Writing data register triggers completion in driver */
        s->i2csr |= (I2CMCF_BIT | I2CMIF_BIT);
        pcibase_update_irq(s);
        break;
    case PCH_I2CMON: s->i2cmon = val; break;
    case PCH_I2CBC: s->i2cbc = val; break;
    case PCH_I2CMOD: s->i2cmod = val; break;
    case PCH_I2CBUFSLV: s->i2cbufslv = val; break;
    case PCH_I2CBUFSUB: s->i2cbufsub = val; break;
    case PCH_I2CBUFFOR: s->i2cbuffor = val; break;
    case PCH_I2CBUFCTL: s->i2cbufctl = val; break;
    case PCH_I2CBUFMSK: s->i2cbufmsk = val; break;
    case PCH_I2CBUFSTA: s->i2cbufsta = val; break;
    case PCH_I2CBUFLEV: s->i2cbuflev = val; break;
    case PCH_I2CESRFOR: s->i2cesrfor = val; break;
    case PCH_I2CESRCTL: s->i2cesrctl = val; break;
    case PCH_I2CESRMSK: s->i2cesrmsk = val; break;
    case PCH_I2CESRSTA: s->i2cesrsta = val; break;
    case PCH_I2CTMR: s->i2ctmr = val; break;
    case PCH_I2CNF: s->i2cnf = val; break;
    case PCH_I2CSRST:
        s->i2csrst = val;
        if (val & 0x01) {
            /* Reset I2C controller */
            s->i2csr = 0;
            s->i2cctl = 0;
            pcibase_update_irq(s);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase_mmio_write: Bad offset 0x%" HWADDR_PRIx "\n", addr);
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

    s->i2csadr = 0;
    s->i2cctl = 0;
    s->i2csr = 0;
    s->i2cdr = 0;
    s->i2cmon = 0;
    s->i2cbc = 0;
    s->i2cmod = NORMAL_MODE;
    s->i2cbufslv = 0;
    s->i2cbufsub = 0;
    s->i2cbuffor = 0;
    s->i2cbufctl = 0;
    s->i2cbufmsk = 0;
    s->i2cbufsta = 0;
    s->i2cbuflev = 0;
    s->i2cesrfor = 0;
    s->i2cesrctl = 0;
    s->i2cesrmsk = 0;
    s->i2cesrsta = 0;
    s->i2ctmr = 0;
    s->i2cnf = 0;
    s->i2csrst = 0;

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PCH_I2C );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_SMBUS );
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
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "pch_i2c_bar1";
      
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
    .name = "i2c_eg20t_pci",
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
