/*
 * QEMU PCI device model for RTL8723BE wireless adapter
 * Based on Linux driver source: drivers/net/wireless/realtek/rtlwifi/rtl8723be/sw.c
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

#define TYPE_PCIBASE_DEVICE "rtl8723be_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10ec           /* PCI_VENDOR_ID_REALTEK */
#define DEVICE_ID 0xb723           /* First entry in rtl8723be_pci_ids */
#define CLASS_ID  0x0280           /* PCI_CLASS_NETWORK_OTHER */

/* Register offsets from driver macros */
#define REG_SYS_ISO_CTRL      0x0000
#define REG_SYS_FUNC_EN       0x0002
#define REG_SYS_CLKR          0x0008
#define REG_HIMR              0x0120
#define REG_HIMRE             0x0128
#define REG_HSISR             0x005c
#define REG_EFUSE_ACCESS      0x00CF
#define REG_EFUSE_TEST        0x0034
#define REG_EFUSE_CTRL        0x0030
#define REG_CAMCMD            0x0670
#define REG_CAMWRITE          0x0674
#define REG_CAMREAD           0x0678
#define REG_CAMDBG            0x067C
#define REG_SECCFG            0x0680
#define REG_SYS_CFG1          0x00FC
#define REG_RXDMA_CONTROL     0x0286
#define REG_NAV_UPPER         0x0652
#define REG_TRXPTCL_CTL       0x0668

/* Interrupt mask register bits */
#define IMR_BCNDMAINT6        BIT(31)
#define IMR_BCNDMAINT5        BIT(30)
#define IMR_BCNDMAINT4        BIT(29)
#define IMR_BCNDMAINT3        BIT(28)
#define IMR_BCNDMAINT2        BIT(27)
#define IMR_BCNDMAINT1        BIT(26)
#define IMR_BCNDOK7           BIT(24)
#define IMR_BCNDOK6           BIT(23)
#define IMR_BCNDOK5           BIT(22)
#define IMR_BCNDOK4           BIT(21)
#define IMR_BCNDOK3           BIT(20)
#define IMR_BCNDOK2           BIT(19)
#define IMR_BCNDOK1           BIT(18)
#define IMR_TXFOVW            BIT(15)
#define IMR_PSTIMEOUT         BIT(14)
#define IMR_RXFOVW            BIT(12)
#define IMR_RDU               BIT(11)
#define IMR_ATIMEND           BIT(10)
#define IMR_MGNTDOK           BIT(6)
#define IMR_TBDER             BIT(5)
#define IMR_HIGHDOK           BIT(8)
#define IMR_TBDOK             BIT(7)
#define IMR_BKDOK             BIT(4)
#define IMR_BEDOK             BIT(3)
#define IMR_VIDOK             BIT(2)
#define IMR_VODOK             BIT(1)
#define IMR_ROK               BIT(0)

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
    struct {
        uint32_t intr_status;
        uint32_t intr_mask;
    } intr;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t reg_sys_iso_ctrl;
        uint32_t reg_sys_func_en;
        uint32_t reg_sys_clkr;
        uint32_t reg_himr;
        uint32_t reg_himre;
        uint32_t reg_hsisr;
        uint32_t reg_efuse_access;
        uint32_t reg_efuse_test;
        uint32_t reg_efuse_ctrl;
        uint32_t reg_camcmd;
        uint32_t reg_camwrite;
        uint32_t reg_camread;
        uint32_t reg_camdbg;
        uint32_t reg_seccfg;
        uint32_t reg_sys_cfg1;
        uint32_t reg_rxdma_control;
        uint32_t reg_nav_upper;
        uint32_t reg_trxptcl_ctl;
        /* Additional registers will be added as needed */
    } regs;

    /* DMA Context */
    struct {
        dma_addr_t tx_ring_addr;
        dma_addr_t rx_ring_addr;
        /* DMA state placeholder */
    } dma;

    /* Operational status flags */
    uint32_t status;

    /* Probe/Reset state */
    uint32_t reset_state;

    /* Power management state (D0-D3) */
    uint32_t pm_state;

    /* Other additions can be placed here */
};

/* Other static definitions */

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case REG_SYS_ISO_CTRL:
        val = s->regs.reg_sys_iso_ctrl;
        break;
    case REG_SYS_FUNC_EN:
        val = s->regs.reg_sys_func_en;
        break;
    case REG_SYS_CLKR:
        val = s->regs.reg_sys_clkr;
        break;
    case REG_HIMR:
        val = s->regs.reg_himr;
        break;
    case REG_HIMRE:
        val = s->regs.reg_himre;
        break;
    case REG_HSISR:
        val = s->regs.reg_hsisr;
        break;
    case REG_EFUSE_ACCESS:
        val = s->regs.reg_efuse_access;
        break;
    case REG_EFUSE_TEST:
        val = s->regs.reg_efuse_test;
        break;
    case REG_EFUSE_CTRL:
        val = s->regs.reg_efuse_ctrl;
        break;
    case REG_CAMCMD:
        val = s->regs.reg_camcmd;
        break;
    case REG_CAMWRITE:
        val = s->regs.reg_camwrite;
        break;
    case REG_CAMREAD:
        val = s->regs.reg_camread;
        break;
    case REG_CAMDBG:
        val = s->regs.reg_camdbg;
        break;
    case REG_SECCFG:
        val = s->regs.reg_seccfg;
        break;
    case REG_SYS_CFG1:
        val = s->regs.reg_sys_cfg1;
        break;
    case REG_RXDMA_CONTROL:
        val = s->regs.reg_rxdma_control;
        break;
    case REG_NAV_UPPER:
        val = s->regs.reg_nav_upper;
        break;
    case REG_TRXPTCL_CTL:
        val = s->regs.reg_trxptcl_ctl;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "rtl8723be: unimplemented MMIO read at 0x%"HWADDR_PRIx"\n", addr);
        val = 0;
        break;
    }

    /* Adjust for access size */
    if (size == 1) {
        return (val >> ((addr & 3) * 8)) & 0xff;
    } else if (size == 2) {
        return (val >> ((addr & 2) * 8)) & 0xffff;
    } else {
        return val;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t *reg = NULL;

    switch (addr) {
    case REG_SYS_ISO_CTRL:
        reg = &s->regs.reg_sys_iso_ctrl;
        break;
    case REG_SYS_FUNC_EN:
        reg = &s->regs.reg_sys_func_en;
        break;
    case REG_SYS_CLKR:
        reg = &s->regs.reg_sys_clkr;
        break;
    case REG_HIMR:
        reg = &s->regs.reg_himr;
        break;
    case REG_HIMRE:
        reg = &s->regs.reg_himre;
        break;
    case REG_HSISR:
        reg = &s->regs.reg_hsisr;
        break;
    case REG_EFUSE_ACCESS:
        reg = &s->regs.reg_efuse_access;
        break;
    case REG_EFUSE_TEST:
        reg = &s->regs.reg_efuse_test;
        break;
    case REG_EFUSE_CTRL:
        reg = &s->regs.reg_efuse_ctrl;
        break;
    case REG_CAMCMD:
        reg = &s->regs.reg_camcmd;
        break;
    case REG_CAMWRITE:
        reg = &s->regs.reg_camwrite;
        break;
    case REG_CAMREAD:
        reg = &s->regs.reg_camread;
        break;
    case REG_CAMDBG:
        reg = &s->regs.reg_camdbg;
        break;
    case REG_SECCFG:
        reg = &s->regs.reg_seccfg;
        break;
    case REG_SYS_CFG1:
        reg = &s->regs.reg_sys_cfg1;
        break;
    case REG_RXDMA_CONTROL:
        reg = &s->regs.reg_rxdma_control;
        break;
    case REG_NAV_UPPER:
        reg = &s->regs.reg_nav_upper;
        break;
    case REG_TRXPTCL_CTL:
        reg = &s->regs.reg_trxptcl_ctl;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "rtl8723be: unimplemented MMIO write to 0x%"HWADDR_PRIx" val 0x%"PRIx64"\n", addr, val);
        return;
    }

    if (reg) {
        /* Update 32-bit register while preserving bits outside access */
        if (size == 1) {
            uint32_t shift = (addr & 3) * 8;
            *reg = (*reg & ~(0xff << shift)) | ((val & 0xff) << shift);
        } else if (size == 2) {
            uint32_t shift = (addr & 2) * 8;
            *reg = (*reg & ~(0xffff << shift)) | ((val & 0xffff) << shift);
        } else {
            *reg = (uint32_t) val;
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all registers to zero */
    memset(&s->regs, 0, sizeof(s->regs));
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
        /* No PIO BARs used by the driver */
        error_setg(errp, "rtl8723be: PIO BARs not supported");
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
    s->num_bars = 1;
    s->bar_info[0].index = 2;   /* Driver uses BAR2 */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;    /* Sufficient size covering all known registers */
    s->bar_info[0].name = "rtl8723be-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI or MSI-X initialization not explicitly required by the driver */
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
    .name = "rtl8723be_pci",
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
