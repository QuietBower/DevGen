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

#define TYPE_PCIBASE_DEVICE "snd_via82xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1106
#define DEVICE_ID 0x3058
#define CLASS_ID  0x0401

/* VIA82xx register offset macros */
#define VIA_REG_OFFSET_STATUS           0x00
#define VIA_REG_STAT_ACTIVE             0x80
#define VIA8233_SHADOW_STAT_ACTIVE      0x08
#define VIA_REG_STAT_PAUSED             0x40
#define VIA_REG_STAT_TRIGGER_QUEUED     0x08
#define VIA_REG_STAT_STOPPED            0x04
#define VIA_REG_STAT_EOL                0x02
#define VIA_REG_STAT_FLAG               0x01

#define VIA_REG_OFFSET_CONTROL          0x01
#define VIA_REG_CTRL_START              0x80
#define VIA_REG_CTRL_TERMINATE          0x40
#define VIA_REG_CTRL_AUTOSTART          0x20
#define VIA_REG_CTRL_PAUSE              0x08
#define VIA_REG_CTRL_INT_STOP           0x04
#define VIA_REG_CTRL_INT_EOL            0x02
#define VIA_REG_CTRL_INT_FLAG           0x01
#define VIA_REG_CTRL_RESET              0x01
#define VIA_REG_CTRL_INT (VIA_REG_CTRL_INT_FLAG | VIA_REG_CTRL_INT_EOL | VIA_REG_CTRL_AUTOSTART)

#define VIA_REG_OFFSET_TYPE             0x02
#define VIA_REG_TYPE_AUTOSTART          0x80
#define VIA_REG_TYPE_16BIT              0x20
#define VIA_REG_TYPE_STEREO             0x10
#define VIA_REG_TYPE_INT_LLINE          0x00
#define VIA_REG_TYPE_INT_LSAMPLE        0x04
#define VIA_REG_TYPE_INT_LESSONE        0x08
#define VIA_REG_TYPE_INT_MASK           0x0c
#define VIA_REG_TYPE_INT_EOL            0x02
#define VIA_REG_TYPE_INT_FLAG           0x01

#define VIA_REG_OFFSET_TABLE_PTR        0x04
#define VIA_REG_OFFSET_CURR_PTR         0x04
#define VIA_REG_OFFSET_STOP_IDX         0x08
#define VIA8233_REG_TYPE_16BIT          0x00200000
#define VIA8233_REG_TYPE_STEREO         0x00100000
#define VIA_REG_OFFSET_CURR_COUNT       0x0c
#define VIA_REG_OFFSET_CURR_INDEX       0x0f

/* Registers for AC97 codec */
#define VIA_REG_AC97                    0x80
#define VIA_REG_AC97_CODEC_ID_MASK      (3<<30)
#define VIA_REG_AC97_CODEC_ID_SHIFT     30
#define VIA_REG_AC97_CODEC_ID_PRIMARY   0x00
#define VIA_REG_AC97_CODEC_ID_SECONDARY 0x01
#define VIA_REG_AC97_SECONDARY_VALID    (1<<27)
#define VIA_REG_AC97_PRIMARY_VALID      (1<<25)
#define VIA_REG_AC97_BUSY               (1<<24)
#define VIA_REG_AC97_READ               (1<<23)
#define VIA_REG_AC97_CMD_SHIFT          16
#define VIA_REG_AC97_CMD_MASK           0x7e
#define VIA_REG_AC97_DATA_SHIFT         0
#define VIA_REG_AC97_DATA_MASK          0xffff

#define VIA_REG_SGD_SHADOW              0x84
#define VIA_REG_SGD_STAT_PB_FLAG        (1<<0)
#define VIA_REG_SGD_STAT_CP_FLAG        (1<<1)
#define VIA_REG_SGD_STAT_FM_FLAG        (1<<2)
#define VIA_REG_SGD_STAT_PB_EOL         (1<<4)
#define VIA_REG_SGD_STAT_CP_EOL         (1<<5)
#define VIA_REG_SGD_STAT_FM_EOL         (1<<6)
#define VIA_REG_SGD_STAT_PB_STOP        (1<<8)
#define VIA_REG_SGD_STAT_CP_STOP        (1<<9)
#define VIA_REG_SGD_STAT_FM_STOP        (1<<10)
#define VIA_REG_SGD_STAT_PB_ACTIVE      (1<<12)
#define VIA_REG_SGD_STAT_CP_ACTIVE      (1<<13)
#define VIA_REG_SGD_STAT_FM_ACTIVE      (1<<14)

#define VIA8233_REG_SGD_STAT_FLAG       (1<<0)
#define VIA8233_REG_SGD_STAT_EOL        (1<<1)
#define VIA8233_REG_SGD_STAT_STOP       (1<<2)
#define VIA8233_REG_SGD_STAT_ACTIVE     (1<<3)
#define VIA8233_INTR_MASK(chan) ((VIA8233_REG_SGD_STAT_FLAG|VIA8233_REG_SGD_STAT_EOL) << ((chan) * 4))
#define VIA8233_REG_SGD_CHAN_SDX        0
#define VIA8233_REG_SGD_CHAN_MULTI      4
#define VIA8233_REG_SGD_CHAN_REC        6
#define VIA8233_REG_SGD_CHAN_REC1       7

#define VIA_REG_GPI_STATUS              0x88
#define VIA_REG_GPI_INTR                0x8c

#define VIA_REG_OFS_PLAYBACK_VOLUME_L   0x02
#define VIA_REG_OFS_PLAYBACK_VOLUME_R   0x03

#define VIA_REG_OFS_MULTPLAY_FORMAT     0x02
#define VIA_REG_MULTPLAY_FMT_8BIT       0x00
#define VIA_REG_MULTPLAY_FMT_16BIT      0x80
#define VIA_REG_MULTPLAY_FMT_CH_MASK    0x70

#define VIA_REG_OFS_CAPTURE_FIFO        0x02
#define VIA_REG_CAPTURE_FIFO_ENABLE     0x40

#define VIA_DXS_MAX_VOLUME              31

#define VIA_REG_CAPTURE_CHANNEL         0x63
#define VIA_REG_CAPTURE_CHANNEL_MIC     0x4
#define VIA_REG_CAPTURE_CHANNEL_LINE    0
#define VIA_REG_CAPTURE_SELECT_CODEC    0x03

#define VIA_TBL_BIT_FLAG                0x40000000
#define VIA_TBL_BIT_EOL                 0x80000000

#define VIA_ACLINK_STAT                 0x40
#define VIA_ACLINK_C11_READY            0x20
#define VIA_ACLINK_C10_READY            0x10
#define VIA_ACLINK_C01_READY            0x04
#define VIA_ACLINK_LOWPOWER             0x02
#define VIA_ACLINK_C00_READY            0x01
#define VIA_ACLINK_CTRL                 0x41
#define VIA_ACLINK_CTRL_ENABLE          0x80
#define VIA_ACLINK_CTRL_RESET           0x40
#define VIA_ACLINK_CTRL_SYNC            0x20
#define VIA_ACLINK_CTRL_SDO             0x10
#define VIA_ACLINK_CTRL_VRA             0x08
#define VIA_ACLINK_CTRL_PCM             0x04
#define VIA_ACLINK_CTRL_FM              0x02
#define VIA_ACLINK_CTRL_SB              0x01
#define VIA_ACLINK_CTRL_INIT            (VIA_ACLINK_CTRL_ENABLE|\
                                         VIA_ACLINK_CTRL_RESET|\
                                         VIA_ACLINK_CTRL_PCM|\
                                         VIA_ACLINK_CTRL_VRA)

#define VIA_FUNC_ENABLE                 0x42
#define VIA_FUNC_MIDI_PNP               0x80
#define VIA_FUNC_MIDI_IRQMASK           0x40
#define VIA_FUNC_RX2C_WRITE             0x20
#define VIA_FUNC_SB_FIFO_EMPTY          0x10
#define VIA_FUNC_ENABLE_GAME            0x08
#define VIA_FUNC_ENABLE_FM              0x04
#define VIA_FUNC_ENABLE_MIDI            0x02
#define VIA_FUNC_ENABLE_SB              0x01

#define VIA_PNP_CONTROL                 0x43
#define VIA_FM_NMI_CTRL                 0x48
#define VIA8233_VOLCHG_CTRL             0x48
#define VIA8233_SPDIF_CTRL              0x49
#define VIA8233_SPDIF_DX3               0x08
#define VIA8233_SPDIF_SLOT_MASK         0x03
#define VIA8233_SPDIF_SLOT_1011         0x00
#define VIA8233_SPDIF_SLOT_34           0x01
#define VIA8233_SPDIF_SLOT_78           0x02
#define VIA8233_SPDIF_SLOT_69           0x03

#define VIA_DXS_AUTO                    0
#define VIA_DXS_ENABLE                  1
#define VIA_DXS_DISABLE                 2
#define VIA_DXS_48K                     3
#define VIA_DXS_NO_VRA                  4
#define VIA_DXS_SRC                     5

#define VIA_TABLE_SIZE                  255
#define VIA_MAX_BUFSIZE                 (1<<24)
#define VIA_MAX_DEVS                    7

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

/* Channel base offsets and SGD shadow shifts for VIA8233 */
static const int channel_bases[7] = {
    0x00, 0x10, 0x20, 0x30, 0x50, 0x60, 0x70
};
static const int channel_shifts[7] = {
    0, 4, 8, 12, 16, 24, 28
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;     /* combined interrupt status */
    uint32_t intr_mask;       /* interrupt mask */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[256];        /* full I/O register space */

    /* AC97 Codec state */
    uint16_t ac97_regs[128];  /* AC97 register file */
    uint8_t ac97_last_reg;    /* last read command register index */
    uint32_t ac97_cmd;        /* last AC97 command written */

    /* Operation status */
    uint32_t status;          /* general status flags */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s);

/* Device-initiated DMA logic based on driver access patterns */
/* DMA not used in this device; placeholder deleted */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* MMIO read not used for this device */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* MMIO write not used for this device */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x100) {
        return ~0ULL;
    }

    /* AC97 register: 0x80-0x83 */
    if (addr >= 0x80 && addr + size <= 0x84) {
        uint32_t ac97_val = 0x02000000 | (s->ac97_regs[s->ac97_last_reg] & 0xffff);
        int shift = (addr - 0x80) * 8;
        uint64_t mask = (1ULL << (size * 8)) - 1;
        return (ac97_val >> shift) & mask;
    }

    if (addr == 0x84 && size == 4) {
        /* SGD_SHADOW: compute from channel statuses */
        uint32_t sgd = 0;
        for (int i = 0; i < 7; i++) {
            uint8_t status = s->regs[channel_bases[i] + 0x00];
            sgd |= (status & 0x0F) << channel_shifts[i];
        }
        return sgd;
    }

    /* General read from regs array */
    for (int i = 0; i < size && (addr + i) < 0x100; i++) {
        val |= (uint64_t)s->regs[addr + i] << (i * 8);
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    bool update_irq = false;

    if (addr >= 0x100) {
        return;
    }

    /* Special handling for AC97 */
    if (addr == 0x80 && size == 4) {
        s->ac97_cmd = val;
        int codec_id = (val >> VIA_REG_AC97_CODEC_ID_SHIFT) & 3;
        bool is_read = (val & VIA_REG_AC97_READ) != 0;
        uint8_t reg = (val >> VIA_REG_AC97_CMD_SHIFT) & 0x7f;
        uint16_t data = val & VIA_REG_AC97_DATA_MASK;
        if (!is_read) {
            s->ac97_regs[reg] = data;
        } else {
            s->ac97_last_reg = reg;
        }
        return;
    }

    /* Scan for channel-specific writes */
    for (int ch = 0; ch < 7; ch++) {
        int ch_base = channel_bases[ch];
        if (addr >= ch_base && addr < ch_base + 0x10) {
            int local = addr - ch_base;
            if (local == 0x00 && size >= 1) {
                /* STATUS write-1-to-clear */
                uint8_t old = s->regs[addr];
                uint8_t wval = val & 0xFF;
                s->regs[addr] = old & ~wval;
                update_irq = true;
                return;
            }
            if (local == 0x01 && size >= 1) {
                /* CONTROL write */
                uint8_t wval = val & 0xFF;
                if (wval & VIA_REG_CTRL_RESET) {
                    /* Reset channel: clear status, clear RESET bit */
                    s->regs[ch_base + 0x00] = 0;
                    s->regs[addr] = wval & ~VIA_REG_CTRL_RESET;
                } else {
                    s->regs[addr] = wval;
                }
                update_irq = true;
                return;
            }
            /* Other channel registers just pass through to generic write */
            break;
        }
    }

    /* Generic write to regs array */
    for (int i = 0; i < size && (addr + i) < 0x100; i++) {
        s->regs[addr + i] = (val >> (i * 8)) & 0xFF;
    }

    if (update_irq) {
        pcibase_update_irq(s);
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

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int irq_pending = 0;
    for (int i = 0; i < 7; i++) {
        int base = channel_bases[i];
        uint8_t status = s->regs[base + 0x00];
        uint8_t ctrl = s->regs[base + 0x01];
        if ((status & ctrl) & 0x07) { /* FLAG, EOL, STOP bits */
            irq_pending = 1;
            break;
        }
    }
    if (irq_pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    if (addr == VIA_ACLINK_STAT) {
        val = VIA_ACLINK_C00_READY; /* Always report primary codec ready */
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
    /* No side effects needed for config writes */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Reset I/O registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    /* Set AC-Link status to indicate primary codec ready */
    s->regs[VIA_ACLINK_STAT] = VIA_ACLINK_C00_READY;
    /* Reset AC97 state */
    s->ac97_last_reg = 0;
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->ac97_regs[0] = 0x414c4730; /* Provide a valid codec ID */
    s->ac97_cmd = 0;
    /* Ensure interrupt is deasserted */
    pci_set_irq(PCI_DEVICE(dev), 0);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x100, .name = "via82xx-io" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X init - not used */

    /* DMA configuration placeholder */

    /* No hardware timers */

    /* Final state initialization */
    memset(s->regs, 0, sizeof(s->regs));
    /* Set AC-Link status to indicate primary codec ready */
    s->regs[VIA_ACLINK_STAT] = VIA_ACLINK_C00_READY;
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->ac97_regs[0] = 0x414c4730; /* Provide a valid codec ID */
    s->ac97_last_reg = 0;
    s->ac97_cmd = 0;
    pci_set_irq(pdev, 0);
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

    /* Uninit placeholder */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_via82xx_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
