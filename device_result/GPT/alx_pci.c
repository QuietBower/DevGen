/*
 * QEMU PCI device model for Qualcomm Atheros alx NIC (behavioral stub)
 * Focused on providing just enough register/DMA/IRQ behavior
 * for the Linux alx driver to probe and bind.
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
/* #include "alx.h" */

#define TYPE_PCIBASE_DEVICE "alx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ALX_PCI_VENDOR_ID 0x1969
#define ALX_DEV_ID_AR8161 0x1091
#define ALX_PCI_DEVICE_ID ALX_DEV_ID_AR8161
#define ALX_PCI_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET
#define ALX_MAX_NAPIS 4

#define ALX_RFD_PIDX                    0x15E0
#define ALX_IMR                         0x1604
#define ALX_ISR_TX_Q0                   BIT(15)
#define ALX_ISR_RX_Q0                   BIT(16)
#define ALX_ISR_PHY                     BIT(12)
#define ALX_ISR                         0x1600
#define ALX_ISR_DIS                     BIT(31)
#define ALX_TX_BASE_ADDR_HI             0x1544
#define ALX_SRAM9                       0x1534
#define ALX_RRD_ADDR_LO                 0x1568
#define ALX_RFD_RING_SZ                 0x1560
#define ALX_TPD_RING_SZ                 0x1584
#define ALX_RRD_RING_SZ                 0x1578
#define ALX_RFD_BUF_SZ                  0x1564
#define ALX_RFD_ADDR_LO                 0x1550
#define ALX_RX_BASE_ADDR_HI             0x1540
#define ALX_MAC_CTRL                    0x1480
#define ALX_HASH_TBL0                   0x1490
#define ALX_HASH_TBL1                   0x1494
#define ALX_MSI_MAP_TBL2                0x15D8
#define ALX_MSI_ID_MAP                  0x15D4
#define ALX_MSI_MAP_TBL1                0x15D0
#define ALX_MSI_RETRANS_TIMER           0x1920
#define ALX_MSI_MASK_SEL_LINE           BIT(16)
#define ALX_MSI_RETRANS_TM_SHIFT        0
#define ALX_MAC_CTRL_PROMISC_EN         BIT(15)
#define ALX_MAC_CTRL_MULTIALL_EN        BIT(25)
#define ALX_MAC_CTRL_PRMBLEN_SHIFT      10
#define ALX_MAC_CTRL_RXFC_EN            BIT(3)
#define ALX_MAC_CTRL_TXFC_EN            BIT(2)
#define ALX_MAC_CTRL_PCRCE              BIT(7)
#define ALX_MAC_CTRL_BRD_EN             BIT(26)
#define ALX_MAC_CTRL_CRCE               BIT(6)
#define ALX_MAC_CTRL_WOLSPED_SWEN       BIT(30)
#define ALX_MAC_CTRL_MHASH_ALG_HI5B     BIT(29)
#define ALX_TPD_PRI0_ADDR_LO            0x1580
#define ALX_TPD_PRI1_ADDR_LO            0x157C
#define ALX_TPD_PRI2_ADDR_LO            0x14E0
#define ALX_TPD_PRI3_ADDR_LO            0x14E4
#define ALX_TPD_PRI3_PIDX               0x1618
#define ALX_TPD_PRI2_PIDX               0x161A
#define ALX_TPD_PRI0_PIDX               0x15F2
#define ALX_TPD_PRI1_PIDX               0x15F0
#define ALX_TPD_PRI1_CIDX               0x15F4
#define ALX_TPD_PRI0_CIDX               0x15F6
#define ALX_TPD_PRI3_CIDX               0x161C
#define ALX_TPD_PRI2_CIDX               0x161E
#define ALX_MTU                         0x149C
#define ALX_RXQ0                        0x15A0
#define ALX_RXQ2                        0x15A8
#define ALX_TXQ0                        0x1590
#define ALX_TXQ1                        0x1594
#define ALX_MASTER                      0x1400
#define ALX_PMCTRL                      0x12F8
#define ALX_MISC                        0x19C0
#define ALX_MISC3                       0x19CC
#define ALX_DMA                         0x15C0
#define ALX_RXQ0_EN                     BIT(31)
#define ALX_TXQ0_EN                     BIT(5)
#define ALX_MAC_STS                     0x1410
#define ALX_PHY_CTRL                    0x140C
#define ALX_LPI_CTRL                    0x1440
#define ALX_MDIO                        0x1414
#define ALX_MDIO_EXTN                   0x1448
#define ALX_CLK_GATE                    0x1814
#define ALX_IRQ_MODU_TIMER              0x1408
#define ALX_TINT_TPD_THRSHLD            0x15C8
#define ALX_TINT_TIMER                  0x15CC
#define ALX_SMB_TIMER                   0x15C4
#define ALX_IDLE_DECISN_TIMER           0x1474
#define ALX_INT_RETRIG                  0x1608
#define ALX_MIB_BASE                    0x1700
#define ALX_MIB_UPDATE                  (ALX_MIB_BASE + 196)
#define ALX_DRV                         0x1804
#define ALX_MSIX_MASK                   0x0090
#define ALX_MSIC2                       0x19C8
#define ALX_SERDES                      0x1424
#define ALX_WRR                         0x1938
#define ALX_HQTPD                       0x193C
#define ALX_WOL0                        0x14A0
#define ALX_STAD0                       0x1488
#define ALX_STAD1                       0x148C
#define ALX_EFLD                        0x0204
#define ALX_SLD                         0x0218

/* Bits for MAC reset / status derived from driver expectations */
#define ALX_MASTER_DMA_MAC_RST          BIT(0)
#define ALX_MASTER_DMA_MAC_RST_TO       BIT(4)


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
    struct {
        uint32_t imr;
        uint32_t isr;
        uint32_t mac_ctrl;
        uint32_t mtu;
        uint32_t master;
        uint32_t pmctrl;
        uint32_t misc;
        uint32_t misc3;
        uint32_t dma;
        uint32_t rxq0;
        uint32_t rxq2;
        uint32_t txq0;
        uint32_t txq1;
        uint32_t mdio;
        uint32_t mdio_extn;
        uint32_t clk_gate;
        uint32_t irq_modu_timer;
        uint32_t tint_tpd_thrshld;
        uint32_t tint_timer;
        uint32_t smb_timer;
        uint32_t idle_decisn_timer;
        uint32_t int_retrig;
        uint32_t drv;
        uint32_t hash_tbl0;
        uint32_t hash_tbl1;
        uint32_t tx_base_addr_hi;
        uint32_t rx_base_addr_hi;
        uint32_t rrd_addr_lo;
        uint32_t rfd_addr_lo;
        uint32_t rrd_ring_sz;
        uint32_t rfd_ring_sz;
        uint32_t tpd_ring_sz;
        uint32_t rfd_buf_sz;
        uint32_t sram9;
        uint32_t msi_map_tbl1;
        uint32_t msi_map_tbl2;
        uint32_t msi_id_map;
        uint32_t msi_retrans_timer;
        uint32_t stad0;
        uint32_t stad1;
        uint32_t wol0;
    } regs;

    /* DMA Context */
    dma_addr_t desc_base_dma;
    uint32_t desc_size;
};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Compute currently active (unmasked) interrupts */
    s->intr_status = s->regs.isr & s->regs.imr;

    if (s->intr_status) {
        if (msix_enabled(pdev) && s->has_msix) {
            /* Use first vector for now */
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev) && s->has_msi) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        /* Deassert legacy INTx when no pending interrupts */
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The Linux driver sets up descriptor rings in coherent memory and
     * updates producer indices, expecting the device to perform DMA to
     * fill RRD/RFD or consume TPD. The exact hardware behavior is not
     * modeled here; we keep only address/size shadows so that writes
     * to these registers succeed without errors.
     *
     * No actual pci_dma_read/pci_dma_write is performed because the
     * descriptor formats and DMA semantics are not defined in the
     * provided source.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4 && size != 2 && size != 1) {
        return 0;
    }

    switch (addr) {
    case ALX_IMR:
        val = s->regs.imr;
        break;
    case ALX_ISR:
        val = s->regs.isr;
        break;
    case ALX_MAC_CTRL:
        val = s->regs.mac_ctrl;
        break;
    case ALX_MTU:
        val = s->regs.mtu;
        break;
    case ALX_MASTER:
        val = s->regs.master;
        break;
    case ALX_PMCTRL:
        val = s->regs.pmctrl;
        break;
    case ALX_MISC:
        val = s->regs.misc;
        break;
    case ALX_MISC3:
        val = s->regs.misc3;
        break;
    case ALX_DMA:
        val = s->regs.dma;
        break;
    case ALX_RXQ0:
        val = s->regs.rxq0;
        break;
    case ALX_RXQ2:
        val = s->regs.rxq2;
        break;
    case ALX_TXQ0:
        val = s->regs.txq0;
        break;
    case ALX_TXQ1:
        val = s->regs.txq1;
        break;
    case ALX_MDIO:
        val = s->regs.mdio;
        break;
    case ALX_MDIO_EXTN:
        val = s->regs.mdio_extn;
        break;
    case ALX_CLK_GATE:
        val = s->regs.clk_gate;
        break;
    case ALX_IRQ_MODU_TIMER:
        val = s->regs.irq_modu_timer;
        break;
    case ALX_TINT_TPD_THRSHLD:
        val = s->regs.tint_tpd_thrshld;
        break;
    case ALX_TINT_TIMER:
        val = s->regs.tint_timer;
        break;
    case ALX_SMB_TIMER:
        val = s->regs.smb_timer;
        break;
    case ALX_IDLE_DECISN_TIMER:
        val = s->regs.idle_decisn_timer;
        break;
    case ALX_INT_RETRIG:
        val = s->regs.int_retrig;
        break;
    case ALX_DRV:
        val = s->regs.drv;
        break;
    case ALX_HASH_TBL0:
        val = s->regs.hash_tbl0;
        break;
    case ALX_HASH_TBL1:
        val = s->regs.hash_tbl1;
        break;
    case ALX_TX_BASE_ADDR_HI:
        val = s->regs.tx_base_addr_hi;
        break;
    case ALX_RX_BASE_ADDR_HI:
        val = s->regs.rx_base_addr_hi;
        break;
    case ALX_RRD_ADDR_LO:
        val = s->regs.rrd_addr_lo;
        break;
    case ALX_RFD_ADDR_LO:
        val = s->regs.rfd_addr_lo;
        break;
    case ALX_RRD_RING_SZ:
        val = s->regs.rrd_ring_sz;
        break;
    case ALX_RFD_RING_SZ:
        val = s->regs.rfd_ring_sz;
        break;
    case ALX_TPD_RING_SZ:
        val = s->regs.tpd_ring_sz;
        break;
    case ALX_RFD_BUF_SZ:
        val = s->regs.rfd_buf_sz;
        break;
    case ALX_SRAM9:
        val = s->regs.sram9;
        break;
    case ALX_MSI_MAP_TBL1:
        val = s->regs.msi_map_tbl1;
        break;
    case ALX_MSI_MAP_TBL2:
        val = s->regs.msi_map_tbl2;
        break;
    case ALX_MSI_ID_MAP:
        val = s->regs.msi_id_map;
        break;
    case ALX_MSI_RETRANS_TIMER:
        val = s->regs.msi_retrans_timer;
        break;
    case ALX_STAD0:
        val = s->regs.stad0;
        break;
    case ALX_STAD1:
        val = s->regs.stad1;
        break;
    case ALX_WOL0:
        val = s->regs.wol0;
        break;
    case ALX_MAC_STS:
        /* Report MAC as idle/ready so that reset polling succeeds */
        val = 0;
        break;
    default:
        /* For undefined registers, just return 0 to keep driver happy */
        val = 0;
        break;
    }

    /* Width adjust */
    if (size == 1) {
        val &= 0xff;
    } else if (size == 2) {
        val &= 0xffff;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    if (size != 4 && size != 2 && size != 1) {
        return;
    }

    switch (addr) {
    case ALX_IMR:
        /* Interrupt mask register */
        s->regs.imr = v32;
        s->intr_mask = v32;
        pcibase_update_irq(s);
        break;
    case ALX_ISR:
        /*
         * The driver writes ALX_ISR with "intr | ALX_ISR_DIS" to ack,
         * then later writes 0. We model ISR as W1C, ignoring DIS bit
         * except as a marker in the value.
         */
        s->regs.isr &= ~v32;
        pcibase_update_irq(s);
        break;
    case ALX_MASTER:
        /*
         * Model MAC reset handshake: when driver writes reset bit,
         * clear both reset and timeout bits immediately so polling
         * in the driver observes a completed reset.
         */
        s->regs.master = v32;
        s->regs.master &= ~(ALX_MASTER_DMA_MAC_RST | ALX_MASTER_DMA_MAC_RST_TO);
        break;
    case ALX_MAC_CTRL:
        s->regs.mac_ctrl = v32;
        break;
    case ALX_MTU:
        s->regs.mtu = v32;
        break;
    case ALX_PMCTRL:
        s->regs.pmctrl = v32;
        break;
    case ALX_MISC:
        s->regs.misc = v32;
        break;
    case ALX_MISC3:
        s->regs.misc3 = v32;
        break;
    case ALX_DMA:
        s->regs.dma = v32;
        break;
    case ALX_RXQ0:
        s->regs.rxq0 = v32;
        break;
    case ALX_RXQ2:
        s->regs.rxq2 = v32;
        break;
    case ALX_TXQ0:
        s->regs.txq0 = v32;
        break;
    case ALX_TXQ1:
        s->regs.txq1 = v32;
        break;
    case ALX_MDIO:
        s->regs.mdio = v32;
        break;
    case ALX_MDIO_EXTN:
        s->regs.mdio_extn = v32;
        break;
    case ALX_CLK_GATE:
        s->regs.clk_gate = v32;
        break;
    case ALX_IRQ_MODU_TIMER:
        s->regs.irq_modu_timer = v32;
        break;
    case ALX_TINT_TPD_THRSHLD:
        s->regs.tint_tpd_thrshld = v32;
        break;
    case ALX_TINT_TIMER:
        s->regs.tint_timer = v32;
        break;
    case ALX_SMB_TIMER:
        s->regs.smb_timer = v32;
        break;
    case ALX_IDLE_DECISN_TIMER:
        s->regs.idle_decisn_timer = v32;
        break;
    case ALX_INT_RETRIG:
        s->regs.int_retrig = v32;
        break;
    case ALX_DRV:
        s->regs.drv = v32;
        break;
    case ALX_HASH_TBL0:
        s->regs.hash_tbl0 = v32;
        break;
    case ALX_HASH_TBL1:
        s->regs.hash_tbl1 = v32;
        break;
    case ALX_TX_BASE_ADDR_HI:
        s->regs.tx_base_addr_hi = v32;
        s->desc_base_dma &= 0x00000000ffffffffULL;
        s->desc_base_dma |= ((uint64_t)v32) << 32;
        break;
    case ALX_RX_BASE_ADDR_HI:
        s->regs.rx_base_addr_hi = v32;
        break;
    case ALX_RRD_ADDR_LO:
        s->regs.rrd_addr_lo = v32;
        break;
    case ALX_RFD_ADDR_LO:
        s->regs.rfd_addr_lo = v32;
        break;
    case ALX_RRD_RING_SZ:
        s->regs.rrd_ring_sz = v32;
        break;
    case ALX_RFD_RING_SZ:
        s->regs.rfd_ring_sz = v32;
        break;
    case ALX_TPD_RING_SZ:
        s->regs.tpd_ring_sz = v32;
        s->desc_size = v32;
        break;
    case ALX_RFD_BUF_SZ:
        s->regs.rfd_buf_sz = v32;
        break;
    case ALX_SRAM9:
        /* Driver writes ALX_SRAM_LOAD_PTR here to "load" ring ptrs */
        s->regs.sram9 = v32;
        break;
    case ALX_MSI_MAP_TBL1:
        s->regs.msi_map_tbl1 = v32;
        break;
    case ALX_MSI_MAP_TBL2:
        s->regs.msi_map_tbl2 = v32;
        break;
    case ALX_MSI_ID_MAP:
        s->regs.msi_id_map = v32;
        break;
    case ALX_MSI_RETRANS_TIMER:
        s->regs.msi_retrans_timer = v32;
        break;
    case ALX_STAD0:
        s->regs.stad0 = v32;
        break;
    case ALX_STAD1:
        s->regs.stad1 = v32;
        break;
    case ALX_WOL0:
        s->regs.wol0 = v32;
        break;
    case ALX_RFD_PIDX:
        /* Rx producer index update; record but do not emulate further */
        (void)v32;
        break;
    default:
        /* For undefined registers, ignore writes */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Initialize register defaults that the driver may rely on */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.imr = 0;
    s->regs.isr = 0;
    s->regs.mac_ctrl = 0;
    s->regs.mtu = 0;
    /*
     * Model hardware after reset with MAC reset bits deasserted so that
     * the driver's reset polling treats the MAC as successfully reset.
     */
    s->regs.master &= ~(ALX_MASTER_DMA_MAC_RST | ALX_MASTER_DMA_MAC_RST_TO);
    s->regs.pmctrl = 0;
    s->regs.misc = 0;
    s->regs.misc3 = 0;
    s->regs.dma = 0;
    s->regs.rxq0 = 0;
    s->regs.rxq2 = 0;
    s->regs.txq0 = 0;
    s->regs.txq1 = 0;
    s->regs.mdio = 0;
    s->regs.mdio_extn = 0;
    s->regs.clk_gate = 0;
    s->regs.irq_modu_timer = 0;
    s->regs.tint_tpd_thrshld = 0;
    s->regs.tint_timer = 0;
    s->regs.smb_timer = 0;
    s->regs.idle_decisn_timer = 0;
    s->regs.int_retrig = 0;
    s->regs.drv = 0;
    s->regs.hash_tbl0 = 0;
    s->regs.hash_tbl1 = 0;
    s->regs.tx_base_addr_hi = 0;
    s->regs.rx_base_addr_hi = 0;
    s->regs.rrd_addr_lo = 0;
    s->regs.rfd_addr_lo = 0;
    s->regs.rrd_ring_sz = 0;
    s->regs.rfd_ring_sz = 0;
    s->regs.tpd_ring_sz = 0;
    s->regs.rfd_buf_sz = 0;
    s->regs.sram9 = 0;
    s->regs.msi_map_tbl1 = 0;
    s->regs.msi_map_tbl2 = 0;
    s->regs.msi_id_map = 0;
    s->regs.msi_retrans_timer = 0;
    s->regs.stad0 = 0;
    s->regs.stad1 = 0;
    s->regs.wol0 = 0;

    s->intr_status = 0;
    s->intr_mask = 0;
    s->desc_base_dma = 0;
    s->desc_size = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ALX_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ALX_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ALX_PCI_CLASS_ID );
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
    s->bar_info[0].size = 0x2000;
    s->bar_info[0].name = "alx-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msix_init_exclusive_bar(pdev, ALX_MAX_NAPIS + 1, 1, NULL)) {
        s->has_msix = false;
    } else {
        s->has_msix = true;
    }
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = false;
    } else {
        s->has_msi = true;
    }

    s->intr_status = 0;
    s->intr_mask = 0;
    s->desc_base_dma = 0;
    s->desc_size = 0;
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "alx_pci",
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
