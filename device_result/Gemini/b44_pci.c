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

#define TYPE_PCIBASE_DEVICE "b44_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef PCI_VENDOR_ID_BROADCOM
#define PCI_VENDOR_ID_BROADCOM 0x14e4
#endif
#ifndef PCI_DEVICE_ID_BCM4401
#define PCI_DEVICE_ID_BCM4401 0x4401
#endif

#define B44_DEVCTRL        0x0000
#define B44_WKUP_LEN       0x0010
#define B44_ISTAT          0x0020
#define B44_IMASK          0x0024
#define B44_GPTIMER        0x0028
#define B44_ADDR_LO        0x0088
#define B44_ADDR_HI        0x008C
#define B44_FILT_ADDR      0x0090
#define B44_FILT_DATA      0x0094
#define B44_MAC_CTRL       0x00A8
#define B44_MAC_FLOW       0x00AC
#define B44_DMATX_CTRL     0x0200
#define B44_DMATX_ADDR     0x0204
#define B44_DMATX_PTR      0x0208
#define B44_DMATX_STAT     0x020C
#define B44_DMARX_CTRL     0x0210
#define B44_DMARX_ADDR     0x0214
#define B44_DMARX_PTR      0x0218
#define B44_DMARX_STAT     0x021C
#define B44_RXCONFIG       0x0400
#define B44_RXMAXLEN       0x0404
#define B44_TXMAXLEN       0x0408
#define B44_MDIO_CTRL      0x0410
#define B44_MDIO_DATA      0x0414
#define B44_EMAC_ISTAT     0x041C
#define B44_CAM_DATA_LO    0x0420
#define B44_CAM_DATA_HI    0x0424
#define B44_CAM_CTRL       0x0428
#define B44_ENET_CTRL      0x042C
#define B44_TX_CTRL        0x0430
#define B44_TX_WMARK       0x0434
#define B44_MIB_CTRL       0x0438
#define B44_TX_GOOD_O      0x0500
#define B44_TX_PAUSE       0x055C
#define B44_RX_GOOD_O      0x0580
#define B44_RX_NPAUSE      0x05D8

#define B44_RCV_LAZY       0x0100UL
#define MAC_CTRL_CRC32_ENAB 0x00000001
#define MAC_CTRL_PHY_LEDCTRL 0x000000e0
#define RCV_LAZY_FC_SHIFT  24
#define RX_HEADER_LEN      28
#define DMARX_CTRL_ENABLE  0x00000001
#define RX_PKT_OFFSET      (RX_HEADER_LEN + 2)
#define DMARX_CTRL_ROSHIFT 1
#define DMATX_CTRL_ENABLE  0x00000001
#define MIB_CTRL_CLR_ON_READ 0x00000001
#define ENET_CTRL_ENABLE   0x00000001

#define SSB_BAR0_WIN       0x80
#define SSB_IDLOW          0x0FF8
#define SSB_IDHIGH         0x0FFC

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
    uint32_t istat;
    uint32_t imask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t devctrl;
    uint32_t wkup_len;
    uint32_t gptimer;
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t filt_addr;
    uint32_t filt_data;
    uint32_t mac_ctrl;
    uint32_t mac_flow;
    uint32_t rxconfig;
    uint32_t rxmaxlen;
    uint32_t txmaxlen;
    uint32_t mdio_ctrl;
    uint32_t mdio_data;
    uint32_t emac_istat;
    uint32_t cam_data_lo;
    uint32_t cam_data_hi;
    uint32_t cam_ctrl;
    uint32_t enet_ctrl;
    uint32_t tx_ctrl;
    uint32_t tx_wmark;
    uint32_t mib_ctrl;

    /* DMA Context */
    uint32_t dmatx_ctrl;
    uint32_t dmatx_addr;
    uint32_t dmatx_ptr;
    uint32_t dmatx_stat;
    uint32_t dmarx_ctrl;
    uint32_t dmarx_addr;
    uint32_t dmarx_ptr;
    uint32_t dmarx_stat;

    /* SSB Context */
    uint32_t ssb_idlow;
    uint32_t ssb_idhigh;
};

struct dma_desc {
    uint32_t ctrl;
    uint32_t addr;
};

struct rx_header {
    uint16_t len;
    uint16_t flags;
    uint16_t pad[12];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->istat & s->imask) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    if (is_write) {
        /* Simulate immediate TX completion by advancing stat to ptr */
        s->dmatx_stat = s->dmatx_ptr;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case B44_DEVCTRL: val = s->devctrl; break;
        case B44_WKUP_LEN: val = s->wkup_len; break;
        case B44_ISTAT: val = s->istat; break;
        case B44_IMASK: val = s->imask; break;
        case B44_GPTIMER: val = s->gptimer; break;
        case B44_ADDR_LO: val = s->addr_lo; break;
        case B44_ADDR_HI: val = s->addr_hi; break;
        case B44_FILT_ADDR: val = s->filt_addr; break;
        case B44_FILT_DATA: val = s->filt_data; break;
        case B44_MAC_CTRL: val = s->mac_ctrl; break;
        case B44_MAC_FLOW: val = s->mac_flow; break;
        case B44_DMATX_CTRL: val = s->dmatx_ctrl; break;
        case B44_DMATX_ADDR: val = s->dmatx_addr; break;
        case B44_DMATX_PTR: val = s->dmatx_ptr; break;
        case B44_DMATX_STAT: val = s->dmatx_stat; break;
        case B44_DMARX_CTRL: val = s->dmarx_ctrl; break;
        case B44_DMARX_ADDR: val = s->dmarx_addr; break;
        case B44_DMARX_PTR: val = s->dmarx_ptr; break;
        case B44_DMARX_STAT: val = s->dmarx_stat; break;
        case B44_RXCONFIG: val = s->rxconfig; break;
        case B44_RXMAXLEN: val = s->rxmaxlen; break;
        case B44_TXMAXLEN: val = s->txmaxlen; break;
        case B44_MDIO_CTRL: val = s->mdio_ctrl; break;
        case B44_MDIO_DATA: val = s->mdio_data; break;
        case B44_EMAC_ISTAT: val = s->emac_istat; break;
        case B44_CAM_DATA_LO: val = s->cam_data_lo; break;
        case B44_CAM_DATA_HI: val = s->cam_data_hi; break;
        case B44_CAM_CTRL: val = s->cam_ctrl; break;
        case B44_ENET_CTRL: val = s->enet_ctrl; break;
        case B44_TX_CTRL: val = s->tx_ctrl; break;
        case B44_TX_WMARK: val = s->tx_wmark; break;
        case B44_MIB_CTRL: val = s->mib_ctrl; break;
        case SSB_IDLOW: val = s->ssb_idlow; break;
        case SSB_IDHIGH: val = s->ssb_idhigh; break;
        default: val = 0; break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
        case B44_DEVCTRL: s->devctrl = val; break;
        case B44_WKUP_LEN: s->wkup_len = val; break;
        case B44_ISTAT: 
            s->istat &= ~val; 
            pcibase_update_irq(s); 
            break;
        case B44_IMASK: 
            s->imask = val; 
            pcibase_update_irq(s); 
            break;
        case B44_GPTIMER: s->gptimer = val; break;
        case B44_ADDR_LO: s->addr_lo = val; break;
        case B44_ADDR_HI: s->addr_hi = val; break;
        case B44_FILT_ADDR: s->filt_addr = val; break;
        case B44_FILT_DATA: s->filt_data = val; break;
        case B44_MAC_CTRL: s->mac_ctrl = val; break;
        case B44_MAC_FLOW: s->mac_flow = val; break;
        case B44_DMATX_CTRL: s->dmatx_ctrl = val; break;
        case B44_DMATX_ADDR: s->dmatx_addr = val; break;
        case B44_DMATX_PTR: 
            s->dmatx_ptr = val; 
            pcibase_do_dma(s, true); 
            break;
        case B44_DMARX_CTRL: s->dmarx_ctrl = val; break;
        case B44_DMARX_ADDR: s->dmarx_addr = val; break;
        case B44_DMARX_PTR: 
            s->dmarx_ptr = val; 
            pcibase_do_dma(s, false); 
            break;
        case B44_RXCONFIG: s->rxconfig = val; break;
        case B44_RXMAXLEN: s->rxmaxlen = val; break;
        case B44_TXMAXLEN: s->txmaxlen = val; break;
        case B44_MDIO_CTRL: s->mdio_ctrl = val; break;
        case B44_MDIO_DATA: 
            s->mdio_data = 0x0024; /* Magic value for Link Up, Reset Clear, Valid ID */
            s->emac_istat = 0xFFFFFFFF; /* Set all bits so b44_wait_bit succeeds */
            break;
        case B44_EMAC_ISTAT: s->emac_istat &= ~val; break;
        case B44_CAM_DATA_LO: s->cam_data_lo = val; break;
        case B44_CAM_DATA_HI: s->cam_data_hi = val; break;
        case B44_CAM_CTRL: s->cam_ctrl = 0; break; /* Auto-clear busy */
        case B44_ENET_CTRL: s->enet_ctrl = 0; break; /* Auto-clear disable */
        case B44_TX_CTRL: s->tx_ctrl = val; break;
        case B44_TX_WMARK: s->tx_wmark = val; break;
        case B44_MIB_CTRL: s->mib_ctrl = val; break;
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

    s->istat = 0;
    s->imask = 0;
    s->devctrl = 0;
    s->wkup_len = 0;
    s->gptimer = 0;
    s->addr_lo = 0;
    s->addr_hi = 0;
    s->filt_addr = 0;
    s->filt_data = 0;
    s->mac_ctrl = 0;
    s->mac_flow = 0;
    s->rxconfig = 0;
    s->rxmaxlen = 0;
    s->txmaxlen = 0;
    s->mdio_ctrl = 0;
    s->mdio_data = 0;
    s->emac_istat = 0;
    s->cam_data_lo = 0;
    s->cam_data_hi = 0;
    s->cam_ctrl = 0;
    s->enet_ctrl = 0;
    s->tx_ctrl = 0;
    s->tx_wmark = 0;
    s->mib_ctrl = 0;
    s->dmatx_ctrl = 0;
    s->dmatx_addr = 0;
    s->dmatx_ptr = 0;
    s->dmatx_stat = 0;
    s->dmarx_ctrl = 0;
    s->dmarx_addr = 0;
    s->dmarx_ptr = 0;
    s->dmarx_stat = 0;
    s->ssb_idlow = 0x8060;  /* Valid Broadcom B44 Core ID */
    s->ssb_idhigh = 0x0426; /* Valid Broadcom Vendor ID */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_BROADCOM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_BCM4401 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Explicitly place PM capability at 0x40 to avoid overlapping with SSB routing registers at 0x80 */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0x40, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Make all device-specific config space writable to act as RAM for SSB routing */
    for (int i = 0x48; i < 0x100; i += 4) {
        pci_set_long(pdev->wmask + i, 0xffffffff);
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "b44-mmio";

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
    .name = "b44_pci",
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
