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


#define TYPE_PCIBASE_DEVICE "gma500_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_GMA500 0x8108
#define PCI_CLASS_DISPLAY_VGA 0x0300

#define PSB_MMIO_RESOURCE 0
#define PSB_GATT_RESOURCE 2
#define PSB_GTT_RESOURCE 3
#define PSB_AUX_RESOURCE 0

#define PSB_VDC_SIZE 0x00080000
#define PSB_SGX_SIZE 0x8000

#define PSB_VDC_OFFSET 0x00000000
#define PSB_LPC_GBA 0x44
#define PSB_CR_PDS_EXEC_BASE 0x0AB8
#define _PSB_MMU_ER_MASK 0x0001FF00

#define PSB_CR_SOFT_RESET 0x0080
#define PSB_CR_EVENT_HOST_ENABLE2 0x0110
#define PSB_CR_EVENT_HOST_ENABLE 0x0130
#define PSB_CR_BIF_CTRL 0x0C00
#define PSB_CR_BIF_DIR_LIST_BASE1 0x0C38
#define PSB_CR_BIF_BANK0 0x0C78
#define PSB_CR_BIF_BANK1 0x0C7C
#define PSB_CR_BIF_DIR_LIST_BASE0 0x0C84
#define PSB_CR_BIF_TWOD_REQ_BASE 0x0C88
#define PSB_CR_BIF_3D_REQ_BASE 0x0CAC
#define PSB_PGETBL_CTL 0x2020
#define PSB_HWSTAM 0x2098
#define PSB_INT_ENABLE_R 0x20A0
#define PSB_INT_IDENTITY_R 0x20A4
#define PSB_INT_MASK_R 0x20A8

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
    uint32_t int_enable;
    uint32_t int_mask;
    uint32_t int_identity;
    uint32_t hwstam;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t cr_soft_reset;
    uint32_t cr_bif_ctrl;
    uint32_t pgetbl_ctl;
    uint32_t pipestat[3];
    
    /* Additional shadow registers for behavioral modeling */
    uint32_t cr_bif_bank0;
    uint32_t cr_bif_bank1;
    uint32_t cr_bif_twod_req_base;
    uint32_t cr_bif_3d_req_base;
    uint32_t cr_pds_exec_base;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_state = (s->int_identity & s->int_enable & ~s->int_mask) != 0;
    
    if (msi_enabled(pdev)) {
        if (irq_state) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, irq_state ? 1 : 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PSB_CR_SOFT_RESET:
        val = s->cr_soft_reset;
        break;
    case PSB_CR_BIF_CTRL:
        val = s->cr_bif_ctrl;
        break;
    case PSB_CR_BIF_BANK0:
        val = s->cr_bif_bank0;
        break;
    case PSB_CR_BIF_BANK1:
        val = s->cr_bif_bank1;
        break;
    case PSB_CR_BIF_TWOD_REQ_BASE:
        val = s->cr_bif_twod_req_base;
        break;
    case PSB_CR_BIF_3D_REQ_BASE:
        val = s->cr_bif_3d_req_base;
        break;
    case PSB_CR_PDS_EXEC_BASE:
        val = s->cr_pds_exec_base;
        break;
    case PSB_PGETBL_CTL:
        val = s->pgetbl_ctl;
        break;
    case PSB_HWSTAM:
        val = s->hwstam;
        break;
    case PSB_INT_ENABLE_R:
        val = s->int_enable;
        break;
    case PSB_INT_MASK_R:
        val = s->int_mask;
        break;
    case PSB_INT_IDENTITY_R:
        val = s->int_identity;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented read at 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PSB_CR_SOFT_RESET:
        s->cr_soft_reset = val;
        break;
    case PSB_CR_BIF_CTRL:
        s->cr_bif_ctrl = val;
        break;
    case PSB_CR_BIF_BANK0:
        s->cr_bif_bank0 = val;
        break;
    case PSB_CR_BIF_BANK1:
        s->cr_bif_bank1 = val;
        break;
    case PSB_CR_BIF_TWOD_REQ_BASE:
        s->cr_bif_twod_req_base = val;
        break;
    case PSB_CR_BIF_3D_REQ_BASE:
        s->cr_bif_3d_req_base = val;
        break;
    case PSB_CR_PDS_EXEC_BASE:
        s->cr_pds_exec_base = val;
        break;
    case PSB_PGETBL_CTL:
        s->pgetbl_ctl = val;
        break;
    case PSB_HWSTAM:
        s->hwstam = val;
        break;
    case PSB_INT_ENABLE_R:
        s->int_enable = val;
        pcibase_update_irq(s);
        break;
    case PSB_INT_MASK_R:
        s->int_mask = val;
        pcibase_update_irq(s);
        break;
    case PSB_INT_IDENTITY_R:
        s->int_identity &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented write at 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", __func__, addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: Unimplemented PIO read at 0x%" HWADDR_PRIx "\n", __func__, addr);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: Unimplemented PIO write at 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", __func__, addr, val);
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

    s->cr_soft_reset = 0;
    s->cr_bif_ctrl = 0;
    s->cr_bif_bank0 = 0;
    s->cr_bif_bank1 = 0;
    s->cr_bif_twod_req_base = 0;
    s->cr_bif_3d_req_base = 0;
    s->cr_pds_exec_base = 0;
    /* 
     * Provide a valid stolen memory base to prevent ioremap failure.
     * stolen_base is 0xDF000000.
     * vram_stolen_size = (pgetbl_ctl & PAGE_MASK) - stolen_base - PAGE_SIZE.
     * Setting pgetbl_ctl to 0xDF801000 gives vram_stolen_size = 0x800000 (8MB).
     */
    s->pgetbl_ctl = 0xDF801000; 
    s->hwstam = 0xFFFFFFFF;
    s->int_enable = 0;
    s->int_mask = 0xFFFFFFFF;
    s->int_identity = 0;
    for (int i = 0; i < 3; i++) {
        s->pipestat[i] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8108 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0300 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Intel GFX Stolen Memory Base (BSM) */
    pci_set_long(pci_conf + 0x5C, 0xDF000000);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ 
        .index = PSB_MMIO_RESOURCE, 
        .type = BAR_TYPE_MMIO, 
        .size = PSB_VDC_SIZE, 
        .name = "psb-mmio" 
    };
    s->bar_info[1] = (BARInfo){ 
        .index = PSB_GATT_RESOURCE, 
        .type = BAR_TYPE_MMIO, 
        .size = 256 * MiB, 
        .name = "psb-gatt" 
    };
    s->bar_info[2] = (BARInfo){ 
        .index = PSB_GTT_RESOURCE, 
        .type = BAR_TYPE_MMIO, 
        .size = 1 * MiB, 
        .name = "psb-gtt" 
    };
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "gma500_pci",
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
