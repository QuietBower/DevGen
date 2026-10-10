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

#define TYPE_PCIBASE_DEVICE "typhoon_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_3COM		0x10b7
#define PCI_DEVICE_ID_3COM_3CR990	0x9900

/* Register Layout and Hardware Identifiers extracted from driver source */
#define TYPHOON_REG_SOFT_RESET          0x00
#define TYPHOON_REG_INTR_STATUS         0x04
#define TYPHOON_REG_INTR_ENABLE         0x08
#define TYPHOON_REG_INTR_MASK           0x0c
#define TYPHOON_REG_SELF_INTERRUPT      0x10
#define TYPHOON_REG_HOST2ARM6           0x18
#define TYPHOON_REG_HOST2ARM5           0x1c
#define TYPHOON_REG_HOST2ARM4           0x20
#define TYPHOON_REG_HOST2ARM3           0x24
#define TYPHOON_REG_HOST2ARM2           0x28
#define TYPHOON_REG_HOST2ARM1           0x2c
#define TYPHOON_REG_HOST2ARM0           0x30
#define TYPHOON_REG_ARM2HOST3           0x34
#define TYPHOON_REG_ARM2HOST0           0x40

#define TYPHOON_REG_HEARTBEAT           TYPHOON_REG_ARM2HOST3
#define TYPHOON_REG_STATUS              TYPHOON_REG_ARM2HOST0
#define TYPHOON_REG_CMD_READY           TYPHOON_REG_HOST2ARM2
#define TYPHOON_REG_TX_LO_READY         TYPHOON_REG_HOST2ARM3
#define TYPHOON_REG_TX_HI_READY         TYPHOON_REG_HOST2ARM1
#define TYPHOON_REG_DOWNLOAD_HMAC_1     TYPHOON_REG_HOST2ARM3
#define TYPHOON_REG_DOWNLOAD_HMAC_0     TYPHOON_REG_HOST2ARM2
#define TYPHOON_REG_DOWNLOAD_HMAC_2     TYPHOON_REG_HOST2ARM4
#define TYPHOON_REG_DOWNLOAD_HMAC_3     TYPHOON_REG_HOST2ARM5
#define TYPHOON_REG_DOWNLOAD_HMAC_4     TYPHOON_REG_HOST2ARM6
#define TYPHOON_REG_DOWNLOAD_BOOT_ADDR  TYPHOON_REG_HOST2ARM1
#define TYPHOON_REG_BOOT_LENGTH         TYPHOON_REG_HOST2ARM1
#define TYPHOON_REG_COMMAND             TYPHOON_REG_HOST2ARM0
#define TYPHOON_REG_BOOT_DEST_ADDR      TYPHOON_REG_HOST2ARM3
#define TYPHOON_REG_BOOT_DATA_LO        TYPHOON_REG_HOST2ARM5
#define TYPHOON_REG_BOOT_CHECKSUM       TYPHOON_REG_HOST2ARM2
#define TYPHOON_REG_BOOT_DATA_HI        TYPHOON_REG_HOST2ARM4
#define TYPHOON_REG_BOOT_RECORD_ADDR_HI TYPHOON_REG_HOST2ARM2
#define TYPHOON_REG_BOOT_RECORD_ADDR_LO TYPHOON_REG_HOST2ARM1

#define TYPHOON_INTR_NONE               0x00000000
#define TYPHOON_INTR_HOST_INT           0x00000001
#define TYPHOON_INTR_ARM2HOST0          0x00000002
#define TYPHOON_INTR_SELF               0x00000800
#define TYPHOON_INTR_ALL                0xffffffff
#define TYPHOON_INTR_ENABLE_ALL         0xffffffef

#define TYPHOON_STATUS_WAITING_FOR_HOST 0x0d
#define TYPHOON_STATUS_WAITING_FOR_SEGMENT 0x10
#define TYPHOON_STATUS_WAITING_FOR_BOOT 0x07
#define TYPHOON_STATUS_RUNNING          0x09
#define TYPHOON_STATUS_SLEEPING         0x11
#define TYPHOON_STATUS_HALTED           0x14

#define TYPHOON_RESET_ALL               0x7f
#define TYPHOON_RESET_NONE              0x00

#define TYPHOON_CMD_READ_MAC_ADDRESS    0x0027
#define TYPHOON_CMD_READ_VERSIONS       0x0043

#define TYPHOON_BOOTCMD_RUNTIME_IMAGE		0xfd
#define TYPHOON_BOOTCMD_SEG_AVAILABLE		0xfc
#define TYPHOON_BOOTCMD_DNLD_COMPLETE		0xfb
#define TYPHOON_BOOTCMD_REG_BOOT_RECORD		0xff
#define TYPHOON_BOOTCMD_BOOT			0x00
#define TYPHOON_BOOTCMD_WAKEUP			0xfa

#define TYPHOON_INTR_BOOTCMD		TYPHOON_INTR_ARM2HOST0

#define TYPHOON_CMD_HELLO_RESP		0x0057
#define TYPHOON_CMD_READ_MEDIA_STATUS	0x001b
#define TYPHOON_CMD_SET_MULTICAST_HASH	0x0025
#define TYPHOON_CMD_SET_RX_FILTER	0x0005
#define TYPHOON_CMD_READ_STATS		0x0007
#define TYPHOON_CMD_XCVR_SELECT		0x0013
#define TYPHOON_CMD_ENABLE_WAKE_EVENTS	0x0049
#define TYPHOON_CMD_GOTO_SLEEP		0x0023
#define TYPHOON_CMD_SET_MAX_PKT_SIZE	0x001a
#define TYPHOON_CMD_SET_MAC_ADDRESS	0x0026
#define TYPHOON_CMD_IRQ_COALESCE_CTRL	0x0045
#define TYPHOON_CMD_VLAN_TYPE_WRITE	0x002b
#define TYPHOON_CMD_SET_OFFLOAD_TASKS	0x004f
#define TYPHOON_CMD_TX_ENABLE		0x0001
#define TYPHOON_CMD_RX_ENABLE		0x0003
#define TYPHOON_CMD_RX_DISABLE		0x0004
#define TYPHOON_CMD_TX_DISABLE		0x0002
#define TYPHOON_CMD_HALT		0x005d

struct cmd_desc {
    uint8_t  flags;
    uint8_t  numDesc;
    uint16_t cmd;
    uint16_t seqNo;
    uint16_t parm1;
    uint32_t parm2;
    uint32_t parm3;
} QEMU_PACKED;

struct resp_desc {
    uint8_t  flags;
    uint8_t  numDesc;
    uint16_t cmd;
    uint16_t seqNo;
    uint16_t parm1;
    uint32_t parm2;
    uint32_t parm3;
} QEMU_PACKED;

struct rx_desc {
    uint8_t  flags;
    uint8_t  numDesc;
    uint16_t frameLen;
    uint32_t addr;
    uint32_t addrHi;
    uint32_t rxStatus;
    uint16_t filterResults;
    uint16_t ipsecResults;
    uint32_t vlanTag;
} QEMU_PACKED;

struct tx_desc {
    uint8_t  flags;
    uint8_t  numDesc;
    uint16_t len;
    union {
        struct {
            uint32_t addr;
            uint32_t addrHi;
        } frag;
        uint64_t tx_addr;
    };
    uint32_t processFlags;
} QEMU_PACKED;

struct typhoon_interface {
    uint32_t ringIndex;
    uint32_t ringIndexHi;
    uint32_t txLoAddr;
    uint32_t txLoAddrHi;
    uint32_t txLoSize;
    uint32_t txHiAddr;
    uint32_t txHiAddrHi;
    uint32_t txHiSize;
    uint32_t rxLoAddr;
    uint32_t rxLoAddrHi;
    uint32_t rxLoSize;
    uint32_t rxBuffAddr;
    uint32_t rxBuffAddrHi;
    uint32_t rxBuffSize;
    uint32_t cmdAddr;
    uint32_t cmdAddrHi;
    uint32_t cmdSize;
    uint32_t respAddr;
    uint32_t respAddrHi;
    uint32_t respSize;
    uint32_t zeroAddr;
    uint32_t zeroAddrHi;
    uint32_t rxHiAddr;
    uint32_t rxHiAddrHi;
    uint32_t rxHiSize;
} QEMU_PACKED;

struct typhoon_indexes {
    volatile uint32_t rxHiCleared;
    volatile uint32_t rxLoCleared;
    volatile uint32_t rxBuffReady;
    volatile uint32_t respCleared;
    volatile uint32_t txLoCleared;
    volatile uint32_t txHiCleared;
    volatile uint32_t rxLoReady;
    volatile uint32_t rxBuffCleared;
    volatile uint32_t cmdCleared;
    volatile uint32_t respReady;
    volatile uint32_t rxHiReady;
} QEMU_PACKED;

struct rx_free {
    uint32_t physAddr;
    uint32_t physAddrHi;
    uint32_t virtAddr;
    uint32_t virtAddrHi;
} QEMU_PACKED;

struct tcpopt_desc {
    uint8_t  flags;
    uint8_t  numDesc;
    uint16_t mss_flags;
#define TYPHOON_TSO_FIRST		0x1000
#define TYPHOON_TSO_LAST		0x2000
    uint32_t respAddrLo;
    uint32_t bytesTx;
    uint32_t status;
} QEMU_PACKED;

struct stats_resp {
    uint8_t  flags;
    uint8_t  numDesc;
    uint16_t cmd;
    uint16_t seqNo;
    uint16_t unused;
    uint32_t txPackets;
    uint64_t txBytes;
    uint32_t txDeferred;
    uint32_t txLateCollisions;
    uint32_t txCollisions;
    uint32_t txCarrierLost;
    uint32_t txMultipleCollisions;
    uint32_t txExcessiveCollisions;
    uint32_t txFifoUnderruns;
    uint32_t txMulticastTxOverflows;
    uint32_t txFiltered;
    uint32_t rxPacketsGood;
    uint64_t rxBytesGood;
    uint32_t rxFifoOverruns;
    uint32_t BadSSD;
    uint32_t rxCrcErrors;
    uint32_t rxOversized;
    uint32_t rxBroadcast;
    uint32_t rxMulticast;
    uint32_t rxOverflow;
    uint32_t rxFiltered;
    uint32_t linkStatus;
#define TYPHOON_LINK_STAT_MASK		0x00000001
#define TYPHOON_LINK_GOOD		0x00000001
#define TYPHOON_LINK_BAD		0x00000000
#define TYPHOON_LINK_SPEED_MASK		0x00000002
#define TYPHOON_LINK_100MBPS		0x00000002
#define TYPHOON_LINK_10MBPS		0x00000000
#define TYPHOON_LINK_DUPLEX_MASK	0x00000004
#define TYPHOON_LINK_FULL_DUPLEX	0x00000004
#define TYPHOON_LINK_HALF_DUPLEX	0x00000000
    uint32_t unused2;
    uint32_t unused3;
} QEMU_PACKED;

struct typhoon_file_header {
    uint8_t  tag[8];
    uint32_t version;
    uint32_t numSections;
    uint32_t startAddr;
    uint32_t hmacDigest[5];
} QEMU_PACKED;

struct typhoon_section_header {
    uint32_t len;
    uint16_t checksum;
    uint16_t reserved;
    uint32_t startAddr;
} QEMU_PACKED;

#define COMMAND_ENTRIES		16
#define RESPONSE_ENTRIES	32
#define RXFREE_ENTRIES		128
#define TXLO_ENTRIES		128
#define RX_ENTRIES		32
#define TXHI_ENTRIES		2

#define COMMAND_RING_SIZE	(COMMAND_ENTRIES * sizeof(struct cmd_desc))
#define RESPONSE_RING_SIZE	(RESPONSE_ENTRIES * sizeof(struct resp_desc))

#define TYPHOON_RESP_ERROR	0x40
#define TYPHOON_CMD_RESPOND      0x40
#define TYPHOON_DESC_VALID       0x80
#define TYPHOON_RESP_DESC        0x05

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
    uint32_t intr_status;
    uint32_t intr_mask;
    uint32_t intr_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[256];

    /* Host/NIC shared-memory interface registered by boot command 0xff. */
    struct typhoon_interface iface;
    uint64_t boot_record_addr;
    uint32_t cmd_consumer;
    uint32_t resp_producer;
    bool iface_valid;
    uint8_t macaddr[6];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t status = s->regs[TYPHOON_REG_INTR_STATUS / 4];
    uint32_t mask = s->regs[TYPHOON_REG_INTR_MASK / 4];
    uint32_t enable = s->regs[TYPHOON_REG_INTR_ENABLE / 4];

    bool irq_active = (status & enable & ~mask) != 0;
    if (irq_active) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t typhoon_dma_addr(uint32_t lo, uint32_t hi)
{
    return ((uint64_t)le32_to_cpu(hi) << 32) | le32_to_cpu(lo);
}

static void typhoon_raise_status(PCIBaseState *s, uint32_t bits)
{
    s->regs[TYPHOON_REG_INTR_STATUS / 4] |= bits;
    pcibase_update_irq(s);
}

static bool typhoon_write_index(PCIBaseState *s, size_t offset,
                                uint32_t value)
{
    uint64_t index_addr = typhoon_dma_addr(s->iface.ringIndex,
                                           s->iface.ringIndexHi);

    return stl_le_pci_dma(PCI_DEVICE(s), index_addr + offset, value,
                          MEMTXATTRS_UNSPECIFIED) == MEMTX_OK;
}

static bool typhoon_dma_write_ring(PCIBaseState *s, uint64_t ring_addr,
                                   uint32_t ring_size, uint32_t offset,
                                   const void *buf, uint32_t len)
{
    uint32_t first;

    if (!ring_size || offset >= ring_size || len > ring_size) {
        return false;
    }

    first = MIN(len, ring_size - offset);
    if (pci_dma_write(PCI_DEVICE(s), ring_addr + offset, buf, first) !=
        MEMTX_OK) {
        return false;
    }

    if (first != len &&
        pci_dma_write(PCI_DEVICE(s), ring_addr,
                      (const uint8_t *)buf + first, len - first) != MEMTX_OK) {
        return false;
    }

    return true;
}

static bool typhoon_queue_response(PCIBaseState *s,
                                   const struct cmd_desc *cmd)
{
    uint8_t response[sizeof(struct stats_resp)] = { 0 };
    struct resp_desc *resp = (struct resp_desc *)response;
    uint16_t command = le16_to_cpu(cmd->cmd);
    uint32_t response_len = sizeof(*resp);
    uint32_t ring_size = le32_to_cpu(s->iface.respSize);
    uint64_t ring_addr = typhoon_dma_addr(s->iface.respAddr,
                                          s->iface.respAddrHi);

    resp->flags = TYPHOON_RESP_DESC | TYPHOON_DESC_VALID;
    resp->cmd = cmd->cmd;
    resp->seqNo = cmd->seqNo;

    switch (command) {
    case TYPHOON_CMD_READ_MAC_ADDRESS:
        resp->parm1 = cpu_to_le16(((uint16_t)s->macaddr[0] << 8) |
                                  s->macaddr[1]);
        resp->parm2 = cpu_to_le32(((uint32_t)s->macaddr[2] << 24) |
                                  ((uint32_t)s->macaddr[3] << 16) |
                                  ((uint32_t)s->macaddr[4] << 8) |
                                  s->macaddr[5]);
        break;
    case TYPHOON_CMD_READ_VERSIONS:
        /* A single-descriptor response denotes a Typhoon 1.0 image. */
        resp->parm2 = cpu_to_le32(0x00000916);
        break;
    case TYPHOON_CMD_READ_STATS: {
        struct stats_resp *stats = (struct stats_resp *)response;

        stats->numDesc = 6;
        stats->linkStatus = cpu_to_le32(TYPHOON_LINK_GOOD |
                                        TYPHOON_LINK_100MBPS |
                                        TYPHOON_LINK_FULL_DUPLEX);
        response_len = sizeof(*stats);
        break;
    }
    default:
        break;
    }

    if (ring_size < sizeof(*resp) || ring_size % sizeof(*resp) ||
        response_len > ring_size ||
        !typhoon_dma_write_ring(s, ring_addr, ring_size, s->resp_producer,
                                response, response_len)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "typhoon_pci: unable to DMA command response\n");
        return false;
    }

    s->resp_producer = (s->resp_producer + response_len) % ring_size;
    if (!typhoon_write_index(s, offsetof(struct typhoon_indexes, respReady),
                             s->resp_producer)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "typhoon_pci: unable to DMA response producer index\n");
        return false;
    }

    /* During probe the driver polls the ring with interrupts disabled. */
    if (s->regs[TYPHOON_REG_INTR_ENABLE / 4] & TYPHOON_INTR_HOST_INT) {
        typhoon_raise_status(s, TYPHOON_INTR_HOST_INT);
    }
    return true;
}

static void typhoon_handle_command(PCIBaseState *s,
                                   const struct cmd_desc *cmd)
{
    uint16_t command = le16_to_cpu(cmd->cmd);

    switch (command) {
    case TYPHOON_CMD_SET_MAC_ADDRESS: {
        uint16_t first = le16_to_cpu(cmd->parm1);
        uint32_t last = le32_to_cpu(cmd->parm2);

        s->macaddr[0] = first >> 8;
        s->macaddr[1] = first;
        s->macaddr[2] = last >> 24;
        s->macaddr[3] = last >> 16;
        s->macaddr[4] = last >> 8;
        s->macaddr[5] = last;
        break;
    }
    case TYPHOON_CMD_GOTO_SLEEP:
        s->regs[TYPHOON_REG_STATUS / 4] = TYPHOON_STATUS_SLEEPING;
        break;
    case TYPHOON_CMD_HALT:
        s->regs[TYPHOON_REG_STATUS / 4] = TYPHOON_STATUS_HALTED;
        break;
    default:
        break;
    }

    if (cmd->flags & TYPHOON_CMD_RESPOND) {
        typhoon_queue_response(s, cmd);
    }
}

static void typhoon_process_commands(PCIBaseState *s, uint32_t ready)
{
    uint32_t ring_size = le32_to_cpu(s->iface.cmdSize);
    uint64_t ring_addr = typhoon_dma_addr(s->iface.cmdAddr,
                                          s->iface.cmdAddrHi);
    unsigned int processed = 0;

    if (ring_size < sizeof(struct cmd_desc) ||
        ring_size % sizeof(struct cmd_desc) || ready >= ring_size ||
        ready % sizeof(struct cmd_desc)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "typhoon_pci: invalid command producer 0x%x\n", ready);
        return;
    }

    while (s->cmd_consumer != ready && processed++ < COMMAND_ENTRIES) {
        struct cmd_desc cmd;
        uint32_t count;

        if (pci_dma_read(PCI_DEVICE(s), ring_addr + s->cmd_consumer,
                         &cmd, sizeof(cmd)) != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "typhoon_pci: unable to DMA command descriptor\n");
            return;
        }

        count = cmd.numDesc + 1;
        if (count > COMMAND_ENTRIES ||
            count * sizeof(cmd) > ring_size) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "typhoon_pci: invalid command descriptor count %u\n",
                          count);
            return;
        }

        typhoon_handle_command(s, &cmd);
        s->cmd_consumer = (s->cmd_consumer + count * sizeof(cmd)) % ring_size;
    }

    if (s->cmd_consumer != ready) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "typhoon_pci: command ring did not converge\n");
        return;
    }

    if (!typhoon_write_index(s, offsetof(struct typhoon_indexes, cmdCleared),
                             s->cmd_consumer)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "typhoon_pci: unable to DMA command consumer index\n");
    }
}

static bool typhoon_register_interface(PCIBaseState *s)
{
    uint64_t addr = ((uint64_t)s->regs[TYPHOON_REG_BOOT_RECORD_ADDR_HI / 4]
                     << 32) |
                    s->regs[TYPHOON_REG_BOOT_RECORD_ADDR_LO / 4];

    if (pci_dma_read(PCI_DEVICE(s), addr, &s->iface, sizeof(s->iface)) !=
        MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "typhoon_pci: unable to DMA boot record at 0x%" PRIx64
                      "\n", addr);
        return false;
    }

    if (le32_to_cpu(s->iface.cmdSize) != COMMAND_RING_SIZE ||
        le32_to_cpu(s->iface.respSize) != RESPONSE_RING_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "typhoon_pci: invalid command/response ring sizes\n");
        return false;
    }

    s->boot_record_addr = addr;
    s->cmd_consumer = 0;
    s->resp_producer = 0;
    s->iface_valid = true;
    return typhoon_write_index(s, offsetof(struct typhoon_indexes, cmdCleared),
                               0) &&
           typhoon_write_index(s, offsetof(struct typhoon_indexes, respReady),
                               0);
}

static bool typhoon_fetch_firmware_segment(PCIBaseState *s)
{
    uint32_t len = s->regs[TYPHOON_REG_BOOT_LENGTH / 4];
    uint64_t addr = ((uint64_t)s->regs[TYPHOON_REG_BOOT_DATA_HI / 4] << 32) |
                    s->regs[TYPHOON_REG_BOOT_DATA_LO / 4];
    g_autofree uint8_t *data = NULL;

    /* The Linux driver supplies at most one 4 KiB staging page. */
    if (!len || len > 4096) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "typhoon_pci: invalid firmware segment length %u\n", len);
        return false;
    }

    data = g_malloc(len);
    if (pci_dma_read(PCI_DEVICE(s), addr, data, len) != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "typhoon_pci: unable to DMA firmware segment\n");
        return false;
    }
    return true;
}

static void typhoon_boot_command(PCIBaseState *s, uint32_t command)
{
    switch (command) {
    case TYPHOON_BOOTCMD_RUNTIME_IMAGE:
        s->regs[TYPHOON_REG_STATUS / 4] =
            TYPHOON_STATUS_WAITING_FOR_SEGMENT;
        typhoon_raise_status(s, TYPHOON_INTR_BOOTCMD);
        break;
    case TYPHOON_BOOTCMD_SEG_AVAILABLE:
        if (typhoon_fetch_firmware_segment(s)) {
            s->regs[TYPHOON_REG_STATUS / 4] =
                TYPHOON_STATUS_WAITING_FOR_SEGMENT;
            typhoon_raise_status(s, TYPHOON_INTR_BOOTCMD);
        } else {
            s->regs[TYPHOON_REG_STATUS / 4] = TYPHOON_STATUS_HALTED;
        }
        break;
    case TYPHOON_BOOTCMD_DNLD_COMPLETE:
        s->regs[TYPHOON_REG_STATUS / 4] = TYPHOON_STATUS_WAITING_FOR_BOOT;
        break;
    case TYPHOON_BOOTCMD_REG_BOOT_RECORD:
        if (typhoon_register_interface(s)) {
            s->regs[TYPHOON_REG_STATUS / 4] = TYPHOON_STATUS_RUNNING;
        } else {
            s->iface_valid = false;
            s->regs[TYPHOON_REG_STATUS / 4] = TYPHOON_STATUS_HALTED;
        }
        break;
    case TYPHOON_BOOTCMD_BOOT:
        /* Registration already starts the selected sleep/runtime image. */
        break;
    case TYPHOON_BOOTCMD_WAKEUP:
        s->regs[TYPHOON_REG_STATUS / 4] = TYPHOON_STATUS_WAITING_FOR_HOST;
        s->iface_valid = false;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "typhoon_pci: unknown boot command 0x%x\n", command);
        break;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        val = s->regs[addr / 4];
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case TYPHOON_REG_SOFT_RESET:
        if (val == TYPHOON_RESET_ALL) {
            s->regs[TYPHOON_REG_STATUS / 4] = TYPHOON_STATUS_WAITING_FOR_HOST;
            s->regs[TYPHOON_REG_INTR_STATUS / 4] = 0;
            s->regs[TYPHOON_REG_INTR_MASK / 4] = 0;
            s->regs[TYPHOON_REG_INTR_ENABLE / 4] = 0;
            s->cmd_consumer = 0;
            s->resp_producer = 0;
            s->iface_valid = false;
            pcibase_update_irq(s);
        }
        s->regs[TYPHOON_REG_SOFT_RESET / 4] = val;
        break;
    case TYPHOON_REG_INTR_MASK:
        s->regs[TYPHOON_REG_INTR_MASK / 4] = val;
        pcibase_update_irq(s);
        break;
    case TYPHOON_REG_INTR_ENABLE:
        s->regs[TYPHOON_REG_INTR_ENABLE / 4] = val;
        pcibase_update_irq(s);
        break;
    case TYPHOON_REG_INTR_STATUS:
        s->regs[TYPHOON_REG_INTR_STATUS / 4] &= ~val;
        pcibase_update_irq(s);
        break;
    case TYPHOON_REG_SELF_INTERRUPT:
        if (val) {
            s->regs[TYPHOON_REG_INTR_STATUS / 4] |= TYPHOON_INTR_SELF;
            pcibase_update_irq(s);
        }
        break;
    case TYPHOON_REG_COMMAND:
        s->regs[TYPHOON_REG_COMMAND / 4] = val;
        typhoon_boot_command(s, val);
        break;
    case TYPHOON_REG_CMD_READY:
        s->regs[TYPHOON_REG_CMD_READY / 4] = val;
        if (s->iface_valid &&
            s->regs[TYPHOON_REG_STATUS / 4] == TYPHOON_STATUS_RUNNING) {
            typhoon_process_commands(s, val);
        }
        break;
    case TYPHOON_REG_TX_LO_READY:
        s->regs[TYPHOON_REG_TX_LO_READY / 4] = val;
        if (s->iface_valid &&
            s->regs[TYPHOON_REG_STATUS / 4] == TYPHOON_STATUS_RUNNING) {
            typhoon_write_index(s,
                                offsetof(struct typhoon_indexes, txLoCleared),
                                val);
        }
        break;
    case TYPHOON_REG_TX_HI_READY:
        s->regs[TYPHOON_REG_TX_HI_READY / 4] = val;
        if (s->iface_valid &&
            s->regs[TYPHOON_REG_STATUS / 4] == TYPHOON_STATUS_RUNNING) {
            typhoon_write_index(s,
                                offsetof(struct typhoon_indexes, txHiCleared),
                                val);
        }
        break;
    default:
        if (addr < sizeof(s->regs)) {
            s->regs[addr / 4] = val;
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    memset(s->regs, 0, sizeof(s->regs));
    memset(&s->iface, 0, sizeof(s->iface));
    s->regs[TYPHOON_REG_STATUS / 4] = TYPHOON_STATUS_WAITING_FOR_HOST;
    s->boot_record_addr = 0;
    s->cmd_consumer = 0;
    s->resp_producer = 0;
    s->iface_valid = false;
    pci_set_irq(PCI_DEVICE(s), 0);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_3COM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_3COM_3CR990 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Stable, locally administered address returned by READ_MAC_ADDRESS. */
    s->macaddr[0] = 0x52;
    s->macaddr[1] = 0x54;
    s->macaddr[2] = 0x00;
    s->macaddr[3] = 0x12;
    s->macaddr[4] = 0x34;
    s->macaddr[5] = 0x56;

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 128;
    s->bar_info[0].name = "typhoon_io";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 128;
    s->bar_info[1].name = "typhoon_mmio";

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

}

static void pcibase_uninit(PCIDevice *pdev)
{
    // PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "typhoon_pci",
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
