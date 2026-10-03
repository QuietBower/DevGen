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

/* Additional include files retrieved from driver context */


#define TYPE_PCIBASE_DEVICE "pcwd_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_QUICKLOGIC    0x11e3
#define PCI_DEVICE_ID_WATCHDOG_PCIPCWD 0x5030
#define CMD_GET_STATUS              0x04
#define WD_PCI_WTRP                 0x01
#define WD_PCI_HRBT                 0x02
#define WD_PCI_TTRP                 0x04
#define WD_PCI_RL2A                 0x08
#define WD_PCI_RL1A                 0x10
#define WD_PCI_R2DS                 0x40
#define WD_PCI_RLY2                 0x80
#define WD_PCI_WDIS                 0x10
#define WD_PCI_ENTP                 0x20
#define WD_PCI_WRSP                 0x40
#define WD_PCI_PCMD                 0x80
#define PCI_COMMAND_TIMEOUT         150
#define CMD_GET_FIRMWARE_VERSION    0x08
#define CMD_READ_WATCHDOG_TIMEOUT   0x18
#define CMD_WRITE_WATCHDOG_TIMEOUT  0x19
#define CMD_GET_CLEAR_RESET_COUNT   0x84

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
    uint8_t status;
    uint8_t firmware_version;
    uint8_t watchdog_timeout;
    uint8_t reset_count;

    uint8_t control_status;
    uint8_t option_switches;
    uint8_t cmd_data_lsb;
    uint8_t cmd_data_msb;
    uint8_t cmd_resp_lsb;
    uint8_t cmd_resp_msb;
    uint16_t heartbeat;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
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

    switch (addr) {
    case 0:
        /* Temperature */
        val = 25; /* 25 Celsius, not 0xF0 so it supports temp */
        break;
    case 1:
        /* Control status */
        val = s->control_status;
        break;
    case 2:
        /* Status */
        val = s->status;
        break;
    case 3:
        /* Option switches */
        val = s->option_switches;
        break;
    case 4:
        /* LSB of response */
        val = s->cmd_resp_lsb;
        break;
    case 5:
        /* MSB of response */
        val = s->cmd_resp_msb;
        break;
    case 6:
        /* Clear WRSP bit */
        s->status &= ~WD_PCI_WRSP;
        val = 0;
        break;
    default:
        val = 0xff;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0:
        /* Keepalive */
        if (val == 0x42) {
            /* Reload timer */
        }
        break;
    case 1:
        /* Clear trip status & LED */
        s->control_status = val & (WD_PCI_R2DS | WD_PCI_WTRP);
        break;
    case 3:
        /* Start/Stop */
        if (val == 0x00) {
            s->status &= ~WD_PCI_WDIS; /* Enable */
        } else if (val == 0xA5) {
            s->status |= WD_PCI_WDIS; /* Disable */
        }
        break;
    case 4:
        s->cmd_data_lsb = val;
        break;
    case 5:
        s->cmd_data_msb = val;
        break;
    case 6:
        /* Command */
        switch (val) {
        case CMD_GET_FIRMWARE_VERSION:
            s->cmd_resp_msb = 1; /* Major */
            s->cmd_resp_lsb = 0; /* Minor */
            s->status |= WD_PCI_WRSP;
            break;
        case CMD_WRITE_WATCHDOG_TIMEOUT:
            s->heartbeat = (s->cmd_data_msb << 8) | s->cmd_data_lsb;
            s->status |= WD_PCI_WRSP;
            break;
        case CMD_READ_WATCHDOG_TIMEOUT:
            s->cmd_resp_msb = (s->heartbeat >> 8) & 0xff;
            s->cmd_resp_lsb = s->heartbeat & 0xff;
            s->status |= WD_PCI_WRSP;
            break;
        case CMD_GET_CLEAR_RESET_COUNT:
            s->cmd_resp_msb = 0;
            s->cmd_resp_lsb = s->reset_count;
            s->reset_count = 0;
            s->status |= WD_PCI_WRSP;
            break;
        default:
            s->status |= WD_PCI_WRSP;
            break;
        }
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->status = WD_PCI_WDIS; /* Disabled by default */
    s->control_status = 0;
    s->option_switches = 0x18; /* Some default switches */
    s->heartbeat = 0;
    s->reset_count = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_QUICKLOGIC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_WATCHDOG_PCIPCWD );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xff00 );
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
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = 16,
        .name = "pcwd_pio"
    };

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
    .name = "pcwd_pci_pci",
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
