/*
 * QEMU PCI device model for snd_rme96_pci (RME Digi96 family)
 * Phase 2: Functional behavior
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
#include "hw/audio/soundhw.h"

#define TYPE_PCIBASE_DEVICE "snd_rme96_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define RME96_SPDIF_NCHANNELS 2
#define RME96_BUFFER_SIZE 0x10000
#define RME96_IO_SIZE 0x60000
#define RME96_IO_PLAY_BUFFER      0x0
#define RME96_IO_REC_BUFFER       0x10000
#define RME96_IO_CONTROL_REGISTER 0x20000
#define RME96_IO_ADDITIONAL_REG   0x20004
#define RME96_IO_CONFIRM_PLAY_IRQ 0x20008
#define RME96_IO_CONFIRM_REC_IRQ  0x2000C
#define RME96_IO_SET_PLAY_POS     0x40000
#define RME96_IO_RESET_PLAY_POS   0x4FFFC
#define RME96_IO_SET_REC_POS      0x50000
#define RME96_IO_RESET_REC_POS    0x5FFFC
#define RME96_IO_GET_PLAY_POS     0x20000
#define RME96_IO_GET_REC_POS      0x30000
#define RME96_WCR_START     (1 << 0)
#define RME96_WCR_START_2   (1 << 1)
#define RME96_WCR_GAIN_0    (1 << 2)
#define RME96_WCR_GAIN_1    (1 << 3)
#define RME96_WCR_MODE24    (1 << 4)
#define RME96_WCR_MODE24_2  (1 << 5)
#define RME96_WCR_BM        (1 << 6)
#define RME96_WCR_BM_2      (1 << 7)
#define RME96_WCR_ADAT      (1 << 8)
#define RME96_WCR_FREQ_0    (1 << 9)
#define RME96_WCR_FREQ_1    (1 << 10)
#define RME96_WCR_DS        (1 << 11)
#define RME96_WCR_PRO       (1 << 12)
#define RME96_WCR_EMP       (1 << 13)
#define RME96_WCR_SEL       (1 << 14)
#define RME96_WCR_MASTER    (1 << 15)
#define RME96_WCR_PD        (1 << 16)
#define RME96_WCR_INP_0     (1 << 17)
#define RME96_WCR_INP_1     (1 << 18)
#define RME96_WCR_THRU_0    (1 << 19)
#define RME96_WCR_THRU_1    (1 << 20)
#define RME96_WCR_THRU_2    (1 << 21)
#define RME96_WCR_THRU_3    (1 << 22)
#define RME96_WCR_THRU_4    (1 << 23)
#define RME96_WCR_THRU_5    (1 << 24)
#define RME96_WCR_THRU_6    (1 << 25)
#define RME96_WCR_THRU_7    (1 << 26)
#define RME96_WCR_DOLBY     (1 << 27)
#define RME96_WCR_MONITOR_0 (1 << 28)
#define RME96_WCR_MONITOR_1 (1 << 29)
#define RME96_WCR_ISEL      (1 << 30)
#define RME96_WCR_IDIS      (1 << 31)
#define RME96_WCR_BITPOS_GAIN_0 2
#define RME96_WCR_BITPOS_GAIN_1 3
#define RME96_WCR_BITPOS_FREQ_0 9
#define RME96_WCR_BITPOS_FREQ_1 10
#define RME96_WCR_BITPOS_INP_0 17
#define RME96_WCR_BITPOS_INP_1 18
#define RME96_WCR_BITPOS_MONITOR_0 28
#define RME96_WCR_BITPOS_MONITOR_1 29
#define RME96_RCR_AUDIO_ADDR_MASK 0xFFFF
#define RME96_RCR_IRQ_2     (1 << 16)
#define RME96_RCR_T_OUT     (1 << 17)
#define RME96_RCR_DEV_ID_0  (1 << 21)
#define RME96_RCR_DEV_ID_1  (1 << 22)
#define RME96_RCR_LOCK      (1 << 23)
#define RME96_RCR_VERF      (1 << 26)
#define RME96_RCR_F0        (1 << 27)
#define RME96_RCR_F1        (1 << 28)
#define RME96_RCR_F2        (1 << 29)
#define RME96_RCR_AUTOSYNC  (1 << 30)
#define RME96_RCR_IRQ       (1 << 31)
#define RME96_RCR_BITPOS_F0 27
#define RME96_RCR_BITPOS_F1 28
#define RME96_RCR_BITPOS_F2 29
#define RME96_AR_WSEL       (1 << 0)
#define RME96_AR_ANALOG     (1 << 1)
#define RME96_AR_FREQPAD_0  (1 << 2)
#define RME96_AR_FREQPAD_1  (1 << 3)
#define RME96_AR_FREQPAD_2  (1 << 4)
#define RME96_AR_PD2        (1 << 5)
#define RME96_AR_DAC_EN     (1 << 6)
#define RME96_AR_CLATCH     (1 << 7)
#define RME96_AR_CCLK       (1 << 8)
#define RME96_AR_CDATA      (1 << 9)
#define RME96_AR_BITPOS_F0 2
#define RME96_AR_BITPOS_F1 3
#define RME96_AR_BITPOS_F2 4
#define RME96_MONITOR_TRACKS_1_2 0
#define RME96_MONITOR_TRACKS_3_4 1
#define RME96_MONITOR_TRACKS_5_6 2
#define RME96_MONITOR_TRACKS_7_8 3
#define RME96_ATTENUATION_0 0
#define RME96_ATTENUATION_6 1
#define RME96_ATTENUATION_12 2
#define RME96_ATTENUATION_18 3
#define RME96_INPUT_OPTICAL 0
#define RME96_INPUT_COAXIAL 1
#define RME96_INPUT_INTERNAL 2
#define RME96_INPUT_XLR 3
#define RME96_INPUT_ANALOG 4
#define RME96_CLOCKMODE_SLAVE 0
#define RME96_CLOCKMODE_MASTER 1
#define RME96_CLOCKMODE_WORDCLOCK 2
#define RME96_SMALL_BLOCK_SIZE 2048
#define RME96_LARGE_BLOCK_SIZE 8192
#define RME96_AD1852_VOL_BITS 14
#define RME96_AD1855_VOL_BITS 10
#define RME96_TB_START_PLAYBACK 1
#define RME96_TB_START_CAPTURE 2
#define RME96_TB_STOP_PLAYBACK 4
#define RME96_TB_STOP_CAPTURE 8
#define RME96_TB_RESET_PLAYPOS 16
#define RME96_TB_RESET_CAPTUREPOS 32
#define RME96_TB_CLEAR_PLAYBACK_IRQ 64
#define RME96_TB_CLEAR_CAPTURE_IRQ 128
#define RME96_RESUME_PLAYBACK (RME96_TB_START_PLAYBACK)
#define RME96_RESUME_CAPTURE (RME96_TB_START_CAPTURE)
#define RME96_RESUME_BOTH (RME96_RESUME_PLAYBACK | RME96_RESUME_CAPTURE)
#define RME96_START_PLAYBACK (RME96_TB_START_PLAYBACK | RME96_TB_RESET_PLAYPOS)
#define RME96_START_CAPTURE (RME96_TB_START_CAPTURE | RME96_TB_RESET_CAPTUREPOS)
#define RME96_START_BOTH (RME96_START_PLAYBACK | RME96_START_CAPTURE)
#define RME96_STOP_PLAYBACK (RME96_TB_STOP_PLAYBACK | RME96_TB_CLEAR_PLAYBACK_IRQ)
#define RME96_STOP_CAPTURE (RME96_TB_STOP_CAPTURE | RME96_TB_CLEAR_CAPTURE_IRQ)
#define RME96_STOP_BOTH (RME96_STOP_PLAYBACK | RME96_STOP_CAPTURE)

/* PCI identifiers: use first entry from snd_rme96_ids (XILINX, PCI_DEVICE_ID_RME_DIGI96) */
#define RME96_VENDOR_ID 0x10ee
#define RME96_DEVICE_ID 0x3fc0
#define RME96_CLASS_ID  0x0401

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t wcreg;              /* write control / status register */
    uint32_t wcreg_spdif;
    uint32_t wcreg_spdif_stream;
    uint32_t rcreg;              /* read status register */
    uint32_t areg;               /* additional register */
    uint16_t vol[2];

    /* Simple audio engine state derived from driver expectations */
    uint32_t play_pos;           /* byte offset within playback buffer */
    uint32_t rec_pos;            /* byte offset within capture buffer */
    uint32_t playback_periodsize;
    uint32_t capture_periodsize;
    uint8_t playback_frlog;
    uint8_t capture_frlog;

    /* IRQ bookkeeping */
    bool irq_play_pending;
    bool irq_rec_pending;

    /* playback / capture running flags (from START bits in wcreg) */
    bool playback_running;
    bool capture_running;

    /* QEMU timer to simulate period interrupts and buffer pointer advance */
    QEMUTimer engine_timer;
};

/* Forward declaration */
static void pcibase_update_irq(PCIBaseState *s);
static void pcibase_engine_timer_cb(void *opaque);

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* IRQ is asserted whenever either IRQ bit is set in rcreg */
    if ((s->rcreg & (RME96_RCR_IRQ | RME96_RCR_IRQ_2))) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Engine timer callback emulates hardware advancing buffer pointers
 * and generating IRQs at the end of each period when running. */
static void pcibase_engine_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    bool any_running = false;

    /* playback position update */
    if (s->playback_running && s->playback_periodsize) {
        any_running = true;
        s->play_pos += s->playback_periodsize;
        if (s->play_pos >= RME96_BUFFER_SIZE) {
            s->play_pos -= RME96_BUFFER_SIZE;
        }
        /* set playback IRQ bit */
        s->rcreg |= RME96_RCR_IRQ;
    }

    /* capture position update */
    if (s->capture_running && s->capture_periodsize) {
        any_running = true;
        s->rec_pos += s->capture_periodsize;
        if (s->rec_pos >= RME96_BUFFER_SIZE) {
            s->rec_pos -= RME96_BUFFER_SIZE;
        }
        /* set capture IRQ2 bit */
        s->rcreg |= RME96_RCR_IRQ_2;
    }

    if ((s->rcreg & (RME96_RCR_IRQ | RME96_RCR_IRQ_2))) {
        pcibase_update_irq(s);
    }

    /* Re-arm timer if any engine is still running */
    if (any_running) {
        /* Use a fixed small interval; exact timing is not modeled in driver */
        timer_mod(&s->engine_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * The driver uses MMAP I/O memory and memcpy_to/fromio to/from the buffers.
 * The hardware DMA format is not described, so we do not implement
 * bus mastering here. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
    /* No explicit DMA engine described in the driver source. */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Implement only accesses observed in the driver. */

    /* Playback and record buffer windows are memory regions accessed
     * via memcpy_toio/memcpy_fromio and mmap. QEMU will handle these
     * accesses via .read/.write callbacks as byte/word accesses.
     * We do not interpret their contents here. */

    switch (addr) {
    case RME96_IO_CONTROL_REGISTER: /* also used as GET_PLAY_POS base in driver macros */
        /* For reads from the control register, driver caches wcreg here. */
        val = s->wcreg;
        break;

    case RME96_IO_ADDITIONAL_REG:
        val = s->areg;
        break;

    /* RME96_IO_GET_PLAY_POS has the same offset as RME96_IO_CONTROL_REGISTER,
     * so we must not add a duplicate case label here. Its reads are already
     * handled by the RME96_IO_CONTROL_REGISTER case above. */

    case RME96_IO_GET_REC_POS:
        val = (uint32_t)(s->rec_pos & RME96_RCR_AUDIO_ADDR_MASK);
        break;

    default:
        /* Within buffer ranges, just return 0 for unmapped reads. */
        val = 0;
        break;
    }

    /* Truncate/extend according to access size */
    if (size == 1) {
        return (uint8_t)val;
    } else if (size == 2) {
        return (uint16_t)val;
    } else {
        return (uint32_t)val;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    /* Handle writes to the control and additional registers and
     * buffer pointer manipulation, as used by the driver. */

    switch (addr) {
    case RME96_IO_CONTROL_REGISTER:
        /* main control register; driver writes wcreg here frequently */
        s->wcreg = v32;

        /* Update running flags based on START bits */
        s->playback_running = (s->wcreg & RME96_WCR_START) != 0;
        s->capture_running  = (s->wcreg & RME96_WCR_START_2) != 0;

        /* When any engine starts, arm timer if not already running */
        if ((s->playback_running || s->capture_running) &&
            !timer_pending(&s->engine_timer)) {
            timer_mod(&s->engine_timer,
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
        }
        break;

    case RME96_IO_ADDITIONAL_REG:
        /* additional/SPDIF/analog control register */
        s->areg = v32;
        break;

    case RME96_IO_RESET_PLAY_POS:
        /* driver writes 0 here to reset playback pointer */
        s->play_pos = 0;
        break;

    case RME96_IO_RESET_REC_POS:
        /* driver writes 0 here to reset capture pointer */
        s->rec_pos = 0;
        break;

    case RME96_IO_CONFIRM_PLAY_IRQ:
        /* Driver writes 0 here to clear playback IRQ (W1C semantics). */
        s->rcreg &= ~RME96_RCR_IRQ;
        pcibase_update_irq(s);
        break;

    case RME96_IO_CONFIRM_REC_IRQ:
        /* Driver writes 0 here to clear capture IRQ. */
        s->rcreg &= ~RME96_RCR_IRQ_2;
        pcibase_update_irq(s);
        break;

    default:
        /* Buffer regions and SET_PLAY/SET_REC_POS windows are used by
         * memcpy_toio/ fromio. For SET_*_POS in resume(): the driver
         * writes at base + offset; hardware essentially sets the
         * current pointer to that offset. We model this minimally. */
        if (addr >= RME96_IO_SET_PLAY_POS &&
            addr < (RME96_IO_SET_PLAY_POS + RME96_BUFFER_SIZE)) {
            /* write at SET_PLAY_POS + offset sets playback pointer */
            s->play_pos = (uint32_t)(addr - RME96_IO_SET_PLAY_POS);
        } else if (addr >= RME96_IO_SET_REC_POS &&
                   addr < (RME96_IO_SET_REC_POS + RME96_BUFFER_SIZE)) {
            s->rec_pos = (uint32_t)(addr - RME96_IO_SET_REC_POS);
        }
        /* Writes to PLAY/REC buffer memory itself are handled as raw
         * memory; we do not add extra semantics. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
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

    /* Reset internal state to defaults similar to snd_rme96_create() */
    s->wcreg = RME96_WCR_FREQ_1 | /* 44.1 kHz playback */
               RME96_WCR_SEL |    /* normal playback */
               RME96_WCR_MASTER | /* master clock mode */
               RME96_WCR_INP_0;   /* coaxial input */

    s->areg = RME96_AR_FREQPAD_1; /* 44.1 kHz analog capture */

    s->wcreg_spdif = 0;
    s->wcreg_spdif_stream = 0;
    s->rcreg = 0;

    s->vol[0] = 0;
    s->vol[1] = 0;

    s->play_pos = 0;
    s->rec_pos = 0;
    s->playback_periodsize = 0;
    s->capture_periodsize = 0;
    s->playback_frlog = 1; /* default 2ch, 16bit */
    s->capture_frlog = 1;

    s->playback_running = false;
    s->capture_running = false;
    s->irq_play_pending = false;
    s->irq_rec_pending = false;

    timer_del(&s->engine_timer);

    /* Program initial hardware-visible registers */
    pcibase_mmio_write(s, RME96_IO_CONTROL_REGISTER, s->wcreg, 4);
    pcibase_mmio_write(s, RME96_IO_ADDITIONAL_REG, s->areg, 4);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  RME96_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RME96_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RME96_CLASS_ID );
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
    s->bar_info[0].size = RME96_IO_SIZE;
    s->bar_info[0].name = "rme96-mmio";
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

    /* Initialize timer used to simulate engine behavior */
    timer_init_ms(&s->engine_timer, QEMU_CLOCK_VIRTUAL,
                  pcibase_engine_timer_cb, s);

    /* Reset internal state and registers */
    pcibase_reset(DEVICE(pdev));
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

    timer_del(&s->engine_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_rme96_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(wcreg, PCIBaseState),
        VMSTATE_UINT32(wcreg_spdif, PCIBaseState),
        VMSTATE_UINT32(wcreg_spdif_stream, PCIBaseState),
        VMSTATE_UINT32(rcreg, PCIBaseState),
        VMSTATE_UINT32(areg, PCIBaseState),
        VMSTATE_UINT16_ARRAY(vol, PCIBaseState, 2),
        VMSTATE_UINT32(play_pos, PCIBaseState),
        VMSTATE_UINT32(rec_pos, PCIBaseState),
        VMSTATE_UINT32(playback_periodsize, PCIBaseState),
        VMSTATE_UINT32(capture_periodsize, PCIBaseState),
        VMSTATE_UINT8(playback_frlog, PCIBaseState),
        VMSTATE_UINT8(capture_frlog, PCIBaseState),
        VMSTATE_BOOL(playback_running, PCIBaseState),
        VMSTATE_BOOL(capture_running, PCIBaseState),
        VMSTATE_BOOL(irq_play_pending, PCIBaseState),
        VMSTATE_BOOL(irq_rec_pending, PCIBaseState),
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
