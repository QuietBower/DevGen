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

#define TYPE_PCIBASE_DEVICE "i5400_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_5400_ERR	0x4030
#define PCI_DEVICE_ID_INTEL_5400_FBD0	0x4035
#define PCI_DEVICE_ID_INTEL_5400_FBD1	0x4036

#define AMBASE			0x48
#define MAXCH			0x56
#define MAXDIMMPERCH		0x57
#define TOLM			0x6C
#define REDMEMB			0x7C
#define MIR0			0x80
#define MIR1			0x84
#define AMIR0			0x8c
#define AMIR1			0x90
#define FERR_FAT_FBD		0x98
#define NERR_FAT_FBD		0x9c
#define FERR_NF_FBD		0xa0
#define NERR_NF_FBD		0xa4
#define EMASK_FBD		0xa8
#define ERR0_FBD		0xac
#define ERR1_FBD		0xb0
#define ERR2_FBD		0xb4
#define MCERR_FBD		0xb8
#define AMBPRESENT_0	0x64
#define AMBPRESENT_1	0x66
#define MTR0		0x80
#define MTR1		0x82
#define MTR2		0x84
#define MTR3		0x86
#define NRECFGLOG		0x74
#define RECFGLOG		0x78
#define NRECMEMA		0xbe
#define NRECMEMB		0xc0
#define NRECFB_DIMMA		0xc4
#define NRECFB_DIMMB		0xc8
#define NRECFB_DIMMC		0xcc
#define NRECFB_DIMMD		0xd0
#define NRECFB_DIMME		0xd4
#define NRECFB_DIMMF		0xd8
#define REDMEMA			0xdC
#define RECMEMA			0xf0
#define RECMEMB			0xf4
#define RECFB_DIMMA		0xf8
#define RECFB_DIMMB		0xec
#define RECFB_DIMMC		0xf0
#define RECFB_DIMMD		0xf4
#define RECFB_DIMME		0xf8
#define RECFB_DIMMF		0xfC

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
    uint32_t ferr_fat_fbd;
    uint32_t nerr_fat_fbd;
    uint32_t ferr_nf_fbd;
    uint32_t nerr_nf_fbd;
    uint32_t redmemb;
    uint16_t recmema;
    uint32_t recmemb;
    uint16_t nrecmema;
    uint32_t nrecmemb;
};

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

    uint8_t *pci_conf = PCI_DEVICE(dev)->config;
    
    /* Initialize PCI configuration space registers expected by the driver */
    pci_set_word(pci_conf + AMBASE, 0x0000);
    pci_set_word(pci_conf + AMBASE + 2, 0x7FFF);
    pci_set_word(pci_conf + AMBASE + 4, 0x7FFF);
    pci_set_word(pci_conf + AMBASE + 6, 0x7FFF);
    
    pci_set_word(pci_conf + TOLM, 0x1000);
    
    pci_set_byte(pci_conf + MAXCH, 2);
    pci_set_byte(pci_conf + MAXDIMMPERCH, 4);
    
    /* MIR0/MTR0 overlap at 0x80, MIR1/MTR1 at 0x82/0x84 */
    pci_set_word(pci_conf + 0x80, 0xFFFF);
    pci_set_word(pci_conf + 0x82, 0xFFFF);
    pci_set_word(pci_conf + 0x84, 0xFFFF);
    pci_set_word(pci_conf + 0x86, 0xFFFF);
    
    pci_set_word(pci_conf + AMBPRESENT_0, 0xFFFF);
    pci_set_word(pci_conf + AMBPRESENT_1, 0xFFFF);
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

    pci_config_set_interrupt_pin(pci_conf, 1);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Enable writes to error registers (W1C or RW) */
    pci_set_long(pdev->wmask + FERR_FAT_FBD, 0xFFFFFFFF);
    pci_set_long(pdev->wmask + FERR_NF_FBD, 0xFFFFFFFF);
    pci_set_long(pdev->wmask + EMASK_FBD, 0xFFFFFFFF);

    /* BAR Initialization */
    s->num_bars = 0;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Set device ID for FBD0 and FBD1 */
    if (PCI_FUNC(pdev->devfn) == 3) {
        pci_config_set_device_id(pdev->config, PCI_DEVICE_ID_INTEL_5400_FBD0);
    } else if (PCI_FUNC(pdev->devfn) == 4) {
        pci_config_set_device_id(pdev->config, PCI_DEVICE_ID_INTEL_5400_FBD1);
    }

    /* Create Function 1, 2, 3, 4 if we are Function 0 */
    if (PCI_FUNC(pdev->devfn) == 0) {
        pdev->cap_present |= QEMU_PCI_CAP_MULTIFUNCTION;
        pdev->config[PCI_HEADER_TYPE] |= PCI_HEADER_TYPE_MULTI_FUNCTION;
        
        PCIDevice *func1 = pci_new(PCI_DEVFN(PCI_SLOT(pdev->devfn), 1), TYPE_PCIBASE_DEVICE);
        pci_realize_and_unref(func1, pci_get_bus(pdev), &error_fatal);
        
        PCIDevice *func2 = pci_new(PCI_DEVFN(PCI_SLOT(pdev->devfn), 2), TYPE_PCIBASE_DEVICE);
        pci_realize_and_unref(func2, pci_get_bus(pdev), &error_fatal);

        PCIDevice *func3 = pci_new(PCI_DEVFN(PCI_SLOT(pdev->devfn), 3), TYPE_PCIBASE_DEVICE);
        pci_realize_and_unref(func3, pci_get_bus(pdev), &error_fatal);

        PCIDevice *func4 = pci_new(PCI_DEVFN(PCI_SLOT(pdev->devfn), 4), TYPE_PCIBASE_DEVICE);
        pci_realize_and_unref(func4, pci_get_bus(pdev), &error_fatal);
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
    .name = "i5400_edac_pci",
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
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_5400_ERR;
    k->class_id  = PCI_CLASS_BRIDGE_HOST;
    k->revision  = 0x01;
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
