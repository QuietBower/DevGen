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

#define TYPE_PCIBASE_DEVICE "ptp_pch_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Vendor and Device ID */
#define VENDOR_ID       0x8086
#define DEVICE_ID       0x8819
#define CLASS_ID        0xFF00    /* base=0xFF (Others), sub=0x00 */
/* Register offsets (byte offsets from BAR base) */
#define PCH_CONTROL      0x00
#define PCH_EVENT        0x04
#define PCH_ADDEND       0x08
#define PCH_ACCUM        0x0C
#define PCH_TEST         0x10
#define PCH_TS_COMPARE   0x14
#define PCH_RSYSTIME_LO  0x18
#define PCH_RSYSTIME_HI  0x1C
#define PCH_SYSTIME_LO   0x20
#define PCH_SYSTIME_HI   0x24
#define PCH_TRGT_LO      0x28
#define PCH_TRGT_HI      0x2C
#define PCH_ASMS_LO      0x30
#define PCH_ASMS_HI      0x34
#define PCH_AMMS_LO      0x38
#define PCH_AMMS_HI      0x3C
#define PCH_CH_CONTROL   0x40
#define PCH_CH_EVENT     0x44
#define PCH_TX_SNAP_LO   0x48
#define PCH_TX_SNAP_HI   0x4C
#define PCH_RX_SNAP_LO   0x50
#define PCH_RX_SNAP_HI   0x54
#define PCH_SRC_UUID_LO  0x58
#define PCH_SRC_UUID_HI  0x5C
#define PCH_CAN_STATUS   0x60
#define PCH_CAN_SNAP_LO  0x64
#define PCH_CAN_SNAP_HI  0x68
#define PCH_TS_SEL       0x6C
#define PCH_TS_ST0       0x70
#define PCH_STL_MAX_SET_EN  0x74
#define PCH_STL_MAX_SET     0x78
#define PCH_TS_ST_LO        0x80
#define PCH_TS_ST_HI        0x84
/* To simplify, use total BAR size and register count */
#define PCH_MMIO_REGS_SIZE    0x100
#define PCH_MMIO_REGS_COUNT   (PCH_MMIO_REGS_SIZE / 4)

/* Bitfield definitions from driver */
#define PCH_TSC_RESET		(1 << 0)
#define PCH_TSC_TTM_MASK	(1 << 1)
#define PCH_TSC_ASMS_MASK	(1 << 2)
#define PCH_TSC_AMMS_MASK	(1 << 3)
#define PCH_TSC_PPSM_MASK	(1 << 4)
#define PCH_TSE_TTIPEND		(1 << 1)
#define PCH_TSE_SNS		(1 << 2)
#define PCH_TSE_SNM		(1 << 3)
#define PCH_TSE_PPS		(1 << 4)
#define PCH_CC_MM		(1 << 0)
#define PCH_CC_TA		(1 << 1)
#define PCH_CC_MODE_SHIFT	16
#define PCH_CC_MODE_MASK	0x001F0000
#define PCH_CC_VERSION		(1 << 31)
#define PCH_CE_TXS		(1 << 0)
#define PCH_CE_RXS		(1 << 1)
#define PCH_CE_OVR		(1 << 0)
#define PCH_CE_VAL		(1 << 1)
#define PCH_ECS_ETH		(1 << 0)
#define PCH_ECS_CAN		(1 << 1)
#define PCH_IEEE1588_ETH	(1 << 0)
#define PCH_IEEE1588_CAN	(1 << 1)

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[PCH_MMIO_REGS_COUNT];
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t ev = s->regs[PCH_EVENT / 4];
    if (ev & (PCH_TSE_TTIPEND | PCH_TSE_SNS | PCH_TSE_SNM | PCH_TSE_PPS)) {
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
    int index = addr / 4;
    if (index >= PCH_MMIO_REGS_COUNT) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: read out of bounds: addr=0x%"HWADDR_PRIx"\n", addr);
        return 0;
    }
    if (size == 4) {
        val = s->regs[index];
    } else if (size == 8) {
        if (index + 1 >= PCH_MMIO_REGS_COUNT) {
            qemu_log_mask(LOG_GUEST_ERROR, "pcibase: 8-byte read out of bounds: addr=0x%"HWADDR_PRIx"\n", addr);
            return 0;
        }
        val = s->regs[index] | ((uint64_t)s->regs[index + 1] << 32);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: unsupported read size %u at addr=0x%"HWADDR_PRIx"\n", size, addr);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int index = addr / 4;
    if (index >= PCH_MMIO_REGS_COUNT) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: write out of bounds: addr=0x%"HWADDR_PRIx"\n", addr);
        return;
    }
    if (size == 4) {
        /* W1C for event register */
        if (addr == PCH_EVENT) {
            s->regs[index] &= ~val;
        } else {
            s->regs[index] = val;
        }
    } else if (size == 8) {
        if (index + 1 >= PCH_MMIO_REGS_COUNT) {
            qemu_log_mask(LOG_GUEST_ERROR, "pcibase: 8-byte write out of bounds: addr=0x%"HWADDR_PRIx"\n", addr);
            return;
        }
        if (addr == PCH_EVENT) {
            /* W1C for event, but unlikely 8-byte write */
            s->regs[index] &= ~(val & 0xFFFFFFFF);
            s->regs[index+1] = 0; /* ignoring high part? Assume not used */
        } else {
            s->regs[index] = val & 0xFFFFFFFF;
            s->regs[index + 1] = (val >> 32) & 0xFFFFFFFF;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: unsupported write size %u at addr=0x%"HWADDR_PRIx"\n", size, addr);
    }
    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
    memset(s->regs, 0, sizeof(s->regs));
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
    s->num_bars = 1;
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PCH_MMIO_REGS_SIZE;
    s->bar_info[0].name = "bar1";
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
    .name = "ptp_pch_pci",
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
