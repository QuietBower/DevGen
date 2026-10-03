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

#define TYPE_PCIBASE_DEVICE "pata_it821x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ITE 0x1283
#define PCI_DEVICE_ID_ITE_8211 0x8211

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t ata_status;
    uint8_t ata_device;
    uint8_t ata_ctl;
    
    /* Firmware Command Emulation State */
    bool fw_cmd_active;
    uint32_t fw_cmd_index;
    uint8_t fw_buf[512];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used by this driver, relies on PIO */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used by this driver, relies on PIO */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Logic for Port I/O (Legacy support) */
    if (addr == 7 && size == 1) { /* status_addr / command_addr */
        if (s->fw_cmd_active) {
            return 0x08; /* ATA_DRQ */
        }
        return 0x00; /* Idle */
    }
    
    if (addr == 0 && size == 2) { /* data_addr */
        if (s->fw_cmd_active && s->fw_cmd_index < 512) {
            val = s->fw_buf[s->fw_cmd_index] | (s->fw_buf[s->fw_cmd_index + 1] << 8);
            s->fw_cmd_index += 2;
            if (s->fw_cmd_index >= 512) {
                s->fw_cmd_active = false;
            }
            return val;
        }
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Logic for Port I/O (Legacy support) */
    if (addr == 7 && size == 1) { /* command_addr */
        if (val == 0xFA) { /* Firmware command */
            s->fw_cmd_active = true;
            s->fw_cmd_index = 0;
        }
    }
    if (addr == 6 && size == 1) { /* device_addr */
        s->ata_device = val;
    }
    if (addr == 2 && size == 1) { /* ctl_addr */
        s->ata_ctl = val;
    }
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

    /* Revert registers to power-on defaults */
    s->ata_status = 0;
    s->ata_device = 0;
    s->ata_ctl = 0;
    s->fw_cmd_active = false;
    s->fw_cmd_index = 0;
    memset(s->fw_buf, 0, sizeof(s->fw_buf));
    
    /* Populate dummy firmware buffer for 0xFA command */
    s->fw_buf[505] = 0x01; /* Firmware version */
    s->fw_buf[506] = 0x02;
    s->fw_buf[507] = 0x03;
    s->fw_buf[508] = 0x04;
    
    for (int i = 0; i < 4; i++) {
        s->fw_buf[128 * i + 52] = 5; /* 5 = No Disk */
    }

    /* Default to Smart mode (bit 0 = 1) */
    pci_set_byte(PCI_DEVICE(dev)->config + 0x50, 0x01);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ITE );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ITE_8211 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE );
    pci_config_set_prog_interface(pci_conf, 0x8f); /* Native mode */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Make specific config bytes writable for driver state storage */
    pdev->wmask[0x40] = 0xFF;
    pdev->wmask[0x41] = 0xFF;
    pdev->wmask[0x42] = 0xFF;
    pdev->wmask[0x4C] = 0xFF;
    pdev->wmask[0x4D] = 0xFF;
    pdev->wmask[0x4E] = 0xFF;
    pdev->wmask[0x4F] = 0xFF;
    pdev->wmask[0x50] = 0xFF;
    pdev->wmask[0x54] = 0xFF;
    pdev->wmask[0x55] = 0xFF;
    pdev->wmask[0x56] = 0xFF;
    pdev->wmask[0x57] = 0xFF;
    pdev->wmask[0x5E] = 0xFF;

    /* BAR Initialization */
    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 8, "ata-io0"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_PIO, 4, "ata-ctl0"};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_PIO, 8, "ata-io1"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_PIO, 4, "ata-ctl1"};
    s->bar_info[4] = (BARInfo){4, BAR_TYPE_PIO, 16, "bmdma"};
      
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
    .name = "pata_it821x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(ata_status, PCIBaseState),
        VMSTATE_UINT8(ata_device, PCIBaseState),
        VMSTATE_UINT8(ata_ctl, PCIBaseState),
        VMSTATE_BOOL(fw_cmd_active, PCIBaseState),
        VMSTATE_UINT32(fw_cmd_index, PCIBaseState),
        VMSTATE_UINT8_ARRAY(fw_buf, PCIBaseState, 512),
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
