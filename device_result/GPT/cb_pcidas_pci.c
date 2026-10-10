/*
 * QEMU PCI device model for cb_pcidas (simplified, driver-visible behavior only)
 * Target: QEMU 8.2.10
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "cb_pcidas_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define AI_BUFFER_SIZE              1024
#define AO_BUFFER_SIZE              1024
#define PCIDAS_CTRL_REG             0x00
#define PCIDAS_CTRL_INT(x)          (((x) & 0x3) << 0)
#define PCIDAS_CTRL_INT_NONE        PCIDAS_CTRL_INT(0)
#define PCIDAS_CTRL_INT_EOS         PCIDAS_CTRL_INT(1)
#define PCIDAS_CTRL_INT_FHF         PCIDAS_CTRL_INT(2)
#define PCIDAS_CTRL_INT_FNE         PCIDAS_CTRL_INT(3)
#define PCIDAS_CTRL_INT_MASK        PCIDAS_CTRL_INT(3)
#define PCIDAS_CTRL_INTE            (1U << 2)
#define PCIDAS_CTRL_DAHFIE          (1U << 3)
#define PCIDAS_CTRL_EOAIE           (1U << 4)
#define PCIDAS_CTRL_DAHFI           (1U << 5)
#define PCIDAS_CTRL_EOAI            (1U << 6)
#define PCIDAS_CTRL_INT_CLR         (1U << 7)
#define PCIDAS_CTRL_EOBI            (1U << 9)
#define PCIDAS_CTRL_ADHFI           (1U << 10)
#define PCIDAS_CTRL_ADNEI           (1U << 11)
#define PCIDAS_CTRL_ADNE            (1U << 12)
#define PCIDAS_CTRL_DAEMIE          (1U << 12)
#define PCIDAS_CTRL_LADFUL          (1U << 13)
#define PCIDAS_CTRL_DAEMI           (1U << 14)
#define PCIDAS_CTRL_AI_INT          (PCIDAS_CTRL_EOAI | PCIDAS_CTRL_EOBI |   \
                                     PCIDAS_CTRL_ADHFI | PCIDAS_CTRL_ADNEI | \
                                     PCIDAS_CTRL_LADFUL)
#define PCIDAS_CTRL_AO_INT          (PCIDAS_CTRL_DAHFI | PCIDAS_CTRL_DAEMI)
#define PCIDAS_AI_REG               0x02
#define PCIDAS_AI_FIRST(x)          ((x) & 0xf)
#define PCIDAS_AI_LAST(x)           (((x) & 0xf) << 4)
#define PCIDAS_AI_CHAN(x)           (PCIDAS_AI_FIRST(x) | PCIDAS_AI_LAST(x))
#define PCIDAS_AI_GAIN(x)           (((x) & 0x3) << 8)
#define PCIDAS_AI_SE                (1U << 10)
#define PCIDAS_AI_UNIP              (1U << 11)
#define PCIDAS_AI_PACER(x)          (((x) & 0x3) << 12)
#define PCIDAS_AI_PACER_SW          PCIDAS_AI_PACER(0)
#define PCIDAS_AI_PACER_INT         PCIDAS_AI_PACER(1)
#define PCIDAS_AI_PACER_EXTN        PCIDAS_AI_PACER(2)
#define PCIDAS_AI_PACER_EXTP        PCIDAS_AI_PACER(3)
#define PCIDAS_AI_PACER_MASK        PCIDAS_AI_PACER(3)
#define PCIDAS_AI_EOC               (1U << 14)
#define PCIDAS_TRIG_REG             0x04
#define PCIDAS_TRIG_SEL(x)          (((x) & 0x3) << 0)
#define PCIDAS_TRIG_SEL_NONE        PCIDAS_TRIG_SEL(0)
#define PCIDAS_TRIG_SEL_SW          PCIDAS_TRIG_SEL(1)
#define PCIDAS_TRIG_SEL_EXT         PCIDAS_TRIG_SEL(2)
#define PCIDAS_TRIG_SEL_ANALOG      PCIDAS_TRIG_SEL(3)
#define PCIDAS_TRIG_SEL_MASK        PCIDAS_TRIG_SEL(3)
#define PCIDAS_TRIG_POL             (1U << 2)
#define PCIDAS_TRIG_MODE            (1U << 3)
#define PCIDAS_TRIG_EN              (1U << 4)
#define PCIDAS_TRIG_BURSTE          (1U << 5)
#define PCIDAS_TRIG_CLR             (1U << 7)
#define PCIDAS_CALIB_REG            0x06
#define PCIDAS_CALIB_8800_SEL       (1U << 8)
#define PCIDAS_CALIB_TRIM_SEL       (1U << 9)
#define PCIDAS_CALIB_DAC08_SEL      (1U << 10)
#define PCIDAS_CALIB_SRC(x)         (((x) & 0x7) << 11)
#define PCIDAS_CALIB_EN             (1U << 14)
#define PCIDAS_CALIB_DATA           (1U << 15)
#define PCIDAS_AO_REG               0x08
#define PCIDAS_AO_EMPTY             (1U << 0)
#define PCIDAS_AO_DACEN             (1U << 1)
#define PCIDAS_AO_START             (1U << 2)
#define PCIDAS_AO_PACER(x)          (((x) & 0x3) << 3)
#define PCIDAS_AO_PACER_SW          PCIDAS_AO_PACER(0)
#define PCIDAS_AO_PACER_INT         PCIDAS_AO_PACER(1)
#define PCIDAS_AO_PACER_EXTN        PCIDAS_AO_PACER(2)
#define PCIDAS_AO_PACER_EXTP        PCIDAS_AO_PACER(3)
#define PCIDAS_AO_PACER_MASK        PCIDAS_AO_PACER(3)
#define PCIDAS_AO_CHAN_EN(c)        (1U << (5 + ((c) & 0x1)))
#define PCIDAS_AO_CHAN_MASK         (PCIDAS_AO_CHAN_EN(0) | PCIDAS_AO_CHAN_EN(1))
#define PCIDAS_AO_UPDATE_BOTH       (1U << 7)
#define PCIDAS_AO_RANGE(c, r)       (((r) & 0x3) << (8 + 2 * ((c) & 0x1)))
#define PCIDAS_AO_RANGE_MASK(c)     PCIDAS_AO_RANGE((c), 0x3)
#define PCIDAS_AI_DATA_REG          0x00
#define PCIDAS_AI_FIFO_CLR_REG      0x02
#define PCIDAS_AI_8254_BASE         0x00
#define PCIDAS_8255_BASE            0x04
#define PCIDAS_AO_8254_BASE         0x08
#define PCIDAS_AO_DATA_REG(x)       (0x00 + ((x) * 2))
#define PCIDAS_AO_FIFO_REG          0x00
#define PCIDAS_AO_FIFO_CLR_REG      0x02
#define AMCC_OP_REG_MCSR_NVCMD      (AMCC_OP_REG_MCSR + 3)
#define MCSR_NV_BUSY                MCSR_NV_ENABLE
#define AMCC_OP_REG_MCSR_NVDATA     (AMCC_OP_REG_MCSR + 2)
#define MCSR_NV_LOAD_LOW_ADDR       0x0
#define MCSR_NV_READ                0x60
#define MCSR_NV_LOAD_HIGH_ADDR      0x20
#define MCSR_NV_ENABLE              0x80
#define INTCSR_INBOX_INTR_STATUS    0x20000
#define AMCC_OP_REG_IMB4            0x1c
#define INTCSR_INTR_ASSERTED        0x800000
#define AMCC_OP_REG_INTCSR          0x38
#define INTCSR_INBOX_SELECT(x)      (((x) & 0x3) << 10)
#define INTCSR_INBOX_BYTE(x)        (((x) & 0x3) << 8)
#define INTCSR_INBOX_FULL_INT       0x1000
#define AMCC_OP_REG_MCSR            0x3c

/* AMCC S5933 op regs base is BAR0 in the driver; we implement BAR0 as IO */

/* BAR metadata definition */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* simple register shadows */
    uint16_t bar1_ctrl;
    uint16_t bar1_ai;      /* AI_REG */
    uint16_t bar1_trig;    /* TRIG_REG */
    uint16_t bar1_calib;   /* CALIB_REG */
    uint16_t bar1_ao;      /* AO_REG */

    /* AMCC S5933 registers (BAR0, dword) */
    uint32_t amcc_mcsr;
    uint8_t  amcc_nvcmd;
    uint8_t  amcc_nvdata;
    uint16_t amcc_nv_addr;
    uint32_t amcc_intcsr;
    uint32_t amcc_imb4;

    /* AI state */
    bool     ai_eoc;       /* EOC bit in AI_REG */
    uint16_t ai_last_data;

    /* IRQ line cached */
    bool irq_asserted;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

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

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Helper: update INTx according to ctrl/INTCSR */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool want = false;

    /* AMCC side: INTCSR_INTR_ASSERTED bit can be set/cleared by driver; we
     * approximate that when INTE is set and any AI/AO interrupt condition is
     * latched in ctrl, interrupt is asserted.
     */
    if (s->bar1_ctrl & PCIDAS_CTRL_INTE) {
        if (s->bar1_ctrl & (PCIDAS_CTRL_AO_INT | PCIDAS_CTRL_AI_INT)) {
            want = true;
        }
    }

    if (want != s->irq_asserted) {
        pci_set_irq(pdev, want ? 1 : 0);
        s->irq_asserted = want;
    }
}

/* PIO (BAR0: AMCC) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case AMCC_OP_REG_MCSR:
        val = s->amcc_mcsr;
        break;
    case AMCC_OP_REG_MCSR_NVCMD:
        val = s->amcc_nvcmd;
        break;
    case AMCC_OP_REG_MCSR_NVDATA:
        val = s->amcc_nvdata;
        break;
    case AMCC_OP_REG_INTCSR:
        val = s->amcc_intcsr;
        break;
    case AMCC_OP_REG_IMB4:
        val = s->amcc_imb4;
        s->amcc_imb4 = 0;
        break;
    default:
        break;
    }

    if (size == 1) {
        return (uint8_t)val;
    } else if (size == 2) {
        return (uint16_t)val;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t wv = (uint32_t)val;

    switch (addr) {
    case AMCC_OP_REG_MCSR:
        s->amcc_mcsr = wv;
        break;
    case AMCC_OP_REG_MCSR_NVCMD:
        s->amcc_nvcmd = (uint8_t)wv;
        if ((s->amcc_nvcmd & (MCSR_NV_ENABLE | MCSR_NV_LOAD_LOW_ADDR)) ==
            (MCSR_NV_ENABLE | MCSR_NV_LOAD_LOW_ADDR)) {
            s->amcc_nv_addr &= 0xff00;
        } else if ((s->amcc_nvcmd & (MCSR_NV_ENABLE | MCSR_NV_LOAD_HIGH_ADDR)) ==
                   (MCSR_NV_ENABLE | MCSR_NV_LOAD_HIGH_ADDR)) {
            s->amcc_nv_addr &= 0x00ff;
        } else if ((s->amcc_nvcmd & (MCSR_NV_ENABLE | MCSR_NV_READ)) ==
                   (MCSR_NV_ENABLE | MCSR_NV_READ)) {
            /* simple deterministic fake content */
            s->amcc_nvdata = (uint8_t)(s->amcc_nv_addr & 0xff);
        }
        break;
    case AMCC_OP_REG_MCSR_NVDATA:
        if ((s->amcc_nvcmd & (MCSR_NV_ENABLE | MCSR_NV_LOAD_LOW_ADDR)) ==
            (MCSR_NV_ENABLE | MCSR_NV_LOAD_LOW_ADDR)) {
            s->amcc_nv_addr = (s->amcc_nv_addr & 0xff00) | ((uint8_t)wv);
        } else if ((s->amcc_nvcmd & (MCSR_NV_ENABLE | MCSR_NV_LOAD_HIGH_ADDR)) ==
                   (MCSR_NV_ENABLE | MCSR_NV_LOAD_HIGH_ADDR)) {
            s->amcc_nv_addr = (s->amcc_nv_addr & 0x00ff) | (((uint8_t)wv) << 8);
        } else {
            s->amcc_nvdata = (uint8_t)wv;
        }
        break;
    case AMCC_OP_REG_INTCSR:
        s->amcc_intcsr = wv;
        if (wv & INTCSR_INBOX_INTR_STATUS) {
            s->amcc_intcsr &= ~INTCSR_INTR_ASSERTED;
        }
        pcibase_update_irq(s);
        break;
    case AMCC_OP_REG_IMB4:
        s->amcc_imb4 = wv;
        break;
    default:
        break;
    }
}

/* MMIO (BAR1+: board regs) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* BAR1 region modeled starting at 0; driver uses 16-bit accesses */
    switch (addr) {
    case PCIDAS_CTRL_REG:
        return s->bar1_ctrl;
    case PCIDAS_AI_REG:
        return s->bar1_ai;
    case PCIDAS_TRIG_REG:
        return s->bar1_trig;
    case PCIDAS_CALIB_REG:
        return s->bar1_calib;
    case PCIDAS_AO_REG:
        return s->bar1_ao;
    default:
        break;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint16_t wv = (uint16_t)val;

    switch (addr) {
    case PCIDAS_CTRL_REG:
        s->bar1_ctrl = wv;
        /* clear bits requested by INT_CLR etc is handled by driver via write */
        pcibase_update_irq(s);
        break;
    case PCIDAS_AI_REG:
        s->bar1_ai = wv;
        break;
    case PCIDAS_TRIG_REG:
        s->bar1_trig = wv;
        break;
    case PCIDAS_CALIB_REG:
        s->bar1_calib = wv;
        break;
    case PCIDAS_AO_REG:
        s->bar1_ao = wv;
        break;
    default:
        break;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->bar1_ctrl = 0;
    s->bar1_ai = 0;
    s->bar1_trig = 0;
    s->bar1_calib = 0;
    s->bar1_ao = 0;

    s->amcc_mcsr = 0;
    s->amcc_nvcmd = 0;
    s->amcc_nvdata = 0;
    s->amcc_nv_addr = 0;
    s->amcc_intcsr = 0;
    s->amcc_imb4 = 0;

    s->ai_eoc = false;
    s->ai_last_data = 0;

    s->irq_asserted = false;
    pci_set_irq(pdev, 0);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* first entry in cb_pcidas_pci_table: PCI_VDEVICE(CB, 0x0001) */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1307);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0001);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR layout matching driver usage:
     * BAR0: AMCC S5933 I/O (we provide 0x40 bytes)
     * BAR1: board registers (we provide 0x10 bytes)
     * BAR2: AI FIFO/data (we provide 0x4 bytes: DATA+FIFO_CLR)
     * BAR3: pacer 8254 / 8255 etc (0x10 bytes, not decoded in detail)
     * BAR4: AO registers/FIFO (0x4 bytes: DATA0/FIFO, FIFO_CLR)
     */
    s->num_bars = 5;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x40;
    s->bar_info[0].name = "cb_pcidas-amcc";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 0x10;
    s->bar_info[1].name = "cb_pcidas-bar1";
    s->bar_info[1].sparse = false;

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 0x4;
    s->bar_info[2].name = "cb_pcidas-bar2";
    s->bar_info[2].sparse = false;

    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = 0x10;
    s->bar_info[3].name = "cb_pcidas-bar3";
    s->bar_info[3].sparse = false;

    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_PIO;
    s->bar_info[4].size = 0x4;
    s->bar_info[4].name = "cb_pcidas-bar4";
    s->bar_info[4].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

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

