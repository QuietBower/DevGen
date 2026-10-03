/*
 * QEMU emulation of Cavium Thunder NIC PF
 * Based on Linux driver: drivers/net/ethernet/cavium/thunder/nic_main.c
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
#include "hw/pci/pcie_sriov.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

typedef enum {
    BAR_TYPE_NONE,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct BARInfo {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
} BARInfo;

#define TYPE_PCIBASE_DEVICE "nicpf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define PCI_DEVICE_ID_THUNDER_NIC_PF 0xA01E
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* BAR0 size: large enough to cover all register offsets used by the driver */
#define NIC_PF_BAR0_SIZE (16 * 1024 * 1024) /* 16 MiB, power of 2 */

#define NIC_PF_MAILBOX_INT    (0x0410)
#define NIC_PF_MAILBOX_ENA_W1S (0x0470)

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Shadow registers: flat backing store for all MMIO registers */
    uint64_t *shadow_regs;
    hwaddr bar0_size;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t int0, int1;

    int0 = s->shadow_regs[NIC_PF_MAILBOX_INT >> 3] &
           s->shadow_regs[NIC_PF_MAILBOX_ENA_W1S >> 3];
    if (int0) {
        msix_notify(pdev, 8);
    }
    int1 = s->shadow_regs[(NIC_PF_MAILBOX_INT + 8) >> 3] &
           s->shadow_regs[(NIC_PF_MAILBOX_ENA_W1S + 8) >> 3];
    if (int1) {
        msix_notify(pdev, 9);
    }
}

/* MMIO handlers: driver uses 64-bit relaxed accesses */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= s->bar0_size || size != 8) {
        return ~0ULL;
    }
    return s->shadow_regs[addr >> 3];
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= s->bar0_size || size != 8) {
        return;
    }
    switch (addr) {
    case NIC_PF_MAILBOX_INT:          /* W1C */
        s->shadow_regs[addr >> 3] &= ~val;
        pcibase_update_irq(s);
        break;
    case NIC_PF_MAILBOX_INT + 8:      /* W1C */
        s->shadow_regs[(addr) >> 3] &= ~val;
        pcibase_update_irq(s);
        break;
    case NIC_PF_MAILBOX_ENA_W1S:      /* W1S */
        s->shadow_regs[addr >> 3] |= val;
        pcibase_update_irq(s);
        break;
    case NIC_PF_MAILBOX_ENA_W1S + 8:  /* W1S */
        s->shadow_regs[addr >> 3] |= val;
        pcibase_update_irq(s);
        break;
    default:
        s->shadow_regs[addr >> 3] = val;
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 8, .max_access_size = 8 },
    .impl  = { .min_access_size = 8, .max_access_size = 8 },
};

/* PIO not used by driver, removed due to unused warning */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->shadow_regs, 0, s->bar0_size);
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0xA01E);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x10); /* pass2 silicon */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Subsystem IDs: emulate 88XX variant (first in nic_get_hw_info switch) */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0xA11E); /* PCI_SUBSYS_DEVID_88XX_NIC_PF */

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Add SRIOV capability to allow driver to read total VFs */
    /* pcie_sriov_init(pdev, 0x100, 0xa01e, 0x177d, 0, 8, 0, 0); */

    /* BAR0: MMIO for registers */
    s->num_bars = 1;
    s->bar0_size = NIC_PF_BAR0_SIZE;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = NIC_PF_BAR0_SIZE,
        .name = "nicpf-mmio",
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Allocate shadow register array */
    s->shadow_regs = g_malloc0(s->bar0_size);

    /* MSI-X initialization */
    if (msix_init(pdev, 10, &s->bar_regions[0], 0, 0,
                  &s->bar_regions[0], 0, 0, 0, errp) < 0) {
        error_setg(errp, "Failed to init MSI-X");
        return;
    }

    s->has_msix = true;
    s->has_msi = false;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    g_free(s->shadow_regs);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "nicpf_pci",
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
