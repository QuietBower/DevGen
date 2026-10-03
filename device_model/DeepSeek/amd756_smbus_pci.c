/*
 * QEMU model for AMD756 SMBus controller
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

#define TYPE_PCIBASE_DEVICE "amd756_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1022
#define DEVICE_ID 0x740B
#define CLASS_ID  0x0C05

#define SMB_IOSIZE 16

/* SMBus register offsets (relative to base address) */
#define SMB_GLOBAL_STATUS    0x0
#define SMB_GLOBAL_ENABLE    0x2
#define SMB_HOST_ADDRESS     0x4
#define SMB_HOST_DATA        0x6
#define SMB_HOST_COMMAND     0x8
#define SMB_HOST_BLOCK_DATA  0x9
#define SMB_HAS_DATA         0xA
#define SMB_HAS_DEVICE_ADDRESS 0xC
#define SMB_HAS_HOST_ADDRESS 0xE
#define SMB_SNOOP_ADDRESS    0xF

/* Global Status register bits */
#define GS_ABRT_STS   (1 << 0)
#define GS_COL_STS    (1 << 1)
#define GS_PRERR_STS  (1 << 2)
#define GS_HST_STS    (1 << 3)
#define GS_HCYC_STS   (1 << 4)
#define GS_TO_STS     (1 << 5)
#define GS_SMB_STS    (1 << 11)
#define GS_CLEAR_STS  (GS_ABRT_STS | GS_COL_STS | GS_PRERR_STS | \
                       GS_HCYC_STS | GS_TO_STS)

/* Global Enable bits */
#define GE_CYC_TYPE_MASK  (7)
#define GE_HOST_STC       (1 << 3)
#define GE_ABORT          (1 << 5)

/* PCI config register offsets (updated from new driver source) */
#define SMBGCFG         0x41
#define SMBBA           0x58
#define SMBBANFORCE     0x14
#define SMBREV          0x08
#define SMB_ADDR_OFFSET 0xE0

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
    struct {
        uint16_t global_status;
        uint16_t global_enable;
        uint16_t host_address;       /* corrected to word */
        uint16_t host_data;
        uint8_t  host_command;
        uint8_t  host_block_data;
        uint16_t has_data;
        uint16_t has_device_address;
        uint16_t has_host_address;
        uint8_t  snoop_address;
    } smb_regs;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case 0x00:
        if (size == 2) return s->smb_regs.global_status;
        else if (size == 1) return s->smb_regs.global_status & 0xFF;
        else if (size == 4) return s->smb_regs.global_status;
        break;
    case 0x01:
        if (size == 1) return (s->smb_regs.global_status >> 8) & 0xFF;
        break;
    case 0x02:
        if (size == 2) return s->smb_regs.global_enable;
        else if (size == 1) return s->smb_regs.global_enable & 0xFF;
        break;
    case 0x03:
        if (size == 1) return (s->smb_regs.global_enable >> 8) & 0xFF;
        break;
    case 0x04:
        if (size == 2) return s->smb_regs.host_address;
        else if (size == 1) return s->smb_regs.host_address & 0xFF;
        break;
    case 0x05:
        if (size == 1) return (s->smb_regs.host_address >> 8) & 0xFF;
        break;
    case 0x06:
        if (size == 2) return s->smb_regs.host_data;
        else if (size == 1) return s->smb_regs.host_data & 0xFF;
        break;
    case 0x07:
        if (size == 1) return (s->smb_regs.host_data >> 8) & 0xFF;
        break;
    case 0x08:
        if (size == 1) return s->smb_regs.host_command;
        break;
    case 0x09:
        if (size == 1) return s->smb_regs.host_block_data;
        break;
    default:
        break;
    }
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case 0x00:
        if (size == 2 || size == 4) {
            /* W1C for clearable bits */
            s->smb_regs.global_status &= ~(val & GS_CLEAR_STS);
        }
        break;
    case 0x02:
        if (size == 2) {
            s->smb_regs.global_enable = val & 0xFFFF;
            /* Trigger transaction if GE_HOST_STC is set */
            if (val & GE_HOST_STC) {
                /* Simulate immediate successful completion */
                s->smb_regs.global_status &= ~(GS_HST_STS | GS_SMB_STS |
                                              GS_ABRT_STS | GS_COL_STS |
                                              GS_PRERR_STS | GS_TO_STS);
                s->smb_regs.global_status |= GS_HCYC_STS;
            }
            if (val & GE_ABORT) {
                /* Abort clears busy and errors */
                s->smb_regs.global_status &= ~(GS_HST_STS | GS_SMB_STS |
                                              GS_ABRT_STS | GS_COL_STS |
                                              GS_PRERR_STS | GS_TO_STS |
                                              GS_HCYC_STS);
            }
        }
        break;
    case 0x04:
        if (size == 2) s->smb_regs.host_address = val & 0xFFFF;
        break;
    case 0x06:
        if (size == 2) s->smb_regs.host_data = val & 0xFFFF;
        break;
    case 0x08:
        if (size == 1) s->smb_regs.host_command = val & 0xFF;
        break;
    case 0x09:
        if (size == 1) s->smb_regs.host_block_data = val & 0xFF;
        break;
    default:
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

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Custom PCI config read/write to handle vendor-specific registers */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    if (address == SMBGCFG) {
        if (len == 1) {
            return 0x80; /* I/O enable bit */
        }
    }
    return pci_default_read_config(pdev, address, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    pci_default_write_config(pdev, address, val, len);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(&s->smb_regs, 0, sizeof(s->smb_regs));
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

    /* Override config read/write for vendor-specific registers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type  = BAR_TYPE_PIO,
        .size  = SMB_IOSIZE,
        .name  = "smbus-io"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    memset(&s->smb_regs, 0, sizeof(s->smb_regs));
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
    .name = "amd756_smbus_pci",
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
