/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
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

#define TYPE_PCIBASE_DEVICE "octeon_ep_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define OCTEP_PCI_DEVICE_ID_CN98_PF 0xB100
#define VENDOR_ID PCI_VENDOR_ID_CAVIUM
#define DEVICE_ID OCTEP_PCI_DEVICE_ID_CN98_PF
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

/* Known register offsets from driver */
#define CN93_SDP_MAC_NUMBER  0x2C100

/* PCI window registers offsets (unknown - need definition from hardware docs) */
/* #define PCI_WIN_WR_ADDR_OFFSET   ??? */
/* #define PCI_WIN_RD_ADDR_OFFSET   ??? */
/* #define PCI_WIN_WR_DATA_OFFSET   ??? */
/* #define PCI_WIN_RD_DATA_OFFSET   ??? */

/* Note: Register offsets and BAR sizes are not fully defined in provided source;
 * required for mmio handlers and BAR initialization. */

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
    /* PCI window registers for indirect CSR access */
    uint64_t pci_win_wr_addr;
    uint64_t pci_win_rd_addr;
    uint64_t pci_win_wr_data;  /* Written by host, triggers window write */
    uint64_t pci_win_rd_data;  /* Read by host, contains window read result */

    /* DMA Context */
    /* Not explicitly used in driver; removed. */

    /* Status_Stru removed; not applicable. */
    /* Probe_Reset_Stru removed; not applicable. */
    /* Pow_Man_Stru removed; not applicable. */
    /* Other_Addition_Info_Stru removed; not applicable. */
};

/* Other_Addition_Info_Defin removed; not applicable. */

/* Internal helper for status-triggered signaling removed; not applicable. */

/* Device-initiated DMA logic removed; not applicable. */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CN93_SDP_MAC_NUMBER:
        /* TODO: Return actual MAC number; for now return 0 */
        val = 0;
        break;
    /* PCI window read data register */
    /* case PCI_WIN_RD_DATA_OFFSET: */
    /*     val = s->pci_win_rd_data; */
    /*     break; */
    default:
        /* All other registers return 0 unless explicitly implemented */
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    /* PCI window write address register */
    /* case PCI_WIN_WR_ADDR_OFFSET: */
    /*     s->pci_win_wr_addr = val; */
    /*     break; */
    /* PCI window read address register */
    /* case PCI_WIN_RD_ADDR_OFFSET: */
    /*     s->pci_win_rd_addr = val; */
    /*     break; */
    /* PCI window write data register */
    /* case PCI_WIN_WR_DATA_OFFSET: */
    /*     s->pci_win_wr_data = val; */
    /*     todo: perform actual write to windowed space */
    /*     break; */
    default:
        /* Other registers are ignored until defined */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Not used by driver; returning 0. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Not used by driver. */
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

    /* No reset-specific register values to apply yet. */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
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

    /* Add Vendor-Specific Extended Capability for firmware ready status */
    {
        uint8_t *ecap = pdev->config + 0x100; /* Start of extended config space */
        /* PCI Express Extended Capability Header */
        pci_set_long(ecap + 0x00, 0x0000000B); /* Cap ID = 0x000B, Next ptr = 0, Version = 0 */
        /* Vendor-Specific Header: VSEC ID = 0xA3, Rev = 0, Length = 12 bytes */
        pci_set_long(ecap + 0x04, (0x0C << 20) | 0x00A3);
        /* Status byte at offset 8: FW_STATUS_READY = 1 */
        ecap[0x08] = 1;
        /* Zero remaining bytes for padding */
        memset(ecap + 0x09, 0, 0x03);
    }

    /* BAR Initialization: BAR0, BAR2, BAR4 for MMIO */
    s->num_bars = 3;
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops, s,
                          "octeon_ep-mmio0", 0x40000); /* 256KB to cover known offsets */
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->bar_regions[0]);

    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_mmio_ops, s,
                          "octeon_ep-mmio2", 0x10000); /* 64KB for PF mailbox etc. */
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->bar_regions[2]);

    memory_region_init_io(&s->bar_regions[4], OBJECT(s), &pcibase_mmio_ops, s,
                          "octeon_ep-mmio4", 0x10000); /* 64KB for additional region */
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->bar_regions[4]);

    /* MSI-X initialization: requires BAR and number of vectors */
    /* s->has_msix = true; */
    /* msix_init(pdev, vectors, &s->bar_regions[bar_idx], 0, &s->bar_regions[bar_idx], bar_idx, 0); */
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

    /* No additional cleanup required yet. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "octeon_ep_pci",
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
