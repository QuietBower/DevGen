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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "rtl8723ae_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define RTL8723E_VENDOR_ID PCI_VENDOR_ID_REALTEK
#define RTL8723E_DEVICE_ID 0x8723
#define RTL8723E_CLASS_ID  PCI_CLASS_NETWORK_OTHER

#define REG_SYS_ISO_CTRL 0x0000
#define REG_SYS_FUNC_EN  0x0002
#define REG_SYS_CLKR     0x0008
#define REG_HIMR         0x0120
#define REG_HIMRE        0x0128
#define REG_EFUSE_TEST   0x0034
#define REG_EFUSE_CTRL   0x0030
#define REG_CAMCMD       0x0670
#define REG_CAMWRITE     0x0674
#define REG_CAMREAD      0x0678
#define REG_CAMDBG       0x067C
#define REG_SECCFG       0x0680

#define REG_RCR          0x0608
#define RCR_ACRC32       BIT(8)
#define RCR_AICV         BIT(9)
#define REG_TSFTR        0x0560
#define REG_MACID        0x0610
#define REG_RRSR         0x0440
#define REG_INIRTS_RATE_SEL 0x0480
#define REG_BSSID        0x0618
#define REG_SIFS_CTX     0x0514
#define REG_SIFS_TRX     0x0516
#define REG_SPEC_SIFS    0x0428
#define REG_MAC_SPEC_SIFS 0x063A
#define REG_RESP_SIFS_OFDM 0x063E
#define REG_SLOT         0x051B
#define REG_AMPDU_MIN_SPACE 0x045C
#define REG_AGGLEN_LMT   0x0458
#define REG_ACMHWCTRL    0x05C0
#define REG_RL           0x042A
#define REG_DUAL_TSF_RST 0x0553
#define REG_PCIE_HRPWM   0x0361
#define REG_CR           0x00
#define REG_FWHW_TXQ_CTRL 0x0420
#define REG_BCN_PSR_RPT  0x06A8
#define IMR8190_DISABLED 0x0

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
    bool irq_enabled;
    bool int_clear;
    uint32_t irq_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t reg_bcn_ctrl_val;
    uint32_t reg_874;
    uint32_t reg_c70;
    uint32_t reg_85c;
    uint32_t reg_a74;

    /* DMA Context */
    dma_addr_t tx_ring_dma[9];
    dma_addr_t rx_ring_dma[2];
    uint32_t transmit_config;
    uint32_t receive_config;

    bool driver_is_goingto_unload;
    bool up_first_time;
    bool first_init;
    bool being_init_adapter;
    bool init_ready;
    
    uint8_t const_pci_aspm;
    uint8_t const_hwsw_rfoff_d3;
    uint8_t const_support_pciaspm;
    uint8_t const_hostpci_aspm_setting;
    uint8_t const_devicepci_aspm_setting;
    bool support_aspm;
    bool support_backdoor;
    
    uint8_t irq_alloc;
};

#define PHIMR_MGNTDOK          BIT(6)
#define PHIMR_PSTIMEOUT        BIT(29)
#define PHIMR_BEDOK            BIT(4)
#define PHIMR_VODOK            BIT(2)
#define PHIMR_TXBCNOK          BIT(25)
#define PHIMR_HISRE_IND        BIT(11)
#define PHIMR_ROK              BIT(0)
#define PHIMR_C2HCMD           BIT(10)
#define PHIMR_VIDOK            BIT(3)
#define PHIMR_RDU              BIT(1)
#define PHIMR_RXFOVW           BIT(8)
#define PHIMR_BKDOK            BIT(5)
#define PHIMR_TSF_BIT32_TOGGLE BIT(24)
#define PHIMR_HIGHDOK          BIT(7)
#define PHIMR_TXBCNERR         BIT(26)
#define PHIMR_TXFOVW           BIT(9)
#define PHIMR_BCNDOK0          BIT(16)
#define PHIMR_BCNDMAINT0       BIT(20)
#define PHIMR_ATIMEND_E        BIT(13)

#define RTL_PCI_MAX_TX_QUEUE_COUNT 9
#define RTL_PCI_MAX_RX_QUEUE       2

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x3a0:
        val = s->irq_status;
        break;
    case 0x3a8:
        val = s->irq_mask[0];
        break;
    case 0x3ac:
        val = s->irq_mask[1];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x3a0:
        s->irq_status &= ~val;
        if (!s->irq_status) {
            pci_set_irq(&s->parent_obj, 0);
        }
        break;
    case 0x3a8:
        s->irq_mask[0] = val;
        break;
    case 0x3ac:
        s->irq_mask[1] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", __func__, addr, val);
        break;
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
    s->irq_status = 0;
    s->irq_mask[0] = 0;
    s->irq_mask[1] = 0;
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  RTL8723E_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RTL8723E_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RTL8723E_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x70);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0x40, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "rtl8723ae-pio";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x4000;
    s->bar_info[1].name = "rtl8723ae-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
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
    .name = "rtl8723ae_pci",
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
