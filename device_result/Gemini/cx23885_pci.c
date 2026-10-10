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

#define TYPE_PCIBASE_DEVICE "cx23885_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CX23885_VENDOR_ID 0x14f1
#define CX23885_DEVICE_ID 0x8852
#define CX23885_CLASS_ID  0x0400

#define PCI_INT_MSK		0x00040010
#define PCI_INT_STAT		0x00040014
#define VID_A_INT_MSK		0x00040020
#define VID_A_INT_STAT		0x00040024
#define VID_B_INT_MSK		0x00040030
#define VID_B_INT_STAT		0x00040034
#define VID_C_INT_MSK		0x00040040
#define VID_C_INT_STAT		0x00040044
#define AUDIO_INT_INT_MSK	0x00040050
#define AUDIO_INT_INT_STAT	0x00040054
#define AUDIO_EXT_INT_MSK	0x00040060
#define AUDIO_EXT_INT_STAT	0x00040064
#define DEV_CNTRL2		0x00040000
#define PAD_CTRL		0x0011004C
#define CLK_DELAY		0x00110048
#define VID_A_DMA_CTL		0x00130040
#define VID_B_DMA_CTL		0x00130140
#define VID_C_DMA_CTL		0x00130240
#define AUD_INT_DMA_CTL		0x00140040
#define AUD_EXT_DMA_CTL		0x00140140
#define UART_CTL		0x001B0000
#define IR_CNTRL_REG		0x00000200

#define DMA1_PTR1		0x00100000
#define DMA1_PTR2		0x00100040
#define DMA1_CNT1		0x00100080
#define DMA1_CNT2		0x001000C0

#define PCI_MSK_GPIO0     (1 << 23)
#define PCI_MSK_GPIO1     (1 << 24)

#define PCI_MSK_RISC_RD   (1 <<  8)
#define PCI_MSK_RISC_WR   (1 <<  9)
#define PCI_MSK_AL_RD     (1 << 10)
#define PCI_MSK_AL_WR     (1 << 11)
#define PCI_MSK_APB_DMA   (1 << 12)
#define PCI_MSK_VID_C     (1 <<  2)
#define PCI_MSK_VID_B     (1 <<  1)
#define PCI_MSK_VID_A      1
#define PCI_MSK_AUD_INT   (1 <<  3)
#define PCI_MSK_AUD_EXT   (1 <<  4)
#define PCI_MSK_AV_CORE   (1 << 27)
#define PCI_MSK_IR        (1 << 28)
#define VID_A_GPCNT		0x00130020
#define AUD_INT_A_GPCNT		0x00140020

#define RDR_CFG2	0x00050008
#define TC_REQ		0x00040090
#define TC_REQ_SET	0x00040094
#define VID_B_DMA		0x00130100
#define VBI_B_DMA		0x00130108
#define VID_C_DMA		0x00130200
#define VBI_C_DMA		0x00130208
#define RDR_TLCTL0	0x00050318
#define RDR_RDRCTL1	0x0005030c
#define I2C1_STAT	0x00180010
#define I2C1_CTRL	0x00180008
#define I2C1_ADDR	0x00180000
#define I2C1_RDATA	0x0018000C
#define I2C1_WDATA	0x00180004
#define I2C2_STAT	0x00190010
#define I2C2_CTRL	0x00190008
#define I2C2_ADDR	0x00190000
#define I2C2_RDATA	0x0019000C
#define I2C2_WDATA	0x00190004
#define I2C3_STAT	0x001A0010
#define I2C3_CTRL	0x001A0008
#define I2C3_ADDR	0x001A0000
#define I2C3_RDATA	0x001A000C
#define I2C3_WDATA	0x001A0004
#define VID_B_GPCNT		0x00130120
#define VID_B_GPCNT_CTL		0x00130134
#define VID_B_LNGTH		0x00130150
#define VID_B_HW_SOP_CTL	0x00130154
#define VID_B_GEN_CTL		0x00130158
#define VID_B_BD_PKT_STATUS	0x0013015C
#define VID_B_SOP_STATUS	0x00130160
#define VID_B_FIFO_OVFL_STAT	0x00130164
#define VID_B_VLD_MISC		0x00130168
#define VID_B_TS_CLK_EN		0x0013016C
#define VID_B_SRC_SEL		0x00130144
#define VID_C_GPCNT		0x00130220
#define VID_C_GPCNT_CTL		0x00130230
#define VID_C_LNGTH		0x00130250
#define VID_C_HW_SOP_CTL	0x00130254
#define VID_C_GEN_CTL		0x00130258
#define VID_C_BD_PKT_STATUS	0x0013025C
#define VID_C_SOP_STATUS	0x00130260
#define VID_C_FIFO_OVFL_STAT	0x00130264
#define VID_C_VLD_MISC		0x00130268
#define VID_C_TS_CLK_EN		0x0013026C
#define GP0_IO		0x00110010
#define MC417_RWD	0x00110020
#define MC417_OEN	0x00110024
#define ALT_PIN_OUT_SEL 0x0011002C
#define GPIO2			0x00000164
#define VID_B_INT_MSTAT	0x00040038
#define VID_B_INT_SSTAT	0x0004003C
#define VID_C_INT_MSTAT	0x00040048
#define VID_C_INT_SSTAT	0x0004004C

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
    uint32_t regs[0x200000 / 4];

    /* DMA Context */

};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = false;

    if (s->regs[PCI_INT_STAT / 4] & s->regs[PCI_INT_MSK / 4]) {
        level = true;
    }

    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr / 4;
    uint64_t val = 0;

    if (index < ARRAY_SIZE(s->regs)) {
        val = s->regs[index];
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr / 4;

    if (index < ARRAY_SIZE(s->regs)) {
        if (addr == PCI_INT_STAT || addr == VID_A_INT_STAT || 
            addr == VID_B_INT_STAT || addr == VID_C_INT_STAT || 
            addr == AUDIO_INT_INT_STAT || addr == AUDIO_EXT_INT_STAT) {
            s->regs[index] &= ~val; /* W1C */
            pcibase_update_irq(s);
        } else if (addr == PCI_INT_MSK || addr == VID_A_INT_MSK || 
                   addr == VID_B_INT_MSK || addr == VID_C_INT_MSK || 
                   addr == AUDIO_INT_INT_MSK || addr == AUDIO_EXT_INT_MSK) {
            s->regs[index] = val;
            pcibase_update_irq(s);
        } else {
            s->regs[index] = val;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[PAD_CTRL / 4] = 0x00500300;
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
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0400 );
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
    s->bar_info[0].size = 0x200000;
    s->bar_info[0].name = "cx23885-mmio";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

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
