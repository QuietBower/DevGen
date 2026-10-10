/*
 * QEMU PCI device model for Creative CA0106 (minimal emulation for ALSA driver)
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
#include "hw/hw.h"

#define TYPE_PCIBASE_DEVICE "snd_ca0106_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x1102
#define PCIBASE_DEVICE_ID 0x0007
#define PCIBASE_CLASS_ID  0x0401

#define CA0106_PTR              0x00
#define CA0106_DATA             0x04
#define CA0106_IPR              0x08
#define CA0106_INTE             0x0c
#define CA0106_HCFG             0x14
#define CA0106_GPIO             0x18
#define CA0106_AC97DATA         0x1c
#define CA0106_AC97ADDRESS      0x1e

#define PLAYBACK_LIST_ADDR      0x00
#define PLAYBACK_DMA_ADDR       0x04
#define PLAYBACK_LIST_PTR       0x02
#define PLAYBACK_LIST_SIZE      0x01
#define PLAYBACK_PERIOD_SIZE    0x05
#define PLAYBACK_POINTER        0x06
#define CAPTURE_DMA_ADDR        0x10
#define CAPTURE_BUFFER_SIZE     0x11
#define CAPTURE_POINTER         0x12

#define BASIC_INTERRUPT         0x40
#define SPCS0                   0x41
#define SPCS1                   0x42
#define SPCS2                   0x43
#define SPCS3                   0x44
#define SPDIF_SELECT1           0x45
#define CAPTURE_SOURCE          0x60
#define CAPTURE_VOLUME1         0x61
#define CAPTURE_VOLUME2         0x62
#define PLAYBACK_ROUTING1       0x63
#define PLAYBACK_ROUTING2       0x64
#define PLAYBACK_MUTE           0x65
#define PLAYBACK_VOLUME1        0x66
#define CAPTURE_ROUTING1        0x67
#define CAPTURE_ROUTING2        0x68
#define CAPTURE_MUTE            0x69
#define PLAYBACK_VOLUME2        0x6a
#define MIDI_UART_A_DATA        0x6c
#define MIDI_UART_B_DATA        0x6e
#define CAPTURE_CONTROL         0x71
#define SPDIF_SELECT2           0x72
#define EXTENDED_INT_MASK       0x75
#define EXTENDED_INT            0x76
#define SPI                     0x7a
#define I2C_A                   0x7b
#define I2C_D1                  0x7d

#define HCFG_AUDIOENABLE        0x00000001
#define HCFG_AC97               0x00000008
#define HCFG_CAPTURE_S32_LE     0x00000400
#define HCFG_PLAYBACK_S32_LE    0x00000800

#define I2C_A_ADC_START         0x00000100
#define I2C_A_ADC_ABORT         0x00000200
#define I2C_A_ADC_LAST          0x00000400
#define I2C_A_ADC_ADD           0x00000034

#define SPCS_COPYRIGHT          0x00000004
#define SPCS_CLKACCY_1000PPM    0x00000000
#define SPCS_SOURCENUM_UNSPEC   0x00000000
#define SPCS_EMPHASIS_NONE      0x00000000
#define SPCS_GENERATIONSTATUS   0x00008000
#define SPCS_CHANNELNUM_LEFT    0x00100000
#define SPCS_SAMPLERATE_48      0x02000000

#define INTE_MIDI_TX_A          0x00000002
#define INTE_MIDI_RX_A          0x00000004
#define INTE_MIDI_TX_B          0x00010000
#define INTE_MIDI_RX_B          0x00020000

#define IPR_MIDI_TX_A           0x00000002
#define IPR_MIDI_RX_A           0x00000004
#define IPR_MIDI_TX_B           0x00010000
#define IPR_MIDI_RX_B           0x00020000

#define CA0106_MIDI_INPUT_AVAIL     0x80
#define CA0106_MIDI_OUTPUT_READY    0x40

#define CA0106_MPU401_ACK           0xfe
#define CA0106_MPU401_RESET         0xff
#define CA0106_MPU401_ENTER_UART    0x3f

#define CA0106_MIDI_CHAN_A      0x1
#define CA0106_MIDI_CHAN_B      0x2

#define SPI_REG_SHIFT           9
#define SPI_REG_MASK            0x1ff

#define SPI_LDA1_REG            0
#define SPI_RDA1_REG            1
#define SPI_PL_REG              2
#define SPI_FMT_REG             3
#define SPI_LDA2_REG            4
#define SPI_RDA2_REG            5
#define SPI_LDA3_REG            6
#define SPI_RDA3_REG            7
#define SPI_MASTDA_REG          8
#define SPI_MS_REG              10
#define SPI_LDA4_REG            13
#define SPI_RDA4_REG            14
#define SPI_DACD4_REG           15

#define SPI_FMT_BIT_I2S         (2<<0)
#define SPI_IWL_BIT_24          (2<<4)
#define SPI_PL_BIT_L_L          (1<<5)
#define SPI_PL_BIT_R_R          (2<<7)
#define SPI_IZD_BIT             (0<<4)
#define SPI_DACD0_BIT           (1<<1)
#define SPI_DACD1_BIT           (1<<2)
#define SPI_DACD2_BIT           (1<<3)
#define SPI_DACD4_BIT           (1<<0)

#define ADC_MUX_LINEIN          0x00000004
#define ADC_MUX                 0x00000015
#define ADC_MASTER              0x0000000c
#define ADC_ATTEN_ADCL          0x0000000e
#define ADC_ATTEN_ADCR          0x0000000f

#define SPI_DMUTE4_BIT          (1<<2)
#define SPI_DMUTE0_BIT          (1<<3)
#define SPI_DMUTE1_BIT          (1<<4)
#define SPI_DMUTE2_BIT          (1<<5)
#define SPI_DMUTE4_REG          15
#define SPI_DMUTE0_REG          9
#define SPI_DMUTE1_REG          9
#define SPI_DMUTE2_REG          9

#define PLAYBACK_FRONT_CHANNEL      0
#define PLAYBACK_REAR_CHANNEL       1
#define PLAYBACK_CENTER_LFE_CHANNEL 2
#define PLAYBACK_UNKNOWN_CHANNEL    3

#define CONTROL_FRONT_CHANNEL       0
#define CONTROL_CENTER_LFE_CHANNEL  1
#define CONTROL_UNKNOWN_CHANNEL     2
#define CONTROL_REAR_CHANNEL        3

#define PCM_FRONT_CHANNEL           0
#define PCM_REAR_CHANNEL            1
#define PCM_CENTER_LFE_CHANNEL      2
#define PCM_UNKNOWN_CHANNEL         3

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
    uint32_t hcfg;
    uint32_t ipr;
    uint32_t inte;
    uint32_t gpio;
    uint32_t ac97data;
    uint32_t ac97address;

    /* Pointer/data indirection for internal register space */
    uint32_t ptr_reg;      /* last value written to CA0106_PTR */

    /* Simple internal register space addressed via CA0106_PTR/CA0106_DATA */
    uint32_t internal_regs[0x100]; /* 256 32-bit registers, indexed by 'reg' field */

    /* For very crude DMA/position emulation */
    uint32_t playback_list_addr[4];
    uint32_t playback_list_size[4];
    uint32_t playback_list_ptr[4];
    uint32_t playback_dma_addr[4];
    uint32_t playback_period_size[4];
    uint32_t playback_pointer[4];

    uint32_t capture_dma_addr[4];
    uint32_t capture_buffer_size[4];
    uint32_t capture_pointer[4];

    uint32_t basic_interrupt;
    uint32_t extended_int_mask;
    uint32_t extended_int;

    /* very simple timer to advance DMA and fire interrupts */
    QEMUTimer *dma_timer;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Raise IRQ when any enabled IPR bit is set; lower otherwise. */
    if (s->ipr & s->inte) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The real hardware performs complex DMA from host buffers based
     * on the descriptor lists programmed by the driver. The driver
     * does not read back audio data, it only relies on pointer
     * updates and interrupts. Implementing true DMA is therefore
     * unnecessary for basic probing and operation and is omitted to
     * avoid guessing the undocumented format.
     */
    (void)s;
    (void)is_write;
}

static void pcibase_dma_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    int ch;

    /* Very crude emulation: if BASIC_INTERRUPT bit for channel is set,
     * advance playback/capture pointers and set corresponding EXTENDED_INT
     * bits so that the driver's interrupt handler will see activity.
     */

    for (ch = 0; ch < 4; ch++) {
        uint32_t mask_play = (0x1u << ch);
        uint32_t mask_cap  = (0x100u << ch);
        uint32_t ext_play_mask = (0x10u << ch);
        uint32_t ext_cap_mask  = (0x110000u << ch);

        if (s->basic_interrupt & mask_play) {
            /* advance playback pointer within a fake buffer */
            if (s->playback_period_size[ch]) {
                s->playback_pointer[ch] += s->playback_period_size[ch];
            }
            /* set extended interrupt bit if enabled */
            if (s->extended_int_mask & ext_play_mask) {
                s->extended_int |= ext_play_mask;
            }
        }

        if (s->basic_interrupt & mask_cap) {
            if (s->capture_buffer_size[ch]) {
                /* move by quarter buffer */
                uint32_t step = s->capture_buffer_size[ch] >> 2;
                if (!step) {
                    step = 256;
                }
                s->capture_pointer[ch] += step;
                if (s->capture_pointer[ch] >= (s->capture_buffer_size[ch] >> 16)) {
                    s->capture_pointer[ch] = 0;
                }
            }
            if (s->extended_int_mask & ext_cap_mask) {
                s->extended_int |= ext_cap_mask;
            }
        }
    }

    /* If any extended_int bit is set, reflect this in IPR and signal IRQ. */
    if (s->extended_int) {
        s->ipr |= 0x1; /* arbitrary nonzero to show interrupt pending */
    }
    pcibase_update_irq(s);

    /* re-arm timer to keep activity going while interrupts are enabled */
    timer_mod(s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* All accesses are little endian; size can be 1,2,4 */
    switch (addr) {
    case CA0106_PTR:
        /* driver only writes this, but return shadow for completeness */
        val = s->ptr_reg;
        break;
    case CA0106_DATA: {
        /* interpret ptr_reg: upper 16 bits = reg, lower 16 bits = chn */
        uint32_t reg = (s->ptr_reg >> 16) & 0xffffu;
        uint32_t chn = s->ptr_reg & 0xffffu;
        uint32_t data = 0;

        /* Many registers in driver use chn up to 3; internal_regs indexed by reg */
        switch (reg) {
        case PLAYBACK_LIST_ADDR:
            if (chn < 4) {
                data = s->playback_list_addr[chn];
            }
            break;
        case PLAYBACK_LIST_SIZE:
            if (chn < 4) {
                data = s->playback_list_size[chn];
            }
            break;
        case PLAYBACK_LIST_PTR:
            if (chn < 4) {
                data = s->playback_list_ptr[chn];
            }
            break;
        case PLAYBACK_DMA_ADDR:
            if (chn < 4) {
                data = s->playback_dma_addr[chn];
            }
            break;
        case PLAYBACK_PERIOD_SIZE:
            if (chn < 4) {
                data = s->playback_period_size[chn];
            }
            break;
        case PLAYBACK_POINTER:
            if (chn < 4) {
                data = s->playback_pointer[chn];
            }
            break;
        case CAPTURE_DMA_ADDR:
            if (chn < 4) {
                data = s->capture_dma_addr[chn];
            }
            break;
        case CAPTURE_BUFFER_SIZE:
            if (chn < 4) {
                data = s->capture_buffer_size[chn];
            }
            break;
        case CAPTURE_POINTER:
            if (chn < 4) {
                data = s->capture_pointer[chn];
            }
            break;
        case BASIC_INTERRUPT:
            data = s->basic_interrupt;
            break;
        case EXTENDED_INT_MASK:
            data = s->extended_int_mask;
            break;
        case EXTENDED_INT:
            data = s->extended_int;
            break;
        default:
            if (reg < (sizeof(s->internal_regs) / sizeof(s->internal_regs[0]))) {
                data = s->internal_regs[reg];
            } else {
                data = 0;
            }
            break;
        }
        val = data;
        break;
    }
    case CA0106_IPR:
        val = s->ipr;
        break;
    case CA0106_INTE:
        val = s->inte;
        break;
    case CA0106_HCFG:
        val = s->hcfg;
        break;
    case CA0106_GPIO:
        val = s->gpio;
        break;
    case CA0106_AC97DATA:
        /* very simple AC97 data shadow */
        val = s->ac97data;
        break;
    case CA0106_AC97ADDRESS:
        val = s->ac97address;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ca0106: mmio read unknown addr 0x%" HWADDR_PRIx ", size %u\n",
                      addr, size);
        val = 0;
        break;
    }

    /* adjust for smaller sizes */
    if (size == 1) {
        val &= 0xffu;
    } else if (size == 2) {
        val &= 0xffffu;
    } else if (size == 4) {
        val &= 0xffffffffu;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = val & 0xffffffffu;

    switch (addr) {
    case CA0106_PTR:
        /* only 32-bit access used in driver */
        s->ptr_reg = v32;
        break;
    case CA0106_DATA: {
        uint32_t reg = (s->ptr_reg >> 16) & 0xffffu;
        uint32_t chn = s->ptr_reg & 0xffffu;

        switch (reg) {
        case PLAYBACK_LIST_ADDR:
            if (chn < 4) {
                s->playback_list_addr[chn] = v32;
            }
            break;
        case PLAYBACK_LIST_SIZE:
            if (chn < 4) {
                s->playback_list_size[chn] = v32;
            }
            break;
        case PLAYBACK_LIST_PTR:
            if (chn < 4) {
                s->playback_list_ptr[chn] = v32;
            }
            break;
        case PLAYBACK_DMA_ADDR:
            if (chn < 4) {
                s->playback_dma_addr[chn] = v32;
            }
            break;
        case PLAYBACK_PERIOD_SIZE:
            if (chn < 4) {
                s->playback_period_size[chn] = v32;
            }
            break;
        case PLAYBACK_POINTER:
            if (chn < 4) {
                s->playback_pointer[chn] = v32;
            }
            break;
        case CAPTURE_DMA_ADDR:
            if (chn < 4) {
                s->capture_dma_addr[chn] = v32;
            }
            break;
        case CAPTURE_BUFFER_SIZE:
            if (chn < 4) {
                s->capture_buffer_size[chn] = v32;
            }
            break;
        case CAPTURE_POINTER:
            if (chn < 4) {
                s->capture_pointer[chn] = v32;
            }
            break;
        case BASIC_INTERRUPT:
            s->basic_interrupt = v32;
            break;
        case EXTENDED_INT_MASK:
            s->extended_int_mask = v32;
            break;
        case EXTENDED_INT:
            /* driver writes stat76 back to clear bits */
            s->extended_int &= ~v32;
            break;
        default:
            if (reg < (sizeof(s->internal_regs) / sizeof(s->internal_regs[0]))) {
                s->internal_regs[reg] = v32;
            }
            break;
        }
        break;
    }
    case CA0106_IPR:
        /* driver writes status back to acknowledge; treat as W1C */
        s->ipr &= ~v32;
        pcibase_update_irq(s);
        break;
    case CA0106_INTE:
        s->inte = v32;
        pcibase_update_irq(s);
        break;
    case CA0106_HCFG:
        s->hcfg = v32;
        break;
    case CA0106_GPIO:
        s->gpio = v32;
        break;
    case CA0106_AC97ADDRESS:
        if (size == 1 || size == 2 || size == 4) {
            s->ac97address = v32 & 0xffu;
        }
        break;
    case CA0106_AC97DATA:
        /* driver uses 16-bit writes and reads */
        s->ac97data = v32 & 0xffffu;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "ca0106: mmio write unknown addr 0x%" HWADDR_PRIx ", size %u, val 0x%llx\n",
                      addr, size, (unsigned long long)val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The ALSA driver only uses MMIO via pci_resource_start(pci,0),
     * so PIO is not expected to be used. Implement as zero. */
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
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    s->hcfg = 0;
    s->ipr = 0;
    s->inte = 0;
    s->gpio = 0;
    s->ac97data = 0;
    s->ac97address = 0;
    s->ptr_reg = 0;

    memset(s->internal_regs, 0, sizeof(s->internal_regs));

    for (i = 0; i < 4; i++) {
        s->playback_list_addr[i] = 0;
        s->playback_list_size[i] = 0;
        s->playback_list_ptr[i] = 0;
        s->playback_dma_addr[i] = 0;
        s->playback_period_size[i] = 0;
        s->playback_pointer[i] = 0;
        s->capture_dma_addr[i] = 0;
        s->capture_buffer_size[i] = 0;
        s->capture_pointer[i] = 0;
    }

    s->basic_interrupt = 0;
    s->extended_int_mask = 0;
    s->extended_int = 0;

    if (s->dma_timer) {
        timer_del(s->dma_timer);
        timer_mod(s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "ca0106-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* allocate and start DMA/interrupt timer */
    s->dma_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_dma_timer_cb, s);
    timer_mod(s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);

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

    if (s->dma_timer) {
        timer_del(s->dma_timer);
        timer_free(s->dma_timer);
        s->dma_timer = NULL;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_ca0106_pci",
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
