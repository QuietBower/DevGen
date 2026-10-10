/*
 * QEMU PCI device model for Sun Cassini (Behavioral Implementation)
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
/* Removed hw/net/pcap.h: not available in QEMU 8.2.10 build environment */

#define TYPE_PCIBASE_DEVICE "cassini_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x108e
#define PCIBASE_DEVICE_ID 0xabba
#define PCIBASE_CLASS_ID  0x0200

/* Selected register offsets used during probe/init (subset) */
#define CAS_REG_CAWR              0x0004
#define CAS_REG_INF_BURST         0x0008
#define CAS_REG_BIM_CFG           0x1008
#define CAS_REG_RX_CFG            0x4000
#define CAS_REG_HP_CFG            0x4140
#define CAS_REG_MAC_TX_CFG        0x6030
#define CAS_REG_MAC_RX_CFG        0x6034
#define CAS_REG_MAC_CTRL_CFG      0x6038
#define CAS_REG_MAC_XIF_CFG       0x603C
#define CAS_REG_MIF_CFG           0x6210
#define CAS_REG_PCS_CFG           0x9010
#define CAS_REG_SATURN_PCFG       0x106c
#define CAS_REG_PCS_MII_STATUS    0x9004
#define CAS_REG_PCS_STATE_MACHINE 0x9014
#define CAS_REG_MAC_RX_STATUS     0x6014
#define CAS_REG_MAC_TX_STATUS     0x6010
#define CAS_REG_MAC_CTRL_STATUS   0x6018
#define CAS_REG_PCI_ERR_STATUS    0x1000
#define CAS_REG_INTR_STATUS       0x000C
#define CAS_REG_INTR_MASK         0x0010
#define CAS_REG_ALIAS_CLEAR       0x0014
#define CAS_REG_INTR_STATUS_ALIAS 0x001C
#define CAS_REG_PCI_ERR_STATUS_MASK 0x1004
#define CAS_REG_SW_RESET          0x1010
#define CAS_REG_BIM_LOCAL_DEV_EN  0x1020
#define CAS_REG_MAC_RX_RESET      0x6004
#define CAS_REG_MAC_TX_RESET      0x6000
#define CAS_REG_MAC_FRAMESIZE_MAX 0x6054
#define CAS_REG_MAC_FRAMESIZE_MIN 0x6050
#define CAS_REG_MAC_ADDR0         0x6080
#define CAS_REG_MAC_HASH_TABLE0   0x6160

/* Interrupt bits used by driver (driver names prefixed INTR_ in source) */
#define CAS_INTR_TX_INTME           0x00000001
#define CAS_INTR_TX_ALL             0x00000002
#define CAS_INTR_TX_DONE            0x00000004
#define CAS_INTR_RX_DONE            0x00000010
#define CAS_INTR_RX_BUF_UNAVAIL     0x00000020
#define CAS_INTR_RX_TAG_ERROR       0x00000040
#define CAS_INTR_RX_COMP_FULL       0x00000080
#define CAS_INTR_RX_BUF_AE          0x00000100
#define CAS_INTR_RX_COMP_AF         0x00000200
#define CAS_INTR_RX_LEN_MISMATCH    0x00000400
#define CAS_INTR_PCS_STATUS         0x00002000
#define CAS_INTR_TX_MAC_STATUS      0x00004000
#define CAS_INTR_RX_MAC_STATUS      0x00008000
#define CAS_INTR_MAC_CTRL_STATUS    0x00010000
#define CAS_INTR_MIF_STATUS         0x00020000
#define CAS_INTR_PCI_ERROR_STATUS   0x00040000

/* Error mask as in driver (names mapped to CAS_INTR_ above) */
#define CAS_INTR_ERROR_MASK (\
    CAS_INTR_MIF_STATUS | CAS_INTR_PCI_ERROR_STATUS | \
    CAS_INTR_PCS_STATUS | CAS_INTR_RX_LEN_MISMATCH | \
    CAS_INTR_TX_MAC_STATUS | CAS_INTR_RX_MAC_STATUS | \
    CAS_INTR_MAC_CTRL_STATUS | CAS_INTR_RX_TAG_ERROR)

/* Misc control bits used early */
#define CAS_MAC_RX_CFG_EN              0x0001
#define CAS_MAC_TX_CFG_EN              0x0001
#define CAS_RX_CFG_DMA_EN              0x00000001
#define CAS_TX_CFG_DMA_EN              0x00000001

/* BAR index used by driver: regs is ioremap() of BAR0 */
#define CAS_BAR_REGS_INDEX 0

#define REG_EXPANSION_ROM_RUN_START       0x100000


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
        uint32_t cawr;
        uint32_t inf_burst;
        uint32_t bim_cfg;
        uint32_t rx_cfg;
        uint32_t hp_cfg;
        uint32_t mac_tx_cfg;
        uint32_t mac_rx_cfg;
        uint32_t mac_ctrl_cfg;
        uint32_t mac_xif_cfg;
        uint32_t mif_cfg;
        uint32_t pcs_cfg;
        uint32_t saturn_pcfg;
        uint32_t pcs_mii_status;
        uint32_t pcs_state_machine;
        uint32_t mac_rx_status;
        uint32_t mac_tx_status;
        uint32_t mac_ctrl_status;
        uint32_t pci_err_status;
        uint32_t pci_err_status_mask;
        uint32_t sw_reset;
        uint32_t bim_local_dev_en;
        uint32_t intr_status_alias;
        uint32_t alias_clear;
        uint32_t mac_rx_reset;
        uint32_t mac_tx_reset;
        uint32_t mac_framesize_max;
        uint32_t mac_framesize_min;
        uint32_t mac_addr0_low;
        uint32_t mac_addr0_high;
    } regs;

    /* Operational status flags */
    int hw_running;
    int opened;

    /* State used to handle reset sequences */
    int reset_in_progress;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* Other additional info */
    uint32_t cas_flags_shadow;
};


/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    uint32_t pending = s->intr_status & ~s->intr_mask;
    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns
 *
 * The full Cassini DMA/descriptor engine is complex and not required
 * for driver probe/bind to succeed. The driver sets up descriptor
 * rings and kicks DMA, but successful probe and basic open/close do
 * not depend on data actually being transferred. Therefore this helper
 * is left empty.
 */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* Cassini is little-endian, allow 1/2/4-byte accesses,
     * but registers we model are 32-bit, so ignore sub-word size
     * details and just return full 32-bit shadow.
     */
    switch (addr) {
    case CAS_REG_CAWR:
        val = s->regs.cawr;
        break;
    case CAS_REG_INF_BURST:
        val = s->regs.inf_burst;
        break;
    case CAS_REG_BIM_CFG:
        val = s->regs.bim_cfg;
        break;
    case CAS_REG_RX_CFG:
        val = s->regs.rx_cfg;
        break;
    case CAS_REG_HP_CFG:
        val = s->regs.hp_cfg;
        break;
    case CAS_REG_MAC_TX_CFG:
        val = s->regs.mac_tx_cfg;
        break;
    case CAS_REG_MAC_RX_CFG:
        val = s->regs.mac_rx_cfg;
        break;
    case CAS_REG_MAC_CTRL_CFG:
        val = s->regs.mac_ctrl_cfg;
        break;
    case CAS_REG_MAC_XIF_CFG:
        val = s->regs.mac_xif_cfg;
        break;
    case CAS_REG_MIF_CFG:
        val = s->regs.mif_cfg;
        break;
    case CAS_REG_PCS_CFG:
        val = s->regs.pcs_cfg;
        break;
    case CAS_REG_SATURN_PCFG:
        val = s->regs.saturn_pcfg;
        break;
    case CAS_REG_PCS_MII_STATUS:
        /* Driver polls this for link state; simplest is to
         * report link up and autoneg complete so link checks
         * succeed.
         */
        val = s->regs.pcs_mii_status;
        break;
    case CAS_REG_PCS_STATE_MACHINE:
        val = s->regs.pcs_state_machine;
        break;
    case CAS_REG_MAC_RX_STATUS:
        val = s->regs.mac_rx_status;
        break;
    case CAS_REG_MAC_TX_STATUS:
        val = s->regs.mac_tx_status;
        break;
    case CAS_REG_MAC_CTRL_STATUS:
        val = s->regs.mac_ctrl_status;
        break;
    case CAS_REG_PCI_ERR_STATUS:
        val = s->regs.pci_err_status;
        break;
    case CAS_REG_PCI_ERR_STATUS_MASK:
        val = s->regs.pci_err_status_mask;
        break;
    case CAS_REG_SW_RESET:
        val = s->regs.sw_reset;
        break;
    case CAS_REG_BIM_LOCAL_DEV_EN:
        val = s->regs.bim_local_dev_en;
        break;
    case CAS_REG_INTR_STATUS:
        /* Main interrupt status */
        val = s->intr_status;
        break;
    case CAS_REG_INTR_STATUS_ALIAS:
        /* Alias mirrors main status in our model */
        val = s->regs.intr_status_alias;
        break;
    case CAS_REG_INTR_MASK:
        val = s->intr_mask;
        break;
    case CAS_REG_ALIAS_CLEAR:
        val = s->regs.alias_clear;
        break;
    case CAS_REG_MAC_RX_RESET:
        val = s->regs.mac_rx_reset;
        break;
    case CAS_REG_MAC_TX_RESET:
        val = s->regs.mac_tx_reset;
        break;
    case CAS_REG_MAC_FRAMESIZE_MAX:
        val = s->regs.mac_framesize_max;
        break;
    case CAS_REG_MAC_FRAMESIZE_MIN:
        val = s->regs.mac_framesize_min;
        break;
    case CAS_REG_MAC_ADDR0:
        /* Low 16 bits of MAC address as used in driver */
        val = s->regs.mac_addr0_low;
        break;
    case CAS_REG_MAC_HASH_TABLE0:
        val = s->regs.mac_addr0_high;
        break;
    default:
        /* Unimplemented registers read as zero */
        val = 0;
        break;
    }

    /* Truncate to requested access size */
    if (size == 1) {
        return (uint8_t)val;
    } else if (size == 2) {
        return (uint16_t)val;
    }
    return (uint32_t)val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    switch (addr) {
    case CAS_REG_CAWR:
        s->regs.cawr = v32;
        break;
    case CAS_REG_INF_BURST:
        s->regs.inf_burst = v32;
        break;
    case CAS_REG_BIM_CFG:
        s->regs.bim_cfg = v32;
        break;
    case CAS_REG_RX_CFG:
        s->regs.rx_cfg = v32;
        break;
    case CAS_REG_HP_CFG:
        s->regs.hp_cfg = v32;
        break;
    case CAS_REG_MAC_TX_CFG:
        s->regs.mac_tx_cfg = v32;
        break;
    case CAS_REG_MAC_RX_CFG:
        s->regs.mac_rx_cfg = v32;
        break;
    case CAS_REG_MAC_CTRL_CFG:
        s->regs.mac_ctrl_cfg = v32;
        break;
    case CAS_REG_MAC_XIF_CFG:
        s->regs.mac_xif_cfg = v32;
        break;
    case CAS_REG_MIF_CFG:
        s->regs.mif_cfg = v32;
        break;
    case CAS_REG_PCS_CFG:
        s->regs.pcs_cfg = v32;
        break;
    case CAS_REG_SATURN_PCFG:
        s->regs.saturn_pcfg = v32;
        break;
    case CAS_REG_PCS_MII_STATUS:
        s->regs.pcs_mii_status = v32;
        break;
    case CAS_REG_PCS_STATE_MACHINE:
        s->regs.pcs_state_machine = v32;
        break;
    case CAS_REG_MAC_RX_STATUS:
        s->regs.mac_rx_status = v32;
        break;
    case CAS_REG_MAC_TX_STATUS:
        s->regs.mac_tx_status = v32;
        break;
    case CAS_REG_MAC_CTRL_STATUS:
        s->regs.mac_ctrl_status = v32;
        break;
    case CAS_REG_PCI_ERR_STATUS:
        /* W1C semantics for error bits: writing 1 clears */
        s->regs.pci_err_status &= ~v32;
        break;
    case CAS_REG_PCI_ERR_STATUS_MASK:
        s->regs.pci_err_status_mask = v32;
        break;
    case CAS_REG_SW_RESET:
        /* Store reset request; we also reflect it in shadow */
        s->regs.sw_reset = v32;
        break;
    case CAS_REG_BIM_LOCAL_DEV_EN:
        s->regs.bim_local_dev_en = v32;
        break;
    case CAS_REG_INTR_MASK:
        s->intr_mask = v32;
        pcibase_update_irq(s);
        break;
    case CAS_REG_INTR_STATUS:
        /* Acknowledge interrupts: W1C */
        s->intr_status &= ~v32;
        s->regs.intr_status_alias = s->intr_status;
        pcibase_update_irq(s);
        break;
    case CAS_REG_ALIAS_CLEAR:
        /* Driver writes specific bits to clear alias RX status */
        s->regs.alias_clear = v32;
        /* For simplicity, clear RX_DONE and BUF_UNAVAIL bits when requested */
        if (v32 & (CAS_INTR_RX_DONE | CAS_INTR_RX_BUF_UNAVAIL)) {
            s->intr_status &= ~(CAS_INTR_RX_DONE | CAS_INTR_RX_BUF_UNAVAIL);
            s->regs.intr_status_alias = s->intr_status;
            pcibase_update_irq(s);
        }
        break;
    case CAS_REG_INTR_STATUS_ALIAS:
        /* Treat writes similarly to main status for ack */
        s->regs.intr_status_alias &= ~v32;
        s->intr_status &= ~v32;
        pcibase_update_irq(s);
        break;
    case CAS_REG_MAC_RX_RESET:
        /* Driver writes 1 and polls for 0. Emulate quick completion. */
        s->regs.mac_rx_reset = 0;
        break;
    case CAS_REG_MAC_TX_RESET:
        s->regs.mac_tx_reset = 0;
        break;
    case CAS_REG_MAC_FRAMESIZE_MAX:
        s->regs.mac_framesize_max = v32;
        break;
    case CAS_REG_MAC_FRAMESIZE_MIN:
        s->regs.mac_framesize_min = v32;
        break;
    case CAS_REG_MAC_ADDR0:
        s->regs.mac_addr0_low = v32;
        break;
    case CAS_REG_MAC_HASH_TABLE0:
        s->regs.mac_addr0_high = v32;
        break;
    default:
        /* Writes to unimplemented registers are ignored */
        break;
    }

    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Cassini driver does not use PIO; leave unimplemented. */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Cassini driver does not use PIO; leave unimplemented. */
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

    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->hw_running = 0;
    s->opened = 0;
    s->reset_in_progress = 0;
    s->pm_state = 0;
    s->cas_flags_shadow = 0;

    /* Provide default link-up friendly values so driver link
     * checks complete successfully.
     */
    s->regs.pcs_mii_status = 0;
    /* PCS_MII_STATUS_LINK_STATUS | PCS_MII_STATUS_AUTONEG_COMP equivalents
     * cannot be defined without driver headers, so we simply keep this
     * non-zero to indicate 'good' status to the driver which treats
     * non-zero as valid.
     */
    s->regs.pcs_mii_status = 0x0001;
    s->regs.pcs_state_machine = 0x0000;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
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
    s->bar_info[0].index = CAS_BAR_REGS_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    /* Cassini register space is at least large enough for EXPANSION_ROM_RUN_START.
     * Use that as a conservative lower bound for BAR size. */
    s->bar_info[0].size  = REG_EXPANSION_ROM_RUN_START;
    s->bar_info[0].name  = "cassini-regs";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Cassini driver does not use MSI/MSI-X explicitly. Disable by default. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadows to safe defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->hw_running = 0;
    s->opened = 0;
    s->reset_in_progress = 0;
    s->pm_state = 0;
    s->cas_flags_shadow = 0;

    /* Initialize fields used in link-detection paths */
    s->regs.pcs_mii_status = 0x0001;    /* generic non-zero good state */
    s->regs.pcs_state_machine = 0x0000; /* no error */
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
    .name = "cassini_pci",
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
