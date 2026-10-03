/*
 * QEMU Hisi DMA PCI device model (HIP08)
 * Generated from hisi_dma.c driver analysis
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

#define TYPE_PCIBASE_DEVICE "hisi_dma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define PCI_VENDOR_ID_HUAWEI 0x19e5
#define PCI_DEVICE_ID_HISI_DMA 0xa122
#define PCI_CLASS_ID_PCIBASE PCI_CLASS_OTHERS

/* Basic macros */
#define BIT(x) (1ULL << (x))
#define GENMASK(h, l) (((1ULL << ((h) - (l) + 1)) - 1) << (l))

/* Common Queue Registers */
#define HISI_DMA_Q_SQ_BASE_L            0x0
#define HISI_DMA_Q_SQ_BASE_H            0x4
#define HISI_DMA_Q_SQ_DEPTH             0x8
#define HISI_DMA_Q_SQ_TAIL_PTR          0xc
#define HISI_DMA_Q_CQ_BASE_L            0x10
#define HISI_DMA_Q_CQ_BASE_H            0x14
#define HISI_DMA_Q_CQ_DEPTH             0x18
#define HISI_DMA_Q_CQ_HEAD_PTR          0x1c
#define HISI_DMA_Q_CTRL0                0x20
#define HISI_DMA_Q_CTRL0_QUEUE_EN       BIT(0)
#define HISI_DMA_Q_CTRL0_QUEUE_PAUSE    BIT(4)
#define HISI_DMA_Q_CTRL1                0x24
#define HISI_DMA_Q_CTRL1_QUEUE_RESET    BIT(0)
#define HISI_DMA_Q_FSM_STS              0x30
#define HISI_DMA_Q_FSM_STS_MASK         GENMASK(3, 0)
#define HISI_DMA_Q_ERR_INT_NUM0         0x84
#define HISI_DMA_Q_ERR_INT_NUM1         0x88
#define HISI_DMA_Q_ERR_INT_NUM2         0x8c

/* HIP08 specific registers */
#define HISI_DMA_HIP08_MODE             0x217C
#define HISI_DMA_HIP08_Q_BASE           0x0
#define HISI_DMA_HIP08_Q_CTRL0_ERR_ABORT_EN BIT(2)
#define HISI_DMA_HIP08_Q_INT_STS        0x40
#define HISI_DMA_HIP08_Q_INT_MSK        0x44
#define HISI_DMA_HIP08_Q_INT_STS_MASK   GENMASK(14, 0)
#define HISI_DMA_HIP08_Q_ERR_INT_NUM3   0x90
#define HISI_DMA_HIP08_Q_ERR_INT_NUM4   0x94
#define HISI_DMA_HIP08_Q_ERR_INT_NUM5   0x98
#define HISI_DMA_HIP08_Q_ERR_INT_NUM6   0x48
#define HISI_DMA_HIP08_Q_CTRL0_SQCQ_DRCT BIT(24)

/* DMA descriptor opcodes and status (inferred from driver usage) */
#define OPCODE_MASK    GENMASK(3, 0)
#define OPCODE_M2M     0x0
#define LOCAL_IRQ_EN   BIT(31)
#define STATUS_MASK    GENMASK(15, 0)
#define STATUS_SUCC    0x0

/* Global constants */
#define HISI_DMA_HIP08_MSI_NUM           32
#define HISI_DMA_HIP08_CHAN_NUM          30
#define HISI_DMA_HIP09_MSI_NUM           4
#define HISI_DMA_HIP09_CHAN_NUM          4
#define HISI_DMA_REVISION_HIP08B         0x21
#define HISI_DMA_REVISION_HIP09A         0x30
#define HISI_DMA_Q_OFFSET                0x100
#define HISI_DMA_Q_DEPTH_VAL             1024
#define PCI_BAR_2                        2

/* Set BAR2 size to cover entire register space (0x217C + 4) */
#define PCI_BAR2_SIZE 0x4000

/* Enums */
enum hisi_dma_reg_layout {
    HISI_DMA_REG_LAYOUT_INVALID = 0,
    HISI_DMA_REG_LAYOUT_HIP08,
    HISI_DMA_REG_LAYOUT_HIP09
};

enum hisi_dma_chan_status {
    DISABLE = -1,
    IDLE = 0,
    RUN,
    CPL,
    PAUSE,
    HALT,
    ABORT,
    WAIT,
    BUFFCLR,
};

/* Descriptor structures (for DMA handling) */
struct hisi_dma_sqe {
    uint32_t dw0;
    uint32_t dw1;
    uint32_t dw2;
    uint32_t length;
    uint64_t src_addr;
    uint64_t dst_addr;
};

struct hisi_dma_cqe {
    uint32_t rsv0;
    uint32_t rsv1;
    uint16_t sq_head;
    uint16_t rsv2;
    uint16_t rsv3;
    uint16_t w0;
};

/* Per-channel state */
typedef struct HisiDMAChannel {
    /* Hardware registers (common) */
    uint32_t sq_base_l;
    uint32_t sq_base_h;
    uint32_t sq_depth;
    uint32_t sq_tail_ptr;
    uint32_t cq_base_l;
    uint32_t cq_base_h;
    uint32_t cq_depth;
    uint32_t cq_head_ptr;
    uint32_t ctrl0;
    uint32_t ctrl1;
    uint32_t fsm_sts;
    uint32_t int_sts;
    uint32_t int_msk;
    uint32_t err_int_num0;
    uint32_t err_int_num1;
    uint32_t err_int_num2;
    uint32_t err_int_num3;
    uint32_t err_int_num4;
    uint32_t err_int_num5;
    uint32_t err_int_num6;
    /* Additional DFX/status registers */
    uint32_t sq_sts;
    uint32_t cq_tail_ptr;
    uint32_t byte_cnt;
    uint32_t desp[8];
    uint32_t sq_sts2;
    /* Internal tracking */
    uint32_t sq_head;
    uint32_t cq_tail;
} HisiDMAChannel;

/* BAR descriptor */
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

    /* Device identity */
    uint32_t revision;
    enum hisi_dma_reg_layout reg_layout;
    uint32_t chan_num;

    /* Common registers (HIP08 area) */
    uint32_t mode;
    /* Pointer to per-channel state array */
    HisiDMAChannel *chan;

    /* DMA base addresses and pointers (will be used later) */
    /* For each channel: dma_addr_t sq_dma, cq_dma; */
    dma_addr_t *sq_dma;
    dma_addr_t *cq_dma;

    /* Status flags */
    uint32_t device_status;
};

/* Internal helper for raising per-channel MSI interrupt */
static void pcibase_chan_update_irq(PCIBaseState *s, int ch)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    HisiDMAChannel *chan = &s->chan[ch];

    /* If any status bit is set and not masked, fire MSI */
    if ((chan->int_sts & ~chan->int_msk) && msi_enabled(pdev)) {
        msi_notify(pdev, ch);
    }
}

/* Process a single SQE from the submission queue */
static void hisi_dma_process_sqe(PCIBaseState *s, int ch)
{
    HisiDMAChannel *chan = &s->chan[ch];
    struct hisi_dma_sqe sqe;
    struct hisi_dma_cqe cqe;
    dma_addr_t sq_addr, cq_addr;
    uint8_t *tmp;

    if (!(chan->ctrl0 & HISI_DMA_Q_CTRL0_QUEUE_EN)) {
        return;
    }

    /* Fetch SQE from guest memory */
    sq_addr = ((uint64_t)chan->sq_base_h << 32) | chan->sq_base_l;
    sq_addr += chan->sq_head * sizeof(struct hisi_dma_sqe);
    pci_dma_read(&s->parent_obj, sq_addr, &sqe, sizeof(sqe));

    /* Only process memcpy operations */
    if ((le32_to_cpu(sqe.dw0) & OPCODE_MASK) == OPCODE_M2M) {
        uint32_t len = le32_to_cpu(sqe.length);
        dma_addr_t src = le64_to_cpu(sqe.src_addr);
        dma_addr_t dst = le64_to_cpu(sqe.dst_addr);

        /* Simulate DMA transfer: copy len bytes from src to dst */
        tmp = g_malloc(len);
        pci_dma_read(&s->parent_obj, src, tmp, len);
        pci_dma_write(&s->parent_obj, dst, tmp, len);
        g_free(tmp);
    }

    /* Write completion queue entry */
    cq_addr = ((uint64_t)chan->cq_base_h << 32) | chan->cq_base_l;
    cq_addr += chan->cq_tail * sizeof(struct hisi_dma_cqe);
    memset(&cqe, 0, sizeof(cqe));
    cqe.w0 = cpu_to_le16(STATUS_SUCC);
    pci_dma_write(&s->parent_obj, cq_addr, &cqe, sizeof(cqe));

    /* Update internal pointers */
    chan->cq_tail = (chan->cq_tail + 1) % (chan->cq_depth + 1);
    chan->sq_head = (chan->sq_head + 1) % (chan->sq_depth + 1);

    /* Set completion interrupt and fire if unmasked */
    chan->int_sts |= 1;  /* bit 0 = completion */
    pcibase_chan_update_irq(s, ch);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int ch;
    uint32_t offset;

    if (size != 4) {
        return ~0ULL;
    }

    if (addr == HISI_DMA_HIP08_MODE) {
        return s->mode;
    }

    ch = addr / HISI_DMA_Q_OFFSET;
    if (ch >= s->chan_num) {
        return 0;
    }
    offset = addr % HISI_DMA_Q_OFFSET;

    switch (offset) {
    case HISI_DMA_Q_SQ_BASE_L:
        val = s->chan[ch].sq_base_l;
        break;
    case HISI_DMA_Q_SQ_BASE_H:
        val = s->chan[ch].sq_base_h;
        break;
    case HISI_DMA_Q_SQ_DEPTH:
        val = s->chan[ch].sq_depth;
        break;
    case HISI_DMA_Q_SQ_TAIL_PTR:
        val = s->chan[ch].sq_tail_ptr;
        break;
    case HISI_DMA_Q_CQ_BASE_L:
        val = s->chan[ch].cq_base_l;
        break;
    case HISI_DMA_Q_CQ_BASE_H:
        val = s->chan[ch].cq_base_h;
        break;
    case HISI_DMA_Q_CQ_DEPTH:
        val = s->chan[ch].cq_depth;
        break;
    case HISI_DMA_Q_CQ_HEAD_PTR:
        val = s->chan[ch].cq_head_ptr;
        break;
    case HISI_DMA_Q_CTRL0:
        val = s->chan[ch].ctrl0;
        break;
    case HISI_DMA_Q_CTRL1:
        val = s->chan[ch].ctrl1;
        break;
    case HISI_DMA_Q_FSM_STS:
        val = s->chan[ch].fsm_sts;
        break;
    case HISI_DMA_HIP08_Q_INT_STS:
        val = s->chan[ch].int_sts;
        break;
    case HISI_DMA_HIP08_Q_INT_MSK:
        val = s->chan[ch].int_msk;
        break;
    case HISI_DMA_Q_ERR_INT_NUM0:
        val = s->chan[ch].err_int_num0;
        break;
    case HISI_DMA_Q_ERR_INT_NUM1:
        val = s->chan[ch].err_int_num1;
        break;
    case HISI_DMA_Q_ERR_INT_NUM2:
        val = s->chan[ch].err_int_num2;
        break;
    case HISI_DMA_HIP08_Q_ERR_INT_NUM3:
        val = s->chan[ch].err_int_num3;
        break;
    case HISI_DMA_HIP08_Q_ERR_INT_NUM4:
        val = s->chan[ch].err_int_num4;
        break;
    case HISI_DMA_HIP08_Q_ERR_INT_NUM5:
        val = s->chan[ch].err_int_num5;
        break;
    case HISI_DMA_HIP08_Q_ERR_INT_NUM6:
        val = s->chan[ch].err_int_num6;
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
    int ch;
    uint32_t offset;

    if (size != 4) {
        return;
    }

    if (addr == HISI_DMA_HIP08_MODE) {
        s->mode = val;
        return;
    }

    ch = addr / HISI_DMA_Q_OFFSET;
    if (ch >= s->chan_num) {
        return;
    }
    offset = addr % HISI_DMA_Q_OFFSET;

    switch (offset) {
    case HISI_DMA_Q_SQ_BASE_L:
        s->chan[ch].sq_base_l = val;
        break;
    case HISI_DMA_Q_SQ_BASE_H:
        s->chan[ch].sq_base_h = val;
        break;
    case HISI_DMA_Q_SQ_DEPTH:
        s->chan[ch].sq_depth = val;
        break;
    case HISI_DMA_Q_SQ_TAIL_PTR:
        s->chan[ch].sq_tail_ptr = val;
        /* Trigger DMA processing if queue enabled */
        if (s->chan[ch].ctrl0 & HISI_DMA_Q_CTRL0_QUEUE_EN) {
            hisi_dma_process_sqe(s, ch);
        }
        break;
    case HISI_DMA_Q_CQ_BASE_L:
        s->chan[ch].cq_base_l = val;
        break;
    case HISI_DMA_Q_CQ_BASE_H:
        s->chan[ch].cq_base_h = val;
        break;
    case HISI_DMA_Q_CQ_DEPTH:
        s->chan[ch].cq_depth = val;
        break;
    case HISI_DMA_Q_CQ_HEAD_PTR:
        s->chan[ch].cq_head_ptr = val;
        break;
    case HISI_DMA_Q_CTRL0:
        s->chan[ch].ctrl0 = val;
        break;
    case HISI_DMA_Q_CTRL1:
        s->chan[ch].ctrl1 = val;
        break;
    case HISI_DMA_HIP08_Q_INT_STS:
        /* Write-1-to-clear */
        s->chan[ch].int_sts &= ~(val & HISI_DMA_HIP08_Q_INT_STS_MASK);
        pcibase_chan_update_irq(s, ch);
        break;
    case HISI_DMA_HIP08_Q_INT_MSK:
        s->chan[ch].int_msk = val & HISI_DMA_HIP08_Q_INT_STS_MASK;
        pcibase_chan_update_irq(s, ch);
        break;
    case HISI_DMA_Q_ERR_INT_NUM0:
        s->chan[ch].err_int_num0 = val;
        break;
    case HISI_DMA_Q_ERR_INT_NUM1:
        s->chan[ch].err_int_num1 = val;
        break;
    case HISI_DMA_Q_ERR_INT_NUM2:
        s->chan[ch].err_int_num2 = val;
        break;
    case HISI_DMA_HIP08_Q_ERR_INT_NUM3:
        s->chan[ch].err_int_num3 = val;
        break;
    case HISI_DMA_HIP08_Q_ERR_INT_NUM4:
        s->chan[ch].err_int_num4 = val;
        break;
    case HISI_DMA_HIP08_Q_ERR_INT_NUM5:
        s->chan[ch].err_int_num5 = val;
        break;
    case HISI_DMA_HIP08_Q_ERR_INT_NUM6:
        s->chan[ch].err_int_num6 = val;
        break;
    default:
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
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    /* Clear common registers */
    s->mode = 0;

    /* Reset all channels */
    for (i = 0; i < s->chan_num; i++) {
        memset(&s->chan[i], 0, sizeof(HisiDMAChannel));
    }
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_HUAWEI);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_HISI_DMA);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID_PCIBASE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, HISI_DMA_REVISION_HIP08B);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x100);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = PCI_BAR_2, .type = BAR_TYPE_MMIO, .size = PCI_BAR2_SIZE, .name = "hisi-dma-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization with per-vector masking to support multiple vectors */
    s->has_msi = true;
    if (msi_init(pdev, 0, HISI_DMA_HIP08_MSI_NUM, true, true, errp)) {
        return;
    }

    /* Final state initialization */
    s->reg_layout = HISI_DMA_REG_LAYOUT_HIP08;
    s->revision = HISI_DMA_REVISION_HIP08B;
    s->chan_num = HISI_DMA_HIP08_CHAN_NUM;
    s->chan = g_new0(HisiDMAChannel, s->chan_num);
    s->sq_dma = g_new0(dma_addr_t, s->chan_num);
    s->cq_dma = g_new0(dma_addr_t, s->chan_num);

    /* Initialize internal tracking */
    for (int i = 0; i < s->chan_num; i++) {
        s->chan[i].sq_head = 0;
        s->chan[i].cq_tail = 0;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    g_free(s->chan);
    g_free(s->sq_dma);
    g_free(s->cq_dma);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "hisi_dma_pci",
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
