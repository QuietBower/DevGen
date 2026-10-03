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

#define TYPE_PCIBASE_DEVICE "intel_eth_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Extracted from driver: /home/eely/linux-7.1/drivers/net/ethernet/stmicro/stmmac/dwmac-intel.c */
#define PCI_VENDOR_ID_INTEL       0x8086
#define PCI_DEVICE_ID_INTEL_QUARK 0x0937
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Register offsets and bitfields from driver headers */
#define SERDES_RATE_MASK    GENMASK(9, 8)
#define SERDES_RST          BIT(2)
#define SERDES_PWR_ST_P0    0x0
#define SERDES_RATE_PCIE_GEN1 0x0
#define SERDES_PLL_CLK      BIT(0)
#define SERDES_RATE_PCIE_SHIFT 8
#define SERDES_PHY_RX_CLK   BIT(1)
#define SERDES_PCLK_SHIFT   12
#define SERDES_GSR0         0x5
#define SERDES_GCR0         0xb
#define SERDES_PCLK_MASK    GENMASK(14, 12)
#define SERDES_PCLK_70MHZ   0x1
#define SERDES_PWR_ST_MASK  GENMASK(6, 4)
#define SERDES_PWR_ST_SHIFT 4
#define SERDES_RATE_PCIE_GEN2 0x1
#define SERDES_PWR_ST_P3    0x3
#define SERDES_LINK_MODE_2G5 0x3
#define SERDES_GCR          0x0
#define SERDES_LINK_MODE_MASK GENMASK(2, 1)
#define PCH_PTP_CLK_FREQ_MASK  (GMAC_GPO0)
#define PSE_PTP_CLK_FREQ_MASK  (GMAC_GPO0 | GMAC_GPO3)
#define PCH_PTP_CLK_FREQ_200MHZ (0)
#define GMAC_GPIO_STATUS    0x0000020C
#define PSE_PTP_CLK_FREQ_200MHZ (GMAC_GPO0 | GMAC_GPO3)
#define GMAC4_ART_TIME_SHIFT 16
#define PMC_ART_VALUE3      0x04
#define PMC_ART_VALUE2      0x03
#define PMC_ART_VALUE1      0x02
#define PMC_ART_VALUE0      0x01
#define GMAC_INT_TSIE       BIT(12)
#define GMAC_GPO1           BIT(17)
#define PTP_ACR_ATSEN1      BIT(5)
#define GMAC_TIMESTAMP_STATUS 0x00000b20
#define GMAC_TIMESTAMP_ATSNS_MASK GENMASK(29, 25)
#define PTP_ACR             0x40
#define PTP_ACR_ATSEN2      BIT(6)
#define GMAC_TIMESTAMP_ATSNS_SHIFT 25
#define PTP_ACR_ATSEN0      BIT(4)
#define PTP_ACR_MASK        GENMASK(7, 4)
#define PTP_ACR_ATSEN3      BIT(7)
#define PTP_ACR_ATSFC       BIT(0)
#define ART_CPUID_LEAF      0x15
#define R_PCH_FIA_15_PCR_LOS1_REG_BASE 8
#define B_PCH_FIA_PCR_L0O   GENMASK(3, 0)
#define DMA_AXI_BLEN4       BIT(1)
#define DMA_AXI_BLEN8       BIT(2)
#define INTEL_MGBE_XPCS_ADDR 0x16
#define INTEL_MGBE_ADHOC_ADDR 0x15
#define DMA_AXI_BLEN16      BIT(3)
#define EHL_PSE_ART_MHZ     19200000
#define STMMAC_MSI_VEC_MAX  32
#define N_MODPHY_PCR_LCPLL_DWORD7_1G       0x002A0003
#define N_MODPHY_PCR_LCPLL_DWORD2_1G       0x00000139
#define N_MODPHY_PCR_CMN_ANA_DWORD30_1G    0x0000D4AC
#define PID_MODPHY3_N_MODPHY_PCR_CMN_ANA_DWORD30 22
#define PID_MODPHY3_N_MODPHY_PCR_LPPLL_DWORD10   21
#define N_MODPHY_PCR_LPPLL_DWORD10_1G      0x00170008
#define PID_MODPHY3_N_MODPHY_PCR_LCPLL_DWORD2    19
#define PID_MODPHY3_B_MODPHY_PCR_LCPLL_DWORD0    18
#define PID_MODPHY3_N_MODPHY_PCR_LCPLL_DWORD7    20
#define B_MODPHY_PCR_LCPLL_DWORD0_1G       0x46AAAA41
#define N_MODPHY_PCR_CMN_ANA_DWORD30_2P5G  0x8200ACAC
#define N_MODPHY_PCR_LCPLL_DWORD2_2P5G     0x0000012D
#define N_MODPHY_PCR_LCPLL_DWORD7_2P5G     0x001F0003
#define N_MODPHY_PCR_LPPLL_DWORD10_2P5G    0x00170008
#define B_MODPHY_PCR_LCPLL_DWORD0_2P5G     0x58555551
#define PID_MODPHY1_N_MODPHY_PCR_LCPLL_DWORD7    15
#define PID_MODPHY1_N_MODPHY_PCR_CMN_ANA_DWORD30 17
#define PID_MODPHY1_B_MODPHY_PCR_LCPLL_DWORD0    13
#define PID_MODPHY1_N_MODPHY_PCR_LPPLL_DWORD10   16
#define PID_MODPHY1_N_MODPHY_PCR_LCPLL_DWORD2    14
#define STMMAC_PPS_MAX      4
#define GMAC_GPO0           BIT(16)
#define GMAC_GPO3           BIT(19)
#define GMAC_INT_STATUS     0x00000038

/* QEMU device state */
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

    /* Register shadow: entire MMIO space placeholder */
    uint8_t regs[0x1000]; /* Size unknown, placeholder */

    /* DMA state placeholder */
    struct {
        uint64_t tx_desc_addr;
        uint64_t rx_desc_addr;
        uint32_t tx_count;
        uint32_t rx_count;
    } dma;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: addr=0x%" HWADDR_PRIx " size=%u\n", __func__, addr, size);
        return 0;
    }
    memcpy(&val, &s->regs[addr], size);
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: addr=0x%" HWADDR_PRIx " size=%u val=0x%" PRIx64 "\n", __func__, addr, size, val);
        return;
    }
    memcpy(&s->regs[addr], &val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    /* PIO not used, placeholder empty */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used, placeholder empty */
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
    memset(s->regs, 0, sizeof(s->regs));
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_QUARK);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR initialization: BAR0 is MMIO, size placeholder (from regs array) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* Placeholder size */
    s->bar_info[0].name = "bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization: driver uses MSI with up to 32 vectors */
    s->has_msi = true;
    s->has_msix = false;
    if (msi_init(pdev, 0, STMMAC_MSI_VEC_MAX, true, false, errp) < 0) {
        error_append_hint(errp, "Failed to initialize MSI\n");
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
    .name = "intel_eth_pci_pci",
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
