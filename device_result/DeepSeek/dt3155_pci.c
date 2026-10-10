/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "dt3155_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID     0x8086
#define DEVICE_ID     0x1223
#define CLASS_ID      0x000000   /* TODO: need correct class ID */

/* Register offsets */
#define IIC_CSR2          0x64
#define IIC_CSR1          0x60
#define ODD_DMA_START     0x0C
#define CSR1              0x40
#define CONFIG            0x13
#define ODD_CSR           0x12
#define EVEN_CSR          0x11
#define EVEN_DMA_STRIDE   0x18
#define ODD_DMA_STRIDE    0x24
#define EVEN_DMA_START    0x00
#define INT_CSR           0x48
#define CSR2              0x10
#define AD_CMD            0x32
#define AD_CMD_REG        0x00
#define SYNC_LVL_3        0x08
#define AD_ADDR           0x30
#define RETRY_WAIT_CNT    0x44
#define DT_ID             0x1F
#define FIFO_TRIGGER      0x38
#define PM_LUT_PGM        0x80
#define AD_NEG_REF        0x02
#define XFER_MODE         0x3C
#define IIC_CLK_DUR       0x5C
#define SYNC_CNL_1        0x00
#define AD_LUT            0x31
#define PM_LUT_SEL        0x40
#define EVEN_PIXEL_FMT    0x30
#define ODD_FLD_MASK      0x50
#define ODD_PIXEL_FMT     0x34
#define VIDEO_CNL_1       0x00
#define PM_LUT_ADDR       0x50
#define AD_POS_REF        0x01
#define EVEN_FLD_MASK     0x4C
#define PM_LUT_DATA       0x51
#define FIFO_FLAG_CNT     0x58
#define DT3155_ID         0x20
#define MASK_LENGTH       0x54
#define ACQ_MODE_EVEN     0x00

/* Bitmask definitions */
#define IIC_READ          0x01010000
#define NEW_CYCLE         0x01000000
#define DIRECT_ABORT      0x00000200
#define IIC_WRITE         0x01000000
#define FLD_END_ODD_EN    0x00000200
#define FLD_DN_EVEN       0x00000010
#define FLD_DN_ODD        0x00000020
#define FLD_END_ODD       0x00000002
#define BUSY_EVEN         0x10
#define CSR_ERROR         0x04
#define FLD_START         0x00000004
#define BUSY_ODD          0x20
#define CAP_CONT_ODD      0x00000002
#define FLD_START_EN      0x00000400
#define FLD_CRPT_EVEN     0x00000100
#define FLD_CRPT_ODD      0x00000200
#define CAP_CONT_EVEN     0x00000001
#define FLD_END_EVEN      0x00000001
#define SRST              0x00000040
#define FIFO_EN           0x00000080
#define CSR_DONE          0x01
#define VT_60HZ           0x00
#define VT_50HZ           0x04

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

enum field_state {
    FIELD_IDLE = 0,
    FIELD_START_DELAY,
    FIELD_END_DELAY,
    FIELD_NEXT_DELAY
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Shadow registers */
    uint32_t even_dma_start;
    uint32_t odd_dma_start;
    uint32_t even_dma_stride;
    uint32_t odd_dma_stride;
    uint32_t csr1;
    uint32_t retry_wait_cnt;
    uint32_t int_csr;
    uint32_t even_pixel_fmt;
    uint32_t odd_pixel_fmt;
    uint32_t fifo_trigger;
    uint32_t xfer_mode;
    uint32_t even_fld_mask;
    uint32_t odd_fld_mask;
    uint32_t mask_length;
    uint32_t fifo_flag_cnt;
    uint32_t iic_clk_dur;
    uint32_t iic_csr1;
    uint32_t iic_csr2;

    uint8_t i2c_regs[256];
    QEMUTimer *i2c_timer;
    QEMUTimer *field_timer;
    int field_state;
    bool streaming_active;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t enabled_irqs = s->int_csr & (FLD_START_EN | FLD_END_ODD_EN) & 
                            (FLD_START | FLD_END_ODD);
    pci_set_irq(pdev, !!enabled_irqs);
}

static void i2c_clear_new_cycle(void *opaque)
{
    PCIBaseState *s = opaque;
    s->iic_csr2 &= ~NEW_CYCLE;
}

static void field_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    int64_t next_delay = 0;

    switch (s->field_state) {
    case FIELD_START_DELAY:
        s->int_csr |= FLD_START;
        s->field_state = FIELD_END_DELAY;
        next_delay = 16000; /* 16 ms field duration */
        break;
    case FIELD_END_DELAY:
        s->int_csr |= FLD_END_ODD;
        s->field_state = FIELD_NEXT_DELAY;
        next_delay = 2000; /* short blanking */
        break;
    case FIELD_NEXT_DELAY:
        s->int_csr |= FLD_START;
        s->field_state = FIELD_END_DELAY;
        next_delay = 16000;
        break;
    default:
        return;
    }
    pcibase_update_irq(s);
    timer_mod(s->field_timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + next_delay);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return val;
    }

    switch (addr) {
    case EVEN_DMA_START:
        val = s->even_dma_start;
        break;
    case ODD_DMA_START:
        val = s->odd_dma_start;
        break;
    case EVEN_DMA_STRIDE:
        val = s->even_dma_stride;
        break;
    case ODD_DMA_STRIDE:
        val = s->odd_dma_stride;
        break;
    case CSR1:
        val = s->csr1;
        break;
    case RETRY_WAIT_CNT:
        val = s->retry_wait_cnt;
        break;
    case INT_CSR:
        val = s->int_csr;
        break;
    case EVEN_PIXEL_FMT:
        val = s->even_pixel_fmt;
        break;
    case ODD_PIXEL_FMT:
        val = s->odd_pixel_fmt;
        break;
    case FIFO_TRIGGER:
        val = s->fifo_trigger;
        break;
    case XFER_MODE:
        val = s->xfer_mode;
        break;
    case EVEN_FLD_MASK:
        val = s->even_fld_mask;
        break;
    case ODD_FLD_MASK:
        val = s->odd_fld_mask;
        break;
    case MASK_LENGTH:
        val = s->mask_length;
        break;
    case FIFO_FLAG_CNT:
        val = s->fifo_flag_cnt;
        break;
    case IIC_CLK_DUR:
        val = s->iic_clk_dur;
        break;
    case IIC_CSR1:
        val = s->iic_csr1;
        break;
    case IIC_CSR2:
        val = s->iic_csr2;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = (uint32_t)val;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case EVEN_DMA_START:
        s->even_dma_start = val32;
        break;
    case ODD_DMA_START:
        s->odd_dma_start = val32;
        break;
    case EVEN_DMA_STRIDE:
        s->even_dma_stride = val32;
        break;
    case ODD_DMA_STRIDE:
        s->odd_dma_stride = val32;
        break;
    case CSR1:
        s->csr1 = val32;
        break;
    case RETRY_WAIT_CNT:
        s->retry_wait_cnt = val32;
        break;
    case INT_CSR:
        /* W1C on status bits, RW on enable bits */
        if (val32 & FLD_START) {
            s->int_csr &= ~FLD_START;
        }
        if (val32 & FLD_END_EVEN) {
            s->int_csr &= ~FLD_END_EVEN;
        }
        if (val32 & FLD_END_ODD) {
            s->int_csr &= ~FLD_END_ODD;
        }
        /* enable bits are set if written as 1, cleared if 0 */
        s->int_csr = (s->int_csr & ~(FLD_START_EN | FLD_END_ODD_EN)) |
                     (val32 & (FLD_START_EN | FLD_END_ODD_EN));
        pcibase_update_irq(s);
        break;
    case EVEN_PIXEL_FMT:
        s->even_pixel_fmt = val32;
        break;
    case ODD_PIXEL_FMT:
        s->odd_pixel_fmt = val32;
        break;
    case FIFO_TRIGGER:
        s->fifo_trigger = val32;
        break;
    case XFER_MODE:
        s->xfer_mode = val32;
        break;
    case EVEN_FLD_MASK:
        s->even_fld_mask = val32;
        break;
    case ODD_FLD_MASK:
        s->odd_fld_mask = val32;
        break;
    case MASK_LENGTH:
        s->mask_length = val32;
        break;
    case FIFO_FLAG_CNT:
        s->fifo_flag_cnt = val32;
        break;
    case IIC_CLK_DUR:
        s->iic_clk_dur = val32;
        break;
    case IIC_CSR1:
        /* Only DIRECT_ABORT can be cleared by writing 1 */
        if (val32 & DIRECT_ABORT) {
            s->iic_csr1 &= ~DIRECT_ABORT;
        }
        break;
    case IIC_CSR2:
        {
            uint8_t index = (val32 >> 17) & 0x7F;
            uint8_t data = val32 & 0xFF;
            uint8_t is_read = (val32 >> 16) & 1;

            s->iic_csr2 = val32 | NEW_CYCLE;
            timer_mod(s->i2c_timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + 45);

            if (is_read) {
                s->iic_csr1 = (s->iic_csr1 & ~(0xFF << 24)) | (s->i2c_regs[index] << 24);
            } else {
                s->i2c_regs[index] = data;
            }

            /* Check if CSR2 write starts/stops streaming */
            if (index == CSR2) {
                if (data & (BUSY_EVEN | BUSY_ODD)) {
                    if (!s->streaming_active) {
                        s->streaming_active = true;
                        s->field_state = FIELD_START_DELAY;
                        timer_mod(s->field_timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + 1000);
                    }
                } else {
                    if (s->streaming_active) {
                        s->streaming_active = false;
                        s->field_state = FIELD_IDLE;
                        timer_del(s->field_timer);
                    }
                }
            }
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO not used by this driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* PIO not used by this driver */
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

    /* Reset all shadow registers to zero */
    s->even_dma_start = 0;
    s->odd_dma_start = 0;
    s->even_dma_stride = 0;
    s->odd_dma_stride = 0;
    s->csr1 = 0;
    s->retry_wait_cnt = 0;
    s->int_csr = 0;
    s->even_pixel_fmt = 0;
    s->odd_pixel_fmt = 0;
    s->fifo_trigger = 0;
    s->xfer_mode = 0;
    s->even_fld_mask = 0;
    s->odd_fld_mask = 0;
    s->mask_length = 0;
    s->fifo_flag_cnt = 0;
    s->iic_clk_dur = 0;
    s->iic_csr1 = 0;
    s->iic_csr2 = 0;

    /* Initialize I2C memory with DT3155_ID at DT_ID */
    memset(s->i2c_regs, 0, sizeof(s->i2c_regs));
    s->i2c_regs[DT_ID] = DT3155_ID;

    /* Stop streaming */
    s->streaming_active = false;
    s->field_state = FIELD_IDLE;
    timer_del(s->field_timer);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* Unknown, requested as BAR0_SIZE */
    s->bar_info[0].name = "dt3155-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize timers */
    s->i2c_timer = timer_new_us(QEMU_CLOCK_VIRTUAL, i2c_clear_new_cycle, s);
    s->field_timer = timer_new_us(QEMU_CLOCK_VIRTUAL, field_timer_cb, s);

    /* MSI/MSI-X not used */
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

    timer_del(s->i2c_timer);
    timer_free(s->i2c_timer);
    timer_del(s->field_timer);
    timer_free(s->field_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "dt3155_pci",
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
