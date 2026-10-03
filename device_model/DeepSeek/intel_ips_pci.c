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
#include <string.h>

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "intel_ips_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_THERMAL_SENSOR 0x3b32
#define PCI_CLASS_ID 0xff00

#define PLATFORM_INFO    0xce
#define PLATFORM_TDP        (1<<29)
#define PLATFORM_RATIO    (1<<28)
#define IA32_MISC_ENABLE    0x1a0
#define IA32_MISC_TURBO_EN    (1ULL<<38)
#define TURBO_POWER_CURRENT_LIMIT    0x1ac
#define TURBO_TDC_OVR_EN    (1UL<<31)
#define TURBO_TDC_MASK    (0x000000007fff0000UL)
#define TURBO_TDC_SHIFT    (16)
#define TURBO_TDP_OVR_EN    (1UL<<15)
#define TURBO_TDP_MASK    (0x0000000000003fffUL)
#define IA32_PERF_CTL        0x199
#define IA32_PERF_TURBO_DIS    (1ULL<<32)
#define THM_CFG_TBAR    0x10
#define THM_CFG_TBAR_HI    0x14
#define THM_TSIU    0x00
#define THM_TSE        0x01
#define TSE_EN    0xb8
#define THM_TSS        0x02
#define THM_TSTR    0x03
#define THM_TSTTP    0x04
#define THM_TSCO    0x08
#define THM_TSES    0x0c
#define THM_TSGPEN    0x0d
#define TSGPEN_HOT_LOHI    (1<<1)
#define TSGPEN_CRIT_LOHI    (1<<2)
#define THM_TSPC    0x0e
#define THM_PPEC    0x10
#define THM_CTA        0x12
#define THM_PTA        0x14
#define PTA_SLOPE_MASK    (0xff00)
#define PTA_SLOPE_SHIFT    8
#define PTA_OFFSET_MASK    (0x00ff)
#define THM_MGTA    0x16
#define MGTA_SLOPE_MASK    (0xff00)
#define MGTA_SLOPE_SHIFT    8
#define MGTA_OFFSET_MASK    (0x00ff)
#define THM_TRC        0x1a
#define TRC_CORE2_EN    (1<<15)
#define TRC_THM_EN    (1<<12)
#define TRC_C6_WAR    (1<<8)
#define TRC_CORE1_EN    (1<<7)
#define TRC_CORE_PWR    (1<<6)
#define TRC_PCH_EN    (1<<5)
#define TRC_MCH_EN    (1<<4)
#define TRC_DIMM4    (1<<3)
#define TRC_DIMM3    (1<<2)
#define TRC_DIMM2    (1<<1)
#define TRC_DIMM1    (1<<0)
#define THM_TES        0x20
#define THM_TEN        0x21
#define TEN_UPDATE_EN    1
#define THM_PSC        0x24
#define PSC_NTG    (1<<0)
#define PSC_NTPC    (1<<1)
#define PSC_PP_DEF    (0<<2)
#define PSP_PP_PC    (1<<2)
#define PSP_PP_BAL    (2<<2)
#define PSP_PP_GFX    (3<<2)
#define PSP_PBRT    (1<<4)
#define THM_CTV1    0x30
#define CTV_TEMP_ERROR (1<<15)
#define CTV_TEMP_MASK    0x3f
#define THM_CTV2    0x32
#define THM_CEC        0x34
#define THM_AE        0x3f
#define THM_HTS        0x50
#define HTS_PCPL_MASK    (0x7fe00000)
#define HTS_PCPL_SHIFT 21
#define HTS_GPL_MASK  (0x001ff000)
#define HTS_GPL_SHIFT 12
#define HTS_PP_MASK    (0x00000c00)
#define HTS_PP_SHIFT  10
#define HTS_PP_DEF    0
#define HTS_PP_PROC    1
#define HTS_PP_BAL    2
#define HTS_PP_GFX    3
#define HTS_PCTD_DIS    (1<<9)
#define HTS_GTD_DIS    (1<<8)
#define HTS_PTL_MASK  (0x000000fe)
#define HTS_PTL_SHIFT 1
#define HTS_NVV    (1<<0)
#define THM_HTSHI    0x54
#define HTS2_PPL_MASK        (0x03ff)
#define HTS2_PRST_MASK    (0x3c00)
#define HTS2_PRST_SHIFT    10
#define HTS2_PRST_UNLOADED    0
#define HTS2_PRST_RUNNING    1
#define HTS2_PRST_TDISOP    2
#define HTS2_PRST_TDISHT    3
#define HTS2_PRST_TDISUSR    4
#define HTS2_PRST_TDISPLAT    5
#define HTS2_PRST_TDISPM    6
#define HTS2_PRST_TDISERR    7
#define THM_PTL        0x56
#define THM_MGTV    0x58
#define TV_MASK    0x000000000000ff00
#define TV_SHIFT    8
#define THM_PTV        0x60
#define PTV_MASK    0x00ff
#define THM_MMGPC    0x64
#define THM_MPPC    0x66
#define THM_MPCPC    0x68
#define THM_TSPIEN    0x82
#define TSPIEN_AUX_LOHI    (1<<0)
#define TSPIEN_HOT_LOHI    (1<<1)
#define TSPIEN_CRIT_LOHI    (1<<2)
#define TSPIEN_AUX2_LOHI    (1<<3)
#define THM_TSLOCK    0x83
#define THM_ATR        0x84
#define THM_TOF        0x87
#define THM_STS        0x98
#define STS_PCPL_MASK        (0x7fe00000)
#define STS_PCPL_SHIFT    21
#define STS_GPL_MASK        (0x001ff000)
#define STS_GPL_SHIFT        12
#define STS_PP_MASK        (0x00000c00)
#define STS_PP_SHIFT        10
#define STS_PP_DEF        0
#define STS_PP_PROC        1
#define STS_PP_BAL        2
#define STS_PP_GFX        3
#define STS_PCTD_DIS        (1<<9)
#define STS_GTD_DIS        (1<<8)
#define STS_PTL_MASK        (0x000000fe)
#define STS_PTL_SHIFT        1
#define STS_NVV        (1<<0)
#define THM_SEC        0x9c
#define SEC_ACK    (1<<0)
#define THM_TC3        0xa4
#define THM_TC1        0xa8
#define STS_PPL_MASK        (0x0003ff00)
#define STS_PPL_SHIFT        16
#define THM_TC2        0xac
#define THM_DTV        0xb0
#define THM_ITV        0xd8
#define ITV_ME_SEQNO_MASK 0x00ff0000
#define ITV_ME_SEQNO_SHIFT (16)
#define ITV_MCH_TEMP_MASK 0x0000ff00
#define ITV_MCH_TEMP_SHIFT (8)
#define ITV_PCH_TEMP_MASK 0x000000ff

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* register shadow */
    uint8_t regs[256];
};

/* Internal helper for status-triggered signaling. Not used but kept for template consistency. */

/* Device-initiated DMA logic based on driver access patterns - not used */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds MMIO read at addr 0x%" HWADDR_PRIx " size %d\n",
                      __func__, addr, size);
        return ~0ULL;
    }
    memcpy(&val, &s->regs[addr], size);
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds MMIO write at addr 0x%" HWADDR_PRIx " size %d val 0x%" PRIx64 "\n",
                      __func__, addr, size, val);
        return;
    }
    memcpy(&s->regs[addr], &val, size);

    /* Special handling for interrupt acknowledgment would go here if needed */
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

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[THM_TSE] = TSE_EN;
    s->regs[THM_TRC] = TRC_CORE1_EN | TRC_CORE_PWR | TRC_MCH_EN;
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
    }
    /* No other BAR types used; PIO and RAM removed to avoid unused symbols */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x3b32);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xff00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Setup BAR information */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "ips-mmio";

    /* BAR Initialization */
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
    .name = "intel_ips_pci",
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
