/*
 * QEMU 8.2.10 SNIC PCI device model.
 * Emulates enough of the VNIC interface to allow snic driver probe success.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "snic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_SNIC   0x1137
#define PCI_DEVICE_ID_SNIC   0x0046
#define PCI_CLASS_SNIC       PCI_CLASS_STORAGE_SCSI

/* VNIC resource types */
enum snic_res_type {
    SNIC_RES_TYPE_WQ = 0,
    SNIC_RES_TYPE_RQ = 1,
    SNIC_RES_TYPE_CQ = 2,
    SNIC_RES_TYPE_INTR = 3,
    SNIC_RES_TYPE_DEVCMD2 = 4,
};

#define SNIC_MAX_INTR 2
#define SNIC_MAX_WQ   2
#define SNIC_MAX_CQ   2
#define SNIC_MAX_DEVCMD2 1

#define RES_TYPE_EOL 0xFF

/* BAR0 layout */
#define SNIC_BAR0_SIZE (64 * 1024)

/* Header area: 0x00 - 0xFF */
#define SNIC_HEADER_SIZE 0x100

/* Resource control pages: each 256 bytes, aligned to 0x100 */
#define SNIC_INTR_OFFSET(i)    (0x100 + (i) * 0x100)
#define SNIC_WQ_OFFSET(i)      (0x300 + (i) * 0x100)
#define SNIC_CQ_OFFSET(i)      (0x500 + (i) * 0x100)
#define SNIC_DEVCMD2_OFFSET    0x700   /* page for devcmd2 WQ */

/* VNIC resource stride (bytes per resource instance) */
#define VNIC_RES_STRIDE 0x100

/* Register offsets within INTR page - based on struct vnic_intr_ctrl */
#define INTR_COALESCING_TIMER_OFF    0x00
#define INTR_COALESCING_VALUE_OFF    0x08
#define INTR_COALESCING_TYPE_OFF     0x10
#define INTR_MASK_ON_ASSERTION_OFF   0x18
#define INTR_MASK_OFF                0x20
#define INTR_CREDITS_OFF             0x28
#define INTR_CREDIT_RETURN_OFF       0x30

/* WQ register offsets - based on struct vnic_wq_ctrl */
#define WQ_RING_BASE_LO_OFF          0x00
#define WQ_RING_BASE_HI_OFF          0x04
#define WQ_RING_SIZE_OFF             0x08
#define WQ_POSTED_INDEX_OFF          0x10
#define WQ_CQ_INDEX_OFF              0x18
#define WQ_ENABLE_OFF                0x20
#define WQ_RUNNING_OFF               0x28
#define WQ_FETCH_INDEX_OFF           0x30
#define WQ_DCA_VALUE_OFF             0x38
#define WQ_ERROR_INTERRUPT_ENABLE_OFF 0x40
#define WQ_ERROR_INTERRUPT_OFFSET_OFF 0x48
#define WQ_ERROR_STATUS_OFF          0x50

/* CQ register offsets - based on struct vnic_cq_ctrl */
#define CQ_RING_BASE_LO_OFF          0x00
#define CQ_RING_BASE_HI_OFF          0x04
#define CQ_RING_SIZE_OFF             0x08
#define CQ_FLOW_CONTROL_ENABLE_OFF   0x10
#define CQ_COLOR_ENABLE_OFF          0x18
#define CQ_HEAD_OFF                  0x20
#define CQ_TAIL_OFF                  0x28
#define CQ_TAIL_COLOR_OFF            0x30
#define CQ_INTERRUPT_ENABLE_OFF      0x38
#define CQ_ENTRY_ENABLE_OFF          0x40
#define CQ_MESSAGE_ENABLE_OFF        0x48
#define CQ_INTERRUPT_OFFSET_OFF      0x50
#define CQ_MESSAGE_ADDR_LO_OFF       0x58
#define CQ_MESSAGE_ADDR_HI_OFF       0x5C

/* VNIC magic */
#define VNIC_MAGIC 0x766E6963

/* Command definitions */
#define CMD_VERSION               'V'
#define CMD_INITIALIZE            1   /* needed: define:CMD_INITIALIZE */
#define CMD_INITIALIZE_DEVCMD2    2   /* needed: define:CMD_INITIALIZE_DEVCMD2 */
#define CMD_DEV_SPEC              3   /* needed: define:CMD_DEV_SPEC */
#define VNIC_DEVCMD_NARGS         15

/* Command descriptor structure */
typedef struct VnicDevCmdDesc {
    uint32_t status;
    uint32_t cmd;
    uint64_t args[VNIC_DEVCMD_NARGS];
} VnicDevCmdDesc;

/* Interrupt controller */
typedef struct {
    uint32_t coalescing_timer;
    uint32_t coalescing_value;
    uint32_t coalescing_type;
    uint32_t mask_on_assertion;
    uint32_t mask;
    uint32_t credits;
    uint32_t credit_return;
} SnICIntrCtrl;

/* WQ controller */
typedef struct {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t posted_index;
    uint32_t cq_index;
    uint32_t enable;
    uint32_t running;
    uint32_t fetch_index;
    uint32_t dca_value;
    uint32_t error_interrupt_enable;
    uint32_t error_interrupt_offset;
    uint32_t error_status;
} SnICWqCtrl;

/* CQ controller */
typedef struct {
    uint64_t ring_base;
    uint32_t ring_size;
    uint32_t flow_control_enable;
    uint32_t color_enable;
    uint32_t cq_head;
    uint32_t cq_tail;
    uint32_t cq_tail_color;
    uint32_t interrupt_enable;
    uint32_t cq_entry_enable;
    uint32_t cq_message_enable;
    uint32_t interrupt_offset;
    uint64_t cq_message_addr;
    uint32_t enable;  /* added missing field */
} SnICCqCtrl;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    MemoryRegion msix_mmio;
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Interrupt controllers */
    SnICIntrCtrl intr[SNIC_MAX_INTR];
    /* Work queue controllers */
    SnICWqCtrl wq[SNIC_MAX_WQ];
    /* Completion queue controllers */
    SnICCqCtrl cq[SNIC_MAX_CQ];
    /* Devcmd2 WQ */
    SnICWqCtrl devcmd2_wq;

    /* Global device state */
    uint32_t dev_enable;
    int state;
    bool reset_in_progress;
};

/* Helper: update IRQ line based on pending unmasked interrupts */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;
    int pending_vec = -1;

    for (i = 0; i < SNIC_MAX_INTR; i++) {
        if (!s->intr[i].mask && s->intr[i].credits > 0) {
            pending_vec = i;
            break;
        }
    }

    if (msix_enabled(pdev)) {
        if (pending_vec >= 0) {
            msix_notify(pdev, pending_vec);
        }
    } else if (msi_enabled(pdev)) {
        if (pending_vec >= 0) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, pending_vec >= 0 ? 1 : 0);
    }
}

/* Raise an interrupt on a specific INTR vector */
static void pcibase_post_intr(PCIBaseState *s, int vector)
{
    if (vector < 0 || vector >= SNIC_MAX_INTR) {
        return;
    }
    if (!s->intr[vector].mask) {
        s->intr[vector].credits++;
        pcibase_update_irq(s);
    }
}

/* Emulate writing to interrupt credit return register */
static void snic_intr_credit_return(PCIBaseState *s, int intr_idx, uint32_t val)
{
    SnICIntrCtrl *intrct = &s->intr[intr_idx];
    uint32_t credits = val & 0xffff;
    bool unmask = (val >> 16) & 1;

    if (credits > intrct->credits) {
        credits = intrct->credits;
    }
    intrct->credits -= credits;

    if (unmask) {
        intrct->mask = 0;
    }
    /* reset_timer is ignored */

    pcibase_update_irq(s);
}

/* Handle a write to an interrupt control register */
static void snic_intr_write(PCIBaseState *s, int intr_idx, hwaddr reg_off, uint64_t val, unsigned size)
{
    SnICIntrCtrl *intrct = &s->intr[intr_idx];

    if (size != 4) return;

    switch (reg_off) {
    case INTR_COALESCING_TIMER_OFF:
        intrct->coalescing_timer = val;
        break;
    case INTR_COALESCING_VALUE_OFF:
        intrct->coalescing_value = val;
        break;
    case INTR_COALESCING_TYPE_OFF:
        intrct->coalescing_type = val;
        break;
    case INTR_MASK_ON_ASSERTION_OFF:
        intrct->mask_on_assertion = val;
        break;
    case INTR_MASK_OFF:
        intrct->mask = val & 1;
        pcibase_update_irq(s);
        break;
    case INTR_CREDIT_RETURN_OFF:
        snic_intr_credit_return(s, intr_idx, val);
        break;
    default:
        break;
    }
}

/* Handle a read from an interrupt control register */
static uint64_t snic_intr_read(PCIBaseState *s, int intr_idx, hwaddr reg_off, unsigned size)
{
    SnICIntrCtrl *intrct = &s->intr[intr_idx];

    if (size != 4) return 0;

    switch (reg_off) {
    case INTR_COALESCING_TIMER_OFF: return intrct->coalescing_timer;
    case INTR_COALESCING_VALUE_OFF: return intrct->coalescing_value;
    case INTR_COALESCING_TYPE_OFF:  return intrct->coalescing_type;
    case INTR_MASK_ON_ASSERTION_OFF: return intrct->mask_on_assertion;
    case INTR_MASK_OFF:             return intrct->mask;
    case INTR_CREDITS_OFF:          return intrct->credits;
    default: return 0;
    }
}

/* Process a WQ descriptor ring for new commands */
static void snic_process_wq(PCIBaseState *s, int wq_idx)
{
    SnICWqCtrl *wq_ctrl;
    if (wq_idx == -1) {
        wq_ctrl = &s->devcmd2_wq;
    } else {
        wq_ctrl = &s->wq[wq_idx];
    }
    
    if (!wq_ctrl->enable) {
        return;
    }
    
    uint32_t posted = wq_ctrl->posted_index;
    uint32_t fetch   = wq_ctrl->fetch_index;
    uint32_t ring_size = wq_ctrl->ring_size;
    if (ring_size == 0) return;
    
    /* No new work */
    if (fetch == posted) return;
    
    /* Descriptor size */
    uint32_t desc_size = sizeof(VnicDevCmdDesc);
    
    /* Process descriptors */
    while (fetch != posted) {
        dma_addr_t desc_addr = wq_ctrl->ring_base + (fetch % ring_size) * desc_size;
        VnicDevCmdDesc desc;
        
        /* DMA read descriptor */
        pci_dma_read(PCI_DEVICE(s), desc_addr, &desc, sizeof(desc));
        
        /* Execute command */
        uint32_t cmd = le32_to_cpu(desc.cmd);
        uint64_t a0 = le64_to_cpu(desc.args[0]);
        uint64_t a1 = le64_to_cpu(desc.args[1]);
        uint32_t status = 0xFFFFFFFF; /* error */
        bool success = false;
        
        switch (cmd) {
        case CMD_VERSION:
            /* Return hardware version, e.g., 0x01000000 */
            desc.args[0] = cpu_to_le64(0x01000000);
            status = 0;
            success = true;
            break;
        case CMD_INITIALIZE:
            /* Nothing special, success */
            status = 0;
            success = true;
            break;
        case CMD_INITIALIZE_DEVCMD2:
            /* Store results ring base and size in args, then set args to 0 */
            {
                status = 0;
                success = true;
                desc.args[0] = 0;
                desc.args[1] = 0;
            }
            break;
        case CMD_DEV_SPEC:
            {
                /* a0 = offset, a1 = size; we need to return a value in a0 */
                uint32_t offset = a0;
                uint64_t value = 0;
                /* Simple table for known spec offsets */
                switch (offset) {
                case 0: /* example: something like capabilities */
                    value = 0x00000001; /* just a nonzero for success */
                    break;
                default:
                    value = 0;
                    break;
                }
                status = 0;
                success = true;
                /* Place result in a0 */
                desc.args[0] = cpu_to_le64(value);
            }
            break;
        default:
            /* unknown command, set error */
            status = 0xFFFFFFFF;
            break;
        }
        
        desc.status = cpu_to_le32(status);
        
        /* Write descriptor back */
        pci_dma_write(PCI_DEVICE(s), desc_addr, &desc, sizeof(desc));
        
        if (success) {
            /* Optional: post completion to CQ if cq_index is valid */
            int cq_idx = wq_ctrl->cq_index;
            if (cq_idx >= 0 && cq_idx < SNIC_MAX_CQ) {
                SnICCqCtrl *cq_ctrl = &s->cq[cq_idx];
                if (cq_ctrl->enable && cq_ctrl->cq_entry_enable) {
                    /* Post a simple completion entry (16 bytes) */
                    uint32_t cq_tail = cq_ctrl->cq_tail % cq_ctrl->ring_size;
                    dma_addr_t cq_entry_addr = cq_ctrl->ring_base + cq_tail * 16;
                    uint8_t cq_entry[16] = {0};
                    /* For simplicity, we write a success status in first 4 bytes */
                    *(uint32_t *)cq_entry = cpu_to_le32(0);
                    pci_dma_write(PCI_DEVICE(s), cq_entry_addr, cq_entry, 16);
                    cq_ctrl->cq_tail = (cq_ctrl->cq_tail + 1) % cq_ctrl->ring_size;
                    /* Update tail color if needed, not implemented */
                    /* Raise interrupt if enabled */
                    if (cq_ctrl->interrupt_enable) {
                        pcibase_post_intr(s, cq_ctrl->interrupt_offset);
                    }
                }
            }
        }
        
        fetch = (fetch + 1) % ring_size;
    }
    wq_ctrl->fetch_index = fetch;
}

/* Handle write to a WQ control register */
static void snic_wq_write(PCIBaseState *s, int wq_idx, hwaddr reg_off, uint64_t val, unsigned size)
{
    SnICWqCtrl *wq_ctrl = (wq_idx == -1) ? &s->devcmd2_wq : &s->wq[wq_idx];
    bool trigger_process = false;

    switch (reg_off) {
    case WQ_RING_BASE_LO_OFF:
        if (size == 4) {
            wq_ctrl->ring_base = (wq_ctrl->ring_base & 0xFFFFFFFF00000000ULL) | val;
        } else if (size == 8) {
            wq_ctrl->ring_base = val;
        }
        break;
    case WQ_RING_BASE_HI_OFF:
        if (size == 4) {
            wq_ctrl->ring_base = (wq_ctrl->ring_base & 0xFFFFFFFF) | ((uint64_t)val << 32);
        }
        break;
    case WQ_RING_SIZE_OFF:
        if (size == 4) wq_ctrl->ring_size = val;
        break;
    case WQ_POSTED_INDEX_OFF:
        if (size == 4) {
            wq_ctrl->posted_index = val;
            trigger_process = true;
        }
        break;
    case WQ_CQ_INDEX_OFF:
        if (size == 4) wq_ctrl->cq_index = val;
        break;
    case WQ_ENABLE_OFF:
        if (size == 4) {
            wq_ctrl->enable = val & 1;
            if (wq_ctrl->enable) {
                wq_ctrl->running = 1;
            } else {
                wq_ctrl->running = 0;
            }
        }
        break;
    case WQ_RUNNING_OFF:
        /* Read-only, ignore */
        break;
    case WQ_FETCH_INDEX_OFF:
        /* Read-only, ignore */
        break;
    case WQ_DCA_VALUE_OFF:
        if (size == 4) wq_ctrl->dca_value = val;
        break;
    case WQ_ERROR_INTERRUPT_ENABLE_OFF:
        if (size == 4) wq_ctrl->error_interrupt_enable = val;
        break;
    case WQ_ERROR_INTERRUPT_OFFSET_OFF:
        if (size == 4) wq_ctrl->error_interrupt_offset = val;
        break;
    case WQ_ERROR_STATUS_OFF:
        /* Read-only, ignore */
        break;
    default:
        break;
    }

    if (trigger_process) {
        snic_process_wq(s, wq_idx);
    }
}

/* Handle read from a WQ control register */
static uint64_t snic_wq_read(PCIBaseState *s, int wq_idx, hwaddr reg_off, unsigned size)
{
    SnICWqCtrl *wq_ctrl = (wq_idx == -1) ? &s->devcmd2_wq : &s->wq[wq_idx];

    switch (reg_off) {
    case WQ_RING_BASE_LO_OFF:
        if (size == 4) return wq_ctrl->ring_base & 0xFFFFFFFF;
        else if (size == 8) return wq_ctrl->ring_base;
        else return 0;
    case WQ_RING_BASE_HI_OFF:
        if (size == 4) return (wq_ctrl->ring_base >> 32) & 0xFFFFFFFF;
        return 0;
    case WQ_RING_SIZE_OFF:
        if (size == 4) return wq_ctrl->ring_size;
        return 0;
    case WQ_POSTED_INDEX_OFF:
        if (size == 4) return wq_ctrl->posted_index;
        return 0;
    case WQ_CQ_INDEX_OFF:
        if (size == 4) return wq_ctrl->cq_index;
        return 0;
    case WQ_ENABLE_OFF:
        if (size == 4) return wq_ctrl->enable;
        return 0;
    case WQ_RUNNING_OFF:
        if (size == 4) return wq_ctrl->running;
        return 0;
    case WQ_FETCH_INDEX_OFF:
        if (size == 4) return wq_ctrl->fetch_index;
        return 0;
    case WQ_DCA_VALUE_OFF:
        if (size == 4) return wq_ctrl->dca_value;
        return 0;
    case WQ_ERROR_INTERRUPT_ENABLE_OFF:
        if (size == 4) return wq_ctrl->error_interrupt_enable;
        return 0;
    case WQ_ERROR_INTERRUPT_OFFSET_OFF:
        if (size == 4) return wq_ctrl->error_interrupt_offset;
        return 0;
    case WQ_ERROR_STATUS_OFF:
        if (size == 4) return wq_ctrl->error_status;
        return 0;
    default:
        return 0;
    }
}

/* Handle write to a CQ control register */
static void snic_cq_write(PCIBaseState *s, int cq_idx, hwaddr reg_off, uint64_t val, unsigned size)
{
    SnICCqCtrl *cq_ctrl = &s->cq[cq_idx];

    switch (reg_off) {
    case CQ_RING_BASE_LO_OFF:
        if (size == 4) {
            cq_ctrl->ring_base = (cq_ctrl->ring_base & 0xFFFFFFFF00000000ULL) | val;
        } else if (size == 8) {
            cq_ctrl->ring_base = val;
        }
        break;
    case CQ_RING_BASE_HI_OFF:
        if (size == 4) {
            cq_ctrl->ring_base = (cq_ctrl->ring_base & 0xFFFFFFFF) | ((uint64_t)val << 32);
        }
        break;
    case CQ_RING_SIZE_OFF:
        if (size == 4) cq_ctrl->ring_size = val;
        break;
    case CQ_FLOW_CONTROL_ENABLE_OFF:
        if (size == 4) cq_ctrl->flow_control_enable = val;
        break;
    case CQ_COLOR_ENABLE_OFF:
        if (size == 4) cq_ctrl->color_enable = val;
        break;
    case CQ_HEAD_OFF:
        /* Read-only, ignore */
        break;
    case CQ_TAIL_OFF:
        /* Read-only, but driver may write to reset? Usually read-only */
        break;
    case CQ_TAIL_COLOR_OFF:
        /* Read-only */
        break;
    case CQ_INTERRUPT_ENABLE_OFF:
        if (size == 4) cq_ctrl->interrupt_enable = val;
        break;
    case CQ_ENTRY_ENABLE_OFF:
        if (size == 4) cq_ctrl->cq_entry_enable = val;
        break;
    case CQ_MESSAGE_ENABLE_OFF:
        if (size == 4) cq_ctrl->cq_message_enable = val;
        break;
    case CQ_INTERRUPT_OFFSET_OFF:
        if (size == 4) cq_ctrl->interrupt_offset = val;
        break;
    case CQ_MESSAGE_ADDR_LO_OFF:
        if (size == 4) {
            cq_ctrl->cq_message_addr = (cq_ctrl->cq_message_addr & 0xFFFFFFFF00000000ULL) | val;
        } else if (size == 8) {
            cq_ctrl->cq_message_addr = val;
        }
        break;
    case CQ_MESSAGE_ADDR_HI_OFF:
        if (size == 4) {
            cq_ctrl->cq_message_addr = (cq_ctrl->cq_message_addr & 0xFFFFFFFF) | ((uint64_t)val << 32);
        }
        break;
    default:
        break;
    }
}

/* Handle read from a CQ control register */
static uint64_t snic_cq_read(PCIBaseState *s, int cq_idx, hwaddr reg_off, unsigned size)
{
    SnICCqCtrl *cq_ctrl = &s->cq[cq_idx];

    switch (reg_off) {
    case CQ_RING_BASE_LO_OFF:
        if (size == 4) return cq_ctrl->ring_base & 0xFFFFFFFF;
        else if (size == 8) return cq_ctrl->ring_base;
        else return 0;
    case CQ_RING_BASE_HI_OFF:
        if (size == 4) return (cq_ctrl->ring_base >> 32) & 0xFFFFFFFF;
        return 0;
    case CQ_RING_SIZE_OFF:
        if (size == 4) return cq_ctrl->ring_size;
        return 0;
    case CQ_FLOW_CONTROL_ENABLE_OFF:
        if (size == 4) return cq_ctrl->flow_control_enable;
        return 0;
    case CQ_COLOR_ENABLE_OFF:
        if (size == 4) return cq_ctrl->color_enable;
        return 0;
    case CQ_HEAD_OFF:
        if (size == 4) return cq_ctrl->cq_head;
        return 0;
    case CQ_TAIL_OFF:
        if (size == 4) return cq_ctrl->cq_tail;
        return 0;
    case CQ_TAIL_COLOR_OFF:
        if (size == 4) return cq_ctrl->cq_tail_color;
        return 0;
    case CQ_INTERRUPT_ENABLE_OFF:
        if (size == 4) return cq_ctrl->interrupt_enable;
        return 0;
    case CQ_ENTRY_ENABLE_OFF:
        if (size == 4) return cq_ctrl->cq_entry_enable;
        return 0;
    case CQ_MESSAGE_ENABLE_OFF:
        if (size == 4) return cq_ctrl->cq_message_enable;
        return 0;
    case CQ_INTERRUPT_OFFSET_OFF:
        if (size == 4) return cq_ctrl->interrupt_offset;
        return 0;
    case CQ_MESSAGE_ADDR_LO_OFF:
        if (size == 4) return cq_ctrl->cq_message_addr & 0xFFFFFFFF;
        else if (size == 8) return cq_ctrl->cq_message_addr;
        return 0;
    case CQ_MESSAGE_ADDR_HI_OFF:
        if (size == 4) return (cq_ctrl->cq_message_addr >> 32) & 0xFFFFFFFF;
        return 0;
    default:
        return 0;
    }
}

/* Build resource table for VNIC discovery */
static void snic_build_resource_table(uint8_t *buffer)
{
    /* header: magic, version (no count) */
    uint32_t magic = cpu_to_le32(VNIC_MAGIC);
    uint32_t version = cpu_to_le32(0);
    memcpy(buffer, &magic, 4);
    memcpy(buffer + 4, &version, 4);

    /* Resource entries: type(1), bar(1), pad(2), bar_offset(4), count(4) */
    int pos = 8;
    int i;
    for (i = 0; i < SNIC_MAX_INTR; i++) {
        buffer[pos] = SNIC_RES_TYPE_INTR;
        buffer[pos+1] = 0;
        buffer[pos+2] = 0;
        buffer[pos+3] = 0;
        uint32_t offset = cpu_to_le32(SNIC_INTR_OFFSET(i));
        memcpy(buffer + pos + 4, &offset, 4);
        uint32_t cnt = cpu_to_le32(1);
        memcpy(buffer + pos + 8, &cnt, 4);
        pos += 12;
    }
    for (i = 0; i < SNIC_MAX_WQ; i++) {
        buffer[pos] = SNIC_RES_TYPE_WQ;
        buffer[pos+1] = 0;
        buffer[pos+2] = 0;
        buffer[pos+3] = 0;
        uint32_t offset = cpu_to_le32(SNIC_WQ_OFFSET(i));
        memcpy(buffer + pos + 4, &offset, 4);
        uint32_t cnt = cpu_to_le32(1);
        memcpy(buffer + pos + 8, &cnt, 4);
        pos += 12;
    }
    for (i = 0; i < SNIC_MAX_CQ; i++) {
        buffer[pos] = SNIC_RES_TYPE_CQ;
        buffer[pos+1] = 0;
        buffer[pos+2] = 0;
        buffer[pos+3] = 0;
        uint32_t offset = cpu_to_le32(SNIC_CQ_OFFSET(i));
        memcpy(buffer + pos + 4, &offset, 4);
        uint32_t cnt = cpu_to_le32(1);
        memcpy(buffer + pos + 8, &cnt, 4);
        pos += 12;
    }
    /* DEVCMD2 resource */
    {
        buffer[pos] = SNIC_RES_TYPE_DEVCMD2;
        buffer[pos+1] = 0;
        buffer[pos+2] = 0;
        buffer[pos+3] = 0;
        uint32_t offset = cpu_to_le32(SNIC_DEVCMD2_OFFSET);
        memcpy(buffer + pos + 4, &offset, 4);
        uint32_t cnt = cpu_to_le32(1);
        memcpy(buffer + pos + 8, &cnt, 4);
        pos += 12;
    }
    /* End-of-list marker */
    buffer[pos] = RES_TYPE_EOL;
    /* rest of the entry can be zero */
    memset(buffer + pos + 1, 0, 11);
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Header area (discovery) */
    if (addr < SNIC_HEADER_SIZE) {
        static uint8_t table[SNIC_HEADER_SIZE];
        static bool init = false;
        if (!init) {
            snic_build_resource_table(table);
            init = true;
        }
        if (addr + size <= SNIC_HEADER_SIZE) {
            switch (size) {
            case 4:
                val = ldl_le_p(table + addr);
                break;
            default:
                break;
            }
        }
        return val;
    }

    /* Interrupt control pages */
    for (int i = 0; i < SNIC_MAX_INTR; i++) {
        if (addr >= SNIC_INTR_OFFSET(i) && addr < SNIC_INTR_OFFSET(i) + 0x100) {
            return snic_intr_read(s, i, addr - SNIC_INTR_OFFSET(i), size);
        }
    }

    /* WQ control pages */
    for (int i = 0; i < SNIC_MAX_WQ; i++) {
        if (addr >= SNIC_WQ_OFFSET(i) && addr < SNIC_WQ_OFFSET(i) + 0x100) {
            return snic_wq_read(s, i, addr - SNIC_WQ_OFFSET(i), size);
        }
    }

    /* CQ control pages */
    for (int i = 0; i < SNIC_MAX_CQ; i++) {
        if (addr >= SNIC_CQ_OFFSET(i) && addr < SNIC_CQ_OFFSET(i) + 0x100) {
            return snic_cq_read(s, i, addr - SNIC_CQ_OFFSET(i), size);
        }
    }

    /* Devcmd2 WQ page */
    if (addr >= SNIC_DEVCMD2_OFFSET && addr < SNIC_DEVCMD2_OFFSET + 0x100) {
        return snic_wq_read(s, -1, addr - SNIC_DEVCMD2_OFFSET, size);
    }

    return 0;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Header area: ignore writes during discovery */
    if (addr < SNIC_HEADER_SIZE) {
        return;
    }

    /* Interrupt control pages */
    for (int i = 0; i < SNIC_MAX_INTR; i++) {
        if (addr >= SNIC_INTR_OFFSET(i) && addr < SNIC_INTR_OFFSET(i) + 0x100) {
            snic_intr_write(s, i, addr - SNIC_INTR_OFFSET(i), val, size);
            return;
        }
    }

    /* WQ control pages */
    for (int i = 0; i < SNIC_MAX_WQ; i++) {
        if (addr >= SNIC_WQ_OFFSET(i) && addr < SNIC_WQ_OFFSET(i) + 0x100) {
            snic_wq_write(s, i, addr - SNIC_WQ_OFFSET(i), val, size);
            return;
        }
    }

    /* CQ control pages */
    for (int i = 0; i < SNIC_MAX_CQ; i++) {
        if (addr >= SNIC_CQ_OFFSET(i) && addr < SNIC_CQ_OFFSET(i) + 0x100) {
            snic_cq_write(s, i, addr - SNIC_CQ_OFFSET(i), val, size);
            return;
        }
    }

    /* Devcmd2 WQ page */
    if (addr >= SNIC_DEVCMD2_OFFSET && addr < SNIC_DEVCMD2_OFFSET + 0x100) {
        snic_wq_write(s, -1, addr - SNIC_DEVCMD2_OFFSET, val, size);
        return;
    }
}

/* PIO handlers (not used) */
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

    s->dev_enable = 0;
    memset(s->intr, 0, sizeof(s->intr));
    memset(s->wq, 0, sizeof(s->wq));
    memset(s->cq, 0, sizeof(s->cq));
    memset(&s->devcmd2_wq, 0, sizeof(s->devcmd2_wq));

    for (int i = 0; i < SNIC_MAX_INTR; i++) {
        s->intr[i].mask = 1;
    }
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, int index, hwaddr size, const char *name, bool is_io, Error **errp)
{
    hwaddr aligned_size = pow2ceil(size);
    MemoryRegion *mr = &s->bar_regions[index];

    if (is_io) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, name, aligned_size);
        pci_register_bar(pdev, index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, name, aligned_size);
        pci_register_bar(pdev, index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_SNIC);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_SNIC);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SNIC);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    pcibase_register_bar(pdev, s, 0, SNIC_BAR0_SIZE, "snic-bar0", false, errp);

    /* Initialize MSI-X with exclusive BAR (BAR1) */
    msix_init_exclusive_bar(pdev, SNIC_MAX_INTR, 1, errp);

    s->num_bars = 2;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit_exclusive_bar(pdev);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_snic_intr = {
    .name = "snic_intr",
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(coalescing_timer, SnICIntrCtrl),
        VMSTATE_UINT32(coalescing_value, SnICIntrCtrl),
        VMSTATE_UINT32(coalescing_type, SnICIntrCtrl),
        VMSTATE_UINT32(mask_on_assertion, SnICIntrCtrl),
        VMSTATE_UINT32(mask, SnICIntrCtrl),
        VMSTATE_UINT32(credits, SnICIntrCtrl),
        VMSTATE_UINT32(credit_return, SnICIntrCtrl),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_snic_wq = {
    .name = "snic_wq",
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(ring_base, SnICWqCtrl),
        VMSTATE_UINT32(ring_size, SnICWqCtrl),
        VMSTATE_UINT32(posted_index, SnICWqCtrl),
        VMSTATE_UINT32(cq_index, SnICWqCtrl),
        VMSTATE_UINT32(enable, SnICWqCtrl),
        VMSTATE_UINT32(running, SnICWqCtrl),
        VMSTATE_UINT32(fetch_index, SnICWqCtrl),
        VMSTATE_UINT32(dca_value, SnICWqCtrl),
        VMSTATE_UINT32(error_interrupt_enable, SnICWqCtrl),
        VMSTATE_UINT32(error_interrupt_offset, SnICWqCtrl),
        VMSTATE_UINT32(error_status, SnICWqCtrl),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_snic_cq = {
    .name = "snic_cq",
    .fields = (VMStateField[]) {
        VMSTATE_UINT64(ring_base, SnICCqCtrl),
        VMSTATE_UINT32(ring_size, SnICCqCtrl),
        VMSTATE_UINT32(flow_control_enable, SnICCqCtrl),
        VMSTATE_UINT32(color_enable, SnICCqCtrl),
        VMSTATE_UINT32(cq_head, SnICCqCtrl),
        VMSTATE_UINT32(cq_tail, SnICCqCtrl),
        VMSTATE_UINT32(cq_tail_color, SnICCqCtrl),
        VMSTATE_UINT32(interrupt_enable, SnICCqCtrl),
        VMSTATE_UINT32(cq_entry_enable, SnICCqCtrl),
        VMSTATE_UINT32(cq_message_enable, SnICCqCtrl),
        VMSTATE_UINT32(interrupt_offset, SnICCqCtrl),
        VMSTATE_UINT64(cq_message_addr, SnICCqCtrl),
        VMSTATE_UINT32(enable, SnICCqCtrl),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pcibase = {
    .name = "snic_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_STRUCT_ARRAY(intr, PCIBaseState, SNIC_MAX_INTR, 0, vmstate_snic_intr, SnICIntrCtrl),
        VMSTATE_STRUCT_ARRAY(wq, PCIBaseState, SNIC_MAX_WQ, 0, vmstate_snic_wq, SnICWqCtrl),
        VMSTATE_STRUCT_ARRAY(cq, PCIBaseState, SNIC_MAX_CQ, 0, vmstate_snic_cq, SnICCqCtrl),
        VMSTATE_STRUCT(devcmd2_wq, PCIBaseState, 0, vmstate_snic_wq, SnICWqCtrl),
        VMSTATE_UINT32(dev_enable, PCIBaseState),
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
