/*
 * QEMU model of Intel i6300ESB Watchdog Timer
 *
 * Based on Linux driver drivers/watchdog/i6300esb.c
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

#define TYPE_PCIBASE_DEVICE "i6300ESB_timer_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define BAR0_SIZE 0x100

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ESB_TIMER1_OFFSET    0x00
#define ESB_TIMER2_OFFSET    0x04
#define ESB_GINTSR_OFFSET    0x08
#define ESB_RELOAD_OFFSET    0x0c

#define ESB_WDT_FUNC         (1 << 2)
#define ESB_WDT_ENABLE       (1 << 1)
#define ESB_WDT_LOCK         (1 << 0)
#define ESB_WDT_REBOOT       (1 << 5)
#define ESB_WDT_FREQ         (1 << 2)
#define ESB_WDT_INTTYPE      (0x03 << 0)
#define ESB_WDT_TIMEOUT      (1 << 9)
#define ESB_WDT_RELOAD       (1 << 8)
#define ESB_UNLOCK1          0x80
#define ESB_UNLOCK2          0x86

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

    /* Hardware Register Shadows (MMIO) */
    struct {
        uint32_t timer1;
        uint32_t timer2;
        uint32_t gintsr;
        uint16_t reload;  /* only lower 16 bits used */
    } regs;

    /* PCI Config Space Registers (device-specific) */
    uint16_t config_reg;
    uint8_t lock_reg;

    /* Unlock sequence state for timer/reload writes */
    int unlock_state;  /* 0: locked, 1: after first unlock, 2: fully unlocked */
};

/* Config space read/write intercepts */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = 0;

    /* Handle device-specific config register offsets */
    if (addr == 0x60 && len == 2) {
        val = s->config_reg;
    } else if (addr == 0x68 && len == 1) {
        val = s->lock_reg;
    } else {
        val = pci_default_read_config(pdev, addr, len);
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr == 0x60 && len == 2) {
        s->config_reg = val;
    } else if (addr == 0x68 && len == 1) {
        s->lock_reg = val;
    } else {
        pci_default_write_config(pdev, addr, val, len);
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ESB_TIMER1_OFFSET:
        val = s->regs.timer1;
        break;
    case ESB_TIMER2_OFFSET:
        val = s->regs.timer2;
        break;
    case ESB_GINTSR_OFFSET:
        val = s->regs.gintsr;
        break;
    case ESB_RELOAD_OFFSET:
        if (size == 2) {
            val = s->regs.reload;
        } else if (size == 4) {
            val = s->regs.reload;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad read size %d at offset 0x%" HWADDR_PRIx "\n",
                          __func__, size, addr);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unimplemented read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ESB_TIMER1_OFFSET:
        if (s->unlock_state == 2) {
            s->regs.timer1 = val;
            s->unlock_state = 0; /* re-lock */
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to TIMER1 while locked\n", __func__);
        }
        break;
    case ESB_TIMER2_OFFSET:
        if (s->unlock_state == 2) {
            s->regs.timer2 = val;
            s->unlock_state = 0; /* re-lock */
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to TIMER2 while locked\n", __func__);
        }
        break;
    case ESB_RELOAD_OFFSET:
        /* Unlock sequence: writes of unlock words to RELOAD register */
        if (size == 2) {
            uint16_t wval = (uint16_t)val;
            if (wval == ESB_UNLOCK1) {
                if (s->unlock_state == 0) {
                    s->unlock_state = 1;
                }
            } else if (wval == ESB_UNLOCK2) {
                if (s->unlock_state == 1) {
                    s->unlock_state = 2;
                }
            } else if (s->unlock_state == 2) {
                /* Normal write to RELOAD register when unlocked.
                 * Handle WDT_TIMEOUT (bit 9) as W1C, and WDT_RELOAD (bit 8) as reload.
                 */
                if (wval & ESB_WDT_TIMEOUT) {
                    s->regs.reload &= ~ESB_WDT_TIMEOUT;
                }
                if (wval & ESB_WDT_RELOAD) {
                    /* Reload: do nothing special */
                }
                /* Other bits may be written; just update shadow */
                s->regs.reload = wval;
                s->unlock_state = 0; /* re-lock */
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: write to RELOAD while locked (val=0x%x)\n",
                              __func__, wval);
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: RELOAD write with bad size %d\n", __func__, size);
        }
        break;
    case ESB_GINTSR_OFFSET:
        /* Not used by driver; store write value */
        s->regs.gintsr = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unimplemented write at offset 0x%" HWADDR_PRIx " val=0x%lx\n",
                      __func__, addr, val);
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

/* PIO handlers (unused) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

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

    s->regs.timer1 = 0;
    s->regs.timer2 = 0;
    s->regs.gintsr = 0;
    s->regs.reload = 0;
    s->config_reg = 0;
    s->lock_reg = 0;
    s->unlock_state = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x25ab );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0805);  /* System Watchdog (0x08 base, 0x05 subclass) */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Override config read/write to handle device-specific registers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "esb-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSIX to clean up */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i6300ESB_timer_pci",
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
