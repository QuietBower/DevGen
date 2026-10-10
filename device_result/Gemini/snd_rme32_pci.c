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

/* Additional include files retrieved from driver context */


#define TYPE_PCIBASE_DEVICE "snd_rme32_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_XILINX_RME	0xea60
#define PCI_DEVICE_ID_RME_DIGI32	0x9896

#define RME32_IO_SIZE 0x30000
#define RME32_IO_DATA_BUFFER        0x0
#define RME32_IO_CONTROL_REGISTER   0x20000
#define RME32_IO_GET_POS            0x20000
#define RME32_IO_CONFIRM_ACTION_IRQ 0x20004
#define RME32_IO_RESET_POS          0x20100

#define RME32_WCR_START     (1 << 0)
#define RME32_WCR_MONO      (1 << 1)
#define RME32_WCR_MODE24    (1 << 2)
#define RME32_WCR_SEL       (1 << 3)
#define RME32_WCR_FREQ_0    (1 << 4)
#define RME32_WCR_FREQ_1    (1 << 5)
#define RME32_WCR_INP_0     (1 << 6)
#define RME32_WCR_INP_1     (1 << 7)
#define RME32_WCR_RESET     (1 << 8)
#define RME32_WCR_MUTE      (1 << 9)
#define RME32_WCR_PRO       (1 << 10)
#define RME32_WCR_DS_BM     (1 << 11)
#define RME32_WCR_ADAT      (1 << 12)
#define RME32_WCR_AUTOSYNC  (1 << 13)
#define RME32_WCR_PD        (1 << 14)
#define RME32_WCR_EMP       (1 << 15)

#define RME32_RCR_AUDIO_ADDR_MASK 0x1ffff
#define RME32_RCR_LOCK      (1 << 23)
#define RME32_RCR_ERF       (1 << 26)
#define RME32_RCR_FREQ_0    (1 << 27)
#define RME32_RCR_FREQ_1    (1 << 28)
#define RME32_RCR_FREQ_2    (1 << 29)
#define RME32_RCR_KMODE     (1 << 30)
#define RME32_RCR_IRQ       (1 << 31)

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
    uint32_t wcreg;
    uint32_t rcreg;

    /* DMA Context */
    uint8_t audio_buffer[0x20000];
    uint32_t playback_pos;
    QEMUTimer *timer;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->rcreg & RME32_RCR_IRQ) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Driver uses MMIO for audio buffer, no bus-mastering DMA required */
}

static void pcibase_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    if (s->wcreg & RME32_WCR_START) {
        /* Advance position and trigger interrupt */
        s->playback_pos = (s->playback_pos + 4096) & RME32_RCR_AUDIO_ADDR_MASK;
        s->rcreg |= RME32_RCR_IRQ;
        pcibase_update_irq(s);
        timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x20000) {
        if (addr + size <= sizeof(s->audio_buffer)) {
            memcpy(&val, &s->audio_buffer[addr], size);
        }
        return val;
    }

    switch (addr) {
    case RME32_IO_GET_POS:
        val = s->rcreg | (s->playback_pos & RME32_RCR_AUDIO_ADDR_MASK);
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x20000) {
        if (addr + size <= sizeof(s->audio_buffer)) {
            uint64_t v = val;
            memcpy(&s->audio_buffer[addr], &v, size);
        }
        return;
    }

    switch (addr) {
    case RME32_IO_CONTROL_REGISTER:
        s->wcreg = val;
        if (s->wcreg & RME32_WCR_START) {
            timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
        } else {
            timer_del(s->timer);
        }
        break;
    case RME32_IO_CONFIRM_ACTION_IRQ:
        if (val == 0) {
            s->rcreg &= ~RME32_RCR_IRQ;
            pcibase_update_irq(s);
        }
        break;
    case RME32_IO_RESET_POS:
        if (val == 0) {
            s->playback_pos = 0;
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    s->wcreg = 0;
    s->rcreg = 0;
    s->playback_pos = 0;
    memset(s->audio_buffer, 0, sizeof(s->audio_buffer));
    if (s->timer) {
        timer_del(s->timer);
    }
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_XILINX_RME );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_RME_DIGI32 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = RME32_IO_SIZE;
    s->bar_info[0].name = "rme32-mmio";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, pcibase_timer_cb, s);
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

    if (s->timer) {
        timer_free(s->timer);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_rme32_pci",
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
