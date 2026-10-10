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

#define TYPE_PCIBASE_DEVICE "atl1_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ATTANSIC		0x1969
#define PCI_DEVICE_ID_ATTANSIC_L1	0x1048

#define REG_MASTER_CTRL			0x1400
#define MASTER_CTRL_SOFT_RST		0x1
#define MASTER_CTRL_ITIMER_EN		0x4
#define REG_PHY_ENABLE			0x140C
#define REG_IDLE_STATUS			0x1410
#define REG_PCIE_CAP_LIST		0x58
#define REG_SPI_FLASH_CTRL		0x200
#define REG_VPD_CAP			0x6C
#define REG_VPD_DATA			0x70
#define REG_MDIO_CTRL			0x1414
#define REG_SPI_DATA			0x208
#define REG_SPI_ADDR			0x204
#define REG_MAC_STA_ADDR		0x1488
#define REG_RX_HASH_TABLE		0x1490
#define REG_MAC_CTRL			0x1480
#define MAC_CTRL_TX_EN			1
#define MAC_CTRL_RX_EN			2
#define REG_RXQ_RRD_PAUSE_THRESH	0x15AC
#define REG_RXQ_RXF_PAUSE_THRESH	0x15A8
#define REG_SRAM_RXF_LEN		0x1524
#define REG_SRAM_RRD_LEN		0x150C
#define REG_MAILBOX			0x15F0
#define REG_SMB_TIMER			0x15E4
#define REG_CMBDISDMA_TIMER		0x140E
#define REG_CSMB_CTRL			0x15D0
#define REG_DESC_TPD_ADDR_LO		0x154C
#define REG_CMB_WRITE_TH		0x15D4
#define REG_MAC_HALF_DUPLX_CTRL		0x1498
#define REG_RXQ_JMBOSZ_RRDTIM		0x15A4
#define REG_TXQ_CTRL			0x1580
#define TXQ_CTRL_EN			0x20
#define REG_DESC_SMB_ADDR_LO		0x1554
#define REG_RXQ_CTRL			0x15A0
#define RXQ_CTRL_EN			0x80000000
#define REG_DESC_RFD_RRD_RING_SIZE	0x1558
#define REG_CMB_WRITE_TIMER		0x15D8
#define REG_DESC_CMB_ADDR_LO		0x1550
#define REG_DMA_CTRL			0x15C0
#define DMA_CTRL_DMAR_EN		0x400
#define DMA_CTRL_DMAW_EN		0x800
#define REG_DESC_BASE_ADDR_HI		0x1540
#define REG_DESC_RRD_ADDR_LO		0x1548
#define REG_DESC_TPD_RING_SIZE		0x155C
#define REG_DESC_RFD_ADDR_LO		0x1544
#define REG_LOAD_PTR			0x1534
#define REG_ISR				0x1600
#define ISR_SMB				0x1
#define ISR_RXF_OV			0x8
#define ISR_RFD_UNRUN			0x10
#define ISR_RRD_OV			0x20
#define ISR_HOST_RFD_UNRUN		0x100
#define ISR_HOST_RRD_OV			0x200
#define ISR_DMAR_TO_RST			0x400
#define ISR_DMAW_TO_RST			0x800
#define ISR_GPHY			0x1000
#define ISR_CMB_RX			0x100000
#define ISR_CMB_TX			0x200000
#define ISR_DIS_SMB			0x20000000
#define ISR_DIS_DMA			0x40000000
#define ISR_DIS_INT			0x80000000
#define REG_TX_JUMBO_TASK_TH_TPD_IPG	0x1584
#define REG_IRQ_MODU_TIMER_INIT		0x1408
#define REG_MAC_IPG_IFG			0x1484
#define REG_MTU				0x149C
#define REG_WOL_CTRL			0x14A0
#define REG_PCIE_PHYMISC		0x1000
#define REG_IMR				0x1604
#define ATL1_REG_COUNT			1538

#define ISR_PHY_LINKDOWN		0x10000000
#define ATL1_DEFAULT_RFD		512
#define ATL1_DEFAULT_TPD		256

#define MDIO_START			0x800000
#define MDIO_BUSY			0x8000000
#define SPI_FLASH_CTRL_START		0x800
#define VPD_CAP_VPD_FLAG		0x80000000
#define IMR_NORMAL_MASK	(\
	IMR_NORXTX_MASK	|\
	ISR_CMB_TX	|\
	ISR_CMB_RX)
#define IMR_NORXTX_MASK	(\
	ISR_SMB		|\
	ISR_GPHY	|\
	ISR_PHY_LINKDOWN|\
	ISR_DMAR_TO_RST	|\
	ISR_DMAW_TO_RST)
#define MB_RFD_PROD_INDX_MASK			0x7FF
#define MB_RFD_PROD_INDX_SHIFT			0
#define MB_RRD_CONS_INDX_MASK			0x7FF
#define MB_RRD_CONS_INDX_SHIFT			11
#define MB_TPD_PROD_INDX_MASK			0x3FF
#define MB_TPD_PROD_INDX_SHIFT			22
#define MII_ATLX_PSSR_SPD_DPLX_RESOLVED	0x0800
#define MII_ATLX_PSSR_SPEED		0xC000
#define MII_ATLX_PSSR_1000MBS		0x8000
#define MII_ATLX_PSSR_100MBS		0x4000
#define MII_ATLX_PSSR_10MBS		0x0000
#define MDIO_SUP_PREAMBLE		0x400000
#define MDIO_RW				0x200000
#define MDIO_CLK_25_4			0
#define MDIO_CLK_SEL_SHIFT		24
#define MDIO_REG_ADDR_MASK		0x1F
#define MDIO_REG_ADDR_SHIFT		16

struct tx_packet_desc {
    uint64_t buffer_addr;
    uint32_t word2;
    uint32_t word3;
};

struct rx_free_desc {
    uint64_t buffer_addr;
    uint16_t buf_len;
    uint16_t coalese;
} QEMU_PACKED;

struct rx_return_desc {
    uint8_t num_buf;
    uint8_t resved;
    uint16_t buf_indx;
    union {
        uint32_t valid;
        struct {
            uint16_t rx_chksum;
            uint16_t pkt_size;
        } xsum_sz;
    } xsz;
    uint16_t pkt_flg;
    uint16_t err_flg;
    uint16_t resved2;
    uint16_t vlan_tag;
};

struct coals_msg_block {
    uint32_t int_stats;
    uint16_t rrd_prod_idx;
    uint16_t rfd_cons_idx;
    uint16_t update;
    uint16_t tpd_cons_idx;
};

struct stats_msg_block {
    uint32_t rx_ok;
    uint32_t rx_bcast;
    uint32_t rx_mcast;
    uint32_t rx_pause;
    uint32_t rx_ctrl;
    uint32_t rx_fcs_err;
    uint32_t rx_len_err;
    uint32_t rx_byte_cnt;
    uint32_t rx_runt;
    uint32_t rx_frag;
    uint32_t rx_sz_64;
    uint32_t rx_sz_65_127;
    uint32_t rx_sz_128_255;
    uint32_t rx_sz_256_511;
    uint32_t rx_sz_512_1023;
    uint32_t rx_sz_1024_1518;
    uint32_t rx_sz_1519_max;
    uint32_t rx_sz_ov;
    uint32_t rx_rxf_ov;
    uint32_t rx_rrd_ov;
    uint32_t rx_align_err;
    uint32_t rx_bcast_byte_cnt;
    uint32_t rx_mcast_byte_cnt;
    uint32_t rx_err_addr;

    uint32_t tx_ok;
    uint32_t tx_bcast;
    uint32_t tx_mcast;
    uint32_t tx_pause;
    uint32_t tx_exc_defer;
    uint32_t tx_ctrl;
    uint32_t tx_defer;
    uint32_t tx_byte_cnt;
    uint32_t tx_sz_64;
    uint32_t tx_sz_65_127;
    uint32_t tx_sz_128_255;
    uint32_t tx_sz_256_511;
    uint32_t tx_sz_512_1023;
    uint32_t tx_sz_1024_1518;
    uint32_t tx_sz_1519_max;
    uint32_t tx_1_col;
    uint32_t tx_2_col;
    uint32_t tx_late_col;
    uint32_t tx_abort_col;
    uint32_t tx_underrun;
    uint32_t tx_rd_eop;
    uint32_t tx_len_err;
    uint32_t tx_trunc;
    uint32_t tx_bcast_byte;
    uint32_t tx_mcast_byte;
    uint32_t smb_updated;
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
    uint32_t isr;
    uint32_t imr;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[ATL1_REG_COUNT];

    /* DMA Context */
    uint64_t desc_base_addr_hi;
    uint32_t desc_tpd_addr_lo;
    uint32_t desc_smb_addr_lo;
    uint32_t desc_cmb_addr_lo;
    uint32_t desc_rrd_addr_lo;
    uint32_t desc_rfd_addr_lo;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->isr & s->imr) != 0;
    
    if (s->has_msi && msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t cmb_addr = ((uint64_t)s->desc_base_addr_hi << 32) | s->desc_cmb_addr_lo;
    
    if (cmb_addr) {
        struct coals_msg_block cmb;
        pci_dma_read(pdev, cmb_addr, &cmb, sizeof(cmb));
        
        cmb.int_stats = cpu_to_le32(ISR_CMB_TX | ISR_CMB_RX);
        
        /* Hack to advance TX cons index to avoid timeout */
        uint16_t cons = le16_to_cpu(cmb.tpd_cons_idx);
        cons = (cons + 1) % ATL1_DEFAULT_TPD;
        cmb.tpd_cons_idx = cpu_to_le16(cons);
        
        pci_dma_write(pdev, cmb_addr, &cmb, sizeof(cmb));
    }
    
    s->isr |= ISR_CMB_TX | ISR_CMB_RX;
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;
    uint32_t index = addr / 4;

    if (index < ATL1_REG_COUNT) {
        val = s->regs[index];
    }

    switch (addr) {
    case REG_IDLE_STATUS:
        val = 0;
        break;
    case REG_MDIO_CTRL:
        val = s->regs[index];
        break;
    case REG_SPI_FLASH_CTRL:
        val = s->regs[index];
        break;
    case REG_VPD_CAP:
        val = s->regs[index] | VPD_CAP_VPD_FLAG;
        break;
    case REG_ISR:
        val = s->isr;
        break;
    case REG_IMR:
        val = s->imr;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr / 4;

    if (index < ATL1_REG_COUNT) {
        s->regs[index] = val;
    }

    switch (addr) {
    case REG_MASTER_CTRL:
        if (val & MASTER_CTRL_SOFT_RST) {
            s->isr = 0;
            s->imr = 0;
            pcibase_update_irq(s);
        }
        break;
    case REG_MDIO_CTRL:
        val &= ~(MDIO_START | MDIO_BUSY);
        if (!(val & MDIO_RW)) {
            val |= (MII_ATLX_PSSR_SPD_DPLX_RESOLVED | MII_ATLX_PSSR_1000MBS);
        }
        s->regs[index] = val;
        break;
    case REG_SPI_FLASH_CTRL:
        val &= ~SPI_FLASH_CTRL_START;
        s->regs[index] = val;
        break;
    case REG_ISR:
        s->isr &= ~val;
        pcibase_update_irq(s);
        break;
    case REG_IMR:
        s->imr = val;
        pcibase_update_irq(s);
        break;
    case REG_DESC_BASE_ADDR_HI:
        s->desc_base_addr_hi = val;
        break;
    case REG_DESC_TPD_ADDR_LO:
        s->desc_tpd_addr_lo = val;
        break;
    case REG_DESC_SMB_ADDR_LO:
        s->desc_smb_addr_lo = val;
        break;
    case REG_DESC_CMB_ADDR_LO:
        s->desc_cmb_addr_lo = val;
        break;
    case REG_DESC_RRD_ADDR_LO:
        s->desc_rrd_addr_lo = val;
        break;
    case REG_DESC_RFD_ADDR_LO:
        s->desc_rfd_addr_lo = val;
        break;
    case REG_MAILBOX:
        pcibase_do_dma(s, true);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
    memset(s->regs, 0, sizeof(s->regs));
    s->isr = 0;
    s->imr = 0;
    s->desc_base_addr_hi = 0;
    s->desc_tpd_addr_lo = 0;
    s->desc_smb_addr_lo = 0;
    s->desc_cmb_addr_lo = 0;
    s->desc_rrd_addr_lo = 0;
    s->desc_rfd_addr_lo = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ATTANSIC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ATTANSIC_L1 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x40000, .name = "atl1-mmio" };

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
    .name = "atl1_pci",
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
