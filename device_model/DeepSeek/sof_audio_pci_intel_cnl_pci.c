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

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_cnl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI Vendor/Device IDs: first entry in sof_pci_ids[] */
#define PCI_VENDOR_ID_INTEL_DEFINED 0x8086  /* Standard Intel vendor ID */
#define HDA_CNL_LP 0x0000  /* Device ID unknown; must be provided */
#define PCI_CLASS_MULTIMEDIA_AUDIO 0x040300  /* Standard HDA class */

/* Register offsets from driver */
#define HDA_DSP_GEN_BASE            0x0
#define HDA_DSP_REG_ADSPCS          (HDA_DSP_GEN_BASE + 0x04)
#define HDA_DSP_REG_ADSPIS          (HDA_DSP_GEN_BASE + 0x0C)
#define HDA_DSP_REG_ADSPIC          (HDA_DSP_GEN_BASE + 0x08)
#define HDA_DSP_REG_ADSPIS2         (HDA_DSP_GEN_BASE + 0x14)
#define HDA_DSP_REG_ADSPIC2         (HDA_DSP_GEN_BASE + 0x10)

#define CNL_DSP_IPC_BASE            0xc0
#define CNL_DSP_REG_HIPCTDR         (CNL_DSP_IPC_BASE + 0x00)
#define CNL_DSP_REG_HIPCTDA         (CNL_DSP_IPC_BASE + 0x04)
#define CNL_DSP_REG_HIPCTDD         (CNL_DSP_IPC_BASE + 0x08)
#define CNL_DSP_REG_HIPCIDR         (CNL_DSP_IPC_BASE + 0x10)
#define CNL_DSP_REG_HIPCIDA         (CNL_DSP_IPC_BASE + 0x14)
#define CNL_DSP_REG_HIPCIDD         (CNL_DSP_IPC_BASE + 0x18)
#define CNL_DSP_REG_HIPCCTL         (CNL_DSP_IPC_BASE + 0x28)

#define HDA_DSP_MBOX_OFFSET         0x0000  /* SRAM_WINDOW_OFFSET(0) = 0x80000? Actually defined as SRAM_WINDOW_OFFSET(x) 0x80000 + x*0x20000, so for x=0 it's 0x80000. But this macro is used in register offsets like HDA_DSP_SRAM_REG_ROM_STATUS = HDA_DSP_MBOX_OFFSET + 0x0. So we need to define SRAM_WINDOW_OFFSET(0) properly. */
#define SRAM_WINDOW_OFFSET(x)       (0x80000 + (x) * 0x20000)
#define HDA_DSP_MBOX_OFFSET         SRAM_WINDOW_OFFSET(0)
#define HDA_DSP_SRAM_REG_ROM_STATUS (HDA_DSP_MBOX_OFFSET + 0x0)

#define CNL_SSP_BASE_OFFSET         0x10000

#define SOF_HDA_VS_D0I3C            0x104A

#define SSP_SET_SCLK_CONSUMER       BIT(25)
#define SSP_SET_SFRM_CONSUMER       BIT(24)
#define SSP_SET_CBP_CFP             (SSP_SET_SCLK_CONSUMER | SSP_SET_SFRM_CONSUMER)

/* Missing: BAR sizes are unknown; need definitions from driver. */
#define HDA_DSP_BAR0_SIZE 0 /* Placeholder – must be set */
#define HDA_DSP_BAR1_SIZE 0
#define HDA_DSP_BAR2_SIZE 0
#define HDA_DSP_BAR4_SIZE 0

/* Enums from driver (if needed) – not used in hardware state so deleted */

/* Static descriptors not directly mapped to QEMU */

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

    /* DMA Context – removed: no DMA logic in driver source */
    /* Status and Reset – removed: no static state */
    /* Power Management – deleted (handled by PCI PM) */
    /* Other Addition Info – deleted */
};

/* Other Addition Info Defin – deleted */

/* Internal helper for status-triggered signaling. Not used: removed. */

/* Device-initiated DMA logic based on driver access patterns – removed, no DMA in driver source */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No driver register access patterns provided; returning default 0. */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No driver register access patterns provided; no-op. */
}

/* PIO not referenced by driver – deleted */

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

    /* No driver-visible reset state defined; no extra reset logic. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0000 ); /* HDA_CNL_LP unknown */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x040300 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = HDA_DSP_BAR0_SIZE, .name = "bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = HDA_DSP_BAR1_SIZE, .name = "bar1" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = HDA_DSP_BAR2_SIZE, .name = "bar2" };
    s->bar_info[3] = (BARInfo){ .index = 4, .type = BAR_TYPE_MMIO, .size = HDA_DSP_BAR4_SIZE, .name = "bar4" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);

    /* No DMA, timer, or field initialization visible in driver source. */
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

    /* No extra uninit required. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sof_audio_pci_intel_cnl_pci",
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
