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


#define TYPE_PCIBASE_DEVICE "amplc_pci236_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMPLICON       0x14b9
#define AMPLC_PCI236_DEVICE_ID       0x0009
/* PCI Class code unknown from driver source; using 0xff0000 as unclassified */
#define AMPLC_PCI236_CLASS_ID        0xff0000

#define PLX9052_INTCSR_OFFSET        0x4c
#define PLX9052_INTCSR_LI2POL        BIT(4)
#define PLX9052_INTCSR_LI1CLRINT     BIT(10)
#define PLX9052_INTCSR_LI1SEL        BIT(8)
#define PLX9052_INTCSR_LI1POL        BIT(1)
#define PLX9052_INTCSR_PCIENAB       BIT(6)
#define PLX9052_INTCSR_LI1ENAB       BIT(0)
#define PLX9052_INTCSR_LI1STAT       BIT(2)

#define PCI236_INTR_DISABLE  (PLX9052_INTCSR_LI1POL | \
                              PLX9052_INTCSR_LI2POL | \
                              PLX9052_INTCSR_LI1SEL | \
                              PLX9052_INTCSR_LI1CLRINT)

#define PCI236_INTR_ENABLE   (PLX9052_INTCSR_LI1ENAB | \
                              PLX9052_INTCSR_LI1POL | \
                              PLX9052_INTCSR_LI2POL | \
                              PLX9052_INTCSR_PCIENAB | \
                              PLX9052_INTCSR_LI1SEL | \
                              PLX9052_INTCSR_LI1CLRINT)


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
    
    /* Interrupt state modeled after PLX9052 INTCSR register */
    uint32_t plx_intcsr;  /* shadow of PLX9052 INTCSR (offset 0x4c in BAR1) */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* (PLX9052 registers are the primary hardware registers) */
    
    /* DMA context deleted (not used by driver) */
    
    /* Operational status flags (e.g., LI1STAT reflected in plx_intcsr) */
    
    /* State used to handle reset sequences */
    
    /* Power management state deleted (not used by driver) */
    
    /* Other addition info struct placeholder (empty) */
    
    /* BAR2: 8255 PPI register file */
    uint8_t bar2_regs[0x10];
};

/* Other addition info definitions placeholder (empty) */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No MMIO BARs; should never be called */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No MMIO BARs; should never be called */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* BAR1: PLX9052 INTCSR at offset 0x4c, 32-bit access */
    if (addr == PLX9052_INTCSR_OFFSET && size == 4) {
        val = s->plx_intcsr;
    } else if (addr < sizeof(s->bar2_regs)) {
        /* BAR2: 8255 PPI registers, 8-bit access */
        if (size == 1) {
            val = s->bar2_regs[addr];
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == PLX9052_INTCSR_OFFSET && size == 4) {
        /* Handle PLX9052 INTCSR write */
        /* Bit 10: LI1CLRINT - write 1 to clear LI1STAT (bit 2) */
        if (val & PLX9052_INTCSR_LI1CLRINT) {
            s->plx_intcsr &= ~PLX9052_INTCSR_LI1STAT;
        }
        /* Preserve LI1STAT: software cannot set it, only hardware can.
         * So we mask out LI1STAT from the written value, keeping any hardware-set bit. */
        s->plx_intcsr = (val & ~PLX9052_INTCSR_LI1STAT) | (s->plx_intcsr & PLX9052_INTCSR_LI1STAT);
        /* LI1CLRINT is write-1-to-clear; after handling, clear it so it reads back 0 */
        s->plx_intcsr &= ~PLX9052_INTCSR_LI1CLRINT;
    } else if (addr < sizeof(s->bar2_regs)) {
        /* BAR2: 8255 PPI registers, 8-bit access */
        if (size == 1) {
            s->bar2_regs[addr] = (uint8_t)val;
        }
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

    /* Reset PLX9052 registers to power-on defaults */
    s->plx_intcsr = 0x00000000;
    /* Reset 8255 PPI registers */
    memset(s->bar2_regs, 0, sizeof(s->bar2_regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMPLICON );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  AMPLC_PCI236_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, AMPLC_PCI236_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization based on supplementary driver source: BAR1 is PLX LCR, BAR2 is PPI */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x10, .name = "bar0-unused" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 0x80, .name = "bar1-plx-config" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 0x10, .name = "bar2-ppi" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not used; placeholder deleted */
    /* DMA not used; placeholder deleted */
    /* Timer not used; placeholder deleted */

    /* Final state initialization before the device is 'live' */
    s->plx_intcsr = 0x00000000; /* default: interrupts disabled */
    memset(s->bar2_regs, 0, sizeof(s->bar2_regs));
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

    /* Free buffers, stop timers, etc. (none needed) */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "amplc_pci236_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(plx_intcsr, PCIBaseState),
        VMSTATE_UINT8_ARRAY(bar2_regs, PCIBaseState, 0x10),
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
