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

#define TYPE_PCIBASE_DEVICE "uli526x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ULI526X_VENDOR_ID 0x10B9
#define ULI526X_DEVICE_ID 0x5261
#define ULI526X_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET
#define ULI526X_IO_SIZE   0x100

enum uli526x_offsets {
    DCR0 = 0x00, DCR1 = 0x08, DCR2 = 0x10, DCR3 = 0x18, DCR4 = 0x20,
    DCR5 = 0x28, DCR6 = 0x30, DCR7 = 0x38, DCR8 = 0x40, DCR9 = 0x48,
    DCR10 = 0x50, DCR11 = 0x58, DCR12 = 0x60, DCR13 = 0x68, DCR14 = 0x70,
    DCR15 = 0x78
};

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
    uint32_t dcr[16];

    /* DMA Context */
    dma_addr_t rx_base;
    dma_addr_t tx_base;

    uint8_t srom[128];
};

struct tx_desc {
    uint32_t tdes0, tdes1, tdes2, tdes3;
};
struct rx_desc {
    uint32_t rdes0, rdes1, rdes2, rdes3;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->dcr[5] & s->dcr[7]) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct tx_desc desc;
    int count = 0;
    
    while (count++ < 256) {
        pci_dma_read(pdev, s->tx_base, &desc, sizeof(desc));
        uint32_t tdes0 = le32_to_cpu(desc.tdes0);
        if (!(tdes0 & 0x80000000)) {
            break;
        }
        desc.tdes0 = cpu_to_le32(tdes0 & ~0x80000000);
        pci_dma_write(pdev, s->tx_base, &desc, sizeof(desc));
        s->tx_base = le32_to_cpu(desc.tdes3);
        s->dcr[5] |= 0x01;
    }
    
    if (s->dcr[5] & 0x01) {
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr > 0x78) return 0;
    int idx = addr / 8;

    switch (addr) {
        case 0x28: /* DCR5 */
            return s->dcr[5];
        case 0x48: /* DCR9 */
            /* Force SROM absent and PHY bit-bang to read 0xFFFF */
            return 0xffffffff;
        case 0x50: /* DCR10 */
            if (s->dcr[10] & 0x08000000) {
                /* Return ready bit + dummy PHY data to satisfy uli526x_sense_speed */
                return (s->dcr[10] & ~0xffff) | 0x10000000 | 0x024;
            }
            return s->dcr[10] | 0x10000000;
        case 0x70: /* DCR14 */
            if (s->dcr[13] == 0x1b0) {
                uint8_t mac[] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
                uint8_t val = mac[s->srom[0] % 6];
                s->srom[0]++;
                return val;
            }
            return s->dcr[14];
        default:
            return s->dcr[idx];
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr > 0x78) return;
    int idx = addr / 8;

    switch (addr) {
        case 0x00: /* DCR0 */
            s->dcr[0] = val;
            break;
        case 0x08: /* DCR1 */
            if (val == 1) {
                pcibase_do_dma(s, true);
            }
            break;
        case 0x18: /* DCR3 */
            s->rx_base = val;
            s->dcr[3] = val;
            break;
        case 0x20: /* DCR4 */
            s->tx_base = val;
            s->dcr[4] = val;
            break;
        case 0x28: /* DCR5 (W1C) */
            s->dcr[5] &= ~val;
            pcibase_update_irq(s);
            break;
        case 0x38: /* DCR7 */
            s->dcr[7] = val;
            pcibase_update_irq(s);
            break;
        case 0x50: /* DCR10 */
            s->dcr[10] = val;
            break;
        case 0x68: /* DCR13 */
            s->dcr[13] = val;
            break;
        case 0x70: /* DCR14 */
            s->dcr[14] = val;
            if (s->dcr[13] == 0x1c0 && val == 0x10) {
                s->srom[0] = 0; /* Reset MAC read index */
            }
            break;
        default:
            s->dcr[idx] = val;
            break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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
    memset(s->dcr, 0, sizeof(s->dcr));
    s->rx_base = 0;
    s->tx_base = 0;
    memset(s->srom, 0, sizeof(s->srom));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ULI526X_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ULI526X_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0].size = ULI526X_IO_SIZE;
    s->bar_info[0].name = "uli526x-mmio";
  
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
    .name = "uli526x_pci",
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
