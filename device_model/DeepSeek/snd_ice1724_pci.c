/*
 * QEMU virtual device model for VIA VT1724 (ICE1724) audio controller.
 * Phase 4: Debug & Update - Fixed I2C protocol with busy timer and write-then-read sequence.
 * Set EEPROM size to 32 bytes to pass driver validation.
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

#define TYPE_PCIBASE_DEVICE "snd_ice1724_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define PCI_VENDOR_ID_ICE           0x1412
#define PCI_DEVICE_ID_VT1724        0x1724
#define PCI_CLASS_MULTIMEDIA_AUDIO  0x040100

/* Main control register offsets (BAR0) */
#define VT1724_REG_CONTROL          0x00
#define VT1724_REG_IRQMASK          0x01
#define VT1724_REG_IRQSTAT          0x02
#define VT1724_REG_SYS_CFG          0x04
#define VT1724_REG_AC97_CFG         0x05
#define VT1724_REG_I2S_FEATURES     0x06
#define VT1724_REG_SPDIF_CFG        0x07
#define VT1724_REG_GPIO_DIRECTION   0x08
#define VT1724_REG_GPIO_WRITE_MASK  0x0C
#define VT1724_REG_GPIO_WRITE_MASK_22 0x0E
#define VT1724_REG_I2C_BYTE_ADDR    0x10
#define VT1724_REG_I2C_DATA         0x11
#define VT1724_REG_I2C_DEV_ADDR     0x12
#define VT1724_REG_I2C_CTRL         0x13
#define VT1724_REG_GPIO_DATA        0x14
#define VT1724_REG_GPIO_DATA_22     0x16
#define VT1724_REG_MPU_DATA         0x18
#define VT1724_REG_MPU_TXFIFO       0x19
#define VT1724_REG_MPU_RXFIFO       0x1A
#define VT1724_REG_MPU_CTRL         0x1B
#define VT1724_REG_POWERDOWN        0x1C
#define VT1724_REG_MPU_FIFO_WM      0x1D

/* Multi-track register offsets (BAR1) */
#define VT1724_MT_IRQ               0x00
#define VT1724_MT_DMA_CONTROL       0x01
#define VT1724_MT_DMA_PAUSE         0x02
#define VT1724_MT_DMA_INT_MASK      0x03
#define VT1724_MT_RATE              0x04
#define VT1724_MT_BURST             0x08
#define VT1724_MT_I2S_FORMAT        0x0C
#define VT1724_MT_PLAYBACK_ADDR     0x10
#define VT1724_MT_PLAYBACK_SIZE     0x14
#define VT1724_MT_PLAYBACK_COUNT    0x1C
#define VT1724_MT_DMA_FIFO_ERR      0x1A
#define VT1724_MT_CAPTURE_ADDR      0x20
#define VT1724_MT_CAPTURE_SIZE      0x24
#define VT1724_MT_CAPTURE_COUNT     0x26
#define VT1724_MT_AC97_CMD          0x30
#define VT1724_MT_AC97_INDEX        0x31
#define VT1724_MT_AC97_DATA         0x32
#define VT1724_MT_SPDIF_CTRL        0x34
#define VT1724_MT_ROUTE_PLAYBACK    0x48
#define VT1724_MT_PDMA4_ADDR        0x40
#define VT1724_MT_PDMA4_SIZE        0x44
#define VT1724_MT_PDMA4_COUNT       0x46
#define VT1724_MT_RDMA1_ADDR        0x30
#define VT1724_MT_RDMA1_SIZE        0x34
#define VT1724_MT_RDMA1_COUNT       0x36
#define VT1724_MT_PDMA3_ADDR        0x50
#define VT1724_MT_PDMA3_SIZE        0x54
#define VT1724_MT_PDMA3_COUNT       0x56
#define VT1724_MT_PDMA2_ADDR        0x60
#define VT1724_MT_PDMA2_SIZE        0x64
#define VT1724_MT_PDMA2_COUNT       0x66
#define VT1724_MT_PDMA1_ADDR        0x70
#define VT1724_MT_PDMA1_SIZE        0x74
#define VT1724_MT_PDMA1_COUNT       0x76
#define VT1724_MT_MONITOR_PEAKINDEX 0x84
#define VT1724_MT_MONITOR_PEAKDATA  0x85

/* DMA control bits */
#define VT1724_PDMA0_START          0x01
#define VT1724_PDMA3_START          0x40
#define VT1724_RDMA1_START          0x04
#define VT1724_PDMA2_START          0x20
#define VT1724_PDMA4_START          0x80
#define VT1724_RDMA0_START          0x02
#define VT1724_PDMA1_START          0x10
#define VT1724_PDMA2_PAUSE          0x20
#define VT1724_PDMA0_PAUSE          0x01
#define VT1724_RDMA1_PAUSE          0x04
#define VT1724_PDMA3_PAUSE          0x40
#define VT1724_PDMA1_PAUSE          0x10
#define VT1724_RDMA0_PAUSE          0x02
#define VT1724_PDMA4_PAUSE          0x80

#define DMA_STARTS  (VT1724_RDMA0_START|VT1724_PDMA0_START|VT1724_RDMA1_START|\
    VT1724_PDMA1_START|VT1724_PDMA2_START|VT1724_PDMA3_START|VT1724_PDMA4_START)
#define DMA_PAUSES  (VT1724_RDMA0_PAUSE|VT1724_PDMA0_PAUSE|VT1724_RDMA1_PAUSE|\
    VT1724_PDMA1_PAUSE|VT1724_PDMA2_PAUSE|VT1724_PDMA3_PAUSE|VT1724_PDMA4_PAUSE)

/* Interrupt masks */
#define VT1724_IRQ_MPU_TX           0x20
#define VT1724_MPU_TX_EMPTY         0x02
#define VT1724_IRQ_MPU_RX           0x80
#define VT1724_IRQ_MTPCM            0x10

/* Multi-track DMA status bits */
#define VT1724_MULTI_RDMA1          0x04
#define VT1724_MULTI_FIFO_ERR       0x08
#define VT1724_MULTI_PDMA4          0x80
#define VT1724_MULTI_PDMA0          0x01
#define VT1724_MULTI_PDMA3          0x40
#define VT1724_MULTI_PDMA1          0x10
#define VT1724_MULTI_PDMA2          0x20
#define VT1724_MULTI_RDMA0          0x02

/* AC97 and other control bits */
#define VT1724_SPDIF_MASTER         0x10
#define VT1724_AC97_WRITE           0x20
#define VT1724_AC97_READY           0x08
#define VT1724_AC97_READ            0x10
#define VT1724_AC97_ID_MASK         0x03
#define VT1724_MT_I2S_MCLK_128X     0x08
#define VT1724_CFG_PRO_I2S          0x80
#define VT1724_CFG_ADC_NONE         0x0c
#define VT1724_CFG_ADC_MASK         0x0c
#define VT1724_CFG_SPDIF_OUT_EN     0x80
#define VT1724_CFG_SPDIF_OUT_INT    0x40
#define VT1724_CFG_SPDIF_IN         0x02
#define VT1724_I2C_BUSY             0x01
#define VT1724_I2C_WRITE            0x01
#define VT1724_I2C_EEPROM           0x80
#define VT1724_RESET                0x80
#define VT1724_MPU_RX_FIFO          0x20
#define VT1724_CFG_MPU401           0x20
#define VT1724_MPU_UART             0x01

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_PIO,
    BAR_TYPE_MMIO,
    BAR_TYPE_RAM
} BARType;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar0_mr;
    MemoryRegion bar1_mr;
    uint8_t bar0[0x100];
    uint8_t bar1[0x100];

    /* I2C state machine */
    uint8_t eeprom[32];
    uint8_t i2c_dev_addr;    /* device address */
    bool i2c_dir;            /* direction: 0=write, 1=read */
    uint8_t i2c_mem_addr;    /* memory address for read */
    bool i2c_busy;           /* I2C busy flag */
    QEMUTimer i2c_timer;     /* timer to complete transaction */

    /* General state */
    uint8_t irq_status;
};

static void i2c_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->i2c_busy = false;
}

static uint64_t bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case VT1724_REG_IRQSTAT:
        val = s->irq_status;
        break;
    case VT1724_REG_I2C_CTRL:
        val = VT1724_I2C_EEPROM;
        if (s->i2c_busy) {
            val |= VT1724_I2C_BUSY;
        }
        break;
    case VT1724_REG_I2C_DATA:
        if (!s->i2c_busy && s->i2c_dir) { /* read direction and not busy */
            val = s->eeprom[s->i2c_mem_addr & 31];
            s->i2c_mem_addr++; /* auto-increment for multi-byte read */
        } else {
            val = 0xFF; /* bus pull-up */
        }
        break;
    default:
        if (addr + size <= sizeof(s->bar0)) {
            for (unsigned i = 0; i < size; i++) {
                val |= (uint64_t)s->bar0[addr + i] << (i * 8);
            }
        }
        break;
    }
    return val;
}

static void bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case VT1724_REG_IRQSTAT:
        s->irq_status &= ~val;
        break;
    case VT1724_REG_I2C_BYTE_ADDR:
        s->i2c_mem_addr = val;
        // fall through to store in backing array
        if (addr + size <= sizeof(s->bar0)) {
            for (unsigned i = 0; i < size; i++) {
                s->bar0[addr + i] = (val >> (i * 8)) & 0xff;
            }
        }
        break;
    case VT1724_REG_I2C_DEV_ADDR:
        s->i2c_dir = val & 1;
        s->i2c_dev_addr = val >> 1;
        if (addr + size <= sizeof(s->bar0)) {
            for (unsigned i = 0; i < size; i++) {
                s->bar0[addr + i] = (val >> (i * 8)) & 0xff;
            }
        }
        break;
    case VT1724_REG_I2C_DATA:
        /* If currently in a write transaction, capture memory address */
        if (s->i2c_busy && !s->i2c_dir) {
            s->i2c_mem_addr = val;
        }
        if (addr + size <= sizeof(s->bar0)) {
            for (unsigned i = 0; i < size; i++) {
                s->bar0[addr + i] = (val >> (i * 8)) & 0xff;
            }
        }
        break;
    case VT1724_REG_I2C_CTRL:
        if (val & 0x02) { /* start bit */
            s->i2c_busy = true;
            /* Complete the transaction after a short virtual delay */
            timer_mod_ns(&s->i2c_timer,
                         qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000); /* 1 us */
        }
        if (addr + size <= sizeof(s->bar0)) {
            for (unsigned i = 0; i < size; i++) {
                s->bar0[addr + i] = (val >> (i * 8)) & 0xff;
            }
        }
        break;
    default:
        if (addr + size <= sizeof(s->bar0)) {
            for (unsigned i = 0; i < size; i++) {
                s->bar0[addr + i] = (val >> (i * 8)) & 0xff;
            }
        }
        break;
    }
}

static const MemoryRegionOps bar0_ops = {
    .read = bar0_read,
    .write = bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static uint64_t bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case VT1724_MT_IRQ:
        val = s->bar1[addr]; /* read-modify-write via write handler */
        break;
    case VT1724_MT_DMA_FIFO_ERR:
        val = s->bar1[addr];
        break;
    default:
        if (addr + size <= sizeof(s->bar1)) {
            for (unsigned i = 0; i < size; i++) {
                val |= (uint64_t)s->bar1[addr + i] << (i * 8);
            }
        }
        break;
    }
    return val;
}

static void bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case VT1724_MT_IRQ:
        s->bar1[addr] &= ~val; /* W1C */
        break;
    case VT1724_MT_DMA_FIFO_ERR:
        s->bar1[addr] &= ~val;
        break;
    default:
        if (addr + size <= sizeof(s->bar1)) {
            for (unsigned i = 0; i < size; i++) {
                s->bar1[addr + i] = (val >> (i * 8)) & 0xff;
            }
        }
        break;
    }
}

static const MemoryRegionOps bar1_ops = {
    .read = bar1_read,
    .write = bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->bar0, 0, sizeof(s->bar0));
    memset(s->bar1, 0, sizeof(s->bar1));
    s->i2c_dev_addr = 0;
    s->i2c_dir = 0;
    s->i2c_mem_addr = 0;
    s->i2c_busy = false;
    s->irq_status = 0;

    /* Initialize EEPROM with valid size (32 bytes) */
    memset(s->eeprom, 0, sizeof(s->eeprom));
    s->eeprom[0] = 0x20;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ICE);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_VT1724);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_ICE);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, PCI_DEVICE_ID_VT1724);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0: Control registers, MMIO, 256 bytes */
    memory_region_init_io(&s->bar0_mr, OBJECT(s), &bar0_ops, s, "bar0-ctrl", 0x100);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0_mr);

    /* BAR1: Multi-track registers, MMIO, 256 bytes */
    memory_region_init_io(&s->bar1_mr, OBJECT(s), &bar1_ops, s, "bar1-mt", 0x100);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar1_mr);

    /* I2C completion timer */
    timer_init_ns(&s->i2c_timer, QEMU_CLOCK_VIRTUAL, i2c_timer_cb, s);

    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(&s->i2c_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* No additional cleanup needed */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_ice1724_pci",
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
