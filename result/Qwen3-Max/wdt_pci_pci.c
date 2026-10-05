/*
 * QEMU PCI-WDT500/501 Watchdog Timer Emulation
 *
 * Based on Linux driver: /linux-6.18/drivers/watchdog/wdt_pci.c
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

/* Register offsets and constants from driver */
#define PCI_VENDOR_ID_ACCESSIO		0x494f
#define PCI_DEVICE_ID_ACCESSIO_WDG_CSM	0x22c0
#define WDT_CR				(io+3)
#define WDT_COUNT0			(io+0)
#define WDT_DC				(io+7)
#define WDT_BUZZER			(io+6)
#define WDT_OPTORST			(io+14)
#define WDT_OPTONOTRST			(io+13)
#define WDT_PROGOUT			(io+15)
#define WDC_SR_PSUUNDR			64
#define WDC_SR_FANGOOD			16
#define WDC_SR_ISII1			8
#define WDC_SR_TGOOD			2
#define WDC_SR_ISOI0			4
#define WDT_SR				(io+4)
#define WDC_SR_PSUOVER			32
#define WDT_RT				(io+5)
#define WDC_SR_WCCR			1

/* PCI class code for watchdog timers */
#ifndef PCI_CLASS_WATCHDOG
#define PCI_CLASS_WATCHDOG		0x0803
#endif

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

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state */
    bool has_msi;
    bool has_msix;
    
    /* Register shadows */
    uint8_t wdt_cr;
    uint8_t wdt_count0;
    uint8_t wdt_dc;
    uint8_t wdt_buzzer;
    uint8_t wdt_optorst;
    uint8_t wdt_optonotrst;
    uint8_t wdt_progout;
    uint8_t wdt_sr;
    uint8_t wdt_rt;

    /* IRQ line */
    qemu_irq irq;
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

/* MMIO / PIO handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x0:
        return s->wdt_count0;
    case 0x3:
        return s->wdt_cr;
    case 0x4:
        return s->wdt_sr;
    case 0x5:
        return s->wdt_rt;
    case 0x6:
        return s->wdt_buzzer;
    case 0x7:
        return s->wdt_dc;
    case 0xd:
        return s->wdt_optonotrst;
    case 0xe:
        return s->wdt_optorst;
    case 0xf:
        return s->wdt_progout;
    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x0:
        s->wdt_count0 = val;
        break;
    case 0x3:
        s->wdt_cr = val;
        break;
    case 0x4:
        s->wdt_sr = val;
        break;
    case 0x5:
        s->wdt_rt = val;
        break;
    case 0x6:
        s->wdt_buzzer = val;
        break;
    case 0x7:
        s->wdt_dc = val;
        break;
    case 0xd:
        s->wdt_optonotrst = val;
        break;
    case 0xe:
        s->wdt_optorst = val;
        break;
    case 0xf:
        s->wdt_progout = val;
        break;
    default:
        break;
    }
}

/* Reset */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    /* Reset register shadows to default values */
    s->wdt_cr = 0;
    s->wdt_count0 = 0;
    s->wdt_dc = 0;
    s->wdt_buzzer = 0;
    s->wdt_optorst = 0;
    s->wdt_optonotrst = 0;
    s->wdt_progout = 0;
    s->wdt_sr = 0;
    s->wdt_rt = 0;
}

/* PCI config space access */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
}

/* Realize (device init) */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ACCESSIO);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ACCESSIO_WDG_CSM);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_WATCHDOG);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* BAR2: PIO, size 16 bytes (driver uses BAR2) */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 16;
    s->bar_info[2].name = "wdt-pio";
    s->num_bars = 3; /* Need to set up to BAR2 */

    /* Initialize unused BARs */
    for (int i = 0; i < 6; i++) {
        if (i != 2) {
            s->bar_info[i].type = BAR_TYPE_NONE;
        }
    }

    /* register BARs */
    for (int i = 0; i < 6; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }

    /* Connect IRQ line */
    s->irq = pci_allocate_irq(pdev);

    /* Initialize register shadows */
    s->wdt_sr = WDC_SR_TGOOD | WDC_SR_PSUOVER | WDC_SR_PSUUNDR | WDC_SR_FANGOOD | WDC_SR_WCCR;
    s->wdt_rt = 0x20; /* Default temperature reading */
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

    /* Free IRQ */
    if (s->irq) {
        qemu_free_irq(s->irq);
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
