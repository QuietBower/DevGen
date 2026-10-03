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
#define DMA_CTL0_MODE_MASK_BITS		0x3
#define DMA_CTL0_BITS_PER_CH		4
#define DMA_MASK_CTL0_MODE	0x33333333
#define DMA_CTL0_DIR_SHIFT_BITS		2
#define DMA_MASK_CTL2_MODE	0x00003333
#define DMA_STATUS_MASK_BITS		0x3
#define DMA_STATUS_SHIFT_BITS		16
#define DMA_STATUS_BITS_PER_CH		2
#define DMA_STATUS_IDLE			0x0
#define DMA_CTL0_ONESHOT		0x2
#define DMA_CTL0_SG			0x1
#define DMA_CTL0_DISABLE		0x0
#define DMA_STATUS_IRQ(x)		(0x1 << (x))
#define DMA_STATUS0_ERR(x)		(0x1 << ((x) + 8))
#define DMA_STATUS2_ERR(x)		(0x1 << (x))
#define DMA_DESC_FOLLOW_WITHOUT_IRQ	0x2
#define DMA_DESC_END_WITH_IRQ		0x1
#define DMA_DESC_END_WITHOUT_IRQ	0x0
#define DMA_DESC_MAX_COUNT_1_BYTE	0x3FF
#define DMA_DESC_WIDTH_SHIFT_BITS	12
#define DMA_DESC_WIDTH_1_BYTE		(0x3 << DMA_DESC_WIDTH_SHIFT_BITS)
#define DMA_DESC_MAX_COUNT_2_BYTES	0x3FF
#define DMA_DESC_WIDTH_2_BYTES		(0x2 << DMA_DESC_WIDTH_SHIFT_BITS)
#define DMA_DESC_MAX_COUNT_4_BYTES	0x7FF
#define DMA_DESC_WIDTH_4_BYTES		(0x0 << DMA_DESC_WIDTH_SHIFT_BITS)

#define TYPE_PCIBASE_DEVICE "pch_dma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_EG20T_PCH_DMA_8CH 0x8810
#define MAX_CHAN_NR 12

#define PCH_DMA_CTL0 0x00
#define PCH_DMA_CTL1 0x04
#define PCH_DMA_CTL2 0x08
#define PCH_DMA_CTL3 0x0C
#define PCH_DMA_STS0 0x10
#define PCH_DMA_STS1 0x14
#define PCH_DMA_STS2 0x18

#define PDC_DEV_ADDR 0x00
#define PDC_MEM_ADDR 0x04
#define PDC_SIZE     0x08
#define PDC_NEXT     0x0C

struct pch_dma_desc_regs {
    uint32_t dev_addr;
    uint32_t mem_addr;
    uint32_t size;
    uint32_t next;
};

struct pch_dma_regs {
    uint32_t dma_ctl0;
    uint32_t dma_ctl1;
    uint32_t dma_ctl2;
    uint32_t dma_ctl3;
    uint32_t dma_sts0;
    uint32_t dma_sts1;
    uint32_t dma_sts2;
    uint32_t reserved3;
    struct pch_dma_desc_regs desc[MAX_CHAN_NR];
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
    uint32_t dma_ctl0;
    uint32_t dma_ctl1;
    uint32_t dma_ctl2;
    uint32_t dma_ctl3;
    uint32_t dma_sts0;
    uint32_t dma_sts1;
    uint32_t dma_sts2;

    /* DMA Context */
    struct pch_dma_desc_regs ch_regs[MAX_CHAN_NR];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = false;

    if (s->dma_sts0 || s->dma_sts2) {
        level = true;
    }

    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    for (int ch = 0; ch < MAX_CHAN_NR; ch++) {
        uint32_t mode = 0;
        if (ch < 8) {
            mode = (s->dma_ctl0 >> (DMA_CTL0_BITS_PER_CH * ch)) & DMA_CTL0_MODE_MASK_BITS;
        } else {
            mode = (s->dma_ctl3 >> (DMA_CTL0_BITS_PER_CH * (ch - 8))) & DMA_CTL0_MODE_MASK_BITS;
        }

        if (mode == DMA_CTL0_ONESHOT || mode == DMA_CTL0_SG) {
            /* Simulate immediate DMA completion */
            if (ch < 8) {
                s->dma_ctl0 &= ~(DMA_CTL0_MODE_MASK_BITS << (DMA_CTL0_BITS_PER_CH * ch));
                s->dma_ctl0 |= (DMA_CTL0_DISABLE << (DMA_CTL0_BITS_PER_CH * ch));
                
                uint32_t shift = DMA_STATUS_SHIFT_BITS + DMA_STATUS_BITS_PER_CH * ch;
                s->dma_sts0 &= ~(DMA_STATUS_MASK_BITS << shift);
                s->dma_sts0 |= (DMA_STATUS_IDLE & DMA_STATUS_MASK_BITS) << shift;
                s->dma_sts0 |= DMA_STATUS_IRQ(ch);
            } else {
                s->dma_ctl3 &= ~(DMA_CTL0_MODE_MASK_BITS << (DMA_CTL0_BITS_PER_CH * (ch - 8)));
                s->dma_ctl3 |= (DMA_CTL0_DISABLE << (DMA_CTL0_BITS_PER_CH * (ch - 8)));
                
                uint32_t shift = DMA_STATUS_SHIFT_BITS + DMA_STATUS_BITS_PER_CH * (ch - 8);
                s->dma_sts2 &= ~(DMA_STATUS_MASK_BITS << shift);
                s->dma_sts2 |= (DMA_STATUS_IDLE & DMA_STATUS_MASK_BITS) << shift;
                s->dma_sts2 |= DMA_STATUS_IRQ(ch - 8);
            }
        }
    }
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case PCH_DMA_CTL0: val = s->dma_ctl0; break;
        case PCH_DMA_CTL1: val = s->dma_ctl1; break;
        case PCH_DMA_CTL2: val = s->dma_ctl2; break;
        case PCH_DMA_CTL3: val = s->dma_ctl3; break;
        case PCH_DMA_STS0: val = s->dma_sts0; break;
        case PCH_DMA_STS1: val = s->dma_sts1; break;
        case PCH_DMA_STS2: val = s->dma_sts2; break;
        default:
            if (addr >= 0x20 && addr < 0x20 + MAX_CHAN_NR * sizeof(struct pch_dma_desc_regs)) {
                int ch = (addr - 0x20) / sizeof(struct pch_dma_desc_regs);
                int reg = (addr - 0x20) % sizeof(struct pch_dma_desc_regs);
                switch (reg) {
                    case PDC_DEV_ADDR: val = s->ch_regs[ch].dev_addr; break;
                    case PDC_MEM_ADDR: val = s->ch_regs[ch].mem_addr; break;
                    case PDC_SIZE: val = s->ch_regs[ch].size; break;
                    case PDC_NEXT: val = s->ch_regs[ch].next; break;
                }
            }
            break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
        case PCH_DMA_CTL0: 
            s->dma_ctl0 = val; 
            pcibase_do_dma(s, true); 
            break;
        case PCH_DMA_CTL1: 
            s->dma_ctl1 = val; 
            break;
        case PCH_DMA_CTL2: 
            s->dma_ctl2 = val; 
            pcibase_update_irq(s); 
            break;
        case PCH_DMA_CTL3: 
            s->dma_ctl3 = val; 
            pcibase_do_dma(s, true); 
            break;
        case PCH_DMA_STS0: 
            s->dma_sts0 &= ~val; /* W1C */
            pcibase_update_irq(s); 
            break;
        case PCH_DMA_STS1: 
            s->dma_sts1 &= ~val; 
            pcibase_update_irq(s); 
            break;
        case PCH_DMA_STS2: 
            s->dma_sts2 &= ~val; 
            pcibase_update_irq(s); 
            break;
        default:
            if (addr >= 0x20 && addr < 0x20 + MAX_CHAN_NR * sizeof(struct pch_dma_desc_regs)) {
                int ch = (addr - 0x20) / sizeof(struct pch_dma_desc_regs);
                int reg = (addr - 0x20) % sizeof(struct pch_dma_desc_regs);
                switch (reg) {
                    case PDC_DEV_ADDR: s->ch_regs[ch].dev_addr = val; break;
                    case PDC_MEM_ADDR: s->ch_regs[ch].mem_addr = val; break;
                    case PDC_SIZE: s->ch_regs[ch].size = val; break;
                    case PDC_NEXT: s->ch_regs[ch].next = val; break;
                }
            }
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

    s->dma_ctl0 = 0;
    s->dma_ctl1 = 0;
    s->dma_ctl2 = 0;
    s->dma_ctl3 = 0;
    s->dma_sts0 = 0;
    s->dma_sts1 = 0;
    s->dma_sts2 = 0;
    for (int i = 0; i < MAX_CHAN_NR; i++) {
        s->ch_regs[i].dev_addr = 0;
        s->ch_regs[i].mem_addr = 0;
        s->ch_regs[i].size = 0;
        s->ch_regs[i].next = 0;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_EG20T_PCH_DMA_8CH );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SYSTEM_DMA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = sizeof(struct pch_dma_regs);
    s->bar_info[1].name = "pch_dma_mmio";
      
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
    .name = "pch_dma_pci",
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
