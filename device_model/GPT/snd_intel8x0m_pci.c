/*
 * QEMU PCI device model for Intel ICH modem (snd_intel8x0m)
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

#define TYPE_PCIBASE_DEVICE "snd_intel8x0m_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID 0x8086
#define PCIBASE_DEVICE_ID 0x2416
#define PCIBASE_CLASS_ID  0x0703

#define ICHREG(x) ICH_REG_##x
#define DEFINE_REGSET(name,base) \
enum { \
    ICH_REG_##name##_BDBAR = base + 0x0, \
    ICH_REG_##name##_CIV   = base + 0x04, \
    ICH_REG_##name##_LVI   = base + 0x05, \
    ICH_REG_##name##_SR    = base + 0x06, \
    ICH_REG_##name##_PICB  = base + 0x08, \
    ICH_REG_##name##_PIV   = base + 0x0a, \
    ICH_REG_##name##_CR    = base + 0x0b, \
}

#define ICH_REG_LVI_MASK        0x1f
#define ICH_FIFOE               0x10
#define ICH_BCIS                0x08
#define ICH_LVBCI               0x04
#define ICH_CELV                0x02
#define ICH_DCH                 0x01
#define ICH_REG_PIV_MASK        0x1f
#define ICH_IOCE                0x10
#define ICH_FEIE                0x08
#define ICH_LVBIE               0x04
#define ICH_RESETREGS           0x02
#define ICH_STARTBM             0x01
#define ICH_REG_GLOB_CNT        0x3c
#define ICH_TRIE        0x00000040
#define ICH_SRIE        0x00000020
#define ICH_PRIE        0x00000010
#define ICH_ACLINK      0x00000008
#define ICH_AC97WARM    0x00000004
#define ICH_AC97COLD    0x00000002
#define ICH_GIE         0x00000001
#define ICH_REG_GLOB_STA        0x40
#define ICH_TRI         0x20000000
#define ICH_TCR         0x10000000
#define ICH_BCS         0x08000000
#define ICH_SPINT       0x04000000
#define ICH_P2INT       0x02000000
#define ICH_M2INT       0x01000000
#define ICH_SAMPLE_CAP  0x00c00000
#define ICH_MULTICHAN_CAP   0x00300000
#define ICH_MD3         0x00020000
#define ICH_AD3         0x00010000
#define ICH_RCS         0x00008000
#define ICH_BIT3        0x00004000
#define ICH_BIT2        0x00002000
#define ICH_BIT1        0x00001000
#define ICH_SRI         0x00000800
#define ICH_PRI         0x00000400
#define ICH_SCR         0x00000200
#define ICH_PCR         0x00000100
#define ICH_MCINT       0x00000080
#define ICH_POINT       0x00000040
#define ICH_PIINT       0x00000020
#define ICH_NVSPINT     0x00000010
#define ICH_MOINT       0x00000004
#define ICH_MIINT       0x00000002
#define ICH_GSCI        0x00000001
#define ICH_REG_ACC_SEMA        0x44
#define ICH_CAS         0x01
#define ICH_MAX_FRAGS       32

/* Bus-master register block layout used by the driver */
#define ICH_REG_OFF_BDBAR 0x00
#define ICH_REG_OFF_CIV   0x04
#define ICH_REG_OFF_LVI   0x05
#define ICH_REG_OFF_SR    0x06
#define ICH_REG_OFF_PICB  0x08
#define ICH_REG_OFF_PIV   0x0a
#define ICH_REG_OFF_CR    0x0b

/* We model two modem streams, base offsets 0x00 and 0x10 in BM region */
DEFINE_REGSET(MDMIN,  0x00);
DEFINE_REGSET(MDMOUT, 0x10);

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

typedef struct ModemStreamState {
    uint32_t bdbar;     /* buffer descriptor base address (guest physical) */
    uint8_t  civ;       /* current index value */
    uint8_t  lvi;       /* last valid index */
    uint8_t  sr;        /* status register */
    uint16_t picb;      /* position in current buffer (words) */
    uint8_t  piv;       /* prefetched index value */
    uint8_t  cr;        /* control register */

    /* Internal helper state for simple emulation */
    bool     running;
} ModemStreamState;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* AC97 MMIO space (BAR2) */
    uint16_t ac97_regs[0x100 / 2];

    /* Bus-master registers (BAR1) */
    ModemStreamState mdm_in;
    ModemStreamState mdm_out;

    /* Global control / status / semaphore in BM region */
    uint32_t glob_cnt;
    uint32_t glob_sta;
    uint8_t  acc_sema;

    /* Cached interrupt status used for IRQ line */
    uint32_t irq_status;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->irq_status) {
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

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * Very simplified DMA: we do not actually transfer audio/modem data.
     * The ALSA driver only needs interrupts and pointer updates; it does
     * not validate the DMA content.
     * Thus this function is intentionally left empty to avoid
     * undocumented behavior.
     */
    (void)s;
    (void)is_write;
}

static ModemStreamState *pcibase_get_stream(PCIBaseState *s, hwaddr offset)
{
    if (offset >= ICH_REG_MDMOUT_BDBAR && offset < ICH_REG_MDMOUT_BDBAR + 0x10) {
        return &s->mdm_out;
    }
    /* default to input stream for base 0x00-0x0f */
    return &s->mdm_in;
}

static uint8_t pcibase_read_bm8(PCIBaseState *s, hwaddr addr)
{
    ModemStreamState *st;
    uint8_t reg_off;

    if (addr >= ICH_REG_MDMOUT_BDBAR && addr < ICH_REG_MDMOUT_BDBAR + 0x10) {
        st = &s->mdm_out;
        reg_off = addr - ICH_REG_MDMOUT_BDBAR;
    } else {
        st = &s->mdm_in;
        reg_off = addr - ICH_REG_MDMIN_BDBAR;
    }

    switch (reg_off) {
    case ICH_REG_OFF_CIV:
        return st->civ;
    case ICH_REG_OFF_LVI:
        return st->lvi;
    case ICH_REG_OFF_SR:
        return st->sr;
    case ICH_REG_OFF_PICB:
        return (uint8_t)(st->picb & 0xff);
    case ICH_REG_OFF_PICB + 1:
        return (uint8_t)((st->picb >> 8) & 0xff);
    case ICH_REG_OFF_PIV:
        return st->piv;
    case ICH_REG_OFF_CR:
        return st->cr;
    default:
        return 0xff;
    }
}

static void pcibase_write_bm8(PCIBaseState *s, hwaddr addr, uint8_t val)
{
    ModemStreamState *st;
    uint8_t reg_off;

    if (addr >= ICH_REG_MDMOUT_BDBAR && addr < ICH_REG_MDMOUT_BDBAR + 0x10) {
        st = &s->mdm_out;
        reg_off = addr - ICH_REG_MDMOUT_BDBAR;
    } else {
        st = &s->mdm_in;
        reg_off = addr - ICH_REG_MDMIN_BDBAR;
    }

    switch (reg_off) {
    case ICH_REG_OFF_CIV:
        st->civ = val & ICH_REG_LVI_MASK;
        break;
    case ICH_REG_OFF_LVI:
        st->lvi = val & ICH_REG_LVI_MASK;
        break;
    case ICH_REG_OFF_SR:
        /* status bits are W1C */
        st->sr &= ~val;
        break;
    case ICH_REG_OFF_PICB:
        st->picb = (st->picb & 0xff00) | val;
        break;
    case ICH_REG_OFF_PICB + 1:
        st->picb = (st->picb & 0x00ff) | ((uint16_t)val << 8);
        break;
    case ICH_REG_OFF_PIV:
        st->piv = val & ICH_REG_PIV_MASK;
        break;
    case ICH_REG_OFF_CR:
        if (val & ICH_RESETREGS) {
            st->civ = 0;
            st->lvi = 0;
            st->sr = ICH_DCH; /* DMA stopped */
            st->picb = 0;
            st->piv = 0;
            st->cr = 0;
            st->running = false;
        } else {
            st->cr = val & (ICH_IOCE | ICH_FEIE | ICH_LVBIE | ICH_STARTBM);
            st->running = !!(st->cr & ICH_STARTBM);
        }
        break;
    default:
        break;
    }
}

static uint32_t pcibase_read_bm32(PCIBaseState *s, hwaddr addr)
{
    ModemStreamState *st;

    if (addr == ICH_REG_GLOB_CNT) {
        return s->glob_cnt;
    }
    if (addr == ICH_REG_GLOB_STA) {
        return s->glob_sta;
    }
    if (addr == ICH_REG_ACC_SEMA) {
        return s->acc_sema;
    }

    if (addr == ICH_REG_MDMIN_BDBAR) {
        st = &s->mdm_in;
    } else if (addr == ICH_REG_MDMOUT_BDBAR) {
        st = &s->mdm_out;
    } else {
        return 0xffffffffU;
    }
    return st->bdbar;
}

static void pcibase_write_bm32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr == ICH_REG_GLOB_CNT) {
        /* Preserve reserved bits, emulate AC97COLD/AC97WARM acknowledge */
        s->glob_cnt = val;
        /* Clear warm reset bit when written with it set, like hardware after reset completes */
        if (s->glob_cnt & ICH_AC97WARM) {
            s->glob_cnt &= ~ICH_AC97WARM;
        }
        return;
    }
    if (addr == ICH_REG_GLOB_STA) {
        /* W1C on several bits, others preserved */
        uint32_t clear_mask = val & (ICH_SRI | ICH_PRI | ICH_TRI | ICH_GSCI |
                                     ICH_MIINT | ICH_MOINT);
        s->glob_sta &= ~clear_mask;
        return;
    }
    if (addr == ICH_REG_ACC_SEMA) {
        /* Writing here not used by driver; ignore */
        return;
    }

    if (addr == ICH_REG_MDMIN_BDBAR) {
        s->mdm_in.bdbar = val;
    } else if (addr == ICH_REG_MDMOUT_BDBAR) {
        s->mdm_out.bdbar = val;
    }
}

static uint16_t pcibase_read_mmio16_ac97(PCIBaseState *s, hwaddr addr)
{
    uint32_t idx = (addr & 0xff) >> 1;
    if (idx >= (sizeof(s->ac97_regs) / sizeof(s->ac97_regs[0]))) {
        return 0xffff;
    }
    return s->ac97_regs[idx];
}

static void pcibase_write_mmio16_ac97(PCIBaseState *s, hwaddr addr, uint16_t val)
{
    uint32_t idx = (addr & 0xff) >> 1;
    if (idx >= (sizeof(s->ac97_regs) / sizeof(s->ac97_regs[0]))) {
        return;
    }
    s->ac97_regs[idx] = val;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* BAR2: AC97 codec space, assume our MMIO BAR is used for that */
    switch (size) {
    case 2:
        val = pcibase_read_mmio16_ac97(s, addr);
        break;
    case 1:
        {
            uint16_t w = pcibase_read_mmio16_ac97(s, addr & ~1ULL);
            if (addr & 1) {
                val = (w >> 8) & 0xff;
            } else {
                val = w & 0xff;
            }
        }
        break;
    case 4:
        {
            uint16_t lo = pcibase_read_mmio16_ac97(s, addr);
            uint16_t hi = pcibase_read_mmio16_ac97(s, addr + 2);
            val = lo | ((uint32_t)hi << 16);
        }
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 2:
        pcibase_write_mmio16_ac97(s, addr, (uint16_t)val);
        break;
    case 1:
        {
            uint16_t w = pcibase_read_mmio16_ac97(s, addr & ~1ULL);
            if (addr & 1) {
                w = (w & 0x00ff) | (((uint16_t)val & 0xff) << 8);
            } else {
                w = (w & 0xff00) | ((uint16_t)val & 0xff);
            }
            pcibase_write_mmio16_ac97(s, addr & ~1ULL, w);
        }
        break;
    case 4:
        {
            uint16_t lo = (uint16_t)(val & 0xffff);
            uint16_t hi = (uint16_t)((val >> 16) & 0xffff);
            pcibase_write_mmio16_ac97(s, addr, lo);
            pcibase_write_mmio16_ac97(s, addr + 2, hi);
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 4) {
        val = pcibase_read_bm32(s, addr);
    } else if (size == 1) {
        val = pcibase_read_bm8(s, addr);
    } else if (size == 2) {
        uint8_t lo = pcibase_read_bm8(s, addr);
        uint8_t hi = pcibase_read_bm8(s, addr + 1);
        val = lo | ((uint16_t)hi << 8);
    } else {
        val = 0xffffffffU;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        pcibase_write_bm32(s, addr, (uint32_t)val);
    } else if (size == 1) {
        pcibase_write_bm8(s, addr, (uint8_t)val);
    } else if (size == 2) {
        pcibase_write_bm8(s, addr, (uint8_t)(val & 0xff));
        pcibase_write_bm8(s, addr + 1, (uint8_t)((val >> 8) & 0xff));
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

    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));

    memset(&s->mdm_in, 0, sizeof(s->mdm_in));
    memset(&s->mdm_out, 0, sizeof(s->mdm_out));

    s->glob_cnt = 0;
    s->glob_sta = ICH_PCR; /* present primary codec */
    s->acc_sema = 0;       /* semaphore not held */
    s->irq_status = 0;

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* We expose two BARs similar to what the driver expects:
     * BAR0: bus-master I/O region (PIO, at least 64 bytes)
     * BAR2: AC97 MMIO region (memory, 256 bytes)
     */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 0x80; /* BM registers, enough for two streams + globals */
    s->bar_info[0].name  = "intel8x0m-bm";

    s->bar_info[1].index = 2; /* match typical use where BAR2 is MMIO for AC97 */
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 0x100; /* AC97 register block */
    s->bar_info[1].name  = "intel8x0m-ac97";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = msi_init(pdev, 0, 1, true, false, errp) == 0;
    s->has_msix = false;

    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_intel8x0m_pci",
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
