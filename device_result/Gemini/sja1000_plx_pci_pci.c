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

#define TYPE_PCIBASE_DEVICE "sja1000_plx_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ADLINK_PCI_VENDOR_ID 0x144A
#define ADLINK_PCI_DEVICE_ID 0x7841

#define PLX_INTCSR 0x4c
#define PLX_CNTRL 0x50
#define PLX_LINT1_EN 0x1
#define PLX_LINT1_POL (1 << 1)
#define PLX_LINT2_EN (1 << 3)
#define PLX_LINT2_POL (1 << 4)
#define PLX_PCI_INT_EN (1 << 6)
#define PLX_PCI_RESET (1 << 30)

#define SJA1000_MOD 0x00
#define SJA1000_CMR 0x01
#define SJA1000_SR 0x02
#define SJA1000_IR 0x03
#define SJA1000_IER 0x04
#define SJA1000_BTR0 0x06
#define SJA1000_BTR1 0x07
#define SJA1000_OCR 0x08
#define SJA1000_ECC 0x0C
#define SJA1000_RXERR 0x0E
#define SJA1000_TXERR 0x0F
#define SJA1000_FI 0x10
#define SJA1000_ID1 0x11
#define SJA1000_ID2 0x12
#define SJA1000_SFF_BUF 0x13
#define SJA1000_ID3 0x13
#define SJA1000_ID4 0x14
#define SJA1000_EFF_BUF 0x15
#define SJA1000_CDR 0x1F

#define PLX9056_INTCSR 0x68
#define PLX9056_LINTI (1 << 11)
#define PLX9056_PCI_INT_EN (1 << 8)

#define PCI_DEVICE_ID_PLX_9056 0x9056
#define MARATHON_PCIE_DEVICE_ID 0x3432
#define PCI_VENDOR_ID_ESDGMBH 0x12fe

struct plx_pci_channel_map {
    uint32_t bar;
    uint32_t offset;
    uint32_t size;
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
    uint32_t plx_intcsr;
    uint32_t plx_cntrl;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t sja_regs[2][0x80];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = false;

    if (s->plx_intcsr & PLX_PCI_INT_EN) {
        /* SJA1000 interrupt logic would go here if implemented */
    }

    pci_set_irq(pdev, irq_active ? 1 : 0);
}

#define REG_CR_BASICCAN_INITIAL 0x21
#define REG_SR_BASICCAN_INITIAL 0x0c
#define REG_IR_BASICCAN_INITIAL 0xe0
#define CDR_PELICAN 0x80
#define REG_MOD_PELICAN_INITIAL 0x01
#define REG_SR_PELICAN_INITIAL 0x3c
#define REG_IR_PELICAN_INITIAL 0x00

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == PLX_INTCSR) {
        return s->plx_intcsr;
    } else if (addr == PLX_CNTRL) {
        return s->plx_cntrl;
    } else if (addr < 0x80) {
        return s->sja_regs[0][addr];
    } else if (addr >= 0x80 && addr < 0x100) {
        return s->sja_regs[1][addr - 0x80];
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == PLX_INTCSR) {
        s->plx_intcsr = val;
        pcibase_update_irq(s);
    } else if (addr == PLX_CNTRL) {
        s->plx_cntrl = val;
        if (val & PLX_PCI_RESET) {
            for (int i = 0; i < 2; i++) {
                s->sja_regs[i][SJA1000_MOD] = REG_CR_BASICCAN_INITIAL;
                s->sja_regs[i][SJA1000_SR] = REG_SR_BASICCAN_INITIAL;
                s->sja_regs[i][SJA1000_IR] = REG_IR_BASICCAN_INITIAL;
            }
        }
    } else if (addr < 0x80) {
        s->sja_regs[0][addr] = val;
        if (addr == SJA1000_CDR && val == CDR_PELICAN) {
            s->sja_regs[0][SJA1000_MOD] = REG_MOD_PELICAN_INITIAL;
            s->sja_regs[0][SJA1000_SR] = REG_SR_PELICAN_INITIAL;
            s->sja_regs[0][SJA1000_IR] = REG_IR_PELICAN_INITIAL;
        }
    } else if (addr >= 0x80 && addr < 0x100) {
        int offset = addr - 0x80;
        s->sja_regs[1][offset] = val;
        if (offset == SJA1000_CDR && val == CDR_PELICAN) {
            s->sja_regs[1][SJA1000_MOD] = REG_MOD_PELICAN_INITIAL;
            s->sja_regs[1][SJA1000_SR] = REG_SR_PELICAN_INITIAL;
            s->sja_regs[1][SJA1000_IR] = REG_IR_PELICAN_INITIAL;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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

    s->plx_intcsr = 0;
    s->plx_cntrl = 0;

    for (int i = 0; i < 2; i++) {
        memset(s->sja_regs[i], 0, 0x80);
        s->sja_regs[i][SJA1000_MOD] = REG_CR_BASICCAN_INITIAL;
        s->sja_regs[i][SJA1000_SR] = REG_SR_BASICCAN_INITIAL;
        s->sja_regs[i][SJA1000_IR] = REG_IR_BASICCAN_INITIAL;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ADLINK_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ADLINK_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280 );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_NONE };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x100, .name = "plx-conf" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x100, .name = "sja1000-chan" };
  
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
    .name = "sja1000_plx_pci_pci",
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
