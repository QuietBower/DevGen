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

#define TYPE_PCIBASE_DEVICE "hisi_lpc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI IDs */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_IWLWIFI 0x4232
#define PCI_CLASS_NETWORK_OTHER 0x0280
/* Hardware constants */
#define IWL_MAX_RX_HW_QUEUES 16
/* Placeholder for future register definitions */

/* CSR-related constants (offsets require CSR_BASE) */
#define CSR_BASE              0x0000
#define CSR_MSIX_BASE         0x2000
#define CSR_HW_REV             (CSR_BASE+0x028)
#define CSR_FUNC_SCRATCH       (CSR_BASE+0x02c)
#define CSR_GP_CNTRL           (CSR_BASE+0x024)
#define CSR_INT_MASK           (CSR_BASE+0x00c)
#define CSR_MSIX_FH_INT_MASK_AD (CSR_MSIX_BASE + 0x804)
#define CSR_HW_RF_ID           (CSR_BASE+0x09c)
#define CSR_HW_REV_TYPE_MSK    0x000FFF0
#define CSR_RESET              0x80
#define CSR_INT_BIT_RF_KILL    (1 << 7)
#define CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW          (0x08000000)
#define CSR_GP_CNTRL_REG_FLAG_RFKILL_WAKE_L1A_EN     (0x04000000)
#define CSR_FUNC_SCRATCH_POWER_OFF_MASK		0xFFFF
#define CSR_53C80_INTR         0x10

/* Enums from driver */
enum iwl_pcie_fw_reset_state {
    FW_RESET_IDLE,
    FW_RESET_REQUESTED,
    FW_RESET_OK,
    FW_RESET_ERROR,
    FW_RESET_TOP_REQUESTED,
};
enum iwl_pcie_imr_status {
    IMR_D2S_IDLE,
    IMR_D2S_REQUESTED,
    IMR_D2S_COMPLETED,
    IMR_D2S_ERROR,
};
enum iwl_device_family {
    IWL_DEVICE_FAMILY_UNDEFINED,
    IWL_DEVICE_FAMILY_1000,
    IWL_DEVICE_FAMILY_100,
    IWL_DEVICE_FAMILY_2000,
    IWL_DEVICE_FAMILY_2030,
    IWL_DEVICE_FAMILY_105,
    IWL_DEVICE_FAMILY_135,
    IWL_DEVICE_FAMILY_5000,
    IWL_DEVICE_FAMILY_5150,
    IWL_DEVICE_FAMILY_6000,
    IWL_DEVICE_FAMILY_6000i,
    IWL_DEVICE_FAMILY_6005,
    IWL_DEVICE_FAMILY_6030,
    IWL_DEVICE_FAMILY_6050,
    IWL_DEVICE_FAMILY_6150,
    IWL_DEVICE_FAMILY_7000,
    IWL_DEVICE_FAMILY_8000,
    IWL_DEVICE_FAMILY_9000,
    IWL_DEVICE_FAMILY_22000,
    IWL_DEVICE_FAMILY_AX210,
    IWL_DEVICE_FAMILY_BZ,
    IWL_DEVICE_FAMILY_SC,
    IWL_DEVICE_FAMILY_DR,
};

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
    bool msix_enabled;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t hw_rev;
        uint32_t func_scratch;
        uint32_t gp_cntrl;
        uint32_t int_mask;
        uint32_t msix_hw_int_mask;
        uint32_t csr_53c80_intr;
    } regs;

    /* DMA Context - not used in this driver's probe path, placeholder removed */

    enum iwl_pcie_fw_reset_state fw_reset_state;
    enum iwl_pcie_imr_status imr_status;
    bool pm_support;
    bool ltr_enabled;
    /* State used to handle reset sequences - not required */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status & s->intr_mask) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    switch (addr) {
    case CSR_HW_REV:
        val = s->regs.hw_rev;
        break;
    case CSR_FUNC_SCRATCH:
        val = s->regs.func_scratch;
        break;
    case CSR_GP_CNTRL:
        val = s->regs.gp_cntrl;
        break;
    case CSR_INT_MASK:
        val = s->regs.int_mask;
        break;
    case CSR_MSIX_FH_INT_MASK_AD:
        val = s->regs.msix_hw_int_mask;
        break;
    case CSR_53C80_INTR:
        val = s->regs.csr_53c80_intr;
        break;
    default:
        val = 0;
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
    case CSR_HW_REV:
        /* read-only, ignore write */
        break;
    case CSR_FUNC_SCRATCH:
        s->regs.func_scratch = val;
        break;
    case CSR_GP_CNTRL:
        /* bits are set/cleared by driver using iwl_set_bit/clear_bit; for now just store value */
        s->regs.gp_cntrl = val;
        break;
    case CSR_INT_MASK:
        s->regs.int_mask = val;
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case CSR_MSIX_FH_INT_MASK_AD:
        s->regs.msix_hw_int_mask = val;
        /* driver writes ~mask to enable interrupts; could update internal state */
        break;
    case CSR_53C80_INTR:
        s->regs.csr_53c80_intr = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO not used by this device */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* PIO not used by this device */
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

    /* Revert registers to power-on defaults */
    s->regs.hw_rev = 0x1010;          /* non-0xFFFFFFFF to pass probe check */
    s->regs.func_scratch = 0;
    s->regs.gp_cntrl = 0;
    s->regs.int_mask = 0;
    s->regs.msix_hw_int_mask = 0;
    s->regs.csr_53c80_intr = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_IWLWIFI);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar1" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization: one vector, using BAR1 for table and PBA */
    if (!msix_init(pdev, 1, &s->bar_regions[1], 1, 0, &s->bar_regions[1], 1, 0x800, 0, errp)) {
        s->msix_enabled = true;
    } else {
        s->msix_enabled = false;
    }

    /* No DMA configuration needed for probe */

    /* Final state initialization */
    pcibase_reset(DEVICE(s));
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

    /* No additional cleanup required */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "hisi_lpc_pci",
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
