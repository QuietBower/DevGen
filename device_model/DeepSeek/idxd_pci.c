/*
 * QEMU model for Intel IDXD (DSA/IAX) device
 * Based on Linux driver analysis: drivers/dma/idxd/init.c
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

/* Register offsets extracted from driver source */
#define IDXD_VER_OFFSET			0x00
#define IDXD_GENCAP_OFFSET		0x10
#define IDXD_WQCAP_OFFSET		0x20
#define IDXD_GRPCAP_OFFSET		0x30
#define IDXD_ENGCAP_OFFSET		0x38
#define IDXD_OPCAP_OFFSET		0x40
#define IDXD_GENCFG_OFFSET		0x80
#define IDXD_GENCTRL_OFFSET		0x88
#define IDXD_GENSTATS_OFFSET		0x90
#define IDXD_CMD_OFFSET			0xa0
#define IDXD_CMDSTS_OFFSET		0xa8
#define IDXD_CMDCAP_OFFSET		0xb0
#define IDXD_IAACAP_OFFSET		0x180
#define IDXD_DSACAP0_OFFSET		0x180
#define IDXD_DSACAP1_OFFSET		0x188
#define IDXD_DSACAP2_OFFSET		0x190
#define IDXD_TABLE_OFFSET		0x60
#define IDXD_TABLE_MULT			0x100

/* PCI IDs – first entry from pci_device_id table */
#define IDXD_VENDOR_ID			0x8086 /* Intel */
#define IDXD_DEVICE_ID			0x0b25 /* DSA SPR0 (placeholder, may need actual define) */
#define IDXD_CLASS_ID			0x088000 /* DMA controller – not in source */
#define IDXD_MMIO_BAR			0
#define IDXD_BAR0_SIZE			0x2000 /* Placeholder – size unknown, must hold registers and MSI-X */

/* Additional device version constants (guessed) */
#define DEVICE_VERSION_2		2
#define DEVICE_VERSION_3		3
#define IDXD_WQCFG_MIN			5

/* Hardware register structures copied from driver */
typedef union {
    struct {
        uint64_t block_on_fault:1;
        uint64_t overlap_copy:1;
        uint64_t cache_control_mem:1;
        uint64_t cache_control_cache:1;
        uint64_t cmd_cap:1;
        uint64_t rsvd:3;
        uint64_t dest_readback:1;
        uint64_t drain_readback:1;
        uint64_t rsvd2:3;
        uint64_t evl_support:2;
        uint64_t batch_continuation:1;
        uint64_t max_xfer_shift:5;
        uint64_t max_batch_shift:4;
        uint64_t max_ims_mult:6;
        uint64_t config_en:1;
        uint64_t rsvd3:32;
    };
    uint64_t bits;
} gen_cap_reg_t;

typedef union {
    struct {
        uint64_t total_wq_size:16;
        uint64_t num_wqs:8;
        uint64_t wqcfg_size:4;
        uint64_t rsvd:20;
        uint64_t shared_mode:1;
        uint64_t dedicated_mode:1;
        uint64_t wq_ats_support:1;
        uint64_t priority:1;
        uint64_t occupancy:1;
        uint64_t occupancy_int:1;
        uint64_t op_config:1;
        uint64_t wq_prs_support:1;
        uint64_t rsvd4:8;
    };
    uint64_t bits;
} wq_cap_reg_t;

typedef union {
    struct {
        uint64_t num_groups:8;
        uint64_t total_rdbufs:8;
        uint64_t rdbuf_ctrl:1;
        uint64_t rdbuf_limit:1;
        uint64_t progress_limit:1;
        uint64_t rsvd:45;
    };
    uint64_t bits;
} group_cap_reg_t;

typedef union {
    struct {
        uint64_t num_engines:8;
        uint64_t rsvd:56;
    };
    uint64_t bits;
} engine_cap_reg_t;

typedef struct {
    uint64_t bits[4];
} opcap_t;

typedef union {
    struct {
        uint64_t dec_aecs_format_ver:1;
        uint64_t drop_init_bits:1;
        uint64_t chaining:1;
        uint64_t force_array_output_mod:1;
        uint64_t load_part_aecs:1;
        uint64_t comp_early_abort:1;
        uint64_t nested_comp:1;
        uint64_t diction_comp:1;
        uint64_t header_gen:1;
        uint64_t crypto_gcm:1;
        uint64_t crypto_cfb:1;
        uint64_t crypto_xts:1;
        uint64_t rsvd:52;
    };
    uint64_t bits;
} iaa_cap_reg_t;

typedef union {
    uint64_t bits;
} dsacap1_reg_t;

typedef union {
    uint64_t bits;
} dsacap2_reg_t;

typedef union {
    struct {
        uint32_t softerr_int_en:1;
        uint32_t halt_int_en:1;
        uint32_t evl_int_en:1;
        uint32_t rsvd:29;
    };
    uint32_t bits;
} genctrl_reg_t;

typedef union {
    struct {
        uint32_t rdbuf_limit:8;
        uint32_t rsvd:4;
        uint32_t user_int_en:1;
        uint32_t evl_en:1;
        uint32_t rsvd2:18;
    };
    uint32_t bits;
} gencfg_reg_t;

typedef union {
    struct {
        uint32_t state:2;
        uint32_t reset_type:2;
        uint32_t rsvd:28;
    };
    uint32_t bits;
} gensts_reg_t;

typedef union {
    struct {
        uint32_t operand:20;
        uint32_t cmd:5;
        uint32_t rsvd:6;
        uint32_t int_req:1;
    };
    uint32_t bits;
} cmd_reg_t;

typedef struct {
    gen_cap_reg_t gen_cap;
    wq_cap_reg_t wq_cap;
    group_cap_reg_t group_cap;
    engine_cap_reg_t engine_cap;
    opcap_t opcap;
    uint32_t cmd_cap;
    iaa_cap_reg_t iaa_cap;
    dsacap1_reg_t dsacap1;
    dsacap2_reg_t dsacap2;
} idxd_hw_regs_t;

#define TYPE_PCIBASE_DEVICE "idxd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* BAR information type */
typedef enum {
    BAR_TYPE_NONE,
    BAR_TYPE_MMIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    uint64_t size;
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
    idxd_hw_regs_t hw_regs;
    uint32_t genctrl;
    uint32_t gencfg;
    uint32_t gensts;
    uint32_t cmd_status;
    cmd_reg_t cmd;
    uint64_t table_offset[2];
    /* Additional dynamic state will be added later */
};

/* MSI-X table size: one for misc, plus one per WQ */
#define IDXD_MSIX_VECTORS 2

static void pcibase_update_irq(PCIBaseState *s)
{
    /* For now, no interrupt sources are implemented; probe will not trigger any. */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case IDXD_VER_OFFSET:
        /* Version register: report device version 2 */
        val = DEVICE_VERSION_2;
        break;
    case IDXD_GENCAP_OFFSET:
        val = s->hw_regs.gen_cap.bits;
        break;
    case IDXD_WQCAP_OFFSET:
        val = s->hw_regs.wq_cap.bits;
        break;
    case IDXD_GRPCAP_OFFSET:
        val = s->hw_regs.group_cap.bits;
        break;
    case IDXD_ENGCAP_OFFSET:
        val = s->hw_regs.engine_cap.bits;
        break;
    case IDXD_OPCAP_OFFSET:
    case IDXD_OPCAP_OFFSET + 8:
    case IDXD_OPCAP_OFFSET + 16:
    case IDXD_OPCAP_OFFSET + 24:
        {
            int idx = (addr - IDXD_OPCAP_OFFSET) / 8;
            if (idx < 4) {
                val = s->hw_regs.opcap.bits[idx];
            }
        }
        break;
    case IDXD_TABLE_OFFSET:
        val = s->table_offset[0];
        break;
    case IDXD_TABLE_OFFSET + 8:
        val = s->table_offset[1];
        break;
    case IDXD_GENCFG_OFFSET:
        val = s->gencfg;
        break;
    case IDXD_GENCTRL_OFFSET:
        val = s->genctrl;
        break;
    case IDXD_GENSTATS_OFFSET:
        val = s->gensts;
        break;
    case IDXD_CMD_OFFSET:
        val = s->cmd.bits;
        break;
    case IDXD_CMDSTS_OFFSET:
        val = s->cmd_status;
        break;
    case IDXD_CMDCAP_OFFSET:
        val = s->hw_regs.cmd_cap;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case IDXD_GENCFG_OFFSET:
        s->gencfg = (uint32_t)val;
        break;
    case IDXD_GENCTRL_OFFSET:
        {
            uint32_t old = s->genctrl;
            s->genctrl = (uint32_t)val;
            if (old != s->genctrl) {
                pcibase_update_irq(s);
            }
        }
        break;
    case IDXD_CMD_OFFSET:
        s->cmd.bits = (uint32_t)val;
        break;
    case IDXD_CMDSTS_OFFSET:
        /* Write-1-to-clear behavior not implemented yet */
        s->cmd_status = (uint32_t)val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp);

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, IDXD_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, IDXD_DEVICE_ID);
    /* Set class code correctly: base=0x08, sub=0x80, prog-if=0x00 */
    pci_set_byte(pci_conf + 0x09, 0x00); /* Programming Interface */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0880); /* Base class 0x08, Subclass 0x80 */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Configuration */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = IDXD_BAR0_SIZE;
    s->bar_info[0].name = "idxd-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization: one vector for misc IRQ, plus one per WQ */
    msix_init(pdev, IDXD_MSIX_VECTORS,
              &s->bar_regions[0], 0, 0,
              &s->bar_regions[0], 0, 0x1000,
              0x90, errp);

    /* No DMA config yet */
    /* No timers */
    /* Initial state: all zeros */
}

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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Reset hardware registers to default values */
    memset(&s->hw_regs, 0, sizeof(s->hw_regs));
    s->genctrl = 0;
    s->gencfg = 0;
    s->gensts = 0;
    s->cmd_status = 0;
    s->cmd.bits = 0;

    /* Set up default capabilities */
    s->hw_regs.gen_cap.bits = (1ULL << 31) | (12ULL << 16) | (4ULL << 21);
    s->hw_regs.wq_cap.bits = 1 | (1ULL << 16);
    s->hw_regs.group_cap.bits = 2 | (2ULL << 8);
    s->hw_regs.engine_cap.bits = 2;
    s->hw_regs.opcap.bits[0] = 0;
    s->hw_regs.opcap.bits[1] = 0;
    s->hw_regs.opcap.bits[2] = 0;
    s->hw_regs.opcap.bits[3] = 0;
    s->hw_regs.cmd_cap = 0;
    s->table_offset[0] = 0;
    s->table_offset[1] = 0;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "idxd_pci",
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
