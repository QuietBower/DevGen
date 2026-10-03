/*
 * Integrated QEMU PCI device model for Intel ICH AC97 (snd_intel8x0)
 * Based on Linux driver intel8x0.c and QEMU 8.2.10 APIs.
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

#define TYPE_PCIBASE_DEVICE "snd_intel8x0_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware identifiers from driver */
#define PCI_VENDOR_ID_INTEL 0x8086
#define INTEL8X0_DEVICE_ID  0x2415
#define INTEL8X0_CLASS_ID   0x0401

/* Global register offsets (BAR0) */
#define GLOB_CNT    0x2c
#define GLOB_STA    0x30
#define ACC_SEMA    0x34
#define ACC_OFFSET  0x60
#define ACC_DATA    0x64
#define SDM_REG     0x80

/* GLOB_CNT bits */
#define ICH_ACLINK      0x00000001
#define ICH_AC97WARM    0x00000002
#define ICH_AC97COLD    0x00000004

/* GLOB_STA bits */
#define ICH_RCS         0x00000001
#define ICH_MCINT       0x00000002
#define ICH_POINT       0x00000004
#define ICH_PIINT       0x00000008
#define ICH_GSCI        0x00000020
#define ICH_PRI         0x00000100
#define ICH_SRI         0x00000200
#define ICH_TRI         0x00000400
#define ICH_SAMPLE_16_20 0x00002000
#define ICH_PCR          0x00000040  /* primary codec ready */
#define ICH_SCR          0x00000080  /* secondary codec ready */

/* Bus master (BAR1) channel offsets */
#define OFF_CR      0x00
#define OFF_SR      0x01
#define OFF_BDBAR   0x04
#define OFF_LVI     0x0c
#define OFF_CIV     0x10
#define OFF_PICB    0x14

/* CR bits */
#define ICH_IOCE        0x01
#define ICH_STARTBM     0x04
#define ICH_RESETREGS   0x80

/* SR bits */
#define ICH_DCH         0x02
#define ICH_LVBCI       0x04
#define ICH_BCIS        0x08
#define ICH_FIFOE       0x10

static const uint32_t channel_base[] = { 0x00, 0x10, 0x20, 0x40, 0x50, 0x60 };
#define MAX_CHANNELS 6

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

/* DMA descriptor structure */
union dma_tx_cnt {
    struct {
        unsigned int count    : 19;
        unsigned int reserved : 12;
        unsigned ioc          : 1;
    } bitfields, bits;
    unsigned int u32_all;
    signed int i32_all;
};
struct intel8x0_dma_desc {
    unsigned int src_addr;
    unsigned int dest_addr;
    union dma_tx_cnt tx_cnt;
    unsigned int reserved;
};

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;
    uint32_t int_sta_reg;

    struct intel8x0_hw_regs {
        uint32_t glob_cnt;
        uint32_t glob_sta;
        uint32_t acc_sema;
        uint32_t sdm;
    } regs;

    struct {
        uint8_t  cr;
        uint8_t  sr;
        uint32_t bdbar;
        uint8_t  lvi;
        uint8_t  civ;
        uint16_t picb;
    } channels[MAX_CHANNELS];

    /* Codec indirect access state */
    uint16_t acc_offset;
};

/* Codec register read helper */
static uint16_t codec_reg_read(int reg)
{
    switch (reg) {
    case 0x00: return 0x8000;  /* reset: codec ready */
    case 0x7c: return 0x4144;
    case 0x7e: return 0x5300;
    case 0x26: return 0x000f;  /* powerdown: all on */
    default:   return 0x0000;
    }
}

/* ---------- BAR0 : Native ICH registers ---------- */
static uint64_t pcibase_native_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case GLOB_CNT:
        if (size == 4) val = s->regs.glob_cnt;
        break;
    case GLOB_STA:
        if (size == 4) val = s->regs.glob_sta;
        break;
    case ACC_SEMA:
        if (size == 1) val = 0;  /* semaphore free */
        break;
    case ACC_OFFSET:
        if (size == 2) val = s->acc_offset;
        break;
    case ACC_DATA:
        if (size == 2) {
            int codec = s->acc_offset & 1;
            int reg   = (s->acc_offset >> 1) & 0x7f;
            val = codec_reg_read(reg);
        }
        break;
    case SDM_REG:
        if (size == 1) val = s->regs.sdm;
        break;
    default:
        val = 0xffffffffULL;
        break;
    }
    return val;
}

static void pcibase_native_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case GLOB_CNT:
        if (size == 4) {
            s->regs.glob_cnt = val;
            if (val & ICH_AC97COLD) {
                s->regs.glob_sta |= ICH_PCR | ICH_SCR;
                s->regs.glob_cnt &= ~ICH_AC97COLD;
            }
            if (val & ICH_AC97WARM) {
                s->regs.glob_sta |= ICH_PCR | ICH_SCR;
                s->regs.glob_cnt &= ~ICH_AC97WARM;
            }
        }
        break;
    case GLOB_STA:
        if (size == 4) s->regs.glob_sta &= ~val;
        break;
    case ACC_SEMA:
        break;  /* ignore writes */
    case ACC_OFFSET:
        if (size == 2) s->acc_offset = (uint16_t)val;
        break;
    case ACC_DATA:
        if (size == 2) break;  /* codec writes ignored */
        break;
    case SDM_REG:
        if (size == 1) s->regs.sdm = (uint8_t)val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_native_mmio_ops = {
    .read = pcibase_native_mmio_read,
    .write = pcibase_native_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------- BAR1 : Bus Master registers ---------- */
static int get_channel_for_addr(hwaddr addr, uint32_t *ch_offset)
{
    int i;
    for (i = 0; i < MAX_CHANNELS; i++) {
        if (addr >= channel_base[i] && addr < channel_base[i] + 0x30) {
            *ch_offset = channel_base[i];
            return i;
        }
    }
    return -1;
}

static uint64_t pcibase_bm_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t ch_base;
    int ch = get_channel_for_addr(addr, &ch_base);

    if (ch >= 0) {
        uint32_t reg = addr - ch_base;
        switch (reg) {
        case OFF_CR:
            if (size == 1) val = s->channels[ch].cr;
            break;
        case OFF_SR:
            if (size == 1) val = s->channels[ch].sr;
            break;
        case OFF_BDBAR:
            if (size == 4) val = s->channels[ch].bdbar;
            break;
        case OFF_LVI:
            if (size == 1) val = s->channels[ch].lvi;
            break;
        case OFF_CIV:
            if (size == 1) val = s->channels[ch].civ;
            break;
        case OFF_PICB:
            if (size == 2 || size == 4) val = s->channels[ch].picb;
            break;
        default:
            break;
        }
    }
    return val;
}

static void pcibase_bm_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t ch_base;
    int ch = get_channel_for_addr(addr, &ch_base);

    if (ch >= 0) {
        uint32_t reg = addr - ch_base;
        switch (reg) {
        case OFF_CR:
            if (size == 1) {
                s->channels[ch].cr = val & 0xff;
                if (s->channels[ch].cr & ICH_RESETREGS) {
                    s->channels[ch].cr &= ~ICH_RESETREGS;
                }
            }
            break;
        case OFF_SR:
            if (size == 1) {
                s->channels[ch].sr &= ~(val & 0xff);
            }
            break;
        case OFF_BDBAR:
            if (size == 4) s->channels[ch].bdbar = val;
            break;
        case OFF_LVI:
            if (size == 1) s->channels[ch].lvi = val;
            break;
        case OFF_CIV:
            if (size == 1) s->channels[ch].civ = val;
            break;
        case OFF_PICB:
            if (size == 2 || size == 4) s->channels[ch].picb = val;
            break;
        default:
            break;
        }
    }
}

static const MemoryRegionOps pcibase_bm_mmio_ops = {
    .read = pcibase_bm_mmio_read,
    .write = pcibase_bm_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------- PIO (unused) ---------- */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------- Reset ---------- */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->regs.glob_cnt = 0;
    s->regs.glob_sta = ICH_PRI | ICH_SRI | ICH_TRI | ICH_PCR | ICH_SCR;
    s->regs.acc_sema = 0;
    s->regs.sdm = 0;
    memset(s->channels, 0, sizeof(s->channels));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->acc_offset = 0;
}

/* ---------- BAR registration helper ---------- */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi,
                                 const MemoryRegionOps *mmio_ops,
                                 const MemoryRegionOps *pio_ops, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ---------- Realize ---------- */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  INTEL8X0_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, INTEL8X0_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo) { .index = 0, .type = BAR_TYPE_MMIO, .size = 0x200, .name = "intel8x0-native" };
    s->bar_info[1] = (BARInfo) { .index = 1, .type = BAR_TYPE_MMIO, .size = 0x100, .name = "intel8x0-bm" };

    pcibase_register_bar(pdev, s, &s->bar_info[0], &pcibase_native_mmio_ops, &pcibase_pio_ops, errp);
    pcibase_register_bar(pdev, s, &s->bar_info[1], &pcibase_bm_mmio_ops, &pcibase_pio_ops, errp);
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
    .name = "snd_intel8x0_pci",
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
