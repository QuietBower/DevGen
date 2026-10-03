/*
 * QEMU PCI device model for PLX PCI bridge with SJA1000 CAN controllers.
 * Based on driver: drivers/net/can/sja1000/plx_pci.c
 * Implements functional behavior for probing.
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

/* PCI IDs */
#define VENDOR_ID 0x144A
#define DEVICE_ID 0x7841
#define CLASS_ID PCI_CLASS_NETWORK_OTHER

/* PLX local configuration register offsets */
#define PLX_INTCSR      0x4c
#define PLX_CNTRL       0x50
#define PLX_LINT1_EN    0x1
#define PLX_LINT1_POL   (1 << 1)
#define PLX_LINT2_EN    (1 << 3)
#define PLX_LINT2_POL   (1 << 4)
#define PLX_PCI_INT_EN  (1 << 6)
#define PLX_PCI_RESET   (1 << 30)

/* SJA1000 register offsets */
#define SJA1000_MOD     0x00
#define SJA1000_CMR     0x01
#define SJA1000_SR      0x02
#define SJA1000_IR      0x03
#define SJA1000_IER     0x04
#define SJA1000_BTR0    0x06
#define SJA1000_BTR1    0x07
#define SJA1000_OCR     0x08
#define SJA1000_ECC     0x0C
#define SJA1000_RXERR   0x0E
#define SJA1000_TXERR   0x0F
#define SJA1000_FI      0x10
#define SJA1000_ID1     0x11
#define SJA1000_ID2     0x12
#define SJA1000_ID3     0x13
#define SJA1000_ID4     0x14
#define SJA1000_ACCC0   0x10
#define SJA1000_ACCC1   0x11
#define SJA1000_ACCC2   0x12
#define SJA1000_ACCC3   0x13
#define SJA1000_ACCM0   0x14
#define SJA1000_ACCM1   0x15
#define SJA1000_ACCM2   0x16
#define SJA1000_ACCM3   0x17
#define SJA1000_SFF_BUF 0x13
#define SJA1000_EFF_BUF 0x15
#define SJA1000_CDR     0x1F

/* SJA1000 register initial values, derived from driver macros */
#define REG_CR_BASICCAN_INITIAL     0x21
#define REG_SR_BASICCAN_INITIAL     0x0c
#define REG_IR_BASICCAN_INITIAL     0xe0
#define CDR_PELICAN                 0x80
#define REG_MOD_PELICAN_INITIAL     0x01
#define REG_SR_PELICAN_INITIAL      0x3c
#define REG_IR_PELICAN_INITIAL      0x00

#define PLX_PCI_MAX_CHAN 2

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

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware register shadows */
    uint8_t plx_conf_regs[0x80];
    uint8_t sja1000_regs[PLX_PCI_MAX_CHAN][0x200];
    bool pelican_mode[PLX_PCI_MAX_CHAN]; /* true if PeliCAN mode set */
};

static void sja1000_reset_channel(PCIBaseState *s, int chan)
{
    /* Set registers to BasicCAN initial values */
    s->sja1000_regs[chan][SJA1000_MOD] = REG_CR_BASICCAN_INITIAL;
    s->sja1000_regs[chan][SJA1000_SR]  = REG_SR_BASICCAN_INITIAL;
    s->sja1000_regs[chan][SJA1000_IR]  = REG_IR_BASICCAN_INITIAL;
    s->pelican_mode[chan] = false;
    /* Other registers are zeroed or default; sufficient for probe */
    memset(s->sja1000_regs[chan] + 4, 0, 0x200 - 4);
}

static void sja1000_switch_to_pelican(PCIBaseState *s, int chan)
{
    s->pelican_mode[chan] = true;
    /* Set registers to PeliCAN initial values */
    s->sja1000_regs[chan][SJA1000_MOD] = REG_MOD_PELICAN_INITIAL;
    s->sja1000_regs[chan][SJA1000_SR]  = REG_SR_PELICAN_INITIAL;
    s->sja1000_regs[chan][SJA1000_IR]  = REG_IR_PELICAN_INITIAL;
    /* Other registers as needed, but probe only checks MOD/SR/IR */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all SJA1000 channels to BasicCAN state */
    for (int i = 0; i < PLX_PCI_MAX_CHAN; i++) {
        sja1000_reset_channel(s, i);
    }
    /* Clear PLX configuration registers */
    memset(s->plx_conf_regs, 0, sizeof(s->plx_conf_regs));
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Determine which BAR is accessed based on address? Not needed: two separate regions. */
    /* For BAR1 (PLX config at 0..0x7f) */
    if (addr < 0x80) {
        if (size != 4) {
            return ~0ULL;
        }
        switch (addr) {
        case PLX_INTCSR: /* 0x4c */
            val = ldl_le_p(s->plx_conf_regs + addr);
            break;
        case PLX_CNTRL:  /* 0x50 */
            val = ldl_le_p(s->plx_conf_regs + addr);
            break;
        default:
            val = 0;
            break;
        }
        return val;
    }

    /* For BAR2 (SJA1000) */
    int chan = (addr >= 0x80) ? 1 : 0;
    unsigned reg = addr & 0x7f;
    if (reg < 0x200) {
        val = s->sja1000_regs[chan][reg];
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* BAR1: PLX configuration */
    if (addr < 0x80) {
        if (size != 4) {
            return;
        }
        switch (addr) {
        case PLX_INTCSR:
            stl_le_p(s->plx_conf_regs + addr, val);
            break;
        case PLX_CNTRL:
        {
            uint32_t old = ldl_le_p(s->plx_conf_regs + addr);
            stl_le_p(s->plx_conf_regs + addr, val);
            /* Detect assertion of reset bit */
            if ((val & PLX_PCI_RESET) && !(old & PLX_PCI_RESET)) {
                /* Reset all SJA1000 channels */
                for (int i = 0; i < PLX_PCI_MAX_CHAN; i++) {
                    sja1000_reset_channel(s, i);
                }
            }
            break;
        }
        default:
            stl_le_p(s->plx_conf_regs + addr, val);
            break;
        }
        return;
    }

    /* BAR2: SJA1000 registers per channel */
    int chan = (addr >= 0x80) ? 1 : 0;
    unsigned reg = addr & 0x7f;
    if (reg < 0x200) {
        s->sja1000_regs[chan][reg] = val;
        /* Handle mode switch via CDR write */
        if (reg == SJA1000_CDR && (val & CDR_PELICAN)) {
            sja1000_switch_to_pelican(s, chan);
        }
    }
}

/* PIO handlers (not used by this driver) */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BARs */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x80, .name = "plx_conf" };
    s->bar_info[1] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x200, .name = "sja1000" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize SJA1000 registers to reset state (also done in pcibase_reset, but do here too) */
    for (int i = 0; i < PLX_PCI_MAX_CHAN; i++) {
        sja1000_reset_channel(s, i);
    }
    memset(s->plx_conf_regs, 0, sizeof(s->plx_conf_regs));
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
