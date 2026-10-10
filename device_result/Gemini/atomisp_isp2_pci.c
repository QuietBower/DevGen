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


#define TYPE_PCIBASE_DEVICE "atomisp_isp2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define ATOMISP_PCI_DEVICE_SOC_MRFLD 0x1178

#define ATOM_ISP_PCI_BAR 0

#define INTR_IIR		16
#define INTR_IER		24
#define ATOMISP_PCI_REV_BYT_A0_MAX	4

#define MRFLD_PCI_CSI_CONTROL 0xe8
#define MRFLD_PCI_CSI_AFE_RCOMP_CONTROL 0xe0
#define PCI_MSI_DATA 0x98
#define MRFLD_PCI_CSI_AFE_HS_CONTROL 0xdc
#define PCI_INTERRUPT_CTRL 0x9C
#define PCI_I_CONTROL 0xfc
#define MRFLD_PCI_PMCS 0x84
#define MRFLD_PCI_CSI_RCOMP_CONTROL 0xf4
#define PCI_MSI_CAPID 0x90
#define MRFLD_PCI_CSI_ACCESS_CTRL_VIOL 0xd4
#define MRFLD_PCI_CSI_DEADLINE_CONTROL 0xec
#define PCI_MSI_ADDR 0x94
#define MRFLD_PCI_CSI_AFE_TRIM_CONTROL 0xe4
#define MRFLD_CSI_RECEIVER_SELECTION_REG 0x8081c
#define MRFLD_INTR_ENABLE_REG 0x510
#define MRFLD_INTR_STATUS_REG 0x508
#define MRFLD_INTR_CLEAR_REG 0x50c
#define MRFLD_ISPSSDVFS 0x13F
#define MRFLD_ISPSSPM0 0x39
#define ISPSSPM1 0x3a
#define DMA_BURST_SIZE_REG 0xCD408
#define CSI2_PORT_A_BASE 0xC0000
#define CSI2_PORT_B_BASE 0xC2000
#define CSI2_PORT_C_BASE 0xC4000

#define MRFLD_PCI_CSI_HSRXCLKTRIM_MASK		0xf
#define MRFLD_PCI_CSI1_HSRXCLKTRIM_SHIFT	16
#define MRFLD_PCI_CSI2_HSRXCLKTRIM_SHIFT	24
#define MRFLD_PCI_CSI3_HSRXCLKTRIM_SHIFT	28
#define MRFLD_PCI_CSI1_HSRXCLKTRIM		0x2
#define MRFLD_PCI_CSI2_HSRXCLKTRIM		0x3
#define MRFLD_PCI_CSI3_HSRXCLKTRIM		0x2

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

struct atomisp_regs {
    uint16_t pcicmdsts;
    uint32_t ispmmadr;
    uint32_t msicap;
    uint32_t msi_addr;
    uint16_t msi_data;
    uint8_t intr;
    uint32_t interrupt_control;
    uint32_t pmcs;
    uint32_t cg_dis;
    uint32_t i_control;
    uint32_t csi_rcomp_config;
    uint32_t csi_afe_dly;
    uint32_t csi_control;
    uint32_t csi_afe_rcomp_config;
    uint32_t csi_afe_hs_control;
    uint32_t csi_deadline_control;
    uint32_t csi_access_viol;
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
    uint32_t intr_enable;
    uint32_t intr_clear;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct atomisp_regs saved_regs;

    /* DMA Context */
    uint32_t dma_burst_size;

    uint32_t csi_receiver_selection;
    uint32_t ispssdvfs;
    uint32_t ispsspm0;
    uint32_t ispsspm1;
    
    uint32_t pmcs;
    
};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    bool level = (s->intr_status & s->intr_enable) != 0;
    if (s->has_msi) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MRFLD_INTR_STATUS_REG:
    case INTR_IIR:
        val = s->intr_status;
        break;
    case MRFLD_INTR_ENABLE_REG:
    case INTR_IER:
        val = s->intr_enable;
        break;
    case MRFLD_CSI_RECEIVER_SELECTION_REG:
        val = s->csi_receiver_selection;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MRFLD_INTR_CLEAR_REG:
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case MRFLD_INTR_ENABLE_REG:
    case INTR_IER:
        s->intr_enable = val;
        pcibase_update_irq(s);
        break;
    case MRFLD_CSI_RECEIVER_SELECTION_REG:
        s->csi_receiver_selection = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
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

    s->intr_status = 0;
    s->intr_enable = 0;
    s->csi_receiver_selection = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ATOMISP_PCI_DEVICE_SOC_MRFLD );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0480 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x05);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = ATOM_ISP_PCI_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100000; /* Defaulting to 1MB to prevent pow2ceil(0) issues */
    s->bar_info[0].name = "atomisp-bar0";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
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
    .name = "atomisp_isp2_pci",
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
