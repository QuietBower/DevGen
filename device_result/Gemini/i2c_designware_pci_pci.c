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

/* Additional include files retrieved from driver context */
#include "hw/pci/pci_device.h"

#define TYPE_PCIBASE_DEVICE "i2c_designware_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_I2C_0817 0x0817

#define DW_IC_CON                0x00
#define DW_IC_TAR                0x04
#define DW_IC_SAR                0x08
#define DW_IC_DATA_CMD           0x10
#define DW_IC_SS_SCL_HCNT        0x14
#define DW_IC_SS_SCL_LCNT        0x18
#define DW_IC_FS_SCL_HCNT        0x1c
#define DW_IC_FS_SCL_LCNT        0x20
#define DW_IC_HS_SCL_HCNT        0x24
#define DW_IC_HS_SCL_LCNT        0x28
#define DW_IC_INTR_STAT          0x2c
#define DW_IC_INTR_MASK          0x30
#define DW_IC_RAW_INTR_STAT      0x34
#define DW_IC_RX_TL              0x38
#define DW_IC_TX_TL              0x3c
#define DW_IC_CLR_INTR           0x40
#define DW_IC_ENABLE             0x6c
#define DW_IC_STATUS             0x70
#define DW_IC_SDA_HOLD           0x7c
#define DW_IC_ENABLE_STATUS      0x9c
#define DW_IC_COMP_PARAM_1       0xf4
#define DW_IC_COMP_VERSION       0xf8
#define DW_IC_COMP_TYPE          0xfc
#define AMD_UCSI_INTR_EN         0xd
#define AMD_UCSI_INTR_REG        0x474

#define DW_IC_CON_MASTER			BIT(0)
#define DW_IC_CON_SLAVE_DISABLE			BIT(6)
#define DW_IC_CON_RESTART_EN			BIT(5)
#define DW_IC_MASTER				0
#define DW_IC_SLAVE				1
#define DW_IC_CON_SPEED_STD			(1 << 1)
#define DW_IC_CON_SPEED_HIGH			(3 << 1)
#define DW_IC_CON_SPEED_FAST			(2 << 1)
#define DW_IC_CON_RX_FIFO_FULL_HLD_CTRL		BIT(9)
#define DW_IC_CON_STOP_DET_IFADDRESSED		BIT(7)
#define DW_IC_DEFAULT_BUS_CAPACITANCE_pF	100
#define ACCESS_NO_IRQ_SUSPEND			BIT(1)
#define ACCESS_POLLING				BIT(3)
#define MODEL_MASK				(0xf << 8)
#define MODEL_AMD_NAVI_GPU			BIT(10)

#define DW_IC_COMP_TYPE_VALUE			0x44570140
#define DW_IC_SDA_HOLD_MIN_VERS			0x3131312A
#define DW_IC_SDA_HOLD_RX_MASK			GENMASK(23, 16)
#define DW_IC_SDA_HOLD_RX_SHIFT			16
#define MODEL_WANGXUN_SP			BIT(11)
#define TXGBE_TX_FIFO_DEPTH			4
#define TXGBE_RX_FIFO_DEPTH			1
#define DW_IC_FIFO_TX_FIELD			GENMASK(23, 16)
#define DW_IC_FIFO_RX_FIELD			GENMASK(15, 8)
#define DW_IC_FIFO_MIN_DEPTH			2
#define DW_IC_CON_BUS_CLEAR_CTRL		BIT(11)
#define DW_IC_SMBUS_INTR_MASK			0xcc
#define DW_IC_INTR_MST_ON_HOLD			BIT(13)
#define DW_IC_STATUS_MASTER_HOLD_TX_FIFO_EMPTY	BIT(7)
#define DW_IC_ENABLE_ENABLE			BIT(0)
#define DW_IC_ENABLE_ABORT			BIT(1)
#define DW_IC_ABORT_TIMEOUT_US			10

struct dw_scl_sda_cfg {
    uint16_t ss_hcnt;
    uint16_t fs_hcnt;
    uint16_t ss_lcnt;
    uint16_t fs_lcnt;
    uint32_t sda_hold_time;
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
    uint32_t intr_stat;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t ic_con;
    uint32_t ic_tar;
    uint32_t ic_sar;
    uint32_t ic_data_cmd;
    uint32_t ic_ss_scl_hcnt;
    uint32_t ic_ss_scl_lcnt;
    uint32_t ic_fs_scl_hcnt;
    uint32_t ic_fs_scl_lcnt;
    uint32_t ic_hs_scl_hcnt;
    uint32_t ic_hs_scl_lcnt;
    uint32_t ic_raw_intr_stat;
    uint32_t ic_rx_tl;
    uint32_t ic_tx_tl;
    uint32_t ic_enable;
    uint32_t ic_status;
    uint32_t ic_sda_hold;
    uint32_t ic_enable_status;
    uint32_t ic_comp_param_1;
    uint32_t ic_comp_version;
    uint32_t ic_comp_type;
    uint32_t amd_ucsi_intr_en;
    uint32_t amd_ucsi_intr_reg;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intr_stat & s->intr_mask) != 0;

    if (s->has_msi) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DW_IC_CON:
        val = s->ic_con;
        break;
    case DW_IC_TAR:
        val = s->ic_tar;
        break;
    case DW_IC_SAR:
        val = s->ic_sar;
        break;
    case DW_IC_DATA_CMD:
        val = s->ic_data_cmd;
        break;
    case DW_IC_SS_SCL_HCNT:
        val = s->ic_ss_scl_hcnt;
        break;
    case DW_IC_SS_SCL_LCNT:
        val = s->ic_ss_scl_lcnt;
        break;
    case DW_IC_FS_SCL_HCNT:
        val = s->ic_fs_scl_hcnt;
        break;
    case DW_IC_FS_SCL_LCNT:
        val = s->ic_fs_scl_lcnt;
        break;
    case DW_IC_HS_SCL_HCNT:
        val = s->ic_hs_scl_hcnt;
        break;
    case DW_IC_HS_SCL_LCNT:
        val = s->ic_hs_scl_lcnt;
        break;
    case DW_IC_INTR_STAT:
        val = s->intr_stat;
        break;
    case DW_IC_INTR_MASK:
        val = s->intr_mask;
        break;
    case DW_IC_RAW_INTR_STAT:
        val = s->ic_raw_intr_stat;
        break;
    case DW_IC_RX_TL:
        val = s->ic_rx_tl;
        break;
    case DW_IC_TX_TL:
        val = s->ic_tx_tl;
        break;
    case DW_IC_CLR_INTR:
        val = 0;
        s->intr_stat = 0;
        pcibase_update_irq(s);
        break;
    case DW_IC_ENABLE:
        val = s->ic_enable;
        break;
    case DW_IC_STATUS:
        val = s->ic_status;
        break;
    case DW_IC_SDA_HOLD:
        val = s->ic_sda_hold;
        break;
    case DW_IC_ENABLE_STATUS:
        val = s->ic_enable_status;
        break;
    case DW_IC_COMP_PARAM_1:
        val = s->ic_comp_param_1;
        break;
    case DW_IC_COMP_VERSION:
        val = s->ic_comp_version;
        break;
    case DW_IC_COMP_TYPE:
        val = s->ic_comp_type;
        break;
    case AMD_UCSI_INTR_EN:
        val = s->amd_ucsi_intr_en;
        break;
    case AMD_UCSI_INTR_REG:
        val = s->amd_ucsi_intr_reg;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case DW_IC_CON:
        s->ic_con = val;
        break;
    case DW_IC_TAR:
        s->ic_tar = val;
        break;
    case DW_IC_SAR:
        s->ic_sar = val;
        break;
    case DW_IC_DATA_CMD:
        s->ic_data_cmd = val;
        break;
    case DW_IC_SS_SCL_HCNT:
        s->ic_ss_scl_hcnt = val;
        break;
    case DW_IC_SS_SCL_LCNT:
        s->ic_ss_scl_lcnt = val;
        break;
    case DW_IC_FS_SCL_HCNT:
        s->ic_fs_scl_hcnt = val;
        break;
    case DW_IC_FS_SCL_LCNT:
        s->ic_fs_scl_lcnt = val;
        break;
    case DW_IC_HS_SCL_HCNT:
        s->ic_hs_scl_hcnt = val;
        break;
    case DW_IC_HS_SCL_LCNT:
        s->ic_hs_scl_lcnt = val;
        break;
    case DW_IC_INTR_MASK:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case DW_IC_RX_TL:
        s->ic_rx_tl = val;
        break;
    case DW_IC_TX_TL:
        s->ic_tx_tl = val;
        break;
    case DW_IC_CLR_INTR:
        s->intr_stat = 0;
        pcibase_update_irq(s);
        break;
    case DW_IC_ENABLE:
        s->ic_enable = val;
        break;
    case DW_IC_SDA_HOLD:
        s->ic_sda_hold = val;
        break;
    case AMD_UCSI_INTR_EN:
        s->amd_ucsi_intr_en = val;
        break;
    case AMD_UCSI_INTR_REG:
        s->amd_ucsi_intr_reg = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
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

    s->ic_con = 0;
    s->ic_tar = 0;
    s->ic_sar = 0;
    s->ic_data_cmd = 0;
    s->ic_ss_scl_hcnt = 0;
    s->ic_ss_scl_lcnt = 0;
    s->ic_fs_scl_hcnt = 0;
    s->ic_fs_scl_lcnt = 0;
    s->ic_hs_scl_hcnt = 0;
    s->ic_hs_scl_lcnt = 0;
    s->intr_stat = 0;
    s->intr_mask = 0;
    s->ic_raw_intr_stat = 0;
    s->ic_rx_tl = 0;
    s->ic_tx_tl = 0;
    s->ic_enable = 0;
    s->ic_status = 0;
    s->ic_sda_hold = 0;
    s->ic_enable_status = 0;
    s->ic_comp_param_1 = 0;
    s->ic_comp_version = 0x3131352a;
    s->ic_comp_type = 0x44570140;
    s->amd_ucsi_intr_en = 0;
    s->amd_ucsi_intr_reg = 0;

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0817 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c80 );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "dw-i2c-bar0";
  
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
