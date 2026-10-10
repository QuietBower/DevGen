/*
 * QEMU PCI device model for snd_sonicvibes
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

/* Removed missing include hw/audio/snd_sonicvibes_regs.h to fix build */

#define TYPE_PCIBASE_DEVICE "snd_sonicvibes_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SV_PCI_VENDOR_ID 0x5333 /* PCI_VENDOR_ID_S3 */
#define SV_PCI_DEVICE_ID 0xca00
#define SV_PCI_CLASS_ID  0x0401 /* PCI_CLASS_MULTIMEDIA_AUDIO */

#define SV_REG_CONTROL  0x00
#define SV_ENHANCED     0x01
#define SV_TEST         0x02
#define SV_REVERB       0x04
#define SV_WAVETABLE    0x08
#define SV_INTA         0x20
#define SV_RESET        0x80

#define SV_REG_IRQMASK  0x01
#define SV_DMAA_MASK    0x01
#define SV_DMAC_MASK    0x04
#define SV_SPEC_MASK    0x08
#define SV_UD_MASK      0x40
#define SV_MIDI_MASK    0x80

#define SV_REG_STATUS   0x02
#define SV_DMAA_IRQ     0x01
#define SV_DMAC_IRQ     0x04
#define SV_SPEC_IRQ     0x08
#define SV_UD_IRQ       0x40
#define SV_MIDI_IRQ     0x80

#define SV_REG_INDEX    0x04
#define SV_MCE          0x40
#define SV_TRD          0x80

#define SV_REG_DATA             0x05
#define SV_IREG_LEFT_ADC        0x00
#define SV_IREG_RIGHT_ADC       0x01
#define SV_IREG_LEFT_AUX1       0x02
#define SV_IREG_RIGHT_AUX1      0x03
#define SV_IREG_LEFT_CD         0x04
#define SV_IREG_RIGHT_CD        0x05
#define SV_IREG_LEFT_LINE       0x06
#define SV_IREG_RIGHT_LINE      0x07
#define SV_IREG_MIC             0x08
#define SV_IREG_GAME_PORT       0x09
#define SV_IREG_LEFT_SYNTH      0x0a
#define SV_IREG_RIGHT_SYNTH     0x0b
#define SV_IREG_LEFT_AUX2       0x0c
#define SV_IREG_RIGHT_AUX2      0x0d
#define SV_IREG_LEFT_ANALOG     0x0e
#define SV_IREG_RIGHT_ANALOG    0x0f
#define SV_IREG_LEFT_PCM        0x10
#define SV_IREG_RIGHT_PCM       0x11
#define SV_IREG_DMA_DATA_FMT    0x12
#define SV_IREG_PC_ENABLE       0x13
#define SV_IREG_UD_BUTTON       0x14
#define SV_IREG_REVISION        0x15
#define SV_IREG_ADC_OUTPUT_CTRL 0x16
#define SV_IREG_DMA_A_UPPER     0x18
#define SV_IREG_DMA_A_LOWER     0x19
#define SV_IREG_DMA_C_UPPER     0x1c
#define SV_IREG_DMA_C_LOWER     0x1d
#define SV_IREG_PCM_RATE_LOW    0x1e
#define SV_IREG_PCM_RATE_HIGH   0x1f
#define SV_IREG_SYNTH_RATE_LOW  0x20
#define SV_IREG_SYNCH_RATE_HIGH 0x21
#define SV_IREG_ADC_CLOCK       0x22
#define SV_IREG_ADC_ALT_RATE    0x23
#define SV_IREG_ADC_PLL_M       0x24
#define SV_IREG_ADC_PLL_N       0x25
#define SV_IREG_SYNTH_PLL_M     0x26
#define SV_IREG_SYNTH_PLL_N     0x27
#define SV_IREG_MPU401          0x2a
#define SV_IREG_DRIVE_CTRL      0x2b
#define SV_IREG_SRS_SPACE       0x2c
#define SV_IREG_SRS_CENTER      0x2d
#define SV_IREG_WAVE_SOURCE     0x2e
#define SV_IREG_ANALOG_POWER    0x30
#define SV_IREG_DIGITAL_POWER   0x31
#define SV_IREG_ADC_PLL         SV_IREG_ADC_PLL_M
#define SV_IREG_SYNTH_PLL       SV_IREG_SYNTH_PLL_M

#define SV_DMA_ADDR0            0x00
#define SV_DMA_ADDR1            0x01
#define SV_DMA_ADDR2            0x02
#define SV_DMA_ADDR3            0x03
#define SV_DMA_COUNT0           0x04
#define SV_DMA_COUNT1           0x05
#define SV_DMA_COUNT2           0x06
#define SV_DMA_MODE             0x0b
#define SV_DMA_RESET            0x0d
#define SV_DMA_MASK             0x0f

#define SV_RECSRC_RESERVED      (0x00 << 5)
#define SV_RECSRC_CD            (0x01 << 5)
#define SV_RECSRC_DAC           (0x02 << 5)
#define SV_RECSRC_AUX2          (0x03 << 5)
#define SV_RECSRC_LINE          (0x04 << 5)
#define SV_RECSRC_AUX1          (0x05 << 5)
#define SV_RECSRC_MIC           (0x06 << 5)
#define SV_RECSRC_OUT           (0x07 << 5)

#define SV_FULLRATE             48000
#define SV_REFFREQUENCY         24576000
#define SV_ADCMULT              512
#define SV_MODE_PLAY            1
#define SV_MODE_CAPTURE         2

/* The following mixer-related macros and structs are not used by the QEMU model
 * at runtime. To avoid pulling in ALSA kernel types into QEMU, they are
 * removed for compile-time correctness while preserving device behavior.
 */

/* QEMU-side BAR description helpers */
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
    struct {
        uint8_t control;
        uint8_t irqmask;
        uint8_t status;
        uint8_t index;
        uint8_t data;
        /* internal index register file */
        uint8_t iregs[0x40];
    } regs;

    /* Simple DMA engine model for A and C channels */
    struct {
        uint32_t addr;
        uint32_t count; /* bytes remaining */
        uint8_t mode;
        bool enabled;
    } dmaa, dmac;

    QEMUTimer dma_timer;
};

/* Forward declaration */
static void pcibase_update_irq(PCIBaseState *s);

static bool pcibase_irq_active(PCIBaseState *s)
{
    /* Active when any unmasked status bit is set */
    uint8_t active = s->regs.status & ~s->regs.irqmask;
    return active != 0;
}

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (pcibase_irq_active(s)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_do_dma_channel(PCIBaseState *s, bool is_c)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t *addrp;
    uint32_t *countp;
    uint8_t *modep;
    uint8_t irq_bit;

    if (is_c) {
        addrp = &s->dmac.addr;
        countp = &s->dmac.count;
        modep = &s->dmac.mode;
        irq_bit = SV_DMAC_IRQ;
    } else {
        addrp = &s->dmaa.addr;
        countp = &s->dmaa.count;
        modep = &s->dmaa.mode;
        irq_bit = SV_DMAA_IRQ;
    }

    if (*countp == 0) {
        return;
    }

    /* Perform a single linear transfer of 'count' bytes */
    uint32_t len = *countp;
    uint8_t *buf = g_malloc(len);

    if (!buf) {
        return;
    }

    /* mode bit1 selects direction: 0 = mem->device (read), 1 = device->mem (write)
     * The real hardware semantics are not fully documented in the driver; we just
     * perform a dummy pci_dma_{read,write} to exercise the API and then
     * immediately complete with IRQ.
     */
    bool to_pci = (*modep & 0x02) != 0;

    if (to_pci) {
        /* device -> system memory */
        memset(buf, 0, len);
        pci_dma_write(pdev, *addrp, buf, len);
    } else {
        /* system memory -> device */
        pci_dma_read(pdev, *addrp, buf, len);
    }

    g_free(buf);

    *countp = 0;

    /* Latch IRQ bit */
    s->regs.status |= irq_bit;
    pcibase_update_irq(s);
}

static void pcibase_dma_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    pcibase_do_dma_channel(s, false);
    pcibase_do_dma_channel(s, true);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return 0xff;
    }

    switch (addr) {
    case SV_REG_CONTROL:
        return s->regs.control;
    case SV_REG_IRQMASK:
        return s->regs.irqmask;
    case SV_REG_STATUS:
        return s->regs.status;
    case SV_REG_INDEX:
        return s->regs.index;
    case SV_REG_DATA: {
        uint8_t idx = s->regs.index & 0x3f;
        uint8_t val = s->regs.iregs[idx];
        /* Some indexed registers have simple fixed behaviour implied by init */
        if (idx == SV_IREG_REVISION) {
            /* Driver reads revision after init, return a plausible value */
            val = 0x01;
        }
        return val;
    }
    default:
        return 0xff;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }

    uint8_t v = (uint8_t)val;

    switch (addr) {
    case SV_REG_CONTROL:
        /* Reset bit observed in driver */
        s->regs.control = v;
        if (v & SV_RESET) {
            /* Clear internal state on reset */
            s->regs.status = 0;
            memset(s->regs.iregs, 0, sizeof(s->regs.iregs));
            s->dmaa.addr = s->dmaa.count = s->dmaa.mode = 0;
            s->dmac.addr = s->dmac.count = s->dmac.mode = 0;
        }
        break;
    case SV_REG_IRQMASK:
        /* Driver writes complement of enabled bits */
        s->regs.irqmask = v;
        pcibase_update_irq(s);
        break;
    case SV_REG_STATUS:
        /* Write-1-to-clear semantics for IRQ bits */
        s->regs.status &= ~(v & (SV_DMAA_IRQ | SV_DMAC_IRQ | SV_SPEC_IRQ | SV_UD_IRQ | SV_MIDI_IRQ));
        pcibase_update_irq(s);
        break;
    case SV_REG_INDEX:
        s->regs.index = v;
        break;
    case SV_REG_DATA: {
        uint8_t idx = s->regs.index & 0x3f;
        s->regs.iregs[idx] = v;
        break;
    }
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1 && size != 4) {
        return (size == 4) ? 0xffffffffU : 0xffU;
    }

    /* Emulate the DDMA A/C register blocks and core SB/enh/io regions as simple
     * shadows. For simplicity we expose the same behaviour as MMIO region 0.
     */

    /* Map control / irqmask / status / index / data in low offsets */
    if (size == 1) {
        switch (addr) {
        case SV_REG_CONTROL:
            return s->regs.control;
        case SV_REG_IRQMASK:
            return s->regs.irqmask;
        case SV_REG_STATUS:
            return s->regs.status;
        case SV_REG_INDEX:
            return s->regs.index;
        case SV_REG_DATA: {
            uint8_t idx = s->regs.index & 0x3f;
            uint8_t val = s->regs.iregs[idx];
            if (idx == SV_IREG_REVISION) {
                val = 0x01;
            }
            return val;
        }
        default:
            break;
        }
    }

    /* DDMA A/C: addr and count as 32-bit at offsets SV_DMA_ADDR0/COUNT0 */
    if (size == 4) {
        switch (addr) {
        case SV_DMA_ADDR0:
            return s->dmaa.addr;
        case SV_DMA_COUNT0:
            return s->dmaa.count;
        case SV_DMA_ADDR0 + 0x10:
            return s->dmac.addr;
        case SV_DMA_COUNT0 + 0x10:
            return s->dmac.count;
        default:
            break;
        }
    }

    return (size == 4) ? 0xffffffffU : 0xffU;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1 && size != 4) {
        return;
    }

    if (size == 1) {
        uint8_t v = (uint8_t)val;
        switch (addr) {
        case SV_REG_CONTROL:
            s->regs.control = v;
            if (v & SV_RESET) {
                s->regs.status = 0;
                memset(s->regs.iregs, 0, sizeof(s->regs.iregs));
                s->dmaa.addr = s->dmaa.count = s->dmaa.mode = 0;
                s->dmac.addr = s->dmac.count = s->dmac.mode = 0;
            }
            break;
        case SV_REG_IRQMASK:
            s->regs.irqmask = v;
            pcibase_update_irq(s);
            break;
        case SV_REG_STATUS:
            s->regs.status &= ~(v & (SV_DMAA_IRQ | SV_DMAC_IRQ | SV_SPEC_IRQ | SV_UD_IRQ | SV_MIDI_IRQ));
            pcibase_update_irq(s);
            break;
        case SV_REG_INDEX:
            s->regs.index = v;
            break;
        case SV_REG_DATA: {
            uint8_t idx = s->regs.index & 0x3f;
            s->regs.iregs[idx] = v;
            break;
        }
        case SV_DMA_MODE:
            s->dmaa.mode = v;
            break;
        case SV_DMA_MODE + 0x10:
            s->dmac.mode = v;
            break;
        case SV_DMA_RESET:
        case SV_DMA_RESET + 0x10:
            /* simple reset of DMA counters */
            s->dmaa.count = 0;
            s->dmac.count = 0;
            break;
        case SV_DMA_MASK:
        case SV_DMA_MASK + 0x10:
            /* ignore mask register, just accept */
            break;
        default:
            break;
        }
    } else if (size == 4) {
        uint32_t v32 = (uint32_t)val;
        switch (addr) {
        case SV_DMA_ADDR0:
            s->dmaa.addr = v32;
            break;
        case SV_DMA_COUNT0:
            s->dmaa.count = v32 & 0x00ffffffU;
            break;
        case SV_DMA_ADDR0 + 0x10:
            s->dmac.addr = v32;
            break;
        case SV_DMA_COUNT0 + 0x10:
            s->dmac.count = (v32 & 0x00ffffffU) << 1; /* dmac works in words */
            break;
        default:
            break;
        }
    }

    /* Whenever counts are programmed, arm a timer to simulate transfer */
    if (s->dmaa.count || s->dmac.count) {
        timer_mod(&s->dma_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
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

    s->regs.control = 0;
    s->regs.irqmask = 0xff;
    s->regs.status = 0x00;
    s->regs.index = 0x00;
    s->regs.data = 0x00;
    memset(s->regs.iregs, 0, sizeof(s->regs.iregs));

    s->dmaa.addr = s->dmaa.count = s->dmaa.mode = 0;
    s->dmac.addr = s->dmac.count = s->dmac.mode = 0;

    s->intr_status = 0;
    s->intr_mask = 0;

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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  SV_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SV_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SV_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* The real SonicVibes is a conventional PCI device; do not expose PCIe caps
     * here, as that can confuse legacy drivers and resource allocation.
     */

    /* BAR Initialization according to driver expectations:
     * BAR0: legacy SB (I/O), BAR1: enhanced/audio core (I/O),
     * BAR2: DDMA A (I/O), BAR3: DDMA C (I/O), BAR4: game port.
     * Size is chosen as 0x10 for SB/enh/DDMA and 0x10 for game to cover used regs.
     */
    s->num_bars = 5;

    /* Keep previously defined BAR0/1/4 mappings for SB/enh/game. */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x10;
    s->bar_info[0].name = "sonicvibes-sb";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 0x10;
    s->bar_info[1].name = "sonicvibes-enh";

    /* Adjust BAR2 and BAR3 to represent DDMA-A and DDMA-C I/O windows
     * so that request_region() in the driver succeeds instead of probing
     * at an arbitrary legacy address like 0x3800.
     */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 0x10;
    s->bar_info[2].name = "sonicvibes-ddma-a";

    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = 0x10;
    s->bar_info[3].name = "sonicvibes-ddma-c";

    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_PIO;
    s->bar_info[4].size = 0x10;
    s->bar_info[4].name = "sonicvibes-game";

    s->has_msi = false;
    s->has_msix = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize DMA timer */
    timer_init_ms(&s->dma_timer, QEMU_CLOCK_VIRTUAL, pcibase_dma_timer_cb, s);

    /* Reset internal state */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    timer_del(&s->dma_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_sonicvibes_pci",
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

