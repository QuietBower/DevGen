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
#define VENDOR_ID 0x8086
#define DEVICE_ID 0xBCBD
#define CLASS_ID  0xff0000

#define DRV_NAME	"dfl-pci"
#define DRV_VERSION	"0.8"
#define PCI_VSEC_ID_INTEL_DFLS 0x43
#define PCI_VNDR_DFLS_CNT 0x8
#define PCI_VNDR_DFLS_RES 0xc
#define GENMASK(h, l) (((1ULL << ((h) - (l) + 1)) - 1) << (l))
#define GENMASK_ULL GENMASK
#define PCI_VNDR_DFLS_RES_BAR_MASK GENMASK(2, 0)
#define PCI_VNDR_DFLS_RES_OFF_MASK GENMASK(31, 3)
#define PCIE_DEVICE_ID_PF_INT_5_X		0xBCBD
#define PCIE_DEVICE_ID_PF_INT_6_X		0xBCC0
#define PCIE_DEVICE_ID_PF_DSC_1_X		0x09C4
#define PCIE_DEVICE_ID_INTEL_PAC_N3000		0x0B30
#define PCIE_DEVICE_ID_INTEL_PAC_D5005		0x0B2B
#define PCIE_DEVICE_ID_SILICOM_PAC_N5010	0x1000
#define PCIE_DEVICE_ID_SILICOM_PAC_N5011	0x1001
#define PCIE_DEVICE_ID_INTEL_DFL		0xbcce
#define PCIE_SUBDEVICE_ID_INTEL_D5005		0x138d
#define PCIE_SUBDEVICE_ID_INTEL_N6000		0x1770
#define PCIE_SUBDEVICE_ID_INTEL_N6001		0x1771
#define PCIE_SUBDEVICE_ID_INTEL_C6100		0x17d4
#define PCIE_DEVICE_ID_VF_INT_5_X		0xBCBF
#define PCIE_DEVICE_ID_VF_INT_6_X		0xBCC1
#define PCIE_DEVICE_ID_VF_DSC_1_X		0x09C5
#define PCIE_DEVICE_ID_INTEL_PAC_D5005_VF	0x0B2C
#define PCIE_DEVICE_ID_INTEL_DFL_VF		0xbccf

#define FME_CAP_NUM_PORTS	GENMASK_ULL(19, 17)
#define FME_HDR_PORT_OFST(n)	(0x38 + ((n) * 0x8))
#define FME_PORT_OFST_DFH_OFST	GENMASK_ULL(23, 0)
#define FME_PORT_OFST_BAR_SKIP	7
#define FME_PORT_OFST_BAR_ID	GENMASK_ULL(34, 32)
#define FME_HDR_CAP		0x30
#define MAX_DFL_FPGA_PORT_NUM 4
#define FME_PORT_OFST_IMP	BIT_ULL(60)
#define DFH_TYPE_FIU		4
#define DFH			0x0
#define DFH_ID_FIU_PORT		1
#define DFH_ID			GENMASK_ULL(11, 0)
#define DFH_TYPE		GENMASK_ULL(63, 60)
#define DFH_ID_FIU_FME		0
#define DFH_SIZE		0x8
#define DFH_EOL			BIT_ULL(40)
#define DFH_NEXT_HDR_OFST	GENMASK_ULL(39, 16)
#define DFH_TYPE_PRIVATE	3
#define DFH_TYPE_AFU		1
#define FME_PORT_OFST_ACC_PF	0
#define FEATURE_ID_FIU_HEADER		0xfe
#define FME_PORT_OFST_ACC_VF	1
#define FME_PORT_OFST_ACC_CTRL	BIT_ULL(55)
#define NEXT_AFU		0x18
#define NEXT_AFU_NEXT_DFH_OFST	GENMASK_ULL(23, 0)
#define FEATURE_DEV_ID_UNUSED	(-1)
#define PORT_HDR_CAP		0x30
#define FEATURE_ID_AFU			0xff
#define PORT_CAP_MMIO_SIZE	GENMASK_ULL(23, 8)
#define DFH_VERSION		GENMASK_ULL(59, 52)
#define DFH_REVISION		GENMASK_ULL(15, 12)
#define PORT_FEATURE_ID_UINT		0x12
#define FME_FEATURE_ID_GLOBAL_ERR	0x4
#define PORT_ERROR_CAP_INT_VECT	GENMASK_ULL(12, 1)
#define FME_ERROR_CAP_SUPP_INT	BIT_ULL(0)
#define PORT_ERROR_CAP_SUPP_INT	BIT_ULL(0)
#define FME_ERROR_CAP		0x70
#define FME_ERROR_CAP_INT_VECT	GENMASK_ULL(12, 1)
#define PORT_UINT_CAP		0x8
#define PORT_ERROR_CAP		0x38
#define PORT_UINT_CAP_FST_VECT	GENMASK_ULL(23, 12)
#define PORT_FEATURE_ID_ERROR		0x10
#define PORT_UINT_CAP_INT_NUM	GENMASK_ULL(11, 0)

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t _reserved; /* placeholder for future register shadow */

    /* Operational status flags */
    uint32_t status;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
}

/* MMIO Read Handler for BAR0 (FME feature list) */
static uint64_t pcibase_mmio_read_fme(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (addr) {
    case 0x00:
        /* FME Device Feature Header: type=FIU, id=FME, next=0x28 */
        val = (4ULL << 60) | (0x28ULL << 16) | 0;
        break;
    case 0x08:
    case 0x10:
    case 0x18:
    case 0x20:
        val = 0;
        break;
    case 0x28:
        /* FME Header Feature DFH: type=3, id=0xfe, next=0x1000 */
        val = (3ULL << 60) | (0x1000ULL << 16) | 0xfe;
        break;
    case 0x30:
        /* FME_HDR_CAP: NUM_PORTS=1 */
        val = (1ULL << 17);
        break;
    case 0x38:
        /* FME_HDR_PORT_OFST(0): IMP=1, BAR_ID=1, DFH_OFST=0 */
        val = (1ULL << 60) | (1ULL << 32) | 0x0;
        break;
    case 0x40:
        /* FME_HDR_PORT_OFST(1): IMP=0 */
        val = 0;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write_fme(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops_fme = {
    .read = pcibase_mmio_read_fme,
    .write = pcibase_mmio_write_fme,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* MMIO Read Handler for BAR1 (Port) */
static uint64_t pcibase_mmio_read_port(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (addr) {
    case 0x00:
        /* Port DFH: type=FIU, id=1, eol=1 */
        val = (4ULL << 60) | (1ULL << 40) | 1ULL;
        break;
    case 0x08:
    case 0x10:
    case 0x18:
    case 0x20:
        val = 0;
        break;
    case 0x30:
        /* PORT_HDR_CAP: MMIO size = 0xff000 bytes */
        val = 0xff000;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write_port(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops_port = {
    .read = pcibase_mmio_read_port,
    .write = pcibase_mmio_write_port,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    pci_device_reset(PCI_DEVICE(dev));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp,
                                 const MemoryRegionOps *ops)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xff00);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR0: FME feature list, small MMIO */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar0-fme" };
    /* BAR1: Port memory, large MMIO */
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 1 * MiB, .name = "bar1-port" };
    /* BAR2: MSI-X table */
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM, .size = 8 * KiB, .name = "bar2-msix" };

    s->num_bars = 0;
    for (int i = 0; i < 6; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            const MemoryRegionOps *ops = NULL;
            if (i == 0) {
                ops = &pcibase_mmio_ops_fme;
            } else if (i == 1) {
                ops = &pcibase_mmio_ops_port;
            } else {
                /* RAM BARs don't need ops; pass NULL */
            }
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp, ops);
            s->num_bars++;
        }
    }

    /* MSI-X initialization */
    if (msix_init_exclusive_bar(pdev, 4, 2, errp)) {
        return;
    }

    /* VSEC capability for DFL discovery */
    int cap_off = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, 16, errp);
    if (cap_off < 0) {
        return;
    }
    /* Set VSEC ID and Rev */
    pci_set_word(pci_conf + cap_off + 4, 0x43); /* VSEC ID (Intel 0x8086. 0x43 low) */
    pci_set_word(pci_conf + cap_off + 6, 0x00);   /* VSEC Rev */
    /* dfl_cnt = 1 */
    pci_set_long(pci_conf + cap_off + 8, 1);
    /* dfl_res[0] = 0 (BAR0, offset0) */
    pci_set_long(pci_conf + cap_off + 12, 0);
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
    .name = "dfl_pci_pci",
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
