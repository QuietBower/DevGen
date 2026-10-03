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

#define TYPE_PCIBASE_DEVICE "kvaser_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs */
#define VENDOR_ID 0x10e8
#define DEVICE_ID 0x8406
#define CLASS_ID  0x0c01  /* Serial bus controller - CAN */

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
#define SJA1000_ACCC0   0x10
#define SJA1000_ACCC1   0x11
#define SJA1000_ACCC2   0x12
#define SJA1000_ACCC3   0x13
#define SJA1000_ACCM0   0x14
#define SJA1000_ACCM1   0x15
#define SJA1000_ACCM2   0x16
#define SJA1000_ACCM3   0x17
#define SJA1000_CDR     0x1F

#define SJA1000_FI_RTR  0x40
#define SJA1000_FI_FF   0x80

/* SJA1000 commands */
#define CMD_TR   0x01
#define CMD_AT   0x02
#define CMD_SRR  0x10

/* SJA1000 mode bits */
#define MOD_RM   0x01
#define MOD_LOM  0x02
#define MOD_STM  0x04

/* SJA1000 clock divider bits */
#define CDR_CBP         0x80
#define CDR_CLKOUT_MASK 0x07

/* SJA1000 output control bits */
#define OCR_TX0_PUSHPULL 0x18
#define OCR_TX1_PUSHPULL 0x00

/* AMCC S5920 registers (accessed via conf_addr) */
#define S5920_INTCSR                  0x38
#define S5920_PTCR                    0x60
#define INTCSR_ADDON_INTENABLE_M      0x2000

/* Misc */
#define KVASER_PCI_CAN_CLOCK  8000000  /* 16MHz/2 */
#define PCI_CONFIG_PORT_SIZE      0x80
#define PCI_PORT_XILINX_SIZE      0x08
#define PCI_PORT_SIZE             0x80
#define KVASER_PCI_PORT_BYTES     0x20
#define XILINX_VERINT         7
#define XILINX_PRESUMED_VERSION 14
#define MAX_NO_OF_CHANNELS    4

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
    uint32_t s5920_intcsr;
    uint32_t s5920_ptcr;
    uint32_t intr_status; /* combined interrupt status from SJA1000 */
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint8_t sja1000_regs[MAX_NO_OF_CHANNELS][0x20];
    int num_channels;
    uint8_t xilinx_ver;
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool any_pending = false;
    int i;

    /* Check all channels: interrupt pending if IER & IR != 0 */
    for (i = 0; i < s->num_channels; i++) {
        uint8_t ier = s->sja1000_regs[i][SJA1000_IER];
        uint8_t ir = s->sja1000_regs[i][SJA1000_IR];
        if (ier & ir) {
            any_pending = true;
            break;
        }
    }

    /* Raise INTx if interrupt enabled in S5920 INTCSR and any pending */
    if ((s->s5920_intcsr & INTCSR_ADDON_INTENABLE_M) && any_pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* BAR0 (S5920 config space) handlers */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x38 && addr < 0x3c) {
        return s->s5920_intcsr;
    } else if (addr >= 0x60 && addr < 0x64) {
        return s->s5920_ptcr;
    }
    return 0;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x38 && addr < 0x3c) {
        s->s5920_intcsr = val;
        pcibase_update_irq(s);
    } else if (addr >= 0x60 && addr < 0x64) {
        s->s5920_ptcr = val;
    }
}

/* BAR1 (SJA1000 channels) handlers */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    int channel = addr / KVASER_PCI_PORT_BYTES;
    int reg = addr % KVASER_PCI_PORT_BYTES;
    if (channel >= s->num_channels || reg >= 0x20) {
        return 0;
    }
    return s->sja1000_regs[channel][reg];
}

static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int channel = addr / KVASER_PCI_PORT_BYTES;
    int reg = addr % KVASER_PCI_PORT_BYTES;
    if (channel >= s->num_channels || reg >= 0x20) {
        return;
    }
    /* Read-only registers: ignore writes */
    if (reg == SJA1000_SR || reg == SJA1000_ECC ||
        reg == SJA1000_RXERR || reg == SJA1000_TXERR) {
        return;
    }
    if (reg == SJA1000_IR) {
        /* W1C: clear bits that are written as 1 */
        s->sja1000_regs[channel][reg] &= ~(uint8_t)val;
    } else {
        s->sja1000_regs[channel][reg] = (uint8_t)val;
    }
    pcibase_update_irq(s);
}

/* BAR2 (Xilinx) handlers */
static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == XILINX_VERINT) {
        return s->xilinx_ver;
    }
    return 0;
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* read-only, ignore writes */
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    /* Reset S5920 state */
    s->s5920_intcsr = 0;
    s->s5920_ptcr = 0;

    /* Reset SJA1000 channels: set MOD_RM, clear others */
    for (i = 0; i < s->num_channels; i++) {
        memset(s->sja1000_regs[i], 0, 0x20);
        s->sja1000_regs[i][SJA1000_MOD] = MOD_RM; /* Reset Mode */
        s->sja1000_regs[i][SJA1000_SR] = 0x0C;   /* Default status after reset */
        s->sja1000_regs[i][SJA1000_OCR] = 0x18;   /* Default output control (push-pull) */
    }

    /* Xilinx version readback */
    s->xilinx_ver = XILINX_PRESUMED_VERSION << 4; /* 0xE0 */

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE || bi->size == 0) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        const MemoryRegionOps *ops;
        switch (bi->index) {
            case 0: ops = &pcibase_bar0_ops; break;
            case 1: ops = &pcibase_bar1_ops; break;
            case 2: ops = &pcibase_bar2_ops; break;
            default:
                g_assert_not_reached();
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        const MemoryRegionOps *ops;
        switch (bi->index) {
            case 0: ops = &pcibase_bar0_ops; break;
            case 1: ops = &pcibase_bar1_ops; break;
            case 2: ops = &pcibase_bar2_ops; break;
            default:
                g_assert_not_reached();
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization - all MMIO to match real hardware */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = PCI_CONFIG_PORT_SIZE, .name = "s5920-conf" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = PCI_PORT_SIZE, .name = "sja1000" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = PCI_PORT_XILINX_SIZE, .name = "xilinx" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initial hardware state (will be set in reset) */
    s->num_channels = MAX_NO_OF_CHANNELS;
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
    .name = "kvaser_pci_pci",
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
