#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "dt3155_pci"

typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ------------------------------------------------------------------ */
/* Device-specific macros from dt3155 driver                          */
/* ------------------------------------------------------------------ */

#define DT3155_DEVICE_ID 0x1223
#define DT3155_NAME "dt3155"

#define DT3155_NEW_CYCLE        0x01000000
#define DT3155_IIC_CSR2         0x64
#define DT3155_DIRECT_ABORT     0x00000200
#define DT3155_IIC_READ         0x01010000
#define DT3155_IIC_CSR1         0x60
#define DT3155_IIC_WRITE        0x01000000
#define DT3155_FLD_END_ODD      0x00000002
#define DT3155_CAP_CONT_ODD     0x00000002
#define DT3155_CAP_CONT_EVEN    0x00000001
#define DT3155_FLD_CRPT_EVEN    0x00000100
#define DT3155_SRST             0x00000040
#define DT3155_EVEN_DMA_STRIDE  0x18
#define DT3155_CSR_DONE         0x01
#define DT3155_INT_CSR          0x48
#define DT3155_EVEN_DMA_START   0x00
#define DT3155_CSR2             0x10
#define DT3155_FLD_START        0x00000004
#define DT3155_EVEN_CSR         0x11
#define DT3155_FLD_DN_EVEN      0x00000010
#define DT3155_FLD_END_ODD_EN   0x00000200
#define DT3155_FLD_DN_ODD       0x00000020
#define DT3155_CSR1             0x40
#define DT3155_ODD_DMA_STRIDE   0x24
#define DT3155_FLD_START_EN     0x00000400
#define DT3155_ODD_DMA_START    0x0C
#define DT3155_CSR_ERROR        0x04
#define DT3155_FLD_CRPT_ODD     0x00000200
#define DT3155_CONFIG           0x13
#define DT3155_ODD_CSR          0x12
#define DT3155_FLD_END_EVEN     0x00000001
#define DT3155_BUSY_ODD         0x20
#define DT3155_BUSY_EVEN        0x10
#define DT3155_FIFO_EN          0x00000080
#define DT3155_VT_60HZ          0x00
#define DT3155_VT_50HZ          0x04
#define DT3155_SYNC_LVL_3       0x08
#define DT3155_AD_CMD_REG       0x00
#define DT3155_AD_CMD           0x32
#define DT3155_AD_ADDR          0x30
#define DT3155_PM_LUT_ADDR      0x50
#define DT3155_PM_LUT_DATA      0x51
#define DT3155_EVEN_FLD_MASK    0x4C
#define DT3155_AD_NEG_REF       0x02
#define DT3155_AD_LUT           0x31
#define DT3155_ADDR_ERR_EVEN    0x00000400
#define DT3155_FIFO_FLAG_CNT    0x58
#define DT3155_DT_ID            0x1F
#define DT3155_VIDEO_CNL_1      0x00
#define DT3155_EVEN_PIXEL_FMT   0x30
#define DT3155_DT3155_ID        0x20
#define DT3155_MASK_LENGTH      0x54
#define DT3155_PM_LUT_SEL       0x40
#define DT3155_SYNC_CNL_1       0x00
#define DT3155_ODD_FLD_MASK     0x50
#define DT3155_XFER_MODE        0x3C
#define DT3155_PM_LUT_PGM       0x80
#define DT3155_RETRY_WAIT_CNT   0x44
#define DT3155_ADDR_ERR_ODD     0x00000800
#define DT3155_ODD_PIXEL_FMT    0x34
#define DT3155_AD_POS_REF       0x01
#define DT3155_FIFO_TRIGGER     0x38
#define DT3155_IIC_CLK_DUR      0x5C
#define DT3155_ACQ_MODE_EVEN    0x00

/* shadows used in struct dt3155_priv in driver */
#define DT3155_REG_SHADOWS            \
    uint8_t csr2;                    \
    uint8_t config;

/* simple model constants (inferred, but BAR size not given by driver; we
 * choose a small size sufficient to cover all known offsets) */
#define DT3155_BAR0_SIZE 0x80

/* ------------------------------------------------------------------ */
/* BAR metadata and device state definition                           */
/* ------------------------------------------------------------------ */

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
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

    DT3155_REG_SHADOWS

    /* runtime register state for modeling */
    uint32_t reg_csr1;
    uint32_t reg_int_csr;
    uint32_t reg_even_dma_start;
    uint32_t reg_odd_dma_start;
    uint32_t reg_even_dma_stride;
    uint32_t reg_odd_dma_stride;

    /* i2c engine visible state */
    uint32_t reg_iic_csr1;
    uint32_t reg_iic_csr2;

    /* IRQ line state */
    uint32_t irq_status;

    /* capture state */
    bool capturing;
    uint32_t width;
    uint32_t height;

    /* DMA buffer base for current frame (guest physical) */
    dma_addr_t dma_base;

    QEMUTimer field_timer;
    uint64_t field_interval_ns;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* Helper: IRQ handling                                               */
/* ------------------------------------------------------------------ */

static void dt3155_update_irq(PCIBaseState *s)
{
    /* Driver only uses legacy INTx (request_irq on pdev->irq). */
    if (s->irq_status) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

static void dt3155_raise_irq(PCIBaseState *s)
{
    s->irq_status = 1;
    dt3155_update_irq(s);
}

static void dt3155_lower_irq(PCIBaseState *s)
{
    s->irq_status = 0;
    dt3155_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* I2C emulation helpers (only as far as driver uses them)            */
/* ------------------------------------------------------------------ */

static void dt3155_i2c_start_cycle(PCIBaseState *s, uint32_t val)
{
    /* Store written value and set NEW_CYCLE bit to indicate busy. */
    s->reg_iic_csr2 = val | DT3155_NEW_CYCLE;
    /* The driver waits and then expects NEW_CYCLE to be cleared. */
    s->reg_iic_csr2 &= ~DT3155_NEW_CYCLE;
}

static uint8_t dt3155_i2c_get_index(uint32_t csr2)
{
    /* index comes from bits [23:17] in driver: (tmp << 17) | ... */
    return (csr2 >> 17) & 0x7f;
}

static void dt3155_i2c_process_read(PCIBaseState *s)
{
    uint8_t idx = dt3155_i2c_get_index(s->reg_iic_csr2);
    uint8_t data = 0x00;

    /* Only behavior visible in driver: DT_ID must read DT3155_ID. */
    if (idx == DT3155_DT_ID) {
        data = DT3155_DT3155_ID;
    }

    /* Driver reads data from IIC_CSR1 as (tmp >> 24). */
    s->reg_iic_csr1 &= 0x00ffffffu;
    s->reg_iic_csr1 |= ((uint32_t)data) << 24;
}

static void dt3155_i2c_process_write(PCIBaseState *s)
{
    uint8_t idx = dt3155_i2c_get_index(s->reg_iic_csr2);
    uint8_t data = s->reg_iic_csr2 & 0xff;

    if (idx == DT3155_CONFIG) {
        s->config = data;
    } else if (idx == DT3155_CSR2) {
        s->csr2 = data;
    }

    /* DIRECT_ABORT is only checked by driver to detect failure, we keep it clear. */
    s->reg_iic_csr1 &= ~DT3155_DIRECT_ABORT;
}

/* ------------------------------------------------------------------ */
/* Field timer: generate end-of-field interrupt events                */
/* ------------------------------------------------------------------ */

static void dt3155_field_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!s->capturing) {
        return;
    }

    /* Model end-of-field interrupt */
    s->reg_int_csr |= (DT3155_FLD_END_EVEN | DT3155_FLD_END_ODD);
    s->reg_int_csr &= ~DT3155_FLD_START;

    dt3155_raise_irq(s);

    /* schedule next field */
    timer_mod(&s->field_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->field_interval_ns);
}

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                    */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* All registers are 32-bit from driver usage, but allow smaller reads. */
    uint32_t val32 = 0;

    switch (addr) {
    case DT3155_EVEN_DMA_START:
        val32 = s->reg_even_dma_start;
        break;
    case DT3155_ODD_DMA_START:
        val32 = s->reg_odd_dma_start;
        break;
    case DT3155_EVEN_DMA_STRIDE:
        val32 = s->reg_even_dma_stride;
        break;
    case DT3155_ODD_DMA_STRIDE:
        val32 = s->reg_odd_dma_stride;
        break;
    case DT3155_CSR1:
        val32 = s->reg_csr1;
        break;
    case DT3155_INT_CSR:
        val32 = s->reg_int_csr;
        break;
    case DT3155_IIC_CSR1:
        val32 = s->reg_iic_csr1;
        break;
    case DT3155_IIC_CSR2:
        val32 = s->reg_iic_csr2;
        break;
    default:
        /* Unknown offsets: return 0, as driver never reads them. */
        val32 = 0;
        break;
    }

    if (size == 1) {
        uint8_t *p = (uint8_t *)&val32;
        return p[addr & 3];
    } else if (size == 2) {
        uint16_t *p = (uint16_t *)&val32;
        return p[(addr & 2) >> 1];
    } else {
        return val32;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32;

    if (size == 1) {
        v32 = (uint8_t)val;
    } else if (size == 2) {
        v32 = (uint16_t)val;
    } else {
        v32 = (uint32_t)val;
    }

    switch (addr) {
    case DT3155_EVEN_DMA_START:
        /* start address of even field DMA */
        s->reg_even_dma_start = v32;
        s->dma_base = v32;
        break;
    case DT3155_ODD_DMA_START:
        s->reg_odd_dma_start = v32;
        break;
    case DT3155_EVEN_DMA_STRIDE:
        s->reg_even_dma_stride = v32;
        s->width = v32;
        break;
    case DT3155_ODD_DMA_STRIDE:
        s->reg_odd_dma_stride = v32;
        break;
    case DT3155_CSR1:
        /* driver writes many bits here to control capture; we just store. */
        s->reg_csr1 = v32;
        break;
    case DT3155_INT_CSR:
        /* driver writes to enable interrupts and clear flags. */
        s->reg_int_csr = v32;
        if (!(s->reg_int_csr & (DT3155_FLD_START | DT3155_FLD_END_EVEN | DT3155_FLD_END_ODD))) {
            dt3155_lower_irq(s);
        }
        break;
    case DT3155_IIC_CSR1:
        /* Direct writes are only used to clear DIRECT_ABORT bit. */
        s->reg_iic_csr1 &= ~DT3155_DIRECT_ABORT;
        s->reg_iic_csr1 |= (v32 & DT3155_DIRECT_ABORT);
        break;
    case DT3155_IIC_CSR2:
        dt3155_i2c_start_cycle(s, v32);
        if (v32 & DT3155_IIC_READ) {
            dt3155_i2c_process_read(s);
        } else if (v32 & DT3155_IIC_WRITE) {
            dt3155_i2c_process_write(s);
        }
        break;
    default:
        /* Unknown register, ignore writes. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* dt3155 driver uses only BAR0 memory-mapped; no PIO accesses. */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO in driver. */
}

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
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                               */
/* ------------------------------------------------------------------ */

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
    }
}

/* ------------------------------------------------------------------ */
/* Reset (clear state, IRQs, DMA)                                     */
/* ------------------------------------------------------------------ */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    s->reg_csr1 = 0;
    s->reg_int_csr = 0;
    s->reg_even_dma_start = 0;
    s->reg_odd_dma_start = 0;
    s->reg_even_dma_stride = 0;
    s->reg_odd_dma_stride = 0;
    s->reg_iic_csr1 = 0;
    s->reg_iic_csr2 = 0;
    s->csr2 = DT3155_VT_50HZ;
    s->config = DT3155_ACQ_MODE_EVEN;

    s->capturing = false;
    s->width = 768;
    s->height = 576;
    s->dma_base = 0;

    s->irq_status = 0;
    dt3155_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* DMA initialization (no explicit DMA engine modeling needed)        */
/* ------------------------------------------------------------------ */

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                            */
/* ------------------------------------------------------------------ */

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Core realize / initialize                                          */
/* ------------------------------------------------------------------ */

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Basic PCI config */
    pci_config_set_vendor_id(pci_conf, PCI_VENDOR_ID_INTEL);
    pci_config_set_device_id(pci_conf, DT3155_DEVICE_ID);
    pci_config_set_interrupt_pin(pci_conf, 1); /* INTA# */

    /* BAR0 MMIO as used by driver */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = DT3155_BAR0_SIZE;
    s->bar_info[0].name = DT3155_NAME "-mmio";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* DMA configuration (nothing special) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* Legacy INTx only; no MSI/MSI-X. */

    /* Field timer: fixed period (approx. 20ms for 50Hz) */
    s->field_interval_ns = 20 * (uint64_t)1000000;
    timer_init_ns(&s->field_timer, QEMU_CLOCK_VIRTUAL, dt3155_field_timer_cb, s);

    /* Initialize default state similar to driver probe */
    s->csr2 = DT3155_VT_50HZ;
    s->config = DT3155_ACQ_MODE_EVEN;
    s->width = 768;
    s->height = 576;
}

/* ------------------------------------------------------------------ */
/* Uninit / cleanup                                                   */
/* ------------------------------------------------------------------ */

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    timer_del(&s->field_timer);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
        s->mmio_backing_size = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                     */
/* ------------------------------------------------------------------ */

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = DT3155_DEVICE_ID;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_OTHERS;

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

static void pcibase_register_types_wrapper(void)
{
    pcibase_register_types();
}

type_init(pcibase_register_types_wrapper);
