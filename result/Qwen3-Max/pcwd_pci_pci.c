/*
 * QEMU PCI Watchdog Device (pcwd_pci)
 *
 * Emulates the hardware interface for the pcwd_pci Linux watchdog driver.
 * Based on /linux-6.18/drivers/watchdog/pcwd_pci.c
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "pcwd_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register definitions from driver */
#define PCI_VENDOR_ID_QUICKLOGIC    0x11e3
#define PCI_DEVICE_ID_WATCHDOG_PCIPCWD 0x5030
#define WD_PCI_WTRP		0x01
#define WD_PCI_HRBT		0x02
#define WD_PCI_TTRP		0x04
#define WD_PCI_RL2A		0x08
#define WD_PCI_RL1A		0x10
#define WD_PCI_R2DS		0x40
#define WD_PCI_RLY2		0x80
#define WD_PCI_WDIS		0x10
#define WD_PCI_ENTP		0x20
#define WD_PCI_WRSP		0x40
#define WD_PCI_PCMD		0x80
#define PCI_COMMAND_TIMEOUT	150
#define CMD_GET_STATUS			0x04
#define CMD_GET_FIRMWARE_VERSION	0x08
#define CMD_READ_WATCHDOG_TIMEOUT	0x18
#define CMD_WRITE_WATCHDOG_TIMEOUT	0x19
#define CMD_GET_CLEAR_RESET_COUNT	0x84

/* BAR metadata definition */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* Device State */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* PIO register state */
    uint8_t reg_port0;   /* Temperature or keepalive */
    uint8_t reg_port1;   /* Control Status #1 */
    uint8_t reg_port2;   /* Status register */
    uint8_t reg_port3;   /* Option switches / disable command */
    uint8_t reg_port4;   /* LSB data */
    uint8_t reg_port5;   /* MSB data */
    uint8_t reg_port6;   /* Command port */

    /* Command response state */
    bool command_pending;
    uint8_t command_response_lsb;
    uint8_t command_response_msb;
    uint8_t last_command;

    /* Firmware version */
    uint8_t fw_rev_major;
    uint8_t fw_rev_minor;

    /* Reset counter */
    uint8_t reset_counter;

    /* Disable sequence state */
    bool first_a5;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
};

/* Forward declarations */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* MemoryRegionOps */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Helper: register a BAR */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Handle command execution */
static void execute_command(PCIBaseState *s, uint8_t cmd)
{
    s->command_pending = true;
    s->last_command = cmd;

    switch (cmd) {
    case CMD_GET_STATUS:
        s->command_response_lsb = 0;
        s->command_response_msb = 0;
        break;
    case CMD_GET_FIRMWARE_VERSION:
        s->command_response_lsb = s->fw_rev_minor;
        s->command_response_msb = s->fw_rev_major;
        break;
    case CMD_READ_WATCHDOG_TIMEOUT:
        /* Return default timeout (0xFFFF when disarmed) */
        s->command_response_lsb = 0xFF;
        s->command_response_msb = 0xFF;
        break;
    case CMD_WRITE_WATCHDOG_TIMEOUT:
        /* Accept the new timeout */
        s->command_response_lsb = s->reg_port4;
        s->command_response_msb = s->reg_port5;
        break;
    case CMD_GET_CLEAR_RESET_COUNT:
        s->command_response_lsb = s->reset_counter;
        s->command_response_msb = 0;
        /* Clear reset counter */
        s->reset_counter = 0;
        break;
    default:
        s->command_response_lsb = 0;
        s->command_response_msb = 0;
        break;
    }

    /* Set WRSP bit in status register */
    s->reg_port2 |= WD_PCI_WRSP;
}

/* PIO handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t val = 0;

    switch (addr) {
    case 0:
        /* Temperature register - return 0xF0 if no temp support */
        val = 0xF0;
        break;
    case 1:
        val = s->reg_port1;
        break;
    case 2:
        val = s->reg_port2;
        break;
    case 3:
        /* Option switches - return fixed value */
        val = 0x00; /* Default: temp reset off, power delay off */
        break;
    case 4:
        if (s->command_pending) {
            val = s->command_response_lsb;
        } else {
            val = s->reg_port4;
        }
        break;
    case 5:
        if (s->command_pending) {
            val = s->command_response_msb;
        } else {
            val = s->reg_port5;
        }
        break;
    case 6:
        if (s->command_pending) {
            /* Reading port 6 clears WRSP bit and command pending */
            s->reg_port2 &= ~WD_PCI_WRSP;
            s->command_pending = false;
        }
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] pio_read unknown addr=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0:
        /* Keepalive - any write triggers keepalive */
        break;
    case 1:
        /* Control Status #1 - handle clear operations */
        s->reg_port1 = (val & WD_PCI_R2DS) | WD_PCI_WTRP;
        break;
    case 2:
        /* Status register is read-only */
        break;
    case 3:
        /* Disable command sequence */
        if (val == 0xA5) {
            /* Two consecutive 0xA5 writes disable */
            if (s->first_a5) {
                s->reg_port2 |= WD_PCI_WDIS;
                s->first_a5 = false;
            } else {
                s->first_a5 = true;
            }
        } else {
            /* Enable command */
            s->reg_port2 &= ~WD_PCI_WDIS;
            /* Reset first_a5 state */
            s->first_a5 = false;
        }
        break;
    case 4:
        s->reg_port4 = val;
        break;
    case 5:
        s->reg_port5 = val;
        break;
    case 6:
        /* Command port */
        execute_command(s, val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] pio_write unknown addr=%" PRIx64 " val=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val);
        break;
    }
}

/* MMIO handlers (not used by this device) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* Reset */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    /* Reset register state */
    s->reg_port0 = 0xF0; /* No temperature support by default */
    s->reg_port1 = 0;    /* Control Status #1 */
    s->reg_port2 = 0;    /* Status register */
    s->reg_port3 = 0;    /* Option switches */
    s->reg_port4 = 0;    /* LSB data */
    s->reg_port5 = 0;    /* MSB data */
    s->reg_port6 = 0;    /* Command port */

    s->command_pending = false;
    s->fw_rev_major = 1;
    s->fw_rev_minor = 0;
    s->reset_counter = 0;
    s->first_a5 = false;
}

/* PCI config space access */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

/* Realize */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_QUICKLOGIC);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_WATCHDOG_PCIPCWD);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Configure BAR0 as PIO */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8; /* Ports 0-7 */
    s->bar_info[0].name = "pcwd-pio";
    s->num_bars = 1;

    /* Register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize firmware version */
    s->fw_rev_major = 1;
    s->fw_rev_minor = 0;
    s->reset_counter = 0;

    /* Initialize status register - watchdog disabled by default */
    s->reg_port2 = WD_PCI_WDIS;
}

/* Uninit/cleanup */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }
}

/* Class init / type registration */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

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