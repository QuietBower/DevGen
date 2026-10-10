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

#define TYPE_PCIBASE_DEVICE "kvaser_pciefd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define KVASER_PCIEFD_VENDOR 0x1a07
#define KVASER_PCIEFD_4HS_DEVICE_ID 0x000d

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

#define KVASER_PCIEFD_SRB_CMD_REG 0x0
#define KVASER_PCIEFD_SRB_IEN_REG 0x04
#define KVASER_PCIEFD_SRB_IRQ_REG 0x0c
#define KVASER_PCIEFD_SRB_STAT_REG 0x10
#define KVASER_PCIEFD_SRB_RX_NR_PACKETS_REG 0x14
#define KVASER_PCIEFD_SRB_CTRL_REG 0x18
#define KVASER_PCIEFD_SRB_FIFO_LAST_REG 0x1f4

#define KVASER_PCIEFD_SRB_IRQ_DPD0 (1 << 8)
#define KVASER_PCIEFD_SRB_IRQ_DPD1 (1 << 9)
#define KVASER_PCIEFD_SRB_IRQ_DOF0 (1 << 10)
#define KVASER_PCIEFD_SRB_IRQ_DOF1 (1 << 11)
#define KVASER_PCIEFD_SRB_IRQ_DUF0 (1 << 12)
#define KVASER_PCIEFD_SRB_IRQ_DUF1 (1 << 13)
#define KVASER_PCIEFD_SRB_CMD_RDB0 (1 << 4)
#define KVASER_PCIEFD_SRB_CMD_RDB1 (1 << 5)
#define KVASER_PCIEFD_SRB_CMD_FOR (1 << 0)
#define KVASER_PCIEFD_SRB_RX_NR_PACKETS_MASK 0xFF
#define KVASER_PCIEFD_SRB_STAT_DI (1 << 15)
#define KVASER_PCIEFD_SRB_CTRL_DMA_ENABLE (1 << 0)

#define KVASER_PCIEFD_DMA_COUNT 2U
#define KVASER_PCIEFD_DMA_SIZE (4U * 1024U)
#define KVASER_PCIEFD_MAX_CAN_CHANNELS 8UL

#define KVASER_PCIEFD_SYSID_VERSION_NR_CHAN_MASK 0xFF000000
#define KVASER_PCIEFD_SYSID_VERSION_MAJOR_MASK 0x00FF0000
#define KVASER_PCIEFD_SYSID_VERSION_MINOR_MASK 0x000000FF
#define KVASER_PCIEFD_SYSID_BUILD_SEQ_MASK 0x0000FFFE
#define KVASER_PCIEFD_KCAN_TX_NR_PACKETS_MAX_MASK 0x00FF0000
#define KVASER_PCIEFD_SRB_STAT_DMA (1 << 24)
#define KVASER_PCIEFD_KCAN_STAT_FD (1 << 19)
#define KVASER_PCIEFD_KCAN_STAT_CAP (1 << 16)
#define KVASER_PCIEFD_KCAN_IRQ_ABD (1 << 13)

/* New macros from iteration */
#define KVASER_PCIEFD_KCAN_STAT_IDLE (1U << 10)
#define KVASER_PCIEFD_KCAN_STAT_RMR (1U << 14)
#define KVASER_PCIEFD_KCAN_STAT_AR (1U << 7)
#define KVASER_PCIEFD_KCAN_STAT_BOFF (1U << 11)
#define KVASER_PCIEFD_KCAN_STAT_IRM (1U << 15)
/* KVASER_PCIEFD_KCAN_STAT_BUS_OFF_MASK omitted pending missing dependencies */
#define KVASER_PCIEFD_KCAN_STAT_SEQNO_MASK 0xFF000000
#define KVASER_PCIEFD_KCAN_TX_NR_PACKETS_CURRENT_MASK 0x000000FF
#define KVASER_PCIEFD_KCAN_CTRL_TYPE_MASK 0xE0000000
#define KVASER_PCIEFD_KCAN_CTRL_TYPE_EFLUSH 0x4
#define KVASER_PCIEFD_KCAN_CMD_MASK 0x0000003F
#define KVASER_PCIEFD_KCAN_CMD_SEQ_MASK 0x00FF0000
#define KVASER_PCIEFD_KCAN_CMD_SRQ (1U << 0)
#define KVASER_PCIEFD_KCAN_CMD_AT (1U << 1)
#define KVASER_PCIEFD_KCAN_MODE_EPEN (1U << 12)
#define KVASER_PCIEFD_KCAN_MODE_CCM (1U << 31)
#define KVASER_PCIEFD_KCAN_MODE_NIFDEN (1U << 15)
#define KVASER_PCIEFD_KCAN_MODE_LOM (1U << 9)
#define KVASER_PCIEFD_KCAN_MODE_EEN (1U << 23)
#define KVASER_PCIEFD_KCAN_MODE_APT (1U << 20)
#define KVASER_PCIEFD_KCAN_MODE_RM (1U << 8)
#define KVASER_PCIEFD_KCAN_PWM_TOP_MASK 0x00FF0000
#define KVASER_PCIEFD_KCAN_PWM_TRIGGER_MASK 0x000000FF
#define KVASER_PCIEFD_KCAN_IOC_LED (1U << 0)
#define KVASER_PCIEFD_KCAN_IRQ_TE (1U << 16)
#define KVASER_PCIEFD_KCAN_IRQ_ROF (1U << 5)
#define KVASER_PCIEFD_KCAN_IRQ_TOF (1U << 15)
#define KVASER_PCIEFD_KCAN_IRQ_TAE (1U << 1)
#define KVASER_PCIEFD_KCAN_IRQ_TAL (1U << 17)
#define KVASER_PCIEFD_KCAN_IRQ_FDIC (1U << 3)
#define KVASER_PCIEFD_KCAN_IRQ_BPP (1U << 2)
#define KVASER_PCIEFD_KCAN_IRQ_TAR (1U << 0)
#define KVASER_PCIEFD_KCAN_BTRN_TSEG2_MASK 0x7C000000
#define KVASER_PCIEFD_KCAN_BTRN_TSEG1_MASK 0x03FE0000
#define KVASER_PCIEFD_KCAN_BTRN_SJW_MASK 0x0001E000
#define KVASER_PCIEFD_KCAN_BTRN_BRP_MASK 0x00001FFF
#define KVASER_PCIEFD_ALTERA_DMA_64BIT (1U << 0)
#define KVASER_PCIEFD_PACKET_SEQ_MASK 0x000000FF
#define KVASER_PCIEFD_PACKET_CHID_MASK 0x0E000000
#define KVASER_PCIEFD_PACKET_TYPE_MASK 0xF0000000
#define KVASER_PCIEFD_PACK_TYPE_DATA 0x0
#define KVASER_PCIEFD_PACK_TYPE_ACK 0x1
#define KVASER_PCIEFD_PACK_TYPE_STATUS 0x8
#define KVASER_PCIEFD_PACK_TYPE_ERROR 0x3
#define KVASER_PCIEFD_PACK_TYPE_EFLUSH_ACK 0x4
#define KVASER_PCIEFD_PACK_TYPE_ACK_DATA 0x6
#define KVASER_PCIEFD_PACK_TYPE_BUS_LOAD 0x9
#define KVASER_PCIEFD_PACK_TYPE_EFRAME_ACK 0x5
#define KVASER_PCIEFD_PACK_TYPE_TXRQ 0x2
#define KVASER_PCIEFD_RPACKET_RTR (1U << 29)
#define KVASER_PCIEFD_RPACKET_IDE (1U << 30)
#define KVASER_PCIEFD_RPACKET_ID_MASK 0x1FFFFFFF
#define KVASER_PCIEFD_RPACKET_DLC_MASK 0x00000F00
#define KVASER_PCIEFD_RPACKET_FDF (1U << 15)
#define KVASER_PCIEFD_RPACKET_BRS (1U << 14)
#define KVASER_PCIEFD_RPACKET_ESI (1U << 13)
#define KVASER_PCIEFD_TPACKET_SMS (1U << 16)
#define KVASER_PCIEFD_TPACKET_AREQ (1U << 31)
#define KVASER_PCIEFD_SPACK_BOFF (1U << 16)
#define KVASER_PCIEFD_SPACK_IRM (1U << 21)
#define KVASER_PCIEFD_SPACK_EPLR (1U << 24)
#define KVASER_PCIEFD_SPACK_EWLR (1U << 23)
#define KVASER_PCIEFD_SPACK_TXERR_MASK 0x000000FF
#define KVASER_PCIEFD_SPACK_RXERR_MASK 0x0000FF00
#define KVASER_PCIEFD_SPACK_RMCD (1U << 22)
#define KVASER_PCIEFD_SPACK_AUTO (1U << 21)
#define KVASER_PCIEFD_SPACK_IDET (1U << 20)
#define KVASER_PCIEFD_EPACK_DIR_TX (1U << 0)
#define KVASER_PCIEFD_APACKET_ABL (1U << 10)
#define KVASER_PCIEFD_APACKET_CT (1U << 9)
#define KVASER_PCIEFD_APACKET_NACK (1U << 11)
#define KVASER_PCIEFD_APACKET_FLU (1U << 8)

/* Altera variant base addresses */
#define ALTERA_SERDES_BASE 0x1000
#define ALTERA_PCI_IEN 0x50
#define ALTERA_PCI_IRQ 0x40
#define ALTERA_SYSID_BASE 0x1f020
#define ALTERA_LOOPBACK_BASE 0x1f000
#define ALTERA_KCAN_SRB_FIFO_BASE 0x1f200
#define ALTERA_KCAN_SRB_BASE 0x1f400
#define ALTERA_KCAN_CH0_BASE 0x10000
#define ALTERA_KCAN_CH1_BASE 0x11000

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
    uint32_t srb_ien;
    uint32_t srb_irq;
    uint32_t kcan_ien[KVASER_PCIEFD_MAX_CAN_CHANNELS];
    uint32_t kcan_irq[KVASER_PCIEFD_MAX_CAN_CHANNELS];

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t sysid_version;
    uint32_t sysid_canfreq;
    uint32_t sysid_busfreq;
    uint32_t sysid_build;
    uint32_t srb_cmd;
    uint32_t srb_stat;
    uint32_t srb_rx_nr_packets;
    uint32_t srb_ctrl;
    uint32_t kcan_ctrl[KVASER_PCIEFD_MAX_CAN_CHANNELS];
    uint32_t kcan_cmd[KVASER_PCIEFD_MAX_CAN_CHANNELS];
    uint32_t kcan_ioc[KVASER_PCIEFD_MAX_CAN_CHANNELS];
    uint32_t kcan_tx_nr_packets[KVASER_PCIEFD_MAX_CAN_CHANNELS];
    uint32_t kcan_stat[KVASER_PCIEFD_MAX_CAN_CHANNELS];
    uint32_t kcan_mode[KVASER_PCIEFD_MAX_CAN_CHANNELS];
    uint32_t kcan_btrn[KVASER_PCIEFD_MAX_CAN_CHANNELS];
    uint32_t kcan_bus_load[KVASER_PCIEFD_MAX_CAN_CHANNELS];
    uint32_t kcan_btrd[KVASER_PCIEFD_MAX_CAN_CHANNELS];
    uint32_t kcan_pwm[KVASER_PCIEFD_MAX_CAN_CHANNELS];

    /* DMA Context */
    dma_addr_t dma_base[KVASER_PCIEFD_DMA_COUNT];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;
    s->pci_irq = 0;
    
    if (s->srb_irq & s->srb_ien) {
        s->pci_irq |= (1 << 4); /* kcan_rx0 is BIT(4) */
    }
    
    for (int i = 0; i < 2; i++) {
        if (s->kcan_irq[i] & s->kcan_ien[i]) {
            s->pci_irq |= (1 << i); /* kcan_tx is BIT(0), BIT(1) */
        }
    }
    
    if (s->pci_irq & s->pci_ien) {
        raise = true;
    }
    
    if (raise) {
        if (s->has_msi) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    
    if (addr == ALTERA_PCI_IEN) return s->pci_ien;
    if (addr == ALTERA_PCI_IRQ) return s->pci_irq;
    
    if (addr >= ALTERA_SYSID_BASE && addr < ALTERA_SYSID_BASE + 0x20) {
        uint32_t offset = addr - ALTERA_SYSID_BASE;
        switch (offset) {
            case KVASER_PCIEFD_SYSID_VERSION_REG: return s->sysid_version;
            case KVASER_PCIEFD_SYSID_CANFREQ_REG: return s->sysid_canfreq;
            case KVASER_PCIEFD_SYSID_BUSFREQ_REG: return s->sysid_busfreq;
            case KVASER_PCIEFD_SYSID_BUILD_REG: return s->sysid_build;
        }
    }
    
    if (addr >= ALTERA_KCAN_SRB_BASE && addr < ALTERA_KCAN_SRB_BASE + 0x200) {
        uint32_t offset = addr - ALTERA_KCAN_SRB_BASE;
        switch (offset) {
            case KVASER_PCIEFD_SRB_CMD_REG: return s->srb_cmd;
            case KVASER_PCIEFD_SRB_IEN_REG: return s->srb_ien;
            case KVASER_PCIEFD_SRB_IRQ_REG: return s->srb_irq;
            case KVASER_PCIEFD_SRB_STAT_REG: return s->srb_stat;
            case KVASER_PCIEFD_SRB_RX_NR_PACKETS_REG: return s->srb_rx_nr_packets;
            case KVASER_PCIEFD_SRB_CTRL_REG: return s->srb_ctrl;
        }
    }
    
    for (int i = 0; i < 2; i++) {
        uint32_t ch_base = (i == 0) ? ALTERA_KCAN_CH0_BASE : ALTERA_KCAN_CH1_BASE;
        if (addr >= ch_base && addr < ch_base + 0x1000) {
            uint32_t offset = addr - ch_base;
            switch (offset) {
                case KVASER_PCIEFD_KCAN_CTRL_REG: return s->kcan_ctrl[i];
                case KVASER_PCIEFD_KCAN_CMD_REG: return s->kcan_cmd[i];
                case KVASER_PCIEFD_KCAN_IOC_REG: return s->kcan_ioc[i];
                case KVASER_PCIEFD_KCAN_IEN_REG: return s->kcan_ien[i];
                case KVASER_PCIEFD_KCAN_IRQ_REG: return s->kcan_irq[i];
                case KVASER_PCIEFD_KCAN_TX_NR_PACKETS_REG: return s->kcan_tx_nr_packets[i];
                case KVASER_PCIEFD_KCAN_STAT_REG: return s->kcan_stat[i];
                case KVASER_PCIEFD_KCAN_MODE_REG: return s->kcan_mode[i];
                case KVASER_PCIEFD_KCAN_BTRN_REG: return s->kcan_btrn[i];
                case KVASER_PCIEFD_KCAN_BUS_LOAD_REG: return s->kcan_bus_load[i];
                case KVASER_PCIEFD_KCAN_BTRD_REG: return s->kcan_btrd[i];
                case KVASER_PCIEFD_KCAN_PWM_REG: return s->kcan_pwm[i];
            }
        }
    }
    
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == ALTERA_PCI_IEN) {
        s->pci_ien = val;
        pcibase_update_irq(s);
        return;
    }
    
    if (addr >= ALTERA_SERDES_BASE && addr < ALTERA_SERDES_BASE + 0x20) {
        uint32_t offset = addr - ALTERA_SERDES_BASE;
        int index = offset / 8;
        if (index < KVASER_PCIEFD_DMA_COUNT) {
            if ((offset % 8) == 0) {
                s->dma_base[index] = (s->dma_base[index] & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFF);
            } else {
                s->dma_base[index] = (s->dma_base[index] & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
            }
        }
        return;
    }

    if (addr >= ALTERA_KCAN_SRB_BASE && addr < ALTERA_KCAN_SRB_BASE + 0x200) {
        uint32_t offset = addr - ALTERA_KCAN_SRB_BASE;
        switch (offset) {
            case KVASER_PCIEFD_SRB_CMD_REG:
                s->srb_cmd = val;
                if (val & KVASER_PCIEFD_SRB_CMD_FOR) {
                    s->srb_rx_nr_packets = 0;
                }
                break;
            case KVASER_PCIEFD_SRB_IEN_REG:
                s->srb_ien = val;
                pcibase_update_irq(s);
                break;
            case KVASER_PCIEFD_SRB_IRQ_REG:
                s->srb_irq &= ~val;
                pcibase_update_irq(s);
                break;
            case KVASER_PCIEFD_SRB_CTRL_REG:
                s->srb_ctrl = val;
                break;
        }
        return;
    }

    for (int i = 0; i < 2; i++) {
        uint32_t ch_base = (i == 0) ? ALTERA_KCAN_CH0_BASE : ALTERA_KCAN_CH1_BASE;
        if (addr >= ch_base && addr < ch_base + 0x1000) {
            uint32_t offset = addr - ch_base;
            switch (offset) {
                case KVASER_PCIEFD_KCAN_CTRL_REG: s->kcan_ctrl[i] = val; break;
                case KVASER_PCIEFD_KCAN_CMD_REG: s->kcan_cmd[i] = val; break;
                case KVASER_PCIEFD_KCAN_IOC_REG: s->kcan_ioc[i] = val; break;
                case KVASER_PCIEFD_KCAN_IEN_REG: 
                    s->kcan_ien[i] = val; 
                    pcibase_update_irq(s);
                    break;
                case KVASER_PCIEFD_KCAN_IRQ_REG: 
                    s->kcan_irq[i] &= ~val;
                    pcibase_update_irq(s);
                    break;
                case KVASER_PCIEFD_KCAN_MODE_REG: s->kcan_mode[i] = val; break;
                case KVASER_PCIEFD_KCAN_BTRN_REG: s->kcan_btrn[i] = val; break;
                case KVASER_PCIEFD_KCAN_BUS_LOAD_REG: s->kcan_bus_load[i] = val; break;
                case KVASER_PCIEFD_KCAN_BTRD_REG: s->kcan_btrd[i] = val; break;
                case KVASER_PCIEFD_KCAN_PWM_REG: s->kcan_pwm[i] = val; break;
            }
            return;
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

    s->sysid_version = (2 << 24); /* 2 channels */
    s->sysid_canfreq = 80000000;
    s->sysid_busfreq = 80000000;
    s->sysid_build = 1;
    s->srb_stat = KVASER_PCIEFD_SRB_STAT_DMA | KVASER_PCIEFD_SRB_STAT_DI;
    for (int i = 0; i < 2; i++) {
        s->kcan_stat[i] = KVASER_PCIEFD_KCAN_STAT_FD | KVASER_PCIEFD_KCAN_STAT_CAP;
        s->kcan_tx_nr_packets[i] = (0xFF << 16); /* KVASER_PCIEFD_KCAN_TX_NR_PACKETS_MAX_MASK */
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
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  KVASER_PCIEFD_VENDOR );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  KVASER_PCIEFD_4HS_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
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
    s->bar_info[0].size = 0x20000; /* 128KB to cover 0x1f400 */
    s->bar_info[0].name = "kvaser-mmio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }
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
