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

#define TYPE_PCIBASE_DEVICE "smsc9420_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ID_REV				(0xC0)
#define MII_ACCESS_MII_BUSY_		(0x00000001)
#define MII_ACCESS			(0x94)
#define MII_ACCESS_MII_READ_		(0x00000000)
#define MII_DATA			(0x98)
#define MII_ACCESS_MII_WRITE_		(0x00000002)
#define E2P_CMD				(0xF8)
#define E2P_CMD_EPC_BUSY_		(0x80000000)
#define E2P_CMD_EPC_CMD_RELOAD_		(0x70000000)
#define GPIO_CFG_EEPR_EN_		(0x00700000)
#define GPIO_CFG			(0xD0)
#define E2P_CMD_EPC_TIMEOUT_		(0x00000200)
#define E2P_DATA			(0xFC)
#define E2P_CMD_EPC_CMD_READ_		(0x00000000)
#define E2P_CMD_EPC_CMD_ERASE_		(0x50000000)
#define E2P_CMD_EPC_CMD_WRITE_		(0x30000000)
#define SMSC9420_EEPROM_SIZE		((u32)11)
#define SMSC9420_EEPROM_MAGIC		(0x9420)
#define E2P_CMD_EPC_CMD_EWEN_		(0x20000000)
#define E2P_CMD_EPC_CMD_EWDS_		(0x10000000)
#define ADDRL				(0x88)
#define ADDRH				(0x84)
#define DMAC_CONTROL_ST_		(BIT(13))
#define DMAC_STS_TXPS_			(BIT(1))
#define DMAC_INTR_ENA_TX_		(BIT(0))
#define MAC_CR_TXEN_			(0x00000008)
#define MAC_CR				(0x80)
#define DMAC_CONTROL			(0x18)
#define DMAC_STATUS			(0x14)
#define DMAC_STS_TS_			(7 << 20)
#define DMAC_INTR_ENA			(0x1C)
#define TX_RING_SIZE			(32)
#define RX_RING_SIZE			(128)
#define PKT_BUF_SZ			(VLAN_ETH_FRAME_LEN + NET_IP_ALIGN + 4)
#define DMAC_STS_RS_ 			(7 << 17)
#define DMAC_CONTROL_SR_		(BIT(1))
#define DMAC_STS_RXPS_			(BIT(8))
#define DMAC_INTR_ENA_RX_		(BIT(6))
#define MAC_CR_RXEN_			(0x00000004)
#define INT_CTL_SW_INT_EN_		(0x00008000)
#define INT_STAT_SW_INT_		(1 << 15)
#define INT_CFG_IRQ_INT_		(0x00080000)
#define INT_STAT			(0xC8)
#define INT_STAT_DMAC_INT_		(0x00000001)
#define DMAC_STS_NIS_			(BIT(16))
#define INT_CTL				(0xC4)
#define INT_CFG_IRQ_EN_			(0x00040000)
#define DMAC_STS_RX_			(BIT(6))
#define DMAC_STS_TX_			(BIT(0))
#define INT_CFG				(0xCC)
#define BUS_MODE_SWR_			(BIT(0))
#define BUS_MODE			(0x00)
#define RDES0_LENGTH_ERROR_		(0x00001000)
#define RDES0_FRAME_TOO_LONG_		(0x00000080)
#define RDES0_RUNT_FRAME_		(0x00000800)
#define RDES0_DESCRIPTOR_ERROR_		(0x00004000)
#define RDES0_ERROR_SUMMARY_		(0x00008000)
#define RDES0_COLLISION_SEEN_		(0x00000040)
#define RDES0_CRC_ERROR_		(0x00000002)
#define RDES0_FIRST_DESCRIPTOR_		(0x00000200)
#define RDES0_MULTICAST_FRAME_		(0x00000400)
#define RDES0_LAST_DESCRIPTOR_		(0x00000100)
#define RDES0_FRAME_LENGTH_MASK_	(0x07FF0000)
#define RDES0_FRAME_LENGTH_SHFT_	(16)
#define RDES0_OWN_			(0x80000000)
#define DMAC_INTR_ENA_NIS_		(BIT(16))
#define MISS_FRAME_CNTR			(0x20)
#define RX_POLL_DEMAND			(0x08)
#define TDES0_LOSS_OF_CARRIER_		(0x00000800)
#define TDES0_HEARTBEAT_FAIL_		(0x00000080)
#define TDES0_COLLISION_COUNT_MASK_	(0x00000078)
#define TDES0_NO_CARRIER_		(0x00000400)
#define TDES0_EXCESSIVE_DEFERRAL_	(0x00000004)
#define TDES0_COLLISION_COUNT_SHFT_	(3)
#define TDES0_ERROR_SUMMARY_		(0x00008000)
#define TDES0_EXCESSIVE_COLLISIONS_	(0x00000100)
#define TDES0_OWN_			(0x80000000)
#define TDES1_LS_			0x40000000
#define TDES1_IC_			0x80000000
#define TX_POLL_DEMAND			(0x04)
#define TDES1_TER_			(BIT(25))
#define HASHH				(0x8C)
#define MAC_CR_PRMS_			(0x00040000)
#define MAC_CR_HPFILT_			(0x00002000)
#define MAC_CR_MCPAS_			(0x00080000)
#define HASHL				(0x90)
#define FLOW				(0x9C)
#define MAC_CR_FDPX_			(0x00100000)
#define TX_BASE_ADDR			(0x10)
#define RDES1_RER_			(0x02000000)
#define COE_CR				(0xB0)
#define RX_COE_EN			(0x00000001)
#define VLAN1				(0xA0)
#define RX_BASE_ADDR			(0x0C)
#define BUS_MODE_DBO_			(BIT(20))
#define DMAC_CONTROL_SF_		(BIT(21))
#define INT_DEAS_TIME			(50)
#define BUS_MODE_DMA_BURST_LENGTH_16	(BIT(12))
#define BUS_CFG				(0xDC)
#define DMAC_CONTROL_OSF_		(BIT(2))
#define GPIO_CFG_LED_1_			(0x10000000)
#define INT_CFG_INT_DEAS_MASK		(0x000000FF)
#define GPIO_CFG_LED_2_			(0x20000000)
#define GPIO_CFG_LED_3_			(0x40000000)
#define BUS_CFG_RXTXWEIGHT_4_1_		(3 << 25)
#define LAN9420_CPSR_ENDIAN_OFFSET	(0)
#define SMSC_BAR			(3)
#define PCI_DEVICE_ID_9420		(0xE420)
#define PCI_VENDOR_ID_9420		(0x1055)

#define BAR3_SIZE 0x100 /* register space size */
#define CLASS_ID 0x0200 /* network controller */

/* Hardware register layout */
typedef struct Smsc9420Regs {
    uint32_t bus_mode;          /* 0x00 */
    uint32_t tx_poll_demand;    /* 0x04 */
    uint32_t rx_poll_demand;    /* 0x08 */
    uint32_t rx_base_addr;      /* 0x0C */
    uint32_t tx_base_addr;      /* 0x10 */
    uint32_t dmac_status;       /* 0x14 */
    uint32_t dmac_control;      /* 0x18 */
    uint32_t dmac_intr_ena;     /* 0x1C */
    uint32_t miss_frame_cntr;   /* 0x20 */
    uint32_t reserved_0x24;
    uint32_t reserved_0x28;
    uint32_t reserved_0x2C;
    uint32_t reserved_0x30;
    uint32_t reserved_0x34;
    uint32_t reserved_0x38;
    uint32_t reserved_0x3C;
    uint32_t reserved_0x40;
    uint32_t reserved_0x44;
    uint32_t reserved_0x48;
    uint32_t reserved_0x4C;
    uint32_t reserved_0x50;
    uint32_t reserved_0x54;
    uint32_t reserved_0x58;
    uint32_t reserved_0x5C;
    uint32_t reserved_0x60;
    uint32_t reserved_0x64;
    uint32_t reserved_0x68;
    uint32_t reserved_0x6C;
    uint32_t reserved_0x70;
    uint32_t reserved_0x74;
    uint32_t reserved_0x78;
    uint32_t reserved_0x7C;
    uint32_t mac_cr;            /* 0x80 */
    uint32_t addrh;             /* 0x84 */
    uint32_t addrl;             /* 0x88 */
    uint32_t hashh;             /* 0x8C */
    uint32_t hashl;             /* 0x90 */
    uint32_t mii_access;        /* 0x94 */
    uint32_t mii_data;          /* 0x98 */
    uint32_t flow;              /* 0x9C */
    uint32_t vlan1;             /* 0xA0 */
    uint32_t reserved_0xA4;
    uint32_t reserved_0xA8;
    uint32_t reserved_0xAC;
    uint32_t coe_cr;            /* 0xB0 */
    uint32_t reserved_0xB4;
    uint32_t reserved_0xB8;
    uint32_t reserved_0xBC;
    uint32_t id_rev;            /* 0xC0 */
    uint32_t int_ctl;           /* 0xC4 */
    uint32_t int_stat;          /* 0xC8 */
    uint32_t int_cfg;           /* 0xCC */
    uint32_t gpio_cfg;          /* 0xD0 */
    uint32_t reserved_0xD4;
    uint32_t reserved_0xD8;
    uint32_t bus_cfg;           /* 0xDC */
    uint32_t reserved_0xE0;
    uint32_t reserved_0xE4;
    uint32_t reserved_0xE8;
    uint32_t reserved_0xEC;
    uint32_t reserved_0xF0;
    uint32_t reserved_0xF4;
    uint32_t e2p_cmd;           /* 0xF8 */
    uint32_t e2p_data;          /* 0xFC */
} Smsc9420Regs;

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    Smsc9420Regs regs;

    /* DMA Context */

    /* Operational status flags */

    /* State used to handle reset sequences */

    /* Power management state (D0-D3) */

};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Not used during probe */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only 32-bit accesses expected by driver */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case BUS_MODE:         /* 0x00 */
        val = s->regs.bus_mode;
        break;
    case TX_POLL_DEMAND:   /* 0x04 */
        val = s->regs.tx_poll_demand;
        break;
    case RX_POLL_DEMAND:   /* 0x08 */
        val = s->regs.rx_poll_demand;
        break;
    case RX_BASE_ADDR:     /* 0x0C */
        val = s->regs.rx_base_addr;
        break;
    case TX_BASE_ADDR:     /* 0x10 */
        val = s->regs.tx_base_addr;
        break;
    case DMAC_STATUS:      /* 0x14 */
        val = s->regs.dmac_status;
        break;
    case DMAC_CONTROL:     /* 0x18 */
        val = s->regs.dmac_control;
        break;
    case DMAC_INTR_ENA:    /* 0x1C */
        val = s->regs.dmac_intr_ena;
        break;
    case MISS_FRAME_CNTR:  /* 0x20 */
        val = s->regs.miss_frame_cntr;
        break;
    case MAC_CR:           /* 0x80 */
        val = s->regs.mac_cr;
        break;
    case ADDRH:            /* 0x84 */
        val = s->regs.addrh;
        break;
    case ADDRL:            /* 0x88 */
        val = s->regs.addrl;
        break;
    case HASHH:            /* 0x8C */
        val = s->regs.hashh;
        break;
    case HASHL:            /* 0x90 */
        val = s->regs.hashl;
        break;
    case MII_ACCESS:       /* 0x94 */
        val = s->regs.mii_access;
        break;
    case MII_DATA:         /* 0x98 */
        val = s->regs.mii_data;
        break;
    case FLOW:             /* 0x9C */
        val = s->regs.flow;
        break;
    case VLAN1:            /* 0xA0 */
        val = s->regs.vlan1;
        break;
    case COE_CR:           /* 0xB0 */
        val = s->regs.coe_cr;
        break;
    case ID_REV:           /* 0xC0 */
        val = s->regs.id_rev;
        break;
    case INT_CTL:          /* 0xC4 */
        val = s->regs.int_ctl;
        break;
    case INT_STAT:         /* 0xC8 */
        val = s->regs.int_stat;
        break;
    case INT_CFG:          /* 0xCC */
        val = s->regs.int_cfg;
        break;
    case GPIO_CFG:         /* 0xD0 */
        val = s->regs.gpio_cfg;
        break;
    case BUS_CFG:          /* 0xDC */
        val = s->regs.bus_cfg;
        break;
    case E2P_CMD:          /* 0xF8 */
        val = s->regs.e2p_cmd;
        break;
    case E2P_DATA:         /* 0xFC */
        val = s->regs.e2p_data;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case BUS_MODE:         /* 0x00 */
        /* Clear self-clearing bits (e.g., SWR) */
        s->regs.bus_mode = val & ~BUS_MODE_SWR_;
        break;
    case TX_POLL_DEMAND:   /* 0x04 */
        s->regs.tx_poll_demand = val;
        break;
    case RX_POLL_DEMAND:   /* 0x08 */
        s->regs.rx_poll_demand = val;
        break;
    case RX_BASE_ADDR:     /* 0x0C */
        s->regs.rx_base_addr = val;
        break;
    case TX_BASE_ADDR:     /* 0x10 */
        s->regs.tx_base_addr = val;
        break;
    case DMAC_STATUS:      /* 0x14 */
        /* Write-1-to-clear bits */
        s->regs.dmac_status &= ~(val & (DMAC_STS_TXPS_ | DMAC_STS_RXPS_ |
                                       DMAC_STS_TX_ | DMAC_STS_RX_ |
                                       DMAC_STS_NIS_));
        break;
    case DMAC_CONTROL:     /* 0x18 */
        s->regs.dmac_control = val;
        break;
    case DMAC_INTR_ENA:    /* 0x1C */
        s->regs.dmac_intr_ena = val;
        break;
    case MAC_CR:           /* 0x80 */
        s->regs.mac_cr = val;
        break;
    case ADDRH:            /* 0x84 */
        s->regs.addrh = val;
        break;
    case ADDRL:            /* 0x88 */
        s->regs.addrl = val;
        break;
    case HASHH:            /* 0x8C */
        s->regs.hashh = val;
        break;
    case HASHL:            /* 0x90 */
        s->regs.hashl = val;
        break;
    case MII_ACCESS:       /* 0x94 */
        s->regs.mii_access = val;
        break;
    case MII_DATA:         /* 0x98 */
        s->regs.mii_data = val;
        break;
    case FLOW:             /* 0x9C */
        s->regs.flow = val;
        break;
    case VLAN1:            /* 0xA0 */
        s->regs.vlan1 = val;
        break;
    case COE_CR:           /* 0xB0 */
        s->regs.coe_cr = val;
        break;
    case INT_CTL:          /* 0xC4 */
        s->regs.int_ctl = val;
        break;
    case INT_STAT:         /* 0xC8 */
        /* Write-1-to-clear for SW_INT */
        if (val & INT_STAT_SW_INT_) {
            s->regs.int_stat &= ~INT_STAT_SW_INT_;
        }
        break;
    case INT_CFG:          /* 0xCC */
        s->regs.int_cfg = val;
        break;
    case GPIO_CFG:         /* 0xD0 */
        s->regs.gpio_cfg = val;
        break;
    case BUS_CFG:          /* 0xDC */
        s->regs.bus_cfg = val;
        break;
    case E2P_CMD:          /* 0xF8 */
        s->regs.e2p_cmd = val;
        /* Handle reload command */
        if (val & E2P_CMD_EPC_CMD_RELOAD_) {
            /* Simulate immediate completion with no error */
            s->regs.e2p_cmd = 0;
            /* Load MAC address from "EEPROM" */
            s->regs.addrl = 0x05040302;
            s->regs.addrh = 0x00000706;
        }
        break;
    case E2P_DATA:         /* 0xFC */
        s->regs.e2p_data = val;
        break;
    default:
        break;
    }
}

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

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.id_rev = 0x94200000;  /* Identify as LAN9420 */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_9420);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_9420);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
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
    s->bar_info[0].index = SMSC_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR3_SIZE;
    s->bar_info[0].name = "smsc9420-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X used by driver */
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
    .name = "smsc9420_pci",
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
