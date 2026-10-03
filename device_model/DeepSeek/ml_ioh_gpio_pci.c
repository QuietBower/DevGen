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
/* (none) */

#define TYPE_PCIBASE_DEVICE "ml_ioh_gpio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10DB
#define DEVICE_ID 0x802E
#define CLASS_ID  0x0c80 /* GPIO */

/* IRQ type bits from the driver */
#define IOH_EDGE_FALLING    0
#define IOH_EDGE_RISING     BIT(0)
#define IOH_LEVEL_L         BIT(1)
#define IOH_LEVEL_H         (BIT(0) | BIT(1))
#define IOH_EDGE_BOTH       BIT(2)
#define IOH_IM_MASK         (BIT(0) | BIT(1) | BIT(2))
#define IOH_IRQ_BASE        0

/* Number of GPIO pins per port, indexed by port 0..7 */
static const int num_ports[] = {6, 12, 16, 16, 15, 16, 16, 12};

/* Register offsets (BAR1 MMIO) */
enum {
    REG_IEN       = 0x00,
    REG_ISTATUS   = 0x04,
    REG_IDISP     = 0x08,
    REG_ICLR      = 0x0C,
    REG_IMASK     = 0x10,
    REG_IMASKCLR  = 0x14,
    REG_PO        = 0x18,
    REG_PI        = 0x1C,
    REG_PM        = 0x20,
    REG_IM_0      = 0x24,
    REG_IM_1      = 0x28,
    REG_RESERVED  = 0x2C,
    PORT_STRIDE   = 0x30,
    REG_SRST      = 0x1FC,
};

/* Hardware register layout structures */
struct ioh_regs_reg {
    uint32_t ien;
    uint32_t istatus;
    uint32_t idisp;
    uint32_t iclr;
    uint32_t imask;
    uint32_t imaskclr;
    uint32_t po;
    uint32_t pi;
    uint32_t pm;
    uint32_t im_0;
    uint32_t im_1;
    uint32_t reserved;
};

struct ioh_regs {
    struct ioh_regs_reg regs[8];
    uint32_t reserve1[16];
    uint32_t ioh_sel_reg[4];
    uint32_t reserve2[11];
    uint32_t srst;
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
    struct ioh_regs reg;

    /* DMA Context */
    /* (none) */

    /* (none) */
    /* (none) */
    /* (none) */
    /* (none) */
};

/* (none) */

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* IRQ not used, no need to raise */
}

/* MMIO read handler for BAR1 (size 0x200, 4-byte accesses only) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint32_t val = 0;

    if (addr >= 0x200 || size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return 0xFFFFFFFF;
    }

    /* Map flat offset to register */
    if (addr < 0x180) {
        int port = addr / PORT_STRIDE;       /* 0..7 */
        int reg_off = addr % PORT_STRIDE;    /* 0..0x2C */
        if (reg_off == REG_PI) {
            /* Compute input register: for each bit, if pm bit set, return po bit, else 0 */
            uint32_t po = s->reg.regs[port].po;
            uint32_t pm = s->reg.regs[port].pm;
            uint32_t pi_val = po & pm; /* output bits drive input */
            return pi_val;
        } else if (reg_off == REG_IMASKCLR) {
            /* imaskclr read: return 0 (write-only) */
            return 0;
        } else {
            /* Default: return shadow register */
            return *(uint32_t *)((uint8_t *)&s->reg + addr);
        }
    } else if (addr >= 0x180 && addr < 0x1C0) {
        /* Reserved area, return 0 */
        return 0;
    } else if (addr >= 0x1C0 && addr < 0x1D0) {
        /* ioh_sel_reg */
        int idx = (addr - 0x1C0) >> 2;
        return s->reg.ioh_sel_reg[idx];
    } else if (addr >= 0x1D0 && addr < 0x1FC) {
        /* Reserved */
        return 0;
    } else if (addr == 0x1FC) {
        return s->reg.srst;
    } else {
        return 0xFFFFFFFF;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);

    if (addr >= 0x200 || size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write addr=0x%" HWADDR_PRIx " size=%u val=0x%" PRIx64 "\n",
                      __func__, addr, size, val);
        return;
    }

    /* Map flat offset to register */
    if (addr < 0x180) {
        int port = addr / PORT_STRIDE;
        int reg_off = addr % PORT_STRIDE;
        switch (reg_off) {
        case REG_ICLR:
            /* Write-1-to-clear for interrupt status */
            s->reg.regs[port].istatus &= ~((uint32_t)val);
            break;
        case REG_IMASK:
            /* Set mask bits */
            s->reg.regs[port].imask |= (uint32_t)val;
            break;
        case REG_IMASKCLR:
            /* Clear mask bits */
            s->reg.regs[port].imask &= ~((uint32_t)val);
            break;
        default:
            /* Regular write */
            *(uint32_t *)((uint8_t *)&s->reg + addr) = (uint32_t)val;
            break;
        }
    } else if (addr >= 0x1C0 && addr < 0x1D0) {
        int idx = (addr - 0x1C0) >> 2;
        s->reg.ioh_sel_reg[idx] = (uint32_t)val;
    } else if (addr == 0x1FC) {
        s->reg.srst = (uint32_t)val;
    } else {
        /* Reserved areas: ignore writes */
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
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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
    memset(&s->reg, 0, sizeof(s->reg));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x10DB);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x802E);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c80);
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_NONE;
    s->bar_info[0].size = 0;
    s->bar_info[0].name = "bar0";
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x200;
    s->bar_info[1].name = "bar1-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X */
    /* No DMA */
    /* No timers */
    /* Final state initialization before the device is 'live' */
    memset(&s->reg, 0, sizeof(s->reg));
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
    .name = "ml_ioh_gpio_pci",
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
