/*
 * QEMU PCI device model for i2c-designware-pci (behavioral skeleton)
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

#define TYPE_PCIBASE_DEVICE "i2c_designware_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DW_IC_CON                    0x00
#define DW_IC_SAR                    0x08
#define DW_IC_INTR_MASK              0x30
#define DW_IC_RX_TL                  0x38
#define DW_IC_TX_TL                  0x3c
#define DW_IC_CLR_INTR               0x40
#define DW_IC_SS_SCL_HCNT            0x14
#define DW_IC_SS_SCL_LCNT            0x18
#define DW_IC_FS_SCL_HCNT            0x1c
#define DW_IC_FS_SCL_LCNT            0x20
#define DW_IC_HS_SCL_HCNT            0x24
#define DW_IC_HS_SCL_LCNT            0x28
#define DW_IC_SDA_HOLD               0x7c
#define DW_IC_COMP_PARAM_1           0xf4
#define DW_IC_COMP_VERSION           0xf8
#define DW_IC_COMP_TYPE              0xfc

#define DW_IC_COMP_TYPE_VALUE        0x44570140

#define DW_IC_CON_MASTER             (1u << 0)
#define DW_IC_CON_SPEED_STD          (1u << 1)
#define DW_IC_CON_SPEED_FAST         (2u << 1)
#define DW_IC_CON_SPEED_HIGH         (3u << 1)
#define DW_IC_CON_RESTART_EN         (1u << 5)
#define DW_IC_CON_SLAVE_DISABLE      (1u << 6)
#define DW_IC_CON_STOP_DET_IFADDRESSED (1u << 7)
#define DW_IC_CON_RX_FIFO_FULL_HLD_CTRL (1u << 9)
#define DW_IC_CON_BUS_CLEAR_CTRL     (1u << 11)

#define DW_IC_INTR_RX_UNDER          (1u << 0)
#define DW_IC_INTR_RX_FULL           (1u << 2)
#define DW_IC_INTR_RD_REQ            (1u << 5)
#define DW_IC_INTR_TX_ABRT           (1u << 6)
#define DW_IC_INTR_STOP_DET          (1u << 9)

#define DW_IC_INTR_DEFAULT_MASK (DW_IC_INTR_RX_FULL | \
                                DW_IC_INTR_TX_ABRT | \
                                DW_IC_INTR_STOP_DET)

#define DW_IC_INTR_SLAVE_MASK   (DW_IC_INTR_DEFAULT_MASK | \
                                DW_IC_INTR_RX_UNDER | \
                                DW_IC_INTR_RD_REQ)

#define DW_IC_COMP_PARAM_1_SPEED_MODE_MASK  (0xcu)
#define DW_IC_COMP_PARAM_1_SPEED_MODE_HIGH  ((1u << 2) | (1u << 3))

#define DW_IC_CON_SPEED_MASK              (0x6u)
#define DW_IC_SDA_HOLD_RX_MASK            (0xffu << 16)
#define DW_IC_SDA_HOLD_RX_SHIFT           16

#define DW_IC_SDA_HOLD_MIN_VERS           0x3131312a

#define AMD_UCSI_INTR_REG                 0x474

#define ACCESS_POLLING                    (1u << 3)
#define ACCESS_NO_IRQ_SUSPEND             (1u << 1)
#define MODEL_AMD_NAVI_GPU                (1u << 10)
#define MODEL_MSCC_OCELOT                 (1u << 8)
#define MODEL_WANGXUN_SP                  (1u << 11)
#define MODEL_MASK                        (0xfu << 8)

#define DW_IC_MASTER                      0
#define DW_IC_SLAVE                       1

#define TXGBE_RX_FIFO_DEPTH               1
#define TXGBE_TX_FIFO_DEPTH               4

#define DW_IC_DEFAULT_FUNCTIONALITY  (0x00000000u)

#define PCIBASE_VENDOR_ID 0x8086
#define PCIBASE_DEVICE_ID 0x0817
#define PCIBASE_CLASS_ID  0x0c05

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t con;
        uint32_t sar;
        uint32_t intr_mask;
        uint32_t rx_tl;
        uint32_t tx_tl;
        uint32_t clr_intr;
        uint32_t ss_scl_hcnt;
        uint32_t ss_scl_lcnt;
        uint32_t fs_scl_hcnt;
        uint32_t fs_scl_lcnt;
        uint32_t hs_scl_hcnt;
        uint32_t hs_scl_lcnt;
        uint32_t sda_hold;
        uint32_t comp_param_1;
        uint32_t comp_version;
        uint32_t comp_type;
    } regs;

};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /*
     * The provided driver source does not show any direct interaction with
     * interrupt status/enable bits, only that an IRQ vector is allocated.
     * Without explicit register semantics, we do not implement any
     * interrupt raising/clearing logic here to avoid inventing behavior.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The given driver fragment does not show any DMA engine programming
     * (no descriptor rings or DMA address registers). Therefore we do not
     * implement bus mastering here.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* Support 32-bit accesses for the visible registers; other sizes return 0 */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case DW_IC_CON:
        val = s->regs.con;
        break;
    case DW_IC_SAR:
        val = s->regs.sar;
        break;
    case DW_IC_INTR_MASK:
        val = s->regs.intr_mask;
        break;
    case DW_IC_RX_TL:
        val = s->regs.rx_tl;
        break;
    case DW_IC_TX_TL:
        val = s->regs.tx_tl;
        break;
    case DW_IC_CLR_INTR:
        /* Reading clear-all-interrupts register returns 0 and clears shadow */
        val = s->regs.clr_intr;
        s->regs.clr_intr = 0;
        pcibase_update_irq(s);
        break;
    case DW_IC_SS_SCL_HCNT:
        val = s->regs.ss_scl_hcnt;
        break;
    case DW_IC_SS_SCL_LCNT:
        val = s->regs.ss_scl_lcnt;
        break;
    case DW_IC_FS_SCL_HCNT:
        val = s->regs.fs_scl_hcnt;
        break;
    case DW_IC_FS_SCL_LCNT:
        val = s->regs.fs_scl_lcnt;
        break;
    case DW_IC_HS_SCL_HCNT:
        val = s->regs.hs_scl_hcnt;
        break;
    case DW_IC_HS_SCL_LCNT:
        val = s->regs.hs_scl_lcnt;
        break;
    case DW_IC_SDA_HOLD:
        val = s->regs.sda_hold;
        break;
    case DW_IC_COMP_PARAM_1:
        val = s->regs.comp_param_1;
        break;
    case DW_IC_COMP_VERSION:
        val = s->regs.comp_version;
        break;
    case DW_IC_COMP_TYPE:
        val = s->regs.comp_type;
        break;
    default:
        /* Unknown/unused offsets return 0 */
        val = 0;
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
    case DW_IC_CON:
        s->regs.con = (uint32_t)val;
        break;
    case DW_IC_SAR:
        s->regs.sar = (uint32_t)val;
        break;
    case DW_IC_INTR_MASK:
        s->regs.intr_mask = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case DW_IC_RX_TL:
        s->regs.rx_tl = (uint32_t)val;
        break;
    case DW_IC_TX_TL:
        s->regs.tx_tl = (uint32_t)val;
        break;
    case DW_IC_CLR_INTR:
        /* Write-one-to-clear semantics for shadowed interrupts */
        s->regs.clr_intr &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case DW_IC_SS_SCL_HCNT:
        s->regs.ss_scl_hcnt = (uint32_t)val;
        break;
    case DW_IC_SS_SCL_LCNT:
        s->regs.ss_scl_lcnt = (uint32_t)val;
        break;
    case DW_IC_FS_SCL_HCNT:
        s->regs.fs_scl_hcnt = (uint32_t)val;
        break;
    case DW_IC_FS_SCL_LCNT:
        s->regs.fs_scl_lcnt = (uint32_t)val;
        break;
    case DW_IC_HS_SCL_HCNT:
        s->regs.hs_scl_hcnt = (uint32_t)val;
        break;
    case DW_IC_HS_SCL_LCNT:
        s->regs.hs_scl_lcnt = (uint32_t)val;
        break;
    case DW_IC_SDA_HOLD:
        s->regs.sda_hold = (uint32_t)val;
        break;
    default:
        /* Writes to read-only / unknown registers are ignored */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    /* No PIO registers are defined/used by the driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
    /* No PIO registers are defined/used by the driver */
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

    /* Initialize shadow registers to sane defaults expected by core code */
    s->regs.con = 0;
    s->regs.sar = 0;
    s->regs.intr_mask = 0;
    s->regs.rx_tl = 0;
    s->regs.tx_tl = 0;
    s->regs.clr_intr = 0;

    /* Timing defaults: leave 0 so that higher-level config can program them */
    s->regs.ss_scl_hcnt = 0;
    s->regs.ss_scl_lcnt = 0;
    s->regs.fs_scl_hcnt = 0;
    s->regs.fs_scl_lcnt = 0;
    s->regs.hs_scl_hcnt = 0;
    s->regs.hs_scl_lcnt = 0;

    s->regs.sda_hold = 0;

    /* Provide identification values for capability detection */
    s->regs.comp_param_1 = 0;
    s->regs.comp_version = 0;
    s->regs.comp_type = DW_IC_COMP_TYPE_VALUE;
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "i2c-designware-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Ensure registers are in reset state */
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i2c_designware_pci_pci",
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
