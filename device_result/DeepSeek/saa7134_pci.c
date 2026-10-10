/* QEMU device model for Philips SAA7134 PCI TV card (Phase 2 implementation) */

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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "saa7134_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define SAA7134_VENDOR_ID 0x1131
#define SAA7134_DEVICE_ID 0x7134
#define SAA7134_CLASS_ID   PCI_CLASS_MULTIMEDIA_VIDEO

/* Register Offsets (single MMIO BAR, byte offsets) */
#define SAA7134_REGION_ENABLE      0x004
#define SAA7134_GPIO_GPRESCAN      0x080
#define SAA7134_I2C_ATTR_STATUS    0x180
#define SAA7134_I2C_DATA           0x181
#define SAA7134_VIDEO_PORT_CTRL0   0x190
#define SAA7134_VIDEO_PORT_CTRL1   0x191
#define SAA7134_VIDEO_PORT_CTRL2   0x192
#define SAA7134_VIDEO_PORT_CTRL3   0x193
#define SAA7134_VIDEO_PORT_CTRL4   0x194
#define SAA7134_VIDEO_PORT_CTRL5   0x195
#define SAA7134_VIDEO_PORT_CTRL6   0x196
#define SAA7134_VIDEO_PORT_CTRL7   0x197
#define SAA7134_VIDEO_PORT_CTRL8   0x198
#define SAA7134_GPIO_GPMODE0       0x1B0
#define SAA7134_GPIO_GPMODE1       0x1B1
#define SAA7134_GPIO_GPMODE3       0x1B3
#define SAA7134_GPIO_GPSTATUS0     0x1B4
#define SAA7134_GPIO_GPSTATUS1     0x1B5
#define SAA7134_GPIO_GPSTATUS2     0x1B6
#define SAA7134_GPIO_GPSTATUS3     0x1B7
#define SAA7134_RS_BA1_BASE        0x200
#define SAA7134_RS_BA2_BASE        0x204
#define SAA7134_RS_PITCH_BASE      0x208
#define SAA7134_RS_CONTROL_BASE    0x20c
#define SAA7134_FIFO_SIZE          0x2a0
#define SAA7134_THRESHOULD         0x2a4
#define SAA7134_MAIN_CTRL          0x2a8
#define SAA7134_IRQ1               0x2c4
#define SAA7134_IRQ2               0x2c8
#define SAA7134_IRQ_REPORT         0x2cc
#define SAA7134_IRQ_STATUS         0x2d0
#define SAA7133_ANALOG_IO_SELECT   0x594
#define SAA7134_TS_PARALLEL        0x1a0
#define SAA7134_TS_PARALLEL_SERIAL 0x1a1
#define SAA7134_TS_SERIAL0         0x1a2
#define SAA7134_TS_SERIAL1         0x1a3
#define SAA7134_TS_DMA0            0x1a4
#define SAA7134_TS_DMA1            0x1a5
#define SAA7134_TS_DMA2            0x1a6

/* Bit definitions for MAIN_CTRL */
#define SAA7134_MAIN_CTRL_TE0    (1 << 0)
#define SAA7134_MAIN_CTRL_TE1    (1 << 1)
#define SAA7134_MAIN_CTRL_TE2    (1 << 2)
#define SAA7134_MAIN_CTRL_TE3    (1 << 3)
#define SAA7134_MAIN_CTRL_TE4    (1 << 4)
#define SAA7134_MAIN_CTRL_TE5    (1 << 5)
#define SAA7134_MAIN_CTRL_TE6    (1 << 6)

/* Interrupt enable bits (IRQ1) */
#define SAA7134_IRQ1_INTE_RA0_0   (1 <<  0)
#define SAA7134_IRQ1_INTE_RA0_1   (1 <<  1)
#define SAA7134_IRQ1_INTE_RA0_4   (1 <<  4)
#define SAA7134_IRQ1_INTE_RA0_5   (1 <<  5)
#define SAA7134_IRQ1_INTE_RA0_6   (1 <<  6)
#define SAA7134_IRQ1_INTE_RA0_7   (1 <<  7)
#define SAA7134_IRQ1_INTE_RA2_0   (1 << 16)
#define SAA7134_IRQ1_INTE_RA2_1   (1 << 17)
#define SAA7134_IRQ1_INTE_RA3_0   (1 << 24)
#define SAA7134_IRQ1_INTE_RA3_1   (1 << 25)

/* Interrupt enable bits (IRQ2) */
#define SAA7134_IRQ2_INTE_AR      (1 <<  0)
#define SAA7134_IRQ2_INTE_PE      (1 <<  1)
#define SAA7134_IRQ2_INTE_DEC0    (1 <<  2)
#define SAA7134_IRQ2_INTE_DEC1    (1 <<  3)
#define SAA7134_IRQ2_INTE_DEC2    (1 <<  4)
#define SAA7134_IRQ2_INTE_DEC3    (1 <<  5)
#define SAA7134_IRQ2_INTE_GPIO16_P (1 << 10)
#define SAA7134_IRQ2_INTE_GPIO16_N (1 << 11)
#define SAA7134_IRQ2_INTE_GPIO18_P (1 << 12)
#define SAA7134_IRQ2_INTE_GPIO18_N (1 << 13)

/* Interrupt report bits */
#define SAA7134_IRQ_REPORT_DONE_RA0 (1 <<  0)
#define SAA7134_IRQ_REPORT_DONE_RA2 (1 <<  2)
#define SAA7134_IRQ_REPORT_DONE_RA3 (1 <<  3)
#define SAA7134_IRQ_REPORT_PE       (1 <<  5)
#define SAA7134_IRQ_REPORT_RDCAP    (1 <<  7)
#define SAA7134_IRQ_REPORT_INTL     (1 <<  8)
#define SAA7134_IRQ_REPORT_GPIO16   (1 << 14)
#define SAA7134_IRQ_REPORT_GPIO18   (1 << 15)

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    uint32_t lmmio[0x600];   /* 32-bit registers */
    uint8_t  bmmio[0x200];   /* 8-bit registers */

    uint32_t status;
    bool hw_initialized;
    uint8_t power_state;
};

/* Forward declarations */
static uint64_t saa7134_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void saa7134_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static const MemoryRegionOps saa7134_mmio_ops;

/* IRQ update logic: raise/lower IRQ line based on IRQ_REPORT */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t irq_report = s->lmmio[SAA7134_IRQ_REPORT >> 2];
    if (irq_report != 0) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* DMA logic not used - driver does bus mastering but we don't emulate actual transfers */
/* Unused generic MMIO ops (kept for reference, now stubs) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Unused PIO stubs */
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

/* Combined MMIO handler for the single BAR */
static uint64_t saa7134_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t index = addr >> 2;
    uint32_t byte_offset = addr & 3;

    if (addr >= 0x1000) {
        return ~0ULL;
    }

    switch (size) {
    case 4:
        val = s->lmmio[index];
        break;
    case 2:
        val = (s->lmmio[index] >> (byte_offset * 8)) & 0xffff;
        break;
    case 1:
        val = (s->lmmio[index] >> (byte_offset * 8)) & 0xff;
        break;
    default:
        break;
    }
    return val;
}

static void saa7134_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr >> 2;
    uint32_t byte_offset = addr & 3;
    uint32_t mask;

    if (addr >= 0x1000) {
        return;
    }

    switch (addr) {
    case SAA7134_IRQ_REPORT:
        if (size == 4) {
            s->lmmio[index] &= ~((uint32_t)val);
        } else if (size == 2) {
            mask = 0xffff << (byte_offset * 8);
            s->lmmio[index] &= ~((val & 0xffff) << (byte_offset * 8));
        } else if (size == 1) {
            mask = 0xff << (byte_offset * 8);
            s->lmmio[index] &= ~((val & 0xff) << (byte_offset * 8));
        }
        pcibase_update_irq(s);
        break;
    case SAA7134_IRQ_STATUS:
        /* Read-only, ignore writes */
        break;
    default:
        switch (size) {
        case 4:
            s->lmmio[index] = (uint32_t)val;
            break;
        case 2:
            mask = 0xffff << (byte_offset * 8);
            s->lmmio[index] = (s->lmmio[index] & ~mask) | ((val & 0xffff) << (byte_offset * 8));
            break;
        case 1:
            mask = 0xff << (byte_offset * 8);
            s->lmmio[index] = (s->lmmio[index] & ~mask) | ((val & 0xff) << (byte_offset * 8));
            break;
        default:
            break;
        }
        break;
    }
    pcibase_update_irq(s);
}

static const MemoryRegionOps saa7134_mmio_ops = {
    .read = saa7134_mmio_read,
    .write = saa7134_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->lmmio, 0, sizeof(s->lmmio));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  SAA7134_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SAA7134_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SAA7134_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &saa7134_mmio_ops, s,
                          "saa7134-mmio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);
    s->num_bars = 0;
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
    .name = "saa7134_pci",
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
