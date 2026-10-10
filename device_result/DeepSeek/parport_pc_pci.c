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

#define TYPE_PCIBASE_DEVICE "parport_pc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1106
#define DEVICE_ID 0x0686
#define CLASS_ID PCI_CLASS_COMMUNICATION_PARALLEL

#define BAR2_SIZE 8
#define BAR3_SIZE 4

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

    /* Parallel port registers */
    uint8_t data_reg;
    uint8_t status_val;
    uint8_t control_reg;
    uint8_t epp_addr_reg;
    uint8_t epp_data_reg;
    uint8_t ecr_reg;
    uint8_t ecp_fifo_count;
    uint8_t ecp_fifo_depth;
    uint8_t ecr_mode_last;
    bool timeout;
};

/* BAR2 (standard parallel port) I/O handlers */
static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0: /* Data */
        val = s->data_reg;
        break;
    case 1: /* Status */
        val = 0xD8 | (s->timeout ? 0x01 : 0);
        break;
    case 2: /* Control */
        val = s->control_reg;
        break;
    case 3: /* EPP Address */
        val = s->epp_addr_reg;
        break;
    case 4: /* EPP Data */
        val = s->epp_data_reg;
        break;
    default:
        val = 0xFF;
        break;
    }
    return val;
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0: /* Data */
        s->data_reg = val & 0xFF;
        break;
    case 1: /* Status (write clears EPP timeout) */
        s->timeout = false;
        break;
    case 2: /* Control */
        s->control_reg = val & 0xFF;
        break;
    case 3: /* EPP Address */
        s->epp_addr_reg = val & 0xFF;
        break;
    case 4: /* EPP Data */
        s->epp_data_reg = val & 0xFF;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* BAR3 (ECP extended registers) I/O handlers */
static uint64_t pcibase_bar3_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0: /* FIFO / Config A */
        if ((s->ecr_reg >> 5) == 7) { /* Configuration mode */
            val = 0x90; /* Return CONFIGA: pword=1, level interrupt */
        } else {
            if (s->ecp_fifo_count > 0) {
                s->ecp_fifo_count--;
            }
            val = 0xAA; /* Arbitrary FIFO read value */
        }
        break;
    case 1: /* Status / Config B */
        if ((s->ecr_reg >> 5) == 7) {
            val = 0x00; /* CONFIGB: no DMA, IRQ line 0 */
        } else {
            val = 0xFF;
        }
        break;
    case 2: /* ECR */
        val = s->ecr_reg;
        /* Set status bits */
        if (s->ecp_fifo_count == 0) {
            val |= 0x01;
        } else {
            val &= ~0x01;
        }
        if (s->ecp_fifo_count >= s->ecp_fifo_depth) {
            val |= 0x02;
        } else {
            val &= ~0x02;
        }
        break;
    default:
        val = 0xFF;
        break;
    }
    return val;
}

static void pcibase_bar3_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0: /* FIFO */
        if ((s->ecr_reg >> 5) != 7) { /* Not in configuration mode */
            if (s->ecp_fifo_count < s->ecp_fifo_depth) {
                s->ecp_fifo_count++;
            }
        }
        break;
    case 1: /* Status/ConfigB ignore */
        break;
    case 2: /* ECR */
        {
            uint8_t old_mode = (s->ecr_reg >> 5) & 0x7;
            s->ecr_reg = val & 0xFF;
            uint8_t new_mode = (s->ecr_reg >> 5) & 0x7;
            if (new_mode != old_mode) {
                s->ecp_fifo_count = 0; /* Reset FIFO on mode change */
            }
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_bar3_ops = {
    .read = pcibase_bar3_read,
    .write = pcibase_bar3_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->data_reg = 0;
    s->status_val = 0xD8;
    s->control_reg = 0;
    s->epp_addr_reg = 0;
    s->epp_data_reg = 0;
    s->ecr_reg = 0;
    s->ecp_fifo_count = 0;
    s->ecp_fifo_depth = 16;
    s->ecr_mode_last = 0;
    s->timeout = false;
}

static void pcibase_register_bar2(PCIDevice *pdev, PCIBaseState *s, Error **errp)
{
    hwaddr size = pow2ceil(BAR2_SIZE);
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_bar2_ops, s, "bar2", size);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[2]);
}

static void pcibase_register_bar3(PCIDevice *pdev, PCIBaseState *s, Error **errp)
{
    hwaddr size = pow2ceil(BAR3_SIZE);
    memory_region_init_io(&s->bar_regions[3], OBJECT(s), &pcibase_bar3_ops, s, "bar3", size);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[3]);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    pcibase_register_bar2(pdev, s, errp);
    pcibase_register_bar3(pdev, s, errp);
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
    .name = "parport_pc_pci",
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
