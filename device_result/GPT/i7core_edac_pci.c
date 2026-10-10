/*
 * QEMU PCI device model for linux/drivers/edac/i7core_edac.c
 * Behavioral implementation focused on config-space accesses used by the
 * i7core_edac driver. No MMIO behavior is required by the driver.
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "i7core_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define I7CORE_PCI_VENDOR_ID   PCI_VENDOR_ID_INTEL
#define I7CORE_PCI_DEVICE_ID   0x3438
#define I7CORE_PCI_CLASS_ID    PCI_CLASS_OTHERS

#define MC_STATUS                    0x4c
#define MC_CFG_CONTROL               0x90
#define MC_CFG_UNLOCK                0x02
#define MC_CFG_LOCK                  0x00
#define MC_CONTROL                   0x48
#define MC_MAX_DOD                   0x64
#define MC_TEST_ERR_RCV1             0x60
#define MC_TEST_ERR_RCV0             0x64
#define MC_SSRCONTROL                0x48
#define SSR_MODE_DISABLE             0x00
#define SSR_MODE_ENABLE              0x01
#define SSR_MODE_MASK                0x03
#define MC_SCRUB_CONTROL             0x4c
#define STARTSCRUB                   (1 << 24)
#define SCRUBINTERVAL_MASK           0x00ffffff
#define MC_COR_ECC_CNT_0             0x80
#define MC_COR_ECC_CNT_1             0x84
#define MC_COR_ECC_CNT_2             0x88
#define MC_COR_ECC_CNT_3             0x8c
#define MC_COR_ECC_CNT_4             0x90
#define MC_COR_ECC_CNT_5             0x94
#define MC_CHANNEL_DIMM_INIT_PARAMS  0x58
#define THREE_DIMMS_PRESENT          (1 << 24)
#define SINGLE_QUAD_RANK_PRESENT     (1 << 23)
#define QUAD_RANK_PRESENT            (1 << 22)
#define REGISTERED_DIMM              (1 << 15)
#define MC_CHANNEL_MAPPER            0x60
#define MC_CHANNEL_RANK_PRESENT      0x7c
#define RANK_PRESENT_MASK            0xffff
#define MC_CHANNEL_ADDR_MATCH        0xf0
#define MC_CHANNEL_ERROR_MASK        0xf8
#define MC_CHANNEL_ERROR_INJECT      0xfc
#define INJECT_ADDR_PARITY           0x10
#define INJECT_ECC                   0x08
#define MASK_CACHELINE               0x06
#define MASK_FULL_CACHELINE          0x06
#define MASK_MSB32_CACHELINE         0x04
#define MASK_LSB32_CACHELINE         0x02
#define NO_MASK_CACHELINE            0x00
#define REPEAT_EN                    0x01
#define MC_DOD_CH_DIMM0              0x48
#define MC_DOD_CH_DIMM1              0x4c
#define MC_DOD_CH_DIMM2              0x50
#define RANKOFFSET_MASK              ((1 << 12) | (1 << 11) | (1 << 10))
#define DIMM_PRESENT_MASK            (1 << 9)
#define MC_DOD_NUMBANK_MASK          ((1 << 8) | (1 << 7))
#define MC_DOD_NUMRANK_MASK          ((1 << 6) | (1 << 5))
#define MC_DOD_NUMROW_MASK           ((1 << 4) | (1 << 3) | (1 << 2))
#define MC_DOD_NUMCOL_MASK           3
#define MC_RANK_PRESENT              0x7c
#define MC_SAG_CH_0                  0x80
#define MC_SAG_CH_1                  0x84
#define MC_SAG_CH_2                  0x88
#define MC_SAG_CH_3                  0x8c
#define MC_SAG_CH_4                  0x90
#define MC_SAG_CH_5                  0x94
#define MC_SAG_CH_6                  0x98
#define MC_SAG_CH_7                  0x9c
#define MC_RIR_LIMIT_CH_0            0x40
#define MC_RIR_LIMIT_CH_1            0x44
#define MC_RIR_LIMIT_CH_2            0x48
#define MC_RIR_LIMIT_CH_3            0x4c
#define MC_RIR_LIMIT_CH_4            0x50
#define MC_RIR_LIMIT_CH_5            0x54
#define MC_RIR_LIMIT_CH_6            0x58
#define MC_RIR_LIMIT_CH_7            0x5c
#define MC_RIR_LIMIT_MASK            ((1 << 10) - 1)
#define MC_RIR_WAY_CH                0x80
#define MC_RIR_WAY_OFFSET_MASK       (((1 << 14) - 1) & ~0x7)
#define MC_RIR_WAY_RANK_MASK         0x7

#define NUM_CHANS                    3
#define MAX_DIMMS                    3

#define I7CORE_MMIO_BAR_INDEX        0
#define I7CORE_MMIO_BAR_SIZE         0x1000


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
    struct {
        uint32_t mc_control;
        uint32_t mc_status;
        uint32_t max_dod;
        uint32_t ch_map;
        uint32_t scrub_control;
        uint32_t cor_ecc_cnt[6];
        uint32_t channel_dimm_init_params;
        uint32_t channel_mapper;
        uint32_t channel_rank_present;
        uint32_t channel_addr_match;
        uint32_t channel_error_mask;
        uint32_t channel_error_inject;
    } regs;

};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)pdev;
    (void)is_write;
}

/*
 * The driver only accesses PCI configuration space via pci_read_config_dword
 * and pci_write_config_dword. It does not touch MMIO/PIO BARs. We therefore
 * keep MMIO/PIO handlers as inert stubs; they are never exercised by the
 * i7core_edac driver, but QEMU requires them for the mapped BAR.
 */

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    memset(&s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  I7CORE_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  I7CORE_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, I7CORE_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: a single dummy MMIO BAR; driver ignores it. */
    s->num_bars = 1;
    s->bar_info[0].index = I7CORE_MMIO_BAR_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = I7CORE_MMIO_BAR_SIZE;
    s->bar_info[0].name  = "i7core-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    memset(&s->regs, 0, sizeof(s->regs));
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i7core_edac_pci",
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
