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

#define TYPE_PCIBASE_DEVICE "snd_fm801_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1319
#define DEVICE_ID 0x0801
#define CLASS_ID  0x0401

#define FM801_PCM_VOL    0x00
#define FM801_FM_VOL     0x02
#define FM801_I2S_VOL    0x04
#define FM801_REC_SRC    0x06
#define FM801_PLY_CTRL   0x08
#define FM801_PLY_COUNT  0x0a
#define FM801_PLY_BUF1   0x0c
#define FM801_PLY_BUF2   0x10
#define FM801_CAP_CTRL   0x14
#define FM801_CAP_COUNT  0x16
#define FM801_CAP_BUF1   0x18
#define FM801_CAP_BUF2   0x1c
#define FM801_CODEC_CTRL 0x22
#define FM801_I2S_MODE   0x24
#define FM801_VOLUME     0x26
#define FM801_I2C_CTRL   0x29
#define FM801_AC97_CMD   0x2a
#define FM801_AC97_DATA  0x2c
#define FM801_MPU401_DATA 0x30
#define FM801_MPU401_CMD  0x31
#define FM801_GPIO_CTRL  0x52
#define FM801_GEN_CTRL   0x54
#define FM801_IRQ_MASK   0x56
#define FM801_IRQ_STATUS 0x5a
#define FM801_OPL3_BANK0 0x68
#define FM801_OPL3_DATA0 0x69
#define FM801_OPL3_BANK1 0x6a
#define FM801_OPL3_DATA1 0x6b
#define FM801_POWERDOWN  0x70

#define FM801_AC97_READ   (1<<7)
#define FM801_AC97_VALID  (1<<8)
#define FM801_AC97_BUSY   (1<<9)
#define FM801_AC97_ADDR_SHIFT 10
#define FM801_BUF1_LAST   (1<<1)
#define FM801_BUF2_LAST   (1<<2)
#define FM801_START       (1<<5)
#define FM801_PAUSE       (1<<6)
#define FM801_IMMED_STOP  (1<<7)
#define FM801_RATE_SHIFT  8
#define FM801_RATE_MASK   (15 << FM801_RATE_SHIFT)
#define FM801_CHANNELS_4  (1<<12)
#define FM801_CHANNELS_6  (2<<12)
#define FM801_CHANNELS_6MS (3<<12)
#define FM801_CHANNELS_MASK (3<<12)
#define FM801_16BIT       (1<<14)
#define FM801_STEREO      (1<<15)
#define FM801_IRQ_PLAYBACK (1<<8)
#define FM801_IRQ_CAPTURE  (1<<9)
#define FM801_IRQ_VOLUME   (1<<14)
#define FM801_IRQ_MPU      (1<<15)
#define FM801_GPIO_GP0     (1<<0)
#define FM801_GPIO_GP1     (1<<1)
#define FM801_GPIO_GP2     (1<<2)
#define FM801_GPIO_GP3     (1<<3)
#define FM801_GPIO_GP(x)   (1<<(0+(x)))
#define FM801_GPIO_GD0     (1<<8)
#define FM801_GPIO_GD1     (1<<9)
#define FM801_GPIO_GD2     (1<<10)
#define FM801_GPIO_GD3     (1<<11)
#define FM801_GPIO_GD(x)   (1<<(8+(x)))
#define FM801_GPIO_GS0     (1<<12)
#define FM801_GPIO_GS1     (1<<13)
#define FM801_GPIO_GS2     (1<<14)
#define FM801_GPIO_GS3     (1<<15)
#define FM801_GPIO_GS(x)   (1<<(12+(x)))

#define FM801_BAR0_SIZE   0x100

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
    uint16_t intr_status;
    uint16_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t regs[0x40];

    /* AC97 codec registers */
    uint16_t ac97_regs[0x80];

    /* DMA Context */
    struct dma_state {
        uint16_t ply_ctrl;
        uint32_t ply_count;
        uint32_t ply_buf1;
        uint32_t ply_buf2;
        uint16_t cap_ctrl;
        uint32_t cap_count;
        uint32_t cap_buf1;
        uint32_t cap_buf2;
    } dma;

    /* Operational status flags */
    bool dma_active;

    /* MPU401 state */
    bool mpu401_reset_issued;
    uint8_t mpu401_data;
    uint8_t mpu401_status; /* not really used, but for completeness */

    /* OPL3 state */
    uint8_t opl3_index[2];
    uint8_t opl3_regs[2][256]; /* simplistic, store written values */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* If any unmasked interrupt status bit is set, raise IRQ */
    if (s->intr_status & ~s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 0 || size > 4) {
        return ~0ULL;
    }

    if (addr >= FM801_BAR0_SIZE) {
        return ~0ULL;
    }

    uint16_t word_val;
    uint32_t dword_val;
    uint8_t byte_val;

    switch (size) {
    case 2:
        if (addr & 1) return ~0ULL;
        word_val = s->regs[addr >> 1];
        switch (addr) {
        case FM801_IRQ_STATUS:
            break;
        case FM801_PLY_COUNT:
            break;
        case FM801_CAP_COUNT:
            break;
        }
        val = word_val;
        break;
    case 4:
        if (addr != FM801_PLY_BUF1 && addr != FM801_PLY_BUF2 &&
            addr != FM801_CAP_BUF1 && addr != FM801_CAP_BUF2) {
            return ~0ULL;
        }
        dword_val = (uint32_t)s->regs[addr >> 1] | ((uint32_t)s->regs[(addr >> 1) + 1] << 16);
        val = dword_val;
        break;
    case 1:
        if (addr >= FM801_OPL3_BANK0 && addr <= FM801_OPL3_DATA1) {
            switch (addr) {
            case FM801_OPL3_DATA0:
                byte_val = s->opl3_regs[0][s->opl3_index[0]];
                break;
            case FM801_OPL3_DATA1:
                byte_val = s->opl3_regs[1][s->opl3_index[1]];
                break;
            case FM801_OPL3_BANK0:
            case FM801_OPL3_BANK1:
                byte_val = s->opl3_index[addr - FM801_OPL3_BANK0];
                break;
            default:
                byte_val = 0xff;
                break;
            }
            val = byte_val;
        } else if (addr == FM801_MPU401_DATA || addr == FM801_MPU401_CMD) {
            if (addr == FM801_MPU401_DATA) {
                if (s->mpu401_reset_issued) {
                    s->mpu401_reset_issued = false;
                    val = 0xfe;
                } else {
                    val = s->mpu401_data;
                }
            } else {
                val = 0x40;
            }
        } else {
            word_val = s->regs[addr >> 1];
            byte_val = (addr & 1) ? (word_val >> 8) : (word_val & 0xff);
            val = byte_val;
        }
        break;
    default:
        return ~0ULL;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 0 || size > 4) {
        return;
    }
    if (addr >= FM801_BAR0_SIZE) {
        return;
    }

    uint16_t word_val;
    uint32_t dword_val;
    int index;

    switch (size) {
    case 2:
        if (addr & 1) return;
        index = addr >> 1;
        word_val = val & 0xffff;

        switch (addr) {
        case FM801_IRQ_STATUS:
            s->intr_status &= ~word_val;
            pcibase_update_irq(s);
            s->regs[index] = s->intr_status;
            return;
        case FM801_IRQ_MASK:
            s->intr_mask = word_val;
            pcibase_update_irq(s);
            break;
        case FM801_AC97_CMD:
        {
            s->regs[index] = word_val;
            uint16_t cmd = word_val;
            int ac97_index = (cmd >> FM801_AC97_ADDR_SHIFT) & 0x7F;
            if (cmd & FM801_AC97_READ) {
                s->regs[FM801_AC97_DATA >> 1] = s->ac97_regs[ac97_index];
                s->regs[FM801_AC97_CMD >> 1] = (cmd & ~FM801_AC97_BUSY) | FM801_AC97_VALID;
            } else {
                s->ac97_regs[ac97_index] = s->regs[FM801_AC97_DATA >> 1];
                s->regs[FM801_AC97_CMD >> 1] = cmd & ~FM801_AC97_BUSY;
                if (ac97_index == 0x00) {
                    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
                    s->ac97_regs[0x00] = 0x8000;
                    s->ac97_regs[0x7C] = 0x4144;
                    s->ac97_regs[0x7E] = 0x5374;
                    s->regs[FM801_CODEC_CTRL >> 1] |= 0x0001;
                }
            }
            return;
        }
        case FM801_AC97_DATA:
            s->regs[index] = word_val;
            return;
        case FM801_CODEC_CTRL:
            s->regs[index] = word_val;
            s->regs[index] |= 0x0001;
            s->ac97_regs[0x00] = 0x8000;
            break;
        case FM801_PLY_CTRL:
            s->regs[index] = word_val;
            s->dma.ply_ctrl = word_val;
            break;
        case FM801_CAP_CTRL:
            s->regs[index] = word_val;
            s->dma.cap_ctrl = word_val;
            break;
        case FM801_PLY_COUNT:
            s->regs[index] = word_val;
            s->dma.ply_count = word_val;
            break;
        case FM801_CAP_COUNT:
            s->regs[index] = word_val;
            s->dma.cap_count = word_val;
            break;
        default:
            s->regs[index] = word_val;
            break;
        }
        break;
    case 4:
        if (addr != FM801_PLY_BUF1 && addr != FM801_PLY_BUF2 &&
            addr != FM801_CAP_BUF1 && addr != FM801_CAP_BUF2) {
            return;
        }
        dword_val = val;
        s->regs[addr >> 1] = dword_val & 0xffff;
        s->regs[(addr >> 1) + 1] = (dword_val >> 16) & 0xffff;
        if (addr == FM801_PLY_BUF1) {
            s->dma.ply_buf1 = dword_val;
        } else if (addr == FM801_PLY_BUF2) {
            s->dma.ply_buf2 = dword_val;
        } else if (addr == FM801_CAP_BUF1) {
            s->dma.cap_buf1 = dword_val;
        } else if (addr == FM801_CAP_BUF2) {
            s->dma.cap_buf2 = dword_val;
        }
        break;
    case 1:
        if (addr >= FM801_OPL3_BANK0 && addr <= FM801_OPL3_DATA1) {
            uint8_t byte = val & 0xff;
            switch (addr) {
            case FM801_OPL3_BANK0:
                s->opl3_index[0] = byte;
                break;
            case FM801_OPL3_BANK1:
                s->opl3_index[1] = byte;
                break;
            case FM801_OPL3_DATA0:
                s->opl3_regs[0][s->opl3_index[0]] = byte;
                break;
            case FM801_OPL3_DATA1:
                s->opl3_regs[1][s->opl3_index[1]] = byte;
                break;
            }
        } else if (addr == FM801_MPU401_DATA || addr == FM801_MPU401_CMD) {
            uint8_t byte = val & 0xff;
            if (addr == FM801_MPU401_DATA) {
                s->mpu401_data = byte;
            } else {
                if (byte == 0xff) {
                    s->mpu401_reset_issued = true;
                }
            }
        } else {
            index = addr >> 1;
            if (addr & 1) {
                s->regs[index] = (s->regs[index] & 0x00ff) | (val << 8);
            } else {
                s->regs[index] = (s->regs[index] & 0xff00) | (val & 0xff);
            }
        }
        break;
    default:
        return;
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

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    memset(&s->dma, 0, sizeof(s->dma));
    s->mpu401_reset_issued = false;
    s->mpu401_data = 0;
    s->mpu401_status = 0;
    memset(s->opl3_index, 0, sizeof(s->opl3_index));
    memset(s->opl3_regs, 0, sizeof(s->opl3_regs));

    /* Initialize AC97 codec registers with valid audio codec ID */
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->ac97_regs[0x7C] = 0x4144; /* Vendor ID1 (bit 15 = 0, audio codec) */
    s->ac97_regs[0x7E] = 0x5374; /* Vendor ID2 */
    s->ac97_regs[0x00] = 0x8000; /* Cold reset default: Codec Ready */

    /* Set CODEC_CTRL ready bit */
    s->regs[FM801_CODEC_CTRL >> 1] = 0x0001;

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

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

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = FM801_BAR0_SIZE,
        .name = "fm801-io"
    };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_fm801_pci",
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
