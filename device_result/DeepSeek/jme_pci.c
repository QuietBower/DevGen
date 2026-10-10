/*
 * QEMU PCI device model for JMicron JMC250/JMC260 Ethernet Controller
 * Based on Linux driver jme.c, QEMU 8.2.10
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

/* Driver-specific definitions */
#define PCI_VENDOR_ID_JMICRON              0x197B
#define PCI_DEVICE_ID_JMICRON_JMC250       0x0250
#define PCI_DEVICE_ID_JMICRON_JMC260       0x0260
#define PCI_CLASS_NETWORK_ETHERNET         0x0200
#define JME_REG_LEN                        0x500
#define TX_RING_NR                          8
#define RX_RING_NR                          4
#define TX_DESC_SIZE                       16
#define RX_DESC_SIZE                       16
#define RING_DESC_ALIGN                    16
#define INTR_ENABLE                        (INTR_SWINTR | \
                                            INTR_TMINTR | \
                                            INTR_LINKCH | \
                                            INTR_PCCRX0TO | \
                                            INTR_PCCRX0 | \
                                            INTR_PCCTXTO | \
                                            INTR_PCCTX | \
                                            INTR_RX0EMP)

#define TYPE_PCIBASE_DEVICE "jme_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Descriptor structures as defined by the hardware */
struct rxdesc {
    union {
        uint8_t all[16];
        uint32_t dw[4];
        struct {
            uint16_t rsv2;
            uint8_t rsv1;
            uint8_t flags;
            uint16_t datalen;
            uint16_t wbcpl;
            uint32_t bufaddrh;
            uint32_t bufaddrl;
        } desc1;
        struct {
            uint16_t vlan;
            uint16_t flags;
            uint16_t framesize;
            uint8_t errstat;
            uint8_t desccnt;
            uint32_t rsshash;
            uint8_t hashfun;
            uint8_t hashtype;
            uint16_t resrv;
        } descwb;
    };
};

struct txdesc {
    union {
        uint8_t all[16];
        uint32_t dw[4];
        struct {
            uint16_t vlan;
            uint8_t rsv1;
            uint8_t flags;
            uint16_t datalen;
            uint16_t mss;
            uint16_t pktsize;
            uint16_t rsv2;
            uint32_t bufaddr;
        } desc1;
        struct {
            uint16_t rsv1;
            uint8_t rsv2;
            uint8_t flags;
            uint16_t datalen;
            uint16_t rsv3;
            uint32_t bufaddrh;
            uint32_t bufaddrl;
        } desc2;
        struct {
            uint8_t ehdrsz;
            uint8_t rsv1;
            uint8_t rsv2;
            uint8_t flags;
            uint16_t trycnt;
            uint16_t segcnt;
            uint16_t pktsz;
            uint16_t rsv3;
            uint32_t bufaddrl;
        } descwb;
    };
};

/* SMI/MDIO PHY constants */
#define SMI_OP_REQ          0x80000000
#define SMI_OP_WRITE        0x40000000
#define SMI_DATA_SHIFT      0
#define SMI_DATA_MASK       0xFFFF
#define SMI_REG_ADDR_SHIFT  20
#define SMI_REG_ADDR_MASK   0x1F
#define SMI_PHY_ADDR_SHIFT  16
#define SMI_PHY_ADDR_MASK   0x1F

/* SMBCSR constants */
#define SMBCSR_EEPROMD      0x00000010
#define SMBCSR_RELOAD       0x00000020
#define SMBCSR_CNACK        0x00000040

/* Interrupt bits (placeholder values, need correct defines) */
#define INTR_SWINTR         0x00000001
#define INTR_TMINTR         0x00000002
#define INTR_LINKCH         0x00000004
#define INTR_PCCRX0TO       0x00000008
#define INTR_PCCRX0         0x00000010
#define INTR_PCCTXTO        0x00000020
#define INTR_PCCTX          0x00000040
#define INTR_RX0EMP         0x00000080
#define INTR_TX0            0x00000100
#define INTR_RX0            0x00000200

/* Register offsets (placeholder macros; actual values to be provided later) */
#define JME_SMI             0x0000
#define JME_WFOI            0x0004
#define JME_WFODP           0x0008
#define JME_GPREG0          0x000C
#define JME_GPREG1          0x0010
#define JME_GHC             0x0014
#define JME_PMCS            0x0018
#define JME_SMBCSR          0x001C
#define JME_SMBINTF         0x0020
#define JME_RXDBA_LO        0x0024
#define JME_RXDBA_HI        0x0028
#define JME_RXQDC           0x002C
#define JME_RXNDA           0x0030
#define JME_TXDBA_LO        0x0034
#define JME_TXDBA_HI        0x0038
#define JME_TXQDC           0x003C
#define JME_TXNDA           0x0040
#define JME_RXMCHT_LO       0x0044
#define JME_RXMCHT_HI       0x0048
#define JME_RXUMA_LO        0x004C
#define JME_RXUMA_HI        0x0050
#define JME_PCCRX0          0x0054
#define JME_PCCTX           0x0058
#define JME_IENS            0x005C
#define JME_IENC            0x0060
#define JME_IEVE            0x0064
#define JME_TMCSR           0x0068
#define JME_TIMER2          0x006C
#define JME_PHY_PWR         0x0070
#define JME_APMC            0x0074
#define JME_TXCS            0x0078
#define JME_RXCS            0x007C
#define JME_TXMCS           0x0080
#define JME_TXTRHD          0x0084
#define JME_CHIPMODE        0x0088
#define JME_PHY_LINK        0x008C

/* Additional missing defines we need but are not provided */
/* #define CM_FPGAVER_SHIFT   16 */
/* #define CM_CHIPREV_SHIFT   0 */
/* etc. We will list them in needed_sources */

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
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

    /* DMA descriptor rings */
    struct rxdesc rx_ring[RX_RING_NR];
    struct txdesc tx_ring[TX_RING_NR];
    dma_addr_t rx_ring_phys;
    dma_addr_t tx_ring_phys;
    int rx_head, rx_tail;
    int tx_head, tx_tail;

    /* Shadow registers */
    uint32_t reg_smi;
    uint32_t reg_wfoi;
    uint32_t reg_wfodp;
    uint32_t reg_gpreg0;
    uint32_t reg_gpreg1;
    uint32_t reg_ghc;
    uint32_t reg_pmcs;
    uint32_t reg_smbcsr;
    uint32_t reg_smbintf;
    uint32_t reg_rxdba_lo;
    uint32_t reg_rxdba_hi;
    uint32_t reg_rxqdc;
    uint32_t reg_rxnda;
    uint32_t reg_txdba_lo;
    uint32_t reg_txdba_hi;
    uint32_t reg_txqdc;
    uint32_t reg_txnda;
    uint32_t reg_rxmcht_lo;
    uint32_t reg_rxmcht_hi;
    uint32_t reg_rxuma_lo;
    uint32_t reg_rxuma_hi;
    uint32_t reg_pccrx0;
    uint32_t reg_pcctx;
    uint32_t reg_tmcsr;
    uint32_t reg_timer2;
    uint32_t reg_phy_pwr;
    uint32_t reg_apmc;
    uint32_t reg_txcs;
    uint32_t reg_rxcs;
    uint32_t reg_txmcs;
    uint32_t reg_txtrhd;
    uint32_t reg_chipmode;
    uint32_t reg_phy_link;

    /* SMBus reload state */
    bool smb_reload;

    /* PHY registers (32 PHY addresses * 32 registers) */
    uint16_t phy_regs[32][32];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->intr_status & s->intr_mask;

    if (msi_enabled(pdev)) {
        if (active) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, !!active);
    }
}

static void pcibase_smi_process(PCIBaseState *s)
{
    uint32_t val = s->reg_smi;
    if (!(val & SMI_OP_REQ)) {
        return;
    }

    uint32_t phy = (val >> SMI_PHY_ADDR_SHIFT) & SMI_PHY_ADDR_MASK;
    uint32_t reg = (val >> SMI_REG_ADDR_SHIFT) & SMI_REG_ADDR_MASK;
    bool write = !!(val & SMI_OP_WRITE);
    uint16_t data = (val >> SMI_DATA_SHIFT) & SMI_DATA_MASK;

    if (write) {
        s->phy_regs[phy][reg] = data;
    } else {
        data = s->phy_regs[phy][reg];
    }

    /* Clear REQ and set data */
    s->reg_smi = (data << SMI_DATA_SHIFT) & SMI_DATA_MASK;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        return 0; /* driver always uses 32-bit accesses */
    }

    switch (addr) {
    case JME_SMI:
        val = s->reg_smi;
        break;
    case JME_WFOI:
        val = s->reg_wfoi;
        break;
    case JME_WFODP:
        val = s->reg_wfodp;
        break;
    case JME_GPREG0:
        val = s->reg_gpreg0;
        break;
    case JME_GPREG1:
        val = s->reg_gpreg1;
        break;
    case JME_GHC:
        val = s->reg_ghc;
        break;
    case JME_PMCS:
        val = s->reg_pmcs;
        break;
    case JME_SMBCSR:
        val = s->reg_smbcsr;
        if (s->smb_reload) {
            val &= ~SMBCSR_RELOAD;
            s->smb_reload = false;
            s->reg_smbcsr &= ~SMBCSR_RELOAD;
        }
        break;
    case JME_SMBINTF:
        val = s->reg_smbintf;
        break;
    case JME_RXDBA_LO:
        val = s->reg_rxdba_lo;
        break;
    case JME_RXDBA_HI:
        val = s->reg_rxdba_hi;
        break;
    case JME_RXQDC:
        val = s->reg_rxqdc;
        break;
    case JME_RXNDA:
        val = s->reg_rxnda;
        break;
    case JME_TXDBA_LO:
        val = s->reg_txdba_lo;
        break;
    case JME_TXDBA_HI:
        val = s->reg_txdba_hi;
        break;
    case JME_TXQDC:
        val = s->reg_txqdc;
        break;
    case JME_TXNDA:
        val = s->reg_txnda;
        break;
    case JME_RXMCHT_LO:
        val = s->reg_rxmcht_lo;
        break;
    case JME_RXMCHT_HI:
        val = s->reg_rxmcht_hi;
        break;
    case JME_RXUMA_LO:
        val = s->reg_rxuma_lo;
        break;
    case JME_RXUMA_HI:
        val = s->reg_rxuma_hi;
        break;
    case JME_PCCRX0:
        val = s->reg_pccrx0;
        break;
    case JME_PCCTX:
        val = s->reg_pcctx;
        break;
    case JME_IENS:
        /* Not readable? Driver does not read IENS. */
        val = 0;
        break;
    case JME_IENC:
        /* Not readable? */
        val = 0;
        break;
    case JME_IEVE:
        val = s->intr_status;
        break;
    case JME_TMCSR:
        val = s->reg_tmcsr;
        break;
    case JME_TIMER2:
        val = s->reg_timer2;
        break;
    case JME_PHY_PWR:
        val = s->reg_phy_pwr;
        break;
    case JME_APMC:
        val = s->reg_apmc;
        break;
    case JME_TXCS:
        val = s->reg_txcs;
        break;
    case JME_RXCS:
        val = s->reg_rxcs;
        break;
    case JME_TXMCS:
        val = s->reg_txmcs;
        break;
    case JME_TXTRHD:
        val = s->reg_txtrhd;
        break;
    case JME_CHIPMODE:
        val = s->reg_chipmode;
        break;
    case JME_PHY_LINK:
        val = s->reg_phy_link;
        break;
    default:
        /* Unknown register, return 0 */
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
    case JME_SMI:
        s->reg_smi = val;
        pcibase_smi_process(s);
        break;
    case JME_WFOI:
        s->reg_wfoi = val;
        break;
    case JME_WFODP:
        s->reg_wfodp = val;
        break;
    case JME_GPREG0:
        s->reg_gpreg0 = val;
        break;
    case JME_GPREG1:
        s->reg_gpreg1 = val;
        break;
    case JME_GHC:
        s->reg_ghc = val;
        break;
    case JME_PMCS:
        s->reg_pmcs = val;
        break;
    case JME_SMBCSR:
        s->reg_smbcsr = val;
        if (val & SMBCSR_RELOAD) {
            s->smb_reload = true;
            s->reg_smbcsr |= SMBCSR_RELOAD;
        }
        break;
    case JME_SMBINTF:
        s->reg_smbintf = val;
        break;
    case JME_RXDBA_LO:
        s->reg_rxdba_lo = val;
        break;
    case JME_RXDBA_HI:
        s->reg_rxdba_hi = val;
        break;
    case JME_RXQDC:
        s->reg_rxqdc = val;
        break;
    case JME_RXNDA:
        s->reg_rxnda = val;
        break;
    case JME_TXDBA_LO:
        s->reg_txdba_lo = val;
        break;
    case JME_TXDBA_HI:
        s->reg_txdba_hi = val;
        break;
    case JME_TXQDC:
        s->reg_txqdc = val;
        break;
    case JME_TXNDA:
        s->reg_txnda = val;
        break;
    case JME_RXMCHT_LO:
        s->reg_rxmcht_lo = val;
        break;
    case JME_RXMCHT_HI:
        s->reg_rxmcht_hi = val;
        break;
    case JME_RXUMA_LO:
        s->reg_rxuma_lo = val;
        break;
    case JME_RXUMA_HI:
        s->reg_rxuma_hi = val;
        break;
    case JME_PCCRX0:
        s->reg_pccrx0 = val;
        break;
    case JME_PCCTX:
        s->reg_pcctx = val;
        break;
    case JME_IENS:
        s->intr_mask |= val;
        pcibase_update_irq(s);
        break;
    case JME_IENC:
        s->intr_mask &= ~val;
        pcibase_update_irq(s);
        break;
    case JME_IEVE:
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case JME_TMCSR:
        s->reg_tmcsr = val;
        break;
    case JME_TIMER2:
        s->reg_timer2 = val;
        break;
    case JME_PHY_PWR:
        s->reg_phy_pwr = val;
        break;
    case JME_APMC:
        s->reg_apmc = val;
        break;
    case JME_TXCS:
        s->reg_txcs = val;
        break;
    case JME_RXCS:
        s->reg_rxcs = val;
        break;
    case JME_TXMCS:
        s->reg_txmcs = val;
        break;
    case JME_TXTRHD:
        s->reg_txtrhd = val;
        break;
    case JME_CHIPMODE:
        s->reg_chipmode = val;
        break;
    case JME_PHY_LINK:
        s->reg_phy_link = val;
        break;
    default:
        /* Unknown register, ignore */
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

    /* Reset all shadow registers to default values */
    s->reg_smi = 0;
    s->reg_wfoi = 0;
    s->reg_wfodp = 0;
    s->reg_gpreg0 = 0;
    s->reg_gpreg1 = 0;
    s->reg_ghc = 0;
    s->reg_pmcs = 0;
    s->reg_smbcsr = SMBCSR_EEPROMD; /* Indicate EEPROM present */
    s->reg_smbintf = 0;
    s->reg_rxdba_lo = 0;
    s->reg_rxdba_hi = 0;
    s->reg_rxqdc = 0;
    s->reg_rxnda = 0;
    s->reg_txdba_lo = 0;
    s->reg_txdba_hi = 0;
    s->reg_txqdc = 0;
    s->reg_txnda = 0;
    s->reg_rxmcht_lo = 0;
    s->reg_rxmcht_hi = 0;
    s->reg_rxuma_lo = 0x33221100; /* MAC: 00:11:22:33:44:55 */
    s->reg_rxuma_hi = 0x00005544;
    s->reg_pccrx0 = 0;
    s->reg_pcctx = 0;
    s->reg_tmcsr = 0;
    s->reg_timer2 = 0;
    s->reg_phy_pwr = 0;
    s->reg_apmc = 0;
    s->reg_txcs = 0;
    s->reg_rxcs = 0;
    s->reg_txmcs = 0;
    s->reg_txtrhd = 0;
    s->reg_chipmode = 0x00010000; /* chiprev=1, fpgaver=0 */
    s->reg_phy_link = 0; /* Link down initially */

    s->intr_status = 0;
    s->intr_mask = 0;
    s->smb_reload = false;

    /* Initialize PHY registers */
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    /* Set standard PHY IDs (common Gigabit PHY values) */
    s->phy_regs[1][2] = 0x0141; /* OUI high */
    s->phy_regs[1][3] = 0x0C00; /* OUI low and model */
    s->phy_regs[1][0] = 0x1140; /* BMCR: auto-neg enabled, reset */
    s->phy_regs[1][1] = 0x796d; /* BMSR: link up, auto-neg complete, 10/100/1000 capabilities */
    s->phy_regs[1][4] = 0x01e1; /* ADVERTISE: 10/100/1000, pause */
    s->phy_regs[1][9] = 0x0e00; /* 1000BASE-T control: advertise 1000 full */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_JMICRON);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_JMICRON_JMC250);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = JME_REG_LEN,
        .name = "jme-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    s->rx_ring_phys = 0;
    s->tx_ring_phys = 0;
    s->rx_head = s->rx_tail = 0;
    s->tx_head = s->tx_tail = 0;

    s->intr_status = 0;
    s->intr_mask = 0;

    /* Initialize PHY regs? Done in reset, but realize is before reset? We'll rely on reset being called later. */
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
    .name = "jme_pci",
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
