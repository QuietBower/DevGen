/* This template provides a robust skeleton for hardware emulation.
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
#include "qemu/bitops.h"
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

#define TYPE_PCIBASE_DEVICE "kvaser_pciefd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define KVASER_PCIEFD_VENDOR 0x1a07
#define KVASER_PCIEFD_4HS_DEVICE_ID 0x000d
#define KVASER_PCIEFD_2HS_V2_DEVICE_ID 0x000e
#define KVASER_PCIEFD_HS_V2_DEVICE_ID 0x000f
#define KVASER_PCIEFD_MINIPCIE_HS_V2_DEVICE_ID 0x0010
#define KVASER_PCIEFD_MINIPCIE_2HS_V2_DEVICE_ID 0x0011
#define KVASER_PCIEFD_2CAN_V3_DEVICE_ID 0x0012
#define KVASER_PCIEFD_1CAN_V3_DEVICE_ID 0x0013
#define KVASER_PCIEFD_4CAN_V2_DEVICE_ID 0x0014
#define KVASER_PCIEFD_MINIPCIE_2CAN_V3_DEVICE_ID 0x0015
#define KVASER_PCIEFD_MINIPCIE_1CAN_V3_DEVICE_ID 0x0016
#define KVASER_PCIEFD_M2_4CAN_DEVICE_ID 0x0017
#define KVASER_PCIEFD_8CAN_DEVICE_ID 0x0019
#define KVASER_PCIEFD_ALTERA_DMA_64BIT BIT(0)
#define KVASER_PCIEFD_SF2_DMA_LSB_MASK GENMASK(31, 12)
#define KVASER_PCIEFD_XILINX_DMA_LSB_MASK GENMASK(31, 12)
#define KVASER_PCIEFD_KCAN_FIFO_REG 0x100
#define KVASER_PCIEFD_KCAN_FIFO_LAST_REG 0x180
#define KVASER_PCIEFD_KCAN_CTRL_REG 0x2c0
#define KVASER_PCIEFD_KCAN_CMD_REG 0x400
#define KVASER_PCIEFD_KCAN_IOC_REG 0x404
#define KVASER_PCIEFD_KCAN_IEN_REG 0x408
#define KVASER_PCIEFD_KCAN_IRQ_REG 0x410
#define KVASER_PCIEFD_KCAN_TX_NR_PACKETS_REG 0x414
#define KVASER_PCIEFD_KCAN_STAT_REG 0x418
#define KVASER_PCIEFD_KCAN_MODE_REG 0x41c
#define KVASER_PCIEFD_KCAN_BTRN_REG 0x420
#define KVASER_PCIEFD_KCAN_BUS_LOAD_REG 0x424
#define KVASER_PCIEFD_KCAN_BTRD_REG 0x428
#define KVASER_PCIEFD_KCAN_PWM_REG 0x430
#define KVASER_PCIEFD_SYSID_VERSION_REG 0x8
#define KVASER_PCIEFD_SYSID_CANFREQ_REG 0xc
#define KVASER_PCIEFD_SYSID_BUSFREQ_REG 0x10
#define KVASER_PCIEFD_SYSID_BUILD_REG 0x14
#define KVASER_PCIEFD_SRB_FIFO_LAST_REG 0x1f4
#define KVASER_PCIEFD_SRB_CMD_REG 0x0
#define KVASER_PCIEFD_SRB_IEN_REG 0x04
#define KVASER_PCIEFD_SRB_IRQ_REG 0x0c
#define KVASER_PCIEFD_SRB_STAT_REG 0x10
#define KVASER_PCIEFD_SRB_RX_NR_PACKETS_REG 0x14
#define KVASER_PCIEFD_SRB_CTRL_REG 0x18
#define KVASER_PCIEFD_SYSID_VERSION_NR_CHAN_MASK GENMASK(31, 24)
#define KVASER_PCIEFD_SYSID_VERSION_MAJOR_MASK GENMASK(23, 16)
#define KVASER_PCIEFD_SYSID_VERSION_MINOR_MASK GENMASK(7, 0)
#define KVASER_PCIEFD_SYSID_BUILD_SEQ_MASK GENMASK(15, 1)
#define KVASER_PCIEFD_SRB_CMD_RDB1 BIT(5)
#define KVASER_PCIEFD_SRB_CMD_RDB0 BIT(4)
#define KVASER_PCIEFD_SRB_CMD_FOR BIT(0)
#define KVASER_PCIEFD_SRB_IRQ_DUF1 BIT(13)
#define KVASER_PCIEFD_SRB_IRQ_DUF0 BIT(12)
#define KVASER_PCIEFD_SRB_IRQ_DOF1 BIT(11)
#define KVASER_PCIEFD_SRB_IRQ_DOF0 BIT(10)
#define KVASER_PCIEFD_SRB_IRQ_DPD1 BIT(9)
#define KVASER_PCIEFD_SRB_IRQ_DPD0 BIT(8)
#define KVASER_PCIEFD_SRB_STAT_DMA BIT(24)
#define KVASER_PCIEFD_SRB_STAT_DI BIT(15)
#define KVASER_PCIEFD_SRB_RX_NR_PACKETS_MASK GENMASK(7, 0)
#define KVASER_PCIEFD_SRB_CTRL_DMA_ENABLE BIT(0)
#define KVASER_PCIEFD_KCAN_CTRL_TYPE_MASK GENMASK(31, 29)
#define KVASER_PCIEFD_KCAN_CTRL_TYPE_EFLUSH 0x4
#define KVASER_PCIEFD_KCAN_CTRL_TYPE_EFRAME 0x5
#define KVASER_PCIEFD_KCAN_CMD_SEQ_MASK GENMASK(23, 16)
#define KVASER_PCIEFD_KCAN_CMD_MASK GENMASK(5, 0)
#define KVASER_PCIEFD_KCAN_CMD_AT BIT(1)
#define KVASER_PCIEFD_KCAN_CMD_SRQ BIT(0)
#define KVASER_PCIEFD_KCAN_IOC_LED BIT(0)
#define KVASER_PCIEFD_KCAN_IRQ_TAL BIT(17)
#define KVASER_PCIEFD_KCAN_IRQ_TE BIT(16)
#define KVASER_PCIEFD_KCAN_IRQ_TOF BIT(15)
#define KVASER_PCIEFD_KCAN_IRQ_TFD BIT(14)
#define KVASER_PCIEFD_KCAN_IRQ_ABD BIT(13)
#define KVASER_PCIEFD_KCAN_IRQ_ROF BIT(5)
#define KVASER_PCIEFD_KCAN_IRQ_FDIC BIT(3)
#define KVASER_PCIEFD_KCAN_IRQ_BPP BIT(2)
#define KVASER_PCIEFD_KCAN_IRQ_TAE BIT(1)
#define KVASER_PCIEFD_KCAN_IRQ_TAR BIT(0)
#define KVASER_PCIEFD_KCAN_TX_NR_PACKETS_MAX_MASK GENMASK(23, 16)
#define KVASER_PCIEFD_KCAN_TX_NR_PACKETS_CURRENT_MASK GENMASK(7, 0)
#define KVASER_PCIEFD_KCAN_STAT_SEQNO_MASK GENMASK(31, 24)
#define KVASER_PCIEFD_KCAN_STAT_FD BIT(19)
#define KVASER_PCIEFD_KCAN_STAT_CAP BIT(16)
#define KVASER_PCIEFD_KCAN_STAT_IRM BIT(15)
#define KVASER_PCIEFD_KCAN_STAT_RMR BIT(14)
#define KVASER_PCIEFD_KCAN_STAT_BOFF BIT(11)
#define KVASER_PCIEFD_KCAN_STAT_IDLE BIT(10)
#define KVASER_PCIEFD_KCAN_STAT_AR BIT(7)
#define KVASER_PCIEFD_KCAN_STAT_BUS_OFF_MASK \
	(KVASER_PCIEFD_KCAN_STAT_AR | KVASER_PCIEFD_KCAN_STAT_BOFF | \
	 KVASER_PCIEFD_KCAN_STAT_RMR | KVASER_PCIEFD_KCAN_STAT_IRM)
#define KVASER_PCIEFD_KCAN_MODE_CCM BIT(31)
#define KVASER_PCIEFD_KCAN_MODE_EEN BIT(23)
#define KVASER_PCIEFD_KCAN_MODE_APT BIT(20)
#define KVASER_PCIEFD_KCAN_MODE_NIFDEN BIT(15)
#define KVASER_PCIEFD_KCAN_MODE_EPEN BIT(12)
#define KVASER_PCIEFD_KCAN_MODE_LOM BIT(9)
#define KVASER_PCIEFD_KCAN_MODE_RM BIT(8)
#define KVASER_PCIEFD_KCAN_BTRN_TSEG2_MASK GENMASK(30, 26)
#define KVASER_PCIEFD_KCAN_BTRN_TSEG1_MASK GENMASK(25, 17)
#define KVASER_PCIEFD_KCAN_BTRN_SJW_MASK GENMASK(16, 13)
#define KVASER_PCIEFD_KCAN_BTRN_BRP_MASK GENMASK(12, 0)
#define KVASER_PCIEFD_KCAN_PWM_TOP_MASK GENMASK(23, 16)
#define KVASER_PCIEFD_KCAN_PWM_TRIGGER_MASK GENMASK(7, 0)
#define KVASER_PCIEFD_PACK_TYPE_DATA 0x0
#define KVASER_PCIEFD_PACK_TYPE_ACK 0x1
#define KVASER_PCIEFD_PACK_TYPE_TXRQ 0x2
#define KVASER_PCIEFD_PACK_TYPE_ERROR 0x3
#define KVASER_PCIEFD_PACK_TYPE_EFLUSH_ACK 0x4
#define KVASER_PCIEFD_PACK_TYPE_EFRAME_ACK 0x5
#define KVASER_PCIEFD_PACK_TYPE_ACK_DATA 0x6
#define KVASER_PCIEFD_PACK_TYPE_STATUS 0x8
#define KVASER_PCIEFD_PACK_TYPE_BUS_LOAD 0x9
#define KVASER_PCIEFD_PACKET_TYPE_MASK GENMASK(31, 28)
#define KVASER_PCIEFD_PACKET_CHID_MASK GENMASK(27, 25)
#define KVASER_PCIEFD_PACKET_SEQ_MASK GENMASK(7, 0)
#define KVASER_PCIEFD_RPACKET_IDE BIT(30)
#define KVASER_PCIEFD_RPACKET_RTR BIT(29)
#define KVASER_PCIEFD_RPACKET_ID_MASK GENMASK(28, 0)
#define KVASER_PCIEFD_TPACKET_AREQ BIT(31)
#define KVASER_PCIEFD_TPACKET_SMS BIT(16)
#define KVASER_PCIEFD_RPACKET_FDF BIT(15)
#define KVASER_PCIEFD_RPACKET_BRS BIT(14)
#define KVASER_PCIEFD_RPACKET_ESI BIT(13)
#define KVASER_PCIEFD_RPACKET_DLC_MASK GENMASK(11, 8)
#define KVASER_PCIEFD_APACKET_NACK BIT(11)
#define KVASER_PCIEFD_APACKET_ABL BIT(10)
#define KVASER_PCIEFD_APACKET_CT BIT(9)
#define KVASER_PCIEFD_APACKET_FLU BIT(8)
#define KVASER_PCIEFD_SPACK_RMCD BIT(22)
#define KVASER_PCIEFD_SPACK_IRM BIT(21)
#define KVASER_PCIEFD_SPACK_IDET BIT(20)
#define KVASER_PCIEFD_SPACK_BOFF BIT(16)
#define KVASER_PCIEFD_SPACK_RXERR_MASK GENMASK(15, 8)
#define KVASER_PCIEFD_SPACK_TXERR_MASK GENMASK(7, 0)
#define KVASER_PCIEFD_SPACK_EPLR BIT(24)
#define KVASER_PCIEFD_SPACK_EWLR BIT(23)
#define KVASER_PCIEFD_SPACK_AUTO BIT(21)
#define KVASER_PCIEFD_EPACK_DIR_TX BIT(0)
#define KVASER_PCIEFD_GET_BLOCK_ADDR(pcie, block) \
	((pcie)->reg_base + (pcie)->driver_data->address_offset->block)
#define KVASER_PCIEFD_PCI_IEN_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), pci_ien))
#define KVASER_PCIEFD_PCI_IRQ_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), pci_irq))
#define KVASER_PCIEFD_SERDES_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), serdes))
#define KVASER_PCIEFD_SYSID_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), sysid))
#define KVASER_PCIEFD_LOOPBACK_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), loopback))
#define KVASER_PCIEFD_SRB_FIFO_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), kcan_srb_fifo))
#define KVASER_PCIEFD_SRB_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), kcan_srb))
#define KVASER_PCIEFD_KCAN_CH0_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), kcan_ch0))
#define KVASER_PCIEFD_KCAN_CH1_ADDR(pcie) \
	(KVASER_PCIEFD_GET_BLOCK_ADDR((pcie), kcan_ch1))
#define KVASER_PCIEFD_KCAN_CHANNEL_SPAN(pcie) \
	(KVASER_PCIEFD_KCAN_CH1_ADDR((pcie)) - KVASER_PCIEFD_KCAN_CH0_ADDR((pcie)))
#define KVASER_PCIEFD_KCAN_CHX_ADDR(pcie, i) \
	(KVASER_PCIEFD_KCAN_CH0_ADDR((pcie)) + (i) * KVASER_PCIEFD_KCAN_CHANNEL_SPAN((pcie)))
#define KVASER_PCIEFD_CAN_TX_MAX_COUNT 17U
#define KVASER_PCIEFD_DMA_COUNT 2U
#define KVASER_PCIEFD_DMA_SIZE (4U * 1024U)
#define KVASER_PCIEFD_MAX_CAN_CHANNELS 8UL

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
    uint32_t pci_ien;
    uint32_t pci_irq;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t sysid_version;
    uint32_t sysid_canfreq;
    uint32_t sysid_busfreq;
    uint32_t sysid_build;

    /* DMA Context */
    dma_addr_t dma_addr[2];
    uint32_t srb_ctrl;

    /* Additional state */
    int nr_channels;
    struct kcan_channel_state {
        uint32_t fifo;
        uint32_t ctrl;
        uint32_t ioc;
        uint32_t ien;
        uint32_t irq;
        uint32_t tx_nr_packets;
        uint32_t stat;
        uint32_t mode;
        uint32_t btrn;
        uint32_t bus_load;
        uint32_t btrd;
        uint32_t pwm;
    } can[KVASER_PCIEFD_MAX_CAN_CHANNELS];

    /* SRB registers */
    uint32_t srb_cmd;
    uint32_t srb_ien;
    uint32_t srb_irq;
    uint32_t srb_stat;
    uint32_t srb_rx_nr_packets;

    uint32_t serdes_word1[2];
    uint32_t serdes_word2[2];
    uint32_t loopback;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t irq = 0;

    /* kcan_rx0 bit (4) set if any SRB interrupt enabled and pending */
    if (s->srb_ien & s->srb_irq) {
        irq |= BIT(4);
    }

    /* for each channel, if (ien & irq) -> set corresponding tx bit */
    for (int i = 0; i < s->nr_channels; i++) {
        if (s->can[i].ien & s->can[i].irq) {
            irq |= BIT(i);  /* altera: channel 0->BIT(0), 1->BIT(1), etc. */
        }
    }

    s->pci_irq = irq & 0x1F;

    /* Assert or deassert interrupt line based on pci_ien mask */
    if (s->pci_ien & s->pci_irq) {
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
    PCIDevice *pdev = PCI_DEVICE(s);
    /* No explicit DMA transfers required for probe; placeholder removed */
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;
    uint64_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    if (addr == 0x40) { /* pci_irq */
        pcibase_update_irq(s);
        val = s->pci_irq;
    } else if (addr == 0x50) { /* pci_ien */
        val = s->pci_ien;
    } else if (addr >= 0x1000 && addr < 0x1010) { /* serdes region */
        int index = (addr - 0x1000) / 8;
        int offset_in_serdes = (addr - 0x1000) % 8;
        if (index < 2) {
            if (offset_in_serdes == 0) {
                val = s->serdes_word1[index];
            } else if (offset_in_serdes == 4) {
                val = s->serdes_word2[index];
            }
        }
    } else if (addr == 0x1f000) { /* loopback */
        val = s->loopback;
    } else if (addr == 0x1f028) { /* sysid_version */
        val = s->sysid_version;
    } else if (addr == 0x1f02c) { /* sysid_canfreq */
        val = s->sysid_canfreq;
    } else if (addr == 0x1f030) { /* sysid_busfreq */
        val = s->sysid_busfreq;
    } else if (addr == 0x1f034) { /* sysid_build */
        val = s->sysid_build;
    } else if (addr >= 0x1f200 && addr < 0x1f400) { /* SRB FIFO region */
        val = 0;
    } else if (addr == 0x1f400) { /* srb_cmd */
        val = s->srb_cmd;
    } else if (addr == 0x1f404) { /* srb_ien */
        val = s->srb_ien;
    } else if (addr == 0x1f40c) { /* srb_irq */
        val = s->srb_irq;
    } else if (addr == 0x1f410) { /* srb_stat */
        val = s->srb_stat;
    } else if (addr == 0x1f414) { /* srb_rx_nr_packets */
        val = s->srb_rx_nr_packets;
    } else if (addr == 0x1f418) { /* srb_ctrl */
        val = s->srb_ctrl;
    } else if (addr >= 0x10000 && addr < 0x10000 + KVASER_PCIEFD_MAX_CAN_CHANNELS * 0x1000) {
        int chan = (addr - 0x10000) / 0x1000;
        hwaddr chan_offset = (addr - 0x10000) % 0x1000;
        if (chan >= s->nr_channels) {
            return ~0ULL;
        }
        switch (chan_offset) {
            case KVASER_PCIEFD_KCAN_FIFO_REG: /* 0x100 */
                val = s->can[chan].fifo;
                break;
            case KVASER_PCIEFD_KCAN_FIFO_LAST_REG: /* 0x180 */
                val = 0;
                break;
            case KVASER_PCIEFD_KCAN_CTRL_REG: /* 0x2c0 */
                val = s->can[chan].ctrl;
                break;
            case KVASER_PCIEFD_KCAN_CMD_REG: /* 0x400 */
                val = 0;
                break;
            case KVASER_PCIEFD_KCAN_IOC_REG: /* 0x404 */
                val = s->can[chan].ioc;
                break;
            case KVASER_PCIEFD_KCAN_IEN_REG: /* 0x408 */
                val = s->can[chan].ien;
                break;
            case KVASER_PCIEFD_KCAN_IRQ_REG: /* 0x410 */
                val = s->can[chan].irq;
                break;
            case KVASER_PCIEFD_KCAN_TX_NR_PACKETS_REG: /* 0x414 */
                val = s->can[chan].tx_nr_packets;
                break;
            case KVASER_PCIEFD_KCAN_STAT_REG: /* 0x418 */
                val = s->can[chan].stat;
                break;
            case KVASER_PCIEFD_KCAN_MODE_REG: /* 0x41c */
                val = s->can[chan].mode;
                break;
            case KVASER_PCIEFD_KCAN_BTRN_REG: /* 0x420 */
                val = s->can[chan].btrn;
                break;
            case KVASER_PCIEFD_KCAN_BUS_LOAD_REG: /* 0x424 */
                val = s->can[chan].bus_load;
                break;
            case KVASER_PCIEFD_KCAN_BTRD_REG: /* 0x428 */
                val = s->can[chan].btrd;
                break;
            case KVASER_PCIEFD_KCAN_PWM_REG: /* 0x430 */
                val = s->can[chan].pwm;
                break;
            default:
                val = 0;
                break;
        }
    } else {
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;

    if (size != 4) {
        return;
    }

    if (addr == 0x40) { /* pci_irq - read-only, ignore writes */
    } else if (addr == 0x50) { /* pci_ien */
        s->pci_ien = val;
        pcibase_update_irq(s);
    } else if (addr >= 0x1000 && addr < 0x1010) { /* serdes */
        int index = (addr - 0x1000) / 8;
        int offset_in_serdes = (addr - 0x1000) % 8;
        if (index < 2) {
            if (offset_in_serdes == 0) {
                s->serdes_word1[index] = val;
            } else if (offset_in_serdes == 4) {
                s->serdes_word2[index] = val;
            }
        }
    } else if (addr == 0x1f000) { /* loopback */
        s->loopback = val;
    } else if (addr == 0x1f028) { /* sysid_version - read-only */
    } else if (addr == 0x1f02c) { /* canfreq - read-only */
    } else if (addr == 0x1f030) { /* busfreq - read-only */
    } else if (addr == 0x1f034) { /* build - read-only */
    } else if (addr >= 0x1f200 && addr < 0x1f400) { /* SRB FIFO - writes ignored */
    } else if (addr == 0x1f400) { /* srb_cmd */
        s->srb_cmd = val;
        if (val & (KVASER_PCIEFD_SRB_CMD_FOR | KVASER_PCIEFD_SRB_CMD_RDB0 | KVASER_PCIEFD_SRB_CMD_RDB1)) {
            s->srb_rx_nr_packets = 0;
        }
    } else if (addr == 0x1f404) { /* srb_ien */
        s->srb_ien = val;
        pcibase_update_irq(s);
    } else if (addr == 0x1f40c) { /* srb_irq, W1C */
        s->srb_irq &= ~val;
        pcibase_update_irq(s);
    } else if (addr == 0x1f410) { /* srb_stat - read-only */
    } else if (addr == 0x1f414) { /* srb_rx_nr_packets - read-only */
    } else if (addr == 0x1f418) { /* srb_ctrl */
        s->srb_ctrl = val;
    } else if (addr >= 0x10000 && addr < 0x10000 + KVASER_PCIEFD_MAX_CAN_CHANNELS * 0x1000) {
        int chan = (addr - 0x10000) / 0x1000;
        hwaddr chan_offset = (addr - 0x10000) % 0x1000;
        if (chan >= s->nr_channels) {
            return;
        }
        switch (chan_offset) {
            case KVASER_PCIEFD_KCAN_FIFO_REG:
                s->can[chan].fifo = val;
                break;
            case KVASER_PCIEFD_KCAN_FIFO_LAST_REG:
                /* Write to FIFO last, ignored */
                break;
            case KVASER_PCIEFD_KCAN_CTRL_REG:
                s->can[chan].ctrl = val;
                break;
            case KVASER_PCIEFD_KCAN_CMD_REG:
                /* Write to CMD, ignored */
                break;
            case KVASER_PCIEFD_KCAN_IOC_REG:
                s->can[chan].ioc = val;
                break;
            case KVASER_PCIEFD_KCAN_IEN_REG:
                s->can[chan].ien = val;
                pcibase_update_irq(s);
                break;
            case KVASER_PCIEFD_KCAN_IRQ_REG:
                s->can[chan].irq &= ~val; /* W1C */
                pcibase_update_irq(s);
                break;
            case KVASER_PCIEFD_KCAN_TX_NR_PACKETS_REG:
                /* read-only */
                break;
            case KVASER_PCIEFD_KCAN_STAT_REG:
                /* read-only */
                break;
            case KVASER_PCIEFD_KCAN_MODE_REG:
                s->can[chan].mode = val;
                break;
            case KVASER_PCIEFD_KCAN_BTRN_REG:
                s->can[chan].btrn = val;
                break;
            case KVASER_PCIEFD_KCAN_BUS_LOAD_REG:
                s->can[chan].bus_load = val;
                break;
            case KVASER_PCIEFD_KCAN_BTRD_REG:
                s->can[chan].btrd = val;
                break;
            case KVASER_PCIEFD_KCAN_PWM_REG:
                s->can[chan].pwm = val;
                break;
            default:
                /* ignore writes to unknown register */
                break;
        }
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

    /* Set default register values */
    s->pci_ien = 0;
    s->pci_irq = 0;

    /* Sysid values: 4 channels, major=1, minor=0 */
    s->sysid_version = (4 << 24) | (1 << 16) | 0;
    s->sysid_canfreq = 80000000;
    s->sysid_busfreq = 100000000;
    s->sysid_build = (1 << 1);  /* sequence 1 (bit 1 set) */

    s->srb_stat = KVASER_PCIEFD_SRB_STAT_DMA | KVASER_PCIEFD_SRB_STAT_DI;
    s->srb_ctrl = 0;
    s->srb_cmd = 0;
    s->srb_ien = 0;
    s->srb_irq = 0;
    s->srb_rx_nr_packets = 0;

    s->loopback = 0;

    s->nr_channels = 4;

    for (int i = 0; i < s->nr_channels; i++) {
        memset(&s->can[i], 0, sizeof(s->can[i]));
        s->can[i].stat = KVASER_PCIEFD_KCAN_STAT_FD | KVASER_PCIEFD_KCAN_STAT_CAP | KVASER_PCIEFD_KCAN_STAT_IDLE;
        s->can[i].tx_nr_packets = (64 << 16) | 0;  /* max 64, current 0 */
    }

    for (int i = 0; i < 2; i++) {
        s->serdes_word1[i] = 0;
        s->serdes_word2[i] = 0;
    }

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1a07);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x000d);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200);
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x20000;
    s->bar_info[0].name = "regs";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No need to set initial register state here; reset will handle it */
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

}

/* Minimal VMState to satisfy QEMU migration subsystems */
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

type_init(pcibase_register_types);