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

#define TYPE_PCIBASE_DEVICE "wdt_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Vendor and Device IDs from driver */
#define VENDOR_ID      0x1057
#define DEVICE_ID      0x01

/* Register offsets from driver (relative to I/O base) */
#define WDT_COUNT0     0x00
#define WDT_CR         0x03
#define WDT_SR         0x04
#define WDT_RT         0x05
#define WDT_BUZZER     0x06
#define WDT_DC         0x07
#define WDT_CLOCK      0x0C
#define WDT_OPTONOTRST 0x0D
#define WDT_OPTORST    0x0E
#define WDT_PROGOUT    0x0F

#define WDC_SR_WCCR    0x01
#define WDC_SR_TGOOD   0x02
#define WDC_SR_ISOI0   0x04
#define WDC_SR_ISII1   0x08
#define WDC_SR_FANGOOD 0x10
#define WDC_SR_PSUOVER 0x20
#define WDC_SR_PSUUNDR 0x40

#define BAR_IO_INDEX 2
#define BAR_IO_SIZE 16
#define CLASS_ID PCI_CLASS_OTHERS


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

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[16];

    /* DMA Context */

    bool watchdog_enabled;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t status = s->regs[WDT_SR];
    bool irq_pending = false;

    if ((status & WDC_SR_ISOI0) || (status & WDC_SR_ISII1) ||
        !(status & WDC_SR_WCCR) ||
        !(status & WDC_SR_TGOOD) ||
        !(status & WDC_SR_PSUOVER) ||
        !(status & WDC_SR_PSUUNDR) ||
        !(status & WDC_SR_FANGOOD)) {
        irq_pending = true;
    }

    pci_set_irq(pdev, irq_pending ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected MMIO read\n", __func__);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected MMIO write\n", __func__);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid PIO read size %u\n", __func__, size);
        return 0xff;
    }

    switch (addr) {
    case WDT_SR:
        val = s->regs[WDT_SR];
        break;
    case WDT_RT:
        val = s->regs[WDT_RT];
        break;
    case WDT_DC:
        val = s->regs[WDT_DC];
        s->regs[WDT_DC] = 0;
        s->watchdog_enabled = false;
        break;
    case WDT_BUZZER:
        val = s->regs[WDT_BUZZER];
        s->regs[WDT_BUZZER] = 0;
        break;
    case WDT_OPTONOTRST:
        val = s->regs[WDT_OPTONOTRST];
        s->regs[WDT_OPTONOTRST] = 0;
        break;
    case WDT_OPTORST:
        val = s->regs[WDT_OPTORST];
        s->regs[WDT_OPTORST] = 0;
        break;
    case WDT_PROGOUT:
        val = s->regs[WDT_PROGOUT];
        s->regs[WDT_PROGOUT] = 0;
        break;
    default:
        val = s->regs[addr];
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid PIO write size %u\n", __func__, size);
        return;
    }

    switch (addr) {
    case WDT_COUNT0: /* fall through to handle counter loads for ctr 0,1,2 */
    case WDT_COUNT0 + 1:
    case WDT_COUNT0 + 2:
        s->regs[addr] = (uint8_t)val;
        break;
    case WDT_CR:
        s->regs[WDT_CR] = (uint8_t)val;
        break;
    case WDT_SR:
        s->regs[WDT_SR] = (uint8_t)val;
        pcibase_update_irq(s);
        break;
    case WDT_RT:
        s->regs[WDT_RT] = (uint8_t)val;
        break;
    case WDT_DC:
        s->regs[WDT_DC] = (uint8_t)val;
        if (val == 0) {
            s->watchdog_enabled = true;
        }
        break;
    case WDT_BUZZER:
        s->regs[WDT_BUZZER] = (uint8_t)val;
        break;
    case WDT_CLOCK:
        s->regs[WDT_CLOCK] = (uint8_t)val;
        break;
    case WDT_OPTONOTRST:
        s->regs[WDT_OPTONOTRST] = (uint8_t)val;
        break;
    case WDT_OPTORST:
        s->regs[WDT_OPTORST] = (uint8_t)val;
        break;
    case WDT_PROGOUT:
        s->regs[WDT_PROGOUT] = (uint8_t)val;
        break;
    default:
        s->regs[addr] = (uint8_t)val;
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

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->regs[WDT_SR] = 0x73;
    s->regs[WDT_RT] = 0x50;
    s->regs[WDT_DC] = 0;
    s->regs[WDT_CR] = 0;
    s->watchdog_enabled = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
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
        .index = BAR_IO_INDEX,
        .type = BAR_TYPE_PIO,
        .size = BAR_IO_SIZE,
        .name = "wdt_pci-io"
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
    .name = "wdt_pci_pci",
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
