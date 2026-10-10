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

#define TYPE_PCIBASE_DEVICE "sata_nv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA 0x008e

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    
    /* Hardware Register Shadows */
    uint32_t adma_stat[2];
    uint32_t adma_ctl[2];
    uint32_t adma_cpb_count[2];
    uint32_t adma_next_cpb_idx[2];
    uint32_t adma_cpb_base_low[2];
    uint32_t adma_cpb_base_high[2];
    uint32_t adma_append[2];
    uint32_t adma_notifier[2];
    uint32_t adma_notifier_error[2];
    uint32_t adma_gen_ctl;
    uint32_t int_status_ck804;
    uint32_t int_enable_ck804;
    uint32_t int_status_mcp55;
    uint32_t int_enable_mcp55;
    uint32_t ctl_mcp55;
    uint32_t ncq_reg_mcp55[2];
    uint32_t mcp_sata_cfg_20;
};

#define NV_ADMA_DMA_BOUNDARY 0xffffffffUL
#define NV_ADMA_CHECK_INTR(GCTL, PORT) ((GCTL) & (1 << (19 + (12 * (PORT)))))

enum nv_adma_regbits {
    CMDEND  = (1 << 15),
    WNB     = (1 << 14),
    IGN     = (1 << 13),
    CS1n    = (1 << (4 + 8)),
    DA2     = (1 << (2 + 8)),
    DA1     = (1 << (1 + 8)),
    DA0     = (1 << (0 + 8)),
};

enum ncq_saw_flag_list {
    ncq_saw_d2h     = (1U << 0),
    ncq_saw_dmas    = (1U << 1),
    ncq_saw_sdb     = (1U << 2),
    ncq_saw_backout = (1U << 3),
};

struct nv_adma_prd {
    uint64_t addr;
    uint32_t len;
    uint8_t  flags;
    uint8_t  packet_len;
    uint16_t reserved;
};

struct nv_adma_cpb {
    uint8_t  resp_flags;
    uint8_t  reserved1;
    uint8_t  ctl_flags;
    uint8_t  len;
    uint8_t  tag;
    uint8_t  next_cpb_idx;
    uint16_t reserved2;
    uint16_t tf[12];
    struct nv_adma_prd aprd[5];
    uint64_t next_aprd;
    uint64_t reserved3;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->int_status_ck804 || s->int_status_mcp55) {
        if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        pci_set_irq(pdev, 0);
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

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

    s->adma_gen_ctl = 0;
    s->int_status_ck804 = 0;
    s->int_enable_ck804 = 0;
    s->int_status_mcp55 = 0;
    s->int_enable_mcp55 = 0;
    s->ctl_mcp55 = 0;
    s->mcp_sata_cfg_20 = 0;
    for (int i = 0; i < 2; i++) {
        s->adma_stat[i] = 0;
        s->adma_ctl[i] = 0;
        s->adma_cpb_count[i] = 0;
        s->adma_next_cpb_idx[i] = 0;
        s->adma_cpb_base_low[i] = 0;
        s->adma_cpb_base_high[i] = 0;
        s->adma_append[i] = 0;
        s->adma_notifier[i] = 0;
        s->adma_notifier_error[i] = 0;
        s->ncq_reg_mcp55[i] = 0;
    }
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NVIDIA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SATA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 6;
    s->has_msi = true;
    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
    }
      
    for (int i = 0; i < 5; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_PIO;
        s->bar_info[i].size = 8;
        s->bar_info[i].name = "pio_bar";
    }
    s->bar_info[5].index = 5;
    s->bar_info[5].type = BAR_TYPE_MMIO;
    s->bar_info[5].size = 4096;
    s->bar_info[5].name = "mmio_bar";

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

static const VMStateDescription vmstate_pcibase = {
    .name = "sata_nv_pci",
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
