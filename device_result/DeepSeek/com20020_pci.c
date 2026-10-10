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

#define TYPE_PCIBASE_DEVICE "com20020_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID       0x1571
#define DEVICE_ID       0xA001
#define CLASS_ID        0x0280   /* Network controller: other */

#define COM20020_REG_R_STATUS   0
#define COM20020_REG_R_DIAGSTAT 1
#define COM20020_REG_W_COMMAND  1
#define COM20020_REG_W_INTMASK  0
#define COM20020_REG_W_CONFIG   6
#define COM20020_REG_W_XREG     7
#define COM20020_REG_W_ADDR_LO  3
#define COM20020_REG_W_ADDR_HI  2
#define COM20020_REG_W_SUBADR   5
#define COM20020_REG_RW_MEMDATA 4

#define XTOcfg(x)   ((x) << 3)
#define P1MODE      0x80
#define SUB_SETUP1  2
#define SUB_NODE    1
#define SUB_SETUP2  4

/* Corrected constants from driver source */
#define NORXflag     0x80
#define TXFREEflag   0x01
#define RESETflag    0x10
#define CFLAGScmd    0x06
#define RESETclear   0x08
#define CONFIGclear  0x10
#define STARTIOcmd   0x18
#define RDDATAflag   0x80
#define AUTOINCflag  0x40
#define TESTvalue    0xD1
#define RESETcfg     0x80

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

typedef struct COM20020Regs {
    uint8_t status;    /* offset 0 read */
    uint8_t intmask;   /* offset 0 write */
    uint8_t diagstat;  /* offset 1 read */
    uint8_t command;   /* offset 1 write */
    uint8_t addr_hi;   /* offset 2 write */
    uint8_t addr_lo;   /* offset 3 write */
    uint8_t memdata;   /* offset 4 RW */
    uint8_t subaddr;   /* offset 5 write */
    uint8_t config;    /* offset 6 write */
    uint8_t xreg;      /* offset 7 write */
} COM20020Regs;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint8_t intr_status;
    uint8_t intr_mask;

    COM20020Regs regs;

    /* Extended state for subaddress paging */
    uint8_t subpage;          /* current config[2:0] subpage index */
    uint8_t xreg_setup1;      /* XREG when subpage = SUB_SETUP1 */
    uint8_t xreg_setup2;      /* XREG when subpage = SUB_SETUP2 */
    uint8_t xreg_node;        /* XREG when subpage = SUB_NODE */
    uint8_t xreg_other[4];    /* mappings for subpages 0,3,5,6,7 */

    /* Internal RAM buffer (2KB) and memory access state */
    uint8_t ram[2048];
    uint16_t memaddr;

    /* Node ID for PROM offset 8 */
    uint8_t node_id;

    /* Reset state tracking */
    bool reset_active;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t active = s->regs.status & s->regs.intmask;
    if (active) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

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

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case 0:
            val = s->regs.status;
            break;
        case 1:
            val = s->regs.diagstat;
            break;
        case 4:
            /* Memory data read: read from RAM at current address,
               then auto-increment if AUTOINCflag set */
            if (s->regs.addr_hi & AUTOINCflag) {
                val = s->ram[s->memaddr];
                s->memaddr++;
            } else {
                val = s->ram[s->memaddr];
            }
            break;
        case 7:
            /* XREG read, dependent on subpage */
            switch (s->subpage) {
                case SUB_SETUP1:
                    val = s->xreg_setup1;
                    break;
                case SUB_SETUP2:
                    val = s->xreg_setup2;
                    break;
                case SUB_NODE:
                    val = s->xreg_node;
                    break;
                default:
                    val = s->xreg_other[s->subpage];
                    break;
            }
            break;
        case 8:
            /* PROM byte: node address */
            val = s->node_id;
            break;
        default:
            break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
        case 0:
            s->regs.intmask = (uint8_t)val;
            pcibase_update_irq(s);
            break;
        case 1:
            s->regs.command = (uint8_t)val;
            /* Handle command processing */
            if (val & CFLAGScmd) {
                if (val & RESETclear) {
                    s->regs.status &= ~RESETflag;
                }
                if (val & CONFIGclear) {
                    /* nothing specific for now */
                }
            }
            /* STARTIOcmd could trigger an action, but for now ignore */
            break;
        case 2:
            s->regs.addr_hi = (uint8_t)val;
            break;
        case 3:
            s->regs.addr_lo = (uint8_t)val;
            /* Update memory address counter: addr_hi contains upper bits and flags,
               addr_lo contains lower 8 bits. Typically address is 11 bits (2KB).
               We'll form the physical address from the actual address bits.
               In COM20020, addr_hi bits 4-0 are A8..A4, addr_lo is A3..A0. 
               Assume addr_hi low 5 bits are address[8:4] (after masking flags),
               addr_lo is address[3:0]. */
            s->memaddr = ((val & 0x0F) | ((s->regs.addr_hi & 0x1F) << 4)) & 0x7FF;
            break;
        case 4:
            /* Memory data write: write to RAM, then auto-increment */
            s->ram[s->memaddr] = (uint8_t)val;
            if (s->regs.addr_hi & AUTOINCflag) {
                s->memaddr = (s->memaddr + 1) & 0x7FF;
            }
            break;
        case 5:
            s->regs.subaddr = (uint8_t)val;
            break;
        case 6:
            s->regs.config = (uint8_t)val;
            /* Update subpage from lower 3 bits */
            s->subpage = val & 0x07;
            /* Reset logic: if RESETcfg bit set, enter reset; if cleared, exit reset */
            if ((val & RESETcfg) && !s->reset_active) {
                s->reset_active = true;
                /* In hardware, reset is triggered; status will reflect later */
            } else if (!(val & RESETcfg) && s->reset_active) {
                s->reset_active = false;
                /* After exiting reset, set initial status bits */
                s->regs.status = NORXflag | TXFREEflag | RESETflag;
            }
            break;
        case 7:
            /* XREG write, dependent on subpage */
            switch (s->subpage) {
                case SUB_SETUP1:
                    s->xreg_setup1 = (uint8_t)val;
                    break;
                case SUB_SETUP2:
                    s->xreg_setup2 = (uint8_t)val;
                    break;
                case SUB_NODE:
                    s->xreg_node = (uint8_t)val;
                    break;
                default:
                    s->xreg_other[s->subpage] = (uint8_t)val;
                    break;
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

    memset(&s->regs, 0, sizeof(COM20020Regs));
    s->subpage = 0;
    s->xreg_setup1 = 0;
    s->xreg_setup2 = 0;
    s->xreg_node = 0;
    memset(s->xreg_other, 0, sizeof(s->xreg_other));
    memset(s->ram, 0, sizeof(s->ram));
    /* Initialize signature byte at address 0 */
    s->ram[0] = TESTvalue;
    s->memaddr = 0;
    s->node_id = 0x00; /* default node address */
    s->reset_active = false;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1571);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0xA001);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 8, .name = "io" };
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

static const VMStateDescription vmstate_pcibase = {
    .name = "com20020_pci",
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