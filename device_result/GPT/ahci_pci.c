/*
 * QEMU AHCI-like PCI device model for Linux ahci.c driver minimal bring-up
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

#define TYPE_PCIBASE_DEVICE "ahci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define AHCI_DRV_NAME "ahci"
#define AHCI_DRV_VERSION "3.0"
#define AHCI_PCI_VENDOR_ID 0x8086
#define AHCI_PCI_DEVICE_ID 0x06d6
#define AHCI_PCI_CLASS_ID PCI_CLASS_STORAGE_SATA

#define AHCI_PCI_BAR_STANDARD 5

#define AHCI_MAX_PORTS       32
#define AHCI_MAX_CMDS        32
#define AHCI_MAX_REMAP       3

#define HOST_CAP             0x00
#define HOST_CTL             0x04
#define HOST_IRQ_STAT        0x08
#define HOST_PORTS_IMPL      0x0c
#define HOST_VERSION         0x10
#define HOST_EM_LOC          0x1c
#define HOST_EM_CTL          0x20
#define AHCI_VSCAP           0xA0
#define AHCI_REMAP_CAP       0xA8
#define HOST_CAP2            0x24

#define HOST_IRQ_EN          (1U << 1)
#define HOST_MRSM            (1U << 2)
#define HOST_RESET           (1U << 0)
#define HOST_AHCI_EN         (1U << 31)

#define AHCI_PORT_BASE       0x100
#define AHCI_PORT_STRIDE     0x80

#define PORT_TFDATA          0x20
#define PORT_SIG             0x24
#define PORT_CMD             0x18
#define PORT_IRQ_STAT        0x10
#define PORT_IRQ_MASK        0x14

#define PORT_CMD_CLO         (1U << 24)
#define PORT_CMD_CAP         (1U << 28)
#define PORT_CMD_LIST_ON     (1U << 15)
#define PORT_CMD_START       (1U << 0)

#define EM_CTRL_MSG_TYPE     0x00ff0000
#define EM_CTL_ALHD          (1U << 26)
#define EM_CTL_TM            (1U << 8)
#define EM_CTL_RST           (1U << 9)

#define SCR_STATUS           0x03000000

/* We choose a reasonable ABAR size large enough for all used offsets. */
#define AHCI_MMIO_SIZE       0x1000

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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* AHCI-visible register shadows */
    uint32_t host_cap;
    uint32_t host_ctl;
    uint32_t host_irq_stat;
    uint32_t host_ports_impl;
    uint32_t host_version;
    uint32_t host_em_loc;
    uint32_t host_em_ctl;
    uint32_t host_cap2;
    uint32_t host_vscap;
    uint64_t host_remap_cap;

    uint32_t port_cmd[AHCI_MAX_PORTS];
    uint32_t port_irq_stat[AHCI_MAX_PORTS];
    uint32_t port_irq_mask[AHCI_MAX_PORTS];
    uint32_t port_sig[AHCI_MAX_PORTS];
    uint32_t port_tfdata[AHCI_MAX_PORTS];

    qemu_irq irq;
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

static void ahci_update_irq(PCIBaseState *s)
{
    if (s->host_irq_stat && (s->host_ctl & HOST_IRQ_EN)) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

static int ahci_port_index_from_addr(hwaddr addr)
{
    if (addr < AHCI_PORT_BASE) {
        return -1;
    }
    return (addr - AHCI_PORT_BASE) / AHCI_PORT_STRIDE;
}

static hwaddr ahci_port_reg_offset(hwaddr addr)
{
    return (addr - AHCI_PORT_BASE) % AHCI_PORT_STRIDE;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > AHCI_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_read OOB addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        return 0;
    }

    switch (addr) {
    case HOST_CAP:
        return s->host_cap;
    case HOST_CTL:
        return s->host_ctl;
    case HOST_IRQ_STAT:
        return s->host_irq_stat;
    case HOST_PORTS_IMPL:
        return s->host_ports_impl;
    case HOST_VERSION:
        return s->host_version;
    case HOST_EM_LOC:
        return s->host_em_loc;
    case HOST_EM_CTL:
        return s->host_em_ctl;
    case HOST_CAP2:
        return s->host_cap2;
    case AHCI_VSCAP:
        return s->host_vscap;
    case AHCI_REMAP_CAP:
        if (size == 8) {
            return s->host_remap_cap;
        } else if (size == 4) {
            return (uint32_t)s->host_remap_cap;
        }
        break;
    default:
        break;
    }

    if (addr >= AHCI_PORT_BASE) {
        int port = ahci_port_index_from_addr(addr);
        hwaddr poff = ahci_port_reg_offset(addr);
        if (port >= 0 && port < AHCI_MAX_PORTS) {
            switch (poff) {
            case PORT_CMD:
                return s->port_cmd[port];
            case PORT_IRQ_STAT:
                return s->port_irq_stat[port];
            case PORT_IRQ_MASK:
                return s->port_irq_mask[port];
            case PORT_SIG:
                return s->port_sig[port];
            case PORT_TFDATA:
                return s->port_tfdata[port];
            default:
                break;
            }
        }
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > AHCI_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_write OOB addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        return;
    }

    switch (addr) {
    case HOST_CTL:
        s->host_ctl = (uint32_t)val;
        if (s->host_ctl & HOST_RESET) {
            s->host_ctl &= ~HOST_RESET;
            s->host_irq_stat = 0;
            memset(s->port_cmd, 0, sizeof(s->port_cmd));
            memset(s->port_irq_stat, 0, sizeof(s->port_irq_stat));
            memset(s->port_irq_mask, 0, sizeof(s->port_irq_mask));
            ahci_update_irq(s);
        }
        return;
    case HOST_IRQ_STAT:
        s->host_irq_stat &= ~((uint32_t)val);
        ahci_update_irq(s);
        return;
    case HOST_EM_CTL:
        s->host_em_ctl = (uint32_t)val;
        return;
    default:
        break;
    }

    if (addr >= AHCI_PORT_BASE) {
        int port = ahci_port_index_from_addr(addr);
        hwaddr poff = ahci_port_reg_offset(addr);
        if (port >= 0 && port < AHCI_MAX_PORTS) {
            switch (poff) {
            case PORT_CMD:
                s->port_cmd[port] = (uint32_t)val;
                return;
            case PORT_IRQ_MASK:
                s->port_irq_mask[port] = (uint32_t)val;
                return;
            case PORT_IRQ_STAT:
                s->port_irq_stat[port] &= ~((uint32_t)val);
                if (s->port_irq_stat[port] == 0) {
                    s->host_irq_stat &= ~(1U << port);
                }
                ahci_update_irq(s);
                return;
            default:
                break;
            }
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->host_cap = 0;
    s->host_ctl = 0;
    s->host_irq_stat = 0;
    s->host_ports_impl = 0;
    s->host_version = 0x00010200;
    s->host_em_loc = 0;
    s->host_em_ctl = 0;
    s->host_cap2 = 0;
    s->host_vscap = 0;
    s->host_remap_cap = 0;

    memset(s->port_cmd, 0, sizeof(s->port_cmd));
    memset(s->port_irq_stat, 0, sizeof(s->port_irq_stat));
    memset(s->port_irq_mask, 0, sizeof(s->port_irq_mask));
    memset(s->port_sig, 0, sizeof(s->port_sig));
    memset(s->port_tfdata, 0, sizeof(s->port_tfdata));

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
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  AHCI_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  AHCI_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, AHCI_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 1;
    s->bar_info[0].index = AHCI_PCI_BAR_STANDARD;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = AHCI_MMIO_SIZE;
    s->bar_info[0].name = "ahci-abar";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    s->host_cap = (AHCI_MAX_PORTS - 1) & 0x1f;
    s->host_ports_impl = 0x1;
    s->host_version = 0x00010200;
    s->host_cap2 = 0;
    s->host_vscap = 0;
    s->host_remap_cap = 0;

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

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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

