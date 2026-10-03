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
/* #HeadFile# remains empty - no additional driver-specific includes available */

#define TYPE_PCIBASE_DEVICE "mei_me_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Vendor and Device ID from first entry of mei_me_pci_tbl: INTEL, MEI_82946GZ */
#define PCI_VENDOR_ID_MEI     0x8086  /* Intel */
#define PCI_DEVICE_ID_MEI_82946GZ 0x0000  /* Placeholder - need real PCI_DEVICE_ID_MEI_82946GZ */
#define PCI_CLASS_CODE_MEI    0x078000 /* Placeholder - need correct class code */

/* Register offsets and bitfields explicitly defined in driver source */
#define H_HPG_CSR             0x10
#define H_HPG_CSR_PGI         0x00000002
#define H_HPG_CSR_PGIHEXR     0x00000001

#define H_D0I3C               0x800
#define H_D0I3C_CIP           0x00000001
#define H_D0I3C_I3            0x00000004
#define H_D0I3C_IR            0x00000002

/* HBM Command Codes (protocol-level, not MMIO registers) */
#define MEI_PG_ISOLATION_ENTRY_REQ_CMD      0x0a
#define MEI_PG_ISOLATION_EXIT_RES_CMD       0x8b
#define HOST_START_REQ_CMD                  0x01
#define MEI_FLOW_CONTROL_CMD                0x08
#define MEI_HBM_NOTIFY_REQ_CMD              0x10
#define MEI_HBM_NOTIFICATION_STOP           0
#define MEI_HBM_NOTIFICATION_START          1

/* HBM version constants */
#define HBM_MINOR_VERSION                   2
#define HBM_MAJOR_VERSION                   2

/* Power gating states from driver enum */
#define MEI_PG_OFF 0
#define MEI_PG_ON  1

/* Placeholder BAR size - actual size needed from hardware specification */
#define MEI_BAR0_SIZE 0x1000  /* TODO: replace with correct size from documentation */

/* New bitfield defines from iteration source */
#define H_IS              0x00000002
#define H_D0I3C_IS        0x00000040
#define H_IE              0x00000001
#define H_D0I3C_IE        0x00000020
#define ME_RST_HRA        0x00000010

/* H_CSR register (offset 0x04) and bitfields from driver analysis */
#define H_CSR                 0x04
#define H_CSR_H_RST           0x00000010
#define H_CSR_H_RDY           0x00000008
#define H_CSR_H_IG            0x00000004

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
        uint32_t hpg_csr;
        uint32_t d0i3c;
        uint32_t hcsr;   /* H_CSR at offset 0x04 */
    } regs;

    /* DMA Context - placeholder until register mapping is known */
    struct {
        uint32_t dummy; /* TODO: define DMA register structure */
    } dma;

    /* Operational status flags */
    uint32_t status;

    /* Reset sequence state */
    unsigned int reset_count;

    /* Power Management state */
    bool d0i3_supported;
    uint32_t power_state;

    /* HBM version */
    struct hbm_version {
        uint8_t minor_version;
        uint8_t major_version;
    } version;
};

/* No additional global definitions */

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = (s->intr_status & s->intr_mask) || (s->regs.hcsr & H_CSR_H_IG);
    if (raise) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case H_CSR:
        val = s->regs.hcsr;
        break;
    case H_HPG_CSR:
        val = s->regs.hpg_csr;
        break;
    case H_D0I3C:
        val = s->regs.d0i3c;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "mei_me_pci: unimplemented MMIO read at 0x%" PRIx64 "\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case H_CSR:
        s->regs.hcsr = val;
        pcibase_update_irq(s);
        break;
    case H_HPG_CSR:
        s->regs.hpg_csr = val;
        pcibase_update_irq(s);
        break;
    case H_D0I3C:
        s->regs.d0i3c = val;
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "mei_me_pci: unimplemented MMIO write at 0x%" PRIx64 ", value 0x%" PRIx64 "\n", addr, val);
        break;
    }
}

/* PIO handlers deleted - driver does not use PIO */

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

    s->regs.hpg_csr = 0;
    s->regs.d0i3c = 0;
    s->regs.hcsr = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status = 0;
    s->reset_count++;
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
        /* PIO not used - this branch will not be taken */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MEI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_MEI_82946GZ );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_CODE_MEI );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = MEI_BAR0_SIZE;
    s->bar_info[0].name = "mei-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization - driver calls pci_enable_msi() */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        error_setg(errp, "MSI initialization failed");
        return;
    }

    /* Final state initialization */
    s->version.major_version = HBM_MAJOR_VERSION;
    s->version.minor_version = HBM_MINOR_VERSION;
    s->d0i3_supported = false; /* to be updated from device-specific config */
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
    .name = "mei_me_pci",
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
