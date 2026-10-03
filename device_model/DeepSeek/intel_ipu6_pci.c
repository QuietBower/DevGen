/*
 * Integrated QEMU PCI device model for Intel IPU6 (QEMU 8.2.10).
 * Auto-generated from Linux driver ipu6.c analysis.
 * Register-level emulation based on explicit driver accesses.
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

/* Additional includes not needed */

#define TYPE_PCIBASE_DEVICE "intel_ipu6_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IPU6_PCI_BAR 0

/* Register offsets from ipu6.c */
#define IPU6_PSYS_REG_SPC_ICACHE_BASE 0x10
#define IPU6_REG_PSYS_INFO_SEG_0_CONFIG_ICACHE_MASTER 0x14
#define IPU6_PSYS_REG_SPC_START_PC 0x4
#define IPU6_PSYS_REG_SPC_STATUS_CTRL 0x0
#define IPU6_PSYS_SPC_OFFSET 0x118000
#define IPU6_ISYS_SPC_OFFSET 0x210000
#define IPU6_ISYS_DMEM_OFFSET 0x200000
#define IPU6_ISYS_IOMMU0_OFFSET 0x2e0000
#define IPU6_ISYS_IOMMU1_OFFSET 0x2e0500
#define IPU6_ISYS_IOMMUI_OFFSET 0x2e0a00
#define IPU6_PSYS_IOMMU0_OFFSET 0x1b0000
#define IPU6_PSYS_IOMMU1_OFFSET 0x1b0700
#define IPU6_PSYS_IOMMU1R_OFFSET 0x1b0e00
#define IPU6_PSYS_IOMMUI_OFFSET 0x1b1500
#define IPU6_PSYS_DMEM_OFFSET 0x100000
#define IPU6_MMU_L1_STREAM_ID_REG_OFFSET 0x0c
#define IPU6_MMU_L2_STREAM_ID_REG_OFFSET 0x4c
#define IPU6_PSYS_MMU1W_L2_STREAM_ID_REG_OFFSET 0x8c
#define BUTTRESS_REG_BTRS_CTRL 0xc
#define BUTTRESS_REG_SKU 0x314
#define BUTTRESS_REG_ISR_ENABLE 0x98
#define BUTTRESS_REG_SECURITY_CTL 0x300
#define BUTTRESS_REG_FW_SOURCE_BASE_LO 0x78
#define BUTTRESS_REG_FW_SOURCE_BASE_HI 0x7c
#define BUTTRESS_REG_WDT 0x8
#define BUTTRESS_REG_IU2CSEDATA0 0x104
#define BUTTRESS_REG_CSE2IUCSR 0x30c
#define BUTTRESS_REG_CSE2IUDB0 0x304
#define BUTTRESS_REG_CAMERA_MASK 0x84
#define BUTTRESS_REG_IU2CSECSR 0x108
#define BUTTRESS_REG_CSE2IUDATA0 0x308
#define BUTTRESS_REG_SECURITY_TOUCH 0x318
#define BUTTRESS_REG_IU2CSEDB0 0x100
#define BUTTRESS_REG_ISR_CLEAR 0x9c
#define BUTTRESS_REG_FW_RESET_CTL 0x30
#define BUTTRESS_REG_IS_FREQ_CTL 0x34
#define BUTTRESS_REG_PS_FREQ_CTL 0x38
#define IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_LEVEL_NOT_PULSE 0x238714
#define IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_CLEAR 0x23820c
#define IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_MASK 0x238204
#define IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_EDGE 0x238200
#define IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_ENABLE 0x238210
#define IPU6_REG_ISYS_CSI_TOP_CTRL0_IRQ_STATUS 0x238208
#define CSI_REG_HUB_FW_ACCESS_PORT_OFS 0x17000
#define CSI_REG_HUB_FW_ACCESS_PORT_V6OFS 0x16000
#define IPU6V6_REG_ISYS_CSI_TOP_CTRL0_IRQ_EDGE 0x238700
#define IPU6V6_REG_ISYS_CSI_TOP_CTRL0_IRQ_MASK 0x238704
#define IPU6V6_REG_ISYS_CSI_TOP_CTRL0_IRQ_STATUS 0x238708
#define IPU6V6_REG_ISYS_CSI_TOP_CTRL0_IRQ_ENABLE 0x238710
#define IPU6V6_REG_ISYS_CSI_TOP_CTRL0_IRQ_CLEAR 0x23870c

/* Vendor/Device IDs: extracted from ipu6_pci_tbl */
#define PCI_VENDOR_ID_INTEL_IPU6 0x8086
#define PCI_DEVICE_ID_INTEL_IPU6 0x9a19
#define PCI_CLASS_INTEL_IPU6 0x048000

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Buttress core registers */
    uint32_t btrs_ctrl;
    uint32_t sku;
    uint32_t security_touch;
    uint32_t camera_mask;
    uint32_t wdt;
    uint32_t security_ctl;
    uint32_t isr_enable;

    /* ISYS SPC registers (at offset 0x210000) */
    uint32_t isys_spc_status_ctrl;
    uint32_t isys_spc_start_pc;
    uint32_t isys_spc_icache_base;
    uint32_t isys_spc_info_seg;
    uint32_t isys_dmem;  /* written via 0x200000 */

    /* PSYS SPC registers (at offset 0x118000) */
    uint32_t psys_spc_status_ctrl;
    uint32_t psys_spc_start_pc;
    uint32_t psys_spc_icache_base;
    uint32_t psys_spc_info_seg;
    uint32_t psys_dmem;  /* written via 0x100000 */

    /* DMA Context */
    /* DMA not used by driver */

    /* Operational status flags */
    /* No specific status fields yet */
    /* State used to handle reset sequences */
    /* Reset state not yet defined */
    /* Power management state (D0-D3) */
    /* PM state not yet defined */
    /* No additional structures needed */
};

/* No additional definitions needed */

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* Not implemented; driver ISR handler not provided */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Not implemented; driver DMA logic not provided */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case BUTTRESS_REG_BTRS_CTRL:
        val = s->btrs_ctrl;
        break;
    case BUTTRESS_REG_SKU:
        val = s->sku;
        break;
    case BUTTRESS_REG_SECURITY_TOUCH:
        val = s->security_touch;
        break;
    case BUTTRESS_REG_CAMERA_MASK:
        val = s->camera_mask;
        break;
    case BUTTRESS_REG_WDT:
        val = s->wdt;
        break;
    case BUTTRESS_REG_SECURITY_CTL:
        val = s->security_ctl;
        break;
    case IPU6_ISYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_STATUS_CTRL:
        val = s->isys_spc_status_ctrl;
        break;
    case IPU6_ISYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_START_PC:
        val = s->isys_spc_start_pc;
        break;
    case IPU6_ISYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_ICACHE_BASE:
        val = s->isys_spc_icache_base;
        break;
    case IPU6_ISYS_SPC_OFFSET + IPU6_REG_PSYS_INFO_SEG_0_CONFIG_ICACHE_MASTER:
        val = s->isys_spc_info_seg;
        break;
    case IPU6_PSYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_STATUS_CTRL:
        val = s->psys_spc_status_ctrl;
        break;
    case IPU6_PSYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_START_PC:
        val = s->psys_spc_start_pc;
        break;
    case IPU6_PSYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_ICACHE_BASE:
        val = s->psys_spc_icache_base;
        break;
    case IPU6_PSYS_SPC_OFFSET + IPU6_REG_PSYS_INFO_SEG_0_CONFIG_ICACHE_MASTER:
        val = s->psys_spc_info_seg;
        break;
    case IPU6_ISYS_DMEM_OFFSET:
        val = s->isys_dmem;
        break;
    case IPU6_PSYS_DMEM_OFFSET:
        val = s->psys_dmem;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ipu6: unimplemented read addr=0x%" HWADDR_PRIx " size=%u\n", addr, size);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case BUTTRESS_REG_BTRS_CTRL:
        s->btrs_ctrl = val;
        break;
    case BUTTRESS_REG_WDT:
        s->wdt = val;
        break;
    case BUTTRESS_REG_ISR_ENABLE:
        s->isr_enable = val;
        break;
    case BUTTRESS_REG_ISR_CLEAR:
        /* Write 1 to clear: driver writes BUTTRESS_IRQS, NOP for now */
        break;
    case IPU6_ISYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_STATUS_CTRL:
        s->isys_spc_status_ctrl = val;
        break;
    case IPU6_ISYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_START_PC:
        s->isys_spc_start_pc = val;
        break;
    case IPU6_ISYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_ICACHE_BASE:
        s->isys_spc_icache_base = val;
        break;
    case IPU6_ISYS_SPC_OFFSET + IPU6_REG_PSYS_INFO_SEG_0_CONFIG_ICACHE_MASTER:
        s->isys_spc_info_seg = val;
        break;
    case IPU6_PSYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_STATUS_CTRL:
        s->psys_spc_status_ctrl = val;
        break;
    case IPU6_PSYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_START_PC:
        s->psys_spc_start_pc = val;
        break;
    case IPU6_PSYS_SPC_OFFSET + IPU6_PSYS_REG_SPC_ICACHE_BASE:
        s->psys_spc_icache_base = val;
        break;
    case IPU6_PSYS_SPC_OFFSET + IPU6_REG_PSYS_INFO_SEG_0_CONFIG_ICACHE_MASTER:
        s->psys_spc_info_seg = val;
        break;
    case IPU6_ISYS_DMEM_OFFSET:
        s->isys_dmem = val;
        break;
    case IPU6_PSYS_DMEM_OFFSET:
        s->psys_dmem = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ipu6: unimplemented write addr=0x%" HWADDR_PRIx " val=0x%" PRIx64 " size=%u\n", addr, val, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by driver */
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

    /* Reset shadow registers to default state */
    s->btrs_ctrl = 0;
    s->sku = 0x00000006;  /* version 6, sku 0 (IPU6-v0) */
    s->security_touch = 0;
    s->camera_mask = 0;
    s->wdt = 0;
    s->security_ctl = 0;  /* non-secure mode */
    s->isr_enable = 0;
    s->isys_spc_status_ctrl = 0;
    s->isys_spc_start_pc = 0;
    s->isys_spc_icache_base = 0;
    s->isys_spc_info_seg = 0;
    s->isys_dmem = 0;
    s->psys_spc_status_ctrl = 0;
    s->psys_spc_start_pc = 0;
    s->psys_spc_icache_base = 0;
    s->psys_spc_info_seg = 0;
    s->psys_dmem = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL_IPU6);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_IPU6);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_INTEL_IPU6);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* MSI initialization: driver expects MSI for non-EP devices */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        error_propagate(errp, NULL);
        return;
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = IPU6_PCI_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 4 * MiB;  /* covers registers up to 0x238714 */
    s->bar_info[0].name = "ipu6-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA configuration not used */
    /* Timers not used */
    /* Final state initialization before the device is 'live' */
    pcibase_reset(DEVICE(pdev));
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

    /* No additional cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "intel_ipu6_pci",
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
