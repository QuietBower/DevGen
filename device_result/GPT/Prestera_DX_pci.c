/*
 * QEMU PCI device model for Marvell Prestera DX (simplified for driver bring-up)
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

#define TYPE_PCIBASE_DEVICE "Prestera_DX_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PRESTERA_MSG_MAX_SIZE 1500
#define PRESTERA_SUPP_FW_MAJ_VER 4
#define PRESTERA_SUPP_FW_MIN_VER 1
#define PRESTERA_PREV_FW_MAJ_VER 4
#define PRESTERA_PREV_FW_MIN_VER 0
#define PRESTERA_FW_PATH_FMT "mrvl/prestera/mvsw_prestera_fw-v%u.%u.img"
#define PRESTERA_FW_ARM64_PATH_FMT "mrvl/prestera/mvsw_prestera_fw_arm64-v%u.%u.img"
#define PRESTERA_FW_HDR_MAGIC 0x351D9D06
#define PRESTERA_FW_DL_TIMEOUT_MS 50000
#define PRESTERA_FW_BLK_SZ 1024
#define PRESTERA_FW_VER_MAJ_MUL 1000000
#define PRESTERA_FW_VER_MIN_MUL 1000
#define PRESTERA_FW_VER_MAJ(v) ((v) / PRESTERA_FW_VER_MAJ_MUL)
#define PRESTERA_FW_VER_MIN(v) \
    (((v) - (PRESTERA_FW_VER_MAJ(v) * PRESTERA_FW_VER_MAJ_MUL)) / \
            PRESTERA_FW_VER_MIN_MUL)
#define PRESTERA_FW_VER_PATCH(v) \
    ((v) - (PRESTERA_FW_VER_MAJ(v) * PRESTERA_FW_VER_MAJ_MUL) - \
            (PRESTERA_FW_VER_MIN(v) * PRESTERA_FW_VER_MIN_MUL))
#define PRESTERA_LDR_REG_OFFSET(f) offsetof(struct prestera_ldr_regs, f)
#define PRESTERA_LDR_READY_MAGIC 0xf00dfeed
#define PRESTERA_LDR_STATUS_IMG_DL BIT(0)
#define PRESTERA_LDR_STATUS_START_FW BIT(1)
#define PRESTERA_LDR_STATUS_INVALID_IMG BIT(2)
#define PRESTERA_LDR_STATUS_NOMEM BIT(3)
#define PRESTERA_LDR_REG_BASE(fw) ((fw)->ldr_regs)
#define PRESTERA_LDR_REG_ADDR(fw, reg) (PRESTERA_LDR_REG_BASE(fw) + (reg))
#define PRESTERA_LDR_READY_REG PRESTERA_LDR_REG_OFFSET(ldr_ready)
#define PRESTERA_LDR_IMG_SIZE_REG PRESTERA_LDR_REG_OFFSET(ldr_img_size)
#define PRESTERA_LDR_CTL_REG PRESTERA_LDR_REG_OFFSET(ldr_ctl_flags)
#define PRESTERA_LDR_BUF_SIZE_REG PRESTERA_LDR_REG_OFFSET(ldr_buf_size)
#define PRESTERA_LDR_BUF_OFFS_REG PRESTERA_LDR_REG_OFFSET(ldr_buf_offs)
#define PRESTERA_LDR_BUF_RD_REG PRESTERA_LDR_REG_OFFSET(ldr_buf_rd)
#define PRESTERA_LDR_BUF_WR_REG PRESTERA_LDR_REG_OFFSET(ldr_buf_wr)
#define PRESTERA_LDR_STATUS_REG PRESTERA_LDR_REG_OFFSET(ldr_status)
#define PRESTERA_LDR_CTL_DL_START BIT(0)
#define PRESTERA_EVT_QNUM_MAX 4
#define PRESTERA_CMD_QNUM_MAX 4
#define PRESTERA_FW_REG_OFFSET(f) offsetof(struct prestera_fw_regs, f)
#define PRESTERA_FW_READY_MAGIC 0xcafebabe
#define PRESTERA_FW_READY_REG PRESTERA_FW_REG_OFFSET(fw_ready)
#define PRESTERA_CMD_BUF_OFFS_REG PRESTERA_FW_REG_OFFSET(cmd_offs)
#define PRESTERA_CMD_BUF_LEN_REG PRESTERA_FW_REG_OFFSET(cmd_len)
#define PRESTERA_CMD_QNUM_REG PRESTERA_FW_REG_OFFSET(cmd_qnum)
#define PRESTERA_EVT_BUF_OFFS_REG PRESTERA_FW_REG_OFFSET(evt_offs)
#define PRESTERA_EVT_QNUM_REG PRESTERA_FW_REG_OFFSET(evt_qnum)
#define PRESTERA_CMDQ_REG_OFFSET(q, f) \
    (PRESTERA_FW_REG_OFFSET(cmdq_list) + \
     (q) * sizeof(struct prestera_fw_cmdq_regs) + \
     offsetof(struct prestera_fw_cmdq_regs, f))
#define PRESTERA_CMDQ_REQ_CTL_REG(q) PRESTERA_CMDQ_REG_OFFSET(q, req_ctl)
#define PRESTERA_CMDQ_REQ_LEN_REG(q) PRESTERA_CMDQ_REG_OFFSET(q, req_len)
#define PRESTERA_CMDQ_RCV_CTL_REG(q) PRESTERA_CMDQ_REG_OFFSET(q, rcv_ctl)
#define PRESTERA_CMDQ_RCV_LEN_REG(q) PRESTERA_CMDQ_REG_OFFSET(q, rcv_len)
#define PRESTERA_CMDQ_OFFS_REG(q) PRESTERA_CMDQ_REG_OFFSET(q, offs)
#define PRESTERA_CMDQ_LEN_REG(q) PRESTERA_CMDQ_REG_OFFSET(q, len)
#define PRESTERA_FW_STATUS_REG PRESTERA_FW_REG_OFFSET(fw_status)
#define PRESTERA_RX_STATUS_REG PRESTERA_FW_REG_OFFSET(rx_status)
#define PRESTERA_CMD_F_REQ_SENT BIT(0)
#define PRESTERA_CMD_F_REPL_RCVD BIT(1)
#define PRESTERA_CMD_F_REPL_SENT BIT(0)
#define PRESTERA_FW_EVT_CTL_STATUS_MASK GENMASK(1, 0)
#define PRESTERA_FW_EVT_CTL_STATUS_ON 0
#define PRESTERA_FW_EVT_CTL_STATUS_OFF 1
#define PRESTERA_EVTQ_REG_OFFSET(q, f) \
    (PRESTERA_FW_REG_OFFSET(evtq_list) + \
     (q) * sizeof(struct prestera_fw_evtq_regs) + \
     offsetof(struct prestera_fw_evtq_regs, f))
#define PRESTERA_EVTQ_RD_IDX_REG(q) PRESTERA_EVTQ_REG_OFFSET(q, rd_idx)
#define PRESTERA_EVTQ_WR_IDX_REG(q) PRESTERA_EVTQ_REG_OFFSET(q, wr_idx)
#define PRESTERA_EVTQ_OFFS_REG(q) PRESTERA_EVTQ_REG_OFFSET(q, offs)
#define PRESTERA_EVTQ_LEN_REG(q) PRESTERA_EVTQ_REG_OFFSET(q, len)
#define PRESTERA_FW_REG_BASE(fw) ((fw)->dev.ctl_regs)
#define PRESTERA_FW_REG_ADDR(fw, reg) PRESTERA_FW_REG_BASE((fw)) + (reg)
#define PRESTERA_FW_CMD_DEFAULT_WAIT_MS 30000
#define PRESTERA_FW_READY_WAIT_MS 20000
#define PRESTERA_DEV_ID_AC3X_98DX_55 0xC804
#define PRESTERA_DEV_ID_AC3X_98DX_65 0xC80C
#define PRESTERA_DEV_ID_ALDRIN2 0xCC1E
#define PRESTERA_DEV_ID_98DX7312M 0x981F
#define PRESTERA_DEV_ID_98DX3500 0x9820
#define PRESTERA_DEV_ID_98DX3501 0x9826
#define PRESTERA_DEV_ID_98DX3510 0x9821
#define PRESTERA_DEV_ID_98DX3520 0x9822
#define PRESTERA_DEFAULT_VID 1
#define PRESTERA_AP_PORT_MAX (10)

/* Basic helper macros from driver context */
#define BIT(nr)            (1UL << (nr))
#define GENMASK_TYPE(t, h, l) (((~(t)0) - ((t)1 << (l)) + 1) & \
                              (~(t)0 >> (sizeof(t) * 8 - 1 - (h))))
#define GENMASK(h, l)      GENMASK_TYPE(unsigned long, h, l)
#define PCI_CLASS_OTHERS        0xff

enum prestera_pci_bar_t {
    PRESTERA_PCI_BAR_FW = 2,
    PRESTERA_PCI_BAR_PP = 4,
};

enum prestera_event_type {
    PRESTERA_EVENT_TYPE_UNSPEC,

    PRESTERA_EVENT_TYPE_PORT,
    PRESTERA_EVENT_TYPE_FDB,
    PRESTERA_EVENT_TYPE_RXTX,

    PRESTERA_EVENT_TYPE_MAX
};

struct prestera_fw_header {
    uint32_t magic_number;
    uint32_t version_value;
    uint8_t reserved[8];
};

struct prestera_ldr_regs {
    uint32_t ldr_ready;
    uint32_t pad1;

    uint32_t ldr_img_size;
    uint32_t ldr_ctl_flags;

    uint32_t ldr_buf_offs;
    uint32_t ldr_buf_size;

    uint32_t ldr_buf_rd;
    uint32_t pad2;
    uint32_t ldr_buf_wr;

    uint32_t ldr_status;
};

struct prestera_fw_evtq_regs {
    uint32_t rd_idx;
    uint32_t pad1;
    uint32_t wr_idx;
    uint32_t pad2;
    uint32_t offs;
    uint32_t len;
};

struct prestera_fw_cmdq_regs {
    uint32_t req_ctl;
    uint32_t req_len;
    uint32_t rcv_ctl;
    uint32_t rcv_len;
    uint32_t offs;
    uint32_t len;
};

struct prestera_fw_regs {
    uint32_t fw_ready;
    uint32_t cmd_offs;
    uint32_t cmd_len;
    uint32_t cmd_qnum;
    uint32_t evt_offs;
    uint32_t evt_qnum;

    uint32_t fw_status;
    uint32_t rx_status;

    struct prestera_fw_cmdq_regs cmdq_list[PRESTERA_EVT_QNUM_MAX];
    struct prestera_fw_evtq_regs evtq_list[PRESTERA_CMD_QNUM_MAX];
};

#define PCIBASE_VENDOR_ID PCI_VENDOR_ID_MARVELL
#define PCIBASE_DEVICE_ID PRESTERA_DEV_ID_AC3X_98DX_55
#define PCIBASE_CLASS_ID PCI_CLASS_OTHERS

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
    uint32_t irq_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct prestera_ldr_regs ldr_regs_shadow;
    struct prestera_fw_regs fw_regs_shadow;

    /* Firmware / buffer emulation backing storage within BAR2 FW window */
    uint8_t *fw_mem;      /* 1 MiB backing for FW BAR (BAR2) */
    uint64_t fw_mem_size;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Very simple MSI/legacy IRQ model: assert when rx_status != 0 */
    if (s->fw_regs_shadow.rx_status != 0) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The provided driver never programs any engine-specific DMA registers
     * on this PCI function; firmware queues are accessed via MMIO only.
     * No bus-master DMA is modeled here.
     */
    (void)s;
    (void)is_write;
}

/* Helper to access FW BAR backing memory safely */
static inline uint32_t pcibase_fw_mem_readl(PCIBaseState *s, hwaddr off)
{
    if (!s->fw_mem || off + 4 > s->fw_mem_size) {
        return 0;
    }
    uint32_t val;
    memcpy(&val, s->fw_mem + off, sizeof(val));
    return le32_to_cpu(val);
}

static inline void pcibase_fw_mem_writel(PCIBaseState *s, hwaddr off, uint32_t val)
{
    if (!s->fw_mem || off + 4 > s->fw_mem_size) {
        return;
    }
    uint32_t tmp = cpu_to_le32(val);
    memcpy(s->fw_mem + off, &tmp, sizeof(tmp));
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* We model BAR2 as containing the FW/loader register block at the
     * beginning. Registers are little-endian and 32-bit wide.
     */

    if (size != 4) {
        /* Driver uses readl()/writel(), so 32-bit accesses only. */
        return 0;
    }

    /* Loader registers are at offset 0 within our ldr_regs_shadow */
    if (addr < sizeof(struct prestera_ldr_regs)) {
        uint32_t *p = (uint32_t *)&s->ldr_regs_shadow;
        val = p[addr / 4];
        return val;
    }

    /* Firmware registers follow loader registers in our model */
    if (addr >= 0x1000 && addr < 0x1000 + sizeof(struct prestera_fw_regs)) {
        hwaddr off = addr - 0x1000;
        uint32_t *p = (uint32_t *)&s->fw_regs_shadow;
        val = p[off / 4];
        return val;
    }

    /* For queue memory and any other region, go to backing fw_mem */
    if (s->fw_mem) {
        val = pcibase_fw_mem_readl(s, addr);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    uint32_t v32 = (uint32_t)val;

    /* Loader register block */
    if (addr < sizeof(struct prestera_ldr_regs)) {
        uint32_t *p = (uint32_t *)&s->ldr_regs_shadow;
        p[addr / 4] = v32;

        /* Implement minimal loader behavior used by driver: when driver
         * writes IMG_SIZE and then sets CTL_DL_START, we will later
         * mark IMG_DL bit in status once firmware "download" is complete.
         * For simplicity we set IMG_DL and START_FW immediately here.
         */
        if (addr == PRESTERA_LDR_CTL_REG && (v32 & PRESTERA_LDR_CTL_DL_START)) {
            /* Fake that image download just completed successfully. */
            s->ldr_regs_shadow.ldr_status |= PRESTERA_LDR_STATUS_IMG_DL;
            s->ldr_regs_shadow.ldr_status &= ~(PRESTERA_LDR_STATUS_INVALID_IMG |
                                              PRESTERA_LDR_STATUS_NOMEM);
        }
        return;
    }

    /* Firmware register block at 0x1000 */
    if (addr >= 0x1000 && addr < 0x1000 + sizeof(struct prestera_fw_regs)) {
        hwaddr off = addr - 0x1000;
        uint32_t *p = (uint32_t *)&s->fw_regs_shadow;
        p[off / 4] = v32;

        /* Specific semantics for a few registers used in wait loops */

        /* CMDQ_REQ_CTL: driver sets REQ_SENT, then waits for RCV_CTL
         * to become REPL_SENT. We fake instantaneous command completion. */
        if (off >= PRESTERA_CMDQ_REQ_CTL_REG(0) &&
            off < PRESTERA_CMDQ_REQ_CTL_REG(PRESTERA_CMD_QNUM_MAX)) {
            unsigned q = (off - PRESTERA_CMDQ_REQ_CTL_REG(0)) /
                         sizeof(struct prestera_fw_cmdq_regs);
            if (q < PRESTERA_CMD_QNUM_MAX) {
                if (v32 & PRESTERA_CMD_F_REQ_SENT) {
                    s->fw_regs_shadow.cmdq_list[q].req_ctl = v32;
                    /* Generate a dummy reply of same length */
                    s->fw_regs_shadow.cmdq_list[q].rcv_len =
                        s->fw_regs_shadow.cmdq_list[q].req_len;
                    s->fw_regs_shadow.cmdq_list[q].rcv_ctl =
                        PRESTERA_CMD_F_REPL_SENT;
                } else if (v32 & PRESTERA_CMD_F_REPL_RCVD) {
                    /* Driver acknowledges completion; clear RCV_CTL */
                    s->fw_regs_shadow.cmdq_list[q].rcv_ctl = 0;
                }
            }
        }

        /* FW_STATUS: only used with PRESTERA_FW_EVT_CTL_STATUS_MASK
         * via u32p_replace_bits() in kernel; we just store the value. */

        /* RX_STATUS: driver writes 0 to clear it in IRQ handler; when it
         * becomes 0, we lower the interrupt. */
        if (off == PRESTERA_RX_STATUS_REG) {
            s->fw_regs_shadow.rx_status = v32;
            pcibase_update_irq(s);
        }

        return;
    }

    /* For queue memory etc., store into backing fw_mem */
    pcibase_fw_mem_writel(s, addr, v32);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    /* This device does not use legacy PIO in the provided driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* No PIO support required by the driver */
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

    /* Revert registers to power-on defaults */
    memset(&s->ldr_regs_shadow, 0, sizeof(s->ldr_regs_shadow));
    memset(&s->fw_regs_shadow, 0, sizeof(s->fw_regs_shadow));

    /* Loader appears ready immediately so that prestera_fw_load() can run */
    s->ldr_regs_shadow.ldr_ready = PRESTERA_LDR_READY_MAGIC;

    /* Provide some default layout for FW memory as driver reads them after
     * prestera_fw_init() completes waiting for FW_READY_MAGIC. We will set
     * FW_READY later in realize.
     */
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

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 0;

    s->bar_info[s->num_bars].index = PRESTERA_PCI_BAR_FW;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 1 * MiB;
    s->bar_info[s->num_bars].name = "prestera-fw-bar";
    s->num_bars++;

    s->bar_info[s->num_bars].index = PRESTERA_PCI_BAR_PP;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    s->bar_info[s->num_bars].size = 1 * MiB;
    s->bar_info[s->num_bars].name = "prestera-pp-bar";
    s->num_bars++;
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Backing storage for FW BAR (BAR2) */
    s->fw_mem_size = 1 * MiB;
    s->fw_mem = g_malloc0(s->fw_mem_size);

    /* Initialize loader and FW registers */
    memset(&s->ldr_regs_shadow, 0, sizeof(s->ldr_regs_shadow));
    memset(&s->fw_regs_shadow, 0, sizeof(s->fw_regs_shadow));

    /* Loader ready so prestera_ldr_wait_reg32() completes */
    s->ldr_regs_shadow.ldr_ready = PRESTERA_LDR_READY_MAGIC;

    /* Provide some plausible buffer layout values for loader */
    s->ldr_regs_shadow.ldr_buf_offs = 0x20000;
    s->ldr_regs_shadow.ldr_buf_size = 0x10000; /* 64 KiB ring */
    s->ldr_regs_shadow.ldr_buf_rd = 0;
    s->ldr_regs_shadow.ldr_buf_wr = 0;
    s->ldr_regs_shadow.ldr_status = 0;

    /* Firmware register layout: place command and event buffers in FW BAR */
    s->fw_regs_shadow.cmd_offs = 0x30000;
    s->fw_regs_shadow.cmd_len  = 0x10000;
    s->fw_regs_shadow.cmd_qnum = 1; /* one command queue is enough for driver */

    s->fw_regs_shadow.evt_offs = 0x40000;
    s->fw_regs_shadow.evt_qnum = 1; /* one event queue */

    /* Initialize command queue 0 registers */
    s->fw_regs_shadow.cmdq_list[0].offs = 0x0;
    s->fw_regs_shadow.cmdq_list[0].len  = 0x1000; /* 4 KiB */
    s->fw_regs_shadow.cmdq_list[0].req_ctl = 0;
    s->fw_regs_shadow.cmdq_list[0].req_len = 0;
    s->fw_regs_shadow.cmdq_list[0].rcv_ctl = 0;
    s->fw_regs_shadow.cmdq_list[0].rcv_len = 0;

    /* Initialize event queue 0 registers */
    s->fw_regs_shadow.evtq_list[0].offs = 0x0;
    s->fw_regs_shadow.evtq_list[0].len  = 0x1000; /* 4 KiB */
    s->fw_regs_shadow.evtq_list[0].rd_idx = 0;
    s->fw_regs_shadow.evtq_list[0].wr_idx = 0;

    /* Mark firmware as ready so prestera_fw_wait_reg32() succeeds */
    s->fw_regs_shadow.fw_ready = PRESTERA_FW_READY_MAGIC;

    /* No MSI-X explicitly used; but driver calls pci_alloc_irq_vectors(..., PCI_IRQ_MSI)
     * so we provide MSI capability.
     */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
    }

    s->has_msix = false;

    /* Final state initialization before the device is 'live' */
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

    if (s->fw_mem) {
        g_free(s->fw_mem);
        s->fw_mem = NULL;
    }

    /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "Prestera_DX_pci",
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
