/*
 * QEMU PCI device model for pcwd_pci watchdog (QEMU 8.2.10)
 * Derived from Linux driver drivers/watchdog/pcwd_pci.c
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

#define TYPE_PCIBASE_DEVICE "pcwd_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_QUICKLOGIC       0x11e3
#define PCI_DEVICE_ID_WATCHDOG_PCIPCWD 0x5030
#define PCI_CLASS_ID                   PCI_CLASS_OTHERS

/* Status/control bit defines from driver */
#define WD_PCI_WTRP    0x01
#define WD_PCI_HRBT    0x02
#define WD_PCI_TTRP    0x04
#define WD_PCI_RL2A    0x08
#define WD_PCI_RL1A    0x10
#define WD_PCI_R2DS    0x40
#define WD_PCI_RLY2    0x80
#define WD_PCI_WDIS    0x10
#define WD_PCI_ENTP    0x20
#define WD_PCI_WRSP    0x40
#define WD_PCI_PCMD    0x80

/* Command codes from driver */
#define CMD_GET_STATUS                0x04
#define CMD_GET_FIRMWARE_VERSION      0x08
#define CMD_READ_WATCHDOG_TIMEOUT     0x18
#define CMD_WRITE_WATCHDOG_TIMEOUT    0x19
#define CMD_GET_CLEAR_RESET_COUNT     0x84

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Device state registers */
    uint8_t port0;          /* temperature read / keepalive write */
    uint8_t port1;          /* control status */
    uint8_t port2_status;   /* status register with WDIS, WRSP */
    uint8_t port3;          /* option switches and enable/disable */
    uint8_t port4_lsb;
    uint8_t port5_msb;
    uint8_t port6_cmd;
    bool    wdis;           /* watchdog disabled (WDIS bit) */
    bool    wrsp;           /* write response pending (WRSP bit) */
    uint16_t heartbeat;     /* watchdog timeout */
    int     stop_count;     /* count of 0xA5 writes for stop */
};

/* PIO read/write handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read size %u at addr 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return ~0ULL;
    }

    switch (addr) {
    case 0: /* port 0: temperature / keepalive */
        val = s->port0;
        break;
    case 1: /* port 1: control status */
        val = s->port1;
        break;
    case 2: /* port 2: main status (WDIS, WRSP) */
        val = (s->wdis ? WD_PCI_WDIS : 0) | (s->wrsp ? WD_PCI_WRSP : 0);
        break;
    case 3: /* port 3: option switches (read) */
        val = s->port3;
        break;
    case 4: /* port 4: response LSB */
        val = s->port4_lsb;
        break;
    case 5: /* port 5: response MSB */
        val = s->port5_msb;
        break;
    case 6: /* port 6: command register read clears WRSP */
        val = 0;
        s->wrsp = false;
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

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write size %u at addr 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return;
    }

    uint8_t b = val & 0xff;

    switch (addr) {
    case 0: /* port 0: keepalive - any write retriggers watchdog */
        /* For now, just ignore; future timer reset could go here */
        break;
    case 1: /* port 1: control status write */
        /* WTRP: write 1 to clear; R2DS: direct write */
        if (b & WD_PCI_WTRP) {
            s->port1 &= ~WD_PCI_WTRP;
        }
        if (b & WD_PCI_R2DS) {
            s->port1 |= WD_PCI_R2DS;
        } else {
            s->port1 &= ~WD_PCI_R2DS;
        }
        break;
    case 2: /* port 2: not writable in this device */
        break;
    case 3: /* port 3: enable/disable watchdog */
        if (b == 0x00) {
            /* enable watchdog */
            s->wdis = false;
            s->stop_count = 0;
        } else if (b == 0xA5) {
            s->stop_count++;
            if (s->stop_count >= 2) {
                s->wdis = true;  /* disable after two 0xA5 writes */
                s->stop_count = 2;
            }
        } else {
            s->stop_count = 0;
        }
        break;
    case 4: /* port 4: data LSB for command */
        s->port4_lsb = b;
        break;
    case 5: /* port 5: data MSB for command */
        s->port5_msb = b;
        break;
    case 6: /* port 6: command */
        s->port6_cmd = b;
        /* Process command */
        switch (b) {
        case CMD_GET_FIRMWARE_VERSION:
            s->port5_msb = 1;   /* major version */
            s->port4_lsb = 0;   /* minor version */
            break;
        case CMD_GET_STATUS:
            s->port5_msb = 0;
            s->port4_lsb = 0;
            break;
        case CMD_READ_WATCHDOG_TIMEOUT:
            s->port5_msb = (s->heartbeat >> 8) & 0xFF;
            s->port4_lsb = s->heartbeat & 0xFF;
            break;
        case CMD_WRITE_WATCHDOG_TIMEOUT:
            s->heartbeat = (s->port5_msb << 8) | s->port4_lsb;
            s->port5_msb = 0;
            s->port4_lsb = 0;
            break;
        case CMD_GET_CLEAR_RESET_COUNT:
            s->port4_lsb = 0;   /* reset counter */
            s->port5_msb = 0;
            break;
        default:
            s->port4_lsb = 0;
            s->port5_msb = 0;
            break;
        }
        s->wrsp = true;         /* signal command completion */
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* Unused MMIO ops kept minimal */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    /* Reset to initial state */
    s->port0 = 0x20;        /* some temperature value (not 0xF0) */
    s->port1 = 0;
    s->port3 = 0x00;        /* option switches default */
    s->wdis = true;         /* watchdog disabled initially */
    s->wrsp = false;
    s->heartbeat = 0;
    s->stop_count = 0;
    s->port4_lsb = 0;
    s->port5_msb = 0;
    s->port6_cmd = 0;
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_QUICKLOGIC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_WATCHDOG_PCIPCWD );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Initialize device state */
    pcibase_reset(DEVICE(pdev));

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;  /* Power-of-two size for I/O region */
    s->bar_info[0].name = "pcwd_pci_io";
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
