/*
 * QEMU PCI device model for Access I/O WDT-PCI (Watchdog)
 * Based on Linux driver: drivers/watchdog/wdt_pci.c
 *
 * Target: QEMU 8.2.10
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/log.h"
#include "qapi/error.h"

#define TYPE_PCIBASE_DEVICE "wdt_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Offsets */
#define WDT_COUNT0      0x00
#define WDT_COUNT1      0x01
#define WDT_COUNT2      0x02
#define WDT_CR          0x03
#define WDT_SR          0x04
#define WDT_RT          0x05
#define WDT_BUZZER      0x06
#define WDT_DC          0x07
#define WDT_CLOCK_REG   0x0c /* Clock Select Register */
#define WDT_OPTONOTRST  0x0d
#define WDT_OPTORST     0x0e
#define WDT_PROGOUT     0x0f

/* Driver defined constants */
#define WDT_CLOCK_FREQ  5000000 /* 5 MHz */

/* Status Register Bits */
#define WDC_SR_PSUUNDR  64
#define WDC_SR_PSUOVER  32
#define WDC_SR_FANGOOD  16
#define WDC_SR_ISII1    8
#define WDC_SR_ISOI0    4
#define WDC_SR_TGOOD    2
#define WDC_SR_WCCR     1

/* PIT Emulation Constants */
#define PIT_RW_LSB      1
#define PIT_RW_MSB      2
#define PIT_RW_BOTH     3

typedef struct PITChannel {
    uint16_t count;
    uint16_t latch;
    uint8_t mode;
    uint8_t rw_mode;
    uint8_t write_state; /* 0: LSB, 1: MSB (for RW_BOTH) */
} PITChannel;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion io_bar;

    /* Hardware State */
    PITChannel pit[3];
    uint8_t status_reg;
    bool wdt_enabled;
    
    QEMUTimer *wdt_timer;
    int64_t next_expiry;
};

/* 
 * The driver configures:
 * CTR0: Mode 3, Count derived from WDT_CLOCK (5MHz) -> Output 100Hz
 * CTR1: Mode 2, Count <heartbeat*100> (Input CTR0 -> Output IRQ/Reset)
 * CTR2: Mode 1 (Retriggerable One-Shot)
 * 
 * We simulate the watchdog timeout based on CTR1 count assuming 100Hz clock.
 */
static void wdt_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    /* Timer expired: Assert Reset Pending bit and IRQ */
    s->status_reg &= ~WDC_SR_WCCR;
    
    /* Pulse IRQ to simulate the hardware interrupt */
    pci_set_irq(&s->parent_obj, 1);
}

static void wdt_rearm(PCIBaseState *s)
{
    if (!s->wdt_enabled) {
        timer_del(s->wdt_timer);
        return;
    }

    /* 
     * CTR1 holds the heartbeat count in 10ms ticks (100Hz).
     * Timeout = count * 10 ms.
     */
    uint64_t ticks = s->pit[1].count;
    if (ticks == 0) ticks = 65536;

    int64_t duration_ns = ticks * 10 * 1000 * 1000;
    s->next_expiry = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + duration_ns;
    timer_mod(s->wdt_timer, s->next_expiry);

    /* Clear IRQ and Reset Pending status on rearm */
    s->status_reg |= WDC_SR_WCCR;
    pci_set_irq(&s->parent_obj, 0);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    switch (addr) {
    case WDT_COUNT0:
    case WDT_COUNT1:
    case WDT_COUNT2:
        /* Simplified PIT read: return latched or current count LSB/MSB */
        val = 0;
        break;

    case WDT_SR:
        val = s->status_reg;
        break;

    case WDT_RT:
        /* 
         * Temperature simulation.
         * Driver calc: temp = (c * 11 / 15) + 7
         * If we return 0x20 (32): (32*11)/15 + 7 = 23 + 7 = 30 C
         */
        val = 0x20;
        break;

    case WDT_BUZZER:
        /* Read disables buzzer */
        break;

    case WDT_DC:
        /* Read disables watchdog */
        s->wdt_enabled = false;
        timer_del(s->wdt_timer);
        pci_set_irq(&s->parent_obj, 0);
        break;

    case WDT_OPTONOTRST:
    case WDT_OPTORST:
    case WDT_PROGOUT:
        /* Read triggers side effects (disable features), ignore for model */
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "[%s] Unhandled read addr=0x%"HWADDR_PRIx"\n", 
                      TYPE_PCIBASE_DEVICE, addr);
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int channel_idx;

    switch (addr) {
    case WDT_COUNT0:
    case WDT_COUNT1:
    case WDT_COUNT2:
        channel_idx = addr - WDT_COUNT0;
        PITChannel *ch = &s->pit[channel_idx];

        if (ch->rw_mode == PIT_RW_BOTH) {
            if (ch->write_state == 0) {
                /* LSB */
                ch->latch = (ch->latch & 0xFF00) | (val & 0xFF);
                ch->write_state = 1;
            } else {
                /* MSB */
                ch->latch = (ch->latch & 0x00FF) | ((val & 0xFF) << 8);
                ch->count = ch->latch;
                ch->write_state = 0;
                /* If this was CTR1, it might affect the watchdog timeout */
                if (channel_idx == 1 && s->wdt_enabled) {
                    wdt_rearm(s);
                }
            }
        } else {
            /* Simplified: assume LSB/MSB only modes not used by this driver for loading */
            ch->count = val & 0xFF; 
        }
        break;

    case WDT_CR:
        /* 8254 Control Word */
        {
            int sc = (val >> 6) & 0x03;
            int rw = (val >> 4) & 0x03;
            int mode = (val >> 1) & 0x07;
            
            if (sc < 3) {
                s->pit[sc].mode = mode;
                s->pit[sc].rw_mode = rw;
                s->pit[sc].write_state = 0; /* Reset write state to LSB */
            }
        }
        break;

    case WDT_DC:
        /* Write 0 enables watchdog */
        if (val == 0) {
            s->wdt_enabled = true;
            wdt_rearm(s);
        }
        break;

    case WDT_CLOCK_REG:
        /* Driver writes to set clock (5MHz). Ignore. */
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "[%s] Unhandled write addr=0x%"HWADDR_PRIx" val=0x%"PRIx64"\n", 
                      TYPE_PCIBASE_DEVICE, addr, val);
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    s->wdt_enabled = false;
    timer_del(s->wdt_timer);
    pci_set_irq(&s->parent_obj, 0);

    /* 
     * Default Status: 
     * PSUOVER=1 (Good), PSUUNDR=1 (Good), FANGOOD=1 (Good), TGOOD=1 (Good), WCCR=1 (No Reset)
     */
    s->status_reg = WDC_SR_PSUOVER | WDC_SR_PSUUNDR | WDC_SR_FANGOOD | WDC_SR_TGOOD | WDC_SR_WCCR;

    memset(s->pit, 0, sizeof(s->pit));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* PCI ID: Access I/O 0x494f : 0x22c0 */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x494f);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x22c0);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR 2: I/O, size 0x10 (covering 0x00-0x0F) */
    memory_region_init_io(&s->io_bar, OBJECT(s), &pcibase_pio_ops, s, "wdt_pci_io", 0x10);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->io_bar);

    s->wdt_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, wdt_timer_cb, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_free(s->wdt_timer);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

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
