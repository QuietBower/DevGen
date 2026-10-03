/*
 * QEMU 8.2.10 virtual PCI device model for Switchtec DMA
 * Derived from Linux driver: /home/eely/linux-7.1/drivers/dma/switchtec_dma.c
 * Phase 2: Full functional implementation for driver probe success.
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

#define TYPE_PCIBASE_DEVICE "switchtec_dma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define VENDOR_ID 0x11f8   /* PCI_VENDOR_ID_MICROSEMI */
#define DEVICE_ID 0x4000
#define CLASS_ID  0x0880   /* PCI_CLASS_SYSTEM_OTHER */

/* Register Offsets */
#define SWITCHTEC_DMAC_CHAN_CTRL_OFFSET      0x1000
#define SWITCHTEC_DMAC_CHAN_CFG_STS_OFFSET   0x160000
#define SWITCHTEC_DMA_CHAN_HW_REGS_SIZE      0x1000
#define SWITCHTEC_DMA_CHAN_FW_REGS_SIZE      0x80
#define SWITCHTEC_REG_CAP                    0x80
#define SWITCHTEC_REG_CHAN_CNT               0x84
#define SWITCHTEC_REG_TAG_LIMIT              0x90
#define SWITCHTEC_REG_CHAN_STS_VEC           0x94
#define SWITCHTEC_REG_SE_BUF_CNT             0x98
#define SWITCHTEC_REG_SE_BUF_BASE            0x9a

/* Bitfields */
#define SWITCHTEC_CHAN_CTRL_PAUSE            BIT(0)
#define SWITCHTEC_CHAN_CTRL_HALT             BIT(1)
#define SWITCHTEC_CHAN_CTRL_RESET            BIT(2)
#define SWITCHTEC_CHAN_CTRL_ERR_PAUSE        BIT(3)
#define SWITCHTEC_CHAN_STS_PAUSED            BIT(9)
#define SWITCHTEC_CHAN_STS_HALTED            BIT(10)
#define SWITCHTEC_CHAN_STS_PAUSED_MASK       GENMASK(29, 13)
#define SWITCHTEC_INVALID_HFID               0xffff

#define SWITCHTEC_DMA_SQ_SIZE                (32 * 1024)
#define SWITCHTEC_DMA_CQ_SIZE                (32 * 1024)
#define SWITCHTEC_DMA_RING_SIZE              (32 * 1024)

#define PERF_BURST_SCALE_MASK                GENMASK_U32(3,   2)
#define PERF_MRRS_MASK                       GENMASK_U32(6,   4)
#define PERF_INTERVAL_MASK                   GENMASK_U32(10,  8)
#define PERF_BURST_SIZE_MASK                 GENMASK_U32(14, 12)
#define PERF_ARB_WEIGHT_MASK                 GENMASK_U32(31, 24)
#define SE_BUF_BASE_MASK                     GENMASK_U32(10,  2)
#define SE_BUF_LEN_MASK                      GENMASK_U32(20, 12)
#define SE_THRESH_MASK                       GENMASK_U32(31, 23)

#define SWITCHTEC_CHAN_ENABLE                BIT(1)
#define SWITCHTEC_SE_DFM                     BIT(5)
#define SWITCHTEC_SE_LIOF                    BIT(6)
#define SWITCHTEC_SE_BRR                     BIT(7)
#define SWITCHTEC_SE_CID_MASK                GENMASK(15, 0)
#define SWITCHTEC_CE_SC_MASK                 GENMASK(16, 0)

/* Enums */
enum chan_op {
    ENABLE_CHAN,
    DISABLE_CHAN,
};

enum switchtec_dma_opcode {
    SWITCHTEC_DMA_OPC_MEMCPY = 0,
    SWITCHTEC_DMA_OPC_RDIMM = 0x1,
    SWITCHTEC_DMA_OPC_WRIMM = 0x2,
    SWITCHTEC_DMA_OPC_RHI = 0x6,
    SWITCHTEC_DMA_OPC_NOP = 0x7,
};

#define MAX_CHANS 32

/* Device-level Register Group */
typedef struct {
    uint32_t cap;
    uint32_t chan_cnt;
    uint32_t tag_limit;
    uint32_t chan_sts_vec;
    uint32_t se_buf_cnt;
    uint16_t se_buf_base;
} SwitchtecDmaDeviceRegs;

/* Per-channel state */
struct ChanState {
    /* HW registers */
    uint16_t cq_head;
    uint16_t sq_tail;
    uint8_t ctrl;
    uint16_t status;

    /* FW registers */
    uint32_t valid_en_se;
    uint32_t cq_base_lo;
    uint32_t cq_base_hi;
    uint16_t cq_size;
    uint32_t sq_base_lo;
    uint32_t sq_base_hi;
    uint16_t sq_size;
    uint32_t int_vec;
    uint32_t perf_cfg;
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

    SwitchtecDmaDeviceRegs dev_regs;

    struct ChanState chans[MAX_CHANS];
    int num_chans;

    uint32_t status;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x80 && addr <= 0x9f) {
        switch (addr) {
        case SWITCHTEC_REG_CAP:
            val = s->dev_regs.cap;
            break;
        case SWITCHTEC_REG_CHAN_CNT:
            val = s->dev_regs.chan_cnt;
            break;
        case SWITCHTEC_REG_TAG_LIMIT:
            val = s->dev_regs.tag_limit;
            break;
        case SWITCHTEC_REG_CHAN_STS_VEC:
            val = s->dev_regs.chan_sts_vec;
            break;
        case SWITCHTEC_REG_SE_BUF_CNT:
            val = s->dev_regs.se_buf_cnt;
            break;
        case SWITCHTEC_REG_SE_BUF_BASE:
            val = s->dev_regs.se_buf_base;
            break;
        default:
            val = 0;
            break;
        }
    } else if (addr >= SWITCHTEC_DMAC_CHAN_CTRL_OFFSET &&
               addr < SWITCHTEC_DMAC_CHAN_CTRL_OFFSET + MAX_CHANS * SWITCHTEC_DMA_CHAN_HW_REGS_SIZE) {
        int chan = (addr - SWITCHTEC_DMAC_CHAN_CTRL_OFFSET) / SWITCHTEC_DMA_CHAN_HW_REGS_SIZE;
        hwaddr offset = (addr - SWITCHTEC_DMAC_CHAN_CTRL_OFFSET) % SWITCHTEC_DMA_CHAN_HW_REGS_SIZE;
        if (chan >= s->num_chans) return 0;
        struct ChanState *cs = &s->chans[chan];
        switch (offset) {
        case 0x0:
            val = cs->cq_head;
            break;
        case 0x4:
            val = cs->sq_tail;
            break;
        case 0x8:
            val = cs->ctrl;
            break;
        case 0xC:
            val = cs->status | (0 << 16);
            break;
        default:
            val = 0;
            break;
        }
    } else if (addr >= SWITCHTEC_DMAC_CHAN_CFG_STS_OFFSET &&
               addr < SWITCHTEC_DMAC_CHAN_CFG_STS_OFFSET + MAX_CHANS * SWITCHTEC_DMA_CHAN_FW_REGS_SIZE) {
        int chan = (addr - SWITCHTEC_DMAC_CHAN_CFG_STS_OFFSET) / SWITCHTEC_DMA_CHAN_FW_REGS_SIZE;
        hwaddr offset = (addr - SWITCHTEC_DMAC_CHAN_CFG_STS_OFFSET) % SWITCHTEC_DMA_CHAN_FW_REGS_SIZE;
        if (chan >= s->num_chans) return 0;
        struct ChanState *cs = &s->chans[chan];
        switch (offset) {
        case 0x00: val = cs->valid_en_se; break;
        case 0x04: val = cs->cq_base_lo; break;
        case 0x08: val = cs->cq_base_hi; break;
        case 0x0C: val = cs->cq_size; break;
        case 0x10: val = cs->sq_base_lo; break;
        case 0x14: val = cs->sq_base_hi; break;
        case 0x18: val = cs->sq_size; break;
        case 0x1C: val = cs->int_vec; break;
        case 0x20: val = cs->perf_cfg; break;
        default: val = 0; break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x80 && addr <= 0x9f) {
        switch (addr) {
        case SWITCHTEC_REG_CHAN_CNT:
            s->dev_regs.chan_cnt = val;
            break;
        case SWITCHTEC_REG_TAG_LIMIT:
            s->dev_regs.tag_limit = val;
            break;
        case SWITCHTEC_REG_CHAN_STS_VEC:
            s->dev_regs.chan_sts_vec = val;
            break;
        case SWITCHTEC_REG_SE_BUF_CNT:
            s->dev_regs.se_buf_cnt = val;
            break;
        case SWITCHTEC_REG_SE_BUF_BASE:
            s->dev_regs.se_buf_base = val & 0xFFFF;
            break;
        }
    } else if (addr >= SWITCHTEC_DMAC_CHAN_CTRL_OFFSET &&
               addr < SWITCHTEC_DMAC_CHAN_CTRL_OFFSET + MAX_CHANS * SWITCHTEC_DMA_CHAN_HW_REGS_SIZE) {
        int chan = (addr - SWITCHTEC_DMAC_CHAN_CTRL_OFFSET) / SWITCHTEC_DMA_CHAN_HW_REGS_SIZE;
        hwaddr offset = (addr - SWITCHTEC_DMAC_CHAN_CTRL_OFFSET) % SWITCHTEC_DMA_CHAN_HW_REGS_SIZE;
        if (chan >= s->num_chans) return;
        struct ChanState *cs = &s->chans[chan];
        switch (offset) {
        case 0x0:
            cs->cq_head = val & 0xFFFF;
            break;
        case 0x4:
            cs->sq_tail = val & 0xFFFF;
            break;
        case 0x8: {
            cs->ctrl = val & 0xFF;
            if (cs->ctrl & SWITCHTEC_CHAN_CTRL_HALT) {
                cs->status |= SWITCHTEC_CHAN_STS_HALTED;
            } else {
                cs->status &= ~SWITCHTEC_CHAN_STS_HALTED;
            }
            if (cs->ctrl & SWITCHTEC_CHAN_CTRL_PAUSE) {
                cs->status |= SWITCHTEC_CHAN_STS_PAUSED;
            } else {
                cs->status &= ~SWITCHTEC_CHAN_STS_PAUSED;
            }
            break;
        }
        case 0xC:
            /* read-only */
            break;
        }
    } else if (addr >= SWITCHTEC_DMAC_CHAN_CFG_STS_OFFSET &&
               addr < SWITCHTEC_DMAC_CHAN_CFG_STS_OFFSET + MAX_CHANS * SWITCHTEC_DMA_CHAN_FW_REGS_SIZE) {
        int chan = (addr - SWITCHTEC_DMAC_CHAN_CFG_STS_OFFSET) / SWITCHTEC_DMA_CHAN_FW_REGS_SIZE;
        hwaddr offset = (addr - SWITCHTEC_DMAC_CHAN_CFG_STS_OFFSET) % SWITCHTEC_DMA_CHAN_FW_REGS_SIZE;
        if (chan >= s->num_chans) return;
        struct ChanState *cs = &s->chans[chan];
        switch (offset) {
        case 0x00: cs->valid_en_se = val; break;
        case 0x04: cs->cq_base_lo = val; break;
        case 0x08: cs->cq_base_hi = val; break;
        case 0x0C: cs->cq_size = val & 0xFFFF; break;
        case 0x10: cs->sq_base_lo = val; break;
        case 0x14: cs->sq_base_hi = val; break;
        case 0x18: cs->sq_size = val & 0xFFFF; break;
        case 0x1C: cs->int_vec = val; break;
        case 0x20: cs->perf_cfg = val; break;
        }
    }
}

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

    s->dev_regs.cap = 0;
    s->dev_regs.chan_cnt = 1;
    s->dev_regs.tag_limit = 0;
    s->dev_regs.chan_sts_vec = 1;
    s->dev_regs.se_buf_cnt = 0;
    s->dev_regs.se_buf_base = 0;

    for (int i = 0; i < MAX_CHANS; i++) {
        memset(&s->chans[i], 0, sizeof(s->chans[i]));
        s->chans[i].valid_en_se = (128 << 12); /* SE buffer length = 128, base = 0 */
    }
    s->num_chans = s->dev_regs.chan_cnt;
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
    } else if (bi->type == BAR_TYPE_PIO) {
        /* PIO not supported */
        error_setg(errp, "PIO BAR not supported");
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* MSI-X capability */
    if (msix_init(pdev, 8,
                  &s->bar_regions[0], 0, 0x0,
                  &s->bar_regions[0], 0, 0x1f0000,
                  0x80,
                  errp)) {
        return;
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x200000,  /* 2 MiB */
        .name = "bar0"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->num_chans = s->dev_regs.chan_cnt;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "switchtec_dma_pci",
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

type_init(pcibase_register_types)
