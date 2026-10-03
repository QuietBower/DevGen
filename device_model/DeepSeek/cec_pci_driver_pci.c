/* QEMU device model for CEC PCI GPIB controller (cec_pci_driver) */

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

#define TYPE_PCIBASE_DEVICE "cec_pci_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers */
#define CEC_VENDOR_ID 0x12fc
#define CEC_DEV_ID    0x5cec
#define CEC_SUBID     0x9050
#define CEC_REG_OFFSET 1

#define BAR1_SIZE     0x100  /* PLX9050 local configuration space */
#define BAR3_SIZE     0x100  /* NEC7210 GPIB controller registers */
#define CEC_CLASS_ID  0x0000 /* placeholder */

/* PLX9050 INTCSR bit definitions (guessed from typical PLX9050 datasheet) */
/* These will be requested from the driver source */
#define PLX9050_LINTR1_EN_BIT      0x01
#define PLX9050_LINTR1_POLARITY_BIT 0x02
#define PLX9050_PCI_INTR_EN_BIT    0x100
#define PLX9050_INTCSR_REG         0x4C

/* NEC7210 register offsets from driver */
#define SPSR    0x04
#define ISR1    0x70
#define ADDRESS_MASK    (0xff << 0)  /* 0xFF, used for address mode */

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t irq_status;   /* unused */
    uint32_t irq_mask;     /* unused */

    /* PLX9050 state */
    uint32_t plx_intcsr;   /* shadow of INTCSR register */

    /* NEC7210 state - full register map needed from nec7210_regs.h */
    uint8_t nec_regs[0x100]; /* placeholder: actual register meanings unknown */
    /* The following fields will be used once register definitions are available:
    uint8_t auxa_bits;
    uint8_t auxb_bits;
    uint8_t reg_bits[8];
    uint8_t imr1, imr2;
    uint8_t isr1, isr2;
    uint8_t adr0, adr1;
    uint8_t eosr;
    uint8_t spmr;
    uint8_t spsr;
    uint8_t cptr;
    uint8_t addr_mode;
    bool natn; // GPIB ATN state
    */
};

/* Helper for raising IRQ via PLX9050 logic */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool int_en = (s->plx_intcsr & PLX9050_PCI_INTR_EN_BIT) != 0;
    bool lint1_en = (s->plx_intcsr & PLX9050_LINTR1_EN_BIT) != 0;
    /* In the real device, the NEC7210's IRQ output connects to LINTR1. */
    /* For now, we need to implement NEC7210 interrupt logic to set nec_irq. */
    bool nec_irq = false; /* placeholder */

    bool raise = int_en && lint1_en && nec_irq;
    pci_set_irq(pdev, raise ? 1 : 0);
}

/* Called from NEC7210 side when its IRQ state changes */
static void nec7210_set_irq(PCIBaseState *s, bool level)
{
    /* TODO: wire to PLX LINTR1 and call pcibase_update_irq */
    /* For now no-op, missing nec7210 driver logic */
}

/* PIO handlers for BAR1 (PLX9050 local config) */
static uint64_t pcibase_pio_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PLX9050_INTCSR_REG:
        val = s->plx_intcsr;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "cec_pci: BAR1 read at 0x%"PRIx64", size %u\n", addr, size);
        break;
    }
    return val;
}

static void pcibase_pio_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PLX9050_INTCSR_REG:
        s->plx_intcsr = val;
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "cec_pci: BAR1 write at 0x%"PRIx64", val 0x%"PRIx64", size %u\n", addr, val, size);
        break;
    }
}

static const MemoryRegionOps pcibase_pio_bar1_ops = {
    .read = pcibase_pio_bar1_read,
    .write = pcibase_pio_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* PIO handlers for BAR3 (NEC7210 GPIB controller) */
static uint64_t pcibase_pio_bar3_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->nec_regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "cec_pci: BAR3 read out of bounds at 0x%"PRIx64"\n", addr);
        return 0xFFFFFFFF;
    }
    if (size == 1) {
        val = s->nec_regs[addr];
        /* Handle known registers without side effects for now */
        switch (addr) {
        case SPSR:
        case ISR1:
            /* Placeholder: side effects will be added when driver logic is known */
            break;
        default:
            if (addr != SPSR && addr != ISR1) {
                qemu_log_mask(LOG_UNIMP, "cec_pci: BAR3 read at 0x%"PRIx64", size %u\n", addr, size);
            }
            break;
        }
    } else {
        /* For larger sizes, return all-ones */
        qemu_log_mask(LOG_UNIMP, "cec_pci: BAR3 read size %u at 0x%"PRIx64"\n", size, addr);
        val = (1ULL << (size * 8)) - 1;
    }
    return val;
}

static void pcibase_pio_bar3_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->nec_regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "cec_pci: BAR3 write out of bounds at 0x%"PRIx64"\n", addr);
        return;
    }
    if (size == 1) {
        s->nec_regs[addr] = (uint8_t)val;
        /* Handle known registers for side effects */
        switch (addr) {
        case SPSR:
        case ISR1:
            /* Placeholder: AUXMR command processing, interrupt generation, etc. */
            break;
        default:
            if (addr != SPSR && addr != ISR1) {
                qemu_log_mask(LOG_UNIMP, "cec_pci: BAR3 write at 0x%"PRIx64", size %u\n", addr, size);
            }
            break;
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "cec_pci: BAR3 write size %u at 0x%"PRIx64"\n", size, addr);
    }
}

static const MemoryRegionOps pcibase_pio_bar3_ops = {
    .read = pcibase_pio_bar3_read,
    .write = pcibase_pio_bar3_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* MMIO handlers (unused by this device, kept from template) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    qemu_log_mask(LOG_UNIMP, "cec_pci: unused MMIO read at 0x%"PRIx64"\n", addr);
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "cec_pci: unused MMIO write at 0x%"PRIx64"\n", addr);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Reset handler */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->nec_regs, 0, sizeof(s->nec_regs));
    s->plx_intcsr = 0;
    /* reset any IRQ state */
    pci_set_irq(PCI_DEVICE(dev), 0);
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
        /* Choose the correct ops based on BAR index */
        const MemoryRegionOps *ops;
        if (bi->index == 1) {
            ops = &pcibase_pio_bar1_ops;
        } else if (bi->index == 3) {
            ops = &pcibase_pio_bar3_ops;
        } else {
            /* Unsupported BAR */
            error_setg(errp, "cec_pci: unsupported PIO BAR index %d", bi->index);
            return;
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CEC_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CEC_DEV_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CEC_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: BAR0 unused, BAR1 PLX9050, BAR2 unused, BAR3 NEC7210 */
    s->num_bars = 4;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].type = BAR_TYPE_NONE;
    }
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = BAR1_SIZE;
    s->bar_info[1].name = "plx9050-local";

    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = BAR3_SIZE;
    s->bar_info[3].name = "nec7210-gpib";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;
    s->plx_intcsr = 0;
    memset(s->nec_regs, 0, sizeof(s->nec_regs));
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
    .name = "cec_pci_driver_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(plx_intcsr, PCIBaseState),
        VMSTATE_BUFFER(nec_regs, PCIBaseState),
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