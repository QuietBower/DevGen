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

#define TYPE_PCIBASE_DEVICE "rtl8192ce_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DEVICE_ID 0x8191

/* Register offsets from driver */
#define REG_SYS_ISO_CTRL                 0x0000
#define REG_SYS_FUNC_EN                  0x0002
#define REG_SYS_CLKR                     0x0008
#define REG_9346CR                       0x000A
#define REG_AFE_XTAL_CTRL                0x0024
#define REG_AFE_PLL_CTRL                 0x0028
#define REG_EFUSE_CTRL                   0x0030
#define REG_EFUSE_TEST                   0x0034
#define REG_GPIO_MUXCFG                  0x0040
#define REG_GPIO_IO_SEL                  0x0042
#define REG_MAC_PINMUX_CFG               0x0043
#define REG_GPIO_PIN_CTRL                0x0044
#define REG_LEDCFG0                      0x004C
#define REG_CR                           0x0100
#define REG_HIMR                         0x0120
#define REG_HISR                         0x0124
#define REG_HIMRE                        0x0128
#define REG_TSFTR                        0x0560
#define REG_BCN_INTERVAL                 0x0554
#define REG_ATIMWND                      0x055A
#define REG_RXTSF_OFFSET_CCK             0x055E
#define REG_RXTSF_OFFSET_OFDM            0x055F
#define REG_BCNTCFG                      0x0510
#define REG_EDCA_VI_PARAM                0x0504
#define REG_EDCA_BK_PARAM                0x050C
#define REG_EDCA_VO_PARAM                0x0500
#define REG_EDCA_BE_PARAM                0x0508
#define REG_RCR                          0x0608
#define REG_BSSID                        0x0618
#define REG_MACID                        0x0610
#define REG_AMPDU_MIN_SPACE              0x045C
#define REG_ACMHWCTRL                    0x05C0
#define REG_FWHW_TXQ_CTRL                0x0420
#define REG_SIFS_CTX                     0x0514
#define REG_RESP_SIFS_OFDM               0x063E
#define REG_BCN_PSR_RPT                  0x06A8
#define REG_MAC_SPEC_SIFS                0x063A
#define REG_DUAL_TSF_RST                 0x0553
#define REG_PCIE_HRPWM                   0x0361
#define REG_SLOT                         0x051B
#define REG_RL                           0x042A
#define REG_INIRTS_RATE_SEL              0x0480
#define REG_AGGLEN_LMT                   0x0458
#define REG_BT_COEX_TABLE                0x06C0
#define REG_HPON_FSM                     0x00EC
#define REG_SYS_CFG                      0x00F0
#define REG_SECCFG                       0x0680
#define REG_CAMCMD                       0x0670
#define REG_CAMDBG                       0x067C
#define REG_CAMREAD                      0x0678
#define REG_CAMWRITE                     0x0674
#define REG_PCIE_CTRL_REG                0x0300
#define REG_TXPAUSE                      0x0522
#define REG_RRSR                         0x0440
#define REG_BWOPMODE                     0x0603
#define REG_SIFS_TRX                     0x0516
#define REG_SPEC_SIFS                    0x0428
#define REG_ARFR0                        0x0444

#define BAR2_SIZE                        0x4000

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
    uint32_t intr_status_low;
    uint32_t intr_status_high;
    uint32_t intr_mask_low;
    uint32_t intr_mask_high;

    /* Full MMIO shadow storage */
    uint8_t mmio_data[BAR2_SIZE];

    bool reset_active;
};

/* Update IRQ line based on interrupt status and mask */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t status = ((uint64_t)s->intr_status_high << 32) | s->intr_status_low;
    uint64_t mask   = ((uint64_t)s->intr_mask_high << 32) | s->intr_mask_low;

    if (status & mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > BAR2_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: addr=0x%" HWADDR_PRIx ", size=%u\n",
                      __func__, addr, size);
        return ~0ULL;
    }

    /* Special read handling: i.e., side effects if any */
    switch (addr) {
    default:
        break;
    }

    /* General read from shadow array */
    memcpy(&val, &s->mmio_data[addr], size);
    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t wval32;

    if (addr + size > BAR2_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: addr=0x%" HWADDR_PRIx ", size=%u, val=0x%" PRIx64 "\n",
                      __func__, addr, size, val);
        return;
    }

    /* Write to shadow array */
    memcpy(&s->mmio_data[addr], &val, size);

    /* Side effects for specific registers */
    switch (addr) {
    case REG_HIMR:
        if (size == 4) {
            s->intr_mask_low = ldl_le_p(&s->mmio_data[REG_HIMR]);
            pcibase_update_irq(s);
        }
        break;
    case REG_HIMRE:
        if (size == 4) {
            s->intr_mask_high = ldl_le_p(&s->mmio_data[REG_HIMRE]);
            pcibase_update_irq(s);
        }
        break;
    case REG_HISR:
        if (size == 4) {
            wval32 = ldl_le_p(&s->mmio_data[REG_HISR]);
            s->intr_status_low &= ~wval32;
            stl_le_p(&s->mmio_data[REG_HISR], s->intr_status_low);
            pcibase_update_irq(s);
        }
        break;
    default:
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

    memset(s->mmio_data, 0, sizeof(s->mmio_data));
    s->intr_status_low = 0;
    s->intr_status_high = 0;
    s->intr_mask_low = 0;
    s->intr_mask_high = 0;
    /* Set auto-load OK flag for EFUSE/EEPROM */
    s->mmio_data[REG_9346CR] = 0x20;
    pcibase_update_irq(s);
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x10EC);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x8191);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Single BAR2 (index 2) */
    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = BAR2_SIZE;
    s->bar_info[0].name  = "rtl8192ce-bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
    .name = "rtl8192ce_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(mmio_data, PCIBaseState, BAR2_SIZE),
        VMSTATE_UINT32(intr_status_low, PCIBaseState),
        VMSTATE_UINT32(intr_status_high, PCIBaseState),
        VMSTATE_UINT32(intr_mask_low, PCIBaseState),
        VMSTATE_UINT32(intr_mask_high, PCIBaseState),
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