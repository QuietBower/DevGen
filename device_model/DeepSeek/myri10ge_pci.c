/*
 * QEMU PCI device model for Myricom Myri10GE (Z8E)
 * Generated from Linux driver myri10ge.c
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

#define TYPE_PCIBASE_DEVICE "myri10ge_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI Identification */
#define PCI_VENDOR_ID_MYRICOM 0x14c1
#define PCI_DEVICE_ID_MYRICOM_MYRI10GE_Z8E 0x0008
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Bar sizes */
#define BAR0_SIZE (16 * MiB)

/* Register Offsets (within BAR0) */
#define MXGEFW_ETH_CMD          0xf80000
#define MXGEFW_BOOT_DUMMY_RDMA  0xfc01c0
#define MXGEFW_BOOT_HANDOFF     0xfc0000
#define MXGEFW_ETH_SEND_STOP    0x3C0000
#define MXGEFW_ETH_SEND_GO      0x380000

#define MXGEFW_VERSION_MINOR    4
#define MXGEFW_VERSION_MAJOR    1
#define MXGEFW_PAD              2

#define MXGEFW_LINK_UP          1
#define MXGEFW_LINK_MYRINET     2
#define MXGEFW_LINK_DOWN        0
#define MXGEFW_LINK_UNKNOWN     3

#define MXGEFW_FLAGS_TSO_HDR    0x1
#define MXGEFW_FLAGS_NO_TSO     0x10
#define MXGEFW_FLAGS_CKSUM      0x8
#define MXGEFW_FLAGS_FIRST      0x2
#define MXGEFW_FLAGS_TSO_LAST   0x8
#define MXGEFW_FLAGS_SMALL      0x1
#define MXGEFW_FLAGS_ALIGN_ODD  0x4
#define MXGEFW_FLAGS_TSO_CHOP   0x10
#define MXGEFW_FLAGS_TSO_PLD    0x20

#define MXGEFW_MAX_SEND_DESC    12
#define MXGEFW_SEND_SMALL_SIZE  1520
#define MXGEFW_RSS_HASH_TYPE_MAX 0x5
#define MXGEFW_RSS_HASH_TYPE_SRC_PORT 0x4

#define MCP_TYPE_ETH            0x45544820
#define MCP_HEADER_PTR_OFFSET   0x3c
#define MXGEFW_SLICE_ENABLE_MULTIPLE_TX_QUEUES 0x2
#define MXGEFW_RSS_MCP_SLOT_TYPE_MIN 0
#define MXGEFW_SLICE_INTR_MODE_ONE_PER_SLICE 0x1

#define MYRI10GE_MAX_SLICES 32
#define MYRI10GE_MAX_BOARDS 8

#define FW_HEADER_SIZE 0x100
#define EEPROM_SIZE 256
#define EEPROM_BASE  0x200000  /* must match sram_size set in fw_header */

#define MXGEFW_CMD_UNKNOWN 0xffffffff

/* Command codes (incomplete list) */
#define MXGEFW_CMD_RESET           0x0001
#define MXGEFW_CMD_GET_RX_RING_SIZE 0x0002
#define MXGEFW_CMD_GET_MAX_TSO6_HDR_SIZE 0x0003
#define MXGEFW_CMD_SET_INTRQ_SIZE  0x0004
#define MXGEFW_CMD_GET_MAX_RSS_QUEUES 0x0005
#define MXGEFW_CMD_ENABLE_RSS_QUEUES 0x0006
#define MXGEFW_CMD_SET_INTRQ_DMA   0x0007
#define MXGEFW_CMD_GET_IRQ_ACK_OFFSET 0x0008
#define MXGEFW_CMD_GET_IRQ_DEASSERT_OFFSET 0x0009
#define MXGEFW_CMD_GET_INTR_COAL_DELAY_OFFSET 0x000A
#define MXGEFW_CMD_GET_SEND_RING_SIZE 0x000B
#define MXGEFW_CMD_GET_SEND_OFFSET 0x000C
#define MXGEFW_CMD_GET_SMALL_RX_OFFSET 0x000D
#define MXGEFW_CMD_GET_BIG_RX_OFFSET 0x000E
#define MXGEFW_CMD_SET_STATS_DMA_V2 0x000F
#define MXGEFW_CMD_SET_STATS_DMA_OBSOLETE 0x0010
#define MXGEFW_CMD_ETHERNET_UP     0x0011
#define MXGEFW_CMD_ETHERNET_DOWN   0x0012
#define MXGEFW_CMD_SET_MTU         0x0013
#define MXGEFW_CMD_SET_SMALL_BUFFER_SIZE 0x0014
#define MXGEFW_CMD_SET_BIG_BUFFER_SIZE 0x0015
#define MXGEFW_CMD_SET_TSO_MODE    0x0016
#define MXGEFW_CMD_SET_RSS_TABLE_SIZE 0x0017
#define MXGEFW_CMD_GET_RSS_TABLE_OFFSET 0x0018
#define MXGEFW_CMD_SET_RSS_ENABLE  0x0019
#define MXGEFW_CMD_SET_MAC_ADDRESS 0x001A
#define MXGEFW_ENABLE_FLOW_CONTROL 0x001B
#define MXGEFW_DISABLE_FLOW_CONTROL 0x001C
#define MXGEFW_ENABLE_PROMISC      0x001D
#define MXGEFW_DISABLE_PROMISC     0x001E
#define MXGEFW_ENABLE_ALLMULTI     0x001F
#define MXGEFW_DISABLE_ALLMULTI    0x0020
#define MXGEFW_LEAVE_ALL_MULTICAST_GROUPS 0x0021
#define MXGEFW_JOIN_MULTICAST_GROUP 0x0022
#define MXGEFW_CMD_SET_RSS_MCP_SLOT_TYPE 0x0023
#define MXGEFW_CMD_GET_DCA_OFFSET  0x0024

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
    uint32_t intr_coal_delay;
    uint32_t irq_claim;
    uint32_t irq_deassert;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Eth command (offset 0xf80000) - layout matches mcp_cmd */
    struct {
        uint32_t cmd;
        uint32_t data0;
        uint32_t data1;
        uint32_t data2;
        uint32_t response_addr_low;
        uint32_t response_addr_high;
        uint8_t pad[40];
    } eth_cmd;
    /* Send go/stop (offsets 0x380000, 0x3C0000) */
    uint32_t send_go;
    uint32_t send_stop;
    /* Boot handoff (0xfc0000) */
    uint32_t boot_handoff;
    /* Dummy RDMA (0xfc01c0) */
    uint32_t boot_dummy_rdma;
    uint32_t rdma_tags_available;
    uint32_t link_state;

    /* DMA Context */
    dma_addr_t cmd_bus;
    dma_addr_t tx_ring_bus;
    dma_addr_t rx_small_ring_bus;
    dma_addr_t rx_big_ring_bus;
    dma_addr_t fw_stats_bus;

    /* Power management state */
    uint32_t power_state;

    /* Operational status flags */
    /* Probe/Reset state (if needed) */
    
    /* Firmware emulation */
    uint8_t fw_header[FW_HEADER_SIZE];
    uint8_t eeprom[EEPROM_SIZE];
    
    /* Boot buffers */
    uint32_t boot_buf[7];
    uint32_t rdma_buf[6];
    
    /* Timer for command processing */
    QEMUTimer *cmd_timer;
};

/* Command processing timer callback */
static void pcibase_cmd_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t cmd = s->eth_cmd.cmd;
    dma_addr_t resp_addr = ((uint64_t)s->eth_cmd.response_addr_high << 32) | s->eth_cmd.response_addr_low;
    uint32_t result = 0, data = 0;

    switch (cmd) {
    case MXGEFW_CMD_RESET:
        result = 0;
        break;
    case MXGEFW_CMD_GET_RX_RING_SIZE:
        data = 0x1000;
        result = 0;
        break;
    case MXGEFW_CMD_GET_MAX_TSO6_HDR_SIZE:
        data = 0;
        result = 0;
        break;
    case MXGEFW_CMD_SET_INTRQ_SIZE:
        result = 0;
        break;
    case MXGEFW_CMD_GET_MAX_RSS_QUEUES:
        data = 1;
        result = 0;
        break;
    case MXGEFW_CMD_ENABLE_RSS_QUEUES:
        result = 0;
        break;
    case MXGEFW_CMD_SET_INTRQ_DMA:
        result = 0;
        break;
    case MXGEFW_CMD_GET_IRQ_ACK_OFFSET:
        data = 0x80000;
        result = 0;
        break;
    case MXGEFW_CMD_GET_IRQ_DEASSERT_OFFSET:
        data = 0x80020;
        result = 0;
        break;
    case MXGEFW_CMD_GET_INTR_COAL_DELAY_OFFSET:
        data = 0x80040;
        result = 0;
        break;
    case MXGEFW_CMD_GET_SEND_RING_SIZE:
        data = 0x1000;
        result = 0;
        break;
    case MXGEFW_CMD_GET_SEND_OFFSET:
        data = 0x90000 + (s->eth_cmd.data0 * 0x1000);
        result = 0;
        break;
    case MXGEFW_CMD_GET_SMALL_RX_OFFSET:
        data = 0xA0000 + (s->eth_cmd.data0 * 0x1000);
        result = 0;
        break;
    case MXGEFW_CMD_GET_BIG_RX_OFFSET:
        data = 0xB0000 + (s->eth_cmd.data0 * 0x1000);
        result = 0;
        break;
    case MXGEFW_CMD_SET_STATS_DMA_V2:
    case MXGEFW_CMD_SET_STATS_DMA_OBSOLETE:
        result = 0;
        break;
    case MXGEFW_CMD_ETHERNET_UP:
    case MXGEFW_CMD_ETHERNET_DOWN:
        result = 0;
        break;
    case MXGEFW_CMD_SET_MTU:
    case MXGEFW_CMD_SET_SMALL_BUFFER_SIZE:
    case MXGEFW_CMD_SET_BIG_BUFFER_SIZE:
    case MXGEFW_CMD_SET_TSO_MODE:
        result = 0;
        break;
    case MXGEFW_CMD_SET_RSS_TABLE_SIZE:
        result = 0;
        break;
    case MXGEFW_CMD_GET_RSS_TABLE_OFFSET:
        data = 0xC0000;
        result = 0;
        break;
    case MXGEFW_CMD_SET_RSS_ENABLE:
        result = 0;
        break;
    case MXGEFW_CMD_SET_MAC_ADDRESS:
        result = 0;
        break;
    case MXGEFW_ENABLE_FLOW_CONTROL:
    case MXGEFW_DISABLE_FLOW_CONTROL:
    case MXGEFW_ENABLE_PROMISC:
    case MXGEFW_DISABLE_PROMISC:
    case MXGEFW_ENABLE_ALLMULTI:
    case MXGEFW_DISABLE_ALLMULTI:
    case MXGEFW_LEAVE_ALL_MULTICAST_GROUPS:
    case MXGEFW_JOIN_MULTICAST_GROUP:
        result = 0;
        break;
    case MXGEFW_CMD_SET_RSS_MCP_SLOT_TYPE:
        result = 0;
        break;
    case MXGEFW_CMD_GET_DCA_OFFSET:
        result = MXGEFW_CMD_UNKNOWN;
        break;
    default:
        result = MXGEFW_CMD_UNKNOWN;
        break;
    }

    /* Write response in mcp_cmd_response layout: data then result */
    uint32_t response[2];
    response[0] = htonl(data);
    response[1] = htonl(result);
    pci_dma_write(pdev, resp_addr, response, sizeof(response));
}

/* Handoff processing */
static void pcibase_handoff(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t bus_addr = ((uint64_t)s->boot_buf[0] << 32) | s->boot_buf[1];
    uint32_t val = 0;
    pci_dma_write(pdev, bus_addr + 4, &val, sizeof(val));
}

/* Dummy RDMA processing */
static void pcibase_dummy_rdma(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t bus_addr = ((uint64_t)s->rdma_buf[0] << 32) | s->rdma_buf[1];
    uint32_t val = 0;
    pci_dma_write(pdev, bus_addr + 4, &val, sizeof(val));
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x100 && addr < 0x200) {
        /* fake firmware header area */
        uint32_t offset = addr - 0x100;
        if (offset < FW_HEADER_SIZE) {
            val = ldl_be_p(&s->fw_header[offset]);
        }
        return val;
    }
    if (addr >= EEPROM_BASE && addr < EEPROM_BASE + EEPROM_SIZE) {
        /* eeprom area: mapped at sram_size (0x200000) to match driver expectation */
        uint32_t offset = addr - EEPROM_BASE;
        if (offset + size <= EEPROM_SIZE) {
            val = 0;
            memcpy(&val, &s->eeprom[offset], size);
        }
        return val;
    }
    switch (addr) {
    case 0x3c: /* MCP_HEADER_PTR_OFFSET */
        val = 0x00010000; /* pre-swapped 0x100 */
        break;
    case MXGEFW_ETH_CMD ... MXGEFW_ETH_CMD + 0x17: /* command area */
        {
            uint32_t off = (addr - MXGEFW_ETH_CMD) / 4;
            uint32_t *ptr = (uint32_t *)&s->eth_cmd;
            if (off < 6) {
                val = ptr[off];
            }
        }
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Command area */
    if (addr >= MXGEFW_ETH_CMD && addr < MXGEFW_ETH_CMD + 0x18) {
        uint32_t off = (addr - MXGEFW_ETH_CMD) / 4;
        if (off < 6) {
            ((uint32_t *)&s->eth_cmd)[off] = val;
            if (off == 5) { /* last word (response_addr_high) */
                timer_mod(s->cmd_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
            }
        }
        return;
    }

    /* Boot handoff */
    if (addr >= MXGEFW_BOOT_HANDOFF && addr < MXGEFW_BOOT_HANDOFF + 0x1c) {
        int idx = (addr - MXGEFW_BOOT_HANDOFF) / 4;
        if (idx < 7) {
            s->boot_buf[idx] = val;
            if (idx == 6) {
                pcibase_handoff(s);
            }
        }
        return;
    }

    /* Dummy RDMA */
    if (addr >= MXGEFW_BOOT_DUMMY_RDMA && addr < MXGEFW_BOOT_DUMMY_RDMA + 0x14) {
        int idx = (addr - MXGEFW_BOOT_DUMMY_RDMA) / 4;
        if (idx < 6) {
            s->rdma_buf[idx] = val;
            if (idx == 5) {
                pcibase_dummy_rdma(s);
            }
        }
        return;
    }

    /* send_go, send_stop, irq_claim, irq_deassert - ignored */
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
    pci_device_reset(PCI_DEVICE(dev));
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_MYRICOM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_MYRICOM_MYRI10GE_Z8E);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: only BAR0 */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "myri10ge-mmio";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* MSI-X initialization using exclusive bar (QEMU 8.2.10) */
    if (msix_init_exclusive_bar(pdev, 1, 1, errp)) {
        return;
    }

    /* Initialize fake firmware header */
    memset(s->fw_header, 0, sizeof(s->fw_header));
    /* header_length at offset 0: 256 (0x100) big-endian */
    s->fw_header[0] = 0x00;
    s->fw_header[1] = 0x00;
    s->fw_header[2] = 0x01;
    s->fw_header[3] = 0x00;
    /* mcp_type at offset 4: MCP_TYPE_ETH (0x45544820) stored big-endian */
    s->fw_header[4] = 0x45;
    s->fw_header[5] = 0x54;
    s->fw_header[6] = 0x48;
    s->fw_header[7] = 0x20;
    /* version at offset 8: "1.4.0" */
    strcpy((char *)&s->fw_header[8], "1.4.0");
    /* sram_size at offset 140 (driver reads from here via offsetof(string_specs) bug) */
    /* Must be > MYRI10GE_FW_OFFSET (1MB) and <= board_span (16MB). Set to 2MB (0x200000) */
    /* Store as big-endian bytes so that ldl_be_p returns little-endian 0x00002000, */
    /* which after driver swab32 yields 0x00200000. */
    s->fw_header[140] = 0x00;
    s->fw_header[141] = 0x00;
    s->fw_header[142] = 0x20;
    s->fw_header[143] = 0x00;

    /* Initialize fake eeprom strings */
    memset(s->eeprom, 0, sizeof(s->eeprom));
    strcpy((char *)s->eeprom, "MAC=00:0c:29:12:34:56\nPC=1\nSN=123\n");

    /* Initialize command timer */
    s->cmd_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_cmd_timer, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(s->cmd_timer);
    timer_free(s->cmd_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "myri10ge_pci",
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
