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

#define TYPE_PCIBASE_DEVICE "peak_pciefd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIEFD_DRV_NAME "peak_pciefd"
#define PEAK_PCI_VENDOR_ID 0x001c
#define PEAK_PCIEFD_ID 0x0013
#define PCAN_CPCIEFD_ID 0x0014
#define PCAN_PCIE104FD_ID 0x0017
#define PCAN_MINIPCIEFD_ID 0x0018
#define PCAN_PCIEFD_OEM_ID 0x0019
#define PCAN_M2_ID 0x001a
#define PCIEFD_BAR0_SIZE (64 * 1024)
#define PCIEFD_RX_DMA_SIZE (4 * 1024)
#define PCIEFD_TX_DMA_SIZE (4 * 1024)
#define PCIEFD_TX_PAGE_SIZE (2 * 1024)
#define PCIEFD_REG_SYS_CTL_SET 0x0000
#define PCIEFD_REG_SYS_CTL_CLR 0x0004
#define PCIEFD_REG_SYS_VER1 0x0040
#define PCIEFD_REG_SYS_VER2 0x0044
#define PCIEFD_FW_VERSION(x, y, z) (((uint32_t)(x) << 24) | ((uint32_t)(y) << 16) | ((uint32_t)(z) << 8))
#define PCIEFD_SYS_CTL_TS_RST 0x00000001
#define PCIEFD_SYS_CTL_CLK_EN 0x00000002
#define PCIEFD_CANX_OFF(c) (((c) + 1) * 0x1000)
#define PCIEFD_ECHO_SKB_MAX PCANFD_ECHO_SKB_DEF
#define PCIEFD_REG_CAN_MISC 0x0000
#define PCIEFD_REG_CAN_CLK_SEL 0x0008
#define PCIEFD_REG_CAN_CMD_PORT_L 0x0010
#define PCIEFD_REG_CAN_CMD_PORT_H 0x0014
#define PCIEFD_REG_CAN_TX_REQ_ACC 0x0020
#define PCIEFD_REG_CAN_TX_CTL_SET 0x0030
#define PCIEFD_REG_CAN_TX_CTL_CLR 0x0038
#define PCIEFD_REG_CAN_TX_DMA_ADDR_L 0x0040
#define PCIEFD_REG_CAN_TX_DMA_ADDR_H 0x0044
#define PCIEFD_REG_CAN_RX_CTL_SET 0x0050
#define PCIEFD_REG_CAN_RX_CTL_CLR 0x0058
#define PCIEFD_REG_CAN_RX_CTL_WRT 0x0060
#define PCIEFD_REG_CAN_RX_CTL_ACK 0x0068
#define PCIEFD_REG_CAN_RX_DMA_ADDR_L 0x0070
#define PCIEFD_REG_CAN_RX_DMA_ADDR_H 0x0074
#define CANFD_MISC_TS_RST 0x00000001
#define CANFD_CLK_SEL_DIV_MASK 0x00000007
#define CANFD_CLK_SEL_DIV_60MHZ 0x00000000
#define CANFD_CLK_SEL_DIV_40MHZ 0x00000001
#define CANFD_CLK_SEL_DIV_30MHZ 0x00000002
#define CANFD_CLK_SEL_DIV_24MHZ 0x00000003
#define CANFD_CLK_SEL_DIV_20MHZ 0x00000004
#define CANFD_CLK_SEL_SRC_MASK 0x00000008
#define CANFD_CLK_SEL_SRC_240MHZ 0x00000008
#define CANFD_CLK_SEL_SRC_80MHZ (~CANFD_CLK_SEL_SRC_240MHZ & CANFD_CLK_SEL_SRC_MASK)
#define CANFD_CLK_SEL_20MHZ (CANFD_CLK_SEL_SRC_240MHZ | CANFD_CLK_SEL_DIV_20MHZ)
#define CANFD_CLK_SEL_24MHZ (CANFD_CLK_SEL_SRC_240MHZ | CANFD_CLK_SEL_DIV_24MHZ)
#define CANFD_CLK_SEL_30MHZ (CANFD_CLK_SEL_SRC_240MHZ | CANFD_CLK_SEL_DIV_30MHZ)
#define CANFD_CLK_SEL_40MHZ (CANFD_CLK_SEL_SRC_240MHZ | CANFD_CLK_SEL_DIV_40MHZ)
#define CANFD_CLK_SEL_60MHZ (CANFD_CLK_SEL_SRC_240MHZ | CANFD_CLK_SEL_DIV_60MHZ)
#define CANFD_CLK_SEL_80MHZ (CANFD_CLK_SEL_SRC_80MHZ)
#define CANFD_CTL_UNC_BIT 0x00010000
#define CANFD_CTL_RST_BIT 0x00020000
#define CANFD_CTL_IEN_BIT 0x00040000
#define CANFD_CTL_IRQ_CL_DEF 16
#define CANFD_CTL_IRQ_TL_DEF 10
#define PCIEFD_TX_PAGE_COUNT (PCIEFD_TX_DMA_SIZE / PCIEFD_TX_PAGE_SIZE)
#define CANFD_MSG_LNK_TX 0x1001
#define PCANFD_ECHO_SKB_DEF -1

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

/* Per-channel register layout */
typedef struct {
    uint32_t misc;
    uint32_t clk_sel;
    uint32_t cmd_port_l;
    uint32_t cmd_port_h;
    uint32_t tx_req_acc;
    uint32_t tx_ctl;     /* shadow, controlled via set/clr */
    uint32_t rx_ctl;     /* shadow, controlled via set/clr */
    uint32_t rx_ctl_wrt;
    uint32_t rx_ctl_ack;
    uint32_t tx_dma_addr_l;
    uint32_t tx_dma_addr_h;
    uint32_t rx_dma_addr_l;
    uint32_t rx_dma_addr_h;
} PCIEFDChanRegs;

/* Global registers */
typedef struct {
    uint32_t sys_ctl;    /* shadow */
    uint32_t sys_ver1;
    uint32_t sys_ver2;
} PCIEFDGlobalRegs;

/* DMA descriptor structures */
typedef struct {
    uint32_t irq_status;
    uint32_t sys_time_low;
    uint32_t sys_time_high;
    /* Variable-length message array follows */
    uint8_t msg[];
} PCIEFDRxDMA;

typedef struct {
    uint16_t size;
    uint16_t type;
    uint32_t laddr_lo;
    uint32_t laddr_hi;
} PCIEFDTxLink;

typedef struct {
    dma_addr_t rx_dma_addr;
    dma_addr_t tx_dma_addr;
    /* Additional DMA state */
} PCIEFDDMAState;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t irq_status;
    uint32_t irq_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    PCIEFDGlobalRegs global_regs;
    PCIEFDChanRegs *chan_regs;
    int can_count;

    /* DMA Context */
    PCIEFDDMAState dma;

    uint32_t status;

    bool in_reset;

};

/* Device-initiated DMA logic based on driver access patterns */
/* Not used during probe, so removed */

static void peak_pciefd_reset_device(PCIBaseState *s)
{
    int i;

    s->global_regs.sys_ctl = 0;
    /* sys_ver1 and sys_ver2 are fixed hardware version, not reset */

    for (i = 0; i < s->can_count; i++) {
        PCIEFDChanRegs *chan = &s->chan_regs[i];
        chan->misc = 0;
        chan->clk_sel = CANFD_CLK_SEL_80MHZ;
        chan->cmd_port_l = 0;
        chan->cmd_port_h = 0;
        chan->tx_req_acc = 0;
        chan->tx_ctl = 0;
        chan->rx_ctl = 0;
        chan->rx_ctl_wrt = 0;
        chan->rx_ctl_ack = 0;
        chan->tx_dma_addr_l = 0;
        chan->tx_dma_addr_h = 0;
        chan->rx_dma_addr_l = 0;
        chan->rx_dma_addr_h = 0;
    }

    s->irq_status = 0;
    s->status = 0;
}

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Legacy INTx only. Drive line high if irq_status != 0 */
    pci_set_irq(pdev, !!s->irq_status);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (addr < 0x1000) {
        /* Global registers */
        switch (addr) {
        case 0x0000: /* sys_ctl_set: return sys_ctl */
            val = s->global_regs.sys_ctl;
            break;
        case 0x0004: /* sys_ctl_clr: return sys_ctl */
            val = s->global_regs.sys_ctl;
            break;
        case 0x0040:
            val = s->global_regs.sys_ver1;
            break;
        case 0x0044:
            val = s->global_regs.sys_ver2;
            break;
        default:
            val = 0;
            break;
        }
    } else if (addr < 0x5000) {
        int ch = (addr >> 12) - 1;
        if (ch < s->can_count) {
            PCIEFDChanRegs *chan = &s->chan_regs[ch];
            uint32_t offset = addr & 0xFFF;
            switch (offset) {
            case 0x0000: val = chan->misc; break;
            case 0x0008: val = chan->clk_sel; break;
            case 0x0010: val = chan->cmd_port_l; break;
            case 0x0014: val = chan->cmd_port_h; break;
            case 0x0020: val = chan->tx_req_acc; break;
            case 0x0030: val = chan->tx_ctl; break;
            case 0x0038: val = chan->tx_ctl; break;
            case 0x0040: val = chan->tx_dma_addr_l; break;
            case 0x0044: val = chan->tx_dma_addr_h; break;
            case 0x0050: val = chan->rx_ctl; break;
            case 0x0058: val = chan->rx_ctl; break;
            case 0x0060: val = chan->rx_ctl_wrt; break;
            case 0x0068: val = chan->rx_ctl_ack; break;
            case 0x0070: val = chan->rx_dma_addr_l; break;
            case 0x0074: val = chan->rx_dma_addr_h; break;
            default: val = 0; break;
            }
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t value = val;

    if (addr < 0x1000) {
        /* Global registers */
        switch (addr) {
        case 0x0000: /* sys_ctl_set */
            s->global_regs.sys_ctl |= value;
            break;
        case 0x0004: /* sys_ctl_clr */
            s->global_regs.sys_ctl &= ~value;
            break;
        /* sys_ver1/2 are read-only, ignore writes */
        default:
            break;
        }
    } else if (addr < 0x5000) {
        int ch = (addr >> 12) - 1;
        if (ch >= s->can_count) {
            return;
        }
        PCIEFDChanRegs *chan = &s->chan_regs[ch];
        uint32_t offset = addr & 0xFFF;
        switch (offset) {
        case 0x0000: /* misc */
            chan->misc = value;
            break;
        case 0x0008: /* clk_sel */
            chan->clk_sel = value;
            break;
        case 0x0010: /* cmd_port_l */
            chan->cmd_port_l = value;
            break;
        case 0x0014: /* cmd_port_h */
            chan->cmd_port_h = value;
            break;
        case 0x0020: /* tx_req_acc */
            chan->tx_req_acc = value;
            break;
        case 0x0030: /* tx_ctl_set */
            chan->tx_ctl |= value;
            break;
        case 0x0038: /* tx_ctl_clr */
            chan->tx_ctl &= ~value;
            break;
        case 0x0040: /* tx_dma_addr_l */
            chan->tx_dma_addr_l = value;
            break;
        case 0x0044: /* tx_dma_addr_h */
            chan->tx_dma_addr_h = value;
            break;
        case 0x0050: /* rx_ctl_set */
            chan->rx_ctl |= value;
            break;
        case 0x0058: /* rx_ctl_clr */
            chan->rx_ctl &= ~value;
            break;
        case 0x0060: /* rx_ctl_wrt */
            chan->rx_ctl_wrt = value;
            break;
        case 0x0068: /* rx_ctl_ack */
            chan->rx_ctl_ack = value;
            break;
        case 0x0070: /* rx_dma_addr_l */
            chan->rx_dma_addr_l = value;
            break;
        case 0x0074: /* rx_dma_addr_h */
            chan->rx_dma_addr_h = value;
            break;
        default:
            break;
        }
    }
}

/* PIO handlers removed as driver uses MMIO only */

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    peak_pciefd_reset_device(s);
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
        /* PIO not used, but keep for completeness */
        error_setg(errp, "PIO BAR not supported");
        return;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PEAK_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PEAK_PCIEFD_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0000 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set subsystem IDs to emulate a 4-channel card (sub_sys_id = 0x0012) */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PEAK_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x0012);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PCIEFD_BAR0_SIZE;
    s->bar_info[0].name = "peak_pciefd_bar0";
    s->num_bars = 1;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X initialization; legacy INTx only */

    /* Field initialization: set default values for registers, allocate channel regs */
    s->can_count = 4; /* Subsystem ID 0x0012 -> 4 channels */
    s->chan_regs = g_new0(PCIEFDChanRegs, s->can_count);
    s->global_regs.sys_ver1 = 0x00000100;
    s->global_regs.sys_ver2 = 0x3300; /* Firmware version 3.3.0 */

    /* Reset all operational registers to default state */
    peak_pciefd_reset_device(s);
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

    g_free(s->chan_regs);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "peak_pciefd_pci",
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
