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


#define TYPE_PCIBASE_DEVICE "addi_apci_3120_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMCC 0x10e8
#define PCI_DEVICE_ID_AMCC_APCI3120 0x818d

#define APCI3120_FIFO_ADVANCE_ON_BYTE_2		BIT(29)
#define APCI3120_AI_FIFO_REG			0x00
#define APCI3120_CTRL_REG			0x00
#define APCI3120_CTRL_EXT_TRIG			BIT(15)
#define APCI3120_CTRL_GATE(x)			BIT(12 + (x))
#define APCI3120_CTRL_PR(x)			(((x) & 0xf) << 8)
#define APCI3120_CTRL_PA(x)			(((x) & 0xf) << 0)
#define APCI3120_AI_SOFTTRIG_REG		0x02
#define APCI3120_STATUS_REG			0x02
#define APCI3120_STATUS_EOC_INT			BIT(15)
#define APCI3120_STATUS_AMCC_INT		BIT(14)
#define APCI3120_STATUS_EOS_INT			BIT(13)
#define APCI3120_STATUS_TIMER2_INT		BIT(12)
#define APCI3120_STATUS_INT_MASK		(0xf << 12)
#define APCI3120_STATUS_TO_DI_BITS(x)		(((x) >> 8) & 0xf)
#define APCI3120_STATUS_TO_VERSION(x)		(((x) >> 4) & 0xf)
#define APCI3120_STATUS_FIFO_FULL		BIT(2)
#define APCI3120_STATUS_FIFO_EMPTY		BIT(1)
#define APCI3120_STATUS_DA_READY		BIT(0)
#define APCI3120_TIMER_REG			0x04
#define APCI3120_CHANLIST_REG			0x06
#define APCI3120_CHANLIST_INDEX(x)		(((x) & 0xf) << 8)
#define APCI3120_CHANLIST_UNIPOLAR		BIT(7)
#define APCI3120_CHANLIST_GAIN(x)		(((x) & 0x3) << 4)
#define APCI3120_CHANLIST_MUX(x)		(((x) & 0xf) << 0)
#define APCI3120_AO_REG(x)			(0x08 + (((x) / 4) * 2))
#define APCI3120_AO_MUX(x)			(((x) & 0x3) << 14)
#define APCI3120_AO_DATA(x)			((x) << 0)
#define APCI3120_TIMER_MODE_REG			0x0c
#define APCI3120_TIMER_MODE(_t, _m)		((_m) << ((_t) * 2))
#define APCI3120_TIMER_MODE0			0
#define APCI3120_TIMER_MODE2			1
#define APCI3120_TIMER_MODE4			2
#define APCI3120_TIMER_MODE5			3
#define APCI3120_TIMER_MODE_MASK(_t)		(3 << ((_t) * 2))
#define APCI3120_CTR0_REG			0x0d
#define APCI3120_CTR0_DO_BITS(x)		((x) << 4)
#define APCI3120_CTR0_TIMER_SEL(x)		((x) << 0)
#define APCI3120_MODE_REG			0x0e
#define APCI3120_MODE_TIMER2_CLK(x)		(((x) & 0x3) << 6)
#define APCI3120_MODE_TIMER2_CLK_OSC		APCI3120_MODE_TIMER2_CLK(0)
#define APCI3120_MODE_TIMER2_CLK_OUT1		APCI3120_MODE_TIMER2_CLK(1)
#define APCI3120_MODE_TIMER2_CLK_EOC		APCI3120_MODE_TIMER2_CLK(2)
#define APCI3120_MODE_TIMER2_CLK_EOS		APCI3120_MODE_TIMER2_CLK(3)
#define APCI3120_MODE_TIMER2_CLK_MASK		APCI3120_MODE_TIMER2_CLK(3)
#define APCI3120_MODE_TIMER2_AS(x)		(((x) & 0x3) << 4)
#define APCI3120_MODE_TIMER2_AS_TIMER		APCI3120_MODE_TIMER2_AS(0)
#define APCI3120_MODE_TIMER2_AS_COUNTER		APCI3120_MODE_TIMER2_AS(1)
#define APCI3120_MODE_TIMER2_AS_WDOG		APCI3120_MODE_TIMER2_AS(2)
#define APCI3120_MODE_TIMER2_AS_MASK		APCI3120_MODE_TIMER2_AS(3)
#define APCI3120_MODE_SCAN_ENA			BIT(3)
#define APCI3120_MODE_TIMER2_IRQ_ENA		BIT(2)
#define APCI3120_MODE_EOS_IRQ_ENA		BIT(1)
#define APCI3120_MODE_EOC_IRQ_ENA		BIT(0)
#define APCI3120_ADDON_ADDR_REG			0x00
#define APCI3120_ADDON_DATA_REG			0x02
#define APCI3120_ADDON_CTRL_REG			0x04
#define APCI3120_ADDON_CTRL_AMWEN_ENA		BIT(1)
#define APCI3120_ADDON_CTRL_A2P_FIFO_ENA	BIT(0)
#define APCI3120_REVA				0xa
#define APCI3120_REVB				0xb
#define APCI3120_REVA_OSC_BASE			70
#define APCI3120_REVB_OSC_BASE			50
#define AGCSTS_TC_ENABLE	0x10000000
#define AMCC_OP_REG_MCSR         0x3c
#define AMCC_OP_REG_AGCSTS        0x3c
#define RESET_A2P_FLAGS		0x04000000L
#define AMCC_OP_REG_AMWTC         0x58
#define AMCC_OP_REG_INTCSR       0x38
#define AGCSTS_RESET_A2P_FIFO	0x02000000
#define AMCC_OP_REG_AMWAR         0x24
#define AINT_WRITE_COMPL	0x00004000
#define EN_A2P_TRANSFERS	0x00000400
#define AMCC_OP_REG_MWTC         0x28
#define AINT_WT_COMPLETE	0x00040000
#define TARGET_ABORT_INT	0x00200000L
#define MASTER_ABORT_INT	0x00100000L
#define AINT_INT_MASK		0x00ff0000
#define ANY_S593X_INT		0x00800000L

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
    uint32_t intcsr;
    uint32_t mcsr;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t ctrl;
    uint16_t status;
    uint16_t timer_mode;
    uint8_t mode;
    uint8_t do_bits;
    uint16_t addon_addr;
    uint16_t addon_ctrl;

    /* DMA Context */
    uint32_t amwar;
    uint32_t amwtc;
    uint32_t mwtc;
};

enum apci3120_boardid {
	BOARD_APCI3120,
	BOARD_APCI3001,
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;
    
    if (s->status & APCI3120_STATUS_INT_MASK) {
        raise = true;
    }
    if (s->intcsr & ANY_S593X_INT) {
        raise = true;
    }
    
    if (raise) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->amwtc > 0) {
        uint32_t len = s->amwtc;
        uint8_t *buf = g_malloc0(len);
        pci_dma_write(pdev, s->amwar, buf, len);
        g_free(buf);
        
        s->mwtc = 0;
        
        s->intcsr |= ANY_S593X_INT;
        s->status |= APCI3120_STATUS_AMCC_INT;
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static uint64_t pcibase_amcc_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case AMCC_OP_REG_INTCSR:
        return s->intcsr;
    case AMCC_OP_REG_MCSR:
        return s->mcsr;
    case AMCC_OP_REG_MWTC:
        return s->mwtc;
    }
    return 0;
}

static void pcibase_amcc_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case AMCC_OP_REG_INTCSR:
        if (val & AINT_WT_COMPLETE) {
            s->intcsr &= ~ANY_S593X_INT;
            s->status &= ~APCI3120_STATUS_AMCC_INT;
            pcibase_update_irq(s);
        }
        s->intcsr = (s->intcsr & ~AINT_INT_MASK) | (val & AINT_INT_MASK);
        break;
    case AMCC_OP_REG_MCSR:
        s->mcsr = val;
        break;
    }
}

static uint64_t pcibase_apci3120_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case APCI3120_AI_FIFO_REG:
        return 0;
    case APCI3120_STATUS_REG:
        {
            uint16_t val = s->status;
            s->status &= ~(APCI3120_STATUS_EOS_INT | APCI3120_STATUS_EOC_INT);
            pcibase_update_irq(s);
            return val;
        }
    case APCI3120_TIMER_REG:
        return 0;
    case APCI3120_TIMER_MODE_REG:
        return s->timer_mode;
    case APCI3120_CTR0_REG:
        s->status &= ~APCI3120_STATUS_TIMER2_INT;
        pcibase_update_irq(s);
        return 0;
    }
    return 0;
}

static void pcibase_apci3120_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case APCI3120_CTRL_REG:
        s->ctrl = val;
        break;
    case APCI3120_TIMER_REG:
        break;
    case APCI3120_CHANLIST_REG:
        break;
    case APCI3120_TIMER_MODE_REG:
        s->timer_mode = val;
        break;
    case APCI3120_CTR0_REG:
        s->do_bits = (val >> 4) & 0xf;
        break;
    case APCI3120_MODE_REG:
        s->mode = val;
        if (s->mode & APCI3120_MODE_EOS_IRQ_ENA) {
            s->status |= APCI3120_STATUS_EOS_INT;
            pcibase_update_irq(s);
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_addon_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case APCI3120_ADDON_ADDR_REG:
        return s->addon_addr;
    case APCI3120_ADDON_CTRL_REG:
        return s->addon_ctrl;
    }
    return 0;
}

static void pcibase_addon_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case APCI3120_ADDON_ADDR_REG:
        s->addon_addr = val;
        break;
    case APCI3120_ADDON_DATA_REG:
        if (s->addon_addr == AMCC_OP_REG_AMWAR) {
            s->amwar = (s->amwar & 0xffff0000) | (val & 0xffff);
        } else if (s->addon_addr == AMCC_OP_REG_AMWAR + 2) {
            s->amwar = (s->amwar & 0xffff) | ((val & 0xffff) << 16);
        } else if (s->addon_addr == AMCC_OP_REG_AMWTC) {
            s->amwtc = (s->amwtc & 0xffff0000) | (val & 0xffff);
        } else if (s->addon_addr == AMCC_OP_REG_AMWTC + 2) {
            s->amwtc = (s->amwtc & 0xffff) | ((val & 0xffff) << 16);
        }
        break;
    case APCI3120_ADDON_CTRL_REG:
        s->addon_ctrl = val;
        if (val & APCI3120_ADDON_CTRL_AMWEN_ENA) {
            pcibase_do_dma(s, true);
        }
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_amcc_ops = {
    .read = pcibase_amcc_read,
    .write = pcibase_amcc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_apci3120_ops = {
    .read = pcibase_apci3120_read,
    .write = pcibase_apci3120_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_addon_ops = {
    .read = pcibase_addon_read,
    .write = pcibase_addon_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    
    s->status = APCI3120_STATUS_DA_READY | (APCI3120_REVA << 4);
    s->intcsr = 0;
    s->mcsr = 0;
    s->ctrl = 0;
    s->timer_mode = 0;
    s->mode = 0;
    s->do_bits = 0;
    s->addon_addr = 0;
    s->addon_ctrl = 0;
    s->amwar = 0;
    s->amwtc = 0;
    s->mwtc = 0;
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
        const MemoryRegionOps *ops = &pcibase_amcc_ops;
        if (bi->index == 1) ops = &pcibase_apci3120_ops;
        else if (bi->index == 2) ops = &pcibase_addon_ops;
        
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10e8 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x818d );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 256, .name = "amcc" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 256, .name = "apci3120" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 256, .name = "addon" };

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
    .name = "addi_apci_3120_pci",
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
