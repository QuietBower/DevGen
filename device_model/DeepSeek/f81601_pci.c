/*
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

#define TYPE_PCIBASE_DEVICE "f81601_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI IDs: first entry from pci_device_id table */
#define VENDOR_ID  0x1c29
#define DEVICE_ID  0x1703
#define CLASS_ID    PCI_CLASS_NETWORK_OTHER

/* PCI Config Register Offsets (accessed via pci_write_config_byte / pci_read_config_byte) */
#define F81601_DECODE_REG               0x209
#define F81601_TRAP_REG                 0x20a

/* Decode register bits (PCI config space) */
#define F81601_IO_MODE                  BIT(7)
#define F81601_MEM_MODE                 BIT(6)
#define F81601_CFG_MODE                 BIT(5)
#define F81601_CAN2_INTERNAL_CLK        BIT(3)
#define F81601_CAN1_INTERNAL_CLK        BIT(2)
#define F81601_CAN2_EN                  BIT(1)
#define F81601_CAN1_EN                  BIT(0)

/* Trap register bit (PCI config space) */
#define F81601_CAN2_HAS_EN              BIT(4)

/* SJA1000 register offsets (accessed via MMIO BAR0 at per-channel base + offset) */
#define SJA1000_MOD                     0x00
#define SJA1000_CMR                     0x01
#define SJA1000_SR                      0x02
#define SJA1000_IR                      0x03
#define SJA1000_IER                     0x04
#define SJA1000_BTR0                    0x06
#define SJA1000_BTR1                    0x07
#define SJA1000_OCR                     0x08
#define SJA1000_ECC                     0x0C
#define SJA1000_RXERR                   0x0E
#define SJA1000_TXERR                   0x0F
#define SJA1000_FI                      0x10
#define SJA1000_ID1                     0x11
#define SJA1000_ID2                     0x12
#define SJA1000_SFF_BUF                 0x13
#define SJA1000_ID3                     0x13
#define SJA1000_ID4                     0x14
#define SJA1000_EFF_BUF                 0x15
#define SJA1000_CDR                     0x1F
#define SJA1000_ACCC0                   0x10
#define SJA1000_ACCC1                   0x11
#define SJA1000_ACCC2                   0x12
#define SJA1000_ACCC3                   0x13
#define SJA1000_ACCM0                   0x14
#define SJA1000_ACCM1                   0x15
#define SJA1000_ACCM2                   0x16
#define SJA1000_ACCM3                   0x17

/* IRQ flags */
#define IRQ_OFF                         0x00
#define IRQ_ALL                         0xFF
#define IRQ_BEI                         0x80

/* SJA1000 mode / command bits */
#define MOD_RM                          0x01
#define MOD_LOM                         0x02
#define MOD_STM                         0x04
#define CMD_TR                          0x01
#define CMD_AT                          0x02
#define CMD_SRR                         0x10

/* Frame information flags */
#define SJA1000_FI_RTR                  0x40
#define SJA1000_FI_FF                   0x80

/* Quirks */
#define SJA1000_QUIRK_NO_CDR_REG        BIT(1)
#define SJA1000_CUSTOM_IRQ_HANDLER      BIT(0)

/* Channel count */
#define F81601_PCI_MAX_CHAN             2

/* Echo skb max */
#define SJA1000_ECHO_SKB_MAX            1

#define BAR0_SIZE 0x100

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

    /* Hardware register storage – per-channel SJA1000 register files */
    uint8_t regs[F81601_PCI_MAX_CHAN][0x80];

    /* Custom PCI configuration registers */
    uint8_t decode_reg;
    uint8_t trap_reg;
};

/* Internal helper for status-triggered signaling (not used) */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
}

/* PCI Config Read/Write Handlers */
static uint32_t pcibase_pci_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr + len <= F81601_DECODE_REG || addr >= F81601_TRAP_REG + 1) {
        return pci_default_read_config(pdev, addr, len);
    }

    /* Read custom config registers */
    if (addr <= F81601_DECODE_REG && addr + len > F81601_DECODE_REG) {
        uint32_t val = pci_default_read_config(pdev, addr, len);
        if (len == 1 && addr == F81601_DECODE_REG) {
            val = s->decode_reg;
        }
        return val;
    }
    if (addr <= F81601_TRAP_REG && addr + len > F81601_TRAP_REG) {
        uint32_t val = pci_default_read_config(pdev, addr, len);
        if (len == 1 && addr == F81601_TRAP_REG) {
            val = s->trap_reg;
        }
        return val;
    }

    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_pci_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr + len <= F81601_DECODE_REG || addr >= F81601_TRAP_REG + 1) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    /* Write custom config registers */
    if (addr <= F81601_DECODE_REG && addr + len > F81601_DECODE_REG) {
        if (len == 1 && addr == F81601_DECODE_REG) {
            s->decode_reg = val;
        }
    }
    if (addr <= F81601_TRAP_REG && addr + len > F81601_TRAP_REG) {
        if (len == 1 && addr == F81601_TRAP_REG) {
            s->trap_reg = val;
        }
    }
    pci_default_write_config(pdev, addr, val, len);
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    unsigned chan = addr / 0x80;
    unsigned off = addr % 0x80;

    if (chan >= F81601_PCI_MAX_CHAN || off >= 0x80) {
        return ~0ULL;
    }

    if (size == 1) {
        val = s->regs[chan][off];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned MMIO read len %d\n", __func__, size);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned chan = addr / 0x80;
    unsigned off = addr % 0x80;

    if (chan >= F81601_PCI_MAX_CHAN || off >= 0x80) {
        return;
    }

    if (size == 1) {
        s->regs[chan][off] = val;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned MMIO write len %d\n", __func__, size);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset custom config */
    s->decode_reg = 0;
    s->trap_reg = F81601_CAN2_HAS_EN;

    /* Reset SJA1000 register files */
    memset(s->regs, 0, sizeof(s->regs));
    for (int i = 0; i < F81601_PCI_MAX_CHAN; i++) {
        s->regs[i][SJA1000_MOD] = MOD_RM;  /* enter reset mode */
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

    /* Custom config handlers are set via class_init, no additional init here */

    /* BAR Initialization: BAR0 is MMIO, size BAR0_SIZE */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "f81601_bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X usage detected in driver */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No timers or buffers to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "f81601_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(regs[0], PCIBaseState, 0x80),
        VMSTATE_UINT8_ARRAY(regs[1], PCIBaseState, 0x80),
        VMSTATE_UINT8(decode_reg, PCIBaseState),
        VMSTATE_UINT8(trap_reg, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->config_read = pcibase_pci_config_read;
    k->config_write = pcibase_pci_config_write;
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
