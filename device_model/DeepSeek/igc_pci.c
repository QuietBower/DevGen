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

#define TYPE_PCIBASE_DEVICE "igc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* PCI Identifiers */
#define VENDOR_ID PCI_VENDOR_ID_INTEL
#define DEVICE_ID 0x15F2  /* IGC_DEV_ID_I225_LM */
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

/* BAR0 size (driver expects 128KB) */
#define IGC_BAR0_SIZE 0x20000

/* Driver register offsets and constants */
#define IGC_CTRL           0x00000
#define IGC_STATUS         0x00008
#define IGC_CTRL_EXT       0x00018
#define IGC_CTRL_EXT_DRV_LOAD  0x10000000
#define IGC_EECD           0x00010
#define IGC_MDIC           0x00020
#define IGC_CONNSW         0x00034
#define IGC_FCAL           0x00028
#define IGC_FCAH           0x0002C
#define IGC_FCT            0x00030
#define IGC_FCTTV          0x00170
#define IGC_VET            0x00038
#define IGC_TSSDP          0x0003C
#define IGC_RCTL           0x00100
#define IGC_TCTL           0x00400
#define IGC_TIPG           0x00410
#define IGC_LEDCTL         0x00E00
#define IGC_IPCNFG         0x0E38
#define IGC_EEE_SU         0x0E34
#define IGC_EEER           0x0E30
#define IGC_FCRTL          0x02160
#define IGC_FCRTH          0x02168
#define IGC_RXPBS          0x02404
#define IGC_TXPBS          0x03404
#define IGC_RDBAL(_n)      (0x0C000 + ((_n) * 0x40))
#define IGC_RDBAH(_n)      (0x0C004 + ((_n) * 0x40))
#define IGC_RDLEN(_n)      (0x0C008 + ((_n) * 0x40))
#define IGC_SRRCTL(_n)     (0x0C00C + ((_n) * 0x40))
#define IGC_RDH(_n)        (0x0C010 + ((_n) * 0x40))
#define IGC_RDT(_n)        (0x0C018 + ((_n) * 0x40))
#define IGC_RXDCTL(_n)     (0x0C028 + ((_n) * 0x40))
#define IGC_RQDPC(_n)      (0x0C030 + ((_n) * 0x40))
#define IGC_TDBAL(_n)      (0x0E000 + ((_n) * 0x40))
#define IGC_TDBAH(_n)      (0x0E004 + ((_n) * 0x40))
#define IGC_TDLEN(_n)      (0x0E008 + ((_n) * 0x40))
#define IGC_TDH(_n)        (0x0E010 + ((_n) * 0x40))
#define IGC_TDT(_n)        (0x0E018 + ((_n) * 0x40))
#define IGC_TXDCTL(_n)     (0x0E028 + ((_n) * 0x40))
#define IGC_RXCSUM         0x05000
#define IGC_RLPML          0x05004
#define IGC_RFCTL          0x05008
#define IGC_MTA            0x05200
#define IGC_RAL(_n)        (0x05400 + ((_n) * 0x08))
#define IGC_RAH(_n)        (0x05404 + ((_n) * 0x08))
#define IGC_PSRTYPE(_i)    (0x05480 + ((_i) * 4))
#define IGC_VLANPQF        0x055B0
#define IGC_WUC            0x05800
#define IGC_FHFTSL         0x05804
#define IGC_WUFC           0x05808
#define IGC_WUFC_EXT       0x0580C
#define IGC_WUS            0x05810
#define IGC_MRQC           0x05818
#define IGC_MANC           0x05820
#define IGC_RETA(_i)       (0x05C00 + ((_i) * 4))
#define IGC_RSSRK(_i)      (0x05C80 + ((_i) * 4))
#define IGC_ETQF(_n)       (0x05CB0 + (4 * (_n)))
#define IGC_FHFT(_n)       (0x09000 + (256 * (_n)))
#define IGC_FHFT_EXT(_n)   (0x09A00 + (256 * (_n)))
#define IGC_WUPM_REG(_i)   (0x05A00 + ((_i) * 4))
#define IGC_FACTPS         0x05B30
#define IGC_FWSM           0x05B54
#define IGC_ICR            0x01500
#define IGC_ICS            0x01504
#define IGC_IMS            0x01508
#define IGC_IMC            0x0150C
#define IGC_IAM            0x01510
#define IGC_GPIE           0x01514
#define IGC_EICS           0x01520
#define IGC_EIMS           0x01524
#define IGC_EIMC           0x01528
#define IGC_EIAC           0x0152C
#define IGC_EIAM           0x01530
#define IGC_IVAR0          0x01700
#define IGC_IVAR_MISC      0x01740
#define IGC_EITR(_n)       (0x01680 + (0x4 * (_n)))
#define IGC_SYSTIML        0x0B600
#define IGC_SYSTIMH        0x0B604
#define IGC_TIMINCA        0x0B608
#define IGC_TIMADJ         0x0B60C
#define IGC_TSYNCTXCTL    0x0B614
#define IGC_TXSTMPL        0x0B618
#define IGC_TXSTMPH        0x0B61C
#define IGC_TSYNCRXCTL    0x0B620
#define IGC_TSAUXC         0x0B640
#define IGC_TRGTTIML0      0x0B644
#define IGC_TRGTTIMH0      0x0B648
#define IGC_TRGTTIML1      0x0B64C
#define IGC_TRGTTIMH1      0x0B650
#define IGC_FREQOUT0       0x0B654
#define IGC_FREQOUT1       0x0B658
#define IGC_AUXSTMPL0      0x0B65C
#define IGC_AUXSTMPH0      0x0B660
#define IGC_AUXSTMPL1      0x0B664
#define IGC_AUXSTMPH1      0x0B668
#define IGC_TSICR          0x0B66C
#define IGC_TSIM           0x0B674
#define IGC_TXSTMPL_0      0x0B618
#define IGC_TXSTMPH_0      0x0B61C
#define IGC_TXSTMPL_1      0x0B698
#define IGC_TXSTMPH_1      0x0B69C
#define IGC_TXSTMPL_2      0x0B6B8
#define IGC_TXSTMPH_2      0x0B6BC
#define IGC_TXSTMPL_3      0x0B6D8
#define IGC_TXSTMPH_3      0x0B6DC
#define IGC_PTM_CTRL       0x12540
#define IGC_PTM_STAT       0x12544
#define IGC_PTM_CYCLE_CTRL  0x1254C
#define IGC_PCIE_DIG_DELAY 0x12550
#define IGC_PCIE_PHY_DELAY 0x12554
#define IGC_GTXOFFSET      0x3310
#define IGC_BASET_L        0x3314
#define IGC_BASET_H        0x3318
#define IGC_QBVCYCLET      0x331C
#define IGC_QBVCYCLET_S    0x3320
#define IGC_STQT(_n)       (0x3324 + 0x4 * (_n))
#define IGC_ENDQT(_n)      (0x3334 + 0x4 * (_n))
#define IGC_TXQCTL(_n)     (0x3344 + 0x4 * (_n))
#define IGC_TXARB          0x3354
#define IGC_DTXMXPKTSZ     0x355C
#define IGC_TQAVCTRL       0x3570
#define IGC_RXPBS_CFG_TS_EN_MASK  GENMASK(31, 31)
#define IGC_TXDCTL_PRIORITY_MASK  GENMASK(27, 27)
#define IGC_RETX_CTL       0x041C
#define IGC_TQAVCC(_n)     (0x3004 + ((_n) * 0x40))
#define IGC_TQAVHC(_n)     (0x300C + ((_n) * 0x40))

/* Stats registers */
#define IGC_CRCERRS        0x04000
#define IGC_ALGNERRC       0x04004
#define IGC_MPC            0x04010
#define IGC_SCC            0x04014
#define IGC_ECOL           0x04018
#define IGC_MCC            0x0401C
#define IGC_LATECOL        0x04020
#define IGC_COLC           0x04028
#define IGC_DC             0x04030
#define IGC_RERC           0x0402C
#define IGC_RLEC           0x04040
#define IGC_XONRXC         0x04048
#define IGC_XONTXC         0x0404C
#define IGC_XOFFRXC        0x04050
#define IGC_XOFFTXC        0x04054
#define IGC_FCRUC          0x04058
#define IGC_PRC64          0x0405C
#define IGC_PRC127         0x04060
#define IGC_PRC255         0x04064
#define IGC_PRC511         0x04068
#define IGC_PRC1023        0x0406C
#define IGC_PRC1522        0x04070
#define IGC_GPRC           0x04074
#define IGC_BPRC           0x04078
#define IGC_MPRC           0x0407C
#define IGC_GPTC           0x04080
#define IGC_GORCL          0x04088
#define IGC_GORCH          0x0408C
#define IGC_GOTCL          0x04090
#define IGC_GOTCH          0x04094
#define IGC_RNBC           0x040A0
#define IGC_RUC            0x040A4
#define IGC_RFC            0x040A8
#define IGC_ROC            0x040AC
#define IGC_RJC            0x040B0
#define IGC_MGTPRC         0x040B4
#define IGC_MGTPDC         0x040B8
#define IGC_MGTPTC         0x040BC
#define IGC_TORH           0x040C4
#define IGC_TOTH           0x040CC
#define IGC_TPR            0x040D0
#define IGC_TPT            0x040D4
#define IGC_PTC64          0x040D8
#define IGC_PTC127         0x040DC
#define IGC_PTC255         0x040E0
#define IGC_PTC511         0x040E4
#define IGC_PTC1023        0x040E8
#define IGC_PTC1522        0x040EC
#define IGC_MPTC           0x040F0
#define IGC_BPTC           0x040F4
#define IGC_TSCTC          0x040F8
#define IGC_IAC            0x04100
#define IGC_RLPIC          0x0414C
#define IGC_TLPIC          0x04148
#define IGC_HGPTC          0x04118

/* Max queues */
#define IGC_MAX_TX_QUEUES  4
#define IGC_MAX_RX_QUEUES  4
#define MAX_Q_VECTORS      8

/* Flow control */
#define IGC_FCRTV          0x02460

/* Timestamp */
#define IGC_TXSTMPH_0      0x0B61C

/* Bits */
#define IGC_CTRL_VME       0x40000000
#define IGC_CTRL_RFCE      0x08000000
#define IGC_CTRL_TFCE      0x10000000
#define IGC_CTRL_GIO_MASTER_DISABLE 0x00000004
#define IGC_STATUS_LU      0x00000002
#define IGC_STATUS_FD      0x00000001
#define IGC_STATUS_TXOFF   0x00000010
#define IGC_STATUS_SPEED_100 0x00000040
#define IGC_STATUS_SPEED_1000 0x00000080
#define IGC_STATUS_SPEED_2500 0x00400000
#define IGC_ICR_INT_ASSERTED BIT(31)
#define IGC_ICR_TXDW       BIT(0)
#define IGC_ICR_LSC        BIT(2)
#define IGC_ICR_RXSEQ      BIT(3)
#define IGC_ICR_RXDMT0     BIT(4)
#define IGC_ICR_RXT0       BIT(7)
#define IGC_ICR_DRSTA      BIT(30)
#define IGC_ICR_DOUTSYNC   0x10000000
#define IGC_ICR_TS         BIT(19)
#define IGC_GPIE_EIAME     0x40000000
#define IGC_GPIE_PBA       0x80000000
#define IGC_GPIE_MSIX_MODE 0x00000010
#define IGC_GPIE_NSICR     0x00000001
#define IGC_TSICR_INTERRUPTS IGC_TSICR_TXTS

/* Helper macros */
#define GENMASK(h, l) (((1U << ((h) - (l) + 1)) - 1) << (l))
#define FIELD_PREP(mask, val) (((typeof(mask))val) << (ffsll(mask) - 1))
#define BIT(nr) (1UL << (nr))
#define ffsll(x) ({ unsigned int _x = (x), _v = 1; if (_x == 0) return 0; while (!(_x & 1)) { _x >>= 1; _v++; } _v; })

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
    MemoryRegion msix_bar;
    uint32_t icr;
    uint32_t ims;
    uint32_t imc;
    uint32_t eims;
    uint32_t eimc;
    uint32_t eiac;
    uint32_t iam;
    uint32_t eitr[10];

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t mmio_regs[IGC_BAR0_SIZE / 4];

    /* DMA Context */
    dma_addr_t tx_ring_base[IGC_MAX_TX_QUEUES];
    dma_addr_t rx_ring_base[IGC_MAX_RX_QUEUES];
    uint32_t tx_ring_len[IGC_MAX_TX_QUEUES];
    uint32_t rx_ring_len[IGC_MAX_RX_QUEUES];

    /* Operational status flags */
    uint32_t status;

    /* Probe/Reset state */
    uint32_t reset_ctrl;

    /* Power management state */
    uint32_t pm_state;

    /* MDIO state */
    uint32_t mdic_cmd;
    bool mdic_op_pending;
};

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (addr >= IGC_BAR0_SIZE) {
        return val;
    }

    /* Ensure 32-bit aligned access */
    if (addr & 3) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: unaligned read at 0x%"HWADDR_PRIx"
", addr);
        return val;
    }

    uint32_t reg = addr / 4;
    val = s->mmio_regs[reg];

    switch (addr) {
    case IGC_ICR:
        /* Reading ICR clears the register */
        s->mmio_regs[reg] = 0;
        break;
    case IGC_ICS:
    case IGC_IMS:
    case IGC_IMC:
    case IGC_EICS:
    case IGC_EIMS:
    case IGC_EIMC:
    case IGC_EIAC:
    case IGC_EIAM:
    case IGC_IAM:
        /* These are write-only but driver may read. Return stored. */
        break;
    case IGC_MDIC:
        /* Special handling for MDIC */
        if (s->mdic_op_pending) {
            /* Complete pending operation: set Ready bit (bit 16), data = 0 */
            val = (s->mdic_cmd & 0xFFFF0000) | (1 << 16);
            s->mdic_op_pending = false;
        }
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= IGC_BAR0_SIZE) {
        return;
    }

    if (addr & 3) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: unaligned write at 0x%"HWADDR_PRIx"
", addr);
        return;
    }

    uint32_t reg = addr / 4;

    switch (addr) {
    case IGC_ICR:
        /* Write-1-to-clear */
        s->mmio_regs[reg] &= ~val;
        break;
    case IGC_ICS:
        /* Write-1-to-set bits in ICR */
        s->mmio_regs[IGC_ICR / 4] |= val;
        break;
    case IGC_IMS:
        s->mmio_regs[reg] |= val;
        break;
    case IGC_IMC:
        s->mmio_regs[reg] &= ~val;
        break;
    case IGC_EIMS:
        s->mmio_regs[reg] |= val;
        break;
    case IGC_EIMC:
        s->mmio_regs[reg] &= ~val;
        break;
    case IGC_EIAC:
        s->mmio_regs[reg] = val;
        break;
    case IGC_EIAM:
        s->mmio_regs[reg] = val;
        break;
    case IGC_EICS:
        /* Write-1-to-set bits in EICR (linked to ICR) */
        s->mmio_regs[IGC_ICR / 4] |= val;
        break;
    case IGC_IAM:
        s->mmio_regs[reg] = val;
        break;
    case IGC_MDIC:
        /* MDIO command */
        s->mdic_cmd = val;
        s->mdic_op_pending = true;
        break;
    default:
        s->mmio_regs[reg] = val;
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

    /* Clear all registers except MAC address and EECD */
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    /* Preserve MAC address (RAL0, RAH0) */
    uint8_t mac[] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    s->mmio_regs[IGC_RAL(0) / 4] = mac[0] | (mac[1] << 8) | (mac[2] << 16) | (mac[3] << 24);
    s->mmio_regs[IGC_RAH(0) / 4] = mac[4] | (mac[5] << 8) | (1 << 31);
    /* MDIC state */
    s->mdic_cmd = 0;
    s->mdic_op_pending = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = IGC_BAR0_SIZE;
    s->bar_info[0].name = "igc-bar0";
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "igc-msix";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X capability */
    uint8_t msix_cap = pci_add_capability(pdev, PCI_CAP_ID_MSIX, 0, 12, errp);
    if (msix_cap == 0) {
        error_setg(errp, "Failed to add MSI-X capability");
        return;
    }

    /* MSI/MSI-X init */
    s->has_msix = true;
    if (s->has_msix) {
        msix_init(pdev, MAX_Q_VECTORS, 0, &s->bar_regions[1], 1, 0x800, msix_cap, errp);
    } else if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
    }

    /* Field Init */
    s->status = 0;
    s->reset_ctrl = 0;
    s->pm_state = 0;
    s->mdic_cmd = 0;
    s->mdic_op_pending = false;

    /* Set default MAC address (will be preserved across resets) */
    uint8_t mac[] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    s->mmio_regs[IGC_RAL(0) / 4] = mac[0] | (mac[1] << 8) | (mac[2] << 16) | (mac[3] << 24);
    s->mmio_regs[IGC_RAH(0) / 4] = mac[4] | (mac[5] << 8) | (1 << 31);

    /* Set EECD to indicate no flash present (simplifies probe) */
    s->mmio_regs[IGC_EECD / 4] = 0;
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "igc_pci",
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
