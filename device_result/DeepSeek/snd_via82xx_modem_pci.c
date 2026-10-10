/*
 * QEMU PCI device model for VIA 82xx Modem (snd-via82xx-modem)
 * Auto-generated from driver: /home/eely/linux-7.1/sound/pci/via82xx_modem.c
 * Phase 2: Functional behavior implementation.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bswap.h"
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

#define TYPE_PCIBASE_DEVICE "snd_via82xx_modem_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identifiers */
#define VENDOR_ID 0x1106
#define DEVICE_ID 0x3068
#define CLASS_ID  0x0703

/* Register offsets and bit definitions extracted from driver source */
#define VIA_REG_OFFSET_STATUS		0x00
#define VIA_REG_STAT_ACTIVE		0x80
#define VIA_REG_STAT_PAUSED		0x40
#define VIA_REG_STAT_TRIGGER_QUEUED	0x08
#define VIA_REG_STAT_STOPPED		0x04
#define VIA_REG_STAT_EOL		0x02
#define VIA_REG_STAT_FLAG		0x01
#define VIA_REG_OFFSET_CONTROL	0x01
#define VIA_REG_CTRL_START		0x80
#define VIA_REG_CTRL_TERMINATE		0x40
#define VIA_REG_CTRL_AUTOSTART		0x20
#define VIA_REG_CTRL_PAUSE		0x08
#define VIA_REG_CTRL_INT_STOP		0x04
#define VIA_REG_CTRL_INT_EOL		0x02
#define VIA_REG_CTRL_RESET		0x01
#define VIA_REG_CTRL_INT (VIA_REG_CTRL_INT_FLAG | VIA_REG_CTRL_INT_EOL | VIA_REG_CTRL_AUTOSTART)
#define VIA_REG_OFFSET_TYPE		0x02
#define VIA_REG_TYPE_AUTOSTART	0x80
#define VIA_REG_TYPE_16BIT		0x20
#define VIA_REG_TYPE_STEREO		0x10
#define VIA_REG_TYPE_INT_LLINE		0x00
#define VIA_REG_TYPE_INT_LSAMPLE	0x04
#define VIA_REG_TYPE_INT_LESSONE	0x08
#define VIA_REG_TYPE_INT_MASK		0x0c
#define VIA_REG_TYPE_INT_EOL		0x02
#define VIA_REG_TYPE_INT_FLAG		0x01
#define VIA_REG_OFFSET_TABLE_PTR	0x04
#define VIA_REG_OFFSET_CURR_PTR	0x04
#define VIA_REG_OFFSET_STOP_IDX	0x08
#define VIA_REG_OFFSET_CURR_COUNT	0x0c
#define VIA_REG_OFFSET_CURR_INDEX	0x0f

#define VIA_REG_AC97			0x80
#define VIA_REG_AC97_CODEC_ID_MASK	(3<<30)
#define VIA_REG_AC97_CODEC_ID_SHIFT	30
#define VIA_REG_AC97_CODEC_ID_PRIMARY	0x00
#define VIA_REG_AC97_CODEC_ID_SECONDARY 0x01
#define VIA_REG_AC97_SECONDARY_VALID	(1<<27)
#define VIA_REG_AC97_PRIMARY_VALID	(1<<25)
#define VIA_REG_AC97_BUSY		(1<<24)
#define VIA_REG_AC97_READ		(1<<23)
#define VIA_REG_AC97_CMD_SHIFT		16
#define VIA_REG_AC97_CMD_MASK		0x7e
#define VIA_REG_AC97_DATA_SHIFT	0
#define VIA_REG_AC97_DATA_MASK		0xffff
#define VIA_REG_SGD_SHADOW		0x84
#define VIA_REG_SGD_STAT_PB_FLAG	(1<<0)
#define VIA_REG_SGD_STAT_CP_FLAG	(1<<1)
#define VIA_REG_SGD_STAT_FM_FLAG	(1<<2)
#define VIA_REG_SGD_STAT_PB_EOL	(1<<4)
#define VIA_REG_SGD_STAT_CP_EOL	(1<<5)
#define VIA_REG_SGD_STAT_FM_EOL	(1<<6)
#define VIA_REG_SGD_STAT_PB_STOP	(1<<8)
#define VIA_REG_SGD_STAT_CP_STOP	(1<<9)
#define VIA_REG_SGD_STAT_FM_STOP	(1<<10)
#define VIA_REG_SGD_STAT_PB_ACTIVE	(1<<12)
#define VIA_REG_SGD_STAT_CP_ACTIVE	(1<<13)
#define VIA_REG_SGD_STAT_FM_ACTIVE	(1<<14)
#define VIA_REG_GPI_STATUS		0x88
#define VIA_REG_GPI_INTR		0x8c

#define VIA_TBL_BIT_FLAG	0x40000000
#define VIA_TBL_BIT_EOL		0x80000000
#define VIA_ACLINK_STAT		0x40
#define VIA_ACLINK_C11_READY	0x20
#define VIA_ACLINK_C10_READY	0x10
#define VIA_ACLINK_C01_READY	0x04
#define VIA_ACLINK_LOWPOWER	0x02
#define VIA_ACLINK_C00_READY	0x01
#define VIA_ACLINK_CTRL		0x41
#define VIA_ACLINK_CTRL_ENABLE	0x80
#define VIA_ACLINK_CTRL_RESET	0x40
#define VIA_ACLINK_CTRL_SYNC	0x20
#define VIA_ACLINK_CTRL_SDO	0x10
#define VIA_ACLINK_CTRL_VRA	0x08
#define VIA_ACLINK_CTRL_PCM	0x04
#define VIA_ACLINK_CTRL_FM		0x02
#define VIA_ACLINK_CTRL_SB		0x01
#define VIA_ACLINK_CTRL_INIT	(VIA_ACLINK_CTRL_ENABLE|\
				 VIA_ACLINK_CTRL_RESET|\
				 VIA_ACLINK_CTRL_PCM)
#define VIA_FUNC_ENABLE		0x42
#define VIA_FUNC_MIDI_PNP	0x80
#define VIA_FUNC_MIDI_IRQMASK	0x40
#define VIA_FUNC_RX2C_WRITE	0x20
#define VIA_FUNC_SB_FIFO_EMPTY	0x10
#define VIA_FUNC_ENABLE_GAME	0x08
#define VIA_FUNC_ENABLE_FM		0x04
#define VIA_FUNC_ENABLE_MIDI	0x02
#define VIA_FUNC_ENABLE_SB		0x01
#define VIA_PNP_CONTROL		0x43
#define VIA_TABLE_SIZE		255
#define VIA_MC97_CTRL		0x44
#define VIA_MC97_CTRL_ENABLE   0x80
#define VIA_MC97_CTRL_SECONDARY 0x40
#define VIA_MC97_CTRL_INIT     (VIA_MC97_CTRL_ENABLE|\
                                 VIA_MC97_CTRL_SECONDARY)
#define VIA_MAX_MODEM_DEVS	2

/* SGD table descriptor format */
struct snd_via_sg_table {
	unsigned int offset;
	unsigned int size;
};

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Hardware Register Shadows (full 256-byte I/O space) */
    uint8_t io_regs[0x100];

    /* AC97 emulation state */
    uint16_t ac97_regs[128];
    uint32_t ac97_response;

    /* Custom PCI config registers */
    uint8_t pci_config_40; /* VIA_ACLINK_STAT */
    uint8_t pci_config_41; /* VIA_ACLINK_CTRL */
    uint8_t pci_config_42; /* VIA_FUNC_ENABLE */
    uint8_t pci_config_43; /* VIA_PNP_CONTROL */
    uint8_t pci_config_44; /* VIA_MC97_CTRL */
};

static void pcibase_update_irq(PCIBaseState *s) {
    uint32_t shadow = 0;
    if (s->io_regs[0x60] & VIA_REG_STAT_FLAG) shadow |= (1 << 16);
    if (s->io_regs[0x60] & VIA_REG_STAT_EOL)  shadow |= (1 << 20);
    if (s->io_regs[0x70] & VIA_REG_STAT_FLAG) shadow |= (1 << 17);
    if (s->io_regs[0x70] & VIA_REG_STAT_EOL)  shadow |= (1 << 21);
    if (shadow & 0x330000) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

static void pcibase_do_dma(PCIBaseState *s, bool is_write) {}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr >= 0x60 && addr < 0x70) {
        /* Stream 0 (playback) */
        int soff = addr - 0x60;
        switch (soff) {
        case 0x00:
            val = s->io_regs[0x60];
            break;
        case 0x01:
            val = s->io_regs[0x61];
            break;
        case 0x02:
            val = s->io_regs[0x62];
            break;
        case 0x04:
            val = ldl_le_p(&s->io_regs[0x64]);
            break;
        case 0x08:
            val = ldl_le_p(&s->io_regs[0x68]);
            break;
        case 0x0c:
            val = ldl_le_p(&s->io_regs[0x6c]) & 0xffffff;
            break;
        case 0x0f:
            val = s->io_regs[0x6f];
            break;
        default:
            val = s->io_regs[addr];
            break;
        }
    } else if (addr >= 0x70 && addr < 0x80) {
        /* Stream 1 (capture) */
        int soff = addr - 0x70;
        switch (soff) {
        case 0x00:
            val = s->io_regs[0x70];
            break;
        case 0x01:
            val = s->io_regs[0x71];
            break;
        case 0x02:
            val = s->io_regs[0x72];
            break;
        case 0x04:
            val = ldl_le_p(&s->io_regs[0x74]);
            break;
        case 0x08:
            val = ldl_le_p(&s->io_regs[0x78]);
            break;
        case 0x0c:
            val = ldl_le_p(&s->io_regs[0x7c]) & 0xffffff;
            break;
        case 0x0f:
            val = s->io_regs[0x7f];
            break;
        default:
            val = s->io_regs[addr];
            break;
        }
    } else {
        switch (addr) {
        case 0x80:
            val = s->ac97_response;
            break;
        case 0x84: {
            uint32_t shadow = 0;
            if (s->io_regs[0x60] & VIA_REG_STAT_FLAG) shadow |= (1 << 16);
            if (s->io_regs[0x60] & VIA_REG_STAT_EOL)  shadow |= (1 << 20);
            if (s->io_regs[0x70] & VIA_REG_STAT_FLAG) shadow |= (1 << 17);
            if (s->io_regs[0x70] & VIA_REG_STAT_EOL)  shadow |= (1 << 21);
            val = shadow;
            break;
        }
        default:
            val = s->io_regs[addr];
            break;
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x60 && addr < 0x70) {
        int soff = addr - 0x60;
        switch (soff) {
        case 0x00: /* STATUS */
            s->io_regs[0x60] &= ~(val & (VIA_REG_STAT_FLAG | VIA_REG_STAT_EOL | VIA_REG_STAT_STOPPED));
            break;
        case 0x01: /* CONTROL */
        {
            uint8_t ctrl = val;
            if (ctrl & VIA_REG_CTRL_RESET) {
                s->io_regs[0x60] = 0;
                s->io_regs[0x61] = 0;
                s->io_regs[0x62] = 0;
                memset(&s->io_regs[0x64], 0, 4);
                memset(&s->io_regs[0x68], 0, 4);
                memset(&s->io_regs[0x6c], 0, 4);
                s->io_regs[0x6f] = 0;
            } else {
                if (ctrl & VIA_REG_CTRL_START) {
                    s->io_regs[0x60] |= VIA_REG_STAT_ACTIVE;
                    s->io_regs[0x60] &= ~(VIA_REG_STAT_STOPPED | VIA_REG_STAT_PAUSED);
                }
                if (ctrl & VIA_REG_CTRL_TERMINATE) {
                    s->io_regs[0x60] |= VIA_REG_STAT_STOPPED;
                    s->io_regs[0x60] &= ~(VIA_REG_STAT_ACTIVE | VIA_REG_STAT_PAUSED);
                }
                if (ctrl & VIA_REG_CTRL_PAUSE) {
                    s->io_regs[0x60] |= VIA_REG_STAT_PAUSED;
                    s->io_regs[0x60] &= ~VIA_REG_STAT_ACTIVE;
                }
                s->io_regs[0x61] = ctrl;
            }
            break;
        }
        case 0x02: /* TYPE */
            s->io_regs[0x62] = val;
            break;
        case 0x04: /* TABLE_PTR */
            stl_le_p(&s->io_regs[0x64], val);
            break;
        case 0x08: /* STOP_IDX */
            stl_le_p(&s->io_regs[0x68], val);
            break;
        default:
            s->io_regs[addr] = val;
            break;
        }
    } else if (addr >= 0x70 && addr < 0x80) {
        int soff = addr - 0x70;
        switch (soff) {
        case 0x00: /* STATUS */
            s->io_regs[0x70] &= ~(val & (VIA_REG_STAT_FLAG | VIA_REG_STAT_EOL | VIA_REG_STAT_STOPPED));
            break;
        case 0x01: /* CONTROL */
        {
            uint8_t ctrl = val;
            if (ctrl & VIA_REG_CTRL_RESET) {
                s->io_regs[0x70] = 0;
                s->io_regs[0x71] = 0;
                s->io_regs[0x72] = 0;
                memset(&s->io_regs[0x74], 0, 4);
                memset(&s->io_regs[0x78], 0, 4);
                memset(&s->io_regs[0x7c], 0, 4);
                s->io_regs[0x7f] = 0;
            } else {
                if (ctrl & VIA_REG_CTRL_START) {
                    s->io_regs[0x70] |= VIA_REG_STAT_ACTIVE;
                    s->io_regs[0x70] &= ~(VIA_REG_STAT_STOPPED | VIA_REG_STAT_PAUSED);
                }
                if (ctrl & VIA_REG_CTRL_TERMINATE) {
                    s->io_regs[0x70] |= VIA_REG_STAT_STOPPED;
                    s->io_regs[0x70] &= ~(VIA_REG_STAT_ACTIVE | VIA_REG_STAT_PAUSED);
                }
                if (ctrl & VIA_REG_CTRL_PAUSE) {
                    s->io_regs[0x70] |= VIA_REG_STAT_PAUSED;
                    s->io_regs[0x70] &= ~VIA_REG_STAT_ACTIVE;
                }
                s->io_regs[0x71] = ctrl;
            }
            break;
        }
        case 0x02: /* TYPE */
            s->io_regs[0x72] = val;
            break;
        case 0x04: /* TABLE_PTR */
            stl_le_p(&s->io_regs[0x74], val);
            break;
        case 0x08: /* STOP_IDX */
            stl_le_p(&s->io_regs[0x78], val);
            break;
        default:
            s->io_regs[addr] = val;
            break;
        }
    } else {
        switch (addr) {
        case 0x80: /* AC97 command write */
        {
            int codec_id = (val >> VIA_REG_AC97_CODEC_ID_SHIFT) & 3;
            int is_read = (val & VIA_REG_AC97_READ) != 0;
            int reg = (val >> VIA_REG_AC97_CMD_SHIFT) & 0x7f;
            uint16_t data = val & 0xffff;
            /* Only secondary codec (ID=1) is serviced */
            if (codec_id == 1) {
                if (is_read) {
                    data = s->ac97_regs[reg];
                    s->ac97_response = VIA_REG_AC97_SECONDARY_VALID | VIA_REG_AC97_PRIMARY_VALID | data;
                } else {
                    s->ac97_regs[reg] = data;
                    s->ac97_response = VIA_REG_AC97_SECONDARY_VALID | VIA_REG_AC97_PRIMARY_VALID | data;
                }
            } else {
                s->ac97_response = VIA_REG_AC97_SECONDARY_VALID | VIA_REG_AC97_PRIMARY_VALID | 0xffff;
            }
            break;
        }
        case 0x88: /* GPI_STATUS */
            s->io_regs[0x88] = val;
            break;
        case 0x8c: /* GPI_INTR */
            s->io_regs[0x8c] = val;
            break;
        default:
            s->io_regs[addr] = val;
            break;
        }
    }
    pcibase_update_irq(s);
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
    memset(s->io_regs, 0, 0x100);
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->ac97_response = 0;
    s->pci_config_40 = 0;
    s->pci_config_41 = 0;
    s->pci_config_42 = 0;
    s->pci_config_43 = 0;
    s->pci_config_44 = 0;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);
    if (addr >= 0x40 && addr <= 0x44 && len == 1) {
        switch (addr) {
        case 0x40:
            val = s->pci_config_40;
            break;
        case 0x41:
            val = s->pci_config_41;
            break;
        case 0x42:
            val = s->pci_config_42;
            break;
        case 0x43:
            val = s->pci_config_43;
            break;
        case 0x44:
            val = s->pci_config_44;
            break;
        }
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    pci_default_write_config(pdev, addr, val, len);
    if (addr >= 0x40 && addr <= 0x44 && len == 1) {
        switch (addr) {
        case 0x40:
            s->pci_config_40 = val;
            break;
        case 0x41:
            s->pci_config_41 = val;
            /* If INIT bits written, set C00_READY */
            if ((val & VIA_ACLINK_CTRL_INIT) == VIA_ACLINK_CTRL_INIT) {
                s->pci_config_40 |= VIA_ACLINK_C00_READY;
            }
            break;
        case 0x42:
            s->pci_config_42 = val;
            break;
        case 0x43:
            s->pci_config_43 = val;
            break;
        case 0x44:
            s->pci_config_44 = val;
            break;
        }
    }
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x100, .name = "via82xx-io" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Enable custom PCI config handlers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_via82xx_modem_pci",
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
