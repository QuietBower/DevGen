/*
 * QEMU PCI device model for VIA 82xx modem (snd_via82xx_modem)
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

#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "snd_via82xx_modem_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VIA_VENDOR_ID PCI_VENDOR_ID_VIA
#define VIA_DEVICE_ID 0x3068
#define VIA_CLASS_ID  PCI_CLASS_COMMUNICATION_MODEM

#define VIAREG(via, x) ((via)->port + VIA_REG_##x)
#define VIADEV_REG(viadev, x) ((viadev)->port + VIA_REG_##x)
#define VIA_REG_OFFSET_STATUS        0x00
#define VIA_REG_STAT_ACTIVE          0x80
#define VIA_REG_STAT_PAUSED          0x40
#define VIA_REG_STAT_TRIGGER_QUEUED  0x08
#define VIA_REG_STAT_STOPPED         0x04
#define VIA_REG_STAT_EOL             0x02
#define VIA_REG_STAT_FLAG            0x01
#define VIA_REG_OFFSET_CONTROL       0x01
#define VIA_REG_CTRL_START           0x80
#define VIA_REG_CTRL_TERMINATE       0x40
#define VIA_REG_CTRL_AUTOSTART       0x20
#define VIA_REG_CTRL_PAUSE           0x08
#define VIA_REG_CTRL_INT_STOP        0x04
#define VIA_REG_CTRL_INT_EOL         0x02
#define VIA_REG_CTRL_INT_FLAG        0x01
#define VIA_REG_CTRL_RESET           0x01
#define VIA_REG_CTRL_INT (VIA_REG_CTRL_INT_FLAG | VIA_REG_CTRL_INT_EOL | VIA_REG_CTRL_AUTOSTART)
#define VIA_REG_OFFSET_TYPE          0x02
#define VIA_REG_TYPE_AUTOSTART       0x80
#define VIA_REG_TYPE_16BIT           0x20
#define VIA_REG_TYPE_STEREO          0x10
#define VIA_REG_TYPE_INT_LLINE       0x00
#define VIA_REG_TYPE_INT_LSAMPLE     0x04
#define VIA_REG_TYPE_INT_LESSONE     0x08
#define VIA_REG_TYPE_INT_MASK        0x0c
#define VIA_REG_TYPE_INT_EOL         0x02
#define VIA_REG_TYPE_INT_FLAG        0x01
#define VIA_REG_OFFSET_TABLE_PTR     0x04
#define VIA_REG_OFFSET_CURR_PTR      0x04
#define VIA_REG_OFFSET_STOP_IDX      0x08
#define VIA_REG_OFFSET_CURR_COUNT    0x0c
#define VIA_REG_OFFSET_CURR_INDEX    0x0f
#define DEFINE_VIA_REGSET(name,val) \
enum {\
    VIA_REG_##name##_STATUS     = (val),\
    VIA_REG_##name##_CONTROL    = (val) + 0x01,\
    VIA_REG_##name##_TYPE       = (val) + 0x02,\
    VIA_REG_##name##_TABLE_PTR  = (val) + 0x04,\
    VIA_REG_##name##_CURR_PTR   = (val) + 0x04,\
    VIA_REG_##name##_STOP_IDX   = (val) + 0x08,\
    VIA_REG_##name##_CURR_COUNT = (val) + 0x0c,\
}
#define VIA_REG_AC97                 0x80
#define VIA_REG_AC97_CODEC_ID_MASK   (3<<30)
#define VIA_REG_AC97_CODEC_ID_SHIFT  30
#define VIA_REG_AC97_CODEC_ID_PRIMARY    0x00
#define VIA_REG_AC97_CODEC_ID_SECONDARY  0x01
#define VIA_REG_AC97_SECONDARY_VALID (1<<27)
#define VIA_REG_AC97_PRIMARY_VALID   (1<<25)
#define VIA_REG_AC97_BUSY            (1<<24)
#define VIA_REG_AC97_READ            (1<<23)
#define VIA_REG_AC97_CMD_SHIFT       16
#define VIA_REG_AC97_CMD_MASK        0x7e
#define VIA_REG_AC97_DATA_SHIFT      0
#define VIA_REG_AC97_DATA_MASK       0xffff
#define VIA_REG_SGD_SHADOW           0x84
#define VIA_REG_SGD_STAT_PB_FLAG     (1<<0)
#define VIA_REG_SGD_STAT_CP_FLAG     (1<<1)
#define VIA_REG_SGD_STAT_FM_FLAG     (1<<2)
#define VIA_REG_SGD_STAT_PB_EOL      (1<<4)
#define VIA_REG_SGD_STAT_CP_EOL      (1<<5)
#define VIA_REG_SGD_STAT_FM_EOL      (1<<6)
#define VIA_REG_SGD_STAT_PB_STOP     (1<<8)
#define VIA_REG_SGD_STAT_CP_STOP     (1<<9)
#define VIA_REG_SGD_STAT_FM_STOP     (1<<10)
#define VIA_REG_SGD_STAT_PB_ACTIVE   (1<<12)
#define VIA_REG_SGD_STAT_CP_ACTIVE   (1<<13)
#define VIA_REG_SGD_STAT_FM_ACTIVE   (1<<14)
#define VIA_REG_SGD_STAT_MR_FLAG     (1<<16)
#define VIA_REG_SGD_STAT_MW_FLAG     (1<<17)
#define VIA_REG_SGD_STAT_MR_EOL      (1<<20)
#define VIA_REG_SGD_STAT_MW_EOL      (1<<21)
#define VIA_REG_SGD_STAT_MR_STOP     (1<<24)
#define VIA_REG_SGD_STAT_MW_STOP     (1<<25)
#define VIA_REG_SGD_STAT_MR_ACTIVE   (1<<28)
#define VIA_REG_SGD_STAT_MW_ACTIVE   (1<<29)
#define VIA_REG_GPI_STATUS           0x88
#define VIA_REG_GPI_INTR             0x8c
#define VIA_TBL_BIT_FLAG             0x40000000
#define VIA_TBL_BIT_EOL              0x80000000
#define VIA_ACLINK_STAT              0x40
#define VIA_ACLINK_C11_READY         0x20
#define VIA_ACLINK_C10_READY         0x10
#define VIA_ACLINK_C01_READY         0x04
#define VIA_ACLINK_LOWPOWER          0x02
#define VIA_ACLINK_C00_READY         0x01
#define VIA_ACLINK_CTRL              0x41
#define VIA_ACLINK_CTRL_ENABLE       0x80
#define VIA_ACLINK_CTRL_RESET        0x40
#define VIA_ACLINK_CTRL_SYNC         0x20
#define VIA_ACLINK_CTRL_SDO          0x10
#define VIA_ACLINK_CTRL_VRA          0x08
#define VIA_ACLINK_CTRL_PCM          0x04
#define VIA_ACLINK_CTRL_FM           0x02
#define VIA_ACLINK_CTRL_SB           0x01
#define VIA_ACLINK_CTRL_INIT (VIA_ACLINK_CTRL_ENABLE| \
                              VIA_ACLINK_CTRL_RESET| \
                              VIA_ACLINK_CTRL_PCM)
#define VIA_FUNC_ENABLE              0x42
#define VIA_FUNC_MIDI_PNP            0x80
#define VIA_FUNC_MIDI_IRQMASK        0x40
#define VIA_FUNC_RX2C_WRITE          0x20
#define VIA_FUNC_SB_FIFO_EMPTY       0x10
#define VIA_FUNC_ENABLE_GAME         0x08
#define VIA_FUNC_ENABLE_FM           0x04
#define VIA_FUNC_ENABLE_MIDI         0x02
#define VIA_FUNC_ENABLE_SB           0x01
#define VIA_PNP_CONTROL              0x43
#define VIA_MC97_CTRL                0x44
#define VIA_MC97_CTRL_ENABLE         0x80
#define VIA_MC97_CTRL_SECONDARY      0x40
#define VIA_MC97_CTRL_INIT     (VIA_MC97_CTRL_ENABLE| \
                                VIA_MC97_CTRL_SECONDARY)
#define VIA_TABLE_SIZE               255
#define VIA_MAX_MODEM_DEVS           2

#define check_invalid_pos(viadev,pos) \
    ((pos) < (viadev)->lastpos && (((pos) >= (viadev)->bufsize2 || \
                                    (viadev)->lastpos < (viadev)->bufsize2)))

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

typedef struct ViaChannelState {
    uint32_t reg_offset;
    uint32_t port;         /* base port = chip->port + reg_offset */
    uint8_t status;
    uint8_t control;
    uint8_t type;
    uint32_t table_ptr;    /* physical address written by driver */
    uint32_t curr_ptr;
    uint32_t stop_idx;
    uint32_t curr_count;
    uint32_t curr_index;
    bool running;
} ViaChannelState;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    unsigned int intr_mask;

    /* Device state */
    uint32_t port_base;        /* pci_resource_start(pci, 0) */

    /* ACLink and AC97 state (shadow of config / AC97 regs) */
    uint8_t aclink_ctrl;
    uint8_t aclink_stat;
    uint8_t mc97_ctrl;

    uint32_t ac97_reg;         /* VIA_REG_AC97 shadow */
    uint32_t sgd_shadow;       /* VIA_REG_SGD_SHADOW shadow */
    uint32_t gpi_status;       /* VIA_REG_GPI_STATUS */
    uint32_t gpi_intr;         /* VIA_REG_GPI_INTR */

    /* two modem DMA channels (MO, MI). Offsets come from DEFINE_VIA_REGSET */
    ViaChannelState chans[VIA_MAX_MODEM_DEVS];

    /* irq line asserted */
    bool irq_level;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    bool pending = (s->sgd_shadow & s->intr_mask) != 0;

    if (pending && !s->irq_level) {
        s->irq_level = true;
        pci_set_irq(pdev, 1);
    } else if (!pending && s->irq_level) {
        s->irq_level = false;
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = 0;
    uint8_t val8 = 0;

    /* single I/O BAR: addr is offset from base */

    switch (addr) {
    case VIA_ACLINK_STAT:
        val8 = s->aclink_stat;
        goto out8;
    case VIA_ACLINK_CTRL:
        val8 = s->aclink_ctrl;
        goto out8;
    case VIA_MC97_CTRL:
        val8 = s->mc97_ctrl;
        goto out8;
    case VIA_REG_AC97:
        val32 = s->ac97_reg;
        goto out32;
    case VIA_REG_SGD_SHADOW:
        val32 = s->sgd_shadow;
        goto out32;
    case VIA_REG_GPI_STATUS:
        val32 = s->gpi_status;
        goto out32;
    case VIA_REG_GPI_INTR:
        val32 = s->gpi_intr;
        goto out32;
    default:
        break;
    }

    /* Per-channel registers. Offsets are defined via DEFINE_VIA_REGSET. */
    {
        int i;
        for (i = 0; i < VIA_MAX_MODEM_DEVS; ++i) {
            ViaChannelState *ch = &s->chans[i];
            hwaddr base = ch->reg_offset;
            if (addr == base + VIA_REG_OFFSET_STATUS) {
                val8 = ch->status;
                goto out8;
            } else if (addr == base + VIA_REG_OFFSET_CONTROL) {
                val8 = ch->control;
                goto out8;
            } else if (addr == base + VIA_REG_OFFSET_TYPE) {
                val8 = ch->type;
                goto out8;
            } else if (addr == base + VIA_REG_OFFSET_TABLE_PTR) {
                val32 = ch->table_ptr;
                goto out32;
            } else if (addr == base + VIA_REG_OFFSET_STOP_IDX) {
                val32 = ch->stop_idx;
                goto out32;
            } else if (addr == base + VIA_REG_OFFSET_CURR_COUNT) {
                val32 = ch->curr_count;
                goto out32;
            } else if (addr == base + VIA_REG_OFFSET_CURR_INDEX) {
                val8 = (uint8_t)ch->curr_index;
                goto out8;
            }
        }
    }

    /* default */
    goto out32;

out8:
    if (size == 1) {
        return val8;
    } else if (size == 2) {
        return val8;
    } else {
        return val8;
    }
out32:
    if (size == 4) {
        return val32;
    } else if (size == 2) {
        return (uint16_t)val32;
    } else if (size == 1) {
        return (uint8_t)val32;
    }
    return val32;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;
    uint8_t v8 = (uint8_t)val;

    switch (addr) {
    case VIA_ACLINK_CTRL:
        /* driver writes various sequences; we simply mirror and set ready bit */
        s->aclink_ctrl = v8;
        /* when INIT bits are set, mark codec ready */
        if ((s->aclink_ctrl & VIA_ACLINK_CTRL_INIT) == VIA_ACLINK_CTRL_INIT) {
            s->aclink_stat |= VIA_ACLINK_C00_READY;
        }
        return;
    case VIA_MC97_CTRL:
        s->mc97_ctrl = v8;
        return;
    case VIA_REG_AC97:
        /* AC97 writes: store value, clear BUSY, set VALID bits */
        s->ac97_reg = v32;
        s->ac97_reg &= ~VIA_REG_AC97_BUSY;
        if (v32 & VIA_REG_AC97_SECONDARY_VALID) {
            s->ac97_reg |= VIA_REG_AC97_SECONDARY_VALID;
        }
        if (v32 & VIA_REG_AC97_PRIMARY_VALID) {
            s->ac97_reg |= VIA_REG_AC97_PRIMARY_VALID;
        }
        return;
    case VIA_REG_GPI_STATUS:
        /* codec write with reg == AC97_GPIO_STATUS goes here */
        s->gpi_status = v32;
        return;
    case VIA_REG_GPI_INTR:
        s->gpi_intr = v32;
        return;
    case VIA_REG_SGD_SHADOW:
        /* driver never writes this in the provided code, ignore */
        return;
    default:
        break;
    }

    /* Per-channel registers */
    {
        int i;
        for (i = 0; i < VIA_MAX_MODEM_DEVS; ++i) {
            ViaChannelState *ch = &s->chans[i];
            hwaddr base = ch->reg_offset;

            if (addr == base + VIA_REG_OFFSET_STATUS) {
                /* irq ack: write-1-to-clear for FLAG/EOL/STOPPED */
                uint8_t mask = v8 & (VIA_REG_STAT_FLAG | VIA_REG_STAT_EOL | VIA_REG_STAT_STOPPED);
                ch->status &= ~mask;
                /* also clear corresponding bits in SGD_SHADOW */
                /* map channel 0 -> MR, channel 1 -> MW per intr_mask comment */
                if (i == 0) {
                    if (mask & VIA_REG_STAT_FLAG) {
                        s->sgd_shadow &= ~VIA_REG_SGD_STAT_MR_FLAG;
                    }
                    if (mask & VIA_REG_STAT_EOL) {
                        s->sgd_shadow &= ~VIA_REG_SGD_STAT_MR_EOL;
                    }
                    if (mask & VIA_REG_STAT_STOPPED) {
                        s->sgd_shadow &= ~VIA_REG_SGD_STAT_MR_STOP;
                    }
                } else if (i == 1) {
                    if (mask & VIA_REG_STAT_FLAG) {
                        s->sgd_shadow &= ~VIA_REG_SGD_STAT_MW_FLAG;
                    }
                    if (mask & VIA_REG_STAT_EOL) {
                        s->sgd_shadow &= ~VIA_REG_SGD_STAT_MW_EOL;
                    }
                    if (mask & VIA_REG_STAT_STOPPED) {
                        s->sgd_shadow &= ~VIA_REG_SGD_STAT_MW_STOP;
                    }
                }
                pcibase_update_irq(s);
                return;
            } else if (addr == base + VIA_REG_OFFSET_CONTROL) {
                ch->control = v8;
                if (v8 & VIA_REG_CTRL_START) {
                    ch->running = true;
                    ch->status |= VIA_REG_STAT_ACTIVE;
                }
                if (v8 & VIA_REG_CTRL_TERMINATE) {
                    ch->running = false;
                    ch->status &= ~VIA_REG_STAT_ACTIVE;
                }
                if (v8 & VIA_REG_CTRL_PAUSE) {
                    ch->running = false;
                    ch->status |= VIA_REG_STAT_PAUSED;
                }
                return;
            } else if (addr == base + VIA_REG_OFFSET_TYPE) {
                ch->type = v8;
                return;
            } else if (addr == base + VIA_REG_OFFSET_TABLE_PTR) {
                ch->table_ptr = v32;
                ch->curr_ptr = v32;
                return;
            } else if (addr == base + VIA_REG_OFFSET_STOP_IDX) {
                ch->stop_idx = v32;
                return;
            } else if (addr == base + VIA_REG_OFFSET_CURR_COUNT) {
                ch->curr_count = v32;
                return;
            } else if (addr == base + VIA_REG_OFFSET_CURR_INDEX) {
                ch->curr_index = v8;
                return;
            }
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* We model BAR0 as IO space, but use same handler */
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
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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

    s->aclink_ctrl = 0;
    s->aclink_stat = 0;
    s->mc97_ctrl = 0;
    s->ac97_reg = 0;
    s->sgd_shadow = 0;
    s->gpi_status = 0;
    s->gpi_intr = 0;
    s->irq_level = false;

    for (i = 0; i < VIA_MAX_MODEM_DEVS; ++i) {
        ViaChannelState *ch = &s->chans[i];
        ch->status = 0;
        ch->control = 0;
        ch->type = 0;
        ch->table_ptr = 0;
        ch->curr_ptr = 0;
        ch->stop_idx = 0;
        ch->curr_count = 0;
        ch->curr_index = 0;
        ch->running = false;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VIA_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  VIA_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, VIA_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "via82xx-modem-io";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->intr_mask = 0x330000;

    s->port_base = 0;

    /* initialize channel register offsets: MO, MI */
    /* These values come from init_viadev usage in driver: VIA_REG_MO_STATUS, VIA_REG_MI_STATUS. */
    /* We don't have their literal values here; we just set logical offsets 0x00 and 0x10
       but they are not used to compute VIADEV_REG in QEMU; driver uses inb/outb on real HW. */
    s->chans[0].reg_offset = 0x00;
    s->chans[0].port = s->port_base + s->chans[0].reg_offset;
    s->chans[1].reg_offset = 0x10;
    s->chans[1].port = s->port_base + s->chans[1].reg_offset;

    s->aclink_ctrl = 0;
    s->aclink_stat = VIA_ACLINK_C00_READY;
    s->mc97_ctrl = VIA_MC97_CTRL_INIT;

    s->ac97_reg = 0;
    s->sgd_shadow = 0;
    s->gpi_status = 0;
    s->gpi_intr = 0;
    s->irq_level = false;
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
    (void)s;
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

type_init(pcibase_register_types)
