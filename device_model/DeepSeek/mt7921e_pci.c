/* QEMU 8.2.10 virtual PCI device model for MT7921e WiFi */
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

#define TYPE_PCIBASE_DEVICE "mt7921e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID  0x14c3
#define DEVICE_ID  0x7961
#define CLASS_ID   0x028000

#define MT_PCIE_MAC_BASE      0x10000
#define MT_WFDMA0_BASE        0xd4000
#define MT_TOP_BASE           0x18060000
#define MT_MCU_WPDMA0_BASE    0x54000000
#define MT_WFDMA0(ofs)        (MT_WFDMA0_BASE + (ofs))
#define MT_PCIE_MAC(ofs)      (MT_PCIE_MAC_BASE + (ofs))
#define MT_TOP(ofs)           (MT_TOP_BASE + (ofs))

/* Commonly used register offsets */
#define MT_HW_CHIPID          0x70010200
#define MT_HW_BOUND           0x70010020
#define MT_HW_REV             0x70010204
#define MT_WFDMA0_HOST_INT_ENA   MT_WFDMA0(0x204)
#define MT_WFDMA0_GLO_CFG        MT_WFDMA0(0x208)
#define MT_WFDMA0_RST            MT_WFDMA0(0x100)
#define MT_WFDMA0_RST_DTX_PTR    MT_WFDMA0(0x20c)
#define MT_WFDMA0_RST_DRX_PTR    MT_WFDMA0(0x280)
#define MT_WFDMA0_INT_TX_PRI     MT_WFDMA0(0x29c)
#define MT_WFDMA0_INT_RX_PRI     MT_WFDMA0(0x298)
#define MT_WFDMA0_PRI_DLY_INT_CFG0 MT_WFDMA0(0x2f0)
#define MT_PCIE_MAC_INT_ENABLE   MT_PCIE_MAC(0x188)
#define MT_PCIE_MAC_PM           MT_PCIE_MAC(0x194)
#define MT_CONN_ON_LPCTL         0x7c060010
#define MT_DMA_SHDL(ofs)        (0x7c026000 + (ofs))
#define MT_DMASHDL_SW_CONTROL    MT_DMA_SHDL(0x004)
#define MT_TOP_LPCR_HOST_BAND0   MT_TOP(0x10)

/* Additional known register addresses derived from Stage-1 defines */
#define MT_WFDMA0_HOST_INT_ENA_REG   MT_WFDMA0_HOST_INT_ENA
#define MT_WFDMA0_GLO_CFG_REG        MT_WFDMA0_GLO_CFG
#define MT_WFDMA0_RST_REG            MT_WFDMA0_RST
#define MT_WFDMA0_RST_DTX_PTR_REG    MT_WFDMA0_RST_DTX_PTR
#define MT_WFDMA0_RST_DRX_PTR_REG    MT_WFDMA0_RST_DRX_PTR
#define MT_WFDMA0_INT_TX_PRI_REG     MT_WFDMA0_INT_TX_PRI
#define MT_WFDMA0_INT_RX_PRI_REG     MT_WFDMA0_INT_RX_PRI
#define MT_WFDMA0_PRI_DLY_INT_CFG0_REG MT_WFDMA0_PRI_DLY_INT_CFG0
#define MT_PCIE_MAC_INT_ENABLE_REG   MT_PCIE_MAC_INT_ENABLE
#define MT_PCIE_MAC_PM_REG           MT_PCIE_MAC_PM

/* Registers from Phase 2 driver source */
#define MT_HIF_REMAP_L1              0xf11ac
#define MT_MCU2HOST_SW_INT_ENA        MT_WFDMA0(0x1f4)
#define MT_WFDMA0_TX_RING0_EXT_CTRL   MT_WFDMA0(0x600)

/* New bit definitions provided in this iteration */
#define HOST_RX_DONE_INT_ENA2         BIT(2)
#define HOST_RX_DONE_INT_ENA4         BIT(22)

#define PCIE_LPCR_HOST_SET_OWN        BIT(0)
#define PCIE_LPCR_HOST_CLR_OWN        BIT(1)
#define PCIE_LPCR_HOST_OWN_SYNC       BIT(2)

#define WFSYS_SW_RST_B                BIT(0)
#define WFSYS_SW_INIT_DONE            BIT(4)

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
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

    /* Hardware Register Shadows */
    uint32_t wfdma0_host_int_ena;
    uint32_t wfdma0_glo_cfg;
    uint32_t wfdma0_rst;
    uint32_t wfdma0_rst_dtx_ptr;
    uint32_t wfdma0_rst_drx_ptr;
    uint32_t wfdma0_int_tx_pri;
    uint32_t wfdma0_int_rx_pri;
    uint32_t wfdma0_pri_dly_int_cfg0;
    uint32_t pcie_mac_int_enable;
    uint32_t pcie_mac_pm;

    /* New registers for Phase 2 */
    uint32_t hif_remap_l1;
    uint32_t mcu2host_sw_int_ena;
    uint32_t wfdma0_tx_ring0_ext_ctrl;

    /* Firmware power management control register */
    uint32_t conn_on_lpctl;

    /* DMA Context (placeholder, not implemented due to missing register definitions) */
};

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MT_WFDMA0_HOST_INT_ENA_REG:
        val = s->wfdma0_host_int_ena;
        break;
    case MT_WFDMA0_GLO_CFG_REG:
        val = s->wfdma0_glo_cfg;
        break;
    case MT_WFDMA0_RST_REG:
        val = s->wfdma0_rst;
        break;
    case MT_WFDMA0_RST_DTX_PTR_REG:
        val = s->wfdma0_rst_dtx_ptr;
        break;
    case MT_WFDMA0_RST_DRX_PTR_REG:
        val = s->wfdma0_rst_drx_ptr;
        break;
    case MT_WFDMA0_INT_TX_PRI_REG:
        val = s->wfdma0_int_tx_pri;
        break;
    case MT_WFDMA0_INT_RX_PRI_REG:
        val = s->wfdma0_int_rx_pri;
        break;
    case MT_WFDMA0_PRI_DLY_INT_CFG0_REG:
        val = s->wfdma0_pri_dly_int_cfg0;
        break;
    case MT_PCIE_MAC_INT_ENABLE_REG:
        val = s->pcie_mac_int_enable;
        break;
    case MT_PCIE_MAC_PM_REG:
        val = s->pcie_mac_pm;
        break;
    case MT_HIF_REMAP_L1:
        val = s->hif_remap_l1;
        break;
    case MT_MCU2HOST_SW_INT_ENA:
        val = s->mcu2host_sw_int_ena;
        break;
    case MT_WFDMA0_TX_RING0_EXT_CTRL:
        val = s->wfdma0_tx_ring0_ext_ctrl;
        break;
    case MT_CONN_ON_LPCTL:
        val = s->conn_on_lpctl;
        break;
    default:
        val = 0;
        qemu_log_mask(LOG_UNIMP, "mt7921e: unimplemented MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MT_WFDMA0_HOST_INT_ENA_REG:
        s->wfdma0_host_int_ena = val;
        break;
    case MT_WFDMA0_GLO_CFG_REG:
        s->wfdma0_glo_cfg = val;
        break;
    case MT_WFDMA0_RST_REG:
        s->wfdma0_rst = val;
        break;
    case MT_WFDMA0_RST_DTX_PTR_REG:
        s->wfdma0_rst_dtx_ptr = val;
        break;
    case MT_WFDMA0_RST_DRX_PTR_REG:
        s->wfdma0_rst_drx_ptr = val;
        break;
    case MT_WFDMA0_INT_TX_PRI_REG:
        s->wfdma0_int_tx_pri = val;
        break;
    case MT_WFDMA0_INT_RX_PRI_REG:
        s->wfdma0_int_rx_pri = val;
        break;
    case MT_WFDMA0_PRI_DLY_INT_CFG0_REG:
        s->wfdma0_pri_dly_int_cfg0 = val;
        break;
    case MT_PCIE_MAC_INT_ENABLE_REG:
        s->pcie_mac_int_enable = val;
        break;
    case MT_PCIE_MAC_PM_REG:
        s->pcie_mac_pm = val;
        break;
    case MT_HIF_REMAP_L1:
        s->hif_remap_l1 = val;
        break;
    case MT_MCU2HOST_SW_INT_ENA:
        s->mcu2host_sw_int_ena = val;
        break;
    case MT_WFDMA0_TX_RING0_EXT_CTRL:
        s->wfdma0_tx_ring0_ext_ctrl = val;
        break;
    case MT_CONN_ON_LPCTL:
        s->conn_on_lpctl = val;
        /* Emulate ownership change: set OWN_SYNC when either SET_OWN or CLR_OWN is written */
        if (val & (PCIE_LPCR_HOST_SET_OWN | PCIE_LPCR_HOST_CLR_OWN)) {
            s->conn_on_lpctl |= PCIE_LPCR_HOST_OWN_SYNC;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "mt7921e: unimplemented MMIO write at 0x%" HWADDR_PRIx " val=0x%" PRIx64 "\n", addr, val);
        break;
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

    s->wfdma0_host_int_ena = 0;
    s->wfdma0_glo_cfg = 0;
    s->wfdma0_rst = 0;
    s->wfdma0_rst_dtx_ptr = 0;
    s->wfdma0_rst_drx_ptr = 0;
    s->wfdma0_int_tx_pri = 0;
    s->wfdma0_int_rx_pri = 0;
    s->wfdma0_pri_dly_int_cfg0 = 0;
    s->pcie_mac_int_enable = 0;
    s->pcie_mac_pm = 0;
    s->hif_remap_l1 = 0;
    s->mcu2host_sw_int_ena = 0;
    s->wfdma0_tx_ring0_ext_ctrl = 0;
    s->conn_on_lpctl = 0;
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: 2 GiB MMIO to cover all known register offsets (e.g., MT_CONN_ON_LPCTL at 0x7c060010) */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x80000000,
        .name = "mt7921e-bh"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI init */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }

    /* Default register values */
    pcibase_reset(DEVICE(pdev));
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

static const VMStateDescription vmstate_pcibase = {
    .name = "mt7921e_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(wfdma0_host_int_ena, PCIBaseState),
        VMSTATE_UINT32(wfdma0_glo_cfg, PCIBaseState),
        VMSTATE_UINT32(wfdma0_rst, PCIBaseState),
        VMSTATE_UINT32(wfdma0_rst_dtx_ptr, PCIBaseState),
        VMSTATE_UINT32(wfdma0_rst_drx_ptr, PCIBaseState),
        VMSTATE_UINT32(wfdma0_int_tx_pri, PCIBaseState),
        VMSTATE_UINT32(wfdma0_int_rx_pri, PCIBaseState),
        VMSTATE_UINT32(wfdma0_pri_dly_int_cfg0, PCIBaseState),
        VMSTATE_UINT32(pcie_mac_int_enable, PCIBaseState),
        VMSTATE_UINT32(pcie_mac_pm, PCIBaseState),
        VMSTATE_UINT32(hif_remap_l1, PCIBaseState),
        VMSTATE_UINT32(mcu2host_sw_int_ena, PCIBaseState),
        VMSTATE_UINT32(wfdma0_tx_ring0_ext_ctrl, PCIBaseState),
        VMSTATE_UINT32(conn_on_lpctl, PCIBaseState),
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
