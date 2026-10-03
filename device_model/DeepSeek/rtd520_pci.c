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

#define TYPE_PCIBASE_DEVICE "rtd520_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* PCI Identification */
#define PCI_VENDOR_ID_RTD          0x1407  /* Real Time Devices */
#define PCI_DEVICE_ID_RTD_7520     0x7520
#define PCI_CLASS_DEVICE           0x0a

/* LAS0 Register Offsets */
#define LAS0_USER_IO          0x0008
#define LAS0_ADC              0x0010
#define LAS0_UPDATE_DAC(x)    (0x0014 + ((x) * 0x4))
#define LAS0_DAC              0x0024
#define LAS0_PACER            0x0028
#define LAS0_TIMER            0x002c
#define LAS0_IT               0x0030
#define LAS0_CLEAR            0x0034
#define LAS0_OVERRUN          0x0038
#define LAS0_PCLK             0x0040
#define LAS0_BCLK             0x0044
#define LAS0_ADC_SCNT         0x0048
#define LAS0_DAC1_UCNT        0x004c
#define LAS0_DAC2_UCNT        0x0050
#define LAS0_DCNT             0x0054
#define LAS0_ACNT             0x0058
#define LAS0_DAC_CLK          0x005c
#define LAS0_8254_TIMER_BASE  0x0060
#define LAS0_DIO0             0x0070
#define LAS0_DIO1             0x0074
#define LAS0_DIO0_CTRL        0x0078
#define LAS0_DIO_STATUS       0x007c
#define LAS0_BOARD_RESET      0x0100
#define LAS0_DMA0_SRC         0x0104
#define LAS0_DMA1_SRC         0x0108
#define LAS0_ADC_CONVERSION   0x010c
#define LAS0_BURST_START      0x0110
#define LAS0_PACER_START      0x0114
#define LAS0_PACER_STOP       0x0118
#define LAS0_ACNT_STOP_ENABLE 0x011c
#define LAS0_PACER_REPEAT     0x0120
#define LAS0_DIN_START        0x0124
#define LAS0_DIN_FIFO_CLEAR   0x0128
#define LAS0_ADC_FIFO_CLEAR   0x012c
#define LAS0_CGT_WRITE        0x0130
#define LAS0_CGL_WRITE        0x0134
#define LAS0_CG_DATA          0x0138
#define LAS0_CGT_ENABLE       0x013c
#define LAS0_CG_ENABLE        0x0140
#define LAS0_CGT_PAUSE        0x0144
#define LAS0_CGT_RESET        0x0148
#define LAS0_CGT_CLEAR        0x014c
#define LAS0_DAC_CTRL(x)      (0x0150 + ((x) * 0x14))
#define LAS0_DAC_SRC(x)       (0x0154 + ((x) * 0x14))
#define LAS0_DAC_CYCLE(x)     (0x0158 + ((x) * 0x14))
#define LAS0_DAC_RESET(x)     (0x015c + ((x) * 0x14))
#define LAS0_DAC_FIFO_CLEAR(x) (0x0160 + ((x) * 0x14))
#define LAS0_ADC_SCNT_SRC     0x0178
#define LAS0_PACER_SELECT     0x0180
#define LAS0_SBUS0_SRC        0x0184
#define LAS0_SBUS0_ENABLE     0x0188
#define LAS0_SBUS1_SRC        0x018c
#define LAS0_SBUS1_ENABLE     0x0190
#define LAS0_SBUS2_SRC        0x0198
#define LAS0_SBUS2_ENABLE     0x019c
#define LAS0_ETRG_POLARITY    0x01a4
#define LAS0_EINT_POLARITY    0x01a8
#define LAS0_8254_CLK_SEL(x)  (0x01ac + ((x) * 0x8))
#define LAS0_8254_GATE_SEL(x) (0x01b0 + ((x) * 0x8))
#define LAS0_UOUT0_SELECT     0x01c4
#define LAS0_UOUT1_SELECT     0x01c8
#define LAS0_DMA0_RESET       0x01cc
#define LAS0_DMA1_RESET       0x01d0

/* LAS1 FIFO Offsets */
#define LAS1_ADC_FIFO         0x0000
#define LAS1_HDIO_FIFO        0x0004
#define LAS1_DAC_FIFO(x)      (0x0008 + ((x) * 0x4))

/* Interrupt Mask Bits (LAS0_IT register) */
#define IRQM_ADC_FIFO_WRITE   BIT(0)
#define IRQM_CGT_RESET        BIT(1)
#define IRQM_CGT_PAUSE        BIT(3)
#define IRQM_ADC_ABOUT_CNT    BIT(4)
#define IRQM_ADC_DELAY_CNT    BIT(5)
#define IRQM_ADC_SAMPLE_CNT   BIT(6)
#define IRQM_DAC1_UCNT        BIT(7)
#define IRQM_DAC2_UCNT        BIT(8)
#define IRQM_UTC1             BIT(9)
#define IRQM_UTC1_INV         BIT(10)
#define IRQM_UTC2             BIT(11)
#define IRQM_DIGITAL_IT       BIT(12)
#define IRQM_EXTERNAL_IT      BIT(13)
#define IRQM_ETRIG_RISING     BIT(14)
#define IRQM_ETRIG_FALLING    BIT(15)

/* FIFO Status Bits (LAS0_ADC reads) */
#define FS_DAC1_NOT_EMPTY     BIT(0)
#define FS_DAC1_HEMPTY        BIT(1)
#define FS_DAC1_NOT_FULL      BIT(2)
#define FS_DAC2_NOT_EMPTY     BIT(4)
#define FS_DAC2_HEMPTY        BIT(5)
#define FS_DAC2_NOT_FULL      BIT(6)
#define FS_ADC_NOT_EMPTY      BIT(8)
#define FS_ADC_HEMPTY         BIT(9)
#define FS_ADC_NOT_FULL       BIT(10)
#define FS_DIN_NOT_EMPTY      BIT(12)
#define FS_DIN_HEMPTY         BIT(13)
#define FS_DIN_NOT_FULL       BIT(14)

/* PLX Interrupt Control */
#define PLX_REG_INTCSR     0x0068
#define PLX_INTCSR_PLIEN   BIT(11)
#define PLX_INTCSR_PIEN    BIT(8)

/* Misc Constants */
#define RTD_CLOCK_RATE     8000000
#define RTD_CLOCK_BASE     125

#define ADC_FIFO_SIZE 1024   /* 1024 samples (0x400) */

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

    bool has_msi;
    bool has_msix;
    uint32_t int_enable;  /* LAS0_IT interrupt enable mask */
    uint32_t int_status;  /* Interrupt status / pending */

    /* Hardware Register Shadows */
    uint32_t las0_regs[0x200 / 4];  /* LAS0 area, size 0x200 bytes */
    uint32_t las1_regs[0x10 / 4];   /* LAS1 area, size 0x10 bytes */

    /* ADC FIFO modeling (1024 entries) */
    uint16_t adc_fifo[ADC_FIFO_SIZE];
    unsigned int adc_fifo_wp;  /* write pointer (next insertion index) */
    unsigned int adc_fifo_rp;  /* read pointer (next removal index) */

    /* PLX bridge config shadow (only INTCSR needed) */
    uint32_t plx_intcsr;

    /* Operational status flags */
    bool reset_initiated;        /* Probe reset state */
};

/* Internal helper: update IRQ according to enable and status */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->int_status & s->int_enable) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/******************************************************************************
 * ADC FIFO helpers
 *****************************************************************************/
static inline unsigned int adc_fifo_fill_level(PCIBaseState *s)
{
    /* Compute number of samples currently in FIFO */
    return (s->adc_fifo_wp - s->adc_fifo_rp) % ADC_FIFO_SIZE;
}

static void adc_fifo_push(PCIBaseState *s, uint16_t value)
{
    unsigned int next_wp = (s->adc_fifo_wp + 1) % ADC_FIFO_SIZE;
    if (next_wp == s->adc_fifo_rp) {
        /* FIFO full, drop */
        return;
    }
    s->adc_fifo[s->adc_fifo_wp] = value;
    s->adc_fifo_wp = next_wp;
}

static uint16_t adc_fifo_pop(PCIBaseState *s)
{
    uint16_t value = 0;
    if (s->adc_fifo_rp != s->adc_fifo_wp) {
        value = s->adc_fifo[s->adc_fifo_rp];
        s->adc_fifo_rp = (s->adc_fifo_rp + 1) % ADC_FIFO_SIZE;
    }
    return value;
}

static void adc_fifo_clear(PCIBaseState *s)
{
    s->adc_fifo_wp = 0;
    s->adc_fifo_rp = 0;
}

/******************************************************************************
 * PLX configuration BAR (BAR0) handlers
 *****************************************************************************/
static uint64_t plx_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xffffffff;

    switch (addr) {
    case PLX_REG_INTCSR:
        val = s->plx_intcsr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from unknown PLX offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void plx_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PLX_REG_INTCSR:
        s->plx_intcsr = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to unknown PLX offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps plx_mmio_ops = {
    .read = plx_mmio_read,
    .write = plx_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/******************************************************************************
 * LAS0 register space (BAR2) handlers
 *****************************************************************************/
static uint64_t las0_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xffffffff;

    if (addr >= 0x200) {
        return val;
    }

    /* Special handling for LAS0_ADC (0x0010) to deliver FIFO status */
    if (addr == LAS0_ADC) {
        unsigned int level = adc_fifo_fill_level(s);
        val = 0;
        if (level > 0) {
            val |= FS_ADC_NOT_EMPTY;
        }
        if (level < ADC_FIFO_SIZE / 2) {
            val |= FS_ADC_HEMPTY;
        }
        if (level < ADC_FIFO_SIZE) {
            val |= FS_ADC_NOT_FULL;
        }
        /* DAC and DIO status bits not used, keep 0 */
        return val;
    }

    /* Default: read from shadow array */
    val = s->las0_regs[addr / 4];
    return val;
}

static void las0_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x200) {
        return;
    }

    /* Handle special registers */
    switch (addr) {
    case LAS0_ADC:  /* 0x0010 - trigger conversion */
        /* push a dummy sample into FIFO */
        adc_fifo_push(s, 0);
        /* Writing does not store a value; break */
        break;

    case LAS0_ADC_FIFO_CLEAR:  /* 0x012c - clear ADC FIFO */
        adc_fifo_clear(s);
        break;

    case LAS0_CLEAR:    /* 0x0034 - write-1-to-clear interrupt */
        s->int_status &= ~val;
        pcibase_update_irq(s);
        break;

    case LAS0_IT:       /* 0x0030 - interrupt enable mask */
        s->int_enable = val;
        pcibase_update_irq(s);
        break;

    case LAS0_BOARD_RESET:  /* 0x0100 - board reset */
        /* Trigger full device reset */
        s->int_enable = 0;
        s->int_status = 0;
        adc_fifo_clear(s);
        memset(s->las0_regs, 0, sizeof(s->las0_regs));
        s->plx_intcsr = 0;
        pci_set_irq(PCI_DEVICE(s), 0);
        break;

    default:
        /* Store to shadow array */
        s->las0_regs[addr / 4] = val;
        break;
    }
}

static const MemoryRegionOps las0_mmio_ops = {
    .read = las0_mmio_read,
    .write = las0_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/******************************************************************************
 * LAS1 register space (BAR3) handlers
 *****************************************************************************/
static uint64_t las1_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xffffffff;

    switch (addr) {
    case LAS1_ADC_FIFO:   /* 0x0000 - read ADC sample */
        val = adc_fifo_pop(s);
        break;
    default:
        /* Return shadow value */
        if (addr < 0x10) {
            val = s->las1_regs[addr / 4];
        }
        break;
    }
    return val;
}

static void las1_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case LAS1_ADC_FIFO:
        /* Should be read-only, but ignore */
        break;
    case LAS1_DAC_FIFO(0):  /* 0x0008 */
    case LAS1_DAC_FIFO(1):  /* 0x000c */
        /* DAC FIFO writes accepted, ignore */
        break;
    default:
        if (addr < 0x10) {
            s->las1_regs[addr / 4] = val;
        }
        break;
    }
}

static const MemoryRegionOps las1_mmio_ops = {
    .read = las1_mmio_read,
    .write = las1_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/******************************************************************************
 * Reset and Realize
 *****************************************************************************/
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->int_enable = 0;
    s->int_status = 0;
    adc_fifo_clear(s);
    memset(s->las0_regs, 0, sizeof(s->las0_regs));
    memset(s->las1_regs, 0, sizeof(s->las1_regs));
    s->plx_intcsr = 0;
    pci_set_irq(PCI_DEVICE(s), 0);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi,
                                 const MemoryRegionOps *ops, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_RTD);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_RTD_7520);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DEVICE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR assignments */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x100, .name = "plx-config" };
    s->bar_info[1] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "las0" };
    s->bar_info[2] = (BARInfo){ .index = 3, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "las1" };

    pcibase_register_bar(pdev, s, &s->bar_info[0], &plx_mmio_ops, errp);
    pcibase_register_bar(pdev, s, &s->bar_info[1], &las0_mmio_ops, errp);
    pcibase_register_bar(pdev, s, &s->bar_info[2], &las1_mmio_ops, errp);

    /* No MSI/MSI-X needed */
    s->has_msi = false;
    s->has_msix = false;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No MSI/MSI-X cleanup needed */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "rtd520_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(int_enable, PCIBaseState),
        VMSTATE_UINT32(int_status, PCIBaseState),
        VMSTATE_UINT32_ARRAY(las0_regs, PCIBaseState, 0x200 / 4),
        VMSTATE_UINT32_ARRAY(las1_regs, PCIBaseState, 0x10 / 4),
        VMSTATE_UINT16_ARRAY(adc_fifo, PCIBaseState, ADC_FIFO_SIZE),
        VMSTATE_UINT32(adc_fifo_wp, PCIBaseState),
        VMSTATE_UINT32(adc_fifo_rp, PCIBaseState),
        VMSTATE_UINT32(plx_intcsr, PCIBaseState),
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

type_init(pcibase_register_types)