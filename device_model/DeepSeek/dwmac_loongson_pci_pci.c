/*
 * QEMU model for dwmac-loongson-pci
 * Phase 4: fixed MDIO PHY reset handling to allow MDIO bus registration.
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
#include "hw/pci/pci_ids.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define PCI_VENDOR_ID_LOONGSON 0x0014

#define TYPE_PCIBASE_DEVICE "dwmac_loongson_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DMA_INTR_ENA_NIE_TX_LOONGSON	0x00040000
#define DMA_INTR_ENA_NIE_RX_LOONGSON	0x00020000
#define DMA_INTR_NORMAL_LOONGSON	(DMA_INTR_ENA_NIE_TX_LOONGSON | \
				 DMA_INTR_ENA_NIE_RX_LOONGSON | \
				 DMA_INTR_ENA_RIE | DMA_INTR_ENA_TIE)
#define DMA_INTR_ENA_AIE_TX_LOONGSON	0x00010000
#define DMA_INTR_ENA_AIE_RX_LOONGSON	0x00008000
#define DMA_INTR_ABNORMAL_LOONGSON	(DMA_INTR_ENA_AIE_TX_LOONGSON | \
				 DMA_INTR_ENA_AIE_RX_LOONGSON | \
				 DMA_INTR_ENA_FBE | DMA_INTR_ENA_UNE)
#define DMA_INTR_DEFAULT_MASK_LOONGSON	(DMA_INTR_NORMAL_LOONGSON | \
				 DMA_INTR_ABNORMAL_LOONGSON)
#define DMA_STATUS_NIS_TX_LOONGSON	0x00040000
#define DMA_STATUS_NIS_RX_LOONGSON	0x00020000
#define DMA_STATUS_AIS_TX_LOONGSON	0x00010000
#define DMA_STATUS_AIS_RX_LOONGSON	0x00008000
#define DMA_STATUS_FBI_TX_LOONGSON	0x00002000
#define DMA_STATUS_FBI_RX_LOONGSON	0x00001000
#define DMA_STATUS_MSK_COMMON_LOONGSON	(DMA_STATUS_NIS_TX_LOONGSON | \
				 DMA_STATUS_NIS_RX_LOONGSON | \
				 DMA_STATUS_AIS_TX_LOONGSON | \
				 DMA_STATUS_AIS_RX_LOONGSON | \
				 DMA_STATUS_FBI_TX_LOONGSON | \
				 DMA_STATUS_FBI_RX_LOONGSON)
#define DMA_STATUS_MSK_RX_LOONGSON	(DMA_STATUS_ERI | DMA_STATUS_RWT | \
				 DMA_STATUS_RPS | DMA_STATUS_RU  | \
				 DMA_STATUS_RI  | DMA_STATUS_OVF | \
				 DMA_STATUS_MSK_COMMON_LOONGSON)
#define DMA_STATUS_MSK_TX_LOONGSON	(DMA_STATUS_ETI | DMA_STATUS_UNF | \
				 DMA_STATUS_TJT | DMA_STATUS_TU  | \
				 DMA_STATUS_TPS | DMA_STATUS_TI  | \
				 DMA_STATUS_MSK_COMMON_LOONGSON)
#define PCI_DEVICE_ID_LOONGSON_GMAC1	0x7a03
#define PCI_DEVICE_ID_LOONGSON_GMAC2	0x7a23
#define PCI_DEVICE_ID_LOONGSON_GNET	0x7a13
#define DWMAC_CORE_MULTICHAN_V1	0x10
#define DWMAC_CORE_MULTICHAN_V2	0x12
#define DMA_INTR_ENA_RIE 0x00000040
#define DMA_INTR_ENA_TIE 0x00000001
#define DMA_INTR_ENA_FBE 0x00002000
#define DMA_INTR_ENA_UNE 0x00000020
#define DMA_STATUS_RU	0x00000080
#define DMA_STATUS_ERI	0x00004000
#define DMA_STATUS_RI	0x00000040
#define DMA_STATUS_RPS	0x00000100
#define DMA_STATUS_OVF	0x00000010
#define DMA_STATUS_RWT	0x00000200
#define DMA_STATUS_TJT	0x00000008
#define DMA_STATUS_TU	0x00000004
#define DMA_STATUS_TI	0x00000001
#define DMA_STATUS_ETI	0x00000400
#define DMA_STATUS_TPS	0x00000002
#define DMA_STATUS_UNF	0x00000020
#define GMAC_CONTROL_PS		0x00008000
#define MAC_CTRL_REG		0x00000000
#define DMA_BUS_MODE_FB		0x00010000
#define DMA_BUS_MODE_MB		0x04000000
#define DMA_BUS_MODE_PBL_MASK	GENMASK(13, 8)
#define DMA_BUS_MODE_AAL	0x02000000
#define DMA_BUS_MODE_MAXPBL	0x01000000
#define DMA_BUS_MODE_RPBL_MASK	GENMASK(22, 17)
#define DMA_BUS_MODE_ATDS	0x00000080
#define DMA_BUS_MODE_USP	0x00800000
#define DMA_CHAN_INTR_ENA(chan)	dma_chan_base_addr(DMA_INTR_ENA, chan)
#define DMA_CHAN_BUS_MODE(chan)	dma_chan_base_addr(DMA_BUS_MODE, chan)
#define DMA_INTR_ENA		0x0000101c
#define DMA_STATUS_GLI		0x04000000
#define DMA_STATUS_GPI		0x10000000
#define DMA_CHAN_STATUS(chan)	dma_chan_base_addr(DMA_STATUS, chan)
#define DMA_STATUS_GMI		0x08000000
#define GMAC_MII_ADDR		0x00000010
#define GMAC_CONTROL_FES	0x00004000
#define GMAC_MII_DATA		0x00000014
#define GMAC_CONTROL_DM		0x00000800
#define DWMAC_CORE_3_70		0x37
#define DMA_BUS_MODE		0x00001000
#define DMA_BUS_MODE_SFT_RESET	0x00000001
#define GMAC_VERSION		0x00000020
#define STMMAC_PPS_MAX		4
#define DMA_STATUS		0x00001014
#define DMA_CHAN_BASE_OFFSET		0x100

/* BAR and MSI placeholders */
#define BAR0_SIZE 0x4000
#define MSI_VECTOR_COUNT 8

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

    /* Hardware Register Shadows (extracted from driver register map) */
    uint32_t mac_ctrl;         /* MAC_CTRL_REG */
    uint32_t dma_bus_mode;     /* DMA_BUS_MODE */
    uint32_t dma_status;       /* DMA_STATUS */
    uint32_t dma_intr_ena;     /* DMA_INTR_ENA */
    uint32_t gmac_version;     /* GMAC_VERSION */
    uint32_t mii_addr;         /* GMAC_MII_ADDR */
    uint32_t mii_data;         /* GMAC_MII_DATA */
    /* Per-channel registers (up to 8 channels) */
    uint32_t chan_bus_mode[8];
    uint32_t chan_status[8];
    uint32_t chan_intr_ena[8];
    /* Simulated PHY registers (one PHY at address 0) */
    uint16_t phy_regs[32];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;
    uint32_t pending = 0;

    for (i = 0; i < 8; i++) {
        if (s->chan_status[i] & s->chan_intr_ena[i]) {
            pending = 1;
            break;
        }
    }

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported read size %u at addr 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return 0;
    }

    if (addr >= 0x1000 && addr < 0x1000 + 8 * DMA_CHAN_BASE_OFFSET) {
        int ch = (addr - 0x1000) / DMA_CHAN_BASE_OFFSET;
        hwaddr offset = (addr - 0x1000) % DMA_CHAN_BASE_OFFSET;
        if (ch >= 8) {
            return 0;
        }
        switch (offset) {
        case 0x00: /* DMA_CHAN_BUS_MODE */
            val = s->chan_bus_mode[ch];
            break;
        case 0x14: /* DMA_CHAN_STATUS */
            val = s->chan_status[ch];
            break;
        case 0x1c: /* DMA_CHAN_INTR_ENA */
            val = s->chan_intr_ena[ch];
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: read from unhandled channel register: ch=%d offset=0x%"HWADDR_PRIx"\n",
                          __func__, ch, offset);
            break;
        }
    } else {
        switch (addr) {
        case 0x00: /* MAC_CTRL_REG */
            val = s->mac_ctrl;
            break;
        case 0x10: /* GMAC_MII_ADDR */
            val = s->mii_addr;
            break;
        case 0x14: /* GMAC_MII_DATA */
            val = s->mii_data;
            break;
        case 0x20: /* GMAC_VERSION */
            val = s->gmac_version;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: read from unknown address 0x%"HWADDR_PRIx"\n",
                          __func__, addr);
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write size %u at addr 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return;
    }

    if (addr >= 0x1000 && addr < 0x1000 + 8 * DMA_CHAN_BASE_OFFSET) {
        int ch = (addr - 0x1000) / DMA_CHAN_BASE_OFFSET;
        hwaddr offset = (addr - 0x1000) % DMA_CHAN_BASE_OFFSET;
        if (ch >= 8) {
            return;
        }
        switch (offset) {
        case 0x00: /* DMA_CHAN_BUS_MODE */
            /* Handle SFT_RESET: clear it immediately */
            if (val & DMA_BUS_MODE_SFT_RESET) {
                val &= ~DMA_BUS_MODE_SFT_RESET;
            }
            s->chan_bus_mode[ch] = val;
            break;
        case 0x14: /* DMA_CHAN_STATUS (Write-1-to-Clear) */
            /* Clear only bits that are set in val */
            s->chan_status[ch] &= ~val;
            pcibase_update_irq(s);
            break;
        case 0x1c: /* DMA_CHAN_INTR_ENA */
            s->chan_intr_ena[ch] = val;
            pcibase_update_irq(s);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: write to unhandled channel register: ch=%d offset=0x%"HWADDR_PRIx"\n",
                          __func__, ch, offset);
            break;
        }
    } else {
        switch (addr) {
        case 0x00: /* MAC_CTRL_REG */
            s->mac_ctrl = val;
            break;
        case 0x10: /* GMAC_MII_ADDR */
            {
                uint32_t new_val = val & ~0x1; /* clear busy bit immediately */
                if (val & 0x1) { /* busy set, execute command */
                    uint32_t phy_addr = (val >> 11) & 0x1F;
                    uint32_t reg_addr = (val >> 6) & 0x1F;
                    if (val & 0x2) { /* write operation */
                        if (phy_addr == 0) {
                            s->phy_regs[reg_addr] = s->mii_data & 0xFFFF;
                            /* Handle PHY reset */
                            if (reg_addr == 0 && (s->phy_regs[0] & 0x8000)) {
                                s->phy_regs[0] &= ~0x8000; /* clear reset bit */
                                /* reinitialize basic registers after reset */
                                s->phy_regs[1] = 0x782D; /* BMSR */
                                s->phy_regs[2] = 0x0141; /* PHY ID1 */
                                s->phy_regs[3] = 0x0DD1; /* PHY ID2 */
                                s->phy_regs[4] = 0x01E1; /* ANAD */
                                s->phy_regs[5] = 0x0000; /* LPAD */
                                s->phy_regs[6] = 0x0004; /* ANER */
                            }
                        }
                    } else { /* read operation */
                        if (phy_addr == 0) {
                            s->mii_data = s->phy_regs[reg_addr];
                        } else {
                            s->mii_data = 0xFFFF; /* no PHY, return all ones */
                        }
                    }
                }
                s->mii_addr = new_val;
            }
            break;
        case 0x14: /* GMAC_MII_DATA */
            s->mii_data = val;
            break;
        case 0x20: /* GMAC_VERSION - read-only, ignore write */
            qemu_log_mask(LOG_GUEST_ERROR, "%s: write to read-only GMAC_VERSION (0x20)\n", __func__);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: write to unknown address 0x%"HWADDR_PRIx"\n",
                          __func__, addr);
            break;
        }
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
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    s->mac_ctrl = 0;
    s->dma_bus_mode = 0;
    s->dma_status = 0;
    s->dma_intr_ena = 0;
    s->gmac_version = 0x00000037; /* DWMAC_CORE_3_70 */
    s->mii_addr = 0;
    s->mii_data = 0;
    memset(s->chan_bus_mode, 0, sizeof(s->chan_bus_mode));
    memset(s->chan_status, 0, sizeof(s->chan_status));
    memset(s->chan_intr_ena, 0, sizeof(s->chan_intr_ena));

    /* Initialize simulated PHY registers for address 0 */
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    /* Basic Control */
    s->phy_regs[0] = 0x3100;
    /* BMSR: auto-negotiation complete, link up */
    s->phy_regs[1] = 0x782D;
    /* PHY ID: 0x0141 0x0DD1 (Marvell 88E1512) */
    s->phy_regs[2] = 0x0141;
    s->phy_regs[3] = 0x0DD1;
    /* Auto-negotiation advertisement */
    s->phy_regs[4] = 0x01E1;
    /* Link partner ability */
    s->phy_regs[5] = 0x0000;
    /* Auto-negotiation expansion */
    s->phy_regs[6] = 0x0004;
    /* Other registers default to 0 */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_LOONGSON);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_LOONGSON_GMAC1);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "mmio";
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI init */
    if (msi_init(pdev, 0, MSI_VECTOR_COUNT, true, false, errp)) {
        error_propagate(errp, NULL);
        return;
    }
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
    .name = "dwmac_loongson_pci_pci",
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

type_init(pcibase_register_types)
