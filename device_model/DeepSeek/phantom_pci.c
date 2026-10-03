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

#define TYPE_PCIBASE_DEVICE "phantom_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_PLX 0x10b5
#define PCI_DEVICE_ID_PLX_9050 0x9050
#define PCI_CLASS_BRIDGE_OTHER 0x0680
#define PHN_IRQCTL 0x4c
#define PHB_RUNNING 1
#define PHB_NOT_OH 2
#define PHN_CONTROL 0x6
#define PHN_CTL_IRQ 0x10
#define PHN_CTL_AMP 0x1

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    int num_bars;
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint32_t status;      /* Operational status flags */
    /* Additional BAR data buffers */
    uint8_t bar0_data[256];
    uint8_t bar2_data[256];
    uint8_t bar3_data[256];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    /* No-op: driver uses legacy IRQ, no automatic update handled here */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint8_t *data = opaque;
    uint64_t val = 0;
    /* Simple bounds check: our BAR data arrays are 256 bytes each */
    if (addr + size > 256) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: addr 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return 0;
    }
    switch (size) {
    case 1:
        val = data[addr];
        break;
    case 2:
        val = lduw_le_p(data + addr);
        break;
    case 4:
        val = ldl_le_p(data + addr);
        break;
    case 8:
        val = ldq_le_p(data + addr);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid size %u\n", __func__, size);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    uint8_t *data = opaque;
    if (addr + size > 256) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: addr 0x%" HWADDR_PRIx " size %u val 0x%" PRIx64 "\n",
                      __func__, addr, size, val);
        return;
    }
    switch (size) {
    case 1:
        data[addr] = val;
        break;
    case 2:
        stw_le_p(data + addr, val);
        break;
    case 4:
        stl_le_p(data + addr, val);
        break;
    case 8:
        stq_le_p(data + addr, val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid size %u\n", __func__, size);
        break;
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
    memset(s->bar0_data, 0, sizeof(s->bar0_data));
    memset(s->bar2_data, 0, sizeof(s->bar2_data));
    memset(s->bar3_data, 0, sizeof(s->bar3_data));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,   0x10b5);
    pci_set_word(pci_conf + PCI_DEVICE_ID,   0x9050);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0680);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: only BAR0, BAR2, BAR3 are used */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops,
                          s->bar0_data, "bar0", 256);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_mmio_ops,
                          s->bar2_data, "bar2", 256);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);

    memory_region_init_io(&s->bar_regions[3], OBJECT(s), &pcibase_mmio_ops,
                          s->bar3_data, "bar3", 256);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[3]);

    /* No MSI/MSI-X; driver uses legacy IRQ */
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
    .name = "phantom_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(bar0_data, PCIBaseState, 256),
        VMSTATE_UINT8_ARRAY(bar2_data, PCIBaseState, 256),
        VMSTATE_UINT8_ARRAY(bar3_data, PCIBaseState, 256),
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
