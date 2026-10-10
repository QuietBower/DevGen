/*
 * QEMU Advantech PCI DIO family stub device for adv_pci_dio comedi driver
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

#define TYPE_PCIBASE_DEVICE "adv_pci_dio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI173X_INT_EN_REG 0x0008
#define PCI173X_INT_RF_REG 0x000c
#define PCI173X_INT_FLAG_REG 0x0010
#define PCI173X_INT_CLR_REG 0x0010
#define PCI173X_INT_IDI0 0x01
#define PCI173X_INT_IDI1 0x02
#define PCI173X_INT_DI0  0x04
#define PCI173X_INT_DI1  0x08
#define PCI1750_INT_REG 0x20
#define PCI1753_INT_REG(x) (0x10 + (x))
#define PCI1753E_INT_REG(x) (0x30 + (x))
#define PCI1754_INT_REG(x) (0x08 + (x) * 2)
#define PCI1752_CFC_REG 0x12
#define PCI1761_INT_EN_REG 0x03
#define PCI1761_INT_RF_REG 0x04
#define PCI1761_INT_CLR_REG 0x05
#define PCI1762_INT_REG 0x06
#define PCI_DIO_MAX_DI_SUBDEVS 2
#define PCI_DIO_MAX_DO_SUBDEVS 2
#define PCI_DIO_MAX_DIO_SUBDEVG 2
#define PCI_DIO_MAX_IRQ_SUBDEVS 4

enum pci_dio_boardid {
    TYPE_PCI1730,
    TYPE_PCI1733,
    TYPE_PCI1734,
    TYPE_PCI1735,
    TYPE_PCI1736,
    TYPE_PCI1739,
    TYPE_PCI1750,
    TYPE_PCI1751,
    TYPE_PCI1752,
    TYPE_PCI1753,
    TYPE_PCI1753E,
    TYPE_PCI1754,
    TYPE_PCI1756,
    TYPE_PCI1761,
    TYPE_PCI1762
};

struct diosubd_data {
    int chans;
    unsigned long addr;
};

struct dio_irq_subd_data {
    unsigned short int_en;
    unsigned long addr;
};

struct dio_boardtype {
    const char *name;
    int nsubdevs;
    struct diosubd_data sdi[PCI_DIO_MAX_DI_SUBDEVS];
    struct diosubd_data sdo[PCI_DIO_MAX_DO_SUBDEVS];
    struct diosubd_data sdio[PCI_DIO_MAX_DIO_SUBDEVG];
    struct dio_irq_subd_data sdirq[PCI_DIO_MAX_IRQ_SUBDEVS];
    unsigned long id_reg;
    unsigned long timer_regbase;
    unsigned int is_16bit:1;
};

struct pci_dio_dev_private_data {
    int boardtype;
    int irq_subd;
    unsigned short int_ctrl;
    unsigned short int_rf;
};

struct pci_dio_sd_private_data {
    /* host driver uses spinlock_t, but we don't need it in device model */
    unsigned long port_offset;
    short int cmd_running;
};

static const struct dio_boardtype boardtypes[] __attribute__((unused)) = {
    [TYPE_PCI1730] = {
        .name        = "pci1730",
        .nsubdevs    = 9,
        .sdi[0]      = { 16, 0x02, },
        .sdi[1]      = { 16, 0x00, },
        .sdo[0]      = { 16, 0x02, },
        .sdo[1]      = { 16, 0x00, },
        .id_reg      = 0x04,
        .sdirq[0]    = { PCI173X_INT_DI0,  0x02, },
        .sdirq[1]    = { PCI173X_INT_DI1,  0x02, },
        .sdirq[2]    = { PCI173X_INT_IDI0, 0x00, },
        .sdirq[3]    = { PCI173X_INT_IDI1, 0x00, },
    },
    [TYPE_PCI1733] = {
        .name        = "pci1733",
        .nsubdevs    = 2,
        .sdi[1]      = { 32, 0x00, },
        .id_reg      = 0x04,
    },
    [TYPE_PCI1734] = {
        .name        = "pci1734",
        .nsubdevs    = 2,
        .sdo[1]      = { 32, 0x00, },
        .id_reg      = 0x04,
    },
    [TYPE_PCI1735] = {
        .name        = "pci1735",
        .nsubdevs    = 4,
        .sdi[0]      = { 32, 0x00, },
        .sdo[0]      = { 32, 0x00, },
        .id_reg      = 0x08,
        .timer_regbase  = 0x04,
    },
    [TYPE_PCI1736] = {
        .name        = "pci1736",
        .nsubdevs    = 3,
        .sdi[1]      = { 16, 0x00, },
        .sdo[1]      = { 16, 0x00, },
        .id_reg      = 0x04,
    },
    [TYPE_PCI1739] = {
        .name        = "pci1739",
        .nsubdevs    = 3,
        .sdio[0]     = { 2, 0x00, },
        .id_reg      = 0x08,
    },
    [TYPE_PCI1750] = {
        .name        = "pci1750",
        .nsubdevs    = 2,
        .sdi[1]      = { 16, 0x00, },
        .sdo[1]      = { 16, 0x00, },
    },
    [TYPE_PCI1751] = {
        .name        = "pci1751",
        .nsubdevs    = 3,
        .sdio[0]     = { 2, 0x00, },
        .timer_regbase  = 0x18,
    },
    [TYPE_PCI1752] = {
        .name        = "pci1752",
        .nsubdevs    = 3,
        .sdo[0]      = { 32, 0x00, },
        .sdo[1]      = { 32, 0x04, },
        .id_reg      = 0x10,
        .is_16bit    = 1,
    },
    [TYPE_PCI1753] = {
        .name        = "pci1753",
        .nsubdevs    = 4,
        .sdio[0]     = { 4, 0x00, },
    },
    [TYPE_PCI1753E] = {
        .name        = "pci1753e",
        .nsubdevs    = 8,
        .sdio[0]     = { 4, 0x00, },
        .sdio[1]     = { 4, 0x20, },
    },
    [TYPE_PCI1754] = {
        .name        = "pci1754",
        .nsubdevs    = 3,
        .sdi[0]      = { 32, 0x00, },
        .sdi[1]      = { 32, 0x04, },
        .id_reg      = 0x10,
        .is_16bit    = 1,
    },
    [TYPE_PCI1756] = {
        .name        = "pci1756",
        .nsubdevs    = 3,
        .sdi[1]      = { 32, 0x00, },
        .sdo[1]      = { 32, 0x04, },
        .id_reg      = 0x10,
        .is_16bit    = 1,
    },
    [TYPE_PCI1761] = {
        .name        = "pci1761",
        .nsubdevs    = 3,
        .sdi[1]      = { 8, 0x01 },
        .sdo[1]      = { 8, 0x00 },
        .id_reg      = 0x02,
    },
    [TYPE_PCI1762] = {
        .name        = "pci1762",
        .nsubdevs    = 3,
        .sdi[1]      = { 16, 0x02, },
        .sdo[1]      = { 16, 0x00, },
        .id_reg      = 0x04,
        .is_16bit    = 1,
    },
};

#define PCI_VENDOR_ID_ADVANTECH        0x13fe
#define PCI_CLASS_OTHERS               0xff

#define ADV_PCI_DIO_VENDOR_ID PCI_VENDOR_ID_ADVANTECH
#define ADV_PCI_DIO_DEVICE_ID 0x1730
#define ADV_PCI_DIO_CLASS_ID PCI_CLASS_OTHERS

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

    /* simple register/state model */
    uint8_t regs[0x20]; /* small I/O space used by driver */

    /* cached interrupt control and edge registers to match driver expectations */
    uint8_t int_en_reg;   /* PCI173X_INT_EN_REG */
    uint8_t int_rf_reg;   /* PCI173X_INT_RF_REG */
    uint8_t int_flag_reg; /* PCI173X_INT_FLAG_REG (RO in HW, SW writes go to CLR) */
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

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > s->mmio_backing_size || !s->mmio_backing) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_read OOB addr=%" PRIx64 " size=%u\n",
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        return 0;
    }

    uint64_t val = 0;
    memcpy(&val, s->mmio_backing + addr, size);
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > s->mmio_backing_size || !s->mmio_backing) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_write OOB addr=%" PRIx64 " size=%u\n",
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        return;
    }

    memcpy(s->mmio_backing + addr, &val, size);
}

static uint8_t pcibase_reg_read8(PCIBaseState *s, hwaddr addr)
{
    if (addr < sizeof(s->regs)) {
        return s->regs[addr];
    }
    return 0;
}

static void pcibase_reg_write8(PCIBaseState *s, hwaddr addr, uint8_t val)
{
    if (addr < sizeof(s->regs)) {
        s->regs[addr] = val;
    }
}

static uint16_t pcibase_reg_read16(PCIBaseState *s, hwaddr addr)
{
    uint16_t v = 0;
    v |= pcibase_reg_read8(s, addr);
    v |= ((uint16_t)pcibase_reg_read8(s, addr + 1)) << 8;
    return v;
}

static void pcibase_reg_write16(PCIBaseState *s, hwaddr addr, uint16_t val)
{
    pcibase_reg_write8(s, addr, val & 0xff);
    pcibase_reg_write8(s, addr + 1, (val >> 8) & 0xff);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* only offsets actually used by driver are meaningful */
    switch (size) {
    case 1:
        if (addr == PCI173X_INT_FLAG_REG) {
            return s->int_flag_reg;
        }
        if (addr == PCI173X_INT_EN_REG) {
            return s->int_en_reg;
        }
        if (addr == PCI173X_INT_RF_REG) {
            return s->int_rf_reg;
        }
        return pcibase_reg_read8(s, addr);
    case 2:
        return pcibase_reg_read16(s, addr);
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] pio_read bad size=%u addr=%" PRIx64 "\n",
                      TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return 0;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 1: {
        uint8_t v = (uint8_t)val;
        if (addr == PCI173X_INT_EN_REG) {
            s->int_en_reg = v;
            pcibase_reg_write8(s, addr, v);
            return;
        }
        if (addr == PCI173X_INT_RF_REG) {
            s->int_rf_reg = v;
            pcibase_reg_write8(s, addr, v);
            return;
        }
        if (addr == PCI173X_INT_CLR_REG) {
            /* clear requested interrupt flags */
            s->int_flag_reg &= ~v;
            return;
        }
        pcibase_reg_write8(s, addr, v);
        break;
    }
    case 2:
        pcibase_reg_write16(s, addr, (uint16_t)val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] pio_write bad size=%u addr=%" PRIx64 "\n",
                      TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        break;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    memset(s->regs, 0, sizeof(s->regs));
    s->int_en_reg = 0;
    s->int_rf_reg = 0;
    s->int_flag_reg = 0;

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  ADV_PCI_DIO_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ADV_PCI_DIO_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ADV_PCI_DIO_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR layout per driver: iobase is BAR2, BAR0 used for expansion test */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x40; /* enough for override test offset 53 */
    s->bar_info[0].name = "adv_pci_dio-bar0";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = sizeof(s->regs);
    s->bar_info[1].name = "adv_pci_dio-bar2";
    s->bar_info[1].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* no MSI/MSI-X; use legacy INTx only */
    s->has_msi = false;
    s->has_msix = false;

    /* allocate a trivial MMIO backing for completeness (unused by driver) */
    s->mmio_backing_size = 0x1000;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

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

