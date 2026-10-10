/*
 * QEMU PCI device model skeleton for Intel uncore snbep
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

#define TYPE_PCIBASE_DEVICE "snr_uncore_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID        PCI_VENDOR_ID_INTEL
/* Fallback to a generic Intel PCI device ID available in pci_ids.h to satisfy the compiler */
#define PCIBASE_DEVICE_ID        PCI_DEVICE_ID_INTEL_ESB_9
#define PCIBASE_CLASS_ID         PCI_CLASS_OTHERS

#define SNBEP_CPUNODEID                  0x40
#define SNBEP_GIDNIDMAP                  0x54
#define SNBEP_PMON_BOX_CTL_RST_CTRL      (1 << 0)
#define SNBEP_PMON_BOX_CTL_RST_CTRS      (1 << 1)
#define SNBEP_PMON_BOX_CTL_FRZ           (1 << 8)
#define SNBEP_PMON_BOX_CTL_FRZ_EN        (1 << 16)
#define SNBEP_PMON_BOX_CTL_INT           (SNBEP_PMON_BOX_CTL_RST_CTRL | \
                                         SNBEP_PMON_BOX_CTL_RST_CTRS | \
                                         SNBEP_PMON_BOX_CTL_FRZ_EN)
#define SNBEP_PMON_CTL_EV_SEL_MASK       0x000000ff
#define SNBEP_PMON_CTL_UMASK_MASK        0x0000ff00
#define SNBEP_PMON_CTL_RST               (1 << 17)
#define SNBEP_PMON_CTL_EDGE_DET          (1 << 18)
#define SNBEP_PMON_CTL_EV_SEL_EXT        (1 << 21)
#define SNBEP_PMON_CTL_EN                (1 << 22)
#define SNBEP_PMON_CTL_INVERT            (1 << 23)
#define SNBEP_PMON_CTL_TRESH_MASK        0xff000000
#define SNBEP_PMON_RAW_EVENT_MASK        (SNBEP_PMON_CTL_EV_SEL_MASK | \
                                         SNBEP_PMON_CTL_UMASK_MASK | \
                                         SNBEP_PMON_CTL_EDGE_DET | \
                                         SNBEP_PMON_CTL_INVERT | \
                                         SNBEP_PMON_CTL_TRESH_MASK)
#define SNBEP_PCI_PMON_BOX_CTL           0xf4
#define SNBEP_PCI_PMON_CTL0              0xd8
#define SNBEP_PCI_PMON_CTR0              0xa0

#define SNBEP_MC_CHy_PCI_PMON_FIXED_CTL  0xf0
#define SNBEP_MC_CHy_PCI_PMON_FIXED_CTR  0xd0
#define SNR_IMC_MMIO_PMON_FIXED_CTL      0x54
#define SNR_IMC_MMIO_PMON_FIXED_CTR      0x38
#define SNR_IMC_MMIO_PMON_CTL0           0x40
#define SNR_IMC_MMIO_PMON_CTR0           0x8
#define SNR_IMC_MMIO_PMON_BOX_CTL        0x22800
#define SNR_IMC_MMIO_OFFSET              0x4000
#define SNR_IMC_MMIO_SIZE                0x4000
#define SNR_IMC_MMIO_BASE_OFFSET         0xd0
#define SNR_IMC_MMIO_BASE_MASK           0x1FFFFFFF
#define SNR_IMC_MMIO_MEM0_OFFSET         0xd8
#define SNR_IMC_MMIO_MEM0_MASK           0x7FF

#define NODE_ID_MASK                     0x7
#define GIDNIDMAP(config, id)            (((config) >> (3 * (id))) & 0x7)


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
    uint32_t box_ctl;
    uint64_t ctl0;
    uint64_t ctr0;

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

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only implement MMIO window used by snr_uncore_mmio_* helpers: box_ctl,
     * control register and counter. These are addressed via hwc->config_base
     * and uncore_mmio_box_ctl(box) which is SNR_IMC_MMIO_PMON_BOX_CTL.
     */

    if (size != 4 && size != 8) {
        return 0;
    }

    switch (addr) {
    case SNR_IMC_MMIO_PMON_BOX_CTL:
        /* Box control register shadow */
        val = s->box_ctl;
        break;
    case SNR_IMC_MMIO_PMON_CTL0:
        /* Event control register shadow (lower 32 bits of ctl0) */
        val = (uint32_t)(s->ctl0 & 0xffffffffu);
        break;
    case SNR_IMC_MMIO_PMON_CTL0 + 4:
        /* Upper 32 bits of ctl0 if accessed */
        val = (uint32_t)((s->ctl0 >> 32) & 0xffffffffu);
        break;
    case SNR_IMC_MMIO_PMON_CTR0:
        /* Counter low dword */
        val = (uint32_t)(s->ctr0 & 0xffffffffu);
        break;
    case SNR_IMC_MMIO_PMON_CTR0 + 4:
        /* Counter high dword */
        val = (uint32_t)((s->ctr0 >> 32) & 0xffffffffu);
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

    if (size != 4 && size != 8) {
        return;
    }

    switch (addr) {
    case SNR_IMC_MMIO_PMON_BOX_CTL:
        /* Box control register. Driver uses:
         *  - write IVBEP_PMON_BOX_CTL_INT to initialize (we just store it)
         *  - snr_uncore_mmio_disable_box: set FRZ bit
         *  - snr_uncore_mmio_enable_box: clear FRZ bit
         */
        s->box_ctl = (uint32_t)val;
        break;
    case SNR_IMC_MMIO_PMON_CTL0:
        /* Lower 32 bits of event control. Driver enables event by
         * writing hwc->config | SNBEP_PMON_CTL_EN.
         */
        s->ctl0 &= ~0xffffffffULL;
        s->ctl0 |= (uint32_t)val;
        break;
    case SNR_IMC_MMIO_PMON_CTL0 + 4:
        /* Upper 32 bits if ever used. Just store. */
        s->ctl0 &= 0xffffffffULL;
        s->ctl0 |= ((uint64_t)(uint32_t)val) << 32;
        break;
    case SNR_IMC_MMIO_PMON_CTR0:
        /* Counter low dword write - treat as load */
        s->ctr0 &= ~0xffffffffULL;
        s->ctr0 |= (uint32_t)val;
        break;
    case SNR_IMC_MMIO_PMON_CTR0 + 4:
        /* Counter high dword */
        s->ctr0 &= 0xffffffffULL;
        s->ctr0 |= ((uint64_t)(uint32_t)val) << 32;
        break;
    default:
        break;
    }
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

    /* On reset driver expects box_ctl and counters cleared until
     * initialization (snbep_uncore_pci_init_box / snr_uncore_mmio_init_box).
     */
    s->box_ctl = 0;
    s->ctl0 = 0;
    s->ctr0 = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Expose CPUNODEID and GIDNIDMAP config dwords with simple fixed
     * values to make snbep_pci2phy_map_init() succeed.
     *
     * CPUNODEID: low bits contain node id (mask 0x7). Use 0.
     */
    pci_set_long(pci_conf + SNBEP_CPUNODEID, 0x0);
    /* GIDNIDMAP: mapping of up to 8 nodes, 3 bits each. Map logical 0..7
     * linearly for simplicity: GIDNIDMAP(config, i) == i.
     */
    {
        uint32_t gidnid = 0;
        int i;
        for (i = 0; i < 8; i++) {
            gidnid |= ((uint32_t)i & 0x7u) << (3 * i);
        }
        pci_set_long(pci_conf + SNBEP_GIDNIDMAP, gidnid);
    }

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000;
    s->bar_info[0].name  = "snbep-uncore-mmio";
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

    /* Initialize internal shadows to reset state */
    s->box_ctl = 0;
    s->ctl0 = 0;
    s->ctr0 = 0;
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
    .name = "snr_uncore_pci",
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
