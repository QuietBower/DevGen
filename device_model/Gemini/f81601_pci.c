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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "f81601_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define F81601_PCI_MAX_CHAN 2
#define F81601_DECODE_REG 0x209
#define F81601_IO_MODE BIT(7)
#define F81601_MEM_MODE BIT(6)
#define F81601_CFG_MODE BIT(5)
#define F81601_CAN2_INTERNAL_CLK BIT(3)
#define F81601_CAN1_INTERNAL_CLK BIT(2)
#define F81601_CAN2_EN BIT(1)
#define F81601_CAN1_EN BIT(0)
#define F81601_TRAP_REG 0x20a
#define F81601_CAN2_HAS_EN BIT(4)

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
#define SJA1000_ACCC0 0x10
#define SJA1000_ID1 0x11
#define SJA1000_ACCC1 0x11
#define SJA1000_ID2 0x12
#define SJA1000_ACCC2 0x12
#define SJA1000_ID3 0x13
#define SJA1000_SFF_BUF 0x13
#define SJA1000_ACCC3 0x13
#define SJA1000_ID4 0x14
#define SJA1000_ACCM0 0x14
#define SJA1000_EFF_BUF 0x15
#define SJA1000_ACCM1 0x15
#define SJA1000_ACCM2 0x16
#define SJA1000_ACCM3 0x17
#define SJA1000_CDR 0x1F

#define IRQ_OFF 0x00
#define MOD_RM 0x01
#define CDR_PELICAN 0x80
#define OCR_MODE_NORMAL 0x02

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
    uint8_t decode_reg;
    uint8_t trap_reg;
    uint8_t sja1000_regs[F81601_PCI_MAX_CHAN][0x20];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* MMIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int chan = addr / 0x80;
    int port = addr % 0x80;

    if (chan < F81601_PCI_MAX_CHAN && port < 0x20) {
        val = s->sja1000_regs[chan][port];
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int chan = addr / 0x80;
    int port = addr % 0x80;

    if (chan < F81601_PCI_MAX_CHAN && port < 0x20) {
        s->sja1000_regs[chan][port] = val;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->sja1000_regs, 0, sizeof(s->sja1000_regs));
    for (int i = 0; i < F81601_PCI_MAX_CHAN; i++) {
        s->sja1000_regs[i][SJA1000_MOD] = MOD_RM;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1c29 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1703 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c09 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* F81601 specific config registers */
    pci_set_byte(pci_conf + F81601_TRAP_REG, F81601_CAN2_HAS_EN);
    pdev->wmask[F81601_DECODE_REG] = 0xFF;

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "f81601-bar0";

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
    .name = "f81601_pci",
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
