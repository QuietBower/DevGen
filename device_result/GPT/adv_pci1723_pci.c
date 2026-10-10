/*
 * QEMU PCI device model for Advantech PCI-1723 based on Linux comedi driver
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

#define TYPE_PCIBASE_DEVICE "adv_pci1723_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_ADVANTECH
#define PCI_VENDOR_ID_ADVANTECH 0x13fe
#endif

#define PCI1723_VENDOR_ID        PCI_VENDOR_ID_ADVANTECH
#define PCI1723_DEVICE_ID        0x1723
#define PCI1723_CLASS_ID         PCI_CLASS_OTHERS

#define PCI1723_AO_REG(x)               (0x00 + ((x) * 2))
#define PCI1723_BOARD_ID_REG            0x10
#define PCI1723_BOARD_ID_MASK           (0xf << 0)
#define PCI1723_SYNC_CTRL_REG           0x12
#define PCI1723_SYNC_CTRL(x)            (((x) & 0x1) << 0)
#define PCI1723_SYNC_CTRL_ASYNC         PCI1723_SYNC_CTRL(0)
#define PCI1723_SYNC_CTRL_SYNC          PCI1723_SYNC_CTRL(1)
#define PCI1723_CTRL_REG                0x14
#define PCI1723_CTRL_BUSY               (1U << 15)
#define PCI1723_CTRL_INIT               (1U << 14)
#define PCI1723_CTRL_SELF               (1U << 8)
#define PCI1723_CTRL_IDX(x)             (((x) & 0x3) << 6)
#define PCI1723_CTRL_RANGE(x)           (((x) & 0x3) << 4)
#define PCI1723_CTRL_SEL(x)             (((x) & 0x1) << 3)
#define PCI1723_CTRL_GAIN               PCI1723_CTRL_SEL(0)
#define PCI1723_CTRL_OFFSET             PCI1723_CTRL_SEL(1)
#define PCI1723_CTRL_CHAN(x)            (((x) & 0x7) << 0)
#define PCI1723_CALIB_CTRL_REG          0x16
#define PCI1723_CALIB_CTRL_CS           (1U << 2)
#define PCI1723_CALIB_CTRL_DAT          (1U << 1)
#define PCI1723_CALIB_CTRL_CLK          (1U << 0)
#define PCI1723_CALIB_STROBE_REG        0x18
#define PCI1723_DIO_CTRL_REG            0x1a
#define PCI1723_DIO_CTRL_HDIO           (1U << 1)
#define PCI1723_DIO_CTRL_LDIO           (1U << 0)
#define PCI1723_DIO_DATA_REG            0x1c
#define PCI1723_CALIB_DATA_REG          0x1e
#define PCI1723_SYNC_STROBE_REG         0x20
#define PCI1723_RESET_AO_STROBE_REG     0x22
#define PCI1723_RESET_CALIB_STROBE_REG  0x24
#define PCI1723_RANGE_STROBE_REG        0x26
#define PCI1723_VREF_REG                0x28
#define PCI1723_VREF(x)                 (((x) & 0x3) << 0)
#define PCI1723_VREF_NEG10V             PCI1723_VREF(0)
#define PCI1723_VREF_0V                 PCI1723_VREF(1)
#define PCI1723_VREF_POS10V             PCI1723_VREF(3)

/* BAR selection based on driver: dev->iobase = pci_resource_start(pcidev, 2); */
#define PCI1723_IO_BAR_INDEX 2

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t ao_reg[8];
    uint16_t board_id_reg;
    uint16_t sync_ctrl_reg;
    uint16_t ctrl_reg;
    uint16_t calib_ctrl_reg;
    uint16_t calib_strobe_reg;
    uint16_t dio_ctrl_reg;
    uint16_t dio_data_reg;
    uint16_t calib_data_reg;
    uint16_t sync_strobe_reg;
    uint16_t reset_ao_strobe_reg;
    uint16_t reset_calib_strobe_reg;
    uint16_t range_strobe_reg;
    uint16_t vref_reg;
};

/* No interrupt logic is used by the driver, keep helper empty */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)pdev;
}

/* No DMA logic is used by the driver */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)pdev;
    (void)is_write;
}

/* I/O space access handlers: driver uses inw/outw on dev->iobase */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Driver uses 16-bit inw/outw only */
    if (size != 2) {
        return 0xffff;
    }

    switch (addr) {
    default:
        /* AO registers 0x00..0x0e (8 channels) */
        if (addr <= PCI1723_AO_REG(7) && (addr % 2) == 0) {
            unsigned chan = (addr - PCI1723_AO_REG(0)) / 2;
            if (chan < 8) {
                val = s->ao_reg[chan];
            }
        } else if (addr == PCI1723_BOARD_ID_REG) {
            val = s->board_id_reg;
        } else if (addr == PCI1723_SYNC_CTRL_REG) {
            val = s->sync_ctrl_reg;
        } else if (addr == PCI1723_CTRL_REG) {
            val = s->ctrl_reg;
        } else if (addr == PCI1723_CALIB_CTRL_REG) {
            val = s->calib_ctrl_reg;
        } else if (addr == PCI1723_CALIB_STROBE_REG) {
            val = s->calib_strobe_reg;
        } else if (addr == PCI1723_DIO_CTRL_REG) {
            val = s->dio_ctrl_reg;
        } else if (addr == PCI1723_DIO_DATA_REG) {
            val = s->dio_data_reg;
        } else if (addr == PCI1723_CALIB_DATA_REG) {
            val = s->calib_data_reg;
        } else if (addr == PCI1723_SYNC_STROBE_REG) {
            val = s->sync_strobe_reg;
        } else if (addr == PCI1723_RESET_AO_STROBE_REG) {
            val = s->reset_ao_strobe_reg;
        } else if (addr == PCI1723_RESET_CALIB_STROBE_REG) {
            val = s->reset_calib_strobe_reg;
        } else if (addr == PCI1723_RANGE_STROBE_REG) {
            val = s->range_strobe_reg;
        } else if (addr == PCI1723_VREF_REG) {
            val = s->vref_reg;
        } else {
            /* unmapped register: return all-ones, typical for I/O */
            val = 0xffff;
        }
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 2) {
        return;
    }

    uint16_t wval = (uint16_t)val;

    switch (addr) {
    default:
        /* AO channels */
        if (addr <= PCI1723_AO_REG(7) && (addr % 2) == 0) {
            unsigned chan = (addr - PCI1723_AO_REG(0)) / 2;
            if (chan < 8) {
                s->ao_reg[chan] = wval;
            }
        } else if (addr == PCI1723_BOARD_ID_REG) {
            s->board_id_reg = wval & PCI1723_BOARD_ID_MASK;
        } else if (addr == PCI1723_SYNC_CTRL_REG) {
            s->sync_ctrl_reg = wval & PCI1723_SYNC_CTRL(1);
        } else if (addr == PCI1723_CTRL_REG) {
            s->ctrl_reg = wval;
        } else if (addr == PCI1723_CALIB_CTRL_REG) {
            s->calib_ctrl_reg = wval;
        } else if (addr == PCI1723_CALIB_STROBE_REG) {
            s->calib_strobe_reg = wval;
        } else if (addr == PCI1723_DIO_CTRL_REG) {
            /* Only two bits are defined in driver headers */
            s->dio_ctrl_reg = wval & (PCI1723_DIO_CTRL_HDIO | PCI1723_DIO_CTRL_LDIO);
        } else if (addr == PCI1723_DIO_DATA_REG) {
            /* DIO state is fully software visible; store shadow */
            s->dio_data_reg = wval;
        } else if (addr == PCI1723_CALIB_DATA_REG) {
            s->calib_data_reg = wval;
        } else if (addr == PCI1723_SYNC_STROBE_REG) {
            s->sync_strobe_reg = wval;
        } else if (addr == PCI1723_RESET_AO_STROBE_REG) {
            s->reset_ao_strobe_reg = wval;
        } else if (addr == PCI1723_RESET_CALIB_STROBE_REG) {
            s->reset_calib_strobe_reg = wval;
        } else if (addr == PCI1723_RANGE_STROBE_REG) {
            s->range_strobe_reg = wval;
        } else if (addr == PCI1723_VREF_REG) {
            s->vref_reg = wval;
        }
        break;
    }
}

/* No MMIO BAR is used by the driver; we keep dummy handlers to satisfy QEMU */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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

    memset(s->ao_reg, 0, sizeof(s->ao_reg));
    s->board_id_reg = 0;
    s->sync_ctrl_reg = 0;
    s->ctrl_reg = 0;
    s->calib_ctrl_reg = 0;
    s->calib_strobe_reg = 0;
    /* Default: all DIO bits output (both bits cleared) so matches typical power-on,
     * but driver does not rely on specific reset value beyond reading then updating. */
    s->dio_ctrl_reg = 0;
    s->dio_data_reg = 0;
    s->calib_data_reg = 0;
    s->sync_strobe_reg = 0;
    s->reset_ao_strobe_reg = 0;
    s->reset_calib_strobe_reg = 0;
    s->range_strobe_reg = 0;
    s->vref_reg = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                 BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size ? bi->size : 1);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, aligned_size);
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI1723_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI1723_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI1723_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Present as conventional PCI; no MSI/MSI-X used by driver */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: only BAR2 is used (I/O space) */
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    s->bar_info[0].index = PCI1723_IO_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_PIO;
    /* Driver uses offsets up to at least 0x28; choose minimal sufficient size */
    s->bar_info[0].size = 0x40;
    s->bar_info[0].name = "adv_pci1723-io";

    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    s->has_msi = false;
    s->has_msix = false;

    memset(s->ao_reg, 0, sizeof(s->ao_reg));
    s->board_id_reg = 0;
    s->sync_ctrl_reg = 0;
    s->ctrl_reg = 0;
    s->calib_ctrl_reg = 0;
    s->calib_strobe_reg = 0;
    s->dio_ctrl_reg = 0;
    s->dio_data_reg = 0;
    s->calib_data_reg = 0;
    s->sync_strobe_reg = 0;
    s->reset_ao_strobe_reg = 0;
    s->reset_calib_strobe_reg = 0;
    s->range_strobe_reg = 0;
    s->vref_reg = 0;
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
    .name = "adv_pci1723_pci",
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
