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
#ifndef BIT
#define BIT(n) (1UL << (n))
#endif
#ifndef GENMASK
#define GENMASK(h, l) (((~0UL) << (l)) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#endif
#ifndef FIELD_PREP
#define FIELD_PREP(_mask, _val) (((typeof(_mask))(_val) << __ffs(_mask)) & (_mask))
#endif
#ifndef BITS_PER_LONG
#define BITS_PER_LONG (__SIZEOF_LONG__ * 8)
#endif

#define TYPE_PCIBASE_DEVICE "mt7925e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* Standard PCI IDs */
#define PCI_VENDOR_ID_MEDIATEK 0x14C3
#define MT7925_DEVICE_ID 0x7925
#define MT7925_CLASS_ID 0x028000

/* Register base addresses */
#define MT_PCIE_MAC_BASE        0x10000
#define MT_WFDMA0_BASE          0xd4000
#define MT_DMA_SHDL_BASE        0x7c026000
#define MT_UWFDMA0_BASE         0x7c024000
#define MT_MCU_WPDMA0_BASE      0x54000000

/* Convenience macros for register offsets */
#define MT_PCIE_MAC(ofs)        (MT_PCIE_MAC_BASE + (ofs))
#define MT_WFDMA0(ofs)          (MT_WFDMA0_BASE + (ofs))
#define MT_DMA_SHDL(ofs)        (MT_DMA_SHDL_BASE + (ofs))
#define MT_UWFDMA0(ofs)         (MT_UWFDMA0_BASE + (ofs))
#define MT_MCU_WPDMA0(ofs)      (MT_MCU_WPDMA0_BASE + (ofs))

/* Specific register offsets (from driver macros) */
#define MT_WFDMA0_RST           MT_WFDMA0(0x100)
#define MT_WFDMA0_GLO_CFG       MT_WFDMA0(0x208)
#define MT_WFDMA0_RST_DTX_PTR   MT_WFDMA0(0x20c)
#define MT_WFDMA0_RST_DRX_PTR   MT_WFDMA0(0x280)
#define MT_WFDMA0_INT_TX_PRI    MT_WFDMA0(0x29c)
#define MT_WFDMA0_INT_RX_PRI    MT_WFDMA0(0x298)
#define MT_WFDMA0_GLO_CFG_EXT0  MT_WFDMA0(0x2b0)
#define MT_WFDMA0_PRI_DLY_INT_CFG0 MT_WFDMA0(0x2f0)
#define MT_WFDMA0_TX_RING0_EXT_CTRL  MT_WFDMA0(0x600)
#define MT_WFDMA0_TX_RING1_EXT_CTRL  MT_WFDMA0(0x604)
#define MT_WFDMA0_TX_RING2_EXT_CTRL  MT_WFDMA0(0x608)
#define MT_WFDMA0_TX_RING3_EXT_CTRL  MT_WFDMA0(0x60c)
#define MT_WFDMA0_TX_RING4_EXT_CTRL  MT_WFDMA0(0x610)
#define MT_WFDMA0_TX_RING5_EXT_CTRL  MT_WFDMA0(0x614)
#define MT_WFDMA0_TX_RING6_EXT_CTRL  MT_WFDMA0(0x618)
#define MT_WFDMA0_TX_RING15_EXT_CTRL MT_WFDMA0(0x63c)
#define MT_WFDMA0_TX_RING16_EXT_CTRL MT_WFDMA0(0x640)
#define MT_WFDMA0_TX_RING17_EXT_CTRL MT_WFDMA0(0x644)
#define MT_WFDMA0_RX_RING0_EXT_CTRL  MT_WFDMA0(0x680)
#define MT_WFDMA0_RX_RING1_EXT_CTRL  MT_WFDMA0(0x684)
#define MT_WFDMA0_RX_RING2_EXT_CTRL  MT_WFDMA0(0x688)
#define MT_WFDMA0_RX_RING3_EXT_CTRL  MT_WFDMA0(0x68c)
#define MT_WFDMA0_RX_RING4_EXT_CTRL  MT_WFDMA0(0x690)
#define MT_WFDMA0_RX_RING5_EXT_CTRL  MT_WFDMA0(0x694)
#define MT_WFDMA0_MCU_INT_ENA    MT_WFDMA0(0x1f4)
#define MT_WFDMA0_HOST_INT_ENA   MT_WFDMA0(0x204)
#define MT_PCIE_MAC_INT_ENABLE   MT_PCIE_MAC(0x188)
#define MT_PCIE_MAC_PM           MT_PCIE_MAC(0x194)
#define MT_DMASHDL_SW_CONTROL    MT_DMA_SHDL(0x004)
#define MT_WFDMA_DUMMY_CR        MT_MCU_WPDMA0(0x120)
#define MT_UWFDMA0_GLO_CFG_EXT1  MT_UWFDMA0(0x2b4)

/* Various bit masks and constants from driver */
#define MT_WFDMA0_GLO_CFG_TX_DMA_EN     BIT(0)
#define MT_WFDMA0_GLO_CFG_TX_DMA_BUSY   BIT(1)
#define MT_WFDMA0_GLO_CFG_RX_DMA_EN     BIT(2)
#define MT_WFDMA0_GLO_CFG_RX_DMA_BUSY   BIT(3)
#define MT_WFDMA0_GLO_CFG_TX_WB_DDONE   BIT(6)
#define MT_WFDMA0_GLO_CFG_FIFO_DIS_CHECK BIT(11)
#define MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN BIT(12)
#define MT_WFDMA0_GLO_CFG_RX_WB_DDONE   BIT(13)
#define MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN BIT(15)
#define MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2 BIT(21)
#define MT_WFDMA0_GLO_CFG_OMIT_RX_INFO   BIT(27)
#define MT_WFDMA0_GLO_CFG_OMIT_TX_INFO   BIT(28)
#define MT_WFDMA0_GLO_CFG_CLK_GAT_DIS    BIT(30)
#define MT_HW_EMI_CTL_SLPPROT_EN         BIT(1)
#define MT_WFDMA0_GLO_CFG_DMA_SIZE       GENMASK(5, 4)
#define MT_WFDMA0_RST_DMASHDL_ALL_RST    BIT(5)
#define MT_WFDMA0_RST_LOGIC_RST          BIT(4)
#define WFSYS_SW_RST_B                   BIT(0)
#define WFSYS_SW_INIT_DONE               BIT(4)
#define MT_WFDMA0_CSR_TX_DMASHDL_ENABLE  BIT(6)
#define MT_DMASHDL_DMASHDL_BYPASS        BIT(28)
#define MT_MCU2HOST_SW_INT_ENA           MT_WFDMA0(0x1f4)
#define MT_MCU_CMD_WAKE_RX_PCIE          BIT(0)
#define MT_PCIE_MAC_PM_L0S_DIS           BIT(8)

/* HIF Remap */
#define MT_HIF_REMAP_L1                 0xf11ac
#define MT_HIF_REMAP_L2                 0xf11b0
#define MT_HIF_REMAP_L1_OFFSET          GENMASK(15, 0)
#define MT_HIF_REMAP_L1_BASE            GENMASK(31, 16)
#define MT_HIF_REMAP_L1_MASK            GENMASK(15, 0)
#define MT_HIF_REMAP_BASE_L1            0xe0000
/* __OFFS placeholder definition to resolve compilation; actual value unknown */
#define __OFFS(id)   0
#define MT_HIF_REMAP_BASE_L2            __OFFS(HIF_REMAP_BASE_L2)

#define MT_WFDMA_NEED_REINIT             BIT(1)
#define HOST_RX_DONE_INT_ENA0            BIT(0)
#define HOST_RX_DONE_INT_ENA2            BIT(2)

#define MT_DRV_TXWI_NO_FREE              BIT(0)
#define MT_DRV_HW_MGMT_TXQ               BIT(4)
#define MT_DRV_AMSDU_OFFLOAD             BIT(5)

/* Standard sizes (may be used as register values) */
#define MT7925_RX_MCU_RING_SIZE          512
#define MT7925_TX_MCU_RING_SIZE          256
#define MT_RX_BUF_SIZE                   2048
#define MT7925_RX_RING_SIZE              1536
#define MT7925_TX_RING_SIZE              2048
#define MT7925_TX_FWDL_RING_SIZE         128
#define MT7925_TOKEN_SIZE                8192

/* More constants */
#define MT_RXD0_SW_PKT_TYPE_MAP          0x380F
#define MT_RXD0_SW_PKT_TYPE_FRAME        0x3801
#define MT_RXD0_SW_PKT_TYPE_MASK         GENMASK(31, 16)

#define PCIE_LPCR_HOST_SET_OWN           BIT(0)
#define PCIE_LPCR_HOST_OWN_SYNC          BIT(2)
#define MT_CONN_ON_LPCTL                 0x7c060010

#define MCU_UNI_CMD(_t)                  (__MCU_CMD_FIELD_UNI | FIELD_PREP(__MCU_CMD_FIELD_ID, MCU_UNI_CMD_##_t))
#define MCU_CMD(_t)                      FIELD_PREP(__MCU_CMD_FIELD_ID, MCU_CMD_##_t)
#define MCU_PKT_ID                       0xa0

#define MT_HW_CHIPID                     0x70010200
#define MT_HW_EMI_CTL                    0x18011100
#define MT_HW_REV                        0x70010204

/* Enum definitions from driver */
typedef enum {
    MT76_BUS_MMIO,
    MT76_BUS_USB,
    MT76_BUS_SDIO,
} mt76_bus_type;

/* More enums can be added if needed */

/* Placeholder for enum rx_pkt_type, etc. */

/* Static structures (if any) */

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
    /* Interrupt registers */
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (specific registers used during probe) */
    uint32_t hw_chipid;
    uint32_t hw_rev;
    uint32_t hw_emi_ctl;
    uint32_t conn_on_lpctl;
    uint32_t pcie_mac_int_enable;
    uint32_t wfdma0_host_int_ena;
    uint32_t wfdma0_rst;
    /* HIF remap registers */
    uint32_t hif_remap_l1;
    uint32_t hif_remap_l2;
    /* (no flat mmio array) */

    /* DMA Context (not needed for probe) */
    /* Operational status flags */
    bool hw_init_done;

    /* State used to handle reset sequences */
    bool reset_done;

    /* Power management state (D0-D3) */
    uint32_t pm_state;

    /* Additional state */
};

/* Additional global definitions */
/* (none) */

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Interrupt update logic: not required for probe, empty */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* DMA transfer logic: not required for probe, empty */
}

/* MMIO Handlers: handle known registers; unknown return 0 and ignore writes */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Ensure access is within BAR range (0x80000000) */
    if (addr + size > 0x80000000) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: 0x%"HWADDR_PRIx"\n", __func__, addr);
        return ~0ULL;
    }

    /* HIF remap: if L1 remap active and address within window, translate */
    if (addr != MT_HIF_REMAP_L1 && addr != MT_HIF_REMAP_L2 && s->hif_remap_l1) {
        uint32_t base = (s->hif_remap_l1 >> 16) << 16; /* bits [31:16] */
        uint32_t mask = MT_HIF_REMAP_L1_MASK;          /* 0xFFFF */
        if (addr >= base && addr <= base + mask) {
            uint32_t offset = s->hif_remap_l1 & MT_HIF_REMAP_L1_OFFSET; /* bits [15:0] */
            addr = offset + (addr - base);
        }
    }

    /* Currently, driver uses only 4-byte aligned accesses; handle specific offsets */
    switch (addr) {
    case MT_HW_CHIPID: /* 0x70010200 */
        return s->hw_chipid;
    case MT_HW_REV: /* 0x70010204 */
        return s->hw_rev;
    case MT_HW_EMI_CTL: /* 0x18011100 */
        return s->hw_emi_ctl;
    case MT_CONN_ON_LPCTL: /* 0x7c060010 */
        return s->conn_on_lpctl;
    case MT_PCIE_MAC_INT_ENABLE: /* 0x10188 */
        return s->pcie_mac_int_enable;
    case MT_WFDMA0_HOST_INT_ENA: /* 0xd4204 */
        return s->wfdma0_host_int_ena;
    case MT_WFDMA0_RST: /* 0xd4100 */
        return s->wfdma0_rst;
    case MT_HIF_REMAP_L1: /* 0xf11ac */
        return s->hif_remap_l1;
    case MT_HIF_REMAP_L2: /* 0xf11b0 */
        return s->hif_remap_l2;
    default:
        /* For all other addresses, return 0 to facilitate busy/done bit clearing */
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Ensure within BAR range */
    if (addr + size > 0x80000000) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: 0x%"HWADDR_PRIx"\n", __func__, addr);
        return;
    }

    /* HIF remap: if L1 remap active and address within window, translate */
    if (addr != MT_HIF_REMAP_L1 && addr != MT_HIF_REMAP_L2 && s->hif_remap_l1) {
        uint32_t base = (s->hif_remap_l1 >> 16) << 16;
        uint32_t mask = MT_HIF_REMAP_L1_MASK;
        if (addr >= base && addr <= base + mask) {
            uint32_t offset = s->hif_remap_l1 & MT_HIF_REMAP_L1_OFFSET;
            addr = offset + (addr - base);
        }
    }

    /* Handle known registers with special behavior */
    switch (addr) {
    case MT_HW_CHIPID: /* 0x70010200 */
        s->hw_chipid = val;
        break;
    case MT_HW_REV: /* 0x70010204 */
        s->hw_rev = val;
        break;
    case MT_HW_EMI_CTL: /* 0x18011100 */
        s->hw_emi_ctl = val;
        break;
    case MT_CONN_ON_LPCTL: /* 0x7c060010 */
        s->conn_on_lpctl = val;
        /* Ownership handshake: update OWN_SYNC based on SET_OWN bit */
        if (val & PCIE_LPCR_HOST_SET_OWN) {
            s->conn_on_lpctl |= PCIE_LPCR_HOST_OWN_SYNC;
        } else {
            s->conn_on_lpctl &= ~PCIE_LPCR_HOST_OWN_SYNC;
        }
        break;
    case MT_PCIE_MAC_INT_ENABLE: /* 0x10188 */
        s->pcie_mac_int_enable = val;
        break;
    case MT_WFDMA0_HOST_INT_ENA: /* 0xd4204 */
        s->wfdma0_host_int_ena = val;
        break;
    case MT_WFDMA0_RST: /* 0xd4100 */
        s->wfdma0_rst = val;
        /* Reset simulation: when SW reset (BIT0) is asserted, set INIT_DONE (BIT4) */
        if (val & WFSYS_SW_RST_B) {
            s->wfdma0_rst |= WFSYS_SW_INIT_DONE;
        } else {
            s->wfdma0_rst &= ~WFSYS_SW_INIT_DONE;
        }
        break;
    case MT_HIF_REMAP_L1: /* 0xf11ac */
        s->hif_remap_l1 = val;
        break;
    case MT_HIF_REMAP_L2: /* 0xf11b0 */
        s->hif_remap_l2 = val;
        break;
    default:
        /* Ignore writes to unknown registers; debugging only */
        qemu_log_mask(LOG_UNIMP, "%s: write to unknown register 0x%"HWADDR_PRIx" value 0x%"PRIx64"\n", __func__, addr, val);
        break;
    }
}

/* PIO handlers deleted: not used by driver */

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset logic: clear all tracked registers */
    s->hw_chipid = 0;
    s->hw_rev = 0;
    s->hw_emi_ctl = 0;
    s->conn_on_lpctl = 0;
    s->pcie_mac_int_enable = 0;
    s->wfdma0_host_int_ena = 0;
    s->wfdma0_rst = 0;
    s->hif_remap_l1 = 0;
    s->hif_remap_l2 = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MEDIATEK );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  MT7925_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, MT7925_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: driver uses BAR0 only, size enlarged to cover high registers */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x80000000, /* 2GB, covers all remapped windows including MT_CONN_ON_LPCTL */
        .name = "bar0",
    };
    
    /* No flat MMIO allocation; handling via direct register fields */
    /* Initialize all tracked registers to zero */
    s->hw_chipid = 0;
    s->hw_rev = 0;
    s->hw_emi_ctl = 0;
    s->conn_on_lpctl = 0;
    s->pcie_mac_int_enable = 0;
    s->wfdma0_host_int_ena = 0;
    s->wfdma0_rst = 0;
    s->hif_remap_l1 = 0;
    s->hif_remap_l2 = 0;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization; driver uses MSI */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }

    /* DMA config: not needed */
    /* Timer config: not needed */
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

    /* No dynamic resources to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mt7925e_pci",
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
