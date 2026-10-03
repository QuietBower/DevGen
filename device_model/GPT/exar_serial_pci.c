/*
 * QEMU PCI Exar 8250-compatible serial template device
 *
 * Implementation Phase (Stage 2):
 *  - Provide BAR layout matching what the Linux exar 8250 driver expects
 *  - Provide simple byte-addressable MMIO/PIO space so that readb()/writeb()
 *    and serial8250_pci_setup_port() see a contiguous UART register map
 *  - Provide a single legacy INTx interrupt line
 *
 * NOTE:
 *  - We do not implement real UART behaviour here; we only provide a
 *    register space that the driver can probe and configure.
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

#define TYPE_PCIBASE_DEVICE "exar_serial_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_ACCESSIO            0x494f
#define PCI_VENDOR_ID_EXAR                0x13a8
#define PCI_VENDOR_ID_CONNECT_TECH        0x12c4
#define PCI_VENDOR_ID_IBM                 0x1014
#define PCI_VENDOR_ID_USR                 0x16ec

#define PCI_DEVICE_ID_ACCESSIO_COM_2S        0x1052

#define PCI_ANY_ID (~0U)

#define PCI_CLASS_COMMUNICATION_SERIAL   0x0700

#define UART_EXAR_INT0          0x80
#define UART_EXAR_8XMODE        0x88
#define UART_EXAR_SLEEP         0x8b
#define UART_EXAR_DVID          0x8d
#define UART_EXAR_FCTR          0x08
#define UART_EXAR_TXTRG         0x0a
#define UART_EXAR_RXTRG         0x0b
#define UART_EXAR_MPIOINT_7_0   0x8f
#define UART_EXAR_MPIOLVL_7_0   0x90
#define UART_EXAR_MPIO3T_7_0    0x91
#define UART_EXAR_MPIOINV_7_0   0x92
#define UART_EXAR_MPIOSEL_7_0   0x93
#define UART_EXAR_MPIOOD_7_0    0x94
#define UART_EXAR_MPIOINT_15_8  0x95
#define UART_EXAR_MPIOLVL_15_8  0x96
#define UART_EXAR_MPIO3T_15_8   0x97
#define UART_EXAR_MPIOINV_15_8  0x98
#define UART_EXAR_MPIOSEL_15_8  0x99
#define UART_EXAR_MPIOOD_15_8   0x9a
#define UART_EXAR_DLD           0x02
#define UART_EXAR_REGB          0x8e
#define UART_EXAR_REGB_EECK     (1U << 4)
#define UART_EXAR_REGB_EECS     (1U << 5)
#define UART_EXAR_REGB_EEDI     (1U << 6)
#define UART_EXAR_REGB_EEDO     (1U << 7)
#define UART_EXAR_XR17C15X_PORT_OFFSET  0x200
#define UART_EXAR_XR17V25X_PORT_OFFSET  0x200
#define UART_EXAR_XR17V35X_PORT_OFFSET  0x400

#define EXAR_FIRST_VENDOR_ID   PCI_VENDOR_ID_ACCESSIO
#define EXAR_FIRST_DEVICE_ID   PCI_DEVICE_ID_ACCESSIO_COM_2S
#define EXAR_PCI_CLASS_ID      PCI_CLASS_COMMUNICATION_SERIAL

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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
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
    .valid = { .min_access_size = 1, .max_access_size = 4 },
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
    MemoryRegion *mr;

    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Helper: simple byte-access backing store for MMIO/PIO */
static uint64_t pcibase_backing_read(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    if (!s->mmio_backing || addr + size > s->mmio_backing_size) {
        return 0;
    }

    switch (size) {
    case 1:
        val = s->mmio_backing[addr];
        break;
    case 2:
        val = s->mmio_backing[addr] |
              ((uint16_t)s->mmio_backing[addr + 1] << 8);
        break;
    case 4:
        val = s->mmio_backing[addr] |
              ((uint32_t)s->mmio_backing[addr + 1] << 8) |
              ((uint32_t)s->mmio_backing[addr + 2] << 16) |
              ((uint32_t)s->mmio_backing[addr + 3] << 24);
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_backing_write(PCIBaseState *s, hwaddr addr,
                                  uint64_t val, unsigned size)
{
    if (!s->mmio_backing || addr + size > s->mmio_backing_size) {
        return;
    }

    switch (size) {
    case 1:
        s->mmio_backing[addr] = (uint8_t)val;
        break;
    case 2:
        s->mmio_backing[addr] = (uint8_t)(val & 0xff);
        s->mmio_backing[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        break;
    case 4:
        s->mmio_backing[addr] = (uint8_t)(val & 0xff);
        s->mmio_backing[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        s->mmio_backing[addr + 2] = (uint8_t)((val >> 16) & 0xff);
        s->mmio_backing[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return pcibase_backing_read(s, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    pcibase_backing_write(s, addr, val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return pcibase_backing_read(s, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    pcibase_backing_write(s, addr, val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

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
        val &= 0xff;
        break;
    case 2:
        val &= 0xffff;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr,
                                 uint32_t val, int len)
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
    Error *local_err = NULL;
    int i;

    /* Basic PCI IDs for first table entry */
    pci_set_word(pci_conf + PCI_VENDOR_ID, EXAR_FIRST_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, EXAR_FIRST_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, EXAR_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define BAR0 as MMIO region large enough for multiple ports */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000; /* covers offsets used by driver */
    s->bar_info[0].name = "exar-mmio";
    s->bar_info[0].sparse = false;

    /* Allocate simple backing for MMIO/PIO */
    s->mmio_backing_size = s->bar_info[0].size;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    for (i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }
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
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;
    k->realize      = pcibase_realize;
    k->exit         = pcibase_uninit;

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
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_class_init,
        .interfaces    = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
