/*
 * QEMU Advantech PCI-1710 Comedi device model (minimal, driver-driven)
 * Target QEMU: 8.2.x
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "standard-headers/linux/pci_regs.h"
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

#define TYPE_PCIBASE_DEVICE "adv_pci1710_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef BIT
#define BIT(nr)                 (1UL << (nr))
#endif
#define PCI171X_AD_DATA_REG 0x00
#define PCI171X_SOFTTRG_REG 0x00
#define PCI171X_RANGE_REG 0x02
#define PCI171X_RANGE_DIFF BIT(5)
#define PCI171X_RANGE_UNI BIT(4)
#define PCI171X_RANGE_GAIN(x) (((x) & 0x7) << 0)
#define PCI171X_MUX_REG 0x04
#define PCI171X_MUX_CHANH(x) (((x) & 0xff) << 8)
#define PCI171X_MUX_CHANL(x) (((x) & 0xff) << 0)
#define PCI171X_MUX_CHAN(x) (PCI171X_MUX_CHANH(x) | PCI171X_MUX_CHANL(x))
#define PCI171X_STATUS_REG 0x06
#define PCI171X_STATUS_IRQ BIT(11)
#define PCI171X_STATUS_FF BIT(10)
#define PCI171X_STATUS_FH BIT(9)
#define PCI171X_STATUS_FE BIT(8)
#define PCI171X_CTRL_REG 0x06
#define PCI171X_CTRL_CNT0 BIT(6)
#define PCI171X_CTRL_ONEFH BIT(5)
#define PCI171X_CTRL_IRQEN BIT(4)
#define PCI171X_CTRL_GATE BIT(3)
#define PCI171X_CTRL_EXT BIT(2)
#define PCI171X_CTRL_PACER BIT(1)
#define PCI171X_CTRL_SW BIT(0)
#define PCI171X_CLRINT_REG 0x08
#define PCI171X_CLRFIFO_REG 0x09
#define PCI171X_DA_REG(x) (0x0a + ((x) * 2))
#define PCI171X_DAREF_REG 0x0e
#define PCI171X_DAREF(c, r) (((r) & 0x3) << ((c) * 2))
#define PCI171X_DAREF_MASK(c) PCI171X_DAREF((c), 0x3)
#define PCI171X_DI_REG 0x10
#define PCI171X_DO_REG 0x10
#define PCI171X_TIMER_BASE 0x18

struct comedi_krange {
    int min;
    int max;
    unsigned int flags;
};

struct comedi_lrange {
    int length;
    struct comedi_krange range[];
};

enum pci1710_boardid {
    BOARD_PCI1710,
    BOARD_PCI1710HG,
    BOARD_PCI1711,
    BOARD_PCI1713,
    BOARD_PCI1731,
};

struct boardtype {
    const char *name;
    const struct comedi_lrange *ai_range;
    unsigned int is_pci1711:1;
    unsigned int is_pci1713:1;
    unsigned int has_ao:1;
};

struct pci1710_private {
    unsigned int max_samples;
    unsigned int ctrl;
    unsigned int ctrl_ext;
    unsigned int mux_scan;
    unsigned char ai_et;
    unsigned int act_chanlist[32];
    unsigned char saved_seglen;
    unsigned char da_ranges;
    unsigned char unipolar_gain;
};

#define PCIBASE_VENDOR_ID 0x13fe
#define PCIBASE_DEVICE_ID 0x1710
#define PCIBASE_CLASS_ID  PCI_CLASS_OTHERS

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

    /* interrupt state */
    uint16_t status_reg;
    uint16_t ctrl_reg;
    uint16_t range_reg;
    uint16_t mux_reg;
    uint8_t clrfifo_reg;
    uint8_t clrint_reg;
    uint16_t daref_reg;
    uint16_t di_state;
    uint16_t do_state;
    uint16_t ad_data_reg;

    /* simple FIFO model for AI samples */
    uint16_t fifo[4096];
    unsigned int fifo_head;
    unsigned int fifo_tail;

    /* irq line cached */
    bool irq_line;
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
    .valid = { .min_access_size = 1, .max_access_size = 8 },
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

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool want = (s->status_reg & PCI171X_STATUS_IRQ) != 0;
    if (want != s->irq_line) {
        pci_set_irq(pdev, want);
        s->irq_line = want;
    }
}

static bool pcibase_fifo_empty(PCIBaseState *s)
{
    return s->fifo_head == s->fifo_tail;
}

static bool pcibase_fifo_full(PCIBaseState *s)
{
    return ((s->fifo_head + 1) % G_N_ELEMENTS(s->fifo)) == s->fifo_tail;
}

static unsigned int pcibase_fifo_level(PCIBaseState *s)
{
    if (s->fifo_head >= s->fifo_tail) {
        return s->fifo_head - s->fifo_tail;
    }
    return G_N_ELEMENTS(s->fifo) - (s->fifo_tail - s->fifo_head);
}

static void pcibase_fifo_push(PCIBaseState *s, uint16_t sample)
{
    if (pcibase_fifo_full(s)) {
        s->status_reg |= PCI171X_STATUS_FF;
        return;
    }
    s->fifo[s->fifo_head] = sample;
    s->fifo_head = (s->fifo_head + 1) % G_N_ELEMENTS(s->fifo);
    s->status_reg &= ~PCI171X_STATUS_FE;
    if (pcibase_fifo_level(s) >= 2) {
        s->status_reg |= PCI171X_STATUS_FH;
    }
    if (s->ctrl_reg & PCI171X_CTRL_IRQEN) {
        s->status_reg |= PCI171X_STATUS_IRQ;
    }
    pcibase_update_irq(s);
}

static bool pcibase_fifo_pop(PCIBaseState *s, uint16_t *psample)
{
    if (pcibase_fifo_empty(s)) {
        s->status_reg |= PCI171X_STATUS_FE;
        s->status_reg &= ~PCI171X_STATUS_FH;
        return false;
    }
    *psample = s->fifo[s->fifo_tail];
    s->fifo_tail = (s->fifo_tail + 1) % G_N_ELEMENTS(s->fifo);
    if (pcibase_fifo_empty(s)) {
        s->status_reg |= PCI171X_STATUS_FE;
        s->status_reg &= ~PCI171X_STATUS_FH;
    }
    return true;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PCI171X_AD_DATA_REG: {
        uint16_t sample = 0;
        if (!pcibase_fifo_pop(s, &sample)) {
            sample = s->ad_data_reg;
        } else {
            s->ad_data_reg = sample;
        }
        if (pcibase_fifo_empty(s)) {
            s->status_reg |= PCI171X_STATUS_FE;
            s->status_reg &= ~PCI171X_STATUS_FH;
        }
        if (pcibase_fifo_empty(s)) {
            s->status_reg &= ~(PCI171X_STATUS_IRQ);
            pcibase_update_irq(s);
        }
        return sample;
    }
    case PCI171X_RANGE_REG:
        return s->range_reg;
    case PCI171X_MUX_REG:
        return s->mux_reg;
    case PCI171X_STATUS_REG:
        return s->status_reg;
    case PCI171X_DAREF_REG:
        return s->daref_reg;
    case PCI171X_DI_REG:
        return s->di_state;
    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint16_t wval = (uint16_t)val;

    switch (addr) {
    case PCI171X_SOFTTRG_REG:
        if (s->ctrl_reg & PCI171X_CTRL_SW) {
            if (!pcibase_fifo_full(s)) {
                uint16_t sample = (uint16_t)(pcibase_fifo_level(s) & 0x0fff);
                pcibase_fifo_push(s, sample);
            } else {
                s->status_reg |= PCI171X_STATUS_FF;
            }
        }
        return;
    case PCI171X_RANGE_REG:
        s->range_reg = wval;
        return;
    case PCI171X_MUX_REG:
        s->mux_reg = wval;
        return;
    case PCI171X_CTRL_REG:
        s->ctrl_reg = wval;
        if ((s->ctrl_reg & PCI171X_CTRL_SW) == 0) {
            s->fifo_head = s->fifo_tail = 0;
            s->status_reg |= PCI171X_STATUS_FE;
            s->status_reg &= ~(PCI171X_STATUS_FF | PCI171X_STATUS_FH | PCI171X_STATUS_IRQ);
            pcibase_update_irq(s);
        }
        return;
    case PCI171X_CLRINT_REG:
        s->clrint_reg = (uint8_t)val;
        s->status_reg &= ~PCI171X_STATUS_IRQ;
        pcibase_update_irq(s);
        return;
    case PCI171X_CLRFIFO_REG:
        s->clrfifo_reg = (uint8_t)val;
        s->fifo_head = s->fifo_tail = 0;
        s->status_reg |= PCI171X_STATUS_FE;
        s->status_reg &= ~(PCI171X_STATUS_FF | PCI171X_STATUS_FH);
        return;
    case PCI171X_DA_REG(0):
    case PCI171X_DA_REG(1):
        return;
    case PCI171X_DAREF_REG:
        s->daref_reg = wval;
        return;
    case PCI171X_DO_REG:
        s->do_state = wval;
        return;
    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    if (addr < 0x100) {
        return pcibase_mmio_read(opaque, addr, size);
    }

    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    if (addr < 0x100) {
        pcibase_mmio_write(opaque, addr, val, size);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->status_reg = PCI171X_STATUS_FE;
    s->ctrl_reg = 0;
    s->range_reg = 0;
    s->mux_reg = 0;
    s->clrfifo_reg = 0;
    s->clrint_reg = 0;
    s->daref_reg = 0;
    s->di_state = 0;
    s->do_state = 0;
    s->ad_data_reg = 0;
    s->fifo_head = s->fifo_tail = 0;
    s->irq_line = false;

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
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x10b5);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x9050);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x20;
    s->bar_info[0].name = "adv_pci1710-bar2";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    s->mmio_backing = NULL;
    s->mmio_backing_size = 0;

    s->status_reg = PCI171X_STATUS_FE;
    s->ctrl_reg = 0;
    s->range_reg = 0;
    s->mux_reg = 0;
    s->clrfifo_reg = 0;
    s->clrint_reg = 0;
    s->daref_reg = 0;
    s->di_state = 0;
    s->do_state = 0;
    s->ad_data_reg = 0;
    s->fifo_head = s->fifo_tail = 0;
    s->irq_line = false;

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

