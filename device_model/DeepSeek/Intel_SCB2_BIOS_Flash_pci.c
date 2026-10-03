/*
 * QEMU model for Intel SCB2 BIOS Flash PCI device.
 * Based on driver at drivers/mtd/maps/scb2_flash.c
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
#include "hw/pci/pci_ids.h"

#define PCI_VENDOR_ID_SERVERWORKS       0x1166
#define PCI_DEVICE_ID_SERVERWORKS_CSB5  0x0201

#define TYPE_PCIBASE_DEVICE "Intel_SCB2_BIOS_Flash_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID         PCI_VENDOR_ID_SERVERWORKS
#define DEVICE_ID         PCI_DEVICE_ID_SERVERWORKS_CSB5
#define CLASS_ID          0x0580   /* Memory controller, other */
#define CSB5_FCR          0x41
#define CSB5_FCR_DECODE_ALL 0x0e

#define SCB2_ADDR  0xfff00000
#define SCB2_WINDOW 0x00100000

struct PCIBaseState {
    PCIDevice parent_obj;

    uint8_t fcr;
    MemoryRegion container_mr;
    MemoryRegion flash_mr;
};

/* CFI query data for a 1MB x16 async flash chip */
static const uint8_t cfi_query[] = {
    [0x10] = 'Q', [0x11] = 'R', [0x12] = 'Y',
    [0x13] = 0, [0x14] = 0, [0x15] = 0, [0x16] = 0, [0x17] = 0, [0x18] = 0, [0x19] = 0, [0x1A] = 0,
    [0x1B] = 0x05, [0x1C] = 0x07, [0x1D] = 0x0a, [0x1E] = 0x0e,
    [0x1F] = 0x04, [0x20] = 0x04, [0x21] = 0x0a, [0x22] = 0x0c,
    [0x23] = 0x04, [0x24] = 0x04, [0x25] = 0x04, [0x26] = 0x04,
    [0x27] = 0x14,
    [0x28] = 0x02, [0x29] = 0x00,
    [0x2A] = 0x02,
    [0x2B] = 0x00,
    [0x2C] = 0x00,
    [0x2D] = 0x01,
    [0x2E] = 0x07, [0x2F] = 0x00,
    [0x30] = 0x00, [0x31] = 0x02,
};

static void scb2_update_decode(PCIBaseState *s)
{
    if (s->fcr & CSB5_FCR_DECODE_ALL) {
        memory_region_add_subregion(&s->container_mr, 0, &s->flash_mr);
    } else {
        memory_region_del_subregion(&s->container_mr, &s->flash_mr);
    }
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr == CSB5_FCR && len == 1) {
        return s->fcr;
    }
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr,
                                 uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Prevent relocation of BAR0 */
    if (addr >= PCI_BASE_ADDRESS_0 && addr < PCI_BASE_ADDRESS_0 + 4) {
        return;
    }

    if (addr == CSB5_FCR && len == 1) {
        s->fcr = val & 0xff;
        scb2_update_decode(s);
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->fcr = 0;
    scb2_update_decode(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize container and flash memory regions */
    memory_region_init(&s->container_mr, OBJECT(s), "scb2-container", SCB2_WINDOW);
    memory_region_init_ram(&s->flash_mr, OBJECT(s), "scb2-flash", SCB2_WINDOW, &error_fatal);

    /* Fill flash with 0xFF (erased state) and overlay CFI query data */
    memset(memory_region_get_ram_ptr(&s->flash_mr), 0xFF, SCB2_WINDOW);
    memcpy((uint8_t *)memory_region_get_ram_ptr(&s->flash_mr) + 0x10,
           cfi_query, sizeof(cfi_query));

    /* Register BAR0 as the container region and fix its base address */
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->container_mr);
    pci_set_long(pdev->config + PCI_BASE_ADDRESS_0,
                 SCB2_ADDR | PCI_BASE_ADDRESS_SPACE_MEMORY);

    s->fcr = 0;
    scb2_update_decode(s);
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

static const VMStateDescription vmstate_pcibase = {
    .name = "Intel_SCB2_BIOS_Flash_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(fcr, PCIBaseState),
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