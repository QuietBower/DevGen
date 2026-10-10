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

#define TYPE_PCIBASE_DEVICE "adv_pci_dio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ADVANTECH 0x13fe
#define PCI_DEVICE_ID_PCI1730   0x1730
#define PCI_CLASS_DIO           0xff  /* PCI_CLASS_OTHERS */

/* Register offsets for PCI1730 */
#define PCI1730_ISO_PORT        0x00
#define PCI1730_DI_DO_PORT      0x02
#define PCI1730_ID_REG          0x04
#define PCI1730_INT_EN_REG      0x08
#define PCI1730_INT_RF_REG      0x0c
#define PCI1730_INT_FLAG_REG    0x10
#define PCI1730_INT_CLR_REG     0x10

/* Interrupt flag bits for PCI1730 */
#define PCI173X_INT_IDI0 0x01
#define PCI173X_INT_IDI1 0x02
#define PCI173X_INT_DI0  0x04
#define PCI173X_INT_DI1  0x08

/* Board type enum copied from driver */
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

/* Subdevice data structures from driver */
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
    struct diosubd_data sdi[2];
    struct diosubd_data sdo[2];
    struct diosubd_data sdio[2];
    struct dio_irq_subd_data sdirq[4];
    unsigned long id_reg;
    unsigned long timer_regbase;
    unsigned int is_16bit:1;
};

/* Static board types array (only first entry used) */
static const struct dio_boardtype boardtypes[] = {
    [TYPE_PCI1730] = {
        .name       = "pci1730",
        .nsubdevs   = 9,
        .sdi[0]     = { 16, 0x02, },
        .sdi[1]     = { 16, 0x00, },
        .sdo[0]     = { 16, 0x02, },
        .sdo[1]     = { 16, 0x00, },
        .id_reg     = 0x04,
        .sdirq[0]   = { PCI173X_INT_DI0,  0x02, },
        .sdirq[1]   = { PCI173X_INT_DI1,  0x02, },
        .sdirq[2]   = { PCI173X_INT_IDI0, 0x00, },
        .sdirq[3]   = { PCI173X_INT_IDI1, 0x00, },
    },
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

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint8_t regs[256];
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->regs, 0, sizeof(s->regs));
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds read at addr 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return val;
    }
    memcpy(&val, &s->regs[addr], size);
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds write at addr 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return;
    }
    memcpy(&s->regs[addr], &val, size);
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_mmio_ops = {
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ADVANTECH);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_PCI1730);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "bar2";
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
    .name = "adv_pci_dio_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_BUFFER(regs, PCIBaseState),
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