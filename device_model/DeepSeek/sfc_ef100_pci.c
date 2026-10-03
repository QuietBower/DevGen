/*
 * QEMU device model for Solarflare EF100 PCI NIC
 * Based on Linux driver: /home/eely/linux-7.1/drivers/net/ethernet/sfc/ef100.c
 * Phase 4: Debug Update - Added basic MMIO stubs for known registers; 
 *            probe still fails due to missing probe function details and register bit definitions.
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

#define TYPE_PCIBASE_DEVICE "sfc_ef100_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_XILINX 0x10ee
#define PCI_DEVICE_ID_EF100_PF 0x0100
#define PCI_CLASS_ID_NETWORK_ETHERNET 0x0200

/* Register offsets from supplementary source */
#define ER_GZ_MC_SFT_STATUS     0x00000010
#define ER_GZ_MC_DB_HWRD        0x00000024
#define ER_GZ_PARAMS_TLV_LEN    0x00000c00
#define ER_GZ_PARAMS_TLV        0x00000c04

/* BAR2 is the main MMIO BAR used by the driver.
 * Size of 128 KiB allows for the FCW window and other registers.
 */
#define BAR2_SIZE 0x20000

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_region;
};

static uint64_t bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ER_GZ_MC_SFT_STATUS:
        /* Return 0 for now, meaning firmware not ready. This likely causes probe failure.
         * We need the correct ready value from driver definitions.
         */
        val = 0x00000000;
        break;
    case ER_GZ_MC_DB_HWRD:
        /* Return a plausible hardware version, but we need the exact value expected. */
        val = 0x00010000;
        break;
    case ER_GZ_PARAMS_TLV_LEN:
        /* Length of parameter table; 0 means no table. Might be acceptable. */
        val = 0x00000000;
        break;
    case ER_GZ_PARAMS_TLV:
        /* Parameter table data; only valid if length non-zero. */
        val = 0x00000000;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "ef100: BAR2 read at offset 0x%"HWADDR_PRIx" size %u, returning 0\n",
                      addr, size);
        val = 0x00000000;
        break;
    }
    return val;
}

static void bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "ef100: BAR2 write at offset 0x%"HWADDR_PRIx" value 0x%"PRIx64" size %u (ignored)\n",
                  addr, val, size);
}

static const MemoryRegionOps bar2_ops = {
    .read = bar2_read,
    .write = bar2_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int pm_pos;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_XILINX);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_EF100_PF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCIe endpoint initialization */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Power management capability */
    pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Register BAR2 as an I/O region with stub handlers */
    memory_region_init_io(&s->bar_region, OBJECT(s), &bar2_ops, s, "sfc_ef100-bar2", BAR2_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_region);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSI-X were enabled, nothing special to do here. */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
}

static const VMStateDescription vmstate_pcibase = {
    .name = "sfc_ef100_pci",
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
