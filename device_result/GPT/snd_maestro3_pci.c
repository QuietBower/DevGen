/*
 * QEMU PCI device model for ESS Maestro3 / Allegro-like audio controller
 * Implemented to satisfy Linux snd_maestro3 driver probing and basic init.
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

#define TYPE_PCIBASE_DEVICE "snd_maestro3_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define M3_VENDOR_ID 0x125d
#define M3_DEVICE_ID 0x1988
#define M3_CLASS_ID  0x0401

/* Selected hardware register offsets (PCI config or I/O) */
#define M3_GPIO_DATA               0x60
#define M3_GPIO_MASK               0x64
#define M3_GPIO_DIRECTION          0x68
#define M3_PCI_LEGACY_AUDIO_CTRL   0x40
#define M3_PCI_ALLEGRO_CONFIG      0x50
#define M3_PCI_ACPI_CONTROL        0x54
#define M3_PCI_USER_CONFIG         0x58
#define M3_PCI_USER_CONFIG_C       0x5C
#define M3_PCI_DDMA_CTRL           0x60
#define M3_HOST_INT_CTRL           0x18
#define M3_HOST_INT_STATUS         0x1A
#define M3_HARDWARE_VOL_CTRL       0x1B
#define M3_SHADOW_MIX_REG_VOICE    0x1C
#define M3_HW_VOL_COUNTER_VOICE    0x1D
#define M3_SHADOW_MIX_REG_MASTER   0x1E
#define M3_HW_VOL_COUNTER_MASTER   0x1F
#define M3_CODEC_COMMAND           0x30
#define M3_CODEC_STATUS            0x30
#define M3_CODEC_DATA              0x32
#define M3_RING_BUS_CTRL_A         0x36
#define M3_RING_BUS_CTRL_B         0x38
#define M3_SDO_OUT_DEST_CTRL       0x3A
#define M3_SDO_IN_DEST_CTRL        0x3C
#define M3_SPDIF_IN_CTRL           0x3E
#define M3_ASSP_INDEX_PORT         0x80
#define M3_ASSP_MEMORY_PORT        0x82
#define M3_ASSP_DATA_PORT          0x84
#define M3_MPU401_DATA_PORT        0x98
#define M3_MPU401_STATUS_PORT      0x99
#define M3_CLK_MULT_DATA_PORT      0x9C
#define M3_ASSP_CONTROL_A          0xA2
#define M3_ASSP_CONTROL_B          0xA4
#define M3_ASSP_CONTROL_C          0xA6
#define M3_ASSP_HOST_INT_STATUS    0xAC

/* HOST_INT_STATUS bits used by driver */
#define M3_HOST_INT_ASSP_INT_PENDING  (1u << 0)  /* guessed bit position from name */
#define M3_HOST_INT_HV_INT_PENDING    (1u << 1)
/* MPU401_INT_PENDING bit is not used in our emulation */

/* HOST_INT_CTRL bits used by driver */
#define M3_HOST_INT_ASSP_INT_ENABLE   (1u << 0)
#define M3_HOST_INT_HV_INT_ENABLE     (1u << 1)

/* ASSP_CONTROL_C bit used by driver */
#define M3_ASSP_HOST_INT_ENABLE       (1u << 0)

/* ASSP_HOST_INT_STATUS bits used by driver */
#define M3_DSP2HOST_REQ_TIMER         (1u << 0)

/* GPIO related bits referenced in driver */
#define M3_GPO_PRIMARY_AC97           0x0001

/* Codec command busy flag */
#define M3_CODEC_BUSY_FLAG            0x01

/* Simple timing constants (in ms) for internal timers */
#define M3_TIMER_PERIOD_MS            10
#define M3_CODEC_BUSY_TIME_MS         1


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
        uint32_t gpio_data;
        uint32_t gpio_mask;
        uint32_t gpio_direction;
        uint32_t pci_legacy_audio_ctrl;
        uint32_t pci_allegro_config;
        uint32_t pci_acpi_control;
        uint32_t pci_user_config;
        uint32_t pci_user_config_c;
        uint32_t pci_ddma_ctrl;
        uint16_t host_int_ctrl;
        uint16_t host_int_status;
        uint8_t  hardware_vol_ctrl;
        uint8_t  shadow_mix_reg_voice;
        uint8_t  hw_vol_counter_voice;
        uint8_t  shadow_mix_reg_master;
        uint8_t  hw_vol_counter_master;
        uint16_t codec_command;
        uint16_t codec_data;
        uint16_t ring_bus_ctrl_a;
        uint16_t ring_bus_ctrl_b;
        uint16_t sdo_out_dest_ctrl;
        uint16_t sdo_in_dest_ctrl;
        uint16_t spdif_in_ctrl;
        uint16_t assp_index_port;
        uint16_t assp_memory_port;
        uint16_t assp_data_port;
        uint8_t  mpu401_data_port;
        uint8_t  mpu401_status_port;
        uint8_t  clk_mult_data_port;
        uint8_t  assp_control_a;
        uint8_t  assp_control_b;
        uint8_t  assp_control_c;
        uint8_t  assp_host_int_status;
    } regs;

    /* Simple ASSP (DSP) internal memory used by assp_read/write */
    uint16_t assp_internal_data[0x2000]; /* size chosen to cover addresses used in driver */
    uint16_t assp_internal_code[0x2000];

    /* Timers to emulate DSP timer interrupt and AC97 codec busy timing */
    QEMUTimer dsp_timer;
    QEMUTimer codec_timer;
    bool dsp_timer_enabled;
    bool codec_busy;
};


/* Internal helper for status-triggered signaling */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Determine if any interrupt source is pending and enabled */
    uint16_t pending = s->regs.host_int_status & s->regs.host_int_ctrl;

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns
 *
 * The maestro3 driver programs DMA addresses into internal DSP memory via
 * snd_m3_assp_write() only; it never touches PCI bus mastering registers
 * directly. The real hardware then performs DMA based on this DSP state.
 * Since the driver never inspects device-visible DMA effects and only uses
 * snd_m3_get_pointer() which also reads back from DSP internal memory, we
 * can avoid implementing actual pci_dma_read/pci_dma_write here.
 *
 * This function is kept empty; it can be extended in later phases if kernel
 * logs require additional behavior.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Forward declarations for timer callbacks */
static void pcibase_dsp_timer_cb(void *opaque);
static void pcibase_codec_timer_cb(void *opaque);

/* Helper: map DSP_PORT_MEMORY_TYPE / INDEX / DATA to our internal arrays.
 * The driver always masks region with MEMTYPE_MASK and uses MEMTYPE_INTERNAL_DATA
 * or MEMTYPE_INTERNAL_CODE, but their numeric values aren't provided here.
 * However, snd_m3_assp_write/read always pass the same region back when
 * reading, so we do not need to decode region; we only need to keep memory
 * addressed by index. We keep two arrays but we don't distinguish region bits
 * because missing MEMTYPE constants prevent exact decoding. For correctness
 * relative to driver behavior, the driver reads only what it previously
 * wrote, and always with identical region and index.
 *
 * We therefore interpret assp_memory_port as a simple address into the data
 * array; this matches all uses in the provided driver code (they never use
 * the index_port and memory_port macros from the PCI template).
 */
static inline uint16_t pcibase_assp_data_read(PCIBaseState *s)
{
    uint16_t idx = s->regs.assp_memory_port;
    if (idx < ARRAY_SIZE(s->assp_internal_data)) {
        return s->assp_internal_data[idx];
    }
    return 0;
}

static inline void pcibase_assp_data_write(PCIBaseState *s, uint16_t val)
{
    uint16_t idx = s->regs.assp_memory_port;
    if (idx < ARRAY_SIZE(s->assp_internal_data)) {
        s->assp_internal_data[idx] = val;
    }
}

/* DSP timer callback: emulate periodic timer interrupt used by driver to
 * advance PCM positions. The real hardware asserts DSP2HOST_REQ_TIMER in
 * ASSP_HOST_INT_STATUS and the corresponding ASSP_INT_PENDING in
 * HOST_INT_STATUS when the timer fires.
 */
static void pcibase_dsp_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!s->dsp_timer_enabled) {
        return;
    }

    /* set DSP timer request bit */
    s->regs.assp_host_int_status |= M3_DSP2HOST_REQ_TIMER;

    /* reflect ASSP_INT_PENDING into HOST_INT_STATUS if enabled */
    s->regs.host_int_status |= M3_HOST_INT_ASSP_INT_PENDING;

    pcibase_update_irq(s);

    /* rearm periodic timer */
    timer_mod(&s->dsp_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + M3_TIMER_PERIOD_MS);
}

/* Codec busy timer: clears codec_busy after short delay to emulate
 * snd_m3_ac97_wait() polling bit 0 of CODEC_COMMAND.
 */
static void pcibase_codec_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    s->codec_busy = false;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* All accesses from driver are 8 or 16 bit inb/inw/outb/outw */
    switch (addr) {
    case M3_GPIO_DATA:
        val = s->regs.gpio_data;
        break;
    case M3_GPIO_MASK:
        val = s->regs.gpio_mask;
        break;
    case M3_GPIO_DIRECTION:
        val = s->regs.gpio_direction;
        break;

    case M3_HOST_INT_CTRL:
        val = s->regs.host_int_ctrl;
        break;
    case M3_HOST_INT_STATUS:
        /* driver uses inb for status, but register is 16-bit; return low 8 */
        if (size == 1) {
            val = s->regs.host_int_status & 0xff;
        } else {
            val = s->regs.host_int_status;
        }
        break;

    case M3_HARDWARE_VOL_CTRL:
        val = s->regs.hardware_vol_ctrl;
        break;
    case M3_SHADOW_MIX_REG_VOICE:
        val = s->regs.shadow_mix_reg_voice;
        break;
    case M3_HW_VOL_COUNTER_VOICE:
        val = s->regs.hw_vol_counter_voice;
        break;
    case M3_SHADOW_MIX_REG_MASTER:
        val = s->regs.shadow_mix_reg_master;
        break;
    case M3_HW_VOL_COUNTER_MASTER:
        val = s->regs.hw_vol_counter_master;
        break;

    case M3_CODEC_COMMAND:
        /* bit 0 is busy flag polled by snd_m3_ac97_wait() */
        val = s->regs.codec_command;
        if (s->codec_busy) {
            val |= M3_CODEC_BUSY_FLAG;
        } else {
            val &= ~M3_CODEC_BUSY_FLAG;
        }
        break;
    case M3_CODEC_DATA:
        val = s->regs.codec_data;
        break;

    case M3_RING_BUS_CTRL_A:
        val = s->regs.ring_bus_ctrl_a;
        break;
    case M3_RING_BUS_CTRL_B:
        val = s->regs.ring_bus_ctrl_b;
        break;
    case M3_SDO_OUT_DEST_CTRL:
        val = s->regs.sdo_out_dest_ctrl;
        break;
    case M3_SDO_IN_DEST_CTRL:
        val = s->regs.sdo_in_dest_ctrl;
        break;
    case M3_SPDIF_IN_CTRL:
        val = s->regs.spdif_in_ctrl;
        break;

    case M3_ASSP_INDEX_PORT:
        val = s->regs.assp_index_port;
        break;
    case M3_ASSP_MEMORY_PORT:
        val = s->regs.assp_memory_port;
        break;
    case M3_ASSP_DATA_PORT:
        val = pcibase_assp_data_read(s);
        break;

    case M3_MPU401_DATA_PORT:
        val = s->regs.mpu401_data_port;
        break;
    case M3_MPU401_STATUS_PORT:
        val = s->regs.mpu401_status_port;
        break;

    case M3_CLK_MULT_DATA_PORT:
        val = s->regs.clk_mult_data_port;
        break;

    case M3_ASSP_CONTROL_A:
        val = s->regs.assp_control_a;
        break;
    case M3_ASSP_CONTROL_B:
        val = s->regs.assp_control_b;
        break;
    case M3_ASSP_CONTROL_C:
        val = s->regs.assp_control_c;
        break;
    case M3_ASSP_HOST_INT_STATUS:
        val = s->regs.assp_host_int_status;
        break;

    default:
        /* For unknown addresses, return all-ones pattern similar to real HW
         * for unmapped I/O, to keep driver happy when probing.
         */
        switch (size) {
        case 1:
            val = 0xff;
            break;
        case 2:
            val = 0xffff;
            break;
        case 4:
            val = 0xffffffffu;
            break;
        default:
            val = 0xffffffffffffffffULL;
            break;
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case M3_GPIO_DATA:
        s->regs.gpio_data = (uint16_t)val;
        break;
    case M3_GPIO_MASK:
        s->regs.gpio_mask = (uint16_t)val;
        break;
    case M3_GPIO_DIRECTION:
        s->regs.gpio_direction = (uint16_t)val;
        break;

    case M3_HOST_INT_CTRL:
        /* driver writes val via outw to enable ASSP/HV interrupts */
        s->regs.host_int_ctrl = (uint16_t)val;
        pcibase_update_irq(s);
        break;

    case M3_HOST_INT_STATUS:
        /* driver writes interrupt enable value and uses outb(status) to ACK
         * pending interrupts. We implement write-1-to-clear semantics.
         */
        if (size == 1) {
            uint8_t w = val & 0xff;
            s->regs.host_int_status &= ~w;
        } else {
            uint16_t w = val & 0xffff;
            s->regs.host_int_status &= ~w;
        }
        pcibase_update_irq(s);
        break;

    case M3_HARDWARE_VOL_CTRL:
        s->regs.hardware_vol_ctrl = (uint8_t)val;
        break;
    case M3_SHADOW_MIX_REG_VOICE:
        s->regs.shadow_mix_reg_voice = (uint8_t)val;
        break;
    case M3_HW_VOL_COUNTER_VOICE:
        s->regs.hw_vol_counter_voice = (uint8_t)val;
        break;
    case M3_SHADOW_MIX_REG_MASTER:
        s->regs.shadow_mix_reg_master = (uint8_t)val;
        break;
    case M3_HW_VOL_COUNTER_MASTER:
        s->regs.hw_vol_counter_master = (uint8_t)val;
        break;

    case M3_CODEC_COMMAND:
        /* driver writes 0x80|reg or reg&0x7f; we set busy flag and clear via timer */
        s->regs.codec_command = (uint16_t)val;
        s->codec_busy = true;
        timer_mod(&s->codec_timer,
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + M3_CODEC_BUSY_TIME_MS);
        break;
    case M3_CODEC_DATA:
        s->regs.codec_data = (uint16_t)val;
        break;

    case M3_RING_BUS_CTRL_A:
        s->regs.ring_bus_ctrl_a = (uint16_t)val;
        break;
    case M3_RING_BUS_CTRL_B:
        s->regs.ring_bus_ctrl_b = (uint16_t)val;
        break;
    case M3_SDO_OUT_DEST_CTRL:
        s->regs.sdo_out_dest_ctrl = (uint16_t)val;
        break;
    case M3_SDO_IN_DEST_CTRL:
        s->regs.sdo_in_dest_ctrl = (uint16_t)val;
        break;
    case M3_SPDIF_IN_CTRL:
        s->regs.spdif_in_ctrl = (uint16_t)val;
        break;

    case M3_ASSP_INDEX_PORT:
        s->regs.assp_index_port = (uint16_t)val;
        break;
    case M3_ASSP_MEMORY_PORT:
        s->regs.assp_memory_port = (uint16_t)val;
        break;
    case M3_ASSP_DATA_PORT:
        pcibase_assp_data_write(s, (uint16_t)val);
        break;

    case M3_MPU401_DATA_PORT:
        s->regs.mpu401_data_port = (uint8_t)val;
        break;
    case M3_MPU401_STATUS_PORT:
        s->regs.mpu401_status_port = (uint8_t)val;
        break;

    case M3_CLK_MULT_DATA_PORT:
        s->regs.clk_mult_data_port = (uint8_t)val;
        break;

    case M3_ASSP_CONTROL_A:
        s->regs.assp_control_a = (uint8_t)val;
        break;
    case M3_ASSP_CONTROL_B:
        s->regs.assp_control_b = (uint8_t)val;
        break;
    case M3_ASSP_CONTROL_C:
        s->regs.assp_control_c = (uint8_t)val;
        /* When ASSP_HOST_INT_ENABLE bit is set or cleared, reevaluate IRQ */
        pcibase_update_irq(s);
        break;

    case M3_ASSP_HOST_INT_STATUS:
        /* driver writes DSP2HOST_REQ_TIMER to clear timer bit */
        s->regs.assp_host_int_status &= ~((uint8_t)val);
        /* If timer bit cleared, we also clear ASSP_INT_PENDING in host_int_status */
        if ((val & M3_DSP2HOST_REQ_TIMER) != 0) {
            s->regs.host_int_status &= ~M3_HOST_INT_ASSP_INT_PENDING;
        }
        pcibase_update_irq(s);
        break;

    default:
        /* ignore writes to unknown addresses */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* BAR0 is configured as PIO in realize() and mapped to same handlers */
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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

    /* Revert registers to power-on defaults visible to driver */
    memset(&s->regs, 0, sizeof(s->regs));

    s->regs.gpio_mask = 0xffff;

    /* HOST_INT registers */
    s->regs.host_int_ctrl = 0x0000;
    s->regs.host_int_status = 0x0000;

    /* Hardware volume defaults per snd_m3_chip_init() */
    s->regs.hardware_vol_ctrl = 0x00;
    s->regs.shadow_mix_reg_voice = 0x88;
    s->regs.hw_vol_counter_voice = 0x88;
    s->regs.shadow_mix_reg_master = 0x88;
    s->regs.hw_vol_counter_master = 0x88;

    /* codec status */
    s->codec_busy = false;

    /* clear ASSP memories */
    memset(s->assp_internal_data, 0, sizeof(s->assp_internal_data));
    memset(s->assp_internal_code, 0, sizeof(s->assp_internal_code));

    /* stop timers */
    s->dsp_timer_enabled = false;
    timer_del(&s->dsp_timer);
    timer_del(&s->codec_timer);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  M3_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  M3_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, M3_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: the driver uses BAR0 as I/O space */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "maestro3-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize timers */
    timer_init_ms(&s->dsp_timer, QEMU_CLOCK_VIRTUAL, pcibase_dsp_timer_cb, s);
    timer_init_ms(&s->codec_timer, QEMU_CLOCK_VIRTUAL, pcibase_codec_timer_cb, s);
    s->dsp_timer_enabled = true;
    timer_mod(&s->dsp_timer,
              qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + M3_TIMER_PERIOD_MS);

    /* msi/msix: not required by driver, leave disabled */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize default register state */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.gpio_mask = 0xffff;
    s->regs.hardware_vol_ctrl = 0x00;
    s->regs.shadow_mix_reg_voice = 0x88;
    s->regs.hw_vol_counter_voice = 0x88;
    s->regs.shadow_mix_reg_master = 0x88;
    s->regs.hw_vol_counter_master = 0x88;

    s->codec_busy = false;
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

    /* Stop timers */
    timer_del(&s->dsp_timer);
    timer_del(&s->codec_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_maestro3_pci",
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
