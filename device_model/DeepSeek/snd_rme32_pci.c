/*
 * QEMU RME32 audio device model for snd-rme32 driver (QEMU 8.2.10)
 * Generated from Linux driver sound/pci/rme32.c
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

#define TYPE_PCIBASE_DEVICE "snd_rme32_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
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
#define RME32_WCR_BITPOS_FREQ_0 4
#define RME32_WCR_BITPOS_FREQ_1 5
#define RME32_WCR_BITPOS_INP_0 6
#define RME32_WCR_BITPOS_INP_1 7

#define RME32_RCR_AUDIO_ADDR_MASK 0x1ffff
#define RME32_RCR_LOCK      (1 << 23)
#define RME32_RCR_ERF       (1 << 26)
#define RME32_RCR_FREQ_0    (1 << 27)
#define RME32_RCR_FREQ_1    (1 << 28)
#define RME32_RCR_FREQ_2    (1 << 29)
#define RME32_RCR_KMODE     (1 << 30)
#define RME32_RCR_IRQ       (1 << 31)
#define RME32_RCR_BITPOS_F0 27
#define RME32_RCR_BITPOS_F1 28
#define RME32_RCR_BITPOS_F2 29

#define RME32_INPUT_OPTICAL 0
#define RME32_INPUT_COAXIAL 1
#define RME32_INPUT_INTERNAL 2
#define RME32_INPUT_XLR 3

#define RME32_CLOCKMODE_SLAVE 0
#define RME32_CLOCKMODE_MASTER_32 1
#define RME32_CLOCKMODE_MASTER_44 2
#define RME32_CLOCKMODE_MASTER_48 3

#define RME32_BLOCK_SIZE 8192
#define RME32_MID_BUFFER_SIZE (1024*1024)
#define RME32_32_REVISION 192
#define RME32_328_REVISION_OLD 100
#define RME32_328_REVISION_NEW 101
#define RME32_PRO_REVISION_WITH_8412 192
#define RME32_PRO_REVISION_WITH_8414 150
#define RME32_ISWORKING(rme32) ((rme32)->wcreg & RME32_WCR_START)
#define RME32_PRO_WITH_8414(rme32) ((rme32)->pci->device == PCI_DEVICE_ID_RME_DIGI32_PRO && (rme32)->rev == RME32_PRO_REVISION_WITH_8414)

/* Not provided by driver but inferred from buffer sizes: 128KB */
#define RME32_BUFFER_SIZE 0x20000

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion container;      /* container for BAR0 */
    MemoryRegion data_buffer;    /* RAM for audio data */
    MemoryRegion register_io;    /* IO for registers at 0x20000 */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows */
    uint32_t wcreg;
    uint32_t rcreg;

    /* Internal simulation state */
    QEMUTimer *irq_timer;
    bool irq_pending;
    uint32_t position;           /* current audio position in bytes */
    bool running;                /* playback/capture active */
    uint32_t buffer_size;        /* RME32_BUFFER_SIZE */
};

/* Forward declarations */
static uint64_t rme32_reg_read(void *opaque, hwaddr addr, unsigned size);
static void rme32_reg_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static void rme32_timer_cb(void *opaque);

static const MemoryRegionOps rme32_reg_ops = {
    .read = rme32_reg_read,
    .write = rme32_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static uint64_t rme32_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t val = 0;

    switch (addr) {
    case 0x0:   /* RME32_IO_CONTROL_REGISTER / GET_POS */
        /* Construct rcreg from internal state */
        val = s->position & RME32_RCR_AUDIO_ADDR_MASK;
        if (s->irq_pending) {
            val |= RME32_RCR_IRQ;
        }
        /* Provide a fixed capture frequency 44100 Hz for CS8412 mapping */
        /* F2=0, F1=1, F0=0 */
        val |= RME32_RCR_FREQ_1;
        /* Lock status: 0 = S/PDIF (non-ADAT) */
        /* Leave LOCK and KMODE clear */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from unimplemented register 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void rme32_reg_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    switch (addr) {
    case 0x0:   /* RME32_IO_CONTROL_REGISTER */
    {
        uint32_t old_wcreg = s->wcreg;
        s->wcreg = val;

        /* Check if START bit changed */
        if ((old_wcreg ^ s->wcreg) & RME32_WCR_START) {
            if (s->wcreg & RME32_WCR_START) {
                /* Start audio engine */
                s->running = true;
                /* Arm timer to fire after one block period */
                /* One block = RME32_BLOCK_SIZE bytes. 
                   Assume sample rate 44100, 2ch, 16-bit => 4 bytes per frame => 11025 bytes/sec per channel? Actually stereo 16-bit = 4 bytes/frame. 44100*4 = 176400 bytes/sec.
                   Time per block = 8192 / 176400 ≈ 0.0464 sec. Set to 46 ms (use ms timer) */
                timer_mod(s->irq_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 46);
            } else {
                /* Stop audio engine */
                s->running = false;
                timer_del(s->irq_timer);
            }
        }
        break;
    }
    case 0x4:   /* RME32_IO_CONFIRM_ACTION_IRQ */
        if (val == 0) {
            s->irq_pending = false;
            pci_set_irq(pdev, 0);
        }
        break;
    case 0x100: /* RME32_IO_RESET_POS */
        if (val == 0) {
            s->position = 0;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to unimplemented register 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static void rme32_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!s->running) {
        return;
    }

    /* Advance position by one block */
    s->position = (s->position + RME32_BLOCK_SIZE) % s->buffer_size;

    /* Assert interrupt */
    s->irq_pending = true;
    pci_set_irq(pdev, 1);

    /* Re-arm timer to next block */
    timer_mod(s->irq_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 46);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Default register values as in snd_rme32_create() */
    s->wcreg = RME32_WCR_SEL | RME32_WCR_INP_0 | RME32_WCR_MUTE;
    s->position = 0;
    s->irq_pending = false;
    s->running = false;
    timer_del(s->irq_timer);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0xea60 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x9896 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0401 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, RME32_PRO_REVISION_WITH_8412); /* CS8412 receiver */
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize buffer size */
    s->buffer_size = RME32_BUFFER_SIZE;

    /* Create BAR0 container region */
    hwaddr bar_size = 0x30000;
    hwaddr aligned_size = pow2ceil(bar_size);
    memory_region_init(&s->container, OBJECT(s), "rme32-container", aligned_size);

    /* Data buffer as RAM from 0x0 to 0x1FFFF */
    memory_region_init_ram(&s->data_buffer, OBJECT(s), "rme32-data", RME32_BUFFER_SIZE, errp);
    memory_region_add_subregion(&s->container, 0, &s->data_buffer);

    /* Register IO region from 0x20000 to end */
    memory_region_init_io(&s->register_io, OBJECT(s), &rme32_reg_ops, s, "rme32-regs", bar_size - RME32_IO_CONTROL_REGISTER);
    memory_region_add_subregion(&s->container, RME32_IO_CONTROL_REGISTER, &s->register_io);

    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->container);

    /* Initialize timer */
    s->irq_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, rme32_timer_cb, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(s->irq_timer);
    timer_free(s->irq_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_rme32_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(wcreg, PCIBaseState),
        VMSTATE_UINT32(position, PCIBaseState),
        VMSTATE_BOOL(irq_pending, PCIBaseState),
        VMSTATE_BOOL(running, PCIBaseState),
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
