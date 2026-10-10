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

#define TYPE_PCIBASE_DEVICE "ae4dma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMD 0x1022
#define AE4_DEVICE_ID 0x149B
#define MAX_AE4_HW_QUEUES 16
#define AE4_MAX_IDX_OFF 0x08
#define AE4_RD_IDX_OFF 0x0c
#define AE4_WR_IDX_OFF 0x10
#define AE4_Q_BASE_L_OFF 0x18
#define AE4_Q_BASE_H_OFF 0x1c
#define AE4_Q_SZ 0x20
#define AE4_DMA_VERSION 4

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
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
    bool int_en;
    uint32_t int_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t max_idx[MAX_AE4_HW_QUEUES];
    uint32_t rd_idx[MAX_AE4_HW_QUEUES];
    uint32_t wr_idx[MAX_AE4_HW_QUEUES];
    uint32_t q_base_l[MAX_AE4_HW_QUEUES];
    uint32_t q_base_h[MAX_AE4_HW_QUEUES];
    uint32_t q_sz[MAX_AE4_HW_QUEUES];

    /* DMA Context */
    dma_addr_t qbase_dma[MAX_AE4_HW_QUEUES];

    uint32_t q_status[MAX_AE4_HW_QUEUES];
    uint32_t q_int_status[MAX_AE4_HW_QUEUES];
    uint32_t cmd_error[MAX_AE4_HW_QUEUES];
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->int_en && s->int_status) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == 0) {
        return MAX_AE4_HW_QUEUES;
    }

    int q_idx = (addr / AE4_Q_SZ) - 1;
    if (q_idx >= 0 && q_idx < MAX_AE4_HW_QUEUES) {
        hwaddr q_off = addr % AE4_Q_SZ;
        switch (q_off) {
            case AE4_MAX_IDX_OFF:
                val = s->max_idx[q_idx];
                break;
            case AE4_RD_IDX_OFF:
                val = s->rd_idx[q_idx];
                break;
            case AE4_WR_IDX_OFF:
                val = s->wr_idx[q_idx];
                break;
            case AE4_Q_BASE_L_OFF:
                val = s->q_base_l[q_idx];
                break;
            case AE4_Q_BASE_H_OFF:
                val = s->q_base_h[q_idx];
                break;
            default:
                val = 0;
                break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == 0) {
        /* max_hw_q is written here */
        return;
    }

    int q_idx = (addr / AE4_Q_SZ) - 1;
    if (q_idx >= 0 && q_idx < MAX_AE4_HW_QUEUES) {
        hwaddr q_off = addr % AE4_Q_SZ;
        switch (q_off) {
            case AE4_MAX_IDX_OFF:
                s->max_idx[q_idx] = val;
                break;
            case AE4_RD_IDX_OFF:
                s->rd_idx[q_idx] = val;
                break;
            case AE4_WR_IDX_OFF:
                s->wr_idx[q_idx] = val;
                break;
            case AE4_Q_BASE_L_OFF:
                s->q_base_l[q_idx] = val;
                s->qbase_dma[q_idx] = (s->qbase_dma[q_idx] & 0xFFFFFFFF00000000ULL) | val;
                break;
            case AE4_Q_BASE_H_OFF:
                s->q_base_h[q_idx] = val;
                s->qbase_dma[q_idx] = (s->qbase_dma[q_idx] & 0x00000000FFFFFFFFULL) | ((uint64_t)val << 32);
                break;
            default:
                break;
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->int_en = false;
    s->int_status = 0;
    for (int i = 0; i < MAX_AE4_HW_QUEUES; i++) {
        s->max_idx[i] = 0;
        s->rd_idx[i] = 0;
        s->wr_idx[i] = 0;
        s->q_base_l[i] = 0;
        s->q_base_h[i] = 0;
        s->q_sz[i] = 0;
        s->qbase_dma[i] = 0;
        s->q_status[i] = 0;
        s->q_int_status[i] = 0;
        s->cmd_error[i] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1022 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x149B );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->has_msi = true;
    s->has_msix = true;

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* Default 4KB size */
    s->bar_info[0].name = "ae4dma-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].size > 0) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        /* Non-fatal */
    }
    if (msix_init(pdev, MAX_AE4_HW_QUEUES, &s->bar_regions[0], 0, 0x800, &s->bar_regions[0], 0, 0x900, 0, errp)) {
        /* Non-fatal */
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &PCIBASE_DEVICE(pdev)->bar_regions[0], &PCIBASE_DEVICE(pdev)->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ae4dma_pci",
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
