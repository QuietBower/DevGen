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

#define TYPE_PCIBASE_DEVICE "bna_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_BROCADE 0x1657
#define PCI_DEVICE_ID_BROCADE_CT 0x0014

#define HOSTFN0_INT_STATUS 0x00014000
#define HOSTFN0_INT_MSK    0x00014004
#define HOSTFN1_INT_STATUS 0x00014100
#define HOSTFN1_INT_MSK    0x00014104
#define HOSTFN2_INT_STATUS 0x00014300
#define HOSTFN2_INT_MSK    0x00014304
#define HOSTFN3_INT_STATUS 0x00014400
#define HOSTFN3_INT_MSK    0x00014404

#define __HFN_INT_MBOX_LPU0	0x00100000U
#define __HFN_INT_MBOX_LPU1	0x00200000U

#define __HFN_INT_ERR_EMC	0x00010000U
#define __HFN_INT_ERR_LPU0	0x00020000U
#define __HFN_INT_ERR_LPU1	0x00040000U
#define __HFN_INT_ERR_PSS	0x00080000U
#define __HFN_INT_LL_HALT	0x01000000U

#define __HFN_INT_ERR_MASK	\
	(__HFN_INT_ERR_EMC | __HFN_INT_ERR_LPU0 | __HFN_INT_ERR_LPU1 | \
	 __HFN_INT_ERR_PSS | __HFN_INT_LL_HALT)

#define BNAD_MSIX_VECTORS 16

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
    uint32_t regs[0x10000]; /* BAR0 MMIO space: 0x40000 bytes */

    /* DMA Context */
    struct {
        dma_addr_t src;
        dma_addr_t dst;
        uint32_t cnt;
        uint32_t cmd;
    } dma;

    uint32_t device_status;      /* Operational status flags */
    bool reset_done;             /* State used to handle reset sequences */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_mask;

    if (pending) {
        if (msix_enabled(pdev)) {
            /* Simplified: raise vector 0 for any pending interrupt */
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only support 32-bit reads for known registers */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case HOSTFN0_INT_STATUS:
    case HOSTFN1_INT_STATUS:
    case HOSTFN2_INT_STATUS:
    case HOSTFN3_INT_STATUS:
        val = s->intr_status;
        break;
    case HOSTFN0_INT_MSK:
    case HOSTFN1_INT_MSK:
    case HOSTFN2_INT_MSK:
    case HOSTFN3_INT_MSK:
        val = s->intr_mask;
        break;
    default:
        /* Return value from register shadow */
        if (addr < sizeof(s->regs)) {
            val = s->regs[addr >> 2];
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case HOSTFN0_INT_STATUS:
    case HOSTFN1_INT_STATUS:
    case HOSTFN2_INT_STATUS:
    case HOSTFN3_INT_STATUS:
        /* Write-1-to-clear: bits set in val are cleared from status */
        s->intr_status &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case HOSTFN0_INT_MSK:
    case HOSTFN1_INT_MSK:
    case HOSTFN2_INT_MSK:
    case HOSTFN3_INT_MSK:
        s->intr_mask = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    default:
        /* Store value into register shadow */
        if (addr < sizeof(s->regs)) {
            s->regs[addr >> 2] = (uint32_t)val;
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO used by driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO used by driver */
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

    /* Reset registers to power-on defaults */
    s->intr_status = 0;
    s->intr_mask = 0;
    memset(s->regs, 0, sizeof(s->regs));
    s->reset_done = true;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_BROCADE );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_BROCADE_CT );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x40000, .name = "bar0" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization (driver prefers MSI-X) */
    if (msix_init_exclusive_bar(pdev, BNAD_MSIX_VECTORS, 1, errp)) {
        /* MSI-X failed; driver may fall back to INTx */
    }

    /* Initial state: interrupts cleared */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->reset_done = false;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit_exclusive_bar(pdev);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "bna_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
        VMSTATE_UINT32_ARRAY(regs, PCIBaseState, 0x10000),
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
