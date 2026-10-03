/*
 * QEMU virtual PCI device model for Linux watchdog driver wdt_pci
 * Target QEMU version: 8.2.10
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

#define TYPE_PCIBASE_DEVICE "wdt_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define WD_TIMO 60
#define WDT_IS_PCI
#define WDT_CR_OFFSET       3
#define WDT_COUNT0_OFFSET   0
#define WDT_DC_OFFSET       7
#define WDT_BUZZER_OFFSET   6
#define WDT_OPTORST_OFFSET      14
#define WDT_OPTONOTRST_OFFSET   13
#define WDT_PROGOUT_OFFSET      15
#define WDC_SR_PSUUNDR      64
#define WDC_SR_FANGOOD      16
#define WDC_SR_ISII1        8
#define WDC_SR_TGOOD        2
#define WDC_SR_ISOI0        4
#define WDT_SR_OFFSET       4
#define WDC_SR_PSUOVER      32
#define WDT_RT_OFFSET       5
#define WDC_SR_WCCR         1

/* PCI IDs: first entry from wdtpci_pci_tbl */
#define WDTPCI_VENDOR_ID 0x494f
#define WDTPCI_DEVICE_ID 0x22c0

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

    /* I/O register state for the watchdog card */
    uint8_t regs[16];

    /* internal state matching driver expectations */
    uint16_t ctr_reload[3]; /* last programmed counter reload values */
    uint8_t watchdog_enabled; /* whether DC register last written enabled (0) or disabled (non-zero) */
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

/* MMIO handlers: this device uses only I/O port BAR according to the driver.
 * Keep MMIO returning 0/unimplemented. */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* PIO handlers implement the card's register interface as used by the driver */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t offset = addr & 0x0f;
    uint8_t val = 0xff;

    switch (offset) {
    case WDT_DC_OFFSET:
        /* DC: driver uses inb to disable/enable; return last value */
        val = s->regs[WDT_DC_OFFSET];
        break;
    case WDT_BUZZER_OFFSET:
    case WDT_OPTONOTRST_OFFSET:
    case WDT_OPTORST_OFFSET:
    case WDT_PROGOUT_OFFSET:
        /* Simple readable outputs */
        val = s->regs[offset];
        break;
    case WDT_SR_OFFSET:
        /* Status register: driver uses this macro but behavior is not shown
         * in the provided code; return stored value. */
        val = s->regs[WDT_SR_OFFSET];
        break;
    case WDT_RT_OFFSET:
        /* Temperature/raw counter register: return stored value. */
        val = s->regs[WDT_RT_OFFSET];
        break;
    default:
        if (offset < sizeof(s->regs)) {
            val = s->regs[offset];
        } else {
            val = 0xff;
        }
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t offset = addr & 0x0f;
    uint8_t v = (uint8_t)val;

    switch (offset) {
    case WDT_CR_OFFSET:
        /* Control register: only index is defined, details not present. */
        s->regs[WDT_CR_OFFSET] = v;
        break;
    case WDT_COUNT0_OFFSET:
    case WDT_COUNT0_OFFSET + 1:
    case WDT_COUNT0_OFFSET + 2:
        /* Counter registers exist, semantics not detailed in provided code. */
        if (offset < sizeof(s->regs)) {
            s->regs[offset] = v;
        }
        break;
    case WDT_DC_OFFSET:
        /* DC register controls enable/disable, but only index is visible here. */
        s->regs[WDT_DC_OFFSET] = v;
        s->watchdog_enabled = (v == 0) ? 1 : 0;
        break;
    case WDT_BUZZER_OFFSET:
    case WDT_OPTONOTRST_OFFSET:
    case WDT_OPTORST_OFFSET:
    case WDT_PROGOUT_OFFSET:
        s->regs[offset] = v;
        break;
    default:
        if (offset < sizeof(s->regs)) {
            s->regs[offset] = v;
        }
        break;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    memset(s->regs, 0, sizeof(s->regs));
    s->ctr_reload[0] = s->ctr_reload[1] = s->ctr_reload[2] = 0;
    s->watchdog_enabled = 0;

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  WDTPCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  WDTPCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR2 is I/O space in driver: pci_resource_start(dev, 2) used as io base */
    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 16; /* driver uses offsets up to 15 */
    s->bar_info[0].name = "wdt_pci-io";
    s->bar_info[0].sparse = false;

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
    static const InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init = pcibase_class_init,
        .interfaces = (InterfaceInfo *)interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);

