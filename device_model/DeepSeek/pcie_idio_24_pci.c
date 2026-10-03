/*
 * QEMU device model for PCIe-IDIO-24 GPIO
 * Based on driver: gpio-pcie-idio-24.c
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

#define BIT(n) (1UL << (n))
#define TYPE_PCIBASE_DEVICE "pcie_idio_24_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI identification */
#define PCI_VENDOR_ID_ACCES           0x494F
#define PCI_DEVICE_ID_IDIO24          0x0FD0
#define PCI_CLASS_ID_DEFAULT          0xFF00

/* Register offsets */
#define PLX_PEX8311_PCI_LCS_INTCSR    0x0
#define INTCSR_INTERNAL_PCI_WIRE      BIT(8)
#define INTCSR_LOCAL_INPUT            BIT(11)
#define IDIO_24_ENABLE_IRQ            (INTCSR_INTERNAL_PCI_WIRE | INTCSR_LOCAL_INPUT)
#define IDIO_24_OUT_BASE              0x0
#define IDIO_24_TTLCMOS_OUT_REG       0x3
#define IDIO_24_IN_BASE               0x4
#define IDIO_24_TTLCMOS_IN_REG        0x7
#define IDIO_24_COS_STATUS_BASE       0x8
#define IDIO_24_CONTROL_REG           0xC
#define IDIO_24_COS_ENABLE            0xE
#define IDIO_24_SOFT_RESET            0xF
#define CONTROL_REG_OUT_MODE          BIT(1)
#define COS_ENABLE_RISING             BIT(1)
#define COS_ENABLE_FALLING            BIT(4)
#define COS_ENABLE_BOTH               (COS_ENABLE_RISING | COS_ENABLE_FALLING)
#define IDIO_24_NGPIO_PER_REG         8
#define IDIO_24_NGPIO                 56

/* BAR indices */
#define BAR1                          1
#define BAR2                          2

/* BAR sizes */
#define PEX8311_BAR_SIZE              256
#define IDIO24_BAR_SIZE               256

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

    /* Hardware Register Shadows */
    struct {
        uint8_t out[4];          /* 0x0-0x3 */
        uint8_t in[4];           /* 0x4-0x7 */
        uint8_t cos_status[4];   /* 0x8-0xB */
        uint8_t control;         /* 0xC */
        uint8_t cos_enable;      /* 0xE */
        uint8_t soft_reset;      /* 0xF */
        uint32_t intcsr;         /* 0x68 */
    } regs;
};

/* PIO Handler for IDIO-24 registers (BAR2) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 1) {
        return ~0ULL;
    }

    switch (addr) {
    case 0x0 ... 0x3:
        val = s->regs.out[addr - 0x0];
        break;
    case 0x4 ... 0x7:
        val = s->regs.in[addr - 0x4];
        break;
    case 0x8 ... 0xB:
        val = s->regs.cos_status[addr - 0x8];
        break;
    case 0xC:
        val = s->regs.control;
        break;
    case 0xE:
        val = s->regs.cos_enable;
        break;
    case 0xF:
        val = 0; /* soft reset write-only, return 0 */
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

    if (size != 1) {
        return;
    }

    switch (addr) {
    case 0x0 ... 0x3:
        s->regs.out[addr - 0x0] = (uint8_t)val;
        break;
    case 0x4 ... 0x7:
        /* input registers are read-only, ignore write */
        break;
    case 0x8 ... 0xB:
        /* COS status: write-1-to-clear */
        s->regs.cos_status[addr - 0x8] &= ~((uint8_t)val);
        break;
    case 0xC:
        s->regs.control = (uint8_t)val;
        break;
    case 0xE:
        s->regs.cos_enable = (uint8_t)val;
        break;
    case 0xF:
        /* Soft reset: write 0 triggers board reset */
        if ((uint8_t)val == 0) {
            memset(&s->regs, 0, sizeof(s->regs));
            s->regs.soft_reset = 0;
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* MMIO Handler for PEX8311 INTCSR register (BAR1) */
static uint64_t pcibase_pex8311_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr != PLX_PEX8311_PCI_LCS_INTCSR || size != 4) {
        return ~0ULL;
    }
    val = s->regs.intcsr;
    return val;
}

static void pcibase_pex8311_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr != PLX_PEX8311_PCI_LCS_INTCSR || size != 4) {
        return;
    }
    s->regs.intcsr = (uint32_t)val;
}

static const MemoryRegionOps pcibase_pex8311_mmio_ops = {
    .read = pcibase_pex8311_mmio_read,
    .write = pcibase_pex8311_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all shadow registers to power-on defaults (all zeros) */
    memset(&s->regs, 0, sizeof(s->regs));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    MemoryRegion *mr;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ACCES);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_IDIO24);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID_DEFAULT);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR1: PEX8311 INTCSR (Memory-mapped, not I/O) */
    mr = &s->bar_regions[BAR1];
    memory_region_init_io(mr, OBJECT(s), &pcibase_pex8311_mmio_ops, s, "pex8311-mmio", PEX8311_BAR_SIZE);
    pci_register_bar(pdev, BAR1, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);

    /* BAR2: IDIO-24 GPIO registers (Memory-mapped) */
    mr = &s->bar_regions[BAR2];
    memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, "idio24-mmio", IDIO24_BAR_SIZE);
    pci_register_bar(pdev, BAR2, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);

    s->num_bars = 2;
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
    .name = "pcie_idio_24_pci",
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
