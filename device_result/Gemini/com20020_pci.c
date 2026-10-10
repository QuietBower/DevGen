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

#define TYPE_PCIBASE_DEVICE "com20020_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define COM20020_VENDOR_ID 0x1571
#define COM20020_DEVICE_ID 0xa001
#define COM20020_CLASS_ID  0x0280

#define COM20020_REG_R_STATUS   0
#define COM20020_REG_W_INTMASK  0
#define COM20020_REG_R_DIAGSTAT 1
#define COM20020_REG_W_COMMAND  1
#define COM20020_REG_W_ADDR_HI  2
#define COM20020_REG_W_ADDR_LO  3
#define COM20020_REG_RW_MEMDATA 4
#define COM20020_REG_W_SUBADR   5
#define COM20020_REG_W_CONFIG   6
#define COM20020_REG_W_XREG     7

#define D_SKB_SIZE 2048

/* New macros from driver */
#define XTOcfg(x)       ((x) << 3)
#define RESETcfg        0x80
#define P1MODE          0x80
#define SUB_SETUP1      2
#define SUB_SETUP2      4
#define STARTIOcmd      0x18
#define SUB_NODE        1
#define NORXflag        0x80
#define TXFREEflag      0x01
#define RESETflag       0x10
#define CFLAGScmd       0x06
#define RESETclear      0x08
#define CONFIGclear     0x10
#define RDDATAflag      0x80
#define AUTOINCflag     0x40
#define TESTvalue       0321

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
    uint8_t intmask;
    uint8_t status;
    uint8_t diagstat;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t command;
    uint8_t addr_hi;
    uint8_t addr_lo;
    uint8_t subadr;
    uint8_t config;
    uint8_t xreg;
    uint8_t setup;
    uint8_t setup2;

    uint8_t memory[D_SKB_SIZE];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case COM20020_REG_R_STATUS:
            val = s->status;
            break;
        case COM20020_REG_R_DIAGSTAT:
            val = s->diagstat;
            break;
        case COM20020_REG_RW_MEMDATA:
        {
            /* Address is formed by ADDR_HI and ADDR_LO. Masking out flags in ADDR_HI */
            uint16_t mem_addr = ((s->addr_hi << 8) | s->addr_lo) & (D_SKB_SIZE - 1);
            val = s->memory[mem_addr];
            
            if (s->addr_hi & AUTOINCflag) {
                mem_addr = (mem_addr + 1) & (D_SKB_SIZE - 1);
                s->addr_lo = mem_addr & 0xFF;
                s->addr_hi = (s->addr_hi & ~0x07) | ((mem_addr >> 8) & 0x07);
            }
            break;
        }
        case 8: /* MAC address read */
            val = 0x00; /* Dummy MAC address */
            break;
        default:
            val = 0;
            break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
        case COM20020_REG_W_COMMAND:
            s->command = val;
            if (val == (CFLAGScmd | RESETclear)) {
                s->status &= ~RESETflag;
            } else if (val == (CFLAGScmd | CONFIGclear)) {
                s->config = 0;
            } else if (val == (CFLAGScmd | RESETclear | CONFIGclear)) {
                s->status &= ~RESETflag;
                s->config = 0;
            }
            break;
        case COM20020_REG_W_INTMASK:
            s->intmask = val;
            break;
        case COM20020_REG_W_ADDR_HI:
            s->addr_hi = val;
            break;
        case COM20020_REG_W_ADDR_LO:
            s->addr_lo = val;
            break;
        case COM20020_REG_RW_MEMDATA:
        {
            /* Address is formed by ADDR_HI and ADDR_LO. Masking out flags in ADDR_HI */
            uint16_t mem_addr = ((s->addr_hi << 8) | s->addr_lo) & (D_SKB_SIZE - 1);
            s->memory[mem_addr] = val;
            
            if (s->addr_hi & AUTOINCflag) {
                mem_addr = (mem_addr + 1) & (D_SKB_SIZE - 1);
                s->addr_lo = mem_addr & 0xFF;
                s->addr_hi = (s->addr_hi & ~0x07) | ((mem_addr >> 8) & 0x07);
            }
            break;
        }
        case COM20020_REG_W_SUBADR:
            s->subadr = val;
            break;
        case COM20020_REG_W_CONFIG:
            s->config = val;
            break;
        case COM20020_REG_W_XREG:
            s->xreg = val;
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

    /* Revert registers to power-on defaults */
    s->status = NORXflag | TXFREEflag | RESETflag;
    s->diagstat = 0x00;
    s->command = 0x00;
    s->intmask = 0x00;
    s->addr_hi = 0x00;
    s->addr_lo = 0x00;
    s->subadr = 0x00;
    s->config = 0x00;
    s->xreg = 0x00;

    memset(s->memory, 0, sizeof(s->memory));
    s->memory[0] = TESTvalue; /* TESTvalue expected by driver */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  COM20020_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  COM20020_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, COM20020_CLASS_ID );
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
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x10; /* Increased to 0x10 to cover offset 8 read */
    s->bar_info[0].name = "com20020-pio";  
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
