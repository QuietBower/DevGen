/*
 * QEMU PCI device model for Kvaser PCIEFD CAN controller (behavioral skeleton)
 *
 * This model is intentionally minimal and only implements the behavior
 * that can be derived strictly from the provided Linux driver source.
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

#define TYPE_PCIBASE_DEVICE "kvaser_pciefd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif

#ifndef BITS_PER_LONG
#define BITS_PER_LONG (sizeof(unsigned long) * 8)
#endif

#ifndef GENMASK
#define GENMASK(h, l) \
    (((~0UL) - ((1UL << (l)) - 1)) & \
     (~0UL >> (BITS_PER_LONG - 1 - (h))))
#endif

#define KVASER_PCIEFD_VENDOR              0x1a07
#define KVASER_PCIEFD_4HS_DEVICE_ID       0x000d

#define KVASER_PCIEFD_PCI_VENDOR_ID       KVASER_PCIEFD_VENDOR
#define KVASER_PCIEFD_PCI_DEVICE_ID       KVASER_PCIEFD_4HS_DEVICE_ID

#define KVASER_PCIEFD_PCI_CLASS_ID        PCI_CLASS_NETWORK_OTHER

#define KVASER_PCIEFD_KCAN_FIFO_REG              0x100
#define KVASER_PCIEFD_KCAN_FIFO_LAST_REG         0x180
#define KVASER_PCIEFD_KCAN_CTRL_REG              0x2c0
#define KVASER_PCIEFD_KCAN_CMD_REG               0x400
#define KVASER_PCIEFD_KCAN_IOC_REG               0x404
#define KVASER_PCIEFD_KCAN_IEN_REG               0x408
#define KVASER_PCIEFD_KCAN_IRQ_REG               0x410
#define KVASER_PCIEFD_KCAN_TX_NR_PACKETS_REG     0x414
#define KVASER_PCIEFD_KCAN_STAT_REG              0x418
#define KVASER_PCIEFD_KCAN_MODE_REG              0x41c
#define KVASER_PCIEFD_KCAN_BTRN_REG              0x420
#define KVASER_PCIEFD_KCAN_BUS_LOAD_REG          0x424
#define KVASER_PCIEFD_KCAN_BTRD_REG              0x428
#define KVASER_PCIEFD_KCAN_PWM_REG               0x430

#define KVASER_PCIEFD_SYSID_VERSION_REG          0x8
#define KVASER_PCIEFD_SYSID_CANFREQ_REG          0xC
#define KVASER_PCIEFD_SYSID_BUSFREQ_REG          0x10
#define KVASER_PCIEFD_SYSID_BUILD_REG            0x14

#define KVASER_PCIEFD_SRB_FIFO_LAST_REG          0x1f4
#define KVASER_PCIEFD_SRB_CMD_REG                0x0
#define KVASER_PCIEFD_SRB_IEN_REG                0x04
#define KVASER_PCIEFD_SRB_IRQ_REG                0x0C
#define KVASER_PCIEFD_SRB_STAT_REG               0x10
#define KVASER_PCIEFD_SRB_RX_NR_PACKETS_REG      0x14
#define KVASER_PCIEFD_SRB_CTRL_REG               0x18

#define KVASER_PCIEFD_SYSID_VERSION_NR_CHAN_MASK GENMASK(31, 24)
#define KVASER_PCIEFD_SYSID_VERSION_MAJOR_MASK   GENMASK(23, 16)
#define KVASER_PCIEFD_SYSID_VERSION_MINOR_MASK   GENMASK(7, 0)
#define KVASER_PCIEFD_SYSID_BUILD_SEQ_MASK       GENMASK(15, 1)

#define KVASER_PCIEFD_SRB_CMD_RDB1               BIT(5)
#define KVASER_PCIEFD_SRB_CMD_RDB0               BIT(4)
#define KVASER_PCIEFD_SRB_CMD_FOR                BIT(0)

#define KVASER_PCIEFD_SRB_IRQ_DUF1               BIT(13)
#define KVASER_PCIEFD_SRB_IRQ_DUF0               BIT(12)
#define KVASER_PCIEFD_SRB_IRQ_DOF1               BIT(11)
#define KVASER_PCIEFD_SRB_IRQ_DOF0               BIT(10)
#define KVASER_PCIEFD_SRB_IRQ_DPD1               BIT(9)
#define KVASER_PCIEFD_SRB_IRQ_DPD0               BIT(8)

#define KVASER_PCIEFD_SRB_STAT_DMA               BIT(24)
#define KVASER_PCIEFD_SRB_STAT_DI                BIT(15)
#define KVASER_PCIEFD_SRB_RX_NR_PACKETS_MASK     GENMASK(7, 0)
#define KVASER_PCIEFD_SRB_CTRL_DMA_ENABLE        BIT(0)

#define KVASER_PCIEFD_KCAN_CTRL_TYPE_MASK        GENMASK(31, 29)
#define KVASER_PCIEFD_KCAN_CTRL_TYPE_EFLUSH      0x4
#define KVASER_PCIEFD_KCAN_CTRL_TYPE_EFRAME      0x5

#define KVASER_PCIEFD_KCAN_CMD_SEQ_MASK          GENMASK(23, 16)
#define KVASER_PCIEFD_KCAN_CMD_MASK              GENMASK(5, 0)
#define KVASER_PCIEFD_KCAN_CMD_AT                BIT(1)
#define KVASER_PCIEFD_KCAN_CMD_SRQ               BIT(0)

#define KVASER_PCIEFD_KCAN_IOC_LED               BIT(0)

#define KVASER_PCIEFD_KCAN_IRQ_TAL               BIT(17)
#define KVASER_PCIEFD_KCAN_IRQ_TE                BIT(16)
#define KVASER_PCIEFD_KCAN_IRQ_TOF               BIT(15)
#define KVASER_PCIEFD_KCAN_IRQ_TFD               BIT(14)
#define KVASER_PCIEFD_KCAN_IRQ_ABD               BIT(13)
#define KVASER_PCIEFD_KCAN_IRQ_ROF               BIT(5)
#define KVASER_PCIEFD_KCAN_IRQ_FDIC              BIT(3)
#define KVASER_PCIEFD_KCAN_IRQ_BPP               BIT(2)
#define KVASER_PCIEFD_KCAN_IRQ_TAE               BIT(1)
#define KVASER_PCIEFD_KCAN_IRQ_TAR               BIT(0)

#define KVASER_PCIEFD_KCAN_TX_NR_PACKETS_MAX_MASK     GENMASK(23, 16)
#define KVASER_PCIEFD_KCAN_TX_NR_PACKETS_CURRENT_MASK GENMASK(7, 0)

#define KVASER_PCIEFD_KCAN_STAT_SEQNO_MASK       GENMASK(31, 24)
#define KVASER_PCIEFD_KCAN_STAT_FD               BIT(19)
#define KVASER_PCIEFD_KCAN_STAT_CAP              BIT(16)
#define KVASER_PCIEFD_KCAN_STAT_IRM              BIT(15)
#define KVASER_PCIEFD_KCAN_STAT_RMR              BIT(14)
#define KVASER_PCIEFD_KCAN_STAT_BOFF             BIT(11)
#define KVASER_PCIEFD_KCAN_STAT_IDLE             BIT(10)
#define KVASER_PCIEFD_KCAN_STAT_AR               BIT(7)
#define KVASER_PCIEFD_KCAN_STAT_BUS_OFF_MASK \
    (KVASER_PCIEFD_KCAN_STAT_AR | KVASER_PCIEFD_KCAN_STAT_BOFF | \
     KVASER_PCIEFD_KCAN_STAT_RMR | KVASER_PCIEFD_KCAN_STAT_IRM)

#define KVASER_PCIEFD_KCAN_MODE_CCM              BIT(31)
#define KVASER_PCIEFD_KCAN_MODE_EEN              BIT(23)
#define KVASER_PCIEFD_KCAN_MODE_APT              BIT(20)
#define KVASER_PCIEFD_KCAN_MODE_NIFDEN           BIT(15)
#define KVASER_PCIEFD_KCAN_MODE_EPEN             BIT(12)
#define KVASER_PCIEFD_KCAN_MODE_LOM              BIT(9)
#define KVASER_PCIEFD_KCAN_MODE_RM               BIT(8)

#define KVASER_PCIEFD_KCAN_BTRN_TSEG2_MASK       GENMASK(30, 26)
#define KVASER_PCIEFD_KCAN_BTRN_TSEG1_MASK       GENMASK(25, 17)
#define KVASER_PCIEFD_KCAN_BTRN_SJW_MASK         GENMASK(16, 13)
#define KVASER_PCIEFD_KCAN_BTRN_BRP_MASK         GENMASK(12, 0)

#define KVASER_PCIEFD_KCAN_PWM_TOP_MASK          GENMASK(23, 16)
#define KVASER_PCIEFD_KCAN_PWM_TRIGGER_MASK      GENMASK(7, 0)

#define KVASER_PCIEFD_PACK_TYPE_DATA             0x0
#define KVASER_PCIEFD_PACK_TYPE_ACK              0x1
#define KVASER_PCIEFD_PACK_TYPE_TXRQ             0x2
#define KVASER_PCIEFD_PACK_TYPE_ERROR            0x3
#define KVASER_PCIEFD_PACK_TYPE_EFLUSH_ACK       0x4
#define KVASER_PCIEFD_PACK_TYPE_EFRAME_ACK       0x5
#define KVASER_PCIEFD_PACK_TYPE_ACK_DATA         0x6
#define KVASER_PCIEFD_PACK_TYPE_STATUS           0x8
#define KVASER_PCIEFD_PACK_TYPE_BUS_LOAD         0x9

#define KVASER_PCIEFD_PACKET_TYPE_MASK           GENMASK(31, 28)
#define KVASER_PCIEFD_PACKET_CHID_MASK           GENMASK(27, 25)
#define KVASER_PCIEFD_PACKET_SEQ_MASK            GENMASK(7, 0)

#define KVASER_PCIEFD_RPACKET_IDE                BIT(30)
#define KVASER_PCIEFD_RPACKET_RTR                BIT(29)
#define KVASER_PCIEFD_RPACKET_ID_MASK            GENMASK(28, 0)

#define KVASER_PCIEFD_TPACKET_AREQ               BIT(31)
#define KVASER_PCIEFD_TPACKET_SMS                BIT(16)

#define KVASER_PCIEFD_RPACKET_FDF                BIT(15)
#define KVASER_PCIEFD_RPACKET_BRS                BIT(14)
#define KVASER_PCIEFD_RPACKET_ESI                BIT(13)
#define KVASER_PCIEFD_RPACKET_DLC_MASK           GENMASK(11, 8)

#define KVASER_PCIEFD_APACKET_NACK               BIT(11)
#define KVASER_PCIEFD_APACKET_ABL                BIT(10)
#define KVASER_PCIEFD_APACKET_CT                 BIT(9)
#define KVASER_PCIEFD_APACKET_FLU                BIT(8)

#define KVASER_PCIEFD_SPACK_RMCD                 BIT(22)
#define KVASER_PCIEFD_SPACK_IRM                  BIT(21)
#define KVASER_PCIEFD_SPACK_IDET                 BIT(20)
#define KVASER_PCIEFD_SPACK_BOFF                 BIT(16)
#define KVASER_PCIEFD_SPACK_RXERR_MASK           GENMASK(15, 8)
#define KVASER_PCIEFD_SPACK_TXERR_MASK           GENMASK(7, 0)
#define KVASER_PCIEFD_SPACK_EPLR                 BIT(24)
#define KVASER_PCIEFD_SPACK_EWLR                 BIT(23)
#define KVASER_PCIEFD_SPACK_AUTO                 BIT(21)

#define KVASER_PCIEFD_EPACK_DIR_TX               BIT(0)

#define KVASER_PCIEFD_CAN_TX_MAX_COUNT           17U
#define KVASER_PCIEFD_DMA_SIZE                   (4U * 1024U)
#define KVASER_PCIEFD_DMA_COUNT                  2U
#define KVASER_PCIEFD_MAX_CAN_CHANNELS           8UL

#define KVASER_PCIEFD_ALTERA_DMA_64BIT           BIT(0)
#define KVASER_PCIEFD_SF2_DMA_LSB_MASK           GENMASK(31, 12)
#define KVASER_PCIEFD_XILINX_DMA_LSB_MASK        GENMASK(31, 12)

struct kvaser_pciefd_address_offset {
    uint32_t serdes;
    uint32_t pci_ien;
    uint32_t pci_irq;
    uint32_t sysid;
    uint32_t loopback;
    uint32_t kcan_srb_fifo;
    uint32_t kcan_srb;
    uint32_t kcan_ch0;
    uint32_t kcan_ch1;
};

struct kvaser_pciefd_irq_mask {
    uint32_t kcan_rx0;
    uint32_t kcan_tx[KVASER_PCIEFD_MAX_CAN_CHANNELS];
    uint32_t all;
};

struct kvaser_pciefd_dev_ops {
    void (*kvaser_pciefd_write_dma_map)(void *pcie, dma_addr_t addr, int index);
};

struct kvaser_pciefd_driver_data {
    const struct kvaser_pciefd_address_offset *address_offset;
    const struct kvaser_pciefd_irq_mask *irq_mask;
    const struct kvaser_pciefd_dev_ops *ops;
};

static const struct kvaser_pciefd_address_offset kvaser_pciefd_altera_address_offset = {
    .serdes = 0x1000,
    .pci_ien = 0x50,
    .pci_irq = 0x40,
    .sysid = 0x1f020,
    .loopback = 0x1f000,
    .kcan_srb_fifo = 0x1f200,
    .kcan_srb = 0x1f400,
    .kcan_ch0 = 0x10000,
    .kcan_ch1 = 0x11000,
};

static const struct kvaser_pciefd_address_offset kvaser_pciefd_sf2_address_offset = {
    .serdes = 0x280c8,
    .pci_ien = 0x102004,
    .pci_irq = 0x102008,
    .sysid = 0x100000,
    .loopback = 0x103000,
    .kcan_srb_fifo = 0x120000,
    .kcan_srb = 0x121000,
    .kcan_ch0 = 0x140000,
    .kcan_ch1 = 0x142000,
};

static const struct kvaser_pciefd_address_offset kvaser_pciefd_xilinx_address_offset = {
    .serdes = 0x00208,
    .pci_ien = 0x102004,
    .pci_irq = 0x102008,
    .sysid = 0x100000,
    .loopback = 0x103000,
    .kcan_srb_fifo = 0x120000,
    .kcan_srb = 0x121000,
    .kcan_ch0 = 0x140000,
    .kcan_ch1 = 0x142000,
};

static const struct kvaser_pciefd_irq_mask kvaser_pciefd_altera_irq_mask = {
    .kcan_rx0 = BIT(4),
    .kcan_tx = { BIT(0), BIT(1), BIT(2), BIT(3) },
    .all = GENMASK(4, 0),
};

static const struct kvaser_pciefd_irq_mask kvaser_pciefd_sf2_irq_mask = {
    .kcan_rx0 = BIT(4),
    .kcan_tx = { BIT(16), BIT(17), BIT(18), BIT(19) },
    .all = GENMASK(19, 16) | BIT(4),
};

static const struct kvaser_pciefd_irq_mask kvaser_pciefd_xilinx_irq_mask = {
    .kcan_rx0 = BIT(4),
    .kcan_tx = { BIT(16), BIT(17), BIT(18), BIT(19), BIT(20), BIT(21), BIT(22), BIT(23) },
    .all = GENMASK(23, 16) | BIT(4),
};

static const struct kvaser_pciefd_dev_ops kvaser_pciefd_altera_dev_ops = {
    .kvaser_pciefd_write_dma_map = NULL,
};

static const struct kvaser_pciefd_dev_ops kvaser_pciefd_sf2_dev_ops = {
    .kvaser_pciefd_write_dma_map = NULL,
};

static const struct kvaser_pciefd_dev_ops kvaser_pciefd_xilinx_dev_ops = {
    .kvaser_pciefd_write_dma_map = NULL,
};

static const struct kvaser_pciefd_driver_data kvaser_pciefd_altera_driver_data = {
    .address_offset = &kvaser_pciefd_altera_address_offset,
    .irq_mask = &kvaser_pciefd_altera_irq_mask,
    .ops = &kvaser_pciefd_altera_dev_ops,
};

static const struct kvaser_pciefd_driver_data kvaser_pciefd_sf2_driver_data __attribute__((unused)) = {
    .address_offset = &kvaser_pciefd_sf2_address_offset,
    .irq_mask = &kvaser_pciefd_sf2_irq_mask,
    .ops = &kvaser_pciefd_sf2_dev_ops,
};

static const struct kvaser_pciefd_driver_data kvaser_pciefd_xilinx_driver_data __attribute__((unused)) = {
    .address_offset = &kvaser_pciefd_xilinx_address_offset,
    .irq_mask = &kvaser_pciefd_xilinx_irq_mask,
    .ops = &kvaser_pciefd_xilinx_dev_ops,
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

    struct kvaser_pciefd_address_offset addr_off;

    uint32_t status_flags;
    uint32_t reset_state;
    uint8_t pm_state;
    const struct kvaser_pciefd_driver_data *driver_data;

    /* Shadow register space for BAR0 (1 MiB) */
    uint32_t *regs;
};

static inline uint32_t pciefd_reg_read32(PCIBaseState *s, hwaddr addr)
{
    if (addr >= (1 * MiB)) {
        return 0;
    }
    return s->regs[addr >> 2];
}

static inline void pciefd_reg_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr >= (1 * MiB)) {
        return;
    }
    s->regs[addr >> 2] = val;
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        return 0;
    }

    /* Adjust for profile offsets where relevant will be done by the guest.
     * Our BAR0 view is flat; guest adds addr_off.* to compute addresses.
     */

    val = pciefd_reg_read32(s, addr);

    /* Implement minimal behavior for specific registers used during probe */

    /* SYSID region: report 1 channel, FD capable, version/build arbitrary */
    if (addr == s->addr_off.sysid + KVASER_PCIEFD_SYSID_VERSION_REG) {
        /* nr_channels in bits 31:24, major in 23:16, minor in 7:0 */
        uint32_t version = (1U << 24) | (1U << 16) | 1U;
        val = version;
    } else if (addr == s->addr_off.sysid + KVASER_PCIEFD_SYSID_BUILD_REG) {
        uint32_t build = (1U << 1);
        val = build;
    } else if (addr == s->addr_off.sysid + KVASER_PCIEFD_SYSID_BUSFREQ_REG) {
        /* Arbitrary non-zero bus frequency */
        val = 100000000U; /* 100 MHz */
    } else if (addr == s->addr_off.sysid + KVASER_PCIEFD_SYSID_CANFREQ_REG) {
        /* Arbitrary non-zero CAN frequency */
        val = 80000000U; /* 80 MHz */
    }

    /* SRB status: report DMA present and idle */
    if (addr == s->addr_off.kcan_srb + KVASER_PCIEFD_SRB_STAT_REG) {
        val = KVASER_PCIEFD_SRB_STAT_DMA | KVASER_PCIEFD_SRB_STAT_DI;
    }

    /* KCAN controller registers for channel 0 (only one channel emulated) */
    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_STAT_REG) {
        /* FD and CAP bits set so driver accepts FD and ONE_SHOT */
        val = KVASER_PCIEFD_KCAN_STAT_FD | KVASER_PCIEFD_KCAN_STAT_CAP |
              KVASER_PCIEFD_KCAN_STAT_IDLE;
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_IOC_REG) {
        /* Return stored IOC shadow */
        val = pciefd_reg_read32(s, addr);
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_TX_NR_PACKETS_REG) {
        /* Return max packets > KVASER_PCIEFD_CAN_TX_MAX_COUNT */
        uint32_t max_count = (KVASER_PCIEFD_CAN_TX_MAX_COUNT + 2) << 16;
        val = max_count;
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_MODE_REG) {
        val = pciefd_reg_read32(s, addr);
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_IRQ_REG) {
        /* Return any pending KCAN IRQ status as stored in shadow */
        val = pciefd_reg_read32(s, addr);
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_TX_NR_PACKETS_REG) {
        val = pciefd_reg_read32(s, addr);
    }

    if (addr == s->addr_off.pci_irq) {
        /* PCI IRQ status is stored in intr_status */
        val = s->intr_status;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val64, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = (uint32_t)val64;

    if (size != 4) {
        return;
    }

    pciefd_reg_write32(s, addr, val);

    /* Handle SYSID/loopback: loopback write is just stored */
    if (addr == s->addr_off.loopback) {
        /* no-op beyond shadow write */
    }

    /* SRB control/command/IRQ/IEN handling */
    if (addr == s->addr_off.kcan_srb + KVASER_PCIEFD_SRB_CTRL_REG) {
        /* enable/disable DMA is not modeled beyond shadow */
    }

    if (addr == s->addr_off.kcan_srb + KVASER_PCIEFD_SRB_CMD_REG) {
        /* FOR/RDB0/RDB1 just acked in shadow */
    }

    if (addr == s->addr_off.kcan_srb + KVASER_PCIEFD_SRB_IRQ_REG) {
        /* W1C on SRB IRQ shadow */
        uint32_t cur = s->intr_status;
        cur &= ~val;
        s->intr_status = cur;
        pciefd_reg_write32(s, addr, cur);
        pcibase_update_irq(s);
    }

    if (addr == s->addr_off.kcan_srb + KVASER_PCIEFD_SRB_IEN_REG) {
        /* Store SRB interrupt enable mask in intr_mask lower bits */
        s->intr_mask = val;
        pcibase_update_irq(s);
    }

    /* PCI interrupt mask/ack */
    if (addr == s->addr_off.pci_ien) {
        s->intr_mask = val;
        pcibase_update_irq(s);
    }

    if (addr == s->addr_off.pci_irq) {
        /* W1C for PCI IRQ status */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
    }

    /* Channel 0 KCAN region */
    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_IEN_REG) {
        /* store per-channel mask in shadow */
        pciefd_reg_write32(s, addr, val);
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_IRQ_REG) {
        /* W1C for KCAN IRQ register */
        uint32_t cur = pciefd_reg_read32(s, addr);
        cur &= ~val;
        pciefd_reg_write32(s, addr, cur);
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_MODE_REG) {
        /* store mode */
        pciefd_reg_write32(s, addr, val);
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_IOC_REG) {
        /* IOC shadow (LED etc.) */
        pciefd_reg_write32(s, addr, val);
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_BUS_LOAD_REG) {
        /* disable bus load reporting; ignore */
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_BTRN_REG ||
        addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_BTRD_REG) {
        /* store bittiming registers */
        pciefd_reg_write32(s, addr, val);
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_CMD_REG) {
        /* command writes: only affect internal shadows in driver; no side effects */
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_CTRL_REG) {
        /* control register; used for EFLUSH etc. */
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_PWM_REG) {
        /* PWM control is only used for LED blinking; keep shadow */
    }

    if (addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_FIFO_REG ||
        addr == s->addr_off.kcan_ch0 + KVASER_PCIEFD_KCAN_FIFO_LAST_REG) {
        /* TX FIFO writes: we do not model actual CAN traffic. For now, we
         * simply ignore, but could in future generate a fake ACK IRQ.
         */
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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

    s->intr_status = 0;
    s->intr_mask = 0;
    s->status_flags = 0;
    s->reset_state = 0;

    if (s->regs) {
        memset(s->regs, 0, 1 * MiB);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  KVASER_PCIEFD_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  KVASER_PCIEFD_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, KVASER_PCIEFD_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 1 * MiB;
    s->bar_info[0].name = "kvaser-pciefd-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    /* allocate shadow register array */
    s->regs = g_malloc0(1 * MiB);

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->driver_data = &kvaser_pciefd_altera_driver_data;
    s->addr_off = *s->driver_data->address_offset;

    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    if (s->regs) {
        g_free(s->regs);
        s->regs = NULL;
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "kvaser_pciefd_pci",
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

