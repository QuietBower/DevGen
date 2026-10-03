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

#define TYPE_PCIBASE_DEVICE "peak_pciefd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PEAK_PCI_VENDOR_ID	0x001c
#define PEAK_PCIEFD_ID		0x0013
#define PCIEFD_BAR0_SIZE		(64 * 1024)
#define PCIEFD_RX_DMA_SIZE		(4 * 1024)
#define PCIEFD_TX_DMA_SIZE		(4 * 1024)
#define PCIEFD_TX_PAGE_SIZE		(2 * 1024)
#define PCIEFD_TX_PAGE_COUNT	(PCIEFD_TX_DMA_SIZE / PCIEFD_TX_PAGE_SIZE)

#define PCIEFD_REG_SYS_CTL_SET		0x0000
#define PCIEFD_REG_SYS_CTL_CLR		0x0004
#define PCIEFD_REG_SYS_VER1		0x0040
#define PCIEFD_REG_SYS_VER2		0x0044

#define PCIEFD_CANX_OFF(c)		(((c) + 1) * 0x1000)
#define PCIEFD_REG_CAN_MISC		0x0000
#define PCIEFD_REG_CAN_CLK_SEL		0x0008
#define PCIEFD_REG_CAN_CMD_PORT_L	0x0010
#define PCIEFD_REG_CAN_CMD_PORT_H	0x0014
#define PCIEFD_REG_CAN_TX_REQ_ACC	0x0020
#define PCIEFD_REG_CAN_TX_CTL_SET	0x0030
#define PCIEFD_REG_CAN_TX_CTL_CLR	0x0038
#define PCIEFD_REG_CAN_TX_DMA_ADDR_L	0x0040
#define PCIEFD_REG_CAN_TX_DMA_ADDR_H	0x0044
#define PCIEFD_REG_CAN_RX_CTL_SET	0x0050
#define PCIEFD_REG_CAN_RX_CTL_CLR	0x0058
#define PCIEFD_REG_CAN_RX_CTL_WRT	0x0060
#define PCIEFD_REG_CAN_RX_CTL_ACK	0x0068
#define PCIEFD_REG_CAN_RX_DMA_ADDR_L	0x0070
#define PCIEFD_REG_CAN_RX_DMA_ADDR_H	0x0074

#define PCIEFD_SYS_CTL_CLK_EN		0x00000002
#define PCIEFD_SYS_CTL_TS_RST		0x00000001
#define CANFD_MISC_TS_RST		0x00000001
#define CANFD_CTL_RST_BIT		0x00020000
#define CANFD_CTL_UNC_BIT		0x00010000
#define CANFD_CTL_IEN_BIT		0x00040000
#define CANFD_CTL_IRQ_TL_DEF	10
#define CANFD_CTL_IRQ_CL_DEF	16
#define PUCAN_CMD_NORMAL_MODE		0x002
#define PUCAN_CMD_LISTEN_ONLY_MODE	0x003
#define PUCAN_CMD_RESET_MODE		0x001
#define CANFD_MSG_LNK_TX	0x1001

#define CANFD_CLK_SEL_SRC_240MHZ	0x00000008
#define CANFD_CLK_SEL_DIV_20MHZ		0x00000004
#define CANFD_CLK_SEL_DIV_24MHZ		0x00000003
#define CANFD_CLK_SEL_DIV_30MHZ		0x00000002
#define CANFD_CLK_SEL_DIV_40MHZ		0x00000001
#define CANFD_CLK_SEL_DIV_60MHZ		0x00000000

struct pucan_rx_msg {
    uint16_t size;
    uint16_t type;
    uint32_t ts_low;
    uint32_t ts_high;
    uint32_t tag_low;
    uint32_t tag_high;
    uint8_t  channel_dlc;
    uint8_t  client;
    uint16_t flags;
    uint32_t can_id;
    uint8_t  d[];
} QEMU_PACKED;

struct pucan_tx_msg {
    uint16_t size;
    uint16_t type;
    uint32_t tag_low;
    uint32_t tag_high;
    uint8_t  channel_dlc;
    uint8_t  client;
    uint16_t flags;
    uint32_t can_id;
    uint8_t  d[];
} QEMU_PACKED;

struct pucan_command {
    uint16_t opcode_channel;
    uint16_t args[3];
} QEMU_PACKED;

struct pciefd_rx_dma {
    uint32_t irq_status;
    uint32_t sys_time_low;
    uint32_t sys_time_high;
    struct pucan_rx_msg msg[];
} QEMU_PACKED;

struct pciefd_tx_link {
    uint16_t size;
    uint16_t type;
    uint32_t laddr_lo;
    uint32_t laddr_hi;
} QEMU_PACKED;

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
    uint32_t irq_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t sys_ctl;
    uint32_t sys_ver1;
    uint32_t sys_ver2;
    struct {
        uint32_t misc;
        uint32_t clk_sel;
        uint32_t cmd_port_l;
        uint32_t cmd_port_h;
        uint32_t tx_req_acc;
        uint32_t tx_ctl;
        uint32_t tx_dma_addr_l;
        uint32_t tx_dma_addr_h;
        uint32_t rx_ctl;
        uint32_t rx_dma_addr_l;
        uint32_t rx_dma_addr_h;
    } can[4];

    /* DMA Context */
    struct {
        dma_addr_t rx_dma_laddr;
        dma_addr_t tx_dma_laddr;
    } dma[4];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* To be implemented once DMA structures are available */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* To be implemented once DMA structures are available */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x1000) {
        switch (addr) {
        case PCIEFD_REG_SYS_CTL_SET:
        case PCIEFD_REG_SYS_CTL_CLR:
            val = s->sys_ctl;
            break;
        case PCIEFD_REG_SYS_VER1:
            val = s->sys_ver1;
            break;
        case PCIEFD_REG_SYS_VER2:
            val = s->sys_ver2;
            break;
        }
    } else {
        int c = (addr / 0x1000) - 1;
        if (c >= 0 && c < 4) {
            hwaddr offset = addr % 0x1000;
            switch (offset) {
            case PCIEFD_REG_CAN_MISC:
                val = s->can[c].misc;
                break;
            case PCIEFD_REG_CAN_CLK_SEL:
                val = s->can[c].clk_sel;
                break;
            case PCIEFD_REG_CAN_CMD_PORT_L:
                val = s->can[c].cmd_port_l;
                break;
            case PCIEFD_REG_CAN_CMD_PORT_H:
                val = s->can[c].cmd_port_h;
                break;
            case PCIEFD_REG_CAN_TX_REQ_ACC:
                val = s->can[c].tx_req_acc;
                break;
            case PCIEFD_REG_CAN_TX_CTL_SET:
            case PCIEFD_REG_CAN_TX_CTL_CLR:
                val = s->can[c].tx_ctl;
                break;
            case PCIEFD_REG_CAN_TX_DMA_ADDR_L:
                val = s->can[c].tx_dma_addr_l;
                break;
            case PCIEFD_REG_CAN_TX_DMA_ADDR_H:
                val = s->can[c].tx_dma_addr_h;
                break;
            case PCIEFD_REG_CAN_RX_CTL_SET:
            case PCIEFD_REG_CAN_RX_CTL_CLR:
            case PCIEFD_REG_CAN_RX_CTL_WRT:
                val = s->can[c].rx_ctl;
                break;
            case PCIEFD_REG_CAN_RX_DMA_ADDR_L:
                val = s->can[c].rx_dma_addr_l;
                break;
            case PCIEFD_REG_CAN_RX_DMA_ADDR_H:
                val = s->can[c].rx_dma_addr_h;
                break;
            }
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x1000) {
        switch (addr) {
        case PCIEFD_REG_SYS_CTL_SET:
            s->sys_ctl |= val;
            break;
        case PCIEFD_REG_SYS_CTL_CLR:
            s->sys_ctl &= ~val;
            break;
        }
    } else {
        int c = (addr / 0x1000) - 1;
        if (c >= 0 && c < 4) {
            hwaddr offset = addr % 0x1000;
            switch (offset) {
            case PCIEFD_REG_CAN_MISC:
                s->can[c].misc = val;
                break;
            case PCIEFD_REG_CAN_CLK_SEL:
                s->can[c].clk_sel = val;
                break;
            case PCIEFD_REG_CAN_CMD_PORT_L:
                s->can[c].cmd_port_l = val;
                break;
            case PCIEFD_REG_CAN_CMD_PORT_H:
                s->can[c].cmd_port_h = val;
                break;
            case PCIEFD_REG_CAN_TX_REQ_ACC:
                s->can[c].tx_req_acc = val;
                pcibase_do_dma(s, true);
                break;
            case PCIEFD_REG_CAN_TX_CTL_SET:
                s->can[c].tx_ctl |= val;
                break;
            case PCIEFD_REG_CAN_TX_CTL_CLR:
                s->can[c].tx_ctl &= ~val;
                break;
            case PCIEFD_REG_CAN_TX_DMA_ADDR_L:
                s->can[c].tx_dma_addr_l = val;
                s->dma[c].tx_dma_laddr = (s->dma[c].tx_dma_laddr & 0xFFFFFFFF00000000ULL) | val;
                break;
            case PCIEFD_REG_CAN_TX_DMA_ADDR_H:
                s->can[c].tx_dma_addr_h = val;
                s->dma[c].tx_dma_laddr = (s->dma[c].tx_dma_laddr & 0x00000000FFFFFFFFULL) | ((uint64_t)val << 32);
                break;
            case PCIEFD_REG_CAN_RX_CTL_SET:
                s->can[c].rx_ctl |= val;
                break;
            case PCIEFD_REG_CAN_RX_CTL_CLR:
                s->can[c].rx_ctl &= ~val;
                break;
            case PCIEFD_REG_CAN_RX_CTL_WRT:
                s->can[c].rx_ctl = val;
                break;
            case PCIEFD_REG_CAN_RX_CTL_ACK:
                /* ACK IRQ */
                break;
            case PCIEFD_REG_CAN_RX_DMA_ADDR_L:
                s->can[c].rx_dma_addr_l = val;
                s->dma[c].rx_dma_laddr = (s->dma[c].rx_dma_laddr & 0xFFFFFFFF00000000ULL) | val;
                break;
            case PCIEFD_REG_CAN_RX_DMA_ADDR_H:
                s->can[c].rx_dma_addr_h = val;
                s->dma[c].rx_dma_laddr = (s->dma[c].rx_dma_laddr & 0x00000000FFFFFFFFULL) | ((uint64_t)val << 32);
                break;
            }
        }
    }
}

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

    s->sys_ctl = 0;
    s->sys_ver1 = 1;
    s->sys_ver2 = 0x00003300; /* v3.3.0 */

    for (int i = 0; i < 4; i++) {
        s->can[i].misc = 0;
        s->can[i].clk_sel = 0;
        s->can[i].cmd_port_l = 0;
        s->can[i].cmd_port_h = 0;
        s->can[i].tx_req_acc = 0;
        s->can[i].tx_ctl = 0;
        s->can[i].tx_dma_addr_l = 0;
        s->can[i].tx_dma_addr_h = 0;
        s->can[i].rx_ctl = 0;
        s->can[i].rx_dma_addr_l = 0;
        s->can[i].rx_dma_addr_h = 0;
        s->dma[i].rx_dma_laddr = 0;
        s->dma[i].tx_dma_laddr = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PEAK_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PEAK_PCIEFD_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280 );
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
    s->bar_info[0].size = PCIEFD_BAR0_SIZE;
    s->bar_info[0].name = "pciefd-bar0";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
