/*
 * QEMU PCI device model for fealnx driver
 * Based on provided template and driver static definitions
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

#define TYPE_PCIBASE_DEVICE "fealnx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets and hardware identifiers extracted from driver */
#define VENDOR_ID 0x1516
#define DEVICE_ID 0x0800
#define CLASS_ID  0x020000

/* Register offsets from enum fealnx_offsets */
#define PAR0_OFFSET        0x00
#define PAR1_OFFSET        0x04
#define MAR0_OFFSET        0x08
#define MAR1_OFFSET        0x0C
#define FAR0_OFFSET        0x10
#define FAR1_OFFSET        0x14
#define TCRRCR_OFFSET      0x18
#define BCR_OFFSET         0x1C
#define TXPDR_OFFSET       0x20
#define RXPDR_OFFSET       0x24
#define RXCWP_OFFSET       0x28
#define TXLBA_OFFSET       0x2C
#define RXLBA_OFFSET       0x30
#define ISR_OFFSET         0x34
#define IMR_OFFSET         0x38
#define FTH_OFFSET         0x3C
#define MANAGEMENT_OFFSET  0x40
#define TALLY_OFFSET       0x44
#define TSR_OFFSET         0x48
#define BMCRSR_OFFSET      0x4C
#define PHYIDENTIFIER_OFFSET 0x50
#define ANARANLPAR_OFFSET  0x54
#define ANEROCR_OFFSET     0x58
#define BPREMRPSR_OFFSET   0x5C

/* Forward declaration for Linux-specific types */
struct sk_buff;

/* Additional static structures and enums from driver */
struct fealnx_desc {
    int32_t status;
    int32_t control;
    uint32_t buffer;
    uint32_t next_desc;
    struct fealnx_desc *next_desc_logical;
    struct sk_buff *skbuff;
    uint32_t reserved1;
    uint32_t reserved2;
};

struct chip_info {
    char *chip_name;
    int flags;
};

enum chip_capability_flags {
    HAS_MII_XCVR,
    HAS_CHIP_XCVR,
};

enum intr_status_bits {
    RFCON = 0x00020000,
    RFCOFF = 0x00010000,
    LSCStatus = 0x00008000,
    ANCStatus = 0x00004000,
    FBE = 0x00002000,
    FBEMask = 0x00001800,
    ParityErr = 0x00000000,
    TargetErr = 0x00001000,
    MasterErr = 0x00000800,
    TUNF = 0x00000400,
    ROVF = 0x00000200,
    ETI = 0x00000100,
    ERI = 0x00000080,
    CNTOVF = 0x00000040,
    RBU = 0x00000020,
    TBU = 0x00000010,
    TI = 0x00000008,
    RI = 0x00000004,
    RxErr = 0x00000002,
};

enum rx_mode_bits {
    CR_W_ENH = 0x02000000,
    CR_W_FD = 0x00100000,
    CR_W_PS10 = 0x00080000,
    CR_W_TXEN = 0x00040000,
    CR_W_PS1000 = 0x00010000,
    CR_W_RXMODEMASK = 0x000000e0,
    CR_W_PROM = 0x00000080,
    CR_W_AB = 0x00000040,
    CR_W_AM = 0x00000020,
    CR_W_ARP = 0x00000008,
    CR_W_ALP = 0x00000004,
    CR_W_SEP = 0x00000002,
    CR_W_RXEN = 0x00000001,
    CR_R_TXSTOP = 0x04000000,
    CR_R_FD = 0x00100000,
    CR_R_PS10 = 0x00080000,
    CR_R_RXSTOP = 0x00008000,
};

enum phy_type_flags {
    MysonPHY = 1,
    AhdocPHY = 2,
    SeeqPHY = 3,
    MarvellPHY = 4,
    Myson981 = 5,
    LevelOnePHY = 6,
    OtherPHY = 10,
};

enum rx_desc_status_bits {
    RXOWN = 0x80000000,
    FLNGMASK = 0x0fff0000,
    FLNGShift = 16,
    MARSTATUS = 0x00004000,
    BARSTATUS = 0x00002000,
    PHYSTATUS = 0x00001000,
    RXFSD = 0x00000800,
    RXLSD = 0x00000400,
    ErrorSummary = 0x80,
    RUNTPKT = 0x40,
    LONGPKT = 0x20,
    FAE = 0x10,
    CRC = 0x08,
    RXER = 0x04,
};

enum rx_desc_control_bits {
    RXIC = 0x00800000,
    RBSShift = 0,
};

enum tx_desc_status_bits {
    TXOWN = 0x80000000,
    JABTO = 0x00004000,
    CSL = 0x00002000,
    LC = 0x00001000,
    EC = 0x00000800,
    UDF = 0x00000400,
    DFR = 0x00000200,
    HF = 0x00000100,
    NCRMask = 0x000000ff,
    NCRShift = 0,
};

enum tx_desc_control_bits {
    TXIC = 0x80000000,
    ETIControl = 0x40000000,
    TXLD = 0x20000000,
    TXFD = 0x10000000,
    CRCEnable = 0x08000000,
    PADEnable = 0x04000000,
    RetryTxLC = 0x02000000,
    PKTSMask = 0x3ff800,
    PKTSShift = 11,
    TBSMask = 0x000007ff,
    TBSShift = 0,
};

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

    /* Hardware Register Shadows */
    uint8_t regs[0x100];

    /* DMA Context */
    uint32_t tx_base;
    uint32_t rx_base;
    uint32_t tx_cur_addr;
    uint32_t rx_cur_addr;

    /* Operational status flags */
    uint32_t status;

    /* State used to handle reset sequences */
    uint8_t mac[6];
};


/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int intr = !!(s->intr_status & s->intr_mask);
    pci_set_irq(pdev, intr);
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_process_tx(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct fealnx_desc desc;

    while (1) {
        /* Read current TX descriptor */
        pci_dma_read(pdev, s->tx_cur_addr, &desc, sizeof(desc));

        if (!(desc.status & TXOWN)) {
            break;
        }

        /* Process descriptor: clear OWN, set success status */
        desc.status = 0;
        pci_dma_write(pdev, s->tx_cur_addr, &desc, sizeof(desc));

        /* Check if interrupt should be raised */
        if (desc.control & TXIC) {
            s->intr_status |= TI;
        }

        /* Move to next descriptor */
        s->tx_cur_addr = desc.next_desc;
    }
    pcibase_update_irq(s);
}

static void pcibase_process_rx(PCIBaseState *s)
{
    /* Minimal RX processing: driver sets RXOWN, we could simulate received packets,
       but for probe this is not required. */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->regs)) {
        return -1;
    }

    switch (addr) {
    case ISR_OFFSET:
        val = s->intr_status;
        break;
    case IMR_OFFSET:
        val = s->intr_mask;
        break;
    case TCRRCR_OFFSET:
        val = ldl_le_p(s->regs + addr);
        if (!(val & CR_W_RXEN)) {
            val |= CR_R_RXSTOP;
        } else {
            val &= ~CR_R_RXSTOP;
        }
        if (!(val & CR_W_TXEN)) {
            val |= CR_R_TXSTOP;
        } else {
            val &= ~CR_R_TXSTOP;
        }
        break;
    case MANAGEMENT_OFFSET:
        val = 0; /* No PHY emulation, return 0 */
        break;
    case BMCRSR_OFFSET:
        val = 0x00040000; /* LinkIsUp2 bit set to indicate link up */
        break;
    case PHYIDENTIFIER_OFFSET:
        val = 0xd0000302; /* MysonPHYID */
        break;
    default:
        if (size == 1) {
            val = s->regs[addr];
        } else if (size == 2) {
            val = lduw_le_p(s->regs + addr);
        } else if (size == 4) {
            val = ldl_le_p(s->regs + addr);
        } else if (size == 8) {
            val = ldq_le_p(s->regs + addr);
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        return;
    }

    switch (addr) {
    case BCR_OFFSET:
        stl_le_p(s->regs + addr, val);
        if (val & 0x01) {
            /* Reset bit set: trigger device reset */
            pci_device_reset(PCI_DEVICE(s));
            /* Clear reset bit after reset */
            s->regs[addr] &= ~0x01;
        }
        break;
    case ISR_OFFSET:
        /* Write-1-to-clear */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case IMR_OFFSET:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case TXPDR_OFFSET:
        /* Writing any value triggers TX processing */
        pcibase_process_tx(s);
        break;
    case RXPDR_OFFSET:
        pcibase_process_rx(s);
        break;
    case TXLBA_OFFSET:
        s->tx_base = val;
        s->tx_cur_addr = val;
        stl_le_p(s->regs + addr, val);
        break;
    case RXLBA_OFFSET:
        s->rx_base = val;
        s->rx_cur_addr = val;
        stl_le_p(s->regs + addr, val);
        break;
    default:
        if (size == 1) {
            s->regs[addr] = val;
        } else if (size == 2) {
            stw_le_p(s->regs + addr, val);
        } else if (size == 4) {
            stl_le_p(s->regs + addr, val);
        } else if (size == 8) {
            stq_le_p(s->regs + addr, val);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by driver */
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

    /* Reset: Revert registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->tx_base = 0;
    s->rx_base = 0;
    s->tx_cur_addr = 0;
    s->rx_cur_addr = 0;

    /* Set default MAC address */
    s->mac[0] = 0x02;
    s->mac[1] = 0x00;
    s->mac[2] = 0x00;
    s->mac[3] = 0x00;
    s->mac[4] = 0x00;
    s->mac[5] = 0x01;
    memcpy(s->regs + PAR0_OFFSET, s->mac, 6);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_config_set_class(pci_conf, CLASS_ID);
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
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "fealnx-bar";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not referenced in driver; placeholder removed */
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
    .name = "fealnx_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(regs, PCIBaseState, 0x100),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
        VMSTATE_UINT32(tx_base, PCIBaseState),
        VMSTATE_UINT32(rx_base, PCIBaseState),
        VMSTATE_UINT32(tx_cur_addr, PCIBaseState),
        VMSTATE_UINT32(rx_cur_addr, PCIBaseState),
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
