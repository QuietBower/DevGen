/*
 * QEMU PCI device model for RDC R6040 Ethernet controller
 * Auto-generated for Linux r6040.c driver probing and basic operation.
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "r6040_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_RDC
#define PCI_VENDOR_ID_RDC 0x17F3
#endif
#define R6040_VENDOR_ID        PCI_VENDOR_ID_RDC
#define R6040_DEVICE_ID        0x6040
#define R6040_CLASS_ID         PCI_CLASS_NETWORK_ETHERNET

#define DRV_NAME    "r6040"
#define DRV_VERSION "0.29"
#define DRV_RELDATE "04Jul2016"
#define TX_TIMEOUT  (6000)
#define MAX_BUF_SIZE    0x600
#define MDIO_READ   0x2000
#define MDIO_WRITE  0x4000
#define MAC_ID      0xBE
#define R6040_IO_SIZE   256
#define MAX_MAC     2
#define MCR0        0x00
#define MCR0_RCVEN  0x0002
#define MCR0_PROMISC    0x0020
#define MCR0_HASH_EN    0x0100
#define MCR0_XMTEN  0x1000
#define MCR0_FD 0x8000
#define MCR1        0x04
#define MAC_RST 0x0001
#define MBCR        0x08
#define MT_ICR      0x0C
#define MR_ICR      0x10
#define MTPR        0x14
#define TM2TX       0x0001
#define MR_BSR      0x18
#define MR_DCR      0x1A
#define MLSR        0x1C
#define TX_FIFO_UNDR    0x0200
#define TX_EXCEEDC 0x2000
#define TX_LATEC   0x4000
#define MMDIO      0x20
#define MMRD       0x24
#define MMWD       0x28
#define MTD_SA0    0x2C
#define MTD_SA1    0x30
#define MRD_SA0    0x34
#define MRD_SA1    0x38
#define MISR       0x3C
#define MIER       0x40
#define MSK_INT    0x0000
#define RX_FINISH  0x0001
#define RX_NO_DESC 0x0002
#define RX_FIFO_FULL   0x0004
#define RX_EARLY   0x0008
#define TX_FINISH  0x0010
#define TX_EARLY   0x0080
#define EVENT_OVRFL    0x0100
#define LINK_CHANGED   0x0200
#define ME_CISR     0x44
#define ME_CIER     0x48
#define MR_CNT      0x50
#define ME_CNT0     0x52
#define ME_CNT1     0x54
#define ME_CNT2     0x56
#define ME_CNT3     0x58
#define MT_CNT      0x5A
#define ME_CNT4     0x5C
#define MP_CNT      0x5E
#define MAR0        0x60
#define MAR1        0x62
#define MAR2        0x64
#define MAR3        0x66
#define MID_0L      0x68
#define MID_0M      0x6A
#define MID_0H      0x6C
#define MID_1L      0x70
#define MID_1M      0x72
#define MID_1H      0x74
#define MID_2L      0x78
#define MID_2M      0x7A
#define MID_2H      0x7C
#define MID_3L      0x80
#define MID_3M      0x82
#define MID_3H      0x84
#define PHY_CC      0x88
#define SCEN        0x8000
#define PHYAD_SHIFT 8
#define TMRDIV_SHIFT    0
#define PHY_ST      0x8A
#define MAC_SM      0xAC
#define MAC_SM_RST  0x0002
#define MD_CSC      0xb6
#define MD_CSC_DEFAULT  0x0030
#define TX_DCNT     0x80
#define RX_DCNT     0x80
#define MBCR_DEFAULT    0x012A
#define MCAST_MAX   3
#define MAC_DEF_TIMEOUT 2048
#define DSC_OWNER_MAC   0x8000
#define DSC_RX_OK   0x4000
#define DSC_RX_ERR  0x0800
#define DSC_RX_ERR_DRI  0x0400
#define DSC_RX_ERR_BUF  0x0200
#define DSC_RX_ERR_LONG 0x0100
#define DSC_RX_ERR_RUNT 0x0080
#define DSC_RX_ERR_CRC  0x0040
#define DSC_RX_BCAST    0x0020
#define DSC_RX_MCAST    0x0010
#define DSC_RX_MCH_HIT  0x0008
#define DSC_RX_MIDH_HIT 0x0004
#define DSC_RX_IDX_MID_MASK 3
#define RX_INTS         (RX_FIFO_FULL | RX_NO_DESC | RX_FINISH)
#define TX_INTS         (TX_FINISH)
#define INT_MASK        (RX_INTS | TX_INTS)

struct r6040_descriptor {
    uint16_t    status, len;
    uint32_t    buf;
    uint32_t    ndesc;
    uint32_t    rev1;
    char        *vbufp;
    struct r6040_descriptor *vndescp;
    void        *skb_ptr;
    uint32_t    rev2;
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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t mcr0;
    uint16_t mcr1;
    uint16_t mbcr;
    uint16_t mt_icr;
    uint16_t mr_icr;
    uint16_t mtpr;
    uint16_t mr_bsr;
    uint16_t mr_dcr;
    uint16_t mlsr;
    uint16_t mmdio;
    uint16_t mmrd;
    uint16_t mmwd;
    uint16_t mtd_sa0;
    uint16_t mtd_sa1;
    uint16_t mrd_sa0;
    uint16_t mrd_sa1;
    uint16_t misr;
    uint16_t mier;
    uint16_t me_cisr;
    uint16_t me_cier;
    uint16_t mar[4];
    uint16_t mid[4][3];
    uint16_t phy_cc;
    uint16_t phy_st;
    uint16_t mac_sm;
    uint16_t md_csc;

    /* Simple PHY emulation */
    uint16_t phy_regs[32];

    /* Descriptor ring addresses programmed by driver */
    uint32_t tx_ring_dma_lo;
    uint32_t tx_ring_dma_hi;
    uint32_t rx_ring_dma_lo;
    uint32_t rx_ring_dma_hi;

    /* Internal helper: whether MAC is enabled */
    bool mac_rx_enabled;
    bool mac_tx_enabled;
};


/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t pending = s->misr & s->mier;

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Minimal TX/RX completion simulation based on descriptor rings. */
    dma_addr_t ring_base;
    uint16_t misr_bits = 0;

    if (is_write) {
        if (!s->mac_tx_enabled) {
            return;
        }
        ring_base = ((dma_addr_t)s->tx_ring_dma_hi << 16) | s->tx_ring_dma_lo;
        if (ring_base) {
            struct r6040_descriptor desc;
            if (pci_dma_read(pdev, ring_base, &desc, sizeof(desc)) == 0) {
                if (le16_to_cpu(desc.status) & DSC_OWNER_MAC) {
                    desc.status = cpu_to_le16(0x0000);
                    pci_dma_write(pdev, ring_base, &desc, sizeof(desc));
                    misr_bits |= TX_FINISH;
                }
            }
        }
    } else {
        if (!s->mac_rx_enabled) {
            return;
        }
        ring_base = ((dma_addr_t)s->rx_ring_dma_hi << 16) | s->rx_ring_dma_lo;
        if (ring_base) {
            struct r6040_descriptor desc;
            if (pci_dma_read(pdev, ring_base, &desc, sizeof(desc)) == 0) {
                if (le16_to_cpu(desc.status) & DSC_OWNER_MAC) {
                    desc.status = cpu_to_le16(0x0000);
                    desc.len = cpu_to_le16(64);
                    pci_dma_write(pdev, ring_base, &desc, sizeof(desc));
                    misr_bits |= RX_FINISH;
                }
            }
        }
    }

    if (misr_bits) {
        s->misr |= misr_bits;
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MCR0:
        val = s->mcr0;
        break;
    case MCR1:
        val = s->mcr1;
        break;
    case MBCR:
        val = s->mbcr;
        break;
    case MT_ICR:
        val = s->mt_icr;
        break;
    case MR_ICR:
        val = s->mr_icr;
        break;
    case MTPR:
        val = s->mtpr;
        break;
    case MR_BSR:
        val = s->mr_bsr;
        break;
    case MR_DCR:
        val = s->mr_dcr;
        break;
    case MLSR:
        val = s->mlsr;
        break;
    case MMDIO:
        val = s->mmdio;
        break;
    case MMRD:
        val = s->mmrd;
        break;
    case MMWD:
        val = s->mmwd;
        break;
    case MTD_SA0:
        val = s->mtd_sa0;
        break;
    case MTD_SA1:
        val = s->mtd_sa1;
        break;
    case MRD_SA0:
        val = s->mrd_sa0;
        break;
    case MRD_SA1:
        val = s->mrd_sa1;
        break;
    case MISR:
        val = s->misr;
        break;
    case MIER:
        val = s->mier;
        break;
    case ME_CISR:
        val = s->me_cisr;
        break;
    case ME_CIER:
        val = s->me_cier;
        break;
    case MAR0:
        val = s->mar[0];
        break;
    case MAR1:
        val = s->mar[1];
        break;
    case MAR2:
        val = s->mar[2];
        break;
    case MAR3:
        val = s->mar[3];
        break;
    case MID_0L:
        val = s->mid[0][0];
        break;
    case MID_0M:
        val = s->mid[0][1];
        break;
    case MID_0H:
        val = s->mid[0][2];
        break;
    case MID_1L:
        val = s->mid[1][0];
        break;
    case MID_1M:
        val = s->mid[1][1];
        break;
    case MID_1H:
        val = s->mid[1][2];
        break;
    case MID_2L:
        val = s->mid[2][0];
        break;
    case MID_2M:
        val = s->mid[2][1];
        break;
    case MID_2H:
        val = s->mid[2][2];
        break;
    case MID_3L:
        val = s->mid[3][0];
        break;
    case MID_3M:
        val = s->mid[3][1];
        break;
    case MID_3H:
        val = s->mid[3][2];
        break;
    case PHY_CC:
        val = s->phy_cc;
        break;
    case PHY_ST:
        val = s->phy_st;
        break;
    case MAC_SM:
        val = s->mac_sm;
        break;
    case MD_CSC:
        val = s->md_csc;
        break;
    case ME_CNT0:
        /* ME_CNT0 used as multicast counter in driver */
        val = 0;
        break;
    case ME_CNT1:
        /* ME_CNT1 used as CRC error counter in driver */
        val = 0;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint16_t v16 = (uint16_t)val;

    switch (addr) {
    case MCR0:
        s->mcr0 = v16;
        s->mac_rx_enabled = (s->mcr0 & MCR0_RCVEN) != 0;
        s->mac_tx_enabled = (s->mcr0 & MCR0_XMTEN) != 0;
        break;
    case MCR1:
        s->mcr1 = v16;
        break;
    case MBCR:
        s->mbcr = v16;
        break;
    case MT_ICR:
        s->mt_icr = v16;
        break;
    case MR_ICR:
        s->mr_icr = v16;
        break;
    case MTPR:
        s->mtpr = v16;
        if (v16 & TM2TX) {
            pcibase_do_dma(s, true);
        }
        break;
    case MR_BSR:
        s->mr_bsr = v16;
        break;
    case MR_DCR:
        s->mr_dcr = v16;
        break;
    case MLSR:
        s->mlsr = v16;
        break;
    case MMDIO: {
        s->mmdio = v16;
        if (v16 & MDIO_READ) {
            int phy = (v16 >> PHYAD_SHIFT) & 0x1f;
            int reg = v16 & 0x1f;
            uint16_t rval = 0xffff;
            if (phy == 0) {
                if (reg < 32) {
                    rval = s->phy_regs[reg];
                }
            }
            s->mmrd = rval;
            s->mmdio &= ~MDIO_READ;
        } else if (v16 & MDIO_WRITE) {
            int phy = (v16 >> PHYAD_SHIFT) & 0x1f;
            int reg = v16 & 0x1f;
            if (phy == 0 && reg < 32) {
                s->phy_regs[reg] = s->mmwd;
            }
            s->mmdio &= ~MDIO_WRITE;
        }
        break;
    }
    case MMRD:
        s->mmrd = v16;
        break;
    case MMWD:
        s->mmwd = v16;
        break;
    case MTD_SA0:
        s->mtd_sa0 = v16;
        s->tx_ring_dma_lo = v16;
        break;
    case MTD_SA1:
        s->mtd_sa1 = v16;
        s->tx_ring_dma_hi = v16;
        break;
    case MRD_SA0:
        s->mrd_sa0 = v16;
        s->rx_ring_dma_lo = v16;
        break;
    case MRD_SA1:
        s->mrd_sa1 = v16;
        s->rx_ring_dma_hi = v16;
        break;
    case MISR:
        s->misr = (uint16_t)(s->misr & ~v16);
        pcibase_update_irq(s);
        break;
    case MIER:
        s->mier = v16;
        pcibase_update_irq(s);
        break;
    case ME_CISR:
        s->me_cisr = v16;
        break;
    case ME_CIER:
        s->me_cier = v16;
        break;
    case MAR0:
        s->mar[0] = v16;
        break;
    case MAR1:
        s->mar[1] = v16;
        break;
    case MAR2:
        s->mar[2] = v16;
        break;
    case MAR3:
        s->mar[3] = v16;
        break;
    case MID_0L:
        s->mid[0][0] = v16;
        break;
    case MID_0M:
        s->mid[0][1] = v16;
        break;
    case MID_0H:
        s->mid[0][2] = v16;
        break;
    case MID_1L:
        s->mid[1][0] = v16;
        break;
    case MID_1M:
        s->mid[1][1] = v16;
        break;
    case MID_1H:
        s->mid[1][2] = v16;
        break;
    case MID_2L:
        s->mid[2][0] = v16;
        break;
    case MID_2M:
        s->mid[2][1] = v16;
        break;
    case MID_2H:
        s->mid[2][2] = v16;
        break;
    case MID_3L:
        s->mid[3][0] = v16;
        break;
    case MID_3M:
        s->mid[3][1] = v16;
        break;
    case MID_3H:
        s->mid[3][2] = v16;
        break;
    case PHY_CC:
        s->phy_cc = v16;
        if (s->phy_cc == 0) {
            s->phy_cc = SCEN | (0x1f << PHYAD_SHIFT) | (7 << TMRDIV_SHIFT);
        }
        break;
    case PHY_ST:
        s->phy_st = v16;
        break;
    case MAC_SM:
        s->mac_sm = v16;
        if (v16 & MAC_SM_RST) {
            s->mac_sm = 0;
        }
        break;
    case MD_CSC:
        s->md_csc = v16;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    uint64_t val = 0;

    /* No legacy PIO space defined for this device in the driver. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
    /* No legacy PIO space defined for this device in the driver. */
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

    /* Reset register shadows to power-on defaults where known */
    s->mcr0 = 0;
    s->mcr1 = 0;
    s->mbcr = MBCR_DEFAULT;
    s->mt_icr = 0;
    s->mr_icr = 0;
    s->mtpr = 0;
    s->mr_bsr = MAX_BUF_SIZE;
    s->mr_dcr = 0;
    s->mlsr = 0;
    s->mmdio = 0;
    s->mmrd = 0;
    s->mmwd = 0;
    s->mtd_sa0 = 0;
    s->mtd_sa1 = 0;
    s->mrd_sa0 = 0;
    s->mrd_sa1 = 0;
    s->misr = 0;
    s->mier = MSK_INT;
    s->me_cisr = 0;
    s->me_cier = 0;
    for (int i = 0; i < 4; i++) {
        s->mar[i] = 0;
        for (int j = 0; j < 3; j++) {
            s->mid[i][j] = 0;
        }
    }
    s->phy_cc = 0;
    s->phy_st = 0;
    s->mac_sm = 0;
    s->md_csc = MD_CSC_DEFAULT;
    s->intr_status = 0;
    s->intr_mask = INT_MASK;

    s->tx_ring_dma_lo = 0;
    s->tx_ring_dma_hi = 0;
    s->rx_ring_dma_lo = 0;
    s->rx_ring_dma_hi = 0;
    s->mac_rx_enabled = false;
    s->mac_tx_enabled = false;

    /* Initialize simple PHY defaults for basic link up */
    for (int r = 0; r < 32; r++) {
        s->phy_regs[r] = 0;
    }
    s->phy_regs[1] = 0x782d;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  R6040_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  R6040_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, R6040_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = R6040_IO_SIZE;
    s->bar_info[0].name = "r6040-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "r6040_pci",
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
