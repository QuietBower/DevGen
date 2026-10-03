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


#define TYPE_PCIBASE_DEVICE "r852_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define R852_DATALINE       0x00
#define R852_CTL            0x04
#define R852_CARD_STA       0x05
#define R852_CARD_IRQ_STA   0x06
#define R852_CARD_IRQ_ENABLE 0x07
#define R852_HW             0x08
#define R852_DMA_CAP        0x09
#define R852_DMA_ADDR       0x0C
#define R852_DMA_SETTINGS   0x10
#define R852_DMA_IRQ_STA    0x14
#define R852_DMA_IRQ_ENABLE 0x18

#define R852_DMA_LEN        512

#define R852_CARD_STA_PRESENT	0x04
#define R852_HW_UNKNOWN		0x80
#define R852_DMA1		0x40
#define R852_DMA2		0x80
#define R852_SMBIT		0x20
#define R852_DMA_MEMORY		0x01
#define R852_DMA_READ		0x02
#define R852_DMA_INTERNAL	0x04
#define R852_DMA_IRQ_MEMORY	0x01
#define R852_DMA_IRQ_INTERNAL	0x04

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
    uint8_t card_irq_sta;
    uint8_t card_irq_enable;
    uint32_t dma_irq_sta;
    uint32_t dma_irq_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t dataline;
    uint8_t ctl;
    uint8_t card_sta;
    uint8_t hw;
    uint8_t dma_cap;

    /* DMA Context */
    uint32_t dma_addr;
    uint32_t dma_settings;

};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    bool card_irq = (s->card_irq_sta & s->card_irq_enable) != 0;
    bool dma_irq = (s->dma_irq_sta & s->dma_irq_enable) != 0;
    
    if (card_irq || dma_irq) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t buf[R852_DMA_LEN] = {0};

    if (s->dma_settings & R852_DMA_MEMORY) {
        if (s->dma_settings & R852_DMA_READ) {
            /* Device to Memory */
            pci_dma_write(pdev, s->dma_addr, buf, R852_DMA_LEN);
        } else {
            /* Memory to Device */
            pci_dma_read(pdev, s->dma_addr, buf, R852_DMA_LEN);
        }
        s->dma_irq_sta |= R852_DMA_IRQ_MEMORY;
        pcibase_update_irq(s);
    }

    if (s->dma_settings & R852_DMA_INTERNAL) {
        s->dma_irq_sta |= R852_DMA_IRQ_INTERNAL;
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case R852_DATALINE: val = s->dataline; break;
        case R852_CTL: val = s->ctl; break;
        case R852_CARD_STA: val = s->card_sta; break;
        case R852_CARD_IRQ_STA: val = s->card_irq_sta; break;
        case R852_CARD_IRQ_ENABLE: val = s->card_irq_enable; break;
        case R852_HW: val = s->hw; break;
        case R852_DMA_CAP: val = s->dma_cap; break;
        case R852_DMA_ADDR: val = s->dma_addr; break;
        case R852_DMA_SETTINGS: val = s->dma_settings; break;
        case R852_DMA_IRQ_STA: val = s->dma_irq_sta; break;
        case R852_DMA_IRQ_ENABLE: val = s->dma_irq_enable; break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
        case R852_DATALINE: 
            s->dataline = val; 
            break;
        case R852_CTL: 
            s->ctl = val; 
            break;
        case R852_CARD_IRQ_STA: 
            s->card_irq_sta &= ~val; /* W1C */
            pcibase_update_irq(s); 
            break;
        case R852_CARD_IRQ_ENABLE: 
            s->card_irq_enable = val; 
            pcibase_update_irq(s); 
            break;
        case R852_HW: 
            s->hw = val; 
            break;
        case R852_DMA_ADDR: 
            s->dma_addr = val; 
            break;
        case R852_DMA_SETTINGS: 
            s->dma_settings = val; 
            pcibase_do_dma(s, false); 
            break;
        case R852_DMA_IRQ_STA: 
            s->dma_irq_sta &= ~val; /* W1C */
            pcibase_update_irq(s); 
            break;
        case R852_DMA_IRQ_ENABLE: 
            s->dma_irq_enable = val; 
            pcibase_update_irq(s); 
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

    s->dataline = 0;
    s->ctl = 0;
    s->card_sta = R852_CARD_STA_PRESENT;
    s->card_irq_sta = 0;
    s->card_irq_enable = 0;
    s->hw = R852_HW_UNKNOWN;
    s->dma_cap = R852_DMA1 | R852_DMA2 | R852_SMBIT;
    s->dma_addr = 0;
    s->dma_settings = 0;
    s->dma_irq_sta = 0;
    s->dma_irq_enable = 0;
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0852 );
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
    s->bar_info[0].size = 0x100; /* Size not explicitly defined in current source, using 256 bytes */
    s->bar_info[0].name = "r852-bar0";

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
    .name = "r852_pci",
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
