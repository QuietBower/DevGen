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

#define TYPE_PCIBASE_DEVICE "ie31200_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_IE31200_HB_1 0x0108

#define IE31200_MCHBAR_LOW 0x48
#define IE31200_MCHBAR_HIGH 0x4c
#define IE31200_ERRSTS 0xc8
#define IE31200_ERRSTS_UE (1 << 1)
#define IE31200_ERRSTS_CE (1 << 0)
#define IE31200_ERRSTS_BITS (IE31200_ERRSTS_UE | IE31200_ERRSTS_CE)
#define IE31200_CAPID0 0xe4
#define IE31200_CAPID0_PDCD (1 << 4)
#define IE31200_CAPID0_DDPCD (1 << 6)
#define IE31200_CAPID0_ECC (1 << 1)

#define IE31200_RANKS_PER_CHANNEL 8
#define IE31200_DIMMS_PER_CHANNEL 2
#define IE31200_CHANNELS 2
#define IE31200_IMC_NUM 2

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
    uint32_t mchbar_low;
    uint32_t mchbar_high;
    uint16_t errsts;
    uint32_t capid0;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (addr) {
        case 0x5004: /* snb_cfg mad_dimm_offset[0] */
        case 0x5008: /* snb_cfg mad_dimm_offset[1] */
        case 0x500c: /* skl_cfg mad_dimm_offset[0] */
        case 0x5010: /* skl_cfg mad_dimm_offset[1] */
        case 0xd80c: /* rpl_s_cfg mad_dimm_offset[0] */
        case 0xd810: /* rpl_s_cfg mad_dimm_offset[1] */
            /* Return size=1, ranks=1, width=DEV_X8 to populate DIMMs */
            val = 0x00000101;
            break;
        case 0x40c8: /* snb_cfg eccerrlog[0] */
        case 0x44c8: /* snb_cfg eccerrlog[1] */
        case 0x4048: /* skl_cfg eccerrlog[0] */
        case 0x4448: /* skl_cfg eccerrlog[1] */
        case 0xe048: /* rpl_s_cfg eccerrlog[0] */
        case 0xe848: /* rpl_s_cfg eccerrlog[1] */
            /* Return 0 to indicate no ECC errors */
            val = 0;
            break;
        default:
            val = 0;
            break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Driver does not write to MMIO space */
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

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    /* Intercept MCHBAR reads to return the dynamically assigned BAR 0 address */
    if (addr == IE31200_MCHBAR_LOW && len == 4) {
        return pci_default_read_config(pdev, 0x10, 4);
    }
    if (addr == IE31200_MCHBAR_HIGH && len == 4) {
        return 0; /* 32-bit BAR, high part is 0 */
    }
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    /* Handle Write-1-to-Clear (W1C) for ERRSTS */
    if (addr == IE31200_ERRSTS && len == 2) {
        uint16_t current = pci_default_read_config(pdev, addr, len);
        uint16_t w1c_mask = val & IE31200_ERRSTS_BITS;
        current &= ~w1c_mask;
        pci_default_write_config(pdev, addr, current, len);
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_reset(DeviceState *dev)
{
    pci_device_reset(PCI_DEVICE(dev));

    uint8_t *pci_conf = PCI_DEVICE(dev)->config;
    pci_set_word(pci_conf + IE31200_ERRSTS, 0);
    
    /* CAPID0: PDCD=0, DDPCD=0, ECC=0 to enable all features */
    pci_set_long(pci_conf + IE31200_CAPID0, 0);
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_IE31200_HB_1 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST );
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
    s->bar_info[0].size = 0x10000; /* 64KB to cover all configs */
    s->bar_info[0].name = "mchbar";
    
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
    .name = "ie31200_edac_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
