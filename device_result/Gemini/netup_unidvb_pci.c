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

#define TYPE_PCIBASE_DEVICE "netup_unidvb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NETUP 0x1b55
#define PCI_DEVICE_ID_NETUP_UNIDVB 0x18f6
#define NETUP_PCI_DEV_REVISION 0x2

#define AVL_PCIE_IENR		0x50
#define AVL_PCIE_ISR		0x40
#define AVL_IRQ_ENABLE		0x80
#define AVL_IRQ_ASSERTED	0x80
#define GPIO_REG_IO		0x4880
#define GPIO_REG_IO_TOGGLE	0x4882
#define GPIO_REG_IO_SET		0x4884
#define GPIO_REG_IO_CLEAR	0x4886
#define GPIO_FEA_RESET		(1 << 0)
#define GPIO_FEB_RESET		(1 << 1)
#define GPIO_RFA_CTL		(1 << 2)
#define GPIO_RFB_CTL		(1 << 3)
#define GPIO_FEA_TU_RESET	(1 << 4)
#define GPIO_FEB_TU_RESET	(1 << 5)
#define NETUP_DMA0_ADDR		0x4900
#define NETUP_DMA1_ADDR		0x4940
#define NETUP_DMA_BLOCKS_COUNT	8
#define NETUP_DMA_PACKETS_COUNT	128
#define BIT_DMA_RUN		1
#define BIT_DMA_ERROR		2
#define BIT_DMA_IRQ		0x200
#define REG_IMASK_SET		0x4894
#define REG_IMASK_CLEAR		0x4896
#define NETUP_UNIDVB_IRQ_DMA2	(1 << 9)
#define NETUP_UNIDVB_IRQ_DMA1	(1 << 8)
#define REG_ISR			0x4890
#define NETUP_UNIDVB_IRQ_I2C1	(1 << 2)
#define NETUP_UNIDVB_IRQ_SPI	(1 << 0)
#define NETUP_UNIDVB_IRQ_CI	(1 << 10)
#define NETUP_UNIDVB_IRQ_I2C0	(1 << 1)

struct netup_i2c_fifo_regs {
    union {
        uint8_t  data8;
        uint16_t data16;
        uint32_t data32;
    };
    uint8_t  padding[4];
    uint16_t stat_ctrl;
};

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
    uint32_t avl_pcie_ienr;
    uint32_t avl_pcie_isr;
    uint16_t gpio_reg_io;

    /* DMA Context */
    struct {
        uint32_t ctrlstat_set;
        uint32_t ctrlstat_clear;
        uint32_t start_addr_lo;
        uint32_t start_addr_hi;
        uint32_t size;
        uint32_t timeout;
        uint32_t curr_addr_lo;
        uint32_t curr_addr_hi;
        uint32_t stat_pkt_received;
        uint32_t stat_pkt_accepted;
        uint32_t stat_pkt_overruns;
        uint32_t stat_pkt_underruns;
        uint32_t stat_fifo_overruns;
    } dma_regs[2];
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool should_assert = false;

    if ((s->intr_status & s->intr_mask) != 0) {
        s->avl_pcie_isr |= AVL_IRQ_ASSERTED;
    } else {
        s->avl_pcie_isr &= ~AVL_IRQ_ASSERTED;
    }

    if ((s->avl_pcie_isr & AVL_IRQ_ASSERTED) && (s->avl_pcie_ienr & AVL_IRQ_ENABLE)) {
        should_assert = true;
    }

    pci_set_irq(pdev, should_assert ? 1 : 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA logic requires struct netup_dma_regs layout to map registers correctly */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= NETUP_DMA0_ADDR && addr < NETUP_DMA0_ADDR + sizeof(s->dma_regs[0])) {
        uint32_t *regs = (uint32_t *)&s->dma_regs[0];
        return regs[(addr - NETUP_DMA0_ADDR) / 4];
    } else if (addr >= NETUP_DMA1_ADDR && addr < NETUP_DMA1_ADDR + sizeof(s->dma_regs[1])) {
        uint32_t *regs = (uint32_t *)&s->dma_regs[1];
        return regs[(addr - NETUP_DMA1_ADDR) / 4];
    }

    switch (addr) {
    case AVL_PCIE_ISR:
        val = s->avl_pcie_isr;
        break;
    case AVL_PCIE_IENR:
        val = s->avl_pcie_ienr;
        break;
    case GPIO_REG_IO:
        val = s->gpio_reg_io;
        break;
    case REG_ISR:
        val = s->intr_status & s->intr_mask;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= NETUP_DMA0_ADDR && addr < NETUP_DMA0_ADDR + sizeof(s->dma_regs[0])) {
        uint32_t *regs = (uint32_t *)&s->dma_regs[0];
        regs[(addr - NETUP_DMA0_ADDR) / 4] = val;
        return;
    } else if (addr >= NETUP_DMA1_ADDR && addr < NETUP_DMA1_ADDR + sizeof(s->dma_regs[1])) {
        uint32_t *regs = (uint32_t *)&s->dma_regs[1];
        regs[(addr - NETUP_DMA1_ADDR) / 4] = val;
        return;
    }

    switch (addr) {
    case AVL_PCIE_IENR:
        s->avl_pcie_ienr = val;
        pcibase_update_irq(s);
        break;
    case GPIO_REG_IO:
        s->gpio_reg_io = val;
        break;
    case REG_IMASK_SET:
        s->intr_mask |= val;
        pcibase_update_irq(s);
        break;
    case REG_IMASK_CLEAR:
        s->intr_mask &= ~val;
        pcibase_update_irq(s);
        break;
    case 0x1000:
        /* High address register written during DMA init */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
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
    
    s->avl_pcie_ienr = 0;
    s->avl_pcie_isr = 0;
    s->gpio_reg_io = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
}

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1b55 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x18f6 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, NETUP_PCI_DEV_REVISION);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100000;
    s->bar_info[0].name = "netup_unidvb_bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x100000;
    s->bar_info[1].name = "netup_unidvb_bar1";
      
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
    .name = "netup_unidvb_pci",
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
