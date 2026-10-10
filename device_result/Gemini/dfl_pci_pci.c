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

#define TYPE_PCIBASE_DEVICE "dfl_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VSEC_ID_INTEL_DFLS 0x43
#define PCI_VNDR_DFLS_CNT 0x8
#define PCI_VNDR_DFLS_RES 0xc
#define PCIE_DEVICE_ID_PF_INT_5_X 0xBCBD
#define PCIE_DEVICE_ID_PF_INT_6_X 0xBCC0
#define PCIE_DEVICE_ID_PF_DSC_1_X 0x09C4
#define PCIE_DEVICE_ID_INTEL_PAC_N3000 0x0B30
#define PCIE_DEVICE_ID_INTEL_PAC_D5005 0x0B2B
#define PCIE_DEVICE_ID_SILICOM_PAC_N5010 0x1000
#define PCIE_DEVICE_ID_SILICOM_PAC_N5011 0x1001
#define PCIE_DEVICE_ID_INTEL_DFL 0xbcce
#define PCIE_SUBDEVICE_ID_INTEL_D5005 0x138d
#define PCIE_SUBDEVICE_ID_INTEL_N6000 0x1770
#define PCIE_SUBDEVICE_ID_INTEL_N6001 0x1771
#define PCIE_SUBDEVICE_ID_INTEL_C6100 0x17d4
#define PCIE_DEVICE_ID_VF_INT_5_X 0xBCBF
#define PCIE_DEVICE_ID_VF_INT_6_X 0xBCC1
#define PCIE_DEVICE_ID_VF_DSC_1_X 0x09C5
#define PCIE_DEVICE_ID_INTEL_PAC_D5005_VF 0x0B2C
#define PCIE_DEVICE_ID_INTEL_DFL_VF 0xbccf
#define FME_HDR_CAP 0x30
#define MAX_DFL_FPGA_PORT_NUM 4
#define DFH_TYPE_FIU 4
#define DFH 0x0
#define DFH_ID_FIU_PORT 1
#define DFH_ID_FIU_FME 0
#define DFH_SIZE 0x8
#define DFH_TYPE_PRIVATE 3
#define DFH_TYPE_AFU 1
#define FME_PORT_OFST_ACC_PF 0
#define FME_FEATURE_ID_HEADER 0xfe
#define FME_PORT_OFST_ACC_VF 1
#define NEXT_AFU 0x18
#define FEATURE_DEV_ID_UNUSED (-1)
#define FEATURE_ID_FIU_HEADER 0xfe
#define PORT_HDR_CAP 0x30
#define FEATURE_ID_AFU 0xff
#define PORT_FEATURE_ID_UINT 0x12
#define FME_FEATURE_ID_GLOBAL_ERR 0x4
#define FME_ERROR_CAP 0x70
#define PORT_UINT_CAP 0x8
#define PORT_ERROR_CAP 0x38
#define PORT_FEATURE_ID_ERROR 0x10

/* Newly added macros from driver source */
#define DFH_TYPE                (0xFULL << 60)
#define DFH_ID                  (0xFFFULL << 0)
#define FME_CAP_NUM_PORTS       (0x7ULL << 17)
#define FME_HDR_PORT_OFST(n)    (0x38 + ((n) * 0x8))
#define FME_PORT_OFST_IMP       (1ULL << 60)
#define FME_PORT_OFST_BAR_ID    (0x7ULL << 32)
#define FME_PORT_OFST_DFH_OFST  (0xFFFFFFULL << 0)
#define FME_PORT_OFST_BAR_SKIP  7
#define PCI_VNDR_DFLS_RES_BAR_MASK 0x7
#define PCI_VNDR_DFLS_RES_OFF_MASK 0xFFFFFFF8

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

    /* Registers */
    uint64_t dfh_reg;
    uint64_t fme_hdr_cap_reg;
    uint64_t fme_port_ofst_reg[MAX_DFL_FPGA_PORT_NUM];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DFH:
        val = s->dfh_reg;
        break;
    case FME_HDR_CAP:
        val = s->fme_hdr_cap_reg;
        break;
    case FME_HDR_PORT_OFST(0):
        val = s->fme_port_ofst_reg[0];
        break;
    case FME_HDR_PORT_OFST(1):
        val = s->fme_port_ofst_reg[1];
        break;
    case FME_HDR_PORT_OFST(2):
        val = s->fme_port_ofst_reg[2];
        break;
    case FME_HDR_PORT_OFST(3):
        val = s->fme_port_ofst_reg[3];
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Registers are read-only in this driver snippet */
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

    /* 
     * Initialize registers based on driver expectations.
     * DFH_TYPE_FIU (4) and DFH_ID_FIU_FME (0)
     */
    s->dfh_reg = ((uint64_t)DFH_TYPE_FIU << 60) | ((uint64_t)DFH_ID_FIU_FME);
    s->fme_hdr_cap_reg = (1ULL << 17); /* FME_CAP_NUM_PORTS = 1 */

    for (int i = 0; i < MAX_DFL_FPGA_PORT_NUM; i++) {
        s->fme_port_ofst_reg[i] = 0;
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIE_DEVICE_ID_PF_INT_5_X );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Add Vendor Specific Extended Capability (VSEC) for DFL */
    uint16_t vsec_offset = 0x100;
    pcie_add_capability(pdev, PCI_EXT_CAP_ID_VNDR, 1, vsec_offset, 0x10);
    pci_set_long(pci_conf + vsec_offset + 0x4, (PCI_VSEC_ID_INTEL_DFLS << 16) | 0x10);
    pci_set_long(pci_conf + vsec_offset + PCI_VNDR_DFLS_CNT, 1);
    pci_set_long(pci_conf + vsec_offset + PCI_VNDR_DFLS_RES, 0); /* BAR 0, Offset 0 */

    /* Setup BARs */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x1000, "dfl-fme"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_MMIO, 0x1000, "dfl-msix"};

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI-X */
    if (msix_init(pdev, 4, &s->bar_regions[1], 1, 0, &s->bar_regions[1], 1, 0x800, 0, errp)) {
        return;
    }
    s->has_msix = true;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &PCIBASE_DEVICE(pdev)->bar_regions[1], &PCIBASE_DEVICE(pdev)->bar_regions[1]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "dfl_pci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT64(dfh_reg, PCIBaseState),
        VMSTATE_UINT64(fme_hdr_cap_reg, PCIBaseState),
        VMSTATE_UINT64_ARRAY(fme_port_ofst_reg, PCIBaseState, MAX_DFL_FPGA_PORT_NUM),
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
