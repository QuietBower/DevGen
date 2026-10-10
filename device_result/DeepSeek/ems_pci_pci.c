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


#define TYPE_PCIBASE_DEVICE "ems_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI Identification */
#define PCI_VENDOR_ID_SIEMENS 0x110A
#define VENDOR_ID PCI_VENDOR_ID_SIEMENS
#define DEVICE_ID 0x2104
#define CLASS_ID 0x00ff00

/* Board-specific constants for v1 */
#define EMS_PCI_V1_MAX_CHAN 2
#define EMS_PCI_MAX_CHAN    EMS_PCI_V1_MAX_CHAN
#define EMS_PCI_V1_BASE_BAR 1
#define EMS_PCI_V1_CONF_BAR 0
#define EMS_PCI_V1_CONF_SIZE 4096
#define EMS_PCI_V1_CAN_BASE_OFFSET 0x400
#define EMS_PCI_V1_CAN_CTRL_SIZE 0x200
#define EMS_PCI_BASE_SIZE  4096

/* PITA2 Register offsets (conf BAR) */
#define PITA2_ICR           0x00
#define PITA2_ICR_INT0      0x00000002
#define PITA2_ICR_INT0_EN   0x00020000
#define PITA2_MISC          0x1c
#define PITA2_MISC_CONFIG   0x04000000

/* SJA1000 Register offsets */
#define SJA1000_MOD         0x00
#define SJA1000_CMR         0x01
#define SJA1000_SR          0x02
#define SJA1000_IR          0x03
#define SJA1000_IER         0x04
#define SJA1000_BTR0        0x06
#define SJA1000_BTR1        0x07
#define SJA1000_OCR         0x08
#define SJA1000_RXERR       0x0E
#define SJA1000_TXERR       0x0F
#define SJA1000_FI          0x10
#define SJA1000_ACCC0       0x10
#define SJA1000_ACCC1       0x11
#define SJA1000_ACCC2       0x12
#define SJA1000_ACCC3       0x13
#define SJA1000_ACCM0       0x14
#define SJA1000_ACCM1       0x15
#define SJA1000_ACCM2       0x16
#define SJA1000_ACCM3       0x17
#define SJA1000_ID1         0x11
#define SJA1000_ID2         0x12
#define SJA1000_ID3         0x13
#define SJA1000_ID4         0x14
#define SJA1000_EFF_BUF     0x15
#define SJA1000_SFF_BUF     0x13
#define SJA1000_CDR         0x1F
#define SJA1000_ECC         0x0C


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

struct SJA1000Channel {
    uint8_t regs[0x200]; /* per-channel register space, sized by EMS_PCI_V1_CAN_CTRL_SIZE */
};

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
    uint32_t pit2_icr;
    uint32_t pit2_misc;
    struct SJA1000Channel channels[EMS_PCI_V1_MAX_CHAN];

    /* DMA Context */



};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;
    /* Check if interrupt is pending and enabled */
    if (s->pit2_icr & PITA2_ICR_INT0 && s->pit2_icr & PITA2_ICR_INT0_EN) {
        raise = true;
    }
    pci_set_irq(pdev, raise ? 1 : 0);
}


/* Forward declarations for separate BAR handlers */
static uint64_t pcibase_conf_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_conf_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_base_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_base_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

/* Separate ops for conf BAR and base BAR */
static const MemoryRegionOps pcibase_conf_mmio_ops = {
    .read = pcibase_conf_mmio_read,
    .write = pcibase_conf_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_base_mmio_ops = {
    .read = pcibase_base_mmio_read,
    .write = pcibase_base_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Dummy handlers for generic MMIO and PIO ops (never used at runtime) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};


static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all shadow registers */
    s->pit2_icr = 0;
    s->pit2_misc = 0;
    memset(s->channels, 0, sizeof(s->channels));
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* We override the generic loop by setting num_bars = 0 and manually registering below */
    s->num_bars = 0;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Field Init Real: register BARs with specific ops */
    {
        /* Conf BAR (BAR0) */
        MemoryRegion *conf_mr = &s->bar_regions[0];
        memory_region_init_io(conf_mr, OBJECT(s), &pcibase_conf_mmio_ops, s, "ems-pci-conf", EMS_PCI_V1_CONF_SIZE);
        pci_register_bar(pdev, EMS_PCI_V1_CONF_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, conf_mr);

        /* Base BAR (BAR1) */
        MemoryRegion *base_mr = &s->bar_regions[1];
        memory_region_init_io(base_mr, OBJECT(s), &pcibase_base_mmio_ops, s, "ems-pci-base", EMS_PCI_BASE_SIZE);
        pci_register_bar(pdev, EMS_PCI_V1_BASE_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, base_mr);
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
    .name = "ems_pci_pci",
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

/* Implementation of conf BAR handlers */
static uint64_t pcibase_conf_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* Only support 32-bit accesses for known registers */
    if (size != 4) {
        return val;
    }
    switch (addr) {
    case PITA2_ICR:
        val = s->pit2_icr;
        break;
    case PITA2_MISC:
        val = s->pit2_misc;
        break;
    default:
        /* Unmapped, return 0 */
        break;
    }
    return val;
}

static void pcibase_conf_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (size != 4) {
        return;
    }
    switch (addr) {
    case PITA2_ICR:
        /* Drivers write PITA2_ICR_INT0_EN | PITA2_ICR_INT0 to clear interrupt.
           Model: write-1-to-clear on INT0 bit, and store enable bits. */
        s->pit2_icr &= ~(val & PITA2_ICR_INT0);  /* clear if written 1 */
        s->pit2_icr |= (val & ~(uint32_t)PITA2_ICR_INT0); /* update other bits */
        pcibase_update_irq(s);
        break;
    case PITA2_MISC:
        s->pit2_misc = val;
        break;
    default:
        break;
    }
}

/* Implementation of base BAR handlers */
static uint64_t pcibase_base_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* The driver uses readb, so ensure we return correct byte. */
    if (size != 1) {
        return val;
    }
    /* Handle signature region (offsets 0 - 0x3ff) */
    if (addr < EMS_PCI_V1_CAN_BASE_OFFSET) {
        /* Read-only signature bytes */
        switch (addr) {
        case 0x00: val = 0x55; break;
        case 0x04: val = 0xAA; break;
        case 0x08: val = 0x01; break;
        case 0x0C: val = 0xCB; break;
        case 0x10: val = 0x11; break;
        default:   val = 0x00; break;
        }
        return val;
    }
    /* CAN channel register space */
    if (addr >= EMS_PCI_V1_CAN_BASE_OFFSET && addr < EMS_PCI_BASE_SIZE) {
        hwaddr ch_offset = addr - EMS_PCI_V1_CAN_BASE_OFFSET;
        int ch = ch_offset / EMS_PCI_V1_CAN_CTRL_SIZE;
        hwaddr reg_off = ch_offset % EMS_PCI_V1_CAN_CTRL_SIZE;
        if (ch >= EMS_PCI_V1_MAX_CHAN) {
            return 0;
        }
        /* The v1 write access uses stride 4: each register is at port*4 */
        if (reg_off > 0x7C) { /* CDR at 0x1F * 4 = 0x7C, max register for SJA1000 */
            return 0;
        }
        uint8_t reg_idx = reg_off / 4;
        /* For byte reads, the driver reads the byte at (base + port*4) */
        return s->channels[ch].regs[reg_idx];
    }
    return val;
}

static void pcibase_base_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (size != 1) {
        return;
    }
    /* Handle signature region: ignore writes (or board reset at offset 0) */
    if (addr < EMS_PCI_V1_CAN_BASE_OFFSET) {
        /* The driver writes 0 to offset 0 to reset the board. We can trigger a device-wide reset. */
        if (addr == 0x00) {
            pcibase_reset(DEVICE(s));
        }
        return;
    }
    /* CAN channel register space */
    if (addr >= EMS_PCI_V1_CAN_BASE_OFFSET && addr < EMS_PCI_BASE_SIZE) {
        hwaddr ch_offset = addr - EMS_PCI_V1_CAN_BASE_OFFSET;
        int ch = ch_offset / EMS_PCI_V1_CAN_CTRL_SIZE;
        hwaddr reg_off = ch_offset % EMS_PCI_V1_CAN_CTRL_SIZE;
        if (ch >= EMS_PCI_V1_MAX_CHAN) {
            return;
        }
        if (reg_off > 0x7C) {
            return;
        }
        uint8_t reg_idx = reg_off / 4;
        /* Store the written byte. For some registers we might need special behavior, but for probe just store. */
        s->channels[ch].regs[reg_idx] = (uint8_t)val;

        /* If writing to IR register (SJA1000_IR = 0x03) or IER, we could update interrupt state. But for probe, not needed. */
    }
}
