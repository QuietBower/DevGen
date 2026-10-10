/*
 * QEMU PCI device model skeleton for intel-eth-pci (dwmac-intel based)
 * Phase 2: Behavioral implementation based on dwmac-intel.c
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

#define TYPE_PCIBASE_DEVICE "intel_eth_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Provide GENMASK macro equivalent used by driver code */
#ifndef GENMASK
#define GENMASK(h, l) (((~0U) - (1U << (l)) + 1U) & (~0U >> (31 - (h))))
#endif

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_QUARK 0x0937
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Minimal subset of GMAC register offsets and related defines that are
 * explicitly present in dwmac-intel.c and may be used during probe.
 */
#define GMAC_GPIO_STATUS              0x0000020C
#define GMAC_INT_STATUS               0x000000B0
#define GMAC_TIMESTAMP_STATUS         0x00000B20
#define GMAC_INT_EN                   0x000000B4
#define GMAC_MDIO_DATA                0x00000204
#define GMAC_MDIO_ADDR                0x00000200
#define GMAC_CONFIG                   0x00000000
#define GMAC_RXQ_CTRL0                0x000000A0
#define GMAC_RXQ_CTRL1                0x000000A4
#define GMAC_RXQ_CTRL2                0x000000A8
#define GMAC_RXQ_CTRL3                0x000000AC
#define GMAC_PACKET_FILTER            0x00000008
#define GMAC_VERSION                  0x00000020
#define GMAC4_VERSION                 0x00000110
#define GMAC_RX_FLOW_CTRL             0x00000090
#define GMAC_TXQ_PRTY_MAP0            0x00000098
#define GMAC_TXQ_PRTY_MAP1            0x0000009C
#define GMAC_PCS_BASE                 0x000000E0
#define GMAC_DEBUG                    0x00000114
#define GMAC_HW_FEATURE0              0x0000011C
#define GMAC_HW_FEATURE1              0x00000120
#define GMAC_HW_FEATURE2              0x00000124
#define GMAC_HW_FEATURE3              0x00000128
#define GMAC_EXT_CONFIG               0x00000004
#define GMAC_EXT_CFG1                 0x00000238
#define GMAC_ARP_ADDR                 0x00000210
#define GMAC4_LPI_CTRL_STATUS         0x000000D0
#define GMAC4_LPI_TIMER_CTRL          0x000000D4
#define GMAC4_LPI_ENTRY_TIMER         0x000000D8
#define GMAC4_MAC_ONEUS_TIC_COUNTER   0x000000DC

/* PTP block offsets (GMAC4 / XGMAC variants) */
#define PTP_GMAC4_OFFSET              0x00000B00
#define PTP_GMAC3_X_OFFSET            0x00000700
#define PTP_XGMAC_OFFSET              0x00000D00

/* MMC block offsets */
#define MMC_GMAC4_OFFSET              0x00000700
#define MMC_GMAC3_X_OFFSET            0x00000100
#define MMC_XGMAC_OFFSET              0x00000800

/* Basic PTP registers present in this file */
#define PTP_TCR                       0x00
#define PTP_SSIR                      0x04
#define PTP_STSR                      0x08
#define PTP_STNSR                     0x0C
#define PTP_STSUR                     0x10
#define PTP_STNSUR                    0x14
#define PTP_TAR                       0x18
#define PTP_ATNR                      0x48
#define PTP_ATSR                      0x4C
#define PTP_TS_EGR_CORR_NS            0x5C
#define PTP_TS_EGR_CORR_SNS           0x64
#define PTP_TS_EGR_LAT                0x6C
#define PTP_TS_INGR_CORR_NS           0x58
#define PTP_TS_INGR_CORR_SNS          0x60
#define PTP_TS_INGR_LAT               0x68

/* Some GMAC configuration bits explicitly used or defined in the file */
#define GMAC_CONFIG_RE                (1U << 0)
#define GMAC_CONFIG_TE                (1U << 1)
#define GMAC_CONFIG_DM                (1U << 13)
#define GMAC_CONFIG_FES               (1U << 14)
#define GMAC_CONFIG_PS                (1U << 15)
#define GMAC_CONFIG_JE                (1U << 16)
#define GMAC_CONFIG_JD                (1U << 17)
#define GMAC_CONFIG_BE                (1U << 18)
#define GMAC_CONFIG_DCRS              (1U << 9)
#define GMAC_CONFIG_IPC               (1U << 27)
#define GMAC_CONFIG_ARPEN             (1U << 31)

#define GMAC_INT_TSIE                 (1U << 12)
#define GMAC_INT_PMT_EN               (1U << 4)
#define GMAC_INT_LPI_EN               (1U << 5)

#define GMAC_INT_DEFAULT_ENABLE       (GMAC_INT_PMT_EN | GMAC_INT_LPI_EN | GMAC_INT_TSIE)

/* MAC/flow-control related masks */
#define GMAC_TX_FLOW_CTRL_TFE         (1U << 1)
#define GMAC_RX_FLOW_CTRL_RFE         (1U << 0)
#define GMAC_TX_FLOW_CTRL_PT_MASK     GENMASK(31, 16)

/* Misc constants in this driver */
#define STMMAC_MSI_VEC_MAX            32
#define STMMAC_PPS_MAX                4
#define STMMAC_RING_MODE              0x2
#define STMMAC_CHAIN_MODE             0x1

/* Additional timestamp-related defines from supplementary driver source */
#define GMAC_TIMESTAMP_ATSNS_MASK     GENMASK(29, 25)
#define GMAC_TIMESTAMP_ATSNS_SHIFT    25
#define PTP_ACR                       0x40
#define GMAC_GPO1                     (1U << 17)

/* NOTE: no explicit BAR indices/sizes are defined in this file. The
 * generic stmmac PCI code uses pci_ioremap_bar() etc. The actual BAR
 * size is platform specific and not expressed here. BAR configuration
 * is therefore left minimal and not tied to real hardware.
 */

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

    /* Interrupt shadow state */
    uint32_t gmac_int_status;
    uint32_t gmac_int_en;

    /* Hardware Register Shadows (The 'Identity' of the device)
     * Minimal set: version and core configuration register.
     */
    uint32_t gmac_version;
    uint32_t gmac_config;

    /* Selected GPIO register shadow for PTP clock & GPO1 toggling */
    uint32_t gmac_gpio_status;

    /* Timestamp status register shadow */
    uint32_t gmac_timestamp_status;

    /* Simple PTP block shadow (subset) */
    uint32_t ptp_regs[0x80 / 4];
};

static bool pcibase_msi_enabled(PCIBaseState *s)
{
    return msi_enabled(PCI_DEVICE(s));
}

static void pcibase_raise_irq(PCIBaseState *s, uint32_t mask)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    s->gmac_int_status |= mask;

    if (!s->gmac_int_en) {
        return;
    }

    if (s->gmac_int_status & s->gmac_int_en) {
        if (pcibase_msi_enabled(s)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    }
}

static void pcibase_lower_irq_if_clear(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!(s->gmac_int_status & s->gmac_int_en)) {
        if (!pcibase_msi_enabled(s)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only little-endian 32-bit accesses are expected in the driver. */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case GMAC_CONFIG:
        val = s->gmac_config;
        break;
    case GMAC_VERSION:
        /* Provide a non-zero version so core code treats hw as present. */
        val = s->gmac_version;
        break;
    case GMAC_INT_STATUS:
        val = s->gmac_int_status;
        break;
    case GMAC_INT_EN:
        val = s->gmac_int_en;
        break;
    case GMAC_GPIO_STATUS:
        val = s->gmac_gpio_status;
        break;
    case GMAC_TIMESTAMP_STATUS:
        val = s->gmac_timestamp_status;
        break;
    default:
        /* PTP register window (GMAC4 offset) */
        if (addr >= PTP_GMAC4_OFFSET && addr < PTP_GMAC4_OFFSET + 0x80) {
            hwaddr off = addr - PTP_GMAC4_OFFSET;
            unsigned idx = off >> 2;
            if (idx < (sizeof(s->ptp_regs) / sizeof(s->ptp_regs[0]))) {
                val = s->ptp_regs[idx];
            }
        } else {
            /* Unimplemented registers read as zero. */
            val = 0;
        }
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
    case GMAC_CONFIG:
        s->gmac_config = (uint32_t)val;
        break;
    case GMAC_INT_EN:
        s->gmac_int_en = (uint32_t)val;
        /* If enabling interrupts and status already set, raise. */
        pcibase_raise_irq(s, 0);
        break;
    case GMAC_INT_STATUS:
        /* Common GMAC convention: write-1-to-clear. */
        s->gmac_int_status &= ~((uint32_t)val);
        pcibase_lower_irq_if_clear(s);
        break;
    case GMAC_GPIO_STATUS:
        /* Driver uses read-modify-write; we simply store the new value. */
        s->gmac_gpio_status = (uint32_t)val;
        break;
    case GMAC_TIMESTAMP_STATUS:
        /* Write-1-to-clear for timestamp auxiliary snapshot count bits. */
        s->gmac_timestamp_status &= ~((uint32_t)val);
        break;
    default:
        /* PTP GMAC4 window writes */
        if (addr >= PTP_GMAC4_OFFSET && addr < PTP_GMAC4_OFFSET + 0x80) {
            hwaddr off = addr - PTP_GMAC4_OFFSET;
            unsigned idx = off >> 2;
            if (idx < (sizeof(s->ptp_regs) / sizeof(s->ptp_regs[0]))) {
                s->ptp_regs[idx] = (uint32_t)val;

                /* Very small subset of behavior: when the driver enables
                 * internal snapshot via PTP_ACR (0x40 from the PTP base),
                 * it later triggers a GPIO edge and waits for an interrupt
                 * via stmmac_cross_ts_isr(), which checks
                 * GMAC_INT_STATUS & GMAC_INT_TSIE and also decodes the
                 * auxiliary snapshot count from GMAC_TIMESTAMP_STATUS.
                 */
                if (off == PTP_ACR) {
                    /* Increment auxiliary snapshot count in
                     * GMAC_TIMESTAMP_STATUS field covered by
                     * GMAC_TIMESTAMP_ATSNS_MASK. We keep count small
                     * and wrap naturally within the mask.
                     */
                    uint32_t count = (s->gmac_timestamp_status & GMAC_TIMESTAMP_ATSNS_MASK) >> GMAC_TIMESTAMP_ATSNS_SHIFT;
                    count = (count + 1U) & ((GMAC_TIMESTAMP_ATSNS_MASK) >> GMAC_TIMESTAMP_ATSNS_SHIFT);
                    s->gmac_timestamp_status &= ~GMAC_TIMESTAMP_ATSNS_MASK;
                    s->gmac_timestamp_status |= (count << GMAC_TIMESTAMP_ATSNS_SHIFT) & GMAC_TIMESTAMP_ATSNS_MASK;

                    /* Reflect event on GPIO: toggle GPO1 bit to
                     * emulate the cross timestamp GPIO pulse.
                     */
                    s->gmac_gpio_status ^= GMAC_GPO1;

                    /* Raise timestamp interrupt if enabled. */
                    if (s->gmac_int_en & GMAC_INT_TSIE) {
                        pcibase_raise_irq(s, GMAC_INT_TSIE);
                    } else {
                        /* Set status so ISR can see it once enabled. */
                        s->gmac_int_status |= GMAC_INT_TSIE;
                        pcibase_raise_irq(s, 0);
                    }
                }
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

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

    /* Revert known register shadows to power-on defaults */
    s->gmac_version = 0x00001000; /* non-zero generic GMAC version */
    s->gmac_config = 0;
    s->gmac_int_status = 0;
    s->gmac_int_en = GMAC_INT_DEFAULT_ENABLE;
    s->gmac_gpio_status = 0;
    s->gmac_timestamp_status = 0;

    memset(s->ptp_regs, 0, sizeof(s->ptp_regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_QUARK);
    /* Network controller class code: 0x0200 (Ethernet controller). This
     * constant is normally from PCI_CLASS_NETWORK_ETHERNET.
     */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization
     * No explicit BAR sizes in this source; we expose a single MMIO
     * BAR large enough to cover GMAC/MTL/PTP regions the core may touch.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000; /* 16KB window */
    s->bar_info[0].name = "intel-eth-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X configuration: stmmac_config_multi_msi/single_msi will
     * call pci_alloc_irq_vectors(), which ends up using MSI/MSI-X
     * capabilities. Enable MSI capability here; multi-vector usage is
     * controlled by core PCI helpers.
     */
    if (!msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
    }

    s->has_msix = false;

    /* Initialize register shadows to reset defaults. */
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
    .name = "intel_eth_pci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(gmac_version, PCIBaseState),
        VMSTATE_UINT32(gmac_config, PCIBaseState),
        VMSTATE_UINT32(gmac_int_status, PCIBaseState),
        VMSTATE_UINT32(gmac_int_en, PCIBaseState),
        VMSTATE_UINT32(gmac_gpio_status, PCIBaseState),
        VMSTATE_UINT32(gmac_timestamp_status, PCIBaseState),
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
