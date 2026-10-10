/*
 * QEMU PCI model for the Marvell/SysKonnect skge Yukon NIC.
 *
 * This is deliberately kept next to skge_pci.c so that it can be used as an
 * A/B model for coverage experiments.  The original model only emulates the
 * PCI probe/MMIO surface.  This variant additionally implements the small
 * descriptor engine needed by the Linux skge driver:
 *
 *   - Q_DA_H/Q_DA_L and Q_CSR are retained for RX/TX queues;
 *   - skge_rx_desc/skge_tx_desc are read from guest memory;
 *   - TX buffers are read with pci_dma_read();
 *   - one deterministic RX frame is written with pci_dma_write();
 *   - BMU_OWN is transferred back to the host;
 *   - IS_R1_F/IS_XA1_F are raised and INTx is asserted.
 *
 * The model does not attempt to reproduce the complete Yukon MAC or a real
 * external network backend.  It is intentionally deterministic: one RX frame
 * is supplied after RX is started, while TX descriptors are consumed and
 * completed.  This is sufficient to drive skge_up(), skge_xmit_frame(),
 * skge_poll(), skge_rx_get(), and skge_tx_done() in a guest.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-core.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#ifndef PCI_VENDOR_ID_3COM
#define PCI_VENDOR_ID_3COM 0x10b7
#endif

#define TYPE_SKGE_DMA_PCI "skge_dma_pci"
OBJECT_DECLARE_SIMPLE_TYPE(SkgeDmaState, SKGE_DMA_PCI)

#define SKGE_VENDOR_ID PCI_VENDOR_ID_3COM
#define SKGE_DEVICE_ID 0x1700
#define SKGE_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

/* Registers needed by skge_probe() and skge_reset(). */
#define B0_CTST       0x0004
#define B0_LED        0x0006
#define B0_POWER_CTRL 0x0007
#define B0_ISRC       0x0008
#define B0_IMSK       0x000c
#define B0_HWE_ISRC   0x0010
#define B0_HWE_IMSK   0x0014
#define B0_SP_ISRC    0x0018
#define B0_R1_CSR     0x0060
#define B0_R2_CSR     0x0064

/* GMAC register window used by gma_read16()/gma_write16() in skge.c. */
#define SKGE_GMAC_BASE(port) (0x2800u + (port) * 0x1000u)
#define GM_SMI_CTRL          0x0080
#define GM_SMI_DATA          0x0084

#define B2_MAC_1      0x0100
#define B2_MAC_2      0x0108
#define B2_PMD_TYP    0x0119
#define B2_MAC_CFG    0x011a
#define B2_CHIP_ID    0x011b
#define B2_E_0        0x011c
#define B2_E_1        0x011d
#define B2_FAR        0x0120
#define B2_TI_CTRL    0x0138
#define B2_IRQM_INI   0x0140
#define B2_IRQM_CTRL  0x0148
#define B2_IRQM_MSK   0x014c
#define B2_TST_CTRL1  0x0158
#define B2_TST_CTRL2  0x0159
#define B2_GP_IO      0x015c

#define B3_RI_WTO_R1  0x0190
#define B3_RI_WTO_XA1 0x0191
#define B3_RI_RTO_R1  0x0193
#define B3_RI_RTO_XA1 0x0194
#define B3_RI_WTO_R2  0x0196
#define B3_RI_WTO_XA2 0x0197
#define B3_RI_RTO_R2  0x0199
#define B3_RI_RTO_XA2 0x019a
#define B3_RI_CTRL    0x01a0
#define B3_MA_TO_CTRL 0x01b8
#define B3_PA_CTRL    0x01f0

#define CS_RST_SET  0x01
#define CS_RST_CLR  0x02
#define CS_MRST_CLR (1u << 3)
#define LED_STAT_OFF 1

/* Interrupt source bits from skge.h. */
#define IS_EXT_REG (1u << 23)
#define IS_XA1_F   (1u << 7)
#define IS_R1_F    (1u << 16)
#define IS_XA2_F   (1u << 1)
#define IS_R2_F    (1u << 13)
#define IS_PA_TO_RX1 (1u << 29)
#define IS_PA_TO_TX1 (1u << 27)
#define IS_PA_TO_RX2 (1u << 28)
#define IS_PA_TO_TX2 (1u << 26)
#define IS_MAC1 (1u << 21)
#define IS_MAC2 (1u << 19)
#define IS_HW_ERR (1u << 31)

/* Real queue offsets used by Q_ADDR() in skge.h. */
#define B8_Q_REGS 0x0400
#define Q_R1      0x0000
#define Q_R2      0x0080
#define Q_XS1     0x0200
#define Q_XA1     0x0280
#define Q_XS2     0x0300
#define Q_XA2     0x0380
#define Q_DA_L    0x20
#define Q_DA_H    0x24
#define Q_CSR     0x34
#define Q_ADDR(q, r) (B8_Q_REGS + (q) + (r))

/* Queue control bits from skge.h. */
#define CSR_STOP    (1u << 5)
#define CSR_START   (1u << 4)
#define CSR_IRQ_CL_F (1u << 1)

/* Descriptor bits and status values from skge.h. */
#define BMU_OWN       (1u << 31)
#define BMU_STF       (1u << 30)
#define BMU_EOF       (1u << 29)
#define BMU_IRQ_EOF   (1u << 27)
#define BMU_TCP_CHECK (0x56u << 16)
#define BMU_BBC       0xffffu
#define GMR_FS_RX_OK  (1u << 8)
#define GMR_FS_BC     (1u << 9)
#define GMR_FS_LEN_SHIFT 16

#define SKGE_BAR_SIZE 0x4000
#define SKGE_MAX_DESC 64
#define SKGE_RX_FRAME_LEN 60

/* Minimal Marvell PHY register/state values needed for link-up reporting. */
#define PHY_MARV_CTRL       0x00
#define PHY_MARV_AUNE_LP    0x05
#define PHY_MARV_1000T_STAT 0x0a
#define PHY_MARV_PHY_STAT   0x11
#define PHY_MARV_INT_MASK   0x12
#define PHY_MARV_INT_STAT   0x13
#define PHY_M_PS_SPEED_1000 (1u << 15)
#define PHY_M_PS_FULL_DUP   (1u << 13)
#define PHY_M_PS_SPDUP_RES  (1u << 11)
#define PHY_M_PS_LINK_UP    (1u << 10)
#define PHY_M_PS_TX_P_EN    (1u << 3)
#define PHY_M_PS_RX_P_EN    (1u << 2)
#define PHY_M_IS_AN_COMPL   (1u << 11)
#define PHY_M_IS_LST_CHANGE (1u << 10)
#define GM_SMI_CT_OP_RD     (1u << 5)
#define GM_SMI_CT_RD_VAL    (1u << 4)
#define GM_SMI_CT_REG_A_MSK (0x1fu << 6)

typedef struct QEMU_PACKED SkgeRxDesc {
    uint32_t control;
    uint32_t next_offset;
    uint32_t dma_lo;
    uint32_t dma_hi;
    uint32_t status;
    uint32_t timestamp;
    uint16_t csum2;
    uint16_t csum1;
    uint16_t csum2_start;
    uint16_t csum1_start;
} SkgeRxDesc;

typedef struct QEMU_PACKED SkgeTxDesc {
    uint32_t control;
    uint32_t next_offset;
    uint32_t dma_lo;
    uint32_t dma_hi;
    uint32_t status;
    uint32_t csum_offs;
    uint16_t csum_write;
    uint16_t csum_start;
    uint32_t rsvd;
} SkgeTxDesc;

typedef struct SkgeQueue {
    uint64_t desc_base;
    uint64_t current;
    uint32_t csr;
    bool running;
    bool rx_frame_supplied;
} SkgeQueue;

struct SkgeDmaState {
    PCIDevice parent_obj;
    MemoryRegion bar0;
    uint8_t regs[SKGE_BAR_SIZE];

    uint8_t chip_id;
    uint8_t chip_rev;
    uint8_t ports;

    uint32_t sp_isrc;
    uint32_t imsk;
    uint32_t isrc;
    uint32_t hwe_isrc;
    uint32_t hwe_imsk;
    uint32_t irqm_msk;
    uint32_t irqm_ini;
    uint32_t irqm_ctrl;

    SkgeQueue rx[2];
    SkgeQueue tx[2];

    uint16_t phy_regs[2][32];
    uint16_t smi_ctrl[2];
    uint16_t smi_data[2];

    /* Opt-in tracing of guest MMIO kicks and TX DMA descriptors. */
    bool trace;
};

#define SKGE_TRACE(s, fmt, ...) \
    do { \
        if ((s)->trace) \
            qemu_log_mask(LOG_GUEST_ERROR, "skge_dma_pci: " fmt, \
                          ##__VA_ARGS__); \
    } while (0)

static inline uint32_t load_le32(const void *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return le32_to_cpu(v);
}

static inline void store_le32(void *p, uint32_t v)
{
    uint32_t le = cpu_to_le32(v);
    memcpy(p, &le, sizeof(le));
}

static inline SkgeDmaState *skge_dma_from_pci(PCIDevice *pdev)
{
    return SKGE_DMA_PCI(pdev);
}

static void skge_dma_update_irq(SkgeDmaState *s)
{
    pci_set_irq(PCI_DEVICE(s), (s->sp_isrc & s->imsk) != 0);
}

static void skge_dma_set_irq(SkgeDmaState *s, uint32_t bits)
{
    s->sp_isrc |= bits;
    s->isrc |= bits;
    skge_dma_update_irq(s);
}

static void skge_dma_clear_irq(SkgeDmaState *s, uint32_t bits)
{
    s->sp_isrc &= ~bits;
    s->isrc &= ~bits;
    skge_dma_update_irq(s);
}

static void skge_dma_init_identity(SkgeDmaState *s)
{
    /* Keep both skge ports distinct from QEMU's default e1000 MAC. */
    static const uint8_t mac1[6] = { 0x52, 0x54, 0x00, 0x56, 0x78, 0x01 };
    static const uint8_t mac2[6] = { 0x52, 0x54, 0x00, 0x56, 0x78, 0x02 };

    s->regs[B2_CHIP_ID] = s->chip_id;
    s->regs[B2_MAC_CFG] = (s->chip_rev << 4) |
                          (s->ports == 1 ? 0x01 : 0x00);
    s->regs[B2_E_0] = 0;
    s->regs[B2_E_1] = 0x04;
    s->regs[B2_PMD_TYP] = 'T';
    memcpy(&s->regs[B2_MAC_1], mac1, sizeof(mac1));
    memcpy(&s->regs[B2_MAC_2], mac2, sizeof(mac2));
}

static void skge_dma_reset_queues(SkgeDmaState *s)
{
    memset(s->rx, 0, sizeof(s->rx));
    memset(s->tx, 0, sizeof(s->tx));
}

static void skge_dma_reset_state(SkgeDmaState *s)
{
    memset(s->regs, 0, sizeof(s->regs));
    s->chip_id = 0xb0; /* CHIP_ID_YUKON */
    s->chip_rev = 1;
    s->ports = 2;
    s->sp_isrc = 0;
    s->imsk = 0;
    s->isrc = 0;
    s->hwe_isrc = 0;
    s->hwe_imsk = 0xffffffffu;
    s->irqm_msk = 0;
    s->irqm_ini = 0;
    s->irqm_ctrl = 0;
    skge_dma_reset_queues(s);
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    memset(s->smi_ctrl, 0, sizeof(s->smi_ctrl));
    memset(s->smi_data, 0, sizeof(s->smi_data));
    for (unsigned port = 0; port < 2; port++) {
        s->phy_regs[port][PHY_MARV_PHY_STAT] =
            PHY_M_PS_SPEED_1000 | PHY_M_PS_FULL_DUP |
            PHY_M_PS_SPDUP_RES | PHY_M_PS_LINK_UP |
            PHY_M_PS_TX_P_EN | PHY_M_PS_RX_P_EN;
    }
    skge_dma_init_identity(s);
    s->regs[B0_LED] = LED_STAT_OFF;
    skge_dma_update_irq(s);
}

static int skge_dma_queue_index(hwaddr addr, bool *is_tx, unsigned *port,
                                unsigned *offset)
{
    static const unsigned qoff[2][2] = {
        { Q_R1, Q_XA1 },
        { Q_R2, Q_XA2 },
    };
    unsigned p, t;

    for (p = 0; p < 2; p++) {
        for (t = 0; t < 2; t++) {
            hwaddr base = B8_Q_REGS + qoff[p][t];
            if (addr >= base && addr < base + 0x80) {
                *is_tx = t != 0;
                *port = p;
                *offset = addr - base;
                return 0;
            }
        }
    }
    return -1;
}

static SkgeQueue *skge_dma_queue(SkgeDmaState *s, bool is_tx, unsigned port)
{
    return is_tx ? &s->tx[port] : &s->rx[port];
}

static uint64_t skge_dma_next_desc(uint64_t base, uint32_t next_offset)
{
    /* skge stores next_offset as a 32-bit descriptor address.  Preserve the
     * high half of the queue base for guests that place coherent memory above
     * 4 GiB, while retaining the normal low-memory behavior. */
    return (base & ~UINT64_C(0xffffffff)) | next_offset;
}

static bool skge_dma_read_rx_desc(SkgeDmaState *s, uint64_t addr,
                                  SkgeRxDesc *d)
{
    return pci_dma_read(PCI_DEVICE(s), addr, d, sizeof(*d)) == MEMTX_OK;
}

static bool skge_dma_write_rx_desc(SkgeDmaState *s, uint64_t addr,
                                   const SkgeRxDesc *d)
{
    return pci_dma_write(PCI_DEVICE(s), addr, d, sizeof(*d)) == MEMTX_OK;
}

static bool skge_dma_read_tx_desc(SkgeDmaState *s, uint64_t addr,
                                  SkgeTxDesc *d)
{
    return pci_dma_read(PCI_DEVICE(s), addr, d, sizeof(*d)) == MEMTX_OK;
}

static bool skge_dma_write_tx_control(SkgeDmaState *s, uint64_t addr,
                                      uint32_t control)
{
    uint32_t le = cpu_to_le32(control);
    return pci_dma_write(PCI_DEVICE(s), addr + offsetof(SkgeTxDesc, control),
                         &le, sizeof(le)) == MEMTX_OK;
}

static void skge_dma_complete_rx(SkgeDmaState *s, unsigned port)
{
    static const uint8_t frame[SKGE_RX_FRAME_LEN] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0x52, 0x54, 0x00, 0x12, 0x34, 0x56,
        0x08, 0x00,
        0x45, 0x00, 0x00, 0x2e, 0x00, 0x01, 0x00, 0x00,
        0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8, 0x00, 0x02,
        0xc0, 0xa8, 0x00, 0x01,
        0x00, 0x01, 0x00, 0x02, 0x00, 0x1a, 0x00, 0x00,
        0x73, 0x6b, 0x67, 0x65, 0x2d, 0x64, 0x6d, 0x61,
        0x2d, 0x72, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00
    };
    SkgeQueue *q = &s->rx[port];
    SkgeRxDesc d;
    uint64_t addr = q->current ? q->current : q->desc_base;
    uint64_t buf;
    uint32_t control, buf_len, write_len;

    if (q->rx_frame_supplied || !addr || !skge_dma_read_rx_desc(s, addr, &d)) {
        return;
    }

    control = load_le32(&d.control);
    if (!(control & BMU_OWN)) {
        return;
    }

    buf = (uint64_t)load_le32(&d.dma_lo) |
          ((uint64_t)load_le32(&d.dma_hi) << 32);
    buf_len = control & BMU_BBC;
    write_len = MIN(buf_len, (uint32_t)sizeof(frame));
    if (!buf || !write_len ||
        pci_dma_write(PCI_DEVICE(s), buf, frame, write_len) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "skge_dma_pci: RX DMA write failed at 0x%" PRIx64
                      "\n", buf);
        return;
    }

    store_le32(&d.status, (write_len << GMR_FS_LEN_SHIFT) |
               GMR_FS_RX_OK | GMR_FS_BC);
    control &= ~BMU_OWN;
    control &= ~BMU_BBC;
    control |= BMU_STF | BMU_EOF | BMU_IRQ_EOF | BMU_TCP_CHECK | write_len;
    store_le32(&d.control, control);
    if (!skge_dma_write_rx_desc(s, addr, &d)) {
        return;
    }

    q->current = skge_dma_next_desc(q->desc_base, load_le32(&d.next_offset));
    q->rx_frame_supplied = true;
    skge_dma_set_irq(s, port ? IS_R2_F : IS_R1_F);
}

static void skge_dma_complete_tx(SkgeDmaState *s, unsigned port)
{
    SkgeQueue *q = &s->tx[port];
    uint64_t addr = q->current ? q->current : q->desc_base;
    bool completed = false;
    unsigned i;

    for (i = 0; i < SKGE_MAX_DESC && addr; i++) {
        SkgeTxDesc d;
        uint32_t control, len;
        uint64_t buf;
        uint8_t scratch[2048];

        if (!skge_dma_read_tx_desc(s, addr, &d)) {
            break;
        }
        control = load_le32(&d.control);
        if (!(control & BMU_OWN)) {
            break;
        }

        len = control & BMU_BBC;
        buf = (uint64_t)load_le32(&d.dma_lo) |
              ((uint64_t)load_le32(&d.dma_hi) << 32);
        if (buf && len) {
            /* Consume the guest buffer.  The deterministic model does not
             * attach a net backend, but the read exercises the real DMA path. */
            uint32_t n = MIN(len, (uint32_t)sizeof(scratch));
            pci_dma_read(PCI_DEVICE(s), buf, scratch, n);
            SKGE_TRACE(s,
                       "TX port=%u dma=0x%" PRIx64 " len=%u ctrl=0x%08x "
                       "dst=%02x:%02x:%02x:%02x:%02x:%02x "
                       "type=%02x%02x payload=%02x:%02x:%02x:%02x ...\n",
                       port, buf, len, control,
                       n > 0 ? scratch[0] : 0, n > 1 ? scratch[1] : 0,
                       n > 2 ? scratch[2] : 0, n > 3 ? scratch[3] : 0,
                       n > 4 ? scratch[4] : 0, n > 5 ? scratch[5] : 0,
                       n > 12 ? scratch[12] : 0, n > 13 ? scratch[13] : 0,
                       n > 14 ? scratch[14] : 0, n > 15 ? scratch[15] : 0,
                       n > 16 ? scratch[16] : 0, n > 17 ? scratch[17] : 0);
        }

        control &= ~BMU_OWN;
        if (!skge_dma_write_tx_control(s, addr, control)) {
            break;
        }
        completed = true;
        q->current = skge_dma_next_desc(q->desc_base,
                                        load_le32(&d.next_offset));
        addr = q->current;
        if (control & BMU_EOF) {
            break;
        }
    }

    if (completed) {
        skge_dma_set_irq(s, port ? IS_XA2_F : IS_XA1_F);
    }
}

static void skge_dma_queue_write(SkgeDmaState *s, bool is_tx, unsigned port,
                                 unsigned offset, uint32_t value)
{
    SkgeQueue *q = skge_dma_queue(s, is_tx, port);

    if (offset == Q_DA_L) {
        q->desc_base = (q->desc_base & UINT64_C(0xffffffff00000000)) |
                       value;
        q->current = q->desc_base;
        SKGE_TRACE(s, "%s port=%u Q_DA_L=0x%08x\n",
                   is_tx ? "TX" : "RX", port, value);
        return;
    }
    if (offset == Q_DA_H) {
        q->desc_base = (q->desc_base & UINT64_C(0xffffffff)) |
                       ((uint64_t)value << 32);
        q->current = q->desc_base;
        SKGE_TRACE(s, "%s port=%u Q_DA_H=0x%08x\n",
                   is_tx ? "TX" : "RX", port, value);
        return;
    }
    if (offset != Q_CSR) {
        return;
    }

    q->csr = value;
    SKGE_TRACE(s, "%s port=%u Q_CSR=0x%08x%s%s\n",
               is_tx ? "TX" : "RX", port, value,
               (value & CSR_START) ? " START" : "",
               (value & CSR_STOP) ? " STOP" : "");
    if (value & CSR_IRQ_CL_F) {
        skge_dma_clear_irq(s, is_tx ? (port ? IS_XA2_F : IS_XA1_F)
                                    : (port ? IS_R2_F : IS_R1_F));
    }
    if (value & CSR_STOP) {
        q->running = false;
        if (!is_tx) {
            q->rx_frame_supplied = false;
        }
    }
    if (value & CSR_START) {
        q->running = true;
        if (is_tx) {
            skge_dma_complete_tx(s, port);
        } else {
            skge_dma_complete_rx(s, port);
        }
    }
}

static bool skge_dma_queue_read(SkgeDmaState *s, bool is_tx, unsigned port,
                                unsigned offset, uint32_t *value)
{
    SkgeQueue *q = skge_dma_queue(s, is_tx, port);

    if (offset == Q_DA_L) {
        *value = q->desc_base;
        return true;
    }
    if (offset == Q_DA_H) {
        *value = q->desc_base >> 32;
        return true;
    }
    if (offset == Q_CSR) {
        *value = q->csr;
        return true;
    }
    return false;
}

static uint64_t skge_dma_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    SkgeDmaState *s = opaque;
    bool is_tx;
    unsigned port, offset;
    uint32_t v;

    if (size == 4 && addr == B0_IMSK) {
        return s->imsk;
    }
    if (size == 4 && addr == B0_ISRC) {
        return s->isrc;
    }
    if (size == 4 && addr == B0_HWE_ISRC) {
        return s->hwe_isrc;
    }
    if (size == 4 && addr == B0_SP_ISRC) {
        uint32_t pending = s->sp_isrc;
        /* Reading the interrupt source acknowledges the edge-triggered IRQ. */
        s->sp_isrc = 0;
        skge_dma_update_irq(s);
        return pending;
    }
    for (unsigned p = 0; p < 2; p++) {
        hwaddr base = SKGE_GMAC_BASE(p);

        if (size == 2 && addr == base + GM_SMI_CTRL)
            return s->smi_ctrl[p];
        if (size == 2 && addr == base + GM_SMI_DATA)
            return s->smi_data[p];
    }
    if (size == 4 && skge_dma_queue_index(addr, &is_tx, &port, &offset) == 0 &&
        skge_dma_queue_read(s, is_tx, port, offset, &v)) {
        return v;
    }
    if (addr + size > sizeof(s->regs)) {
        return size == 1 ? 0xff : size == 2 ? 0xffff : 0xffffffffu;
    }
    if (size == 1) {
        return s->regs[addr];
    }
    if (size == 2) {
        return lduw_le_p(&s->regs[addr]);
    }
    return ldl_le_p(&s->regs[addr]);
}

static void skge_dma_mmio_write(void *opaque, hwaddr addr, uint64_t value,
                                unsigned size)
{
    SkgeDmaState *s = opaque;
    bool is_tx;
    unsigned port, offset;
    uint32_t v = value;

    if (size == 1 && addr == B0_CTST) {
        if (v & CS_RST_SET) {
            skge_dma_reset_state(s);
        }
        s->regs[addr] = v;
        return;
    }
    if (size == 4 && addr == B0_IMSK) {
        s->imsk = v;
        SKGE_TRACE(s, "MMIO B0_IMSK=0x%08x\n", v);
        stl_le_p(&s->regs[addr], v);
        skge_dma_update_irq(s);
        return;
    }
    if (size == 4 && addr == B0_HWE_IMSK) {
        s->hwe_imsk = v;
        stl_le_p(&s->regs[addr], v);
        return;
    }
    for (unsigned p = 0; p < 2; p++) {
        hwaddr base = SKGE_GMAC_BASE(p);

        if (size == 2 && addr == base + GM_SMI_DATA) {
            s->smi_data[p] = (uint16_t)v;
            return;
        }
        if (size == 2 && addr == base + GM_SMI_CTRL) {
            uint16_t ctrl = (uint16_t)v;
            unsigned reg = (ctrl & GM_SMI_CT_REG_A_MSK) >> 6;

            s->smi_ctrl[p] = ctrl & ~GM_SMI_CT_RD_VAL;
            if (ctrl & GM_SMI_CT_OP_RD) {
                s->smi_data[p] = s->phy_regs[p][reg];
                s->smi_ctrl[p] |= GM_SMI_CT_RD_VAL;
                if (reg == PHY_MARV_INT_STAT)
                    s->phy_regs[p][reg] = 0;
            } else {
                s->phy_regs[p][reg] = s->smi_data[p];
                if (reg == PHY_MARV_INT_MASK && s->smi_data[p]) {
                    s->phy_regs[p][PHY_MARV_INT_STAT] |=
                        PHY_M_IS_AN_COMPL | PHY_M_IS_LST_CHANGE;
                    skge_dma_set_irq(s, IS_EXT_REG);
                }
            }
            return;
        }
    }
    if (skge_dma_queue_index(addr, &is_tx, &port, &offset) == 0) {
        if (size == 4) {
            skge_dma_queue_write(s, is_tx, port, offset, v);
            stl_le_p(&s->regs[addr], v);
        } else if (size == 1 && offset == Q_CSR) {
            skge_dma_queue_write(s, is_tx, port, offset, v);
            s->regs[addr] = v;
        }
        return;
    }
    if (addr + size > sizeof(s->regs)) {
        return;
    }
    if (size == 1) {
        s->regs[addr] = v;
    } else if (size == 2) {
        stw_le_p(&s->regs[addr], v);
    } else if (size == 4) {
        stl_le_p(&s->regs[addr], v);
    }
}

static const MemoryRegionOps skge_dma_mmio_ops = {
    .read = skge_dma_mmio_read,
    .write = skge_dma_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void skge_dma_reset(DeviceState *dev)
{
    SkgeDmaState *s = SKGE_DMA_PCI(dev);
    pci_device_reset(PCI_DEVICE(dev));
    skge_dma_reset_state(s);
}

static void skge_dma_realize(PCIDevice *pdev, Error **errp)
{
    SkgeDmaState *s = SKGE_DMA_PCI(pdev);
    uint8_t *conf = pdev->config;

    pci_set_word(conf + PCI_VENDOR_ID, SKGE_VENDOR_ID);
    pci_set_word(conf + PCI_DEVICE_ID, SKGE_DEVICE_ID);
    pci_set_word(conf + PCI_CLASS_DEVICE, SKGE_CLASS_ID);
    pci_set_byte(conf + PCI_REVISION_ID, 1);
    pci_config_set_interrupt_pin(conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    if (pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp) < 0) {
        return;
    }

    memory_region_init_io(&s->bar0, OBJECT(s), &skge_dma_mmio_ops, s,
                          "skge-dma-mmio", SKGE_BAR_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
    skge_dma_reset_state(s);
}

static void skge_dma_exit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription skge_dma_vmstate = {
    .name = TYPE_SKGE_DMA_PCI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, SkgeDmaState),
        VMSTATE_UINT8(chip_id, SkgeDmaState),
        VMSTATE_UINT8(chip_rev, SkgeDmaState),
        VMSTATE_UINT8(ports, SkgeDmaState),
        VMSTATE_UINT32(sp_isrc, SkgeDmaState),
        VMSTATE_UINT32(imsk, SkgeDmaState),
        VMSTATE_UINT32(isrc, SkgeDmaState),
        VMSTATE_UINT32(hwe_isrc, SkgeDmaState),
        VMSTATE_UINT32(hwe_imsk, SkgeDmaState),
        VMSTATE_UINT32(irqm_msk, SkgeDmaState),
        VMSTATE_UINT32(irqm_ini, SkgeDmaState),
        VMSTATE_UINT32(irqm_ctrl, SkgeDmaState),
        VMSTATE_UINT16_2DARRAY(phy_regs, SkgeDmaState, 2, 32),
        VMSTATE_UINT16_ARRAY(smi_ctrl, SkgeDmaState, 2),
        VMSTATE_UINT16_ARRAY(smi_data, SkgeDmaState, 2),
        VMSTATE_BOOL(trace, SkgeDmaState),
        VMSTATE_BUFFER(regs, SkgeDmaState),
        VMSTATE_END_OF_LIST()
    }
};

static Property skge_dma_properties[] = {
    DEFINE_PROP_BOOL("trace", SkgeDmaState, trace, false),
    DEFINE_PROP_END_OF_LIST(),
};

static void skge_dma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = skge_dma_realize;
    k->exit = skge_dma_exit;
    dc->reset = skge_dma_reset;
    dc->vmsd = &skge_dma_vmstate;
    device_class_set_props(dc, skge_dma_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void skge_dma_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo info = {
        .name = TYPE_SKGE_DMA_PCI,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(SkgeDmaState),
        .class_init = skge_dma_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&info);
}

type_init(skge_dma_register_types);
