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


#define TYPE_PCIBASE_DEVICE "exar_serial_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ACCESSIO		0x494f
#define PCI_DEVICE_ID_ACCESSIO_COM_2S		0x1052

#define UART_EXAR_INT0		0x80
#define UART_EXAR_8XMODE	0x88
#define UART_EXAR_SLEEP		0x8b
#define UART_EXAR_DVID		0x8d
#define UART_EXAR_FCTR		0x08
#define UART_EXAR_TXTRG		0x0a
#define UART_EXAR_RXTRG		0x0b
#define UART_EXAR_MPIOINT_7_0	0x8f
#define UART_EXAR_MPIOLVL_7_0	0x90
#define UART_EXAR_MPIO3T_7_0	0x91
#define UART_EXAR_MPIOINV_7_0	0x92
#define UART_EXAR_MPIOSEL_7_0	0x93
#define UART_EXAR_MPIOOD_7_0	0x94
#define UART_EXAR_MPIOINT_15_8	0x95
#define UART_EXAR_MPIOLVL_15_8	0x96
#define UART_EXAR_MPIO3T_15_8	0x97
#define UART_EXAR_MPIOINV_15_8	0x98
#define UART_EXAR_MPIOSEL_15_8	0x99
#define UART_EXAR_MPIOOD_15_8	0x94
#define UART_EXAR_DLD			0x02
#define UART_EXAR_REGB		0x8e

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
    uint8_t int0;
    uint8_t mode8x;
    uint8_t sleep;
    uint8_t dvid;
    uint8_t fctr;
    uint8_t txtrg;
    uint8_t rxtrg;
    uint8_t mpioint_7_0;
    uint8_t mpiolvl_7_0;
    uint8_t mpio3t_7_0;
    uint8_t mpioinv_7_0;
    uint8_t mpiosel_7_0;
    uint8_t mpiood_7_0;
    uint8_t mpioint_15_8;
    uint8_t mpiolvl_15_8;
    uint8_t mpio3t_15_8;
    uint8_t mpioinv_15_8;
    uint8_t mpiosel_15_8;
    uint8_t mpiood_15_8;
    uint8_t dld;
    uint8_t regb;
};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->int0) {
        if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msi || !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t reg = addr & 0xFF;

    switch (reg) {
        case UART_EXAR_INT0:
            val = s->int0;
            s->int0 = 0; /* Clear on read */
            pcibase_update_irq(s);
            break;
        case UART_EXAR_8XMODE: val = s->mode8x; break;
        case UART_EXAR_SLEEP: val = s->sleep; break;
        case UART_EXAR_DVID: val = s->dvid; break;
        case UART_EXAR_FCTR: val = s->fctr; break;
        case UART_EXAR_TXTRG: val = s->txtrg; break;
        case UART_EXAR_RXTRG: val = s->rxtrg; break;
        case UART_EXAR_MPIOINT_7_0: val = s->mpioint_7_0; break;
        case UART_EXAR_MPIOLVL_7_0: val = s->mpiolvl_7_0; break;
        case UART_EXAR_MPIO3T_7_0: val = s->mpio3t_7_0; break;
        case UART_EXAR_MPIOINV_7_0: val = s->mpioinv_7_0; break;
        case UART_EXAR_MPIOSEL_7_0: val = s->mpiosel_7_0; break;
        case UART_EXAR_MPIOOD_7_0: val = s->mpiood_7_0; break;
        case UART_EXAR_MPIOINT_15_8: val = s->mpioint_15_8; break;
        case UART_EXAR_MPIOLVL_15_8: val = s->mpiolvl_15_8; break;
        case UART_EXAR_MPIO3T_15_8: val = s->mpio3t_15_8; break;
        case UART_EXAR_MPIOINV_15_8: val = s->mpioinv_15_8; break;
        case UART_EXAR_MPIOSEL_15_8: val = s->mpiosel_15_8; break;
        /* UART_EXAR_MPIOOD_15_8 is identical to UART_EXAR_MPIOOD_7_0 in the template macro */
        case UART_EXAR_DLD: val = s->dld; break;
        case UART_EXAR_REGB: val = s->regb; break;
        default: val = 0; break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg = addr & 0xFF;

    switch (reg) {
        case UART_EXAR_8XMODE: s->mode8x = val; break;
        case UART_EXAR_SLEEP: s->sleep = val; break;
        case UART_EXAR_FCTR: s->fctr = val; break;
        case UART_EXAR_TXTRG: s->txtrg = val; break;
        case UART_EXAR_RXTRG: s->rxtrg = val; break;
        case UART_EXAR_MPIOINT_7_0: s->mpioint_7_0 = val; break;
        case UART_EXAR_MPIOLVL_7_0: s->mpiolvl_7_0 = val; break;
        case UART_EXAR_MPIO3T_7_0: s->mpio3t_7_0 = val; break;
        case UART_EXAR_MPIOINV_7_0: s->mpioinv_7_0 = val; break;
        case UART_EXAR_MPIOSEL_7_0: s->mpiosel_7_0 = val; break;
        case UART_EXAR_MPIOOD_7_0: 
            s->mpiood_7_0 = val; 
            s->mpiood_15_8 = val; /* Handle duplicate macro gracefully */
            break;
        case UART_EXAR_MPIOINT_15_8: s->mpioint_15_8 = val; break;
        case UART_EXAR_MPIOLVL_15_8: s->mpiolvl_15_8 = val; break;
        case UART_EXAR_MPIO3T_15_8: s->mpio3t_15_8 = val; break;
        case UART_EXAR_MPIOINV_15_8: s->mpioinv_15_8 = val; break;
        case UART_EXAR_MPIOSEL_15_8: s->mpiosel_15_8 = val; break;
        case UART_EXAR_DLD: s->dld = val; break;
        case UART_EXAR_REGB: s->regb = val; break;
        default: break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO not used by driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* PIO not used by driver */
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

    s->int0 = 0;
    s->mode8x = 0;
    s->sleep = 0;
    s->dvid = 0x82; /* 2-port XR17V35x */
    s->fctr = 0;
    s->txtrg = 0;
    s->rxtrg = 0;
    s->mpioint_7_0 = 0;
    s->mpiolvl_7_0 = 0;
    s->mpio3t_7_0 = 0;
    s->mpioinv_7_0 = 0;
    s->mpiosel_7_0 = 0;
    s->mpiood_7_0 = 0;
    s->mpioint_15_8 = 0;
    s->mpiolvl_15_8 = 0;
    s->mpio3t_15_8 = 0;
    s->mpioinv_15_8 = 0;
    s->mpiosel_15_8 = 0;
    s->mpiood_15_8 = 0;
    s->dld = 0;
    s->regb = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ACCESSIO );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ACCESSIO_COM_2S );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_COMMUNICATION_SERIAL );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "bar0";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
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
    .name = "exar_serial_pci",
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
