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


#define TYPE_PCIBASE_DEVICE "x38_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_X38_HB 0x29e0
#define X38_RANKS 8
#define X38_RANKS_PER_CHANNEL 4
#define X38_CHANNELS 2
#define X38_MCHBAR_LOW 0x48
#define X38_MCHBAR_HIGH 0x4c
#define X38_MCHBAR_MASK 0xfffffc000ULL
#define X38_MMR_WINDOW_SIZE 16384
#define X38_TOM 0xa0
#define X38_TOM_MASK 0x3ff
#define X38_TOM_SHIFT 26
#define X38_ERRSTS 0xc8
#define X38_ERRSTS_UE 0x0002
#define X38_ERRSTS_CE 0x0001
#define X38_ERRSTS_BITS (X38_ERRSTS_UE | X38_ERRSTS_CE)
#define X38_C0DRB 0x200
#define X38_C1DRB 0x600
#define X38_DRB_MASK 0x3ff
#define X38_DRB_SHIFT 26
#define X38_C0ECCERRLOG 0x280
#define X38_C1ECCERRLOG 0x680
#define X38_ECCERRLOG_CE 0x1
#define X38_ECCERRLOG_UE 0x2
#define X38_ECCERRLOG_RANK_BITS 0x18000000
#define X38_ECCERRLOG_SYNDROME_BITS 0xff0000
#define X38_CAPID0 0xe0

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
    uint64_t mchbar;
    uint16_t tom;
    uint16_t errsts;
    uint16_t c0drb[4];
    uint16_t c1drb[4];
    uint64_t c0eccerrlog;
    uint64_t c1eccerrlog;
    uint64_t capid0;
};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= X38_C0DRB && addr < X38_C0DRB + 8) {
        int idx = (addr - X38_C0DRB) / 2;
        val = s->c0drb[idx];
    } else if (addr >= X38_C1DRB && addr < X38_C1DRB + 8) {
        int idx = (addr - X38_C1DRB) / 2;
        val = s->c1drb[idx];
    } else if (addr == X38_C0ECCERRLOG) {
        val = size == 8 ? s->c0eccerrlog : (uint32_t)s->c0eccerrlog;
    } else if (addr == X38_C0ECCERRLOG + 4) {
        val = s->c0eccerrlog >> 32;
    } else if (addr == X38_C1ECCERRLOG) {
        val = size == 8 ? s->c1eccerrlog : (uint32_t)s->c1eccerrlog;
    } else if (addr == X38_C1ECCERRLOG + 4) {
        val = s->c1eccerrlog >> 32;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to offset 0x%" HWADDR_PRIx "\n", __func__, addr);
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

    s->c0drb[0] = 4;
    s->c0drb[1] = 8;
    s->c0drb[2] = 12;
    s->c0drb[3] = 16;

    s->c1drb[0] = 20;
    s->c1drb[1] = 24;
    s->c1drb[2] = 28;
    s->c1drb[3] = 32;

    s->c0eccerrlog = 0;
    s->c1eccerrlog = 0;

    uint8_t *pci_conf = PCI_DEVICE(dev)->config;
    pci_set_long(pci_conf + X38_MCHBAR_LOW, 0xFED14000);
    pci_set_long(pci_conf + X38_MCHBAR_HIGH, 0x00000000);
    pci_set_word(pci_conf + X38_ERRSTS, 0x0000);
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_X38_HB );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    
    /* Explicitly place PM capability at 0x60 to avoid overlap with MCHBAR (0x48) */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0x60, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 0;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize custom MCHBAR MMIO region and map it statically */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops, s, "x38-mchbar", X38_MMR_WINDOW_SIZE);
    memory_region_add_subregion(pci_address_space(pdev), 0xFED14000, &s->bar_regions[0]);

    /* Set up PCI config space for MCHBAR, TOM, CAPID0, ERRSTS */
    pci_set_long(pci_conf + X38_MCHBAR_LOW, 0xFED14000);
    pci_set_long(pci_conf + X38_MCHBAR_HIGH, 0x00000000);
    
    /* Explicitly clear w1cmask to prevent overlap with wmask */
    pci_set_long(pdev->w1cmask + X38_MCHBAR_LOW, 0);
    pci_set_long(pdev->w1cmask + X38_MCHBAR_HIGH, 0);
    pci_set_long(pdev->wmask + X38_MCHBAR_LOW, 0xFFFFFFFF);
    pci_set_long(pdev->wmask + X38_MCHBAR_HIGH, 0xFFFFFFFF);

    pci_set_word(pci_conf + X38_TOM, 32); /* 32 * 64MB = 2GB */
    pci_set_byte(pci_conf + X38_CAPID0 + 8, 0x00); /* Dual channel */

    pci_set_word(pci_conf + X38_ERRSTS, 0x0000);
    
    /* Explicitly clear wmask to prevent overlap with w1cmask */
    pci_set_word(pdev->wmask + X38_ERRSTS, 0);
    pci_set_word(pdev->w1cmask + X38_ERRSTS, X38_ERRSTS_BITS);
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
    .name = "x38_edac_pci",
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
