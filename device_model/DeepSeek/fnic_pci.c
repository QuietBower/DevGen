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

#define TYPE_PCIBASE_DEVICE "fnic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_CISCO      0x1137
#define PCI_DEVICE_ID_CISCO_FNIC  0x0045
#define PCI_CLASS_ID             0x0C0400

#define VNIC_RES_MAGIC           0x766e6963
#define VNIC_RES_VERSION         0

enum {
    RES_TYPE_EOL = 0,
    RES_TYPE_WQ = 1,
    RES_TYPE_RQ = 2,
    RES_TYPE_CQ = 3,
    RES_TYPE_INTR_CTRL = 4,
};

#define WQ_BASE       0x40
#define RQ_BASE       0x140
#define CQ_BASE       0x240
#define INTR_BASE     0x340
#define RES_COUNT     2

/* vnic_wq_ctrl offsets */
#define VNIC_WQ_CTRL_RING_BASE         0x00
#define VNIC_WQ_CTRL_RING_SIZE         0x08
#define VNIC_WQ_CTRL_POSTED_INDEX      0x10
#define VNIC_WQ_CTRL_CQ_INDEX          0x18
#define VNIC_WQ_CTRL_ENABLE            0x20
#define VNIC_WQ_CTRL_RUNNING           0x28
#define VNIC_WQ_CTRL_FETCH_INDEX       0x30
#define VNIC_WQ_CTRL_DCA_VALUE         0x38
#define VNIC_WQ_CTRL_ERROR_INTR_ENABLE 0x40
#define VNIC_WQ_CTRL_ERROR_INTR_OFFSET 0x48
#define VNIC_WQ_CTRL_ERROR_STATUS      0x50

/* vnic_rq_ctrl offsets */
#define VNIC_RQ_CTRL_RING_BASE         0x00
#define VNIC_RQ_CTRL_RING_SIZE         0x08
#define VNIC_RQ_CTRL_POSTED_INDEX      0x10
#define VNIC_RQ_CTRL_CQ_INDEX          0x18
#define VNIC_RQ_CTRL_ENABLE            0x20
#define VNIC_RQ_CTRL_RUNNING           0x28
#define VNIC_RQ_CTRL_FETCH_INDEX       0x30
#define VNIC_RQ_CTRL_ERROR_INTR_ENABLE 0x38
#define VNIC_RQ_CTRL_ERROR_INTR_OFFSET 0x40
#define VNIC_RQ_CTRL_ERROR_STATUS      0x48
#define VNIC_RQ_CTRL_DROP_PKT_CNT      0x50
#define VNIC_RQ_CTRL_DROP_PKT_CNT_RC   0x58

/* vnic_cq_ctrl offsets */
#define VNIC_CQ_CTRL_RING_BASE         0x00
#define VNIC_CQ_CTRL_RING_SIZE         0x08
#define VNIC_CQ_CTRL_FLOW_CTRL_ENABLE  0x10
#define VNIC_CQ_CTRL_COLOR_ENABLE      0x18
#define VNIC_CQ_CTRL_CQ_HEAD           0x20
#define VNIC_CQ_CTRL_CQ_TAIL           0x28
#define VNIC_CQ_CTRL_CQ_TAIL_COLOR     0x30
#define VNIC_CQ_CTRL_INTR_ENABLE       0x38
#define VNIC_CQ_CTRL_CQ_ENTRY_ENABLE   0x40
#define VNIC_CQ_CTRL_CQ_MSG_ENABLE     0x48
#define VNIC_CQ_CTRL_INTR_OFFSET       0x50
#define VNIC_CQ_CTRL_CQ_MSG_ADDR       0x58

/* vnic_intr_ctrl offsets */
#define VNIC_INTR_CTRL_COALESCING_TIMER   0x00
#define VNIC_INTR_CTRL_COALESCING_VALUE   0x08
#define VNIC_INTR_CTRL_COALESCING_TYPE    0x10
#define VNIC_INTR_CTRL_MASK_ON_ASSERTION  0x18
#define VNIC_INTR_CTRL_MASK               0x20
#define VNIC_INTR_CTRL_INT_CREDITS        0x28
#define VNIC_INTR_CTRL_INT_CREDIT_RETURN  0x30

typedef struct VnicWqCtrl {
    uint64_t ring_base;
    uint64_t ring_size;
    uint32_t posted_index;
    uint32_t cq_index;
    uint32_t enable;
    uint32_t running;
    uint32_t fetch_index;
    uint32_t dca_value;
    uint32_t error_intr_enable;
    uint32_t error_intr_offset;
    uint32_t error_status;
} VnicWqCtrl;

typedef struct VnicRqCtrl {
    uint64_t ring_base;
    uint64_t ring_size;
    uint32_t posted_index;
    uint32_t cq_index;
    uint32_t enable;
    uint32_t running;
    uint32_t fetch_index;
    uint32_t error_intr_enable;
    uint32_t error_intr_offset;
    uint32_t error_status;
    uint32_t drop_pkt_cnt;
    uint32_t drop_pkt_cnt_rc;
} VnicRqCtrl;

typedef struct VnicCqCtrl {
    uint64_t ring_base;
    uint64_t ring_size;
    uint32_t flow_ctrl_enable;
    uint32_t color_enable;
    uint32_t cq_head;
    uint32_t cq_tail;
    uint32_t cq_tail_color;
    uint32_t intr_enable;
    uint32_t cq_entry_enable;
    uint32_t cq_msg_enable;
    uint32_t intr_offset;
    uint64_t cq_msg_addr;
} VnicCqCtrl;

typedef struct VnicIntrCtrl {
    uint32_t coalescing_timer;
    uint32_t coalescing_value;
    uint32_t coalescing_type;
    uint32_t mask_on_assertion;
    uint32_t mask;
    uint32_t int_credits;
    uint32_t int_credit_return;
} VnicIntrCtrl;

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar0;

    VnicWqCtrl wq_ctrl;
    VnicRqCtrl rq_ctrl;
    VnicCqCtrl cq_ctrl;
    VnicIntrCtrl intr_ctrl;

    uint32_t intr_status;
    uint32_t intr_mask;

    bool has_msi;
    bool has_msix;
};

static void pcibase_update_irq(PCIBaseState *s);

/* Resource table stored as a flat byte array, matching the driver's expectation */
static const uint8_t resource_table[] = {
    /* Entry 0: WQ (offset 0x08) */
    RES_TYPE_WQ, 0x00, 0x00, 0x00, 
    WQ_BASE & 0xff, (WQ_BASE >> 8) & 0xff, (WQ_BASE >> 16) & 0xff, (WQ_BASE >> 24) & 0xff,
    RES_COUNT & 0xff, (RES_COUNT >> 8) & 0xff, 0x00, 0x00,

    /* Entry 1: RQ (offset 0x14) */
    RES_TYPE_RQ, 0x00, 0x00, 0x00,
    RQ_BASE & 0xff, (RQ_BASE >> 8) & 0xff, (RQ_BASE >> 16) & 0xff, (RQ_BASE >> 24) & 0xff,
    RES_COUNT & 0xff, (RES_COUNT >> 8) & 0xff, 0x00, 0x00,

    /* Entry 2: CQ (offset 0x20) */
    RES_TYPE_CQ, 0x00, 0x00, 0x00,
    CQ_BASE & 0xff, (CQ_BASE >> 8) & 0xff, (CQ_BASE >> 16) & 0xff, (CQ_BASE >> 24) & 0xff,
    RES_COUNT & 0xff, (RES_COUNT >> 8) & 0xff, 0x00, 0x00,

    /* Entry 3: INTR (offset 0x2C) */
    RES_TYPE_INTR_CTRL, 0x00, 0x00, 0x00,
    INTR_BASE & 0xff, (INTR_BASE >> 8) & 0xff, (INTR_BASE >> 16) & 0xff, (INTR_BASE >> 24) & 0xff,
    RES_COUNT & 0xff, (RES_COUNT >> 8) & 0xff, 0x00, 0x00,

    /* Terminator: EOL (offset 0x38) */
    RES_TYPE_EOL, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

/* BAR0 header: first 8 bytes contain magic (0x766e6963) and version (0) */
static const uint8_t bar0_header[] = {
    /* Offset 0: magic (little-endian) */
    (VNIC_RES_MAGIC >> 0) & 0xff,
    (VNIC_RES_MAGIC >> 8) & 0xff,
    (VNIC_RES_MAGIC >> 16) & 0xff,
    (VNIC_RES_MAGIC >> 24) & 0xff,
    /* Offset 4: version (little-endian) */
    (VNIC_RES_VERSION >> 0) & 0xff,
    (VNIC_RES_VERSION >> 8) & 0xff,
    (VNIC_RES_VERSION >> 16) & 0xff,
    (VNIC_RES_VERSION >> 24) & 0xff,
};

static uint64_t pcibase_resource_table_read(hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    hwaddr off = addr - 0x08;
    if (off + size > sizeof(resource_table)) {
        return 0;
    }
    memcpy(&val, &resource_table[off], MIN(size, 8));
    return val;
}

static uint64_t pcibase_wq_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    VnicWqCtrl *wq = &s->wq_ctrl;
    uint64_t val = 0;
    switch (addr) {
    case VNIC_WQ_CTRL_RING_BASE:
        val = wq->ring_base;
        break;
    case VNIC_WQ_CTRL_RING_SIZE:
        val = wq->ring_size;
        break;
    case VNIC_WQ_CTRL_POSTED_INDEX:
        val = wq->posted_index;
        break;
    case VNIC_WQ_CTRL_CQ_INDEX:
        val = wq->cq_index;
        break;
    case VNIC_WQ_CTRL_ENABLE:
        val = wq->enable;
        break;
    case VNIC_WQ_CTRL_RUNNING:
        val = wq->running;
        break;
    case VNIC_WQ_CTRL_FETCH_INDEX:
        val = wq->fetch_index;
        break;
    case VNIC_WQ_CTRL_DCA_VALUE:
        val = wq->dca_value;
        break;
    case VNIC_WQ_CTRL_ERROR_INTR_ENABLE:
        val = wq->error_intr_enable;
        break;
    case VNIC_WQ_CTRL_ERROR_INTR_OFFSET:
        val = wq->error_intr_offset;
        break;
    case VNIC_WQ_CTRL_ERROR_STATUS:
        val = wq->error_status;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "fnic: bad WQ MMIO read at offset 0x%"PRIx64"\n", addr);
        return 0;
    }
    return val;
}

static void pcibase_wq_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    VnicWqCtrl *wq = &s->wq_ctrl;
    switch (addr) {
    case VNIC_WQ_CTRL_RING_BASE:
        wq->ring_base = val;
        break;
    case VNIC_WQ_CTRL_RING_SIZE:
        wq->ring_size = val;
        break;
    case VNIC_WQ_CTRL_POSTED_INDEX:
        wq->posted_index = val;
        break;
    case VNIC_WQ_CTRL_CQ_INDEX:
        wq->cq_index = val;
        break;
    case VNIC_WQ_CTRL_ENABLE:
        wq->enable = val;
        wq->running = val ? 1 : 0;
        break;
    case VNIC_WQ_CTRL_RUNNING:
        /* read-only */
        break;
    case VNIC_WQ_CTRL_FETCH_INDEX:
        wq->fetch_index = val;
        break;
    case VNIC_WQ_CTRL_DCA_VALUE:
        wq->dca_value = val;
        break;
    case VNIC_WQ_CTRL_ERROR_INTR_ENABLE:
        wq->error_intr_enable = val;
        break;
    case VNIC_WQ_CTRL_ERROR_INTR_OFFSET:
        wq->error_intr_offset = val;
        break;
    case VNIC_WQ_CTRL_ERROR_STATUS:
        wq->error_status &= ~val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "fnic: bad WQ MMIO write at offset 0x%"PRIx64"\n", addr);
    }
}

static uint64_t pcibase_rq_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    VnicRqCtrl *rq = &s->rq_ctrl;
    uint64_t val = 0;
    switch (addr) {
    case VNIC_RQ_CTRL_RING_BASE:
        val = rq->ring_base;
        break;
    case VNIC_RQ_CTRL_RING_SIZE:
        val = rq->ring_size;
        break;
    case VNIC_RQ_CTRL_POSTED_INDEX:
        val = rq->posted_index;
        break;
    case VNIC_RQ_CTRL_CQ_INDEX:
        val = rq->cq_index;
        break;
    case VNIC_RQ_CTRL_ENABLE:
        val = rq->enable;
        break;
    case VNIC_RQ_CTRL_RUNNING:
        val = rq->running;
        break;
    case VNIC_RQ_CTRL_FETCH_INDEX:
        val = rq->fetch_index;
        break;
    case VNIC_RQ_CTRL_ERROR_INTR_ENABLE:
        val = rq->error_intr_enable;
        break;
    case VNIC_RQ_CTRL_ERROR_INTR_OFFSET:
        val = rq->error_intr_offset;
        break;
    case VNIC_RQ_CTRL_ERROR_STATUS:
        val = rq->error_status;
        break;
    case VNIC_RQ_CTRL_DROP_PKT_CNT:
        val = rq->drop_pkt_cnt;
        break;
    case VNIC_RQ_CTRL_DROP_PKT_CNT_RC:
        val = rq->drop_pkt_cnt_rc;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "fnic: bad RQ MMIO read at offset 0x%"PRIx64"\n", addr);
        return 0;
    }
    return val;
}

static void pcibase_rq_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    VnicRqCtrl *rq = &s->rq_ctrl;
    switch (addr) {
    case VNIC_RQ_CTRL_RING_BASE:
        rq->ring_base = val;
        break;
    case VNIC_RQ_CTRL_RING_SIZE:
        rq->ring_size = val;
        break;
    case VNIC_RQ_CTRL_POSTED_INDEX:
        rq->posted_index = val;
        break;
    case VNIC_RQ_CTRL_CQ_INDEX:
        rq->cq_index = val;
        break;
    case VNIC_RQ_CTRL_ENABLE:
        rq->enable = val;
        rq->running = val ? 1 : 0;
        break;
    case VNIC_RQ_CTRL_RUNNING:
        /* read-only */
        break;
    case VNIC_RQ_CTRL_FETCH_INDEX:
        rq->fetch_index = val;
        break;
    case VNIC_RQ_CTRL_ERROR_INTR_ENABLE:
        rq->error_intr_enable = val;
        break;
    case VNIC_RQ_CTRL_ERROR_INTR_OFFSET:
        rq->error_intr_offset = val;
        break;
    case VNIC_RQ_CTRL_ERROR_STATUS:
        rq->error_status &= ~val;
        break;
    case VNIC_RQ_CTRL_DROP_PKT_CNT:
        break;
    case VNIC_RQ_CTRL_DROP_PKT_CNT_RC:
        rq->drop_pkt_cnt_rc = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "fnic: bad RQ MMIO write at offset 0x%"PRIx64"\n", addr);
    }
}

static uint64_t pcibase_cq_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    VnicCqCtrl *cq = &s->cq_ctrl;
    uint64_t val = 0;
    switch (addr) {
    case VNIC_CQ_CTRL_RING_BASE:
        val = cq->ring_base;
        break;
    case VNIC_CQ_CTRL_RING_SIZE:
        val = cq->ring_size;
        break;
    case VNIC_CQ_CTRL_FLOW_CTRL_ENABLE:
        val = cq->flow_ctrl_enable;
        break;
    case VNIC_CQ_CTRL_COLOR_ENABLE:
        val = cq->color_enable;
        break;
    case VNIC_CQ_CTRL_CQ_HEAD:
        val = cq->cq_head;
        break;
    case VNIC_CQ_CTRL_CQ_TAIL:
        val = cq->cq_tail;
        break;
    case VNIC_CQ_CTRL_CQ_TAIL_COLOR:
        val = cq->cq_tail_color;
        break;
    case VNIC_CQ_CTRL_INTR_ENABLE:
        val = cq->intr_enable;
        break;
    case VNIC_CQ_CTRL_CQ_ENTRY_ENABLE:
        val = cq->cq_entry_enable;
        break;
    case VNIC_CQ_CTRL_CQ_MSG_ENABLE:
        val = cq->cq_msg_enable;
        break;
    case VNIC_CQ_CTRL_INTR_OFFSET:
        val = cq->intr_offset;
        break;
    case VNIC_CQ_CTRL_CQ_MSG_ADDR:
        val = cq->cq_msg_addr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "fnic: bad CQ MMIO read at offset 0x%"PRIx64"\n", addr);
        return 0;
    }
    return val;
}

static void pcibase_cq_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    VnicCqCtrl *cq = &s->cq_ctrl;
    switch (addr) {
    case VNIC_CQ_CTRL_RING_BASE:
        cq->ring_base = val;
        break;
    case VNIC_CQ_CTRL_RING_SIZE:
        cq->ring_size = val;
        break;
    case VNIC_CQ_CTRL_FLOW_CTRL_ENABLE:
        cq->flow_ctrl_enable = val;
        break;
    case VNIC_CQ_CTRL_COLOR_ENABLE:
        cq->color_enable = val;
        break;
    case VNIC_CQ_CTRL_CQ_HEAD:
        cq->cq_head = val;
        break;
    case VNIC_CQ_CTRL_CQ_TAIL:
        cq->cq_tail = val;
        break;
    case VNIC_CQ_CTRL_CQ_TAIL_COLOR:
        cq->cq_tail_color = val;
        break;
    case VNIC_CQ_CTRL_INTR_ENABLE:
        cq->intr_enable = val;
        pcibase_update_irq(s);
        break;
    case VNIC_CQ_CTRL_CQ_ENTRY_ENABLE:
        cq->cq_entry_enable = val;
        break;
    case VNIC_CQ_CTRL_CQ_MSG_ENABLE:
        cq->cq_msg_enable = val;
        break;
    case VNIC_CQ_CTRL_INTR_OFFSET:
        cq->intr_offset = val;
        break;
    case VNIC_CQ_CTRL_CQ_MSG_ADDR:
        cq->cq_msg_addr = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "fnic: bad CQ MMIO write at offset 0x%"PRIx64"\n", addr);
    }
}

static uint64_t pcibase_intr_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    VnicIntrCtrl *intr = &s->intr_ctrl;
    uint64_t val = 0;
    switch (addr) {
    case VNIC_INTR_CTRL_COALESCING_TIMER:
        val = intr->coalescing_timer;
        break;
    case VNIC_INTR_CTRL_COALESCING_VALUE:
        val = intr->coalescing_value;
        break;
    case VNIC_INTR_CTRL_COALESCING_TYPE:
        val = intr->coalescing_type;
        break;
    case VNIC_INTR_CTRL_MASK_ON_ASSERTION:
        val = intr->mask_on_assertion;
        break;
    case VNIC_INTR_CTRL_MASK:
        val = intr->mask;
        break;
    case VNIC_INTR_CTRL_INT_CREDITS:
        val = intr->int_credits;
        break;
    case VNIC_INTR_CTRL_INT_CREDIT_RETURN:
        val = intr->int_credit_return;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "fnic: bad INTR MMIO read at offset 0x%"PRIx64"\n", addr);
        return 0;
    }
    return val;
}

static void pcibase_intr_ctrl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    VnicIntrCtrl *intr = &s->intr_ctrl;
    switch (addr) {
    case VNIC_INTR_CTRL_COALESCING_TIMER:
        intr->coalescing_timer = val;
        break;
    case VNIC_INTR_CTRL_COALESCING_VALUE:
        intr->coalescing_value = val;
        break;
    case VNIC_INTR_CTRL_COALESCING_TYPE:
        intr->coalescing_type = val;
        break;
    case VNIC_INTR_CTRL_MASK_ON_ASSERTION:
        intr->mask_on_assertion = val;
        break;
    case VNIC_INTR_CTRL_MASK:
        intr->mask = val;
        s->intr_mask = (val & 1) ? 1 : 0;
        pcibase_update_irq(s);
        break;
    case VNIC_INTR_CTRL_INT_CREDITS:
        intr->int_credits = val;
        break;
    case VNIC_INTR_CTRL_INT_CREDIT_RETURN:
        intr->int_credit_return = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "fnic: bad INTR MMIO write at offset 0x%"PRIx64"\n", addr);
    }
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = 0;
    if (s->cq_ctrl.intr_enable) pending |= 0x1;
    if (s->wq_ctrl.error_intr_enable && s->wq_ctrl.error_status) pending |= 0x2;
    if (s->rq_ctrl.error_intr_enable && s->rq_ctrl.error_status) pending |= 0x4;

    if (pending && !s->intr_mask) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x08) {
        /* Read from BAR0 header: magic and version, handle any access size */
        uint64_t val = 0;
        if (addr + size <= sizeof(bar0_header)) {
            memcpy(&val, &bar0_header[addr], MIN(size, sizeof(val)));
        }
        return val;
    } else if (addr >= 0x08 && addr < 0x40) {
        return pcibase_resource_table_read(addr, size);
    } else if (addr >= WQ_BASE && addr < WQ_BASE + 0x100) {
        return pcibase_wq_ctrl_read(s, addr - WQ_BASE, size);
    } else if (addr >= RQ_BASE && addr < RQ_BASE + 0x100) {
        return pcibase_rq_ctrl_read(s, addr - RQ_BASE, size);
    } else if (addr >= CQ_BASE && addr < CQ_BASE + 0x100) {
        return pcibase_cq_ctrl_read(s, addr - CQ_BASE, size);
    } else if (addr >= INTR_BASE && addr < INTR_BASE + 0x100) {
        return pcibase_intr_ctrl_read(s, addr - INTR_BASE, size);
    }
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x08) {
        /* header is read-only */
        return;
    } else if (addr >= 0x08 && addr < 0x40) {
        /* resource table is read-only */
        return;
    } else if (addr >= WQ_BASE && addr < WQ_BASE + 0x100) {
        pcibase_wq_ctrl_write(s, addr - WQ_BASE, val, size);
    } else if (addr >= RQ_BASE && addr < RQ_BASE + 0x100) {
        pcibase_rq_ctrl_write(s, addr - RQ_BASE, val, size);
    } else if (addr >= CQ_BASE && addr < CQ_BASE + 0x100) {
        pcibase_cq_ctrl_write(s, addr - CQ_BASE, val, size);
    } else if (addr >= INTR_BASE && addr < INTR_BASE + 0x100) {
        pcibase_intr_ctrl_write(s, addr - INTR_BASE, val, size);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(&s->wq_ctrl, 0, sizeof(s->wq_ctrl));
    memset(&s->rq_ctrl, 0, sizeof(s->rq_ctrl));
    memset(&s->cq_ctrl, 0, sizeof(s->cq_ctrl));
    memset(&s->intr_ctrl, 0, sizeof(s->intr_ctrl));
    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CISCO);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_CISCO_FNIC);
    pci_config_set_class(pci_conf, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    memory_region_init_io(&s->bar0, OBJECT(s), &pcibase_mmio_ops, s, "bar0", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    if (msix_init_exclusive_bar(pdev, 2, 1, errp)) {
        qemu_log_mask(LOG_GUEST_ERROR, "fnic: failed to initialize MSI-X");
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "fnic_pci",
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
