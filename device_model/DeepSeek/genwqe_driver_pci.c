/*
 * QEMU PCI device model for GenWQE driver (card_base.c)
 * Phase 2: Functional behavior based on driver source.
 * Register offsets updated to exact values from driver.
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

#define TYPE_PCIBASE_DEVICE "genwqe_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs extracted from driver */
#define PCI_VENDOR_ID_IBM       0x1014
#define PCI_SUBVENDOR_ID_IBM    0x1014
#define GENWQE_VENDOR_ID        PCI_VENDOR_ID_IBM
#define GENWQE_DEVICE_ID        0x044b
#define GENWQE_CLASS_CODE       0x120000
#define GENWQE_SUBSYSTEM_ID     0x035f
#define GENWQE_MSI_IRQS         4

/*
 * Register offsets from driver (exact values)
 */
#define IO_SLU_UNITCFG            0x00000000
#define IO_APP_UNITCFG            0x02000000
#define IO_SLU_BITSTREAM          0x00040040
#define IO_SLC_CFGREG_GFIR        0x00020000
#define IO_SLC_CFGREG_SOFTRESET   0x00020018
#define IO_APP_SEC_LEM_DEBUG_OVR  0x02000040
#define IO_APP_ERR_ACT_MASK       0x02000020
#define IO_SLC_VF_APPJOB_TIMEOUT  0x00050018

/* New SLC Queue registers from supplementary source */
#define IO_SLC_QUEUE_CONFIG       0x00010010
#define IO_SLC_QUEUE_STATUS       0x00010100
#define IO_SLC_QUEUE_SEGMENT      0x00010000
#define IO_SLC_QUEUE_INITSQN      0x00010020
#define IO_SLC_QUEUE_OFFSET       0x00010008
#define IO_SLC_QUEUE_WRAP         0x00010028
#define IO_SLC_QUEUE_WTIME        0x00010030
#define IO_SLC_QUEUE_ERRCNTS      0x00010038
#define IO_SLC_QUEUE_LRW          0x00010040

/* Required constants (updated from driver) */
#define IO_ILLEGAL_VALUE          0xffffffffffffffffull
#define GFIR_ERR_TRIGGER          0x0000ffff
#define GENWQE_SLU_ARCH_REQ       2
#define GENWQE_MAX_VFS            15

/* BAR0 size must cover all known registers; 64MB to be safe */
#define BAR0_SIZE (64 * MiB)

enum genwqe_card_state {
    GENWQE_CARD_UNUSED = 0,
    GENWQE_CARD_USED = 1,
    GENWQE_CARD_FATAL_ERROR = 2,
    GENWQE_CARD_RELOAD_BITSTREAM = 3,
    GENWQE_CARD_STATE_MAX,
};

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware register shadows */
    struct {
        uint64_t slu_unitcfg;
        uint64_t app_unitcfg;
        uint64_t slu_bitstream;
        uint64_t gfir;
        uint64_t softreset;
        uint64_t app_sec_lem_debug_ovr;
        uint64_t app_err_act_mask;
        uint64_t vf_appjob_timeout[GENWQE_MAX_VFS];
        /* New SLC Queue registers */
        uint64_t queue_config;
        uint64_t queue_status;
        uint64_t queue_segment;
        uint64_t queue_initsqn;
        uint64_t queue_offset;
        uint64_t queue_wrap;
        uint64_t queue_wtime;
        uint64_t queue_errcnts;
        uint64_t queue_lrw;
    } regs;

    enum genwqe_card_state card_state;
    int reset_count;
};

static void pcibase_reset(DeviceState *dev);

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case IO_SLU_UNITCFG:
        val = s->regs.slu_unitcfg;
        break;
    case IO_APP_UNITCFG:
        val = s->regs.app_unitcfg;
        break;
    case IO_SLU_BITSTREAM:
        val = s->regs.slu_bitstream;
        break;
    case IO_SLC_CFGREG_GFIR:
        val = s->regs.gfir;
        break;
    case IO_SLC_CFGREG_SOFTRESET:
        val = s->regs.softreset;
        break;
    case IO_APP_SEC_LEM_DEBUG_OVR:
        val = s->regs.app_sec_lem_debug_ovr;
        break;
    case IO_APP_ERR_ACT_MASK:
        val = s->regs.app_err_act_mask;
        break;
    /* New SLC Queue registers */
    case IO_SLC_QUEUE_CONFIG:
        val = s->regs.queue_config;
        break;
    case IO_SLC_QUEUE_STATUS:
        val = s->regs.queue_status;
        break;
    case IO_SLC_QUEUE_SEGMENT:
        val = s->regs.queue_segment;
        break;
    case IO_SLC_QUEUE_INITSQN:
        val = s->regs.queue_initsqn;
        break;
    case IO_SLC_QUEUE_OFFSET:
        val = s->regs.queue_offset;
        break;
    case IO_SLC_QUEUE_WRAP:
        val = s->regs.queue_wrap;
        break;
    case IO_SLC_QUEUE_WTIME:
        val = s->regs.queue_wtime;
        break;
    case IO_SLC_QUEUE_ERRCNTS:
        val = s->regs.queue_errcnts;
        break;
    case IO_SLC_QUEUE_LRW:
        val = s->regs.queue_lrw;
        break;
    default:
        /* VF_APPJOB_TIMEOUT registers */
        if (addr >= IO_SLC_VF_APPJOB_TIMEOUT &&
            addr < IO_SLC_VF_APPJOB_TIMEOUT + GENWQE_MAX_VFS * 8) {
            int vf = (addr - IO_SLC_VF_APPJOB_TIMEOUT) / 8;
            if (vf < GENWQE_MAX_VFS) {
                val = s->regs.vf_appjob_timeout[vf];
            }
        } else {
            /* Unmapped registers return illegal value */
            val = IO_ILLEGAL_VALUE;
        }
        break;
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case IO_SLU_UNITCFG:
        s->regs.slu_unitcfg = val;
        break;
    case IO_APP_UNITCFG:
        s->regs.app_unitcfg = val;
        break;
    case IO_SLU_BITSTREAM:
        s->regs.slu_bitstream = val;
        break;
    case IO_SLC_CFGREG_GFIR:
        /* Driver uses __genwqe_writeq to check value; maybe W1C? */
        s->regs.gfir = val;
        break;
    case IO_SLC_CFGREG_SOFTRESET:
        s->regs.softreset = val;
        if (val & 0x70ull) {
            /* Reset the device (simplified) */
            pcibase_reset(DEVICE(s));
        }
        break;
    case IO_APP_SEC_LEM_DEBUG_OVR:
        s->regs.app_sec_lem_debug_ovr = val;
        break;
    case IO_APP_ERR_ACT_MASK:
        s->regs.app_err_act_mask = val;
        break;
    /* New SLC Queue registers */
    case IO_SLC_QUEUE_CONFIG:
        s->regs.queue_config = val;
        break;
    case IO_SLC_QUEUE_STATUS:
        s->regs.queue_status = val;
        break;
    case IO_SLC_QUEUE_SEGMENT:
        s->regs.queue_segment = val;
        break;
    case IO_SLC_QUEUE_INITSQN:
        s->regs.queue_initsqn = val;
        break;
    case IO_SLC_QUEUE_OFFSET:
        s->regs.queue_offset = val;
        break;
    case IO_SLC_QUEUE_WRAP:
        s->regs.queue_wrap = val;
        break;
    case IO_SLC_QUEUE_WTIME:
        s->regs.queue_wtime = val;
        break;
    case IO_SLC_QUEUE_ERRCNTS:
        s->regs.queue_errcnts = val;
        break;
    case IO_SLC_QUEUE_LRW:
        s->regs.queue_lrw = val;
        break;
    default:
        if (addr >= IO_SLC_VF_APPJOB_TIMEOUT &&
            addr < IO_SLC_VF_APPJOB_TIMEOUT + GENWQE_MAX_VFS * 8) {
            int vf = (addr - IO_SLC_VF_APPJOB_TIMEOUT) / 8;
            if (vf < GENWQE_MAX_VFS) {
                s->regs.vf_appjob_timeout[vf] = val;
            }
        }
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Reset function */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Set registers to default values that allow driver to probe */
    s->regs.slu_unitcfg = 0x0000000100000000ULL;  /* SLU ID 1, unitcfg 0x0000 */
    s->regs.app_unitcfg = 0x0000000200000000ULL;  /* Arbitrary valid app ID */
    s->regs.slu_bitstream = 0x0000000000000001ULL; /* Non-zero to indicate access */
    s->regs.gfir = 0;
    s->regs.softreset = 0;
    s->regs.app_sec_lem_debug_ovr = 0;
    s->regs.app_err_act_mask = 0;
    for (int i = 0; i < GENWQE_MAX_VFS; i++) {
        s->regs.vf_appjob_timeout[i] = 0;
    }
    /* New Queue registers: initialize to zero */
    s->regs.queue_config = 0;
    s->regs.queue_status = 0;
    s->regs.queue_segment = 0;
    s->regs.queue_initsqn = 0;
    s->regs.queue_offset = 0;
    s->regs.queue_wrap = 0;
    s->regs.queue_wtime = 0;
    s->regs.queue_errcnts = 0;
    s->regs.queue_lrw = 0;

    s->card_state = GENWQE_CARD_UNUSED;
    s->intr_status = 0;
    s->intr_mask = 0;
}

/* Register a single BAR */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else {
        error_setg(errp, "unsupported BAR type %d", bi->type);
    }
}

/* Device realize */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  GENWQE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  GENWQE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 0: MMIO */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "BAR0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, GENWQE_MSI_IRQS, true, false, errp);

    pci_set_byte(pci_conf + 0x09, 0x12); /* Base class: Processing accelerators */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_SUBVENDOR_ID_IBM);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, GENWQE_SUBSYSTEM_ID);
}

/* Device uninit */
static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* No extra cleanup */
}

/* VMState for migration */
static const VMStateDescription vmstate_pcibase = {
    .name = "genwqe_driver_pci",
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
