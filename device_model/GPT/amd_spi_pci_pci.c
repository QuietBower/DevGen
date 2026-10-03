/*
 * QEMU PCI device model for AMD SPI (PCI variant)
 * Target: QEMU 8.2.x
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

#define TYPE_PCIBASE_DEVICE "amd_spi_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define AMD_PCI_DEVICE_ID_LPC_BRIDGE        0x1682
#define AMD_PCI_LPC_SPI_BASE_ADDR_REG       0xA0
#define AMD_SPI_BASE_ADDR_MASK              ~0xFFu
#define AMD_HID2_PCI_BAR_OFFSET             0x00002000
#define AMD_HID2_MEM_SIZE                   0x200

enum amd_spi_versions {
    AMD_SPI_V1 = 1,
    AMD_SPI_V2,
    AMD_HID2_SPI,
};

#define AMD_SPI_PCI_VENDOR_ID   PCI_VENDOR_ID_AMD
#define AMD_SPI_PCI_DEVICE_ID   AMD_PCI_DEVICE_ID_LPC_BRIDGE
#define AMD_SPI_PCI_CLASS_ID    PCI_CLASS_OTHERS

#define AMD_SPI_MAX_HZ          100000000
#define AMD_SPI_MIN_HZ          800000

/* HID DMA related sizes taken from driver definitions */
#define AMD_SPI_HID2_DMA_SIZE   4096

/* HID2 register offsets (from driver macros) */
#define AMD_SPI_HID2_INT_MASK   0x158
#define AMD_SPI_HID2_CNTRL      0x150

/* Bit and mask helpers to match driver usage */
#ifndef BIT
#define BIT(nr)                 (1U << (nr))
#endif

#ifndef GENMASK
#define GENMASK(h, l) \
    (((~0U) - (1U << (l)) + 1) & \
     (~0U >> (31 - (h))))
#endif

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

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        uint64_t val = 0;
        memcpy(&val, s->mmio_backing + addr, size);
        return val;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        memcpy(s->mmio_backing + addr, &val, size);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
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
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Identify as the LPC bridge device the AMD SPI driver binds to */
    pci_set_word(pci_conf + PCI_VENDOR_ID, AMD_SPI_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, AMD_SPI_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, AMD_SPI_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /*
     * Program the LPC SPI base address register. The AMD SPI PCI driver
     * reads this dword at AMD_PCI_LPC_SPI_BASE_ADDR_REG, masks it with
     * AMD_SPI_BASE_ADDR_MASK (~0xFF) and then adds AMD_HID2_PCI_BAR_OFFSET
     * before ioremapping the HID2 window. We make it point at BAR0 so that
     * the computed MMIO address will fall inside the registered BAR.
     */
    {
        /* Make base point to BAR0 programmed address (which guest will set).
         * Only lower 8 bits must be zero for mask; keep default 0 so that
         * driver sees a valid, naturally aligned base and succeeds probing.
         */
        uint32_t cfg_val = 0x00000000u;
        pci_set_long(pci_conf + AMD_PCI_LPC_SPI_BASE_ADDR_REG, cfg_val);

        /* BAR0 MMIO region: must cover AMD_HID2_PCI_BAR_OFFSET + AMD_HID2_MEM_SIZE.
         * Use 16 MiB so that any reasonable guest-assigned BAR0 base, when
         * added with AMD_HID2_PCI_BAR_OFFSET, still remains inside BAR0.
         */
        s->num_bars = 1;
        s->bar_info[0].index = 0;
        s->bar_info[0].type = BAR_TYPE_MMIO;
        s->bar_info[0].size = 0x1000000; /* 16 MiB */
        s->bar_info[0].name = "amd-spi-mmio";
        s->bar_info[0].sparse = false;

        s->mmio_backing_size = s->bar_info[0].size;
        s->mmio_backing = g_malloc0(s->mmio_backing_size);
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
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

