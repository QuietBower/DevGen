/*
 * QEMU PCI device model for cx88-alsa driver
 *
 * Based on Linux driver: drivers/media/pci/cx88/cx88-alsa.c
 * and drivers/media/pci/cx88/cx88-core.c
 * Target QEMU: 8.2.10
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "qapi/error.h"

#define TYPE_PCIBASE_DEVICE "cx88_audio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Offsets from driver analysis */
#define MO_PCI_INTMSK       0x200040
#define MO_PCI_INTSTAT      0x200044
#define MO_DEV_CNTRL2       0x200034
#define MO_AUD_INTMSK       0x200060
#define MO_AUD_INTSTAT      0x200064
#define MO_AUD_DMACNTRL     0x32C040
#define MO_AUDD_LNGTH       0x32C048
#define MO_AUDD_GPCNTRL     0x32C030
#define MO_AUDD_GPCNT       0x32C020
#define AUD_VOL_CTL         0x320594
#define AUD_BAL_CTL         0x320598
#define MO_SRST_IO          0x35C05C

/* Bit definitions */
#define AUD_INT_DN_RISCI1   (1 << 0)
#define AUD_INT_DN_RISCI2   (1 << 4)
#define AUD_INT_DN_SYNC     (1 << 12)
#define AUD_INT_OPC_ERR     (1 << 16)
#define PCI_INT_AUDINT      (1 << 1)
#define GP_COUNT_CONTROL_RESET 0x3

/* Constants */
#define VENDOR_ID           0x14f1
#define DEVICE_ID           0x8801
#define CLASS_ID            PCI_CLASS_MULTIMEDIA_AUDIO

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion mmio;

    QEMUTimer *dma_timer;
    bool dma_running;

    /* Registers */
    uint32_t pci_intmsk;
    uint32_t pci_intstat;
    uint32_t dev_cntrl2;
    uint32_t aud_intmsk;
    uint32_t aud_intstat;
    uint32_t aud_dmacntrl;
    uint32_t aud_lngth;
    uint32_t aud_gpcntrl;
    uint32_t aud_gpcnt;
    uint32_t aud_vol;
    uint32_t aud_bal;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    /* Audio interrupt summary */
    if (s->aud_intstat & s->aud_intmsk) {
        s->pci_intstat |= PCI_INT_AUDINT;
    } else {
        s->pci_intstat &= ~PCI_INT_AUDINT;
    }

    /* Main PCI interrupt */
    /* The driver checks status & mask. If any bit is set, IRQ is raised. */
    bool level = (s->pci_intstat & s->pci_intmsk) != 0;
    pci_set_irq(PCI_DEVICE(s), level);
}

static void pcibase_dma_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!s->dma_running) {
        return;
    }

    /* Simulate progress */
    s->aud_gpcnt += 1024; /* Arbitrary increment per tick */

    /* 
     * Raise RISC interrupt.
     * The driver expects RISC interrupts (AUD_INT_DN_RISCI1) to signal completion/progress.
     */
    s->aud_intstat |= AUD_INT_DN_RISCI1;
    
    pcibase_update_irq(s);

    /* Reschedule timer (approx 10ms for audio buffer periods) */
    timer_mod(s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MO_PCI_INTMSK:
        val = s->pci_intmsk;
        break;
    case MO_PCI_INTSTAT:
        val = s->pci_intstat;
        break;
    case MO_DEV_CNTRL2:
        val = s->dev_cntrl2;
        break;
    case MO_AUD_INTMSK:
        val = s->aud_intmsk;
        break;
    case MO_AUD_INTSTAT:
        val = s->aud_intstat;
        break;
    case MO_AUD_DMACNTRL:
        val = s->aud_dmacntrl;
        break;
    case MO_AUDD_LNGTH:
        val = s->aud_lngth;
        break;
    case MO_AUDD_GPCNTRL:
        val = s->aud_gpcntrl;
        break;
    case MO_AUDD_GPCNT:
        val = s->aud_gpcnt;
        break;
    case AUD_VOL_CTL:
        val = s->aud_vol;
        break;
    case AUD_BAL_CTL:
        val = s->aud_bal;
        break;
    case MO_SRST_IO:
        val = 0; /* Always read 0 for reset register */
        break;
    default:
        /* 
         * The driver may access other SRAM offsets or core registers not explicitly 
         * modeled here. Return 0 to avoid crashes.
         */
        qemu_log_mask(LOG_UNIMP, "[%s] Unimplemented read addr=0x%"PRIx64"\n",
                      TYPE_PCIBASE_DEVICE, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MO_PCI_INTMSK:
        s->pci_intmsk = val;
        pcibase_update_irq(s);
        break;
    case MO_PCI_INTSTAT:
        /* W1C: Write 1 to Clear */
        s->pci_intstat &= ~val;
        pcibase_update_irq(s);
        break;
    case MO_DEV_CNTRL2:
        s->dev_cntrl2 = val;
        break;
    case MO_AUD_INTMSK:
        s->aud_intmsk = val;
        pcibase_update_irq(s);
        break;
    case MO_AUD_INTSTAT:
        /* W1C */
        s->aud_intstat &= ~val;
        pcibase_update_irq(s);
        break;
    case MO_AUD_DMACNTRL:
        s->aud_dmacntrl = val;
        /* 
         * Check for DMA enable.
         * Typically 0x11 (FIFO Enable | RISC Enable) starts the engine.
         */
        if ((val & 0x11) == 0x11) {
            if (!s->dma_running) {
                s->dma_running = true;
                timer_mod(s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
            }
        } else {
            s->dma_running = false;
            timer_del(s->dma_timer);
        }
        break;
    case MO_AUDD_LNGTH:
        s->aud_lngth = val;
        break;
    case MO_AUDD_GPCNTRL:
        s->aud_gpcntrl = val;
        if (val == GP_COUNT_CONTROL_RESET) {
            s->aud_gpcnt = 0;
        }
        break;
    case AUD_VOL_CTL:
        s->aud_vol = val;
        break;
    case AUD_BAL_CTL:
        s->aud_bal = val;
        break;
    case MO_SRST_IO:
        /* Soft reset logic placeholder */
        break;
    default:
        /* 
         * The driver performs SRAM setup by writing to various offsets.
         * Since we simulate DMA via timer, we ignore specific SRAM configuration writes
         * but log them for completeness.
         */
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* 
     * The cx88 driver expects a large MMIO window (16MB) which covers 
     * core registers, audio registers, and SRAM aliases.
     */
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "cx88-mmio", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    s->dma_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_dma_timer_cb, s);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    s->pci_intmsk = 0;
    s->pci_intstat = 0;
    s->dev_cntrl2 = 0;
    s->aud_intmsk = 0;
    s->aud_intstat = 0;
    s->aud_dmacntrl = 0;
    s->aud_lngth = 0;
    s->aud_gpcntrl = 0;
    s->aud_gpcnt = 0;
    s->aud_vol = 0;
    s->aud_bal = 0;
    s->dma_running = false;
    if (s->dma_timer) {
        timer_del(s->dma_timer);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (s->dma_timer) {
        timer_free(s->dma_timer);
        s->dma_timer = NULL;
    }
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;
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

type_init(pcibase_register_types)
