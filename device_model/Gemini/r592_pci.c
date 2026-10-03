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


#define TYPE_PCIBASE_DEVICE "r592_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define R592_TPC_EXEC                  0x00
#define R592_STATUS                    0x10
#define R592_IO                        0x18
#define R592_POWER                     0x20
#define R592_IO_MODE                   0x24
#define R592_REG_MSC                   0x28
#define R592_FIFO_DMA                  0x2C
#define R592_FIFO_PIO                  0x30
#define R592_FIFO_DMA_SETTINGS         0x34

#define R592_REG_MSC_IRQ_INSERT        (1 << 8)
#define R592_REG_MSC_IRQ_REMOVE        (1 << 9)
#define R592_REG_MSC_FIFO_EMPTY        (1 << 10)
#define R592_REG_MSC_FIFO_DMA_DONE     (1 << 11)
#define R592_REG_MSC_FIFO_DMA_ERR      (1 << 14)
#define DMA_IRQ_ACK_MASK               (R592_REG_MSC_FIFO_DMA_DONE | R592_REG_MSC_FIFO_DMA_ERR)
#define DMA_IRQ_EN_MASK                (DMA_IRQ_ACK_MASK << 16)
#define R592_REG_MSC_PRSNT             (1 << 1)
#define R592_REG_MSC_LED               (1 << 15)
#define R592_FIFO_DMA_SETTINGS_EN      (1 << 0)
#define R592_FIFO_DMA_SETTINGS_DIR     (1 << 1)
#define R592_FIFO_DMA_SETTINGS_CAP     (1 << 24)
#define R592_TPC_EXEC_LEN_SHIFT        16
#define R592_TPC_EXEC_TPC_SHIFT        28
#define R592_TPC_EXEC_BIG_FIFO         (1 << 26)
#define R592_STATUS_RDY                (1 << 28)
#define R592_STATUS_CED                (1 << 29)
#define R592_STATUS_P_CMDNACK          (1 << 16)
#define R592_STATUS_P_BREQ             (1 << 17)
#define R592_STATUS_P_INTERR           (1 << 18)
#define R592_STATUS_P_CED              (1 << 19)
#define R592_IO_DIRECTION              (1 << 24)
#define R592_LFIFO_SIZE                512

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t tpc_exec;
    uint32_t status;
    uint32_t io;
    uint32_t power;
    uint32_t io_mode;
    uint32_t reg_msc;
    uint32_t fifo_dma;
    uint32_t fifo_pio;
    uint32_t fifo_dma_settings;

    /* DMA Context */
    dma_addr_t dma_address;
    uint32_t dma_length;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t irq_enable = (s->reg_msc >> 16) & 0xFFFF;
    uint16_t irq_status = s->reg_msc & 0xFFFF;

    if (irq_enable & irq_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_dev_to_mem)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t buf[R592_LFIFO_SIZE];
    memset(buf, 0, sizeof(buf));

    if (is_dev_to_mem) {
        pci_dma_write(pdev, s->fifo_dma, buf, R592_LFIFO_SIZE);
    } else {
        pci_dma_read(pdev, s->fifo_dma, buf, R592_LFIFO_SIZE);
    }

    s->fifo_dma_settings &= ~R592_FIFO_DMA_SETTINGS_EN;
    s->reg_msc |= R592_REG_MSC_FIFO_DMA_DONE;
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case R592_TPC_EXEC:
        val = s->tpc_exec;
        break;
    case R592_STATUS:
        val = s->status;
        break;
    case R592_IO:
        val = s->io;
        break;
    case R592_POWER:
        val = s->power;
        break;
    case R592_IO_MODE:
        val = s->io_mode;
        break;
    case R592_REG_MSC:
        val = s->reg_msc;
        break;
    case R592_FIFO_DMA:
        val = s->fifo_dma;
        break;
    case R592_FIFO_PIO:
        val = s->fifo_pio;
        break;
    case R592_FIFO_DMA_SETTINGS:
        val = s->fifo_dma_settings;
        break;
    default:
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t ro_mask;

    switch (addr) {
    case R592_TPC_EXEC:
        s->tpc_exec = val;
        s->status |= (R592_STATUS_RDY | R592_STATUS_CED);
        break;
    case R592_STATUS:
        s->status = val;
        break;
    case R592_IO:
        s->io = val;
        break;
    case R592_POWER:
        s->power = val;
        break;
    case R592_IO_MODE:
        s->io_mode = val;
        break;
    case R592_REG_MSC:
        ro_mask = R592_REG_MSC_PRSNT | R592_REG_MSC_FIFO_EMPTY;
        s->reg_msc = (val & ~ro_mask) | (s->reg_msc & ro_mask);
        pcibase_update_irq(s);
        break;
    case R592_FIFO_DMA:
        s->fifo_dma = val;
        break;
    case R592_FIFO_PIO:
        s->fifo_pio = val;
        break;
    case R592_FIFO_DMA_SETTINGS:
        s->fifo_dma_settings = val;
        if (val & R592_FIFO_DMA_SETTINGS_EN) {
            bool is_dev_to_mem = (val & R592_FIFO_DMA_SETTINGS_DIR) != 0;
            pcibase_do_dma(s, is_dev_to_mem);
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
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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

    s->tpc_exec = 0;
    s->status = R592_STATUS_RDY | R592_STATUS_CED;
    s->io = 0;
    s->power = 0;
    s->io_mode = 0;
    s->reg_msc = R592_REG_MSC_PRSNT | R592_REG_MSC_FIFO_EMPTY;
    s->fifo_dma = 0;
    s->fifo_pio = 0;
    s->fifo_dma_settings = R592_FIFO_DMA_SETTINGS_CAP;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1180 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0592 );
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
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* Default fallback size */
    s->bar_info[0].name = "r592-mmio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

     /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "r592_pci",
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
