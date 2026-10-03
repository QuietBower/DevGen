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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "SMI_PCIe_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SMI_VID                         0x1ADE
#define SMI_PID                         0x3038
#define PCI_CLASS_MULTIMEDIA_OTHER      0x0480
#define CLASS_ID_VALUE                  0x0080 /* lower 16 bits of class code (subclass=0x80, prog-if=0) */

/* Register base addresses */
#define SYSTEM_CONTROL_REG_BASE              0x0880
#define MSI_CONTROL_REG_BASE                 0x0800
#define I2C_A_CONTROL_REG_BASE               0x0940
#define I2C_B_CONTROL_REG_BASE               0x0980
#define ATV_PORTA_CONTROL_REG_BASE           0x09C0
#define DTV_PORTA_CONTROL_REG_BASE           0x0A00
#define ATV_PORTB_CONTROL_REG_BASE           0x0B00
#define DTV_PORTB_CONTROL_REG_BASE           0x0B40
#define DMA_PORTA_CONTROL_REG_BASE           0x0AC0
#define DMA_PORTB_CONTROL_REG_BASE           0x0C00
#define IR_DATA_BUFFER_BASE                  0x0F00

/* Register offsets relative to base addresses */
#define MPEG2_CTRL_B                    (DTV_PORTB_CONTROL_REG_BASE + 0x00)
#define MSI_INT_STATUS                  (MSI_CONTROL_REG_BASE + 0x08)
#define MSI_INT_STATUS_CLR              (MSI_CONTROL_REG_BASE + 0x0C)
#define MPEG2_CTRL_A                    (DTV_PORTA_CONTROL_REG_BASE + 0x00)
#define VIDEO_CTRL_STATUS_B             (ATV_PORTB_CONTROL_REG_BASE + 0x04)
#define MUX_MODE_CTRL                   (SYSTEM_CONTROL_REG_BASE + 0x00)
#define VIDEO_CTRL_STATUS_A             (ATV_PORTA_CONTROL_REG_BASE + 0x04)
#define MSI_INT_ENA_CLR                 (MSI_CONTROL_REG_BASE + 0x18)
#define PERIPHERAL_CTRL                 (SYSTEM_CONTROL_REG_BASE + 0x08)
#define I2C_A_SW_CTL                    (I2C_A_CONTROL_REG_BASE + 0x08)
#define I2C_B_SW_CTL                    (I2C_B_CONTROL_REG_BASE + 0x08)
#define MSI_INT_ENA_SET                 (MSI_CONTROL_REG_BASE + 0x1C)
#define DMA_PORTA_CHAN0_ADDR_LOW        (DMA_PORTA_CONTROL_REG_BASE + 0x00)
#define DMA_PORTB_CHAN0_ADDR_LOW        (DMA_PORTB_CONTROL_REG_BASE + 0x00)
#define DMA_PORTA_CHAN0_TRANS_STATE     (DMA_PORTA_CONTROL_REG_BASE + 0x08)
#define DMA_PORTA_CHAN1_ADDR_HI         (DMA_PORTA_CONTROL_REG_BASE + 0x14)
#define DMA_PORTB_CHAN1_ADDR_HI         (DMA_PORTB_CONTROL_REG_BASE + 0x14)
#define DMA_PORTB_CHAN0_TRANS_STATE     (DMA_PORTB_CONTROL_REG_BASE + 0x08)
#define DMA_PORTA_CHAN0_CONTROL         (DMA_PORTA_CONTROL_REG_BASE + 0x0C)
#define DMA_PORTB_CHAN1_TRANS_STATE     (DMA_PORTB_CONTROL_REG_BASE + 0x18)
#define DMA_PORTB_CHAN1_CONTROL         (DMA_PORTB_CONTROL_REG_BASE + 0x1C)
#define DMA_PORTA_CHAN0_ADDR_HI         (DMA_PORTA_CONTROL_REG_BASE + 0x04)
#define DMA_PORTB_MANAGEMENT            (DMA_PORTB_CONTROL_REG_BASE + 0x20)
#define DMA_PORTB_CHAN0_ADDR_HI         (DMA_PORTB_CONTROL_REG_BASE + 0x04)
#define DMA_PORTA_CHAN1_ADDR_LOW        (DMA_PORTA_CONTROL_REG_BASE + 0x10)
#define DMA_PORTA_CHAN1_TRANS_STATE     (DMA_PORTA_CONTROL_REG_BASE + 0x18)
#define DMA_PORTA_CHAN1_CONTROL         (DMA_PORTA_CONTROL_REG_BASE + 0x1C)
#define DMA_PORTB_CHAN1_ADDR_LOW        (DMA_PORTB_CONTROL_REG_BASE + 0x10)
#define DMA_PORTB_CHAN0_CONTROL         (DMA_PORTB_CONTROL_REG_BASE + 0x0C)
#define DMA_PORTA_MANAGEMENT            (DMA_PORTA_CONTROL_REG_BASE + 0x20)

/* Bit masks and constants */
#define SW_I2C_MSK_DAT_EN              0x10
#define SW_I2C_MSK_DAT_OUT             0x04
#define SW_I2C_MSK_CLK_OUT             0x02
#define SW_I2C_MSK_CLK_EN              0x08
#define SW_I2C_MSK_DAT_IN              0x40
#define SW_I2C_MSK_CLK_IN              0x80
#define ALL_INT                        0x0000FFFF
#define DMA_TRANS_UNIT_188             (0x00000007)
#define IR_X_INT                       0x00000800
#define SMI_TS_DMA_BUF_SIZE            (1024 * 188)

/* Driver-specific enums */
#define DVBSKY_FE_M88DS3103            2
#define DVBSKY_FE_SIT2                 3
#define DVBSKY_FE_M88RS6000            1
#define DVBSKY_FE_NULL                 0
#define SMI_TS_DMA_BOTH                3
#define SMI_TS_NULL                    0
#define SMI_DVBSKY_S950                1
#define SMI_DVBSKY_S952                0
#define SMI_DVBSKY_T9580               2
#define SMI_TECHNOTREND_S2_4200        4
#define SMI_TS_DMA_SINGLE              1

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t system_control[0x100 / 4];   /* 0x0880 - 0x08FF */
    uint32_t msi_control[0x80 / 4];       /* 0x0800 - 0x087F */
    uint32_t i2c_a_control[0x40 / 4];      /* 0x0940 - 0x097F */
    uint32_t i2c_b_control[0x40 / 4];      /* 0x0980 - 0x09BF */
    uint32_t atv_porta_control[0x40 / 4];  /* 0x09C0 - 0x09FF */
    uint32_t dtv_porta_control[0xC0 / 4];  /* 0x0A00 - 0x0ABF */
    uint32_t atv_portb_control[0x40 / 4];  /* 0x0B00 - 0x0B3F */
    uint32_t dtv_portb_control[0x40 / 4];  /* 0x0B40 - 0x0B7F */
    uint32_t dma_porta_control[0x60 / 4];  /* 0x0AC0 - 0x0B1F */
    uint32_t dma_portb_control[0x60 / 4];  /* 0x0C00 - 0x0C5F */
    uint32_t ir_data_buffer[0x100 / 4];    /* 0x0F00 - 0x0FFF */

    /* DMA Context */
    struct {
        dma_addr_t src;
        dma_addr_t dst;
        dma_addr_t cnt;
        dma_addr_t cmd;
    } dma_porta;
    struct {
        dma_addr_t src;
        dma_addr_t dst;
        dma_addr_t cnt;
        dma_addr_t cmd;
    } dma_portb;

    /* Operational status flags */
    uint32_t status;

    /* State used to handle reset sequences */
    bool reset_active;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_mask;
    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint32_t *pcibase_reg_get(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
        case MUX_MODE_CTRL: return &s->system_control[(MUX_MODE_CTRL - SYSTEM_CONTROL_REG_BASE) / 4];
        case PERIPHERAL_CTRL: return &s->system_control[(PERIPHERAL_CTRL - SYSTEM_CONTROL_REG_BASE) / 4];
        case MSI_INT_STATUS: return &s->msi_control[(MSI_INT_STATUS - MSI_CONTROL_REG_BASE) / 4];
        case MSI_INT_STATUS_CLR: return &s->msi_control[(MSI_INT_STATUS_CLR - MSI_CONTROL_REG_BASE) / 4];
        case MSI_INT_ENA_CLR: return &s->msi_control[(MSI_INT_ENA_CLR - MSI_CONTROL_REG_BASE) / 4];
        case MSI_INT_ENA_SET: return &s->msi_control[(MSI_INT_ENA_SET - MSI_CONTROL_REG_BASE) / 4];
        case I2C_A_SW_CTL: return &s->i2c_a_control[(I2C_A_SW_CTL - I2C_A_CONTROL_REG_BASE) / 4];
        case I2C_B_SW_CTL: return &s->i2c_b_control[(I2C_B_SW_CTL - I2C_B_CONTROL_REG_BASE) / 4];
        case VIDEO_CTRL_STATUS_A: return &s->atv_porta_control[(VIDEO_CTRL_STATUS_A - ATV_PORTA_CONTROL_REG_BASE) / 4];
        case MPEG2_CTRL_A: return &s->dtv_porta_control[(MPEG2_CTRL_A - DTV_PORTA_CONTROL_REG_BASE) / 4];
        case VIDEO_CTRL_STATUS_B: return &s->atv_portb_control[(VIDEO_CTRL_STATUS_B - ATV_PORTB_CONTROL_REG_BASE) / 4];
        case MPEG2_CTRL_B: return &s->dtv_portb_control[(MPEG2_CTRL_B - DTV_PORTB_CONTROL_REG_BASE) / 4];
        case DMA_PORTA_CHAN0_ADDR_LOW: return &s->dma_porta_control[(DMA_PORTA_CHAN0_ADDR_LOW - DMA_PORTA_CONTROL_REG_BASE) / 4];
        case DMA_PORTA_CHAN0_ADDR_HI: return &s->dma_porta_control[(DMA_PORTA_CHAN0_ADDR_HI - DMA_PORTA_CONTROL_REG_BASE) / 4];
        case DMA_PORTA_CHAN0_TRANS_STATE: return &s->dma_porta_control[(DMA_PORTA_CHAN0_TRANS_STATE - DMA_PORTA_CONTROL_REG_BASE) / 4];
        case DMA_PORTA_CHAN0_CONTROL: return &s->dma_porta_control[(DMA_PORTA_CHAN0_CONTROL - DMA_PORTA_CONTROL_REG_BASE) / 4];
        case DMA_PORTA_CHAN1_ADDR_LOW: return &s->dma_porta_control[(DMA_PORTA_CHAN1_ADDR_LOW - DMA_PORTA_CONTROL_REG_BASE) / 4];
        case DMA_PORTA_CHAN1_ADDR_HI: return &s->dma_porta_control[(DMA_PORTA_CHAN1_ADDR_HI - DMA_PORTA_CONTROL_REG_BASE) / 4];
        case DMA_PORTA_CHAN1_TRANS_STATE: return &s->dma_porta_control[(DMA_PORTA_CHAN1_TRANS_STATE - DMA_PORTA_CONTROL_REG_BASE) / 4];
        case DMA_PORTA_CHAN1_CONTROL: return &s->dma_porta_control[(DMA_PORTA_CHAN1_CONTROL - DMA_PORTA_CONTROL_REG_BASE) / 4];
        case DMA_PORTA_MANAGEMENT: return &s->dma_porta_control[(DMA_PORTA_MANAGEMENT - DMA_PORTA_CONTROL_REG_BASE) / 4];
        case DMA_PORTB_CHAN0_ADDR_LOW: return &s->dma_portb_control[(DMA_PORTB_CHAN0_ADDR_LOW - DMA_PORTB_CONTROL_REG_BASE) / 4];
        case DMA_PORTB_CHAN0_ADDR_HI: return &s->dma_portb_control[(DMA_PORTB_CHAN0_ADDR_HI - DMA_PORTB_CONTROL_REG_BASE) / 4];
        case DMA_PORTB_CHAN0_TRANS_STATE: return &s->dma_portb_control[(DMA_PORTB_CHAN0_TRANS_STATE - DMA_PORTB_CONTROL_REG_BASE) / 4];
        case DMA_PORTB_CHAN0_CONTROL: return &s->dma_portb_control[(DMA_PORTB_CHAN0_CONTROL - DMA_PORTB_CONTROL_REG_BASE) / 4];
        case DMA_PORTB_CHAN1_ADDR_LOW: return &s->dma_portb_control[(DMA_PORTB_CHAN1_ADDR_LOW - DMA_PORTB_CONTROL_REG_BASE) / 4];
        case DMA_PORTB_CHAN1_ADDR_HI: return &s->dma_portb_control[(DMA_PORTB_CHAN1_ADDR_HI - DMA_PORTB_CONTROL_REG_BASE) / 4];
        case DMA_PORTB_CHAN1_TRANS_STATE: return &s->dma_portb_control[(DMA_PORTB_CHAN1_TRANS_STATE - DMA_PORTB_CONTROL_REG_BASE) / 4];
        case DMA_PORTB_CHAN1_CONTROL: return &s->dma_portb_control[(DMA_PORTB_CHAN1_CONTROL - DMA_PORTB_CONTROL_REG_BASE) / 4];
        case DMA_PORTB_MANAGEMENT: return &s->dma_portb_control[(DMA_PORTB_MANAGEMENT - DMA_PORTB_CONTROL_REG_BASE) / 4];
        default: return NULL;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t *reg = pcibase_reg_get(s, addr);
    if (reg) {
        if (addr == MSI_INT_STATUS) {
            return s->intr_status;
        } else if (addr == MSI_INT_ENA_CLR || addr == MSI_INT_ENA_SET) {
            return s->intr_mask;
        } else {
            return *reg;
        }
    }
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t *reg = pcibase_reg_get(s, addr);

    if (reg) {
        if (addr == MSI_INT_STATUS_CLR) {
            s->intr_status &= ~val;
            pcibase_update_irq(s);
        } else if (addr == MSI_INT_ENA_CLR) {
            s->intr_mask &= ~val;
            pcibase_update_irq(s);
        } else if (addr == MSI_INT_ENA_SET) {
            s->intr_mask |= val;
            pcibase_update_irq(s);
        } else {
            *reg = val;
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->system_control, 0, sizeof(s->system_control));
    memset(s->msi_control, 0, sizeof(s->msi_control));
    memset(s->i2c_a_control, 0, sizeof(s->i2c_a_control));
    memset(s->i2c_b_control, 0, sizeof(s->i2c_b_control));
    memset(s->atv_porta_control, 0, sizeof(s->atv_porta_control));
    memset(s->dtv_porta_control, 0, sizeof(s->dtv_porta_control));
    memset(s->atv_portb_control, 0, sizeof(s->atv_portb_control));
    memset(s->dtv_portb_control, 0, sizeof(s->dtv_portb_control));
    memset(s->dma_porta_control, 0, sizeof(s->dma_porta_control));
    memset(s->dma_portb_control, 0, sizeof(s->dma_portb_control));
    memset(s->ir_data_buffer, 0, sizeof(s->ir_data_buffer));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status = 0;
    s->reset_active = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, SMI_VID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, SMI_PID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID_VALUE);
    pci_set_byte(pci_conf + PCI_CLASS_DEVICE + 1, PCI_CLASS_MULTIMEDIA_OTHER >> 16);
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
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x1000,
        .name = "smipcie-mmio",
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI or MSI-X initialization */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;
    s->has_msix = false;

    /* Final state initialization before the device is 'live' */
    pcibase_reset(DEVICE(s));
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "SMI_PCIe_driver_pci",
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
