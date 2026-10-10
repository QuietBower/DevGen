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


#define TYPE_PCIBASE_DEVICE "rtl8192ee_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define RTL8192EE_VENDOR_ID 0x10ec
#define RTL8192EE_DEVICE_ID 0x818B

#define REG_SYS_ISO_CTRL 0x0000
#define REG_SYS_FUNC_EN 0x0002
#define REG_SYS_CLKR 0x0008
#define REG_SYS_SWR_CTRL1 0x0010
#define REG_SYS_SWR_CTRL2 0x0014
#define REG_AFE_CTRL2 0x0028
#define REG_EFUSE_CTRL 0x0030
#define REG_EFUSE_TEST 0x0034
#define REG_AFE_CTRL4 0x0078
#define REG_EFUSE_ACCESS 0x00CF
#define REG_HIMR 0x0120
#define REG_HIMRE 0x0128
#define REG_VOQ_TXBD_IDX 0x03A0
#define REG_VIQ_TXBD_IDX 0x03A4
#define REG_BEQ_TXBD_IDX 0x03A8
#define REG_BKQ_TXBD_IDX 0x03AC
#define REG_MGQ_TXBD_IDX 0x03B0
#define REG_RXQ_TXBD_IDX 0x03B4
#define REG_HI0Q_TXBD_IDX 0x03B8
#define REG_RETRY_LIMIT 0x042A
#define REG_CAMCMD 0x0670
#define REG_CAMWRITE 0x0674
#define REG_CAMREAD 0x0678
#define REG_CAMDBG 0x067C
#define REG_SECCFG 0x0680
#define DM_REG_L1SBD_PD_CH_11N 0XC6C

#define REG_CR 0x00
#define REG_PCIE_CTRL_REG 0x0300
#define REG_NAV_UPPER 0x0652
#define ISR REG_HISR
#define REG_HISRE 0x012C
#define REG_9346CR 0x000A

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
    uint32_t irq_mask[4];
    uint32_t sys_irq_mask;
    uint32_t inta;
    uint32_t intb;
    uint32_t intc;
    uint32_t intd;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t sys_iso_ctrl;
    uint32_t sys_func_en;
    uint32_t sys_clkr;
    uint32_t sys_swr_ctrl1;
    uint32_t sys_swr_ctrl2;
    uint32_t afe_ctrl2;
    uint32_t efuse_ctrl;
    uint32_t efuse_test;
    uint32_t afe_ctrl4;
    uint32_t efuse_access;
    uint32_t himr;
    uint32_t himre;
    uint32_t voq_txbd_idx;
    uint32_t viq_txbd_idx;
    uint32_t beq_txbd_idx;
    uint32_t bkq_txbd_idx;
    uint32_t mgq_txbd_idx;
    uint32_t rxq_txbd_idx;
    uint32_t hi0q_txbd_idx;
    uint32_t retry_limit;
    uint32_t camcmd;
    uint32_t camwrite;
    uint32_t camread;
    uint32_t camdbg;
    uint32_t seccfg;
    uint32_t dm_reg_l1sbd_pd_ch_11n;

    uint32_t pcie_ctrl_reg;
    uint32_t nav_upper;
    uint32_t hisre;
    uint32_t reg_9346cr;

    uint8_t reg_0x64;
    uint8_t reg_0x65;
    uint32_t reg_0x4fc;
    uint8_t reg_0x577;

    /* DMA Context */
    dma_addr_t tx_ring_dma[9];
    dma_addr_t rx_ring_dma[2];

    bool msi_support;
    bool using_msi;
    
    uint8_t const_pci_aspm;
    uint8_t const_hwsw_rfoff_d3;
    uint8_t const_support_pciaspm;
    uint8_t const_hostpci_aspm_setting;
    uint8_t const_devicepci_aspm_setting;
    bool support_aspm;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = false;

    if (s->inta & s->himr) {
        level = true;
    }

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
    case REG_SYS_ISO_CTRL:
        val = s->sys_iso_ctrl;
        break;
    case REG_SYS_FUNC_EN:
        val = s->sys_func_en;
        break;
    case REG_SYS_CLKR:
        val = s->sys_clkr;
        break;
    case REG_SYS_CLKR + 1:
        val = (s->sys_clkr >> 8) & 0xFF;
        break;
    case REG_9346CR:
        val = s->reg_9346cr;
        break;
    case REG_SYS_SWR_CTRL1:
        val = s->sys_swr_ctrl1;
        break;
    case REG_SYS_SWR_CTRL2:
        val = s->sys_swr_ctrl2;
        break;
    case REG_AFE_CTRL2:
        val = s->afe_ctrl2;
        break;
    case REG_EFUSE_CTRL:
        val = s->efuse_ctrl;
        break;
    case REG_EFUSE_TEST:
        val = s->efuse_test;
        break;
    case REG_AFE_CTRL4:
        val = s->afe_ctrl4;
        break;
    case REG_AFE_CTRL4 + 1:
        val = (s->afe_ctrl4 >> 8) & 0xFF;
        break;
    case REG_EFUSE_ACCESS:
        val = s->efuse_access;
        break;
    case REG_HIMR:
        val = s->himr;
        break;
    case REG_HIMRE:
        val = s->himre;
        break;
    case REG_HISRE:
        val = s->hisre;
        break;
    case REG_PCIE_CTRL_REG:
        val = s->pcie_ctrl_reg;
        break;
    case REG_VOQ_TXBD_IDX:
        val = s->voq_txbd_idx;
        break;
    case REG_VIQ_TXBD_IDX:
        val = s->viq_txbd_idx;
        break;
    case REG_BEQ_TXBD_IDX:
        val = s->beq_txbd_idx;
        break;
    case REG_BKQ_TXBD_IDX:
        val = s->bkq_txbd_idx;
        break;
    case REG_MGQ_TXBD_IDX:
        val = s->mgq_txbd_idx;
        break;
    case REG_RXQ_TXBD_IDX:
        val = s->rxq_txbd_idx;
        break;
    case REG_HI0Q_TXBD_IDX:
        val = s->hi0q_txbd_idx;
        break;
    case REG_RETRY_LIMIT:
        val = s->retry_limit;
        break;
    case REG_NAV_UPPER:
        val = s->nav_upper;
        break;
    case REG_CAMCMD:
        val = s->camcmd;
        break;
    case REG_CAMWRITE:
        val = s->camwrite;
        break;
    case REG_CAMREAD:
        val = s->camread;
        break;
    case REG_CAMDBG:
        val = s->camdbg;
        break;
    case REG_SECCFG:
        val = s->seccfg;
        break;
    case DM_REG_L1SBD_PD_CH_11N:
        val = s->dm_reg_l1sbd_pd_ch_11n;
        break;
    case 0x64:
        val = s->reg_0x64;
        break;
    case 0x65:
        val = s->reg_0x65;
        break;
    case 0x4fc:
        val = s->reg_0x4fc;
        break;
    case 0x577:
        val = s->reg_0x577;
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
    case REG_SYS_ISO_CTRL:
        s->sys_iso_ctrl = val;
        break;
    case REG_SYS_FUNC_EN:
        s->sys_func_en = val;
        break;
    case REG_SYS_CLKR:
        s->sys_clkr = val;
        break;
    case REG_SYS_CLKR + 1:
        s->sys_clkr = (s->sys_clkr & 0xFFFF00FF) | ((val & 0xFF) << 8);
        break;
    case REG_9346CR:
        s->reg_9346cr = val;
        break;
    case REG_SYS_SWR_CTRL1:
        s->sys_swr_ctrl1 = val;
        break;
    case REG_SYS_SWR_CTRL2:
        if (size == 1) {
            s->sys_swr_ctrl2 = (s->sys_swr_ctrl2 & 0xFFFFFF00) | (val & 0xFF);
        } else {
            s->sys_swr_ctrl2 = val;
        }
        break;
    case REG_AFE_CTRL2:
        if (size == 1) {
            s->afe_ctrl2 = (s->afe_ctrl2 & 0xFFFFFF00) | (val & 0xFF);
        } else {
            s->afe_ctrl2 = val;
        }
        break;
    case REG_EFUSE_CTRL:
        s->efuse_ctrl = val;
        break;
    case REG_EFUSE_TEST:
        s->efuse_test = val;
        break;
    case REG_AFE_CTRL4:
        if (size == 1) {
            s->afe_ctrl4 = (s->afe_ctrl4 & 0xFFFFFF00) | (val & 0xFF);
        } else {
            s->afe_ctrl4 = val;
        }
        break;
    case REG_AFE_CTRL4 + 1:
        s->afe_ctrl4 = (s->afe_ctrl4 & 0xFFFF00FF) | ((val & 0xFF) << 8);
        break;
    case REG_EFUSE_ACCESS:
        s->efuse_access = val;
        break;
    case REG_HIMR:
        s->himr = val;
        pcibase_update_irq(s);
        break;
    case REG_HIMRE:
        s->himre = val;
        pcibase_update_irq(s);
        break;
    case REG_HISRE:
        s->hisre = val;
        break;
    case REG_PCIE_CTRL_REG:
        s->pcie_ctrl_reg = val;
        break;
    case REG_VOQ_TXBD_IDX:
        s->voq_txbd_idx = val;
        break;
    case REG_VIQ_TXBD_IDX:
        s->viq_txbd_idx = val;
        break;
    case REG_BEQ_TXBD_IDX:
        s->beq_txbd_idx = val;
        break;
    case REG_BKQ_TXBD_IDX:
        s->bkq_txbd_idx = val;
        break;
    case REG_MGQ_TXBD_IDX:
        s->mgq_txbd_idx = val;
        break;
    case REG_RXQ_TXBD_IDX:
        s->rxq_txbd_idx = val;
        break;
    case REG_HI0Q_TXBD_IDX:
        s->hi0q_txbd_idx = val;
        break;
    case REG_RETRY_LIMIT:
        s->retry_limit = val;
        break;
    case REG_NAV_UPPER:
        s->nav_upper = val;
        break;
    case REG_CAMCMD:
        s->camcmd = val;
        break;
    case REG_CAMWRITE:
        s->camwrite = val;
        break;
    case REG_CAMREAD:
        s->camread = val;
        break;
    case REG_CAMDBG:
        s->camdbg = val;
        break;
    case REG_SECCFG:
        s->seccfg = val;
        break;
    case DM_REG_L1SBD_PD_CH_11N:
        s->dm_reg_l1sbd_pd_ch_11n = val;
        break;
    case 0x64:
        s->reg_0x64 = val;
        break;
    case 0x65:
        s->reg_0x65 = val;
        break;
    case 0x4fc:
        s->reg_0x4fc = val;
        break;
    case 0x577:
        s->reg_0x577 = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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

    s->sys_iso_ctrl = 0;
    s->sys_func_en = 0;
    s->sys_clkr = 0;
    s->sys_swr_ctrl1 = 0;
    s->sys_swr_ctrl2 = 0;
    s->afe_ctrl2 = 0;
    s->efuse_ctrl = 0;
    s->efuse_test = 0;
    s->afe_ctrl4 = 0;
    s->efuse_access = 0;
    s->himr = 0;
    s->himre = 0;
    s->voq_txbd_idx = 0;
    s->viq_txbd_idx = 0;
    s->beq_txbd_idx = 0;
    s->bkq_txbd_idx = 0;
    s->mgq_txbd_idx = 0;
    s->rxq_txbd_idx = 0;
    s->hi0q_txbd_idx = 0;
    s->retry_limit = 0;
    s->camcmd = 0;
    s->camwrite = 0;
    s->camread = 0;
    s->camdbg = 0;
    s->seccfg = 0;
    s->dm_reg_l1sbd_pd_ch_11n = 0;
    s->inta = 0;

    s->pcie_ctrl_reg = 0;
    s->nav_upper = 0;
    s->hisre = 0;
    s->reg_9346cr = 0;

    s->reg_0x64 = 0;
    s->reg_0x65 = 0;
    s->reg_0x4fc = 0;
    s->reg_0x577 = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_REALTEK );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x818B );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0x40, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "rtl8192ee-pio";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 16384;
    s->bar_info[1].name = "rtl8192ee-mmio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = false;
    } else {
        s->has_msi = true;
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
    .name = "rtl8192ee_pci",
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
