/*
 * QEMU PCI device model for Marvell Prestera DX (AC3X 98DX)
 * Based on Linux driver prestera_pci.c.
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
/* No additional headers needed */

#define TYPE_PCIBASE_DEVICE "Prestera_DX_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI_VENDOR_ID_MARVELL already defined in pci_ids.h */
#define PRESTERA_DEV_ID_AC3X_98DX_55 0xC804
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

#define PRESTERA_LDR_READY_MAGIC  0xf00dfeed
#define PRESTERA_LDR_STATUS_IMG_DL  BIT(0)
#define PRESTERA_LDR_STATUS_START_FW  BIT(1)
#define PRESTERA_LDR_STATUS_INVALID_IMG  BIT(2)
#define PRESTERA_LDR_STATUS_NOMEM  BIT(3)
#define PRESTERA_LDR_CTL_DL_START  BIT(0)

#define PRESTERA_EVT_QNUM_MAX  4
#define PRESTERA_CMD_QNUM_MAX  4

#define PRESTERA_FW_READY_MAGIC  0xcafebabe
#define PRESTERA_CMD_F_REQ_SENT  BIT(0)
#define PRESTERA_CMD_F_REPL_RCVD BIT(1)
#define PRESTERA_CMD_F_REPL_SENT BIT(0)

/* Register offset macros using struct offsets */
#define PRESTERA_LDR_REG_OFFSET(f)  offsetof(struct prestera_ldr_regs, f)
#define PRESTERA_LDR_READY_REG      PRESTERA_LDR_REG_OFFSET(ldr_ready)
#define PRESTERA_LDR_IMG_SIZE_REG   PRESTERA_LDR_REG_OFFSET(ldr_img_size)
#define PRESTERA_LDR_CTL_REG        PRESTERA_LDR_REG_OFFSET(ldr_ctl_flags)
#define PRESTERA_LDR_BUF_SIZE_REG   PRESTERA_LDR_REG_OFFSET(ldr_buf_size)
#define PRESTERA_LDR_BUF_OFFS_REG   PRESTERA_LDR_REG_OFFSET(ldr_buf_offs)
#define PRESTERA_LDR_BUF_RD_REG     PRESTERA_LDR_REG_OFFSET(ldr_buf_rd)
#define PRESTERA_LDR_BUF_WR_REG     PRESTERA_LDR_REG_OFFSET(ldr_buf_wr)
#define PRESTERA_LDR_STATUS_REG     PRESTERA_LDR_REG_OFFSET(ldr_status)

#define PRESTERA_FW_REG_OFFSET(f)  offsetof(struct prestera_fw_regs, f)
#define PRESTERA_FW_READY_REG      PRESTERA_FW_REG_OFFSET(fw_ready)
#define PRESTERA_CMD_BUF_OFFS_REG  PRESTERA_FW_REG_OFFSET(cmd_offs)
#define PRESTERA_CMD_BUF_LEN_REG   PRESTERA_FW_REG_OFFSET(cmd_len)
#define PRESTERA_CMD_QNUM_REG      PRESTERA_FW_REG_OFFSET(cmd_qnum)
#define PRESTERA_EVT_BUF_OFFS_REG  PRESTERA_FW_REG_OFFSET(evt_offs)
#define PRESTERA_EVT_QNUM_REG      PRESTERA_FW_REG_OFFSET(evt_qnum)
#define PRESTERA_CMDQ_REG_OFFSET(q, f) \
        (PRESTERA_FW_REG_OFFSET(cmdq_list) + \
         (q) * sizeof(struct prestera_fw_cmdq_regs) + \
         offsetof(struct prestera_fw_cmdq_regs, f))
#define PRESTERA_CMDQ_REQ_CTL_REG(q)  PRESTERA_CMDQ_REG_OFFSET(q, req_ctl)
#define PRESTERA_CMDQ_REQ_LEN_REG(q)  PRESTERA_CMDQ_REG_OFFSET(q, req_len)
#define PRESTERA_CMDQ_RCV_CTL_REG(q)  PRESTERA_CMDQ_REG_OFFSET(q, rcv_ctl)
#define PRESTERA_CMDQ_RCV_LEN_REG(q)  PRESTERA_CMDQ_REG_OFFSET(q, rcv_len)
#define PRESTERA_CMDQ_OFFS_REG(q)     PRESTERA_CMDQ_REG_OFFSET(q, offs)
#define PRESTERA_CMDQ_LEN_REG(q)      PRESTERA_CMDQ_REG_OFFSET(q, len)
#define PRESTERA_FW_STATUS_REG        PRESTERA_FW_REG_OFFSET(fw_status)
#define PRESTERA_RX_STATUS_REG        PRESTERA_FW_REG_OFFSET(rx_status)
#define PRESTERA_EVTQ_REG_OFFSET(q, f) \
        (PRESTERA_FW_REG_OFFSET(evtq_list) + \
         (q) * sizeof(struct prestera_fw_evtq_regs) + \
         offsetof(struct prestera_fw_evtq_regs, f))
#define PRESTERA_EVTQ_RD_IDX_REG(q)   PRESTERA_EVTQ_REG_OFFSET(q, rd_idx)
#define PRESTERA_EVTQ_WR_IDX_REG(q)   PRESTERA_EVTQ_REG_OFFSET(q, wr_idx)
#define PRESTERA_EVTQ_OFFS_REG(q)     PRESTERA_EVTQ_REG_OFFSET(q, offs)
#define PRESTERA_EVTQ_LEN_REG(q)      PRESTERA_EVTQ_REG_OFFSET(q, len)

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

/* Hardware register structures (from driver) */
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

struct prestera_fw_cmdq_regs {
    uint32_t req_ctl;
    uint32_t req_len;
    uint32_t rcv_ctl;
    uint32_t rcv_len;
    uint32_t offs;
    uint32_t len;
};

struct prestera_fw_evtq_regs {
    uint32_t rd_idx;
    uint32_t pad1;
    uint32_t wr_idx;
    uint32_t pad2;
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
    struct prestera_fw_cmdq_regs cmdq_list[PRESTERA_CMD_QNUM_MAX];
    struct prestera_fw_evtq_regs evtq_list[PRESTERA_EVT_QNUM_MAX];
};

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct prestera_ldr_regs ldr;
    struct prestera_fw_regs fw;

    /* Placeholders for ring buffers (to be allocated during realize) */
    uint8_t *ldr_ring_buf;
    uint32_t ldr_buf_len;
    uint32_t ldr_wr_idx;
    uint8_t *cmd_mbox;
    size_t cmd_mbox_len;
    uint8_t *evt_buf;
    uint8_t *evt_msg;

    /* Additional state for emulation */
    uint8_t *bar2_ram;          /* Backing store for BAR2 MMIO */
    bool fw_active;             /* Firmware is running, FW regs visible */
    uint32_t ldr_img_size;      /* Expected firmware image size */
    uint32_t ldr_consumed;      /* Bytes consumed from ring buffer */
    uint32_t ldr_last_wr;       /* Previous write pointer value */
    uint32_t ldr_rd_base;       /* Read pointer at start of download */
};

/* Additional definitions */
#define PRESTERA_DEFAULT_IRQ_PIN  1
#define PRESTERA_MSI_VECTOR_COUNT 1

/* Forward declarations */
static void pcibase_update_irq(PCIBaseState *s);
static void pcibase_start_firmware(PCIBaseState *s);

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->fw_active && s->fw.rx_status != 0) {
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

/* Helper to initialize firmware registers and transition to FW mode */
static void pcibase_start_firmware(PCIBaseState *s)
{
    s->fw_active = true;
    s->fw.fw_ready = PRESTERA_FW_READY_MAGIC;

    /* Initialize FW registers with static configuration */
    s->fw.cmd_offs = 0x2000;      /* Command mailbox offset in BAR2 */
    s->fw.cmd_len = 0x1000;       /* Command mailbox size */
    s->fw.cmd_qnum = 1;           /* One command queue */
    s->fw.evt_offs = 0x4000;      /* Event buffer offset */
    s->fw.evt_qnum = 1;           /* One event queue */
    s->fw.fw_status = 0;
    s->fw.rx_status = 0;

    /* Command queue 0 */
    s->fw.cmdq_list[0].req_ctl = 0;
    s->fw.cmdq_list[0].req_len = 0;
    s->fw.cmdq_list[0].rcv_ctl = 0;
    s->fw.cmdq_list[0].rcv_len = 0;
    s->fw.cmdq_list[0].offs = 0;   /* Queue starts at beginning of mailbox */
    s->fw.cmdq_list[0].len = 0x1000; /* Same as mailbox length */

    /* Event queue 0 */
    s->fw.evtq_list[0].rd_idx = 0;
    s->fw.evtq_list[0].wr_idx = 0;
    s->fw.evtq_list[0].offs = 0;   /* Queue starts at beginning of evt buffer */
    s->fw.evtq_list[0].len = 0x100; /* Small event queue */
}

/* Handle writes to LDR registers */
static void pcibase_ldr_write(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    uint32_t *reg_ptr = (uint32_t *)((uint8_t *)&s->ldr + addr);
    *reg_ptr = val;

    if (addr == PRESTERA_LDR_CTL_REG) {
        if (val & PRESTERA_LDR_CTL_DL_START) {
            /* Start firmware download */
            s->ldr_img_size = s->ldr.ldr_img_size;
            s->ldr.ldr_status = PRESTERA_LDR_STATUS_IMG_DL;

            /* Consume all pre-written data immediately */
            uint32_t rd = s->ldr.ldr_buf_rd;
            uint32_t wr = s->ldr.ldr_buf_wr;
            uint32_t buf_size = s->ldr.ldr_buf_size;
            s->ldr_rd_base = rd;
            s->ldr_consumed = (wr - rd) & (buf_size - 1);
            s->ldr_last_wr = wr;
            s->ldr.ldr_buf_rd = wr; /* Hardware consumed all existing data */

            /* If image already fully present, start firmware immediately */
            if (s->ldr_consumed >= s->ldr_img_size && s->ldr_img_size > 0) {
                s->ldr.ldr_status &= ~PRESTERA_LDR_STATUS_IMG_DL;
                s->ldr.ldr_status |= PRESTERA_LDR_STATUS_START_FW;
                pcibase_start_firmware(s);
            }
        }
    } else if (addr == PRESTERA_LDR_BUF_WR_REG) {
        /* Update write pointer */
        if (s->ldr.ldr_status & PRESTERA_LDR_STATUS_IMG_DL) {
            uint32_t prev_wr = s->ldr_last_wr;
            uint32_t new_wr = val;
            uint32_t buf_size = s->ldr.ldr_buf_size;
            uint32_t delta = (new_wr - prev_wr) & (buf_size - 1);
            s->ldr_consumed += delta;
            s->ldr_last_wr = new_wr;
            s->ldr.ldr_buf_rd = new_wr; /* Hardware consumes all newly written data */

            /* Check for download completion */
            if (s->ldr_consumed >= s->ldr_img_size && s->ldr_img_size > 0) {
                s->ldr.ldr_status &= ~PRESTERA_LDR_STATUS_IMG_DL;
                s->ldr.ldr_status |= PRESTERA_LDR_STATUS_START_FW;
                pcibase_start_firmware(s);
            }
        } else {
            /* Not in download mode, just track last write */
            s->ldr_last_wr = val;
        }
    }
}

/* Handle writes to FW registers */
static void pcibase_fw_write(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr >= sizeof(s->fw)) {
        return;
    }

    /* Determine if this write is to a command queue control register */
    bool cmd_handled = false;
    for (int q = 0; q < PRESTERA_CMD_QNUM_MAX; q++) {
        hwaddr req_ctl_off = PRESTERA_CMDQ_REQ_CTL_REG(q);
        hwaddr rcv_ctl_off = PRESTERA_CMDQ_RCV_CTL_REG(q);
        if (addr == req_ctl_off) {
            /* Write to command request control */
            if (val == PRESTERA_CMD_F_REQ_SENT) {
                /* Driver sent a command: we need to prepare a reply */
                /* Get the command queue registers from shadow */
                struct prestera_fw_cmdq_regs *cq = &s->fw.cmdq_list[q];
                uint32_t req_len = cq->req_len;

                /* Command buffer base in BAR2 RAM */
                uint32_t cmd_mbox_offs = s->fw.cmd_offs;
                uint32_t cmdq_offs = cq->offs;
                hwaddr buf_addr = cmd_mbox_offs + cmdq_offs;

                /* Write a dummy reply (zero word) after the request */
                uint32_t reply_word = 0;
                memcpy(s->bar2_ram + buf_addr + req_len, &reply_word, sizeof(reply_word));

                /* Update queue registers: set reply length and control */
                cq->rcv_len = sizeof(reply_word);
                cq->rcv_ctl = PRESTERA_CMD_F_REPL_SENT;
            }
            cmd_handled = true;
            break;
        } else if (addr == rcv_ctl_off) {
            /* Driver clears the reply control (e.g., PRESTERA_CMD_F_REPL_RCVD) */
            /* Just update the shadow */
            *(uint32_t *)((uint8_t *)&s->fw + addr) = val;
            cmd_handled = true;
            break;
        }
    }
    if (cmd_handled) {
        return;
    }

    /* Default: write to shadow */
    *(uint32_t *)((uint8_t *)&s->fw + addr) = val;

    /* Special handling for rx_status clear */
    if (addr == PRESTERA_RX_STATUS_REG && val == 0) {
        s->fw.rx_status = 0;
        pcibase_update_irq(s);
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x2000000) {
        return -1;
    }

    if (s->fw_active) {
        /* Firmware register space (first 4KB) */
        if (addr < 0x1000) {
            /* Map to FW shadow if within struct size */
            if (addr < sizeof(s->fw)) {
                memcpy(&val, (uint8_t *)&s->fw + addr, MIN(size, 4));
            } else {
                val = 0;
            }
        } else {
            /* Buffer area: read from BAR2 RAM */
            memcpy(&val, s->bar2_ram + addr, MIN(size, 4));
        }
    } else {
        /* LDR register space (first 40 bytes) */
        if (addr < sizeof(s->ldr)) {
            memcpy(&val, (uint8_t *)&s->ldr + addr, MIN(size, 4));
        } else {
            /* Buffer area: read from BAR2 RAM */
            memcpy(&val, s->bar2_ram + addr, MIN(size, 4));
        }
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x2000000) {
        return;
    }

    if (s->fw_active) {
        if (addr < 0x1000) {
            /* FW register write */
            pcibase_fw_write(s, addr, (uint32_t)val);
        } else {
            /* Buffer area: write to BAR2 RAM */
            memcpy(s->bar2_ram + addr, &val, MIN(size, 4));
        }
    } else {
        if (addr < sizeof(s->ldr)) {
            /* LDR register write */
            pcibase_ldr_write(s, addr, (uint32_t)val);
        } else {
            /* Buffer area: write to BAR2 RAM */
            memcpy(s->bar2_ram + addr, &val, MIN(size, 4));
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    /* Reset register shadows to power-on defaults */
    memset(&s->ldr, 0, sizeof(s->ldr));
    memset(&s->fw, 0, sizeof(s->fw));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->ldr_wr_idx = 0;
    s->fw_active = false;
    s->ldr_consumed = 0;
    s->ldr_img_size = 0;
    s->ldr_last_wr = 0;
    s->ldr_rd_base = 0;

    /* Set initial LDR state */
    s->ldr.ldr_ready = PRESTERA_LDR_READY_MAGIC;
    s->ldr.ldr_buf_offs = 0x1000;
    s->ldr.ldr_buf_size = 0x1000;

    /* Clear BAR2 RAM if allocated */
    if (s->bar2_ram) {
        memset(s->bar2_ram, 0, 0x2000000);
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MARVELL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PRESTERA_DEV_ID_AC3X_98DX_55 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, PRESTERA_DEFAULT_IRQ_PIN);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Enable MSI */
    if (msi_init(pdev, 0, PRESTERA_MSI_VECTOR_COUNT, true, false, errp) < 0) {
        error_setg(errp, "Failed to initialize MSI");
        return;
    }
    s->has_msi = true;

    /* Allocate BAR2 backing RAM */
    s->bar2_ram = g_malloc0(0x2000000);

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000000;  /* 32 MB */
    s->bar_info[0].name = "bar2-fw";
    s->bar_info[1].index = 4;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 1 * MiB;    /* PP registers */
    s->bar_info[1].name = "bar4-pp";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Other state initialization */
    s->fw_active = false;
    s->ldr_consumed = 0;
    s->ldr_last_wr = 0;
    s->ldr_img_size = 0;
    s->ldr_rd_base = 0;
    /* LDR initial values already set in reset, but set again */
    s->ldr.ldr_ready = PRESTERA_LDR_READY_MAGIC;
    s->ldr.ldr_buf_offs = 0x1000;
    s->ldr.ldr_buf_size = 0x1000;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }

    g_free(s->bar2_ram);
    g_free(s->ldr_ring_buf);
    g_free(s->cmd_mbox);
    g_free(s->evt_buf);
    g_free(s->evt_msg);
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
