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

#define PCI_VENDOR_ID_SIEMENS 0x110a

#define TYPE_PCIBASE_DEVICE "ems_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define EMS_PCI_V1_MAX_CHAN 2
#define PITA2_ICR           0x00
#define PITA2_ICR_INT0      0x00000002
#define PITA2_ICR_INT0_EN   0x00020000
#define PITA2_MISC          0x1c
#define PITA2_MISC_CONFIG   0x04000000

#define EMS_PCI_V1_BASE_BAR 1
#define EMS_PCI_V1_CONF_BAR 0
#define EMS_PCI_V1_CONF_SIZE 4096
#define EMS_PCI_V1_CAN_BASE_OFFSET 0x400
#define EMS_PCI_V1_CAN_CTRL_SIZE 0x200
#define EMS_PCI_BASE_SIZE  4096

#define SJA1000_MOD		0x00
#define SJA1000_CMR		0x01
#define SJA1000_SR		0x02
#define SJA1000_IR		0x03
#define SJA1000_IER		0x04
#define SJA1000_BTR0		0x06
#define SJA1000_BTR1		0x07
#define SJA1000_OCR		0x08
#define SJA1000_ECC		0x0C
#define SJA1000_RXERR		0x0E
#define SJA1000_TXERR		0x0F
#define SJA1000_FI		0x10
#define SJA1000_ID1		0x11
#define SJA1000_ID2		0x12
#define SJA1000_SFF_BUF		0x13
#define SJA1000_ID3		0x13
#define SJA1000_ID4		0x14
#define SJA1000_EFF_BUF		0x15
#define SJA1000_CDR		0x1F

#define SJA1000_ACCC0		0x10
#define SJA1000_ACCC1		0x11
#define SJA1000_ACCC2		0x12
#define SJA1000_ACCC3		0x13
#define SJA1000_ACCM0		0x14
#define SJA1000_ACCM1		0x15
#define SJA1000_ACCM2		0x16
#define SJA1000_ACCM3		0x17

#define OCR_TX0_PUSHPULL  0x18
#define OCR_TX1_PUSHPULL  0xc0
#define CDR_CBP		0x40
#define CDR_CLKOUT_MASK 0x07

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
    uint32_t pita2_icr;
    uint32_t pita2_misc;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t can_ctrl[EMS_PCI_V1_MAX_CHAN][EMS_PCI_V1_CAN_CTRL_SIZE];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if ((s->pita2_icr & PITA2_ICR_INT0_EN) && (s->pita2_icr & PITA2_ICR_INT0)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 4) {
        /* CONF BAR */
        switch (addr) {
            case PITA2_ICR:
                val = s->pita2_icr;
                break;
            case PITA2_MISC:
                val = s->pita2_misc;
                break;
            default:
                val = 0;
                break;
        }
    } else if (size == 1) {
        /* BASE BAR */
        if (addr == 0x00) val = 0x55;
        else if (addr == 0x04) val = 0xAA;
        else if (addr == 0x08) val = 0x01;
        else if (addr == 0x0C) val = 0xCB;
        else if (addr == 0x10) val = 0x11;
        else if (addr >= EMS_PCI_V1_CAN_BASE_OFFSET) {
            int chan = (addr >= EMS_PCI_V1_CAN_BASE_OFFSET + EMS_PCI_V1_CAN_CTRL_SIZE) ? 1 : 0;
            hwaddr offset = addr - EMS_PCI_V1_CAN_BASE_OFFSET - (chan * EMS_PCI_V1_CAN_CTRL_SIZE);
            if (offset < EMS_PCI_V1_CAN_CTRL_SIZE) {
                val = s->can_ctrl[chan][offset];
            }
        }
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        /* CONF BAR */
        switch (addr) {
            case PITA2_ICR:
                if (val & PITA2_ICR_INT0) {
                    s->pita2_icr &= ~PITA2_ICR_INT0;
                }
                s->pita2_icr = (val & ~PITA2_ICR_INT0) | (s->pita2_icr & PITA2_ICR_INT0);
                pcibase_update_irq(s);
                break;
            case PITA2_MISC:
                s->pita2_misc = val;
                break;
        }
    } else if (size == 1) {
        /* BASE BAR */
        if (addr == 0x00) {
            memset(s->can_ctrl, 0, sizeof(s->can_ctrl));
        } else if (addr >= EMS_PCI_V1_CAN_BASE_OFFSET) {
            int chan = (addr >= EMS_PCI_V1_CAN_BASE_OFFSET + EMS_PCI_V1_CAN_CTRL_SIZE) ? 1 : 0;
            hwaddr offset = addr - EMS_PCI_V1_CAN_BASE_OFFSET - (chan * EMS_PCI_V1_CAN_CTRL_SIZE);
            if (offset < EMS_PCI_V1_CAN_CTRL_SIZE) {
                s->can_ctrl[chan][offset] = val;
            }
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    s->pita2_icr = 0;
    s->pita2_misc = 0;
    memset(s->can_ctrl, 0, sizeof(s->can_ctrl));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SIEMENS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x2104 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_OTHER );
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
    s->bar_info[0].index = EMS_PCI_V1_CONF_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = EMS_PCI_V1_CONF_SIZE;
    s->bar_info[0].name = "ems_pci_v1_conf";

    s->bar_info[1].index = EMS_PCI_V1_BASE_BAR;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = EMS_PCI_BASE_SIZE;
    s->bar_info[1].name = "ems_pci_v1_base";
  
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ems_pci_pci",
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
