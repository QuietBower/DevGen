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

#define TYPE_PCIBASE_DEVICE "pch_dma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI vendor and device IDs (first entry in pci_device_id table) */
#define PCI_VENDOR_ID_PCH_DMA      PCI_VENDOR_ID_INTEL   /* 0x8086 */
#define PCI_DEVICE_ID_PCH_DMA      0x8810               /* EG20T PCH DMA 8CH */
#define PCI_CLASS_PCH_DMA          0x0800               /* DMA controller */

/* Number of channels for the first device variant (8-channel) */
#define PCH_DMA_NUM_CHANNELS       8

/* Register offsets from the driver's pch_dma.c */
#define PCH_DMA_CTL0               0x00
#define PCH_DMA_CTL1               0x04
#define PCH_DMA_CTL2               0x08
#define PCH_DMA_CTL3               0x0C
#define PCH_DMA_STS0               0x10
#define PCH_DMA_STS1               0x14
#define PCH_DMA_STS2               0x18
#define PCH_DMA_RESERVED3          0x1C

/* Per-channel descriptor register offsets (relative to channel base) */
#define PDC_DEV_ADDR               0x00
#define PDC_MEM_ADDR               0x04
#define PDC_SIZE                   0x08
#define PDC_NEXT                   0x0C

/* DMA Control 0 (CTL0) bit fields */
#define DMA_CTL0_DISABLE           0x0
#define DMA_CTL0_SG                0x1
#define DMA_CTL0_ONESHOT           0x2
#define DMA_CTL0_MODE_MASK_BITS    0x3
#define DMA_CTL0_DIR_SHIFT_BITS    2
#define DMA_CTL0_BITS_PER_CH       4

/* DMA Control 2 (CTL2) bit fields */
#define DMA_CTL2_START_SHIFT_BITS  8
#define DMA_CTL2_IRQ_ENABLE_MASK   ((1UL << DMA_CTL2_START_SHIFT_BITS) - 1)

/* DMA Status bit fields */
#define DMA_STATUS_IDLE            0x0
#define DMA_STATUS_DESC_READ       0x1
#define DMA_STATUS_WAIT             0x2
#define DMA_STATUS_ACCESS          0x3
#define DMA_STATUS_BITS_PER_CH     2
#define DMA_STATUS_MASK_BITS       0x3
#define DMA_STATUS_SHIFT_BITS      16
#define DMA_STATUS_IRQ(x)          (0x1 << (x))
#define DMA_STATUS0_ERR(x)         (0x1 << ((x) + 8))
#define DMA_STATUS2_ERR(x)         (0x1 << (x))

/* Compute the size of the MMIO register region */
#define PCH_DMA_REG_SIZE           (0x20 + PCH_DMA_NUM_CHANNELS * 0x10)

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
    uint32_t irq_status;        /* Interrupt status register */

    /* Hardware Register Shadows */
    struct {
        uint32_t ctl0;
        uint32_t ctl1;
        uint32_t ctl2;
        uint32_t ctl3;
        uint32_t sts0;
        uint32_t sts1;
        uint32_t sts2;
        uint32_t reserved3;
        struct {
            uint32_t dev_addr;
            uint32_t mem_addr;
            uint32_t size;
            uint32_t next;
        } ch[PCH_DMA_NUM_CHANNELS];
    } regs;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* IRQ is raised if any channel 0-7 has both sts0 IRQ bit and ctl2 enable bit set */
    bool raise = (s->regs.sts0 & s->regs.ctl2 & 0xFF) != 0;
    pci_set_irq(pdev, raise ? 1 : 0);
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return 0;
    }

    if (addr >= 0x00 && addr <= 0x1C) {
        switch (addr) {
        case PCH_DMA_CTL0:
            val = s->regs.ctl0;
            break;
        case PCH_DMA_CTL1:
            val = s->regs.ctl1;
            break;
        case PCH_DMA_CTL2:
            val = s->regs.ctl2;
            break;
        case PCH_DMA_CTL3:
            val = s->regs.ctl3;
            break;
        case PCH_DMA_STS0:
            val = s->regs.sts0;
            break;
        case PCH_DMA_STS1:
            val = s->regs.sts1;
            break;
        case PCH_DMA_STS2:
            val = s->regs.sts2;
            break;
        case PCH_DMA_RESERVED3:
            val = s->regs.reserved3;
            break;
        }
    } else if (addr >= 0x20 && addr < PCH_DMA_REG_SIZE) {
        int ch = (addr - 0x20) / 0x10;
        int reg = (addr - 0x20) % 0x10;
        if (ch < PCH_DMA_NUM_CHANNELS) {
            switch (reg) {
            case PDC_DEV_ADDR:
                val = s->regs.ch[ch].dev_addr;
                break;
            case PDC_MEM_ADDR:
                val = s->regs.ch[ch].mem_addr;
                break;
            case PDC_SIZE:
                val = s->regs.ch[ch].size;
                break;
            case PDC_NEXT:
                val = s->regs.ch[ch].next;
                break;
            }
        }
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    if (addr >= 0x00 && addr <= 0x1C) {
        switch (addr) {
        case PCH_DMA_CTL0:
            s->regs.ctl0 = val;
            break;
        case PCH_DMA_CTL1:
            s->regs.ctl1 = val;
            break;
        case PCH_DMA_CTL2:
            s->regs.ctl2 = val;
            break;
        case PCH_DMA_CTL3:
            s->regs.ctl3 = val;
            break;
        case PCH_DMA_STS0:
            /* Write-1-to-clear for IRQ and error bits (bits 0-15), ignore state bits */
            s->regs.sts0 &= ~(val & 0x0000FFFF);
            pcibase_update_irq(s);
            break;
        case PCH_DMA_STS1:
            s->regs.sts1 = val;
            break;
        case PCH_DMA_STS2:
            /* Similarly, clear lower 16 bits for channels 8-11 (not used in 8-channel) */
            s->regs.sts2 &= ~(val & 0x0000FFFF);
            pcibase_update_irq(s);
            break;
        case PCH_DMA_RESERVED3:
            s->regs.reserved3 = val;
            break;
        }
    } else if (addr >= 0x20 && addr < PCH_DMA_REG_SIZE) {
        int ch = (addr - 0x20) / 0x10;
        int reg = (addr - 0x20) % 0x10;
        if (ch < PCH_DMA_NUM_CHANNELS) {
            switch (reg) {
            case PDC_DEV_ADDR:
                s->regs.ch[ch].dev_addr = val;
                break;
            case PDC_MEM_ADDR:
                s->regs.ch[ch].mem_addr = val;
                break;
            case PDC_SIZE:
                s->regs.ch[ch].size = val;
                break;
            case PDC_NEXT:
                s->regs.ch[ch].next = val;
                break;
            }
        }
    }
}

/* PIO Handlers (not used by this driver) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xFFFFFFFF;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No-op */
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
    /* Reset all registers to zero */
    memset(&s->regs, 0, sizeof(s->regs));
    s->irq_status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_PCH_DMA);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PCH_DMA);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_PCH_DMA);
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
    s->bar_info[0].index = 1;  /* Driver uses BAR1 */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PCH_DMA_REG_SIZE;
    s->bar_info[0].name = "pch-dma-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA configuration: no internal hardware timers */
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
    /* Nothing else to clean up */
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
