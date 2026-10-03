/*
 * QEMU PCI device model for Korg 1212 (snd_korg1212)
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
#include "qemu/typedefs.h"

#define TYPE_PCIBASE_DEVICE "snd_korg1212_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define KORG1212_VENDOR_ID 0x10b5
#define KORG1212_DEVICE_ID 0x906d

#define KORG1212_PCI_CLASS_ID PCI_CLASS_MULTIMEDIA_AUDIO

#define MAILBOX0_OFFSET      0x40
#define MAILBOX1_OFFSET      0x44
#define MAILBOX2_OFFSET      0x48
#define MAILBOX3_OFFSET      0x4c
#define OUT_DOORBELL_OFFSET  0x60
#define IN_DOORBELL_OFFSET   0x64
#define STATUS_REG_OFFSET    0x68
#define PCI_CONTROL_OFFSET   0x6c
#define SENS_CONTROL_OFFSET  0x6e
#define DEV_VEND_ID_OFFSET   0x70

#define PCI_INT_ENABLE_BIT               0x00000100
#define PCI_DOORBELL_INT_ENABLE_BIT      0x00000200
#define LOCAL_INT_ENABLE_BIT             0x00010000
#define LOCAL_DOORBELL_INT_ENABLE_BIT    0x00020000
#define LOCAL_DMA1_INT_ENABLE_BIT        0x00080000

#define PCI_CMD_MEM_SPACE_ENABLE_BIT     0x0002
#define PCI_CMD_IO_SPACE_ENABLE_BIT      0x0001
#define PCI_CMD_BUS_MASTER_ENABLE_BIT    0x0004

#define PCI_STAT_PARITY_ERROR_BIT        0x8000
#define PCI_STAT_SYSTEM_ERROR_BIT        0x4000
#define PCI_STAT_MASTER_ABORT_RCVD_BIT   0x2000
#define PCI_STAT_TARGET_ABORT_RCVD_BIT   0x1000
#define PCI_STAT_TARGET_ABORT_SENT_BIT   0x0800

typedef enum CardState {
   K1212_STATE_NONEXISTENT,
   K1212_STATE_UNINITIALIZED,
   K1212_STATE_DSP_IN_PROCESS,
   K1212_STATE_DSP_COMPLETE,
   K1212_STATE_READY,
   K1212_STATE_OPEN,
   K1212_STATE_SETUP,
   K1212_STATE_PLAYING,
   K1212_STATE_MONITOR,
   K1212_STATE_CALIBRATING,
   K1212_STATE_ERRORSTOP,
   K1212_STATE_MAX_STATE
} CardState;

typedef enum korg1212_dbcnst {
   K1212_DB_RequestForData        = 0,
   K1212_DB_TriggerPlay           = 1,
   K1212_DB_SelectPlayMode        = 2,
   K1212_DB_ConfigureBufferMemory = 3,
   K1212_DB_RequestAdatTimecode   = 4,
   K1212_DB_SetClockSourceRate    = 5,
   K1212_DB_ConfigureMiscMemory   = 6,
   K1212_DB_TriggerFromAdat       = 7,
   K1212_DB_DMAERROR              = 0x80,
   K1212_DB_CARDSTOPPED           = 0x81,
   K1212_DB_RebootCard            = 0xA0,
   K1212_DB_BootFromDSPPage4      = 0xA4,
   K1212_DB_DSPDownloadDone       = 0xAE,
   K1212_DB_StartDSPDownload      = 0xAF
} korg1212_dbcnst;

#define K1212_DEBUG_LEVEL             0
#define MAX_COMMAND_RETRIES           5
#define COMMAND_ACK_MASK              0x8000
#define DOORBELL_VAL_MASK             0x00FF
#define CARD_BOOT_DELAY_IN_MS         10
#define CARD_BOOT_TIMEOUT             10
#define DSP_BOOT_DELAY_IN_MS          200

#define K1212SENSUPDATE_DELAY_IN_MS   50
#define ONE_RTC_TICK                  1
#define SENSCLKPULSE_WIDTH            4
#define LOADSHIFT_DELAY               4
#define INTERCOMMAND_DELAY            40
#define STOPCARD_DELAY                300
#define COMMAND_ACK_DELAY             13

#define SET_SENS_LOCALINIT_BITPOS     15
#define SET_SENS_DATA_BITPOS          10
#define SET_SENS_CLOCK_BITPOS         8
#define SET_SENS_LOADSHIFT_BITPOS     0

struct KorgAudioFrame {
    uint16_t frameData16[10];
    uint32_t frameData32[2];
    uint32_t timeCodeVal;
};

struct KorgAudioBuffer {
    struct KorgAudioFrame bufferData[1024];
};

struct KorgSharedBuffer {
    int16_t volumeData[12];
    uint32_t cardCommand;
    uint16_t routeData[12];
    uint32_t AdatTimeCode;
};

struct SensBits {
   union {
      struct {
         unsigned int leftChanVal:8;
         unsigned int leftChanId:8;
      } v;
      uint16_t  leftSensBits;
   } l;
   union {
      struct {
         unsigned int rightChanVal:8;
         unsigned int rightChanId:8;
      } v;
      uint16_t  rightSensBits;
   } r;
};

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
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t mailbox[4];
    uint32_t out_doorbell;
    uint32_t in_doorbell;
    uint32_t status_reg;
    uint32_t pci_control;
    uint16_t sens_control;
    uint32_t dev_vend_id;

    /* Simple state to emulate DSP boot/command acks */
    QEMUTimer dsp_timer;
    bool dsp_booted;
};


/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Very simple: if any interrupt sources are enabled and in_doorbell is non-zero,
     * assert the legacy INTx line. The driver uses IRQF_SHARED and only checks
     * inDoorbellPtr != 0.
     */
    if ((s->status_reg & (PCI_INT_ENABLE_BIT |
                          PCI_DOORBELL_INT_ENABLE_BIT |
                          LOCAL_INT_ENABLE_BIT |
                          LOCAL_DOORBELL_INT_ENABLE_BIT |
                          LOCAL_DMA1_INT_ENABLE_BIT)) &&
        s->in_doorbell) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The real hardware DMA format is not visible in the driver, only that
     * the driver programs physical addresses into DSP-visible memory and the
     * DSP pulls data. For driver probing/initialization, we don't need to
     * actually move data on the PCI bus, so leave this empty.
     */
    (void)s;
    (void)is_write;
}

/* Timer callback to emulate DSP download completion and subsequent boot sequence. */
static void pcibase_dsp_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    /* When DSP download completes, the hardware raises an interrupt with
     * doorbell value K1212_DB_DSPDownloadDone. The ISR will then wake the
     * waiting thread and call snd_korg1212_OnDSPDownloadComplete(), which
     * in turn issues more commands. We simply inject that doorbell value.
     */
    s->in_doorbell = K1212_DB_DSPDownloadDone;
    pcibase_update_irq(s);
}

/* Handle outbound command doorbells written by the driver. */
static void pcibase_handle_command(PCIBaseState *s, uint32_t doorbell)
{
    s->out_doorbell = doorbell;

    switch (doorbell) {
    case K1212_DB_RebootCard:
        /* After reboot, later the driver will start DSP download. No
         * observable register effects are required immediately.
         */
        break;
    case K1212_DB_StartDSPDownload:
        /* Schedule a timer to emulate DSP download completion.
         * The driver waits up to HZ * CARD_BOOT_TIMEOUT, but we can
         * complete quickly (e.g., after 10ms virtual time).
         */
        timer_mod(&s->dsp_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
        break;
    case K1212_DB_BootFromDSPPage4:
        /* After boot, the driver configures buffers and then starts
         * an idle monitor that generates periodic buffer interrupts.
         * For now, just mark DSP as booted.
         */
        s->dsp_booted = true;
        break;
    case K1212_DB_TriggerPlay:
        /* Playback start: real hardware would start DMA and generate
         * buffer interrupts. The driver advances buffers based on
         * interrupts, but basic operation doesn't require real data.
         * We do nothing here.
         */
        break;
    default:
        /* Other commands don't need side effects for probing. */
        break;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MAILBOX0_OFFSET:
        val = s->mailbox[0];
        break;
    case MAILBOX1_OFFSET:
        val = s->mailbox[1];
        break;
    case MAILBOX2_OFFSET:
        val = s->mailbox[2];
        break;
    case MAILBOX3_OFFSET:
        /*
         * The driver only reads mailbox3 when polling for command ack
         * and checks COMMAND_ACK_MASK and low byte. We simply return
         * whatever was last written by the driver.
         */
        val = s->mailbox[3];
        break;
    case OUT_DOORBELL_OFFSET:
        val = s->out_doorbell;
        break;
    case IN_DOORBELL_OFFSET:
        val = s->in_doorbell;
        break;
    case STATUS_REG_OFFSET:
        val = s->status_reg;
        break;
    case PCI_CONTROL_OFFSET:
        val = s->pci_control;
        break;
    case SENS_CONTROL_OFFSET:
        /* 16-bit access expected */
        val = s->sens_control;
        break;
    case DEV_VEND_ID_OFFSET:
        /* Device/Vendor ID as 32-bit value */
        val = s->dev_vend_id;
        break;
    default:
        /* Unused offsets return 0 */
        val = 0;
        break;
    }

    /* Mask to requested size */
    if (size < 8) {
        uint64_t mask = (1ULL << (size * 8)) - 1;
        val &= mask;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MAILBOX0_OFFSET:
        s->mailbox[0] = (uint32_t)val;
        break;
    case MAILBOX1_OFFSET:
        s->mailbox[1] = (uint32_t)val;
        break;
    case MAILBOX2_OFFSET:
        s->mailbox[2] = (uint32_t)val;
        break;
    case MAILBOX3_OFFSET:
        /* The driver writes arbitrary 32-bit values; later reads
         * low 16 bits and checks ack bit/doorbell echo.
         */
        s->mailbox[3] = (uint32_t)val;
        break;
    case OUT_DOORBELL_OFFSET:
        /* Doorbell write sends a command to the card */
        pcibase_handle_command(s, (uint32_t)val);
        break;
    case IN_DOORBELL_OFFSET:
        /* ISR writes back the doorbell value it observed to clear it. */
        s->in_doorbell &= ~((uint32_t)val);
        if (s->in_doorbell == 0) {
            /* Clear associated interrupt */
            pcibase_update_irq(s);
        }
        break;
    case STATUS_REG_OFFSET:
        /* Driver writes this to enable/disable interrupts. */
        s->status_reg = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case PCI_CONTROL_OFFSET:
        s->pci_control = (uint32_t)val;
        break;
    case SENS_CONTROL_OFFSET:
        /* 16-bit control register; driver bit-bangs sensitivity logic. */
        if (size == 2 || size == 4) {
            s->sens_control = (uint16_t)val;
        }
        break;
    case DEV_VEND_ID_OFFSET:
        /* Typically read-only; ignore writes. */
        break;
    default:
        /* Ignore writes to unmapped offsets */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The driver only uses BAR0 MMIO via pcim_iomap(pci, 0, 0).
     * BAR1/2 IO/mem resources are not used directly in the driver
     * snippet we have, so we return 0 for any PIO access.
     */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO behavior used by driver */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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

    s->mailbox[0] = 0;
    s->mailbox[1] = 0;
    s->mailbox[2] = 0;
    s->mailbox[3] = 0;
    s->out_doorbell = 0;
    s->in_doorbell = 0;
    s->status_reg = 0;
    s->pci_control = 0;
    s->sens_control = 0;
    s->dev_vend_id = (KORG1212_DEVICE_ID << 16) | KORG1212_VENDOR_ID;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->dsp_booted = false;

    /* Cancel any pending DSP timer */
    timer_del(&s->dsp_timer);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  KORG1212_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  KORG1212_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, KORG1212_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: driver uses BAR0 MMIO (pcim_iomap(pci, 0, 0)) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "korg1212-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->mailbox[0] = 0;
    s->mailbox[1] = 0;
    s->mailbox[2] = 0;
    s->mailbox[3] = 0;
    s->out_doorbell = 0;
    s->in_doorbell = 0;
    s->status_reg = 0;
    s->pci_control = 0;
    s->sens_control = 0;
    s->dev_vend_id = (KORG1212_DEVICE_ID << 16) | KORG1212_VENDOR_ID;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->dsp_booted = false;

    /* Initialize DSP timer */
    timer_init_ms(&s->dsp_timer, QEMU_CLOCK_VIRTUAL, pcibase_dsp_timer_cb, s);
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

    timer_del(&s->dsp_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_korg1212_pci",
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
