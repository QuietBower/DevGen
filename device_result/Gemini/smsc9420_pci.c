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

#define TYPE_PCIBASE_DEVICE "smsc9420_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_9420 0x1055
#define PCI_DEVICE_ID_9420 0xE420
#define SMSC_BAR 3

#define ID_REV 0xC0
#define MII_ACCESS 0x94
#define MII_DATA 0x98
#define E2P_CMD 0xF8
#define E2P_DATA 0xFC
#define GPIO_CFG 0xD0
#define ADDRL 0x88
#define ADDRH 0x84
#define MAC_CR 0x80
#define DMAC_CONTROL 0x18
#define DMAC_STATUS 0x14
#define DMAC_INTR_ENA 0x1C
#define INT_STAT 0xC8
#define INT_CTL 0xC4
#define INT_CFG 0xCC
#define BUS_MODE 0x00
#define MISS_FRAME_CNTR 0x20
#define RX_POLL_DEMAND 0x08
#define TX_POLL_DEMAND 0x04
#define HASHH 0x8C
#define HASHL 0x90
#define FLOW 0x9C
#define TX_BASE_ADDR 0x10
#define RX_BASE_ADDR 0x0C
#define COE_CR 0xB0
#define VLAN1 0xA0
#define BUS_CFG 0xDC

#define LAN9420_CPSR_ENDIAN_OFFSET 0
#define RX_RING_SIZE 128
#define TX_RING_SIZE 32

/* Bitfield Definitions from Driver */
#define E2P_CMD_EPC_BUSY_       (0x80000000)
#define MII_ACCESS_MII_BUSY_    (0x00000001)
#define MII_ACCESS_MII_READ_    (0x00000000)
#define INT_STAT_DMAC_INT_      (0x00000001)
#define INT_STAT_SW_INT_        (1 << 15)
#define INT_CFG_IRQ_EN_         (0x00040000)
#define INT_CFG_IRQ_INT_        (0x00080000)
#define INT_CTL_SW_INT_EN_      (0x00008000)
#define DMAC_STS_TX_            (BIT(0))
#define DMAC_STS_RX_            (BIT(6))
#define DMAC_STS_NIS_           (BIT(16))
#define DMAC_STS_TS_            (7 << 20)
#define DMAC_STS_RS_            (7 << 17)
#define DMAC_CONTROL_ST_        (BIT(13))
#define DMAC_CONTROL_SR_        (BIT(1))
#define TDES0_OWN_              (0x80000000)
#define RDES0_OWN_              (0x80000000)
#define BUS_MODE_SWR_           (BIT(0))

struct smsc9420_dma_desc {
    uint32_t status;
    uint32_t length;
    uint32_t buffer1;
    uint32_t buffer2;
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

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t int_stat;
    uint32_t int_ctl;
    uint32_t int_cfg;
    uint32_t dmac_intr_ena;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t id_rev;
    uint32_t mac_cr;
    uint32_t bus_mode;
    uint32_t bus_cfg;
    uint32_t gpio_cfg;
    uint32_t e2p_cmd;
    uint32_t e2p_data;
    uint32_t mii_access;
    uint32_t mii_data;
    uint32_t addrl;
    uint32_t addrh;
    uint32_t hashh;
    uint32_t hashl;
    uint32_t flow;
    uint32_t vlan1;
    uint32_t coe_cr;
    uint32_t miss_frame_cntr;

    /* DMA Context */
    uint32_t tx_base_addr;
    uint32_t rx_base_addr;
    uint32_t dmac_control;
    uint32_t dmac_status;
    uint32_t rx_poll_demand;
    uint32_t tx_poll_demand;
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = false;

    if (s->dmac_status & s->dmac_intr_ena) {
        s->int_stat |= INT_STAT_DMAC_INT_;
    } else {
        s->int_stat &= ~INT_STAT_DMAC_INT_;
    }

    if ((s->int_cfg & INT_CFG_IRQ_EN_) && s->int_stat) {
        s->int_cfg |= INT_CFG_IRQ_INT_;
        level = true;
    } else {
        s->int_cfg &= ~INT_CFG_IRQ_INT_;
    }

    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_tx)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct smsc9420_dma_desc desc;
    hwaddr desc_addr;

    if (is_tx) {
        if (!(s->dmac_control & DMAC_CONTROL_ST_)) return;
        desc_addr = s->tx_base_addr;
        pci_dma_read(pdev, desc_addr, &desc, sizeof(desc));
        if (desc.status & TDES0_OWN_) {
            desc.status &= ~TDES0_OWN_;
            pci_dma_write(pdev, desc_addr, &desc, sizeof(desc));
            s->dmac_status |= DMAC_STS_TX_ | DMAC_STS_NIS_;
            pcibase_update_irq(s);
        }
    } else {
        if (!(s->dmac_control & DMAC_CONTROL_SR_)) return;
        desc_addr = s->rx_base_addr;
        pci_dma_read(pdev, desc_addr, &desc, sizeof(desc));
        if (desc.status & RDES0_OWN_) {
            desc.status &= ~RDES0_OWN_;
            pci_dma_write(pdev, desc_addr, &desc, sizeof(desc));
            s->dmac_status |= DMAC_STS_RX_ | DMAC_STS_NIS_;
            pcibase_update_irq(s);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
        case ID_REV: return s->id_rev;
        case MAC_CR: return s->mac_cr;
        case BUS_MODE: return s->bus_mode;
        case BUS_CFG: return s->bus_cfg;
        case GPIO_CFG: return s->gpio_cfg;
        case E2P_CMD: return s->e2p_cmd;
        case E2P_DATA: return s->e2p_data;
        case MII_ACCESS: return s->mii_access;
        case MII_DATA: return s->mii_data;
        case ADDRL: return s->addrl;
        case ADDRH: return s->addrh;
        case HASHH: return s->hashh;
        case HASHL: return s->hashl;
        case FLOW: return s->flow;
        case VLAN1: return s->vlan1;
        case COE_CR: return s->coe_cr;
        case MISS_FRAME_CNTR: return s->miss_frame_cntr;
        case TX_BASE_ADDR: return s->tx_base_addr;
        case RX_BASE_ADDR: return s->rx_base_addr;
        case DMAC_CONTROL: return s->dmac_control;
        case DMAC_STATUS: return s->dmac_status;
        case DMAC_INTR_ENA: return s->dmac_intr_ena;
        case INT_STAT: return s->int_stat;
        case INT_CTL: return s->int_ctl;
        case INT_CFG: return s->int_cfg;
        case RX_POLL_DEMAND: return s->rx_poll_demand;
        case TX_POLL_DEMAND: return s->tx_poll_demand;
        default: return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
        case MAC_CR: s->mac_cr = val; break;
        case BUS_MODE: 
            s->bus_mode = val & ~BUS_MODE_SWR_; 
            break;
        case BUS_CFG: s->bus_cfg = val; break;
        case GPIO_CFG: s->gpio_cfg = val; break;
        case E2P_CMD: 
            s->e2p_cmd = val & ~E2P_CMD_EPC_BUSY_; 
            break;
        case E2P_DATA: s->e2p_data = val; break;
        case MII_ACCESS: 
            s->mii_access = val & ~MII_ACCESS_MII_BUSY_; 
            if (val & MII_ACCESS_MII_READ_) {
                s->mii_data = 0x0001; /* Dummy PHY ID */
            }
            break;
        case MII_DATA: s->mii_data = val; break;
        case ADDRL: s->addrl = val; break;
        case ADDRH: s->addrh = val; break;
        case HASHH: s->hashh = val; break;
        case HASHL: s->hashl = val; break;
        case FLOW: s->flow = val; break;
        case VLAN1: s->vlan1 = val; break;
        case COE_CR: s->coe_cr = val; break;
        case MISS_FRAME_CNTR: s->miss_frame_cntr = val; break;
        case TX_BASE_ADDR: s->tx_base_addr = val; break;
        case RX_BASE_ADDR: s->rx_base_addr = val; break;
        case DMAC_CONTROL: 
            if ((s->dmac_control & DMAC_CONTROL_ST_) && !(val & DMAC_CONTROL_ST_)) {
                s->dmac_status |= DMAC_STS_TS_;
            }
            if ((s->dmac_control & DMAC_CONTROL_SR_) && !(val & DMAC_CONTROL_SR_)) {
                s->dmac_status |= DMAC_STS_RS_;
            }
            s->dmac_control = val; 
            break;
        case DMAC_STATUS: 
            s->dmac_status &= ~val; 
            pcibase_update_irq(s);
            break;
        case DMAC_INTR_ENA: 
            s->dmac_intr_ena = val; 
            pcibase_update_irq(s);
            break;
        case INT_STAT: 
            s->int_stat &= ~val; 
            pcibase_update_irq(s);
            break;
        case INT_CTL: 
            s->int_ctl = val; 
            if (val & INT_CTL_SW_INT_EN_) {
                s->int_stat |= INT_STAT_SW_INT_;
            }
            pcibase_update_irq(s);
            break;
        case INT_CFG: 
            s->int_cfg = val; 
            pcibase_update_irq(s);
            break;
        case RX_POLL_DEMAND: 
            s->rx_poll_demand = val; 
            pcibase_do_dma(s, false);
            break;
        case TX_POLL_DEMAND: 
            s->tx_poll_demand = val; 
            pcibase_do_dma(s, true);
            break;
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

    s->id_rev = 0x94200000;
    s->mac_cr = 0;
    s->bus_mode = 0;
    s->bus_cfg = 0;
    s->gpio_cfg = 0;
    s->e2p_cmd = 0;
    s->e2p_data = 0;
    s->mii_access = 0;
    s->mii_data = 0;
    s->addrl = 0;
    s->addrh = 0;
    s->hashh = 0;
    s->hashl = 0;
    s->flow = 0;
    s->vlan1 = 0;
    s->coe_cr = 0;
    s->miss_frame_cntr = 0;
    s->tx_base_addr = 0;
    s->rx_base_addr = 0;
    s->dmac_control = 0;
    s->dmac_status = 0;
    s->dmac_intr_ena = 0;
    s->int_stat = 0;
    s->int_ctl = 0;
    s->int_cfg = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_9420 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_9420 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = SMSC_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; 
    s->bar_info[0].name = "smsc9420-bar3";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
