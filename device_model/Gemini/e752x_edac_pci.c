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

#define TYPE_PCIBASE_DEVICE "e752x_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_7520_0      0x3590
#define PCI_DEVICE_ID_INTEL_7520_1_ERR  0x3591
#define PCI_DEVICE_ID_INTEL_7525_0      0x359E
#define PCI_DEVICE_ID_INTEL_7525_1_ERR  0x3593
#define PCI_DEVICE_ID_INTEL_7320_0      0x3592
#define PCI_DEVICE_ID_INTEL_7320_1_ERR  0x3593
#define PCI_DEVICE_ID_INTEL_3100_0      0x35B0
#define PCI_DEVICE_ID_INTEL_3100_1_ERR  0x35B1

#define E752X_MCHSCRB       0x52
#define E752X_DRB           0x60
#define E752X_DRA           0x70
#define E752X_DRC           0x7C
#define E752X_DRM           0x80
#define E752X_DDRCSR        0x9A
#define E752X_TOLM          0xC4
#define E752X_REMAPBASE     0xC6
#define E752X_REMAPLIMIT    0xC8
#define E752X_REMAPOFFSET   0xCA
#define E752X_FERR_GLOBAL   0x40
#define E752X_NERR_GLOBAL   0x44
#define E752X_HI_FERR       0x50
#define E752X_HI_NERR       0x52
#define E752X_HI_ERRMASK    0x54
#define E752X_HI_SMICMD     0x5A
#define E752X_SYSBUS_FERR   0x60
#define E752X_SYSBUS_NERR   0x62
#define E752X_SYSBUS_ERRMASK 0x64
#define E752X_SYSBUS_SMICMD 0x6A
#define E752X_BUF_FERR      0x70
#define E752X_BUF_NERR      0x72
#define E752X_BUF_ERRMASK   0x74
#define E752X_BUF_SMICMD    0x7A
#define E752X_DRAM_FERR     0x80
#define E752X_DRAM_NERR     0x82
#define E752X_DRAM_ERRMASK  0x84
#define E752X_DRAM_SMICMD   0x8A
#define E752X_DRAM_RETR_ADD 0xAC
#define E752X_DRAM_SEC1_ADD 0xA0
#define E752X_DRAM_SEC2_ADD 0xC8
#define E752X_DRAM_DED_ADD  0xA4
#define E752X_DRAM_SCRB_ADD 0xA8
#define E752X_DRAM_SEC1_SYNDROME 0xC4
#define E752X_DRAM_SEC2_SYNDROME 0xC6
#define E752X_DEVPRES1      0xF4
#define I3100_NSI_FERR      0x48
#define I3100_NSI_NERR      0x4C
#define I3100_NSI_SMICMD    0x54
#define I3100_NSI_EMASK     0x90
#define ICH5R_PCI_STAT      0x06
#define ICH5R_PCI_2ND_STAT  0x1E
#define ICH5R_PCI_BRIDGE_CTL 0x3E

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
    struct {
        uint32_t ferr_global;
        uint32_t nerr_global;
        uint32_t nsi_ferr;
        uint32_t nsi_nerr;
        uint8_t hi_ferr;
        uint8_t hi_nerr;
        uint16_t sysbus_ferr;
        uint16_t sysbus_nerr;
        uint8_t buf_ferr;
        uint8_t buf_nerr;
        uint16_t dram_ferr;
        uint16_t dram_nerr;
        uint32_t dram_sec1_add;
        uint32_t dram_sec2_add;
        uint16_t dram_sec1_syndrome;
        uint16_t dram_sec2_syndrome;
        uint32_t dram_ded_add;
        uint32_t dram_scrb_add;
        uint32_t dram_retr_add;
    } error_info;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;

    pci_device_reset(pdev);

    if (PCI_FUNC(pdev->devfn) == 1) {
        /* f1 registers (Error Reporting Device) */
        pci_set_long(pci_conf + E752X_FERR_GLOBAL, 0);
        pci_set_long(pci_conf + E752X_NERR_GLOBAL, 0);
        pci_set_byte(pci_conf + E752X_HI_FERR, 0);
        pci_set_byte(pci_conf + E752X_HI_NERR, 0);
        pci_set_word(pci_conf + E752X_SYSBUS_FERR, 0);
        pci_set_word(pci_conf + E752X_SYSBUS_NERR, 0);
        pci_set_byte(pci_conf + E752X_BUF_FERR, 0);
        pci_set_byte(pci_conf + E752X_BUF_NERR, 0);
        pci_set_word(pci_conf + E752X_DRAM_FERR, 0);
        pci_set_word(pci_conf + E752X_DRAM_NERR, 0);
        pci_set_byte(pci_conf + E752X_TOLM, 0x10); /* 4GB TOLM */
    } else {
        /* f0 registers (Control Device) */
        pci_set_byte(pci_conf + E752X_DEVPRES1, 0x20); /* bit 5 set to pass force_function_unhide check */
        pci_set_byte(pci_conf + E752X_DRB, 0x01);      /* Dummy memory size to populate csrows */
        pci_set_byte(pci_conf + E752X_DRB + 1, 0x01);
        pci_set_long(pci_conf + E752X_DRC, 0x00200000); /* drc_ddim = 2 (EDAC enabled) */
    }
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
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* The driver expects two functions: f0 (Control) and f1 (Error Reporting). 
     * We dynamically adjust the device ID and writable masks based on the function number. */
    if (PCI_FUNC(pdev->devfn) == 1) {
        pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_7520_1_ERR);
        
        /* Make error mask and SMI command registers writable */
        pdev->wmask[E752X_HI_ERRMASK] = 0xff;
        pdev->wmask[E752X_HI_SMICMD] = 0xff;
        pdev->wmask[E752X_SYSBUS_ERRMASK] = 0xff;
        pdev->wmask[E752X_SYSBUS_ERRMASK+1] = 0xff;
        pdev->wmask[E752X_SYSBUS_SMICMD] = 0xff;
        pdev->wmask[E752X_SYSBUS_SMICMD+1] = 0xff;
        pdev->wmask[E752X_BUF_ERRMASK] = 0xff;
        pdev->wmask[E752X_BUF_SMICMD] = 0xff;
        pdev->wmask[E752X_DRAM_ERRMASK] = 0xff;
        pdev->wmask[E752X_DRAM_SMICMD] = 0xff;
        
        /* Make error status registers writable (W1C behavior handled by driver) */
        pdev->wmask[E752X_FERR_GLOBAL] = 0xff;
        pdev->wmask[E752X_FERR_GLOBAL+1] = 0xff;
        pdev->wmask[E752X_FERR_GLOBAL+2] = 0xff;
        pdev->wmask[E752X_FERR_GLOBAL+3] = 0xff;
        
        pdev->wmask[E752X_NERR_GLOBAL] = 0xff;
        pdev->wmask[E752X_NERR_GLOBAL+1] = 0xff;
        pdev->wmask[E752X_NERR_GLOBAL+2] = 0xff;
        pdev->wmask[E752X_NERR_GLOBAL+3] = 0xff;
        
        pdev->wmask[E752X_HI_FERR] = 0xff;
        pdev->wmask[E752X_HI_NERR] = 0xff;
        
        pdev->wmask[E752X_SYSBUS_FERR] = 0xff;
        pdev->wmask[E752X_SYSBUS_FERR+1] = 0xff;
        pdev->wmask[E752X_SYSBUS_NERR] = 0xff;
        pdev->wmask[E752X_SYSBUS_NERR+1] = 0xff;
        
        pdev->wmask[E752X_BUF_FERR] = 0xff;
        pdev->wmask[E752X_BUF_NERR] = 0xff;
        
        pdev->wmask[E752X_DRAM_FERR] = 0xff;
        pdev->wmask[E752X_DRAM_FERR+1] = 0xff;
        pdev->wmask[E752X_DRAM_NERR] = 0xff;
        pdev->wmask[E752X_DRAM_NERR+1] = 0xff;
    } else {
        pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_7520_0);
        
        /* Make scrub rate and device presence registers writable */
        pdev->wmask[E752X_MCHSCRB] = 0xff;
        pdev->wmask[E752X_MCHSCRB+1] = 0xff;
        pdev->wmask[E752X_DEVPRES1] = 0xff;

        pdev->cap_present |= QEMU_PCI_CAP_MULTIFUNCTION;
        PCIDevice *f1 = pci_new(PCI_DEVFN(PCI_SLOT(pdev->devfn), 1), TYPE_PCIBASE_DEVICE);
        pci_realize_and_unref(f1, pci_get_bus(pdev), NULL);
    }

    /* BAR Initialization */
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
    .name = "e752x_edac_pci",
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
