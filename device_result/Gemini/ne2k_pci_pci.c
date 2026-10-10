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

#define TYPE_PCIBASE_DEVICE "ne2k_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef EI_SHIFT
#define EI_SHIFT(x) (x)
#endif

/* Register Layout and Hardware Identifiers extracted from driver source */
#define NE_CMD		0x00
#define NE_RESET	0x1f
#define NE_DATAPORT	0x10
#define NESM_START_PG	0x40
#define NESM_STOP_PG	0x80
#define NE_IO_EXTENT	0x20

#define E8390_PAGE0	0x00
#define E8390_PAGE1	0x40
#define E8390_CMD	0x00
#define E8390_START	0x02
#define E8390_STOP	0x01
#define E8390_TXOFF	0x02
#define E8390_RREAD	0x08
#define E8390_RWRITE	0x10
#define E8390_NODMA	0x20

#define ENISR_RESET	0x80
#define ENISR_RDC	0x40

#define EN0_ISR		EI_SHIFT(0x07)
#define EN0_IMR		EI_SHIFT(0x0f)
#define EN0_DCFG	EI_SHIFT(0x0e)
#define EN0_RCNTLO	EI_SHIFT(0x0a)
#define EN0_RCNTHI	EI_SHIFT(0x0b)
#define EN0_RSARLO	EI_SHIFT(0x08)
#define EN0_RSARHI	EI_SHIFT(0x09)
#define EN0_RXCR	EI_SHIFT(0x0c)
#define EN0_TXCR	EI_SHIFT(0x0d)
#define EN0_COUNTER0	EI_SHIFT(0x0d)

#define E8390_RXOFF		(0x20)
#define TX_PAGES 12
#define FORCE_FDX 0x8000

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
    uint8_t cmd;
    uint8_t isr;
    uint8_t imr;
    uint8_t rxcr;
    uint8_t txcr;
    uint8_t rsarhi;
    uint8_t rsarlo;
    uint8_t rcnthi;
    uint8_t rcntlo;
    uint8_t dcfg;
    uint8_t page_offset;

};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->isr & s->imr) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
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

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case NE_CMD:
        val = s->cmd;
        break;
    case NE_DATAPORT:
        val = 0x5252525252525252ULL; /* Dummy MAC/Data */
        break;
    case NE_RESET:
        val = ENISR_RESET;
        break;
    case EN0_ISR:
        val = s->isr;
        break;
    case EN0_COUNTER0:
        val = 0;
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
    case NE_CMD:
        s->cmd = val;
        s->page_offset = val & 0xc0;
        break;
    case NE_DATAPORT:
        break;
    case NE_RESET:
        s->isr |= ENISR_RESET;
        pcibase_update_irq(s);
        break;
    case EN0_ISR:
        s->isr &= ~val;
        pcibase_update_irq(s);
        break;
    case EN0_IMR:
        s->imr = val;
        pcibase_update_irq(s);
        break;
    case EN0_DCFG:
        s->dcfg = val;
        break;
    case EN0_RCNTLO:
        s->rcntlo = val;
        break;
    case EN0_RCNTHI:
        s->rcnthi = val;
        break;
    case EN0_RSARLO:
        s->rsarlo = val;
        break;
    case EN0_RSARHI:
        s->rsarhi = val;
        break;
    case EN0_RXCR:
        s->rxcr = val;
        break;
    case EN0_TXCR:
        s->txcr = val;
        break;
    default:
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

    s->cmd = E8390_NODMA | E8390_PAGE0 | E8390_STOP;
    s->isr = ENISR_RESET;
    s->imr = 0;
    s->dcfg = 0;
    s->rcntlo = 0;
    s->rcnthi = 0;
    s->rsarlo = 0;
    s->rsarhi = 0;
    s->rxcr = 0;
    s->txcr = 0;
    s->page_offset = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10ec );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8029 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200 );
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
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x20;
    s->bar_info[0].name = "ne2k-io";  
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ne2k_pci_pci",
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
