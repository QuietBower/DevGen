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

#define TYPE_PCIBASE_DEVICE "cx23885_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CX23885     0x14f1
#define PCI_DEVICE_ID_CX23885     0x8852
#define PCI_CLASS_ID_CX23885      0x0480

/* MMIO region size to cover all accessed registers (up to ~0x1B0000) */
#define CX23885_MMIO_SIZE 0x200000

/* Register offsets extracted from driver */
#define GPIO_ISM                0x00110014
#define GP0_IO                  0x00110010
#define MC417_RWD               0x00110020
#define MC417_OEN               0x00110024
#define MC417_CTL               0x00110028
#define ALT_PIN_OUT_SEL         0x0011002C
#define PAD_CTRL                0x0011004C
#define CLK_DELAY               0x00110048
#define GPIO2                   0x00000164
#define CH_PWR_CTRL1            0x0000000E
#define CH_PWR_CTRL2            0x0000000F
#define IR_CNTRL_REG            0x00000200

#define PCI_INT_MSK             0x00040010
#define PCI_INT_STAT            0x00040014
#define TC_REQ                  0x00040090
#define TC_REQ_SET              0x00040094
#define DEV_CNTRL2              0x00040000

#define VID_A_INT_MSK           0x00040020
#define VID_A_INT_STAT          0x00040024
#define VID_B_INT_MSK           0x00040030
#define VID_B_INT_STAT          0x00040034
#define VID_B_INT_MSTAT         0x00040038
#define VID_B_INT_SSTAT         0x0004003C
#define VID_C_INT_MSK           0x00040040
#define VID_C_INT_STAT          0x00040044
#define VID_C_INT_MSTAT         0x00040048
#define VID_C_INT_SSTAT         0x0004004C

#define AUDIO_INT_INT_MSK       0x00040050
#define AUDIO_INT_INT_STAT      0x00040054
#define AUDIO_EXT_INT_MSK       0x00040060
#define AUDIO_EXT_INT_STAT      0x00040064

#define VID_A_DMA_CTL           0x00130040
#define VID_B_DMA_CTL           0x00130140
#define VID_C_DMA_CTL           0x00130240
#define AUD_INT_DMA_CTL         0x00140040
#define AUD_EXT_DMA_CTL         0x00140140

#define VID_B_DMA               0x00130100
#define VBI_B_DMA               0x00130108
#define VID_C_DMA               0x00130200
#define VBI_C_DMA               0x00130208

#define VID_A_GPCNT             0x00130020
#define VID_A_GPCNT_CTL         0x00130030
#define VBI_A_GPCNT             0x00130024
#define VID_B_GPCNT             0x00130120
#define VID_B_GPCNT_CTL         0x00130134
#define VID_B_SRC_SEL           0x00130144
#define VID_B_LNGTH             0x00130150
#define VID_B_HW_SOP_CTL        0x00130154
#define VID_B_GEN_CTL           0x00130158
#define VID_B_BD_PKT_STATUS     0x0013015C
#define VID_B_SOP_STATUS        0x00130160
#define VID_B_FIFO_OVFL_STAT    0x00130164
#define VID_B_VLD_MISC          0x00130168
#define VID_B_TS_CLK_EN         0x0013016C
#define VID_C_GPCNT             0x00130220
#define VID_C_GPCNT_CTL         0x00130230
#define VID_C_LNGTH             0x00130250
#define VID_C_HW_SOP_CTL        0x00130254
#define VID_C_GEN_CTL           0x00130258
#define VID_C_BD_PKT_STATUS     0x0013025C
#define VID_C_SOP_STATUS        0x00130260
#define VID_C_FIFO_OVFL_STAT    0x00130264
#define VID_C_VLD_MISC          0x00130268
#define VID_C_TS_CLK_EN         0x0013026C

#define AUD_INT_A_GPCNT         0x00140020
#define AUD_INT_A_GPCNT_CTL     0x00140030

#define RDR_CFG2                0x00050008
#define RDR_TLCTL0              0x00050318
#define RDR_RDRCTL1             0x0005030c

#define I2C1_ADDR               0x00180000
#define I2C1_WDATA              0x00180004
#define I2C1_CTRL               0x00180008
#define I2C1_RDATA              0x0018000C
#define I2C1_STAT               0x00180010
#define I2C2_ADDR               0x00190000
#define I2C2_WDATA              0x00190004
#define I2C2_CTRL               0x00190008
#define I2C2_RDATA              0x0019000C
#define I2C2_STAT               0x00190010
#define I2C3_ADDR               0x001A0000
#define I2C3_WDATA              0x001A0004
#define I2C3_CTRL               0x001A0008
#define I2C3_RDATA              0x001A000C
#define I2C3_STAT               0x001A0010

#define UART_CTL                0x001B0000

/* DMA pointer/counter registers */
#define DMA1_PTR1               0x00100000
#define DMA2_PTR1               0x00100004
#define DMA3_PTR1               0x00100008
#define DMA4_PTR1               0x0010000C
#define DMA5_PTR1               0x00100010
#define DMA6_PTR1               0x00100014
#define DMA7_PTR1               0x00100018
#define DMA8_PTR1               0x0010001C
#define DMA1_PTR2               0x00100040
#define DMA2_PTR2               0x00100044
#define DMA3_PTR2               0x00100048
#define DMA4_PTR2               0x0010004C
#define DMA5_PTR2               0x00100050
#define DMA6_PTR2               0x00100054
#define DMA7_PTR2               0x00100058
#define DMA8_PTR2               0x0010005C
#define DMA1_CNT1               0x00100080
#define DMA2_CNT1               0x00100084
#define DMA3_CNT1               0x00100088
#define DMA4_CNT1               0x0010008C
#define DMA5_CNT1               0x00100090
#define DMA6_CNT1               0x00100094
#define DMA7_CNT1               0x00100098
#define DMA8_CNT1               0x0010009C
#define DMA1_CNT2               0x001000C0
#define DMA2_CNT2               0x001000C4
#define DMA3_CNT2               0x001000C8
#define DMA4_CNT2               0x001000CC
#define DMA5_CNT2               0x001000D0
#define DMA6_CNT2               0x001000D4
#define DMA7_CNT2               0x001000D8
#define DMA8_CNT2               0x001000DC

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

    /* Full MMIO register file */
    uint32_t regs[CX23885_MMIO_SIZE / 4];
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    unsigned idx_stat = PCI_INT_STAT >> 2;
    unsigned idx_mask = PCI_INT_MSK >> 2;
    uint32_t status = s->regs[idx_stat];
    uint32_t mask = s->regs[idx_mask];

    if (status & mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned index = addr >> 2;

    if (index >= ARRAY_SIZE(s->regs)) {
        return ~0ULL;
    }
    return s->regs[index];
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned index = addr >> 2;

    if (index >= ARRAY_SIZE(s->regs)) {
        return;
    }

    /* Write-1-to-clear status registers */
    switch (addr) {
    case PCI_INT_STAT:
    case VID_A_INT_STAT:
    case VID_B_INT_STAT:
    case VID_C_INT_STAT:
    case AUDIO_INT_INT_STAT:
    case AUDIO_EXT_INT_STAT:
    case VID_B_INT_MSTAT:
    case VID_B_INT_SSTAT:
    case VID_C_INT_MSTAT:
    case VID_C_INT_SSTAT:
        s->regs[index] &= ~val;
        pcibase_update_irq(s);
        return;
    case PCI_INT_MSK:
        s->regs[index] = val;
        pcibase_update_irq(s);
        return;
    default:
        /* Normal write */
        s->regs[index] = val;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO not used, but placeholder */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* PIO not used, but placeholder */
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

    /* Power-on defaults: all registers zero */
    memset(s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x14f1 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8852 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0480 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;   /* Only MMIO BAR detected */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = CX23885_MMIO_SIZE;  /* Cover all accessible registers */
    s->bar_info[0].name = "cx23885-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA configuration - none needed for probe */
    /* Timer initialization - none needed */
    /* Final state initialization */
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

    /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cx23885_pci",
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
