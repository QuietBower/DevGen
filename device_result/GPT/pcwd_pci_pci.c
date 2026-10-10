/*
 * QEMU PCI PC Watchdog (pcwd_pci) minimal emulation for Linux driver
 * Target QEMU: 8.2.10
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

#define PCI_VENDOR_ID_QUICKLOGIC    0x11e3
#define PCI_DEVICE_ID_WATCHDOG_PCIPCWD 0x5030

/* status/control bits from driver */
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

#define CMD_GET_STATUS                     0x04
#define CMD_GET_FIRMWARE_VERSION           0x08
#define CMD_READ_WATCHDOG_TIMEOUT          0x18
#define CMD_WRITE_WATCHDOG_TIMEOUT         0x19
#define CMD_GET_CLEAR_RESET_COUNT          0x84

#define QUIET   0
#define WATCHDOG_HEARTBEAT 0

/* BAR metadata */
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

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Internal emulated registers/fields (derived only from driver usage) */
    /* I/O port layout (BAR0 I/O):
     *  +0: temperature register (read), also used for keepalive write
     *  +1: control status register 1 (read/write)
     *  +2: status register 2 (read)
     *  +3: control/status register 3 (read/write)
     *  +4: command/LSB data register (read/write)
     *  +5: MSB data register (read/write)
     *  +6: command/ack register (read)
     */

    uint8_t port0_temp;        /* in Celsius */
    uint8_t port1_ctrl_status; /* control status #1 */
    uint8_t port2_status;      /* contains WD_PCI_WDIS, WD_PCI_WRSP, etc. */
    uint8_t port3_ctrl;        /* used for start/stop and option switches */
    uint8_t port4_data_lsb;
    uint8_t port5_data_msb;
    uint8_t port6_cmd_ack;

    /* command engine state */
    uint8_t last_cmd;
    uint8_t reset_counter;

    /* heartbeat and countdown */
    uint16_t programmed_timeout;  /* seconds */
    uint16_t time_left;           /* seconds */

    /* flags */
    bool card_enabled;        /* inverse of WDIS */
    bool supports_temp;       /* determined by pcipcwd_check_temperature_support */
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

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

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

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

/* ----------------- I/O port behavior (BAR0) ---------------------- */

/* helper: update WDIS bit in port2 according to card_enabled flag */
static void pcwd_update_wdis(PCIBaseState *s)
{
    if (s->card_enabled) {
        s->port2_status &= ~WD_PCI_WDIS;
    } else {
        s->port2_status |= WD_PCI_WDIS;
    }
}

/* Command execution as seen by driver via send_command() */
static void pcwd_execute_command(PCIBaseState *s, uint8_t cmd)
{
    s->last_cmd = cmd;

    switch (cmd) {
    case CMD_GET_FIRMWARE_VERSION:
        /* driver expects some version; content not interpreted further */
        s->port5_data_msb = 1;  /* major */
        s->port4_data_lsb = 0;  /* minor */
        break;

    case CMD_WRITE_WATCHDOG_TIMEOUT: {
        uint16_t t = ((uint16_t)s->port5_data_msb << 8) | s->port4_data_lsb;
        s->programmed_timeout = t;
        s->time_left = t;
        break;
    }

    case CMD_READ_WATCHDOG_TIMEOUT: {
        /* read time left; driver may see 0xFFFF when not armed */
        uint16_t t = s->time_left;
        s->port5_data_msb = (t >> 8) & 0xff;
        s->port4_data_lsb = t & 0xff;
        break;
    }

    case CMD_GET_CLEAR_RESET_COUNT:
        /* driver writes msb=0, reset_counter=0xff; we return previous and clear */
        s->port5_data_msb = 0;
        s->port4_data_lsb = s->reset_counter;
        s->reset_counter = 0;
        break;

    case CMD_GET_STATUS:
        /* not used by provided driver snippet, leave default values */
        break;

    default:
        break;
    }

    /* set WRSP to signal response ready */
    s->port2_status |= WD_PCI_WRSP;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t val = 0;

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcwd_pci: invalid I/O read size %u\n", size);
        return 0xff;
    }

    switch (addr) {
    case 0: /* temperature / keepalive */
        /* temperature only meaningful if supports_temp; driver uses it only in that case */
        val = s->port0_temp;
        break;
    case 1: /* control status #1 */
        val = s->port1_ctrl_status;
        break;
    case 2: /* status register */
        val = s->port2_status;
        break;
    case 3: /* option switches / control */
        val = s->port3_ctrl;
        break;
    case 4: /* command data LSB */
        val = s->port4_data_lsb;
        break;
    case 5: /* command data MSB */
        val = s->port5_data_msb;
        break;
    case 6:
        /* read of port 6 after command to clear WRSP, per driver comment */
        s->port2_status &= ~WD_PCI_WRSP;
        val = s->port6_cmd_ack;
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
    uint8_t v = (uint8_t)val;

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcwd_pci: invalid I/O write size %u\n", size);
        return;
    }

    switch (addr) {
    case 0:
        /* keepalive: driver writes any data here to retrigger watchdog */
        if (s->card_enabled && s->programmed_timeout != 0) {
            s->time_left = s->programmed_timeout;
        }
        break;
    case 1:
        /* driver writes (control_status & WD_PCI_R2DS) | WD_PCI_WTRP to clear trips */
        s->port1_ctrl_status &= ~(WD_PCI_WTRP | WD_PCI_TTRP);
        break;
    case 2:
        /* driver never writes here in provided code */
        break;
    case 3:
        /* start/stop and option switches */
        /* pcipcwd_start: write 0x00, then read port2 and expect !WDIS */
        /* pcipcwd_stop: write 0xA5 twice, then expect WDIS set */
        s->port3_ctrl = v;
        if (v == 0x00) {
            s->card_enabled = true;
            pcwd_update_wdis(s);
        } else if (v == 0xA5) {
            s->card_enabled = false;
            pcwd_update_wdis(s);
        }
        break;
    case 4:
        /* LSB data for command */
        s->port4_data_lsb = v;
        break;
    case 5:
        /* MSB data for command */
        s->port5_data_msb = v;
        break;
    case 6:
        /* command register: triggers processing */
        pcwd_execute_command(s, v);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* This device uses only I/O ports (BAR0). No MMIO defined in driver. */
    qemu_log_mask(LOG_GUEST_ERROR,
                  "pcwd_pci: unexpected MMIO read addr=%" PRIx64 " size=%u\n",
                  (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* This device uses only I/O ports (BAR0). No MMIO defined in driver. */
    qemu_log_mask(LOG_GUEST_ERROR,
                  "pcwd_pci: unexpected MMIO write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  (uint64_t)addr, val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    /* Initialize internal state as driver expects after probe */
    s->port0_temp = 30; /* some Celsius value != 0xF0 to signal temp support */
    s->port1_ctrl_status = 0;
    s->port2_status = 0;
    s->port3_ctrl = 0x18; /* bits 0-2 heartbeat index, bits 3-4 options */
    s->port4_data_lsb = 0;
    s->port5_data_msb = 0;
    s->port6_cmd_ack = 0;
    s->last_cmd = 0;
    s->reset_counter = 0;

    s->programmed_timeout = 0;
    s->time_left = 0xffff; /* board not yet armed */

    s->card_enabled = false;
    pcwd_update_wdis(s);

    /* supports_temp will be detected by driver via port0 read */
    s->supports_temp = (s->port0_temp != 0xF0);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_QUICKLOGIC);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_WATCHDOG_PCIPCWD);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define one I/O BAR, index 0, size 8 bytes (ports 0..7 used in driver) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "pcwd-pci-io";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

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

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

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

