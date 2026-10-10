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

/* Additional include files retrieved from driver context */
#include "hw/pci/pci_device.h"

#define TYPE_PCIBASE_DEVICE "ioatdma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#ifndef PCI_DEVICE_ID_INTEL_IOAT_TBG0
#define PCI_DEVICE_ID_INTEL_IOAT_TBG0 0x3430
#endif

#define IOAT_MMIO_BAR 0

#define IOAT_CHANCNT_OFFSET 0x00
#define IOAT_XFERCAP_OFFSET 0x01
#define IOAT_INTRCTRL_OFFSET 0x03
#define IOAT_VER_OFFSET 0x08
#define IOAT_DMA_CAP_OFFSET 0x10
#define IOAT_DCACTRL_OFFSET 0x30
#define IOAT_PREFETCH_LIMIT_OFFSET 0x4C
#define IOAT_PCI_DMAUNCERRSTS_OFFSET 0x148
#define IOAT_PCI_CHANERR_INT_OFFSET 0x180

#define IOAT_CHANCTRL_OFFSET 0x00
#define IOAT2_CHANCMD_OFFSET 0x04
#define IOAT1_CHANCMD_OFFSET 0x14
#define IOAT_CHAN_DMACOUNT_OFFSET 0x06
#define IOAT_CHANSTS_OFFSET 0x08
#define IOAT2_CHAINADDR_OFFSET_LOW 0x10
#define IOAT2_CHAINADDR_OFFSET_HIGH 0x14
#define IOAT_CHANCMP_OFFSET_LOW 0x18
#define IOAT_CHANCMP_OFFSET_HIGH 0x1C
#define IOAT_CHANERR_OFFSET 0x28
#define IOAT_CHANERR_MASK_OFFSET 0x2C
#define IOAT_CHAN_DRSCTL_OFFSET 0xB6
#define IOAT_CHAN_LTR_SWSEL_OFFSET 0xBC
#define IOAT_CHAN_LTR_ACTIVE_OFFSET 0xC0
#define IOAT_CHAN_LTR_IDLE_OFFSET 0xC4

#define IOAT_MAX_CHANS 4

#define IOAT_VER_2_0            0x20
#define IOAT_VER_3_0            0x30
#define IOAT_VER_3_2            0x32
#define IOAT_VER_3_4            0x34

#define IOAT_INTRCTRL_MASTER_INT_EN         0x01
#define IOAT_INTRCTRL_INT_STATUS            0x02
#define IOAT_INTRCTRL_INT                   0x04
#define IOAT_INTRCTRL_MSIX_VECTOR_CONTROL   0x08

#define IOAT_CHANCMD_RESET          0x20
#define IOAT_CHANCMD_RESUME         0x10
#define IOAT_CHANCMD_ABORT          0x08
#define IOAT_CHANCMD_SUSPEND        0x04
#define IOAT_CHANCMD_APPEND         0x02
#define IOAT_CHANCMD_START          0x01

#define IOAT_CHANSTS_STATUS         0x7ULL
#define IOAT_CHANSTS_DONE           0x1
#define IOAT_CHANSTS_ACTIVE         0x0
#define IOAT_CHANSTS_SUSPENDED      0x2
#define IOAT_CHANSTS_HALTED         0x3
#define IOAT_CHANSTS_COMPLETED_DESCRIPTOR_ADDR  (~0x3fULL)

#define IOAT_CHAN_LTR_ACTIVE_SNVAL      0x0000
#define IOAT_CHAN_LTR_ACTIVE_SNLATSCALE 0x0800
#define IOAT_CHAN_LTR_ACTIVE_SNREQMNT   0x8000

#define IOAT_CHAN_LTR_IDLE_SNVAL        0x0258
#define IOAT_CHAN_LTR_IDLE_SNLATSCALE   0x0800
#define IOAT_CHAN_LTR_IDLE_SNREQMNT     0x8000

#define IOAT_CHAN_LTR_SWSEL_ACTIVE      0x0
#define IOAT_CHAN_LTR_SWSEL_IDLE        0x1

#define IOAT_DMA_DCA_ANY_CPU            ~0

#define IOAT_CAP_DWBES                  0x00002000
#define IOAT_CAP_XOR                    0x00000100
#define IOAT_CAP_PQ                     0x00000200
#define IOAT_CAP_RAID16SS               0x00020000
#define IOAT_CAP_DPS                    0x00800000

#define IOAT_CHANERR_XOR_P_OR_CRC_ERR   0x10000
#define IOAT_CHANERR_XOR_Q_ERR          0x20000
#define IOAT_CHANERR_SRC_ADDR_ERR       0x0001
#define IOAT_CHANERR_DEST_ADDR_ERR      0x0002
#define IOAT_CHANERR_NEXT_ADDR_ERR      0x0004
#define IOAT_CHANERR_CHAIN_ADDR_VALUE_ERR 0x0010
#define IOAT_CHANERR_CHANCMD_ERR        0x0020
#define IOAT_CHANERR_READ_DATA_ERR      0x0100
#define IOAT_CHANERR_WRITE_DATA_ERR     0x0200
#define IOAT_CHANERR_CONTROL_ERR        0x0400
#define IOAT_CHANERR_LENGTH_ERR         0x0800
#define IOAT_CHANERR_COMPLETION_ADDR_ERR 0x1000

#define IOAT_OP_COPY                    0x00
#define IOAT_OP_XOR                     0x87
#define IOAT_OP_XOR_VAL                 0x88
#define IOAT_OP_PQ                      0x89
#define IOAT_OP_PQ_VAL                  0x8a
#define IOAT_OP_PQ_16S                  0xa0
#define IOAT_OP_PQ_VAL_16S              0xa1
#define IOAT_OP_PQ_UP                   0x8b

#define IOAT_CHANCTRL_INT_REARM         0x0001
#define IOAT_CHANCTRL_ERR_INT_EN        0x0010
#define IOAT_CHANCTRL_ERR_COMPLETION_EN 0x0004
#define IOAT_CHANCTRL_ANY_ERR_ABORT_EN  0x0008

#define IOAT_DESC_CTL_INT_EN             (1U << 0)
#define IOAT_DESC_CTL_COMPL_WRITE        (1U << 3)
#define IOAT_DESC_CTL_NULL               (1U << 5)
#define IOAT_DESC_CTL_OP_SHIFT           24
#define IOAT_DESC_CTL_OP_MASK            0xff

/* Bound descriptor walks even if a malicious guest supplies a huge count. */
#define IOAT_MAX_DESCS_PER_KICK           4096

struct ioat_dma_descriptor {
    uint32_t    size;
    union {
        uint32_t ctl;
        struct {
            unsigned int int_en:1;
            unsigned int src_snoop_dis:1;
            unsigned int dest_snoop_dis:1;
            unsigned int compl_write:1;
            unsigned int fence:1;
            unsigned int null:1;
            unsigned int src_brk:1;
            unsigned int dest_brk:1;
            unsigned int bundle:1;
            unsigned int dest_dca:1;
            unsigned int hint:1;
            unsigned int rsvd2:13;
            unsigned int op:8;
        } ctl_f;
    };
    uint64_t    src_addr;
    uint64_t    dst_addr;
    uint64_t    next;
    uint64_t    rsv1;
    uint64_t    rsv2;
    union {
        uint64_t    user1;
        uint64_t    tx_cnt;
    };
    uint64_t    user2;
};

struct ioat_xor_descriptor {
    uint32_t    size;
    union {
        uint32_t ctl;
        struct {
            unsigned int int_en:1;
            unsigned int src_snoop_dis:1;
            unsigned int dest_snoop_dis:1;
            unsigned int compl_write:1;
            unsigned int fence:1;
            unsigned int src_cnt:3;
            unsigned int bundle:1;
            unsigned int dest_dca:1;
            unsigned int hint:1;
            unsigned int rsvd:13;
            unsigned int op:8;
        } ctl_f;
    };
    uint64_t    src_addr;
    uint64_t    dst_addr;
    uint64_t    next;
    uint64_t    src_addr2;
    uint64_t    src_addr3;
    uint64_t    src_addr4;
    uint64_t    src_addr5;
};

struct ioat_xor_ext_descriptor {
    uint64_t    src_addr6;
    uint64_t    src_addr7;
    uint64_t    src_addr8;
    uint64_t    next;
    uint64_t    rsvd[4];
};

struct ioat_pq_descriptor {
    union {
        uint32_t    size;
        uint32_t    dwbes;
        struct {
            unsigned int rsvd:25;
            unsigned int p_val_err:1;
            unsigned int q_val_err:1;
            unsigned int rsvd1:4;
            unsigned int wbes:1;
        } dwbes_f;
    };
    union {
        uint32_t ctl;
        struct {
            unsigned int int_en:1;
            unsigned int src_snoop_dis:1;
            unsigned int dest_snoop_dis:1;
            unsigned int compl_write:1;
            unsigned int fence:1;
            unsigned int src_cnt:3;
            unsigned int bundle:1;
            unsigned int dest_dca:1;
            unsigned int hint:1;
            unsigned int p_disable:1;
            unsigned int q_disable:1;
            unsigned int rsvd2:2;
            unsigned int wb_en:1;
            unsigned int prl_en:1;
            unsigned int rsvd3:7;
            unsigned int op:8;
        } ctl_f;
    };
    uint64_t    src_addr;
    uint64_t    p_addr;
    uint64_t    next;
    uint64_t    src_addr2;
    union {
        uint64_t    src_addr3;
        uint64_t    sed_addr;
    };
    uint8_t     coef[8];
    uint64_t    q_addr;
};

struct ioat_pq_ext_descriptor {
    uint64_t    src_addr4;
    uint64_t    src_addr5;
    uint64_t    src_addr6;
    uint64_t    next;
    uint64_t    src_addr7;
    uint64_t    src_addr8;
    uint64_t    rsvd[2];
};

struct ioat_pq_update_descriptor {
    uint32_t    size;
    union {
        uint32_t ctl;
        struct {
            unsigned int int_en:1;
            unsigned int src_snoop_dis:1;
            unsigned int dest_snoop_dis:1;
            unsigned int compl_write:1;
            unsigned int fence:1;
            unsigned int src_cnt:3;
            unsigned int bundle:1;
            unsigned int dest_dca:1;
            unsigned int hint:1;
            unsigned int p_disable:1;
            unsigned int q_disable:1;
            unsigned int rsvd:3;
            unsigned int coef:8;
            unsigned int op:8;
        } ctl_f;
    };
    uint64_t    src_addr;
    uint64_t    p_addr;
    uint64_t    next;
    uint64_t    src_addr2;
    uint64_t    p_src;
    uint64_t    q_src;
    uint64_t    q_addr;
};

struct ioat_raw_descriptor {
    uint64_t    field[8];
};

struct ioat_sed_raw_descriptor {
    uint64_t    a[8];
    uint64_t    b[8];
    uint64_t    c[8];
};

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

typedef struct IOATChannelState {
    uint16_t chanctrl;
    uint8_t chancmd;
    uint16_t dmacount;
    uint64_t chansts;
    uint64_t chain_addr;
    uint64_t current_desc;
    uint32_t chancmp_low;
    uint32_t chancmp_high;
    uint32_t chanerr;
    uint32_t chanerr_mask;
    uint16_t chan_drsctl;
    uint32_t chan_ltr_swsel;
    uint32_t chan_ltr_active;
    uint32_t chan_ltr_idle;
} IOATChannelState;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint8_t intrctrl;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t chancnt;
    uint8_t xfercap;
    uint8_t version;
    uint32_t dma_cap;
    uint32_t dcactrl;
    uint32_t prefetch_limit;
    uint32_t dmauncerrsts;
    uint32_t chanerr_int;

    IOATChannelState channels[IOAT_MAX_CHANS];
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s, unsigned int chan_idx)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (msix_enabled(pdev)) {
        if (s->intrctrl & IOAT_INTRCTRL_MSIX_VECTOR_CONTROL) {
            msix_notify(pdev, chan_idx);
        }
    } else if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

static void pcibase_set_chan_error(PCIBaseState *s, unsigned int chan_idx,
                                   uint32_t error)
{
    s->channels[chan_idx].chanerr |= error;
    s->channels[chan_idx].chansts = IOAT_CHANSTS_HALTED;
}

static bool pcibase_copy(PCIBaseState *s, unsigned int chan_idx,
                         uint64_t src, uint64_t dst, uint32_t len)
{
    uint8_t *buf;
    MemTxResult result;

    if (!len || len > (1U << s->xfercap)) {
        pcibase_set_chan_error(s, chan_idx, IOAT_CHANERR_LENGTH_ERR);
        return false;
    }

    buf = g_malloc(len);
    result = pci_dma_read(PCI_DEVICE(s), src, buf, len);
    if (result != MEMTX_OK) {
        g_free(buf);
        pcibase_set_chan_error(s, chan_idx,
                               IOAT_CHANERR_SRC_ADDR_ERR |
                               IOAT_CHANERR_READ_DATA_ERR);
        return false;
    }

    result = pci_dma_write(PCI_DEVICE(s), dst, buf, len);
    g_free(buf);
    if (result != MEMTX_OK) {
        pcibase_set_chan_error(s, chan_idx,
                               IOAT_CHANERR_DEST_ADDR_ERR |
                               IOAT_CHANERR_WRITE_DATA_ERR);
        return false;
    }

    return true;
}

/*
 * Consume the descriptor count published by the driver.  I/OAT v2+ starts a
 * chain by programming CHAINADDR and advances it by writing DMACOUNT.
 */
static void pcibase_process_descriptors(PCIBaseState *s,
                                        unsigned int chan_idx,
                                        uint16_t new_count)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    IOATChannelState *chan = &s->channels[chan_idx];
    uint16_t pending = new_count - chan->dmacount;
    uint64_t desc_addr = chan->current_desc ?: chan->chain_addr;
    unsigned int processed = 0;

    chan->dmacount = new_count;
    if (!pending) {
        return;
    }
    if (!desc_addr || (desc_addr & 0x3f)) {
        pcibase_set_chan_error(s, chan_idx,
                               IOAT_CHANERR_CHAIN_ADDR_VALUE_ERR);
        return;
    }
    if (pending > IOAT_MAX_DESCS_PER_KICK) {
        pcibase_set_chan_error(s, chan_idx, IOAT_CHANERR_CONTROL_ERR);
        return;
    }

    chan->chansts = IOAT_CHANSTS_ACTIVE;
    while (processed++ < pending) {
        struct ioat_dma_descriptor desc;
        uint32_t ctl;
        uint32_t len;
        uint64_t src;
        uint64_t dst;
        uint64_t next;
        uint64_t completion;
        uint64_t completion_addr;

        if (pci_dma_read(pdev, desc_addr, &desc, sizeof(desc)) != MEMTX_OK) {
            pcibase_set_chan_error(s, chan_idx,
                                   IOAT_CHANERR_NEXT_ADDR_ERR |
                                   IOAT_CHANERR_READ_DATA_ERR);
            return;
        }

        ctl = le32_to_cpu(desc.ctl);
        len = le32_to_cpu(desc.size);
        src = le64_to_cpu(desc.src_addr);
        dst = le64_to_cpu(desc.dst_addr);
        next = le64_to_cpu(desc.next);

        if (!(ctl & IOAT_DESC_CTL_NULL)) {
            unsigned int op = (ctl >> IOAT_DESC_CTL_OP_SHIFT) &
                              IOAT_DESC_CTL_OP_MASK;

            if (op != IOAT_OP_COPY) {
                pcibase_set_chan_error(s, chan_idx,
                                       IOAT_CHANERR_CONTROL_ERR);
                return;
            }
            if (!pcibase_copy(s, chan_idx, src, dst, len)) {
                return;
            }
        }

        completion = desc_addr | IOAT_CHANSTS_DONE;
        chan->chansts = completion;
        completion_addr = ((uint64_t)chan->chancmp_high << 32) |
                          chan->chancmp_low;
        if (ctl & IOAT_DESC_CTL_COMPL_WRITE) {
            uint64_t completion_le = cpu_to_le64(completion);

            if (!completion_addr ||
                pci_dma_write(pdev, completion_addr, &completion_le,
                              sizeof(completion_le)) != MEMTX_OK) {
                pcibase_set_chan_error(s, chan_idx,
                                       IOAT_CHANERR_COMPLETION_ADDR_ERR |
                                       IOAT_CHANERR_WRITE_DATA_ERR);
                return;
            }
        }

        chan->current_desc = next;
        if (ctl & IOAT_DESC_CTL_INT_EN) {
            s->intrctrl |= IOAT_INTRCTRL_INT_STATUS | IOAT_INTRCTRL_INT;
            if (s->intrctrl & IOAT_INTRCTRL_MASTER_INT_EN) {
                pcibase_update_irq(s, chan_idx);
            }
        }

        desc_addr = next;
        if (processed < pending && (!desc_addr || (desc_addr & 0x3f))) {
            pcibase_set_chan_error(s, chan_idx,
                                   IOAT_CHANERR_NEXT_ADDR_ERR);
            return;
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x80) {
        switch (addr) {
            case IOAT_CHANCNT_OFFSET: val = s->chancnt; break;
            case IOAT_XFERCAP_OFFSET: val = s->xfercap; break;
            case IOAT_INTRCTRL_OFFSET: val = s->intrctrl; break;
            case IOAT_VER_OFFSET: val = s->version; break;
            case IOAT_DMA_CAP_OFFSET: val = s->dma_cap; break;
            case IOAT_DCACTRL_OFFSET: val = s->dcactrl; break;
            case IOAT_PREFETCH_LIMIT_OFFSET: val = s->prefetch_limit; break;
            case IOAT_PCI_DMAUNCERRSTS_OFFSET: val = s->dmauncerrsts; break;
            case IOAT_PCI_CHANERR_INT_OFFSET: val = s->chanerr_int; break;
        }
    } else {
        int chan_idx = (addr / 0x80) - 1;
        if (chan_idx >= 0 && chan_idx < IOAT_MAX_CHANS) {
            hwaddr chan_offset = addr % 0x80;
            switch (chan_offset) {
                case IOAT_CHANCTRL_OFFSET: val = s->channels[chan_idx].chanctrl; break;
                case IOAT2_CHANCMD_OFFSET: val = s->channels[chan_idx].chancmd; break;
                case IOAT_CHAN_DMACOUNT_OFFSET: val = s->channels[chan_idx].dmacount; break;
                case IOAT_CHANSTS_OFFSET: val = s->channels[chan_idx].chansts; break;
                case IOAT2_CHAINADDR_OFFSET_LOW: val = s->channels[chan_idx].chain_addr; break;
                case IOAT2_CHAINADDR_OFFSET_HIGH: val = s->channels[chan_idx].chain_addr >> 32; break;
                case IOAT_CHANCMP_OFFSET_LOW: val = s->channels[chan_idx].chancmp_low; break;
                case IOAT_CHANCMP_OFFSET_HIGH: val = s->channels[chan_idx].chancmp_high; break;
                case IOAT_CHANERR_OFFSET: val = s->channels[chan_idx].chanerr; break;
                case IOAT_CHANERR_MASK_OFFSET: val = s->channels[chan_idx].chanerr_mask; break;
                case IOAT_CHAN_DRSCTL_OFFSET: val = s->channels[chan_idx].chan_drsctl; break;
                case IOAT_CHAN_LTR_SWSEL_OFFSET: val = s->channels[chan_idx].chan_ltr_swsel; break;
                case IOAT_CHAN_LTR_ACTIVE_OFFSET: val = s->channels[chan_idx].chan_ltr_active; break;
                case IOAT_CHAN_LTR_IDLE_OFFSET: val = s->channels[chan_idx].chan_ltr_idle; break;
            }
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x80) {
        switch (addr) {
            case IOAT_INTRCTRL_OFFSET:
                /* INT_STATUS/INT are status bits and are cleared by ack. */
                s->intrctrl = (val & ~(IOAT_INTRCTRL_INT_STATUS |
                                      IOAT_INTRCTRL_INT));
                pci_set_irq(PCI_DEVICE(s), 0);
                break;
            case IOAT_DCACTRL_OFFSET: s->dcactrl = val; break;
            case IOAT_PREFETCH_LIMIT_OFFSET: s->prefetch_limit = val; break;
            case IOAT_PCI_DMAUNCERRSTS_OFFSET: s->dmauncerrsts = val; break;
            case IOAT_PCI_CHANERR_INT_OFFSET: s->chanerr_int = val; break;
        }
    } else {
        int chan_idx = (addr / 0x80) - 1;
        if (chan_idx >= 0 && chan_idx < IOAT_MAX_CHANS) {
            hwaddr chan_offset = addr % 0x80;
            switch (chan_offset) {
                case IOAT_CHANCTRL_OFFSET: s->channels[chan_idx].chanctrl = val; break;
                case IOAT2_CHANCMD_OFFSET:
                    if (val & IOAT_CHANCMD_RESET) {
                        s->channels[chan_idx].chansts = IOAT_CHANSTS_HALTED;
                        s->channels[chan_idx].chanerr = 0;
                        s->channels[chan_idx].dmacount = 0;
                        s->channels[chan_idx].chain_addr = 0;
                        s->channels[chan_idx].current_desc = 0;
                    } else if (val & IOAT_CHANCMD_SUSPEND) {
                        s->channels[chan_idx].chansts = IOAT_CHANSTS_SUSPENDED;
                    } else if (val & IOAT_CHANCMD_ABORT) {
                        s->channels[chan_idx].chansts = IOAT_CHANSTS_HALTED;
                    } else if (val & IOAT_CHANCMD_RESUME) {
                        s->channels[chan_idx].chansts = IOAT_CHANSTS_DONE;
                    }
                    /* Command bits are hardware-owned and self-clearing. */
                    s->channels[chan_idx].chancmd = 0;
                    break;
                case IOAT_CHAN_DMACOUNT_OFFSET:
                    pcibase_process_descriptors(s, chan_idx, val);
                    break;
                case IOAT2_CHAINADDR_OFFSET_LOW:
                    s->channels[chan_idx].chain_addr =
                        (s->channels[chan_idx].chain_addr &
                         0xffffffff00000000ULL) | (uint32_t)val;
                    s->channels[chan_idx].current_desc = 0;
                    break;
                case IOAT2_CHAINADDR_OFFSET_HIGH:
                    s->channels[chan_idx].chain_addr =
                        (s->channels[chan_idx].chain_addr & 0xffffffffULL) |
                        ((uint64_t)(uint32_t)val << 32);
                    s->channels[chan_idx].current_desc = 0;
                    break;
                case IOAT_CHANCMP_OFFSET_LOW: s->channels[chan_idx].chancmp_low = val; break;
                case IOAT_CHANCMP_OFFSET_HIGH: s->channels[chan_idx].chancmp_high = val; break;
                case IOAT_CHANERR_OFFSET:
                    /* Channel error is write-one-to-clear. */
                    s->channels[chan_idx].chanerr &= ~(uint32_t)val;
                    break;
                case IOAT_CHANERR_MASK_OFFSET: s->channels[chan_idx].chanerr_mask = val; break;
                case IOAT_CHAN_DRSCTL_OFFSET: s->channels[chan_idx].chan_drsctl = val; break;
                case IOAT_CHAN_LTR_SWSEL_OFFSET: s->channels[chan_idx].chan_ltr_swsel = val; break;
                case IOAT_CHAN_LTR_ACTIVE_OFFSET: s->channels[chan_idx].chan_ltr_active = val; break;
                case IOAT_CHAN_LTR_IDLE_OFFSET: s->channels[chan_idx].chan_ltr_idle = val; break;
            }
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

    s->chancnt = IOAT_MAX_CHANS;
    s->xfercap = 12; /* 4KB */
    s->version = IOAT_VER_3_4; /* 3.4 */
    /* This model implements the mandatory memcpy engine, not RAID offloads. */
    s->dma_cap = 0;
    s->intrctrl = 0;
    s->dcactrl = 0;
    s->prefetch_limit = 0;
    s->dmauncerrsts = 0;
    s->chanerr_int = 0;

    for (int i = 0; i < IOAT_MAX_CHANS; i++) {
        s->channels[i].chanctrl = 0;
        s->channels[i].chancmd = 0;
        s->channels[i].dmacount = 0;
        s->channels[i].chansts = IOAT_CHANSTS_HALTED;
        s->channels[i].chain_addr = 0;
        s->channels[i].current_desc = 0;
        s->channels[i].chancmp_low = 0;
        s->channels[i].chancmp_high = 0;
        s->channels[i].chanerr = 0;
        s->channels[i].chanerr_mask = 0;
        s->channels[i].chan_drsctl = 0;
        s->channels[i].chan_ltr_swsel = 0;
        s->channels[i].chan_ltr_active = 0;
        s->channels[i].chan_ltr_idle = 0;
    }
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int ret;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_IOAT_TBG0 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0801 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = IOAT_MMIO_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "ioat-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;
    ret = msix_init(pdev, IOAT_MAX_CHANS, &s->bar_regions[0], 0, 0x2000,
                    &s->bar_regions[0], 0, 0x3000, 0, errp);
    if (ret < 0) {
        s->has_msix = false;
        return;
    }
    for (int i = 0; i < IOAT_MAX_CHANS; i++) {
        msix_vector_use(pdev, i);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_present(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ioatdma_pci",
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
