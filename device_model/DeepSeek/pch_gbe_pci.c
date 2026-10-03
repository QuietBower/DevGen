/*
 * QEMU model for PCI-based OKI Semi PCH GbE (Ethernet controller) for QEMU 8.2.10.
 * Derived from Linux driver: pch_gbe_main.c at /home/eely/linux-7.1/drivers/net/ethernet/oki-semi/pch_gbe/
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

#define TYPE_PCIBASE_DEVICE "pch_gbe_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor/Device IDs and Class Code from driver */
#define PCI_VENDOR_ID_PCH_GbE          0x8086
#define PCI_DEVICE_ID_PCH_GbE          0x8802
#define PCI_CLASS_PCH_GbE              0x0200

/* BAR Index */
#define PCH_GBE_PCI_BAR                1

/* Register Offsets */
#define REG_INT_ST                      0x0000
#define REG_INT_EN                      0x0004
#define REG_MODE                        0x0008
#define REG_RESET                       0x000C
#define REG_TCPIP_ACC                   0x0010
#define REG_EX_LIST                     0x0014
#define REG_INT_ST_HOLD                 0x0018
#define REG_PHY_INT_CTRL                0x001C
#define REG_MAC_RX_EN                   0x0020
#define REG_RX_FCTRL                    0x0024
#define REG_PAUSE_REQ                   0x0028
#define REG_RX_MODE                     0x002C
#define REG_TX_MODE                     0x0030
#define REG_RX_FIFO_ST                  0x0034
#define REG_TX_FIFO_ST                  0x0038
#define REG_TX_FID                      0x003C
#define REG_TX_RESULT                   0x0040
#define REG_PAUSE_PKT1                  0x0044
#define REG_PAUSE_PKT2                  0x0048
#define REG_PAUSE_PKT3                  0x004C
#define REG_PAUSE_PKT4                  0x0050
#define REG_PAUSE_PKT5                  0x0054
#define REG_MAC_ADR_BASE                0x0060
#define REG_ADDR_MASK                   0x00E0
#define REG_MIIM                        0x00E4
#define REG_MAC_ADDR_LOAD               0x00E8
#define REG_RGMII_ST                    0x00EC
#define REG_RGMII_CTRL                  0x00F0
#define REG_DMA_CTRL                    0x0100
#define REG_RX_DSC_BASE                 0x0110
#define REG_RX_DSC_SIZE                 0x0114
#define REG_RX_DSC_HW_P                 0x0118
#define REG_RX_DSC_HW_P_HLD             0x011C
#define REG_RX_DSC_SW_P                 0x0120
#define REG_TX_DSC_BASE                 0x0130
#define REG_TX_DSC_SIZE                 0x0134
#define REG_TX_DSC_HW_P                 0x0138
#define REG_TX_DSC_HW_P_HLD             0x013C
#define REG_TX_DSC_SW_P                 0x0140
#define REG_RX_DMA_ST                   0x0150
#define REG_TX_DMA_ST                   0x0154
#define REG_WOL_ST                      0x0160
#define REG_WOL_CTRL                    0x0164
#define REG_WOL_ADDR_MASK               0x0168

/* Hardware constants derived from driver usage */
#define PCH_GBE_ALL_RST                 0x00000001
#define PCH_GBE_BUSY                    0x80000000
#define PCH_GBE_WLA_BUSY                0x80000000
#define PCH_GBE_MIIM_OPER_READY         0x04000000
#define PCH_GBE_MIIM_REG_ADDR_SHIFT     16
#define PCH_GBE_MIIM_PHY_ADDR_SHIFT     21
#define PCH_GBE_MIIM_OPER_READ          0x00000000
#define PCH_GBE_MIIM_OPER_WRITE         0x04000000
#define PCH_GBE_TX_DMA_EN               0x00000001
#define PCH_GBE_RX_DMA_EN               0x00000002
#define PCH_GBE_MRE_MAC_RX_EN           0x00000001
#define PCH_GBE_MODE_GMII_ETHER         0x80000000
#define PCH_GBE_MODE_FULL_DUPLEX        0x00000001
#define PCH_GBE_MODE_HALF_DUPLEX        0x00000000
#define PCH_GBE_MODE_MII_ETHER          0x40000000

/* MII standard register numbers */
#define MII_BMCR                        0x00
#define MII_BMSR                        0x01

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

struct pch_gbe_regs_mac_adr {
    uint32_t high;
    uint32_t low;
};

struct pch_gbe_regs {
    uint32_t INT_ST;
    uint32_t INT_EN;
    uint32_t MODE;
    uint32_t RESET;
    uint32_t TCPIP_ACC;
    uint32_t EX_LIST;
    uint32_t INT_ST_HOLD;
    uint32_t PHY_INT_CTRL;
    uint32_t MAC_RX_EN;
    uint32_t RX_FCTRL;
    uint32_t PAUSE_REQ;
    uint32_t RX_MODE;
    uint32_t TX_MODE;
    uint32_t RX_FIFO_ST;
    uint32_t TX_FIFO_ST;
    uint32_t TX_FID;
    uint32_t TX_RESULT;
    uint32_t PAUSE_PKT1;
    uint32_t PAUSE_PKT2;
    uint32_t PAUSE_PKT3;
    uint32_t PAUSE_PKT4;
    uint32_t PAUSE_PKT5;
    uint32_t reserve[2];
    struct pch_gbe_regs_mac_adr mac_adr[16];
    uint32_t ADDR_MASK;
    uint32_t MIIM;
    uint32_t MAC_ADDR_LOAD;
    uint32_t RGMII_ST;
    uint32_t RGMII_CTRL;
    uint32_t reserve3[3];
    uint32_t DMA_CTRL;
    uint32_t reserve4[3];
    uint32_t RX_DSC_BASE;
    uint32_t RX_DSC_SIZE;
    uint32_t RX_DSC_HW_P;
    uint32_t RX_DSC_HW_P_HLD;
    uint32_t RX_DSC_SW_P;
    uint32_t reserve5[3];
    uint32_t TX_DSC_BASE;
    uint32_t TX_DSC_SIZE;
    uint32_t TX_DSC_HW_P;
    uint32_t TX_DSC_HW_P_HLD;
    uint32_t TX_DSC_SW_P;
    uint32_t reserve6[3];
    uint32_t RX_DMA_ST;
    uint32_t TX_DMA_ST;
    uint32_t reserve7[2];
    uint32_t WOL_ST;
    uint32_t WOL_CTRL;
    uint32_t WOL_ADDR_MASK;
};

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;
    struct pch_gbe_regs reg;
    uint8_t power_state;
};

/* Helper to get 32-bit register pointer from MMIO offset */
static uint32_t *pcibase_get_reg32(PCIBaseState *s, hwaddr addr)
{
    if (addr >= 0x2000 || (addr & 3))
        return NULL;
    return (uint32_t *)((uint8_t *)&s->reg + addr);
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t *p;
    uint64_t val = 0;

    if (size != 4)
        return ~0ULL;

    p = pcibase_get_reg32(s, addr);
    if (!p)
        return ~0ULL;

    switch (addr) {
    case REG_ADDR_MASK:
        val = *p;
        if (val & PCH_GBE_BUSY) {
            *p = val & ~PCH_GBE_BUSY;
        }
        return val;
    case REG_WOL_ADDR_MASK:
        val = *p;
        if (val & PCH_GBE_WLA_BUSY) {
            *p = val & ~PCH_GBE_WLA_BUSY;
        }
        return val;
    case REG_MIIM:
        /* Force the ready bit to be set on every read to satisfy the driver's
         * polling loop.  Prevents infinite "pch-gbe.miim won't go Ready" errors. */
        return *p | PCH_GBE_MIIM_OPER_READY;
    default:
        return *p;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t *p;

    if (size != 4)
        return;

    p = pcibase_get_reg32(s, addr);
    if (!p)
        return;

    switch (addr) {
    case REG_MAC_ADDR_LOAD:
        if (val & 1) {
            /* Load default MAC address: 02:00:00:00:00:00 */
            s->reg.mac_adr[0].high = 0x00000002;
            s->reg.mac_adr[0].low = 0x00000000;
        }
        break;
    case REG_RESET:
        *p = val;
        if (val & PCH_GBE_ALL_RST) {
            *p &= ~PCH_GBE_ALL_RST;
            s->reg.MAC_RX_EN = 0;
            s->reg.DMA_CTRL = 0;
            s->reg.MIIM = PCH_GBE_MIIM_OPER_READY;
        }
        break;
    case REG_ADDR_MASK:
        *p = val & ~PCH_GBE_BUSY;
        break;
    case REG_WOL_ADDR_MASK:
        *p = val & ~PCH_GBE_WLA_BUSY;
        break;
    case REG_MIIM:
        {
            uint32_t phy_addr = (val >> PCH_GBE_MIIM_PHY_ADDR_SHIFT) & 0x1F;
            uint32_t reg_addr = (val >> PCH_GBE_MIIM_REG_ADDR_SHIFT) & 0x1F;
            bool is_write = val & PCH_GBE_MIIM_OPER_WRITE;
            uint16_t data = val & 0xFFFF;
            uint16_t ret = 0;

            if (phy_addr == 1) {
                if (!is_write) {
                    if (reg_addr == MII_BMCR) {
                        ret = 0x1000; /* auto-negotiation enabled */
                    } else if (reg_addr == MII_BMSR) {
                        ret = 0x786d; /* link up, capable */
                    } else {
                        ret = 0x0000;
                    }
                }
                /* Writes ignored */
            } else {
                if (!is_write)
                    ret = 0xFFFF; /* no device */
            }
            s->reg.MIIM = PCH_GBE_MIIM_OPER_READY | (ret & 0xFFFF);
        }
        break;
    default:
        *p = val;
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(&s->reg, 0, sizeof(s->reg));
    /* Reload default MAC */
    s->reg.mac_adr[0].high = 0x00000002;
    s->reg.mac_adr[0].low = 0x00000000;
    /* Set MIIM ready to allow driver to detect idle state */
    s->reg.MIIM = PCH_GBE_MIIM_OPER_READY;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_PCH_GbE );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PCH_GbE );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_PCH_GbE );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        error_report("msi_init failed");
    }

    s->num_bars = 1;
    s->bar_info[0].index = PCH_GBE_PCI_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000;
    s->bar_info[0].name = "pch_gbe_mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize default MAC address */
    s->reg.mac_adr[0].high = 0x00000002;
    s->reg.mac_adr[0].low = 0x00000000;
    /* MIIM ready initially */
    s->reg.MIIM = PCH_GBE_MIIM_OPER_READY;
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
    .name = "pch_gbe_pci",
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
