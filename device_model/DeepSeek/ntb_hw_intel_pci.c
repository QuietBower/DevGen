/*
 * QEMU Intel Xeon NTB Device Model (Gen1)
 * Based on Linux driver ntb_hw_gen1.c
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

#define TYPE_PCIBASE_DEVICE "ntb_hw_intel_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor/Device IDs (first entry in pci_device_id table) */
#define PCI_VENDOR_ID_INTEL         0x8086
#define PCI_DEVICE_ID_INTEL_NTB_B2B_JSF 0x3725
#define PCI_CLASS_BRIDGE_NTB        0x0680

/* Register Offsets (from driver source) */
#define XEON_NTBCNTL_OFFSET         0x0058
#define XEON_PPD_OFFSET             0x00d4
#define XEON_LINK_STATUS_OFFSET     0x01a2
#define XEON_PDOORBELL_OFFSET       0x0060
#define XEON_PDBMSK_OFFSET          0x0062
#define XEON_SDOORBELL_OFFSET       0x0064
#define XEON_SDBMSK_OFFSET          0x0066
#define XEON_SPAD_OFFSET            0x0080
#define XEON_B2B_SPAD_OFFSET        0x0100
#define XEON_B2B_DOORBELL_OFFSET    0x0140
#define XEON_SBAR0BASE_OFFSET       0x0040
#define XEON_SBAR23LMT_OFFSET       0x0020
#define XEON_SBAR23XLAT_OFFSET      0x0030
#define XEON_SBAR4LMT_OFFSET        0x0028
#define XEON_SBAR4XLAT_OFFSET       0x0038
#define XEON_SBAR5LMT_OFFSET        0x002c
#define XEON_SBAR5XLAT_OFFSET       0x003c
#define XEON_PBAR23LMT_OFFSET       0x0000
#define XEON_PBAR23XLAT_OFFSET      0x0010
#define XEON_PBAR45LMT_OFFSET       0x0008
#define XEON_PBAR45XLAT_OFFSET      0x0018
#define XEON_PBAR5LMT_OFFSET       0x000c
#define XEON_PBAR5XLAT_OFFSET       0x001c
#define XEON_DEVSTS_OFFSET          0x059a
#define XEON_UNCERRSTS_OFFSET       0x014c
#define XEON_CORERRSTS_OFFSET       0x0158

/* NTB Control bits */
#define NTB_CTL_DISABLE             BIT(1)
#define NTB_CTL_CFG_LOCK            BIT(0)
#define NTB_CTL_S2P_BAR2_SNOOP      BIT(2)
#define NTB_CTL_P2S_BAR2_SNOOP      BIT(4)
#define NTB_CTL_S2P_BAR4_SNOOP      BIT(6)
#define NTB_CTL_P2S_BAR4_SNOOP      BIT(8)
#define NTB_CTL_S2P_BAR5_SNOOP      BIT(12)
#define NTB_CTL_P2S_BAR5_SNOOP      BIT(14)

/* Link Status bits */
#define NTB_LNK_STA_ACTIVE_BIT      0x2000
#define NTB_LNK_STA_ACTIVE(sta)     (!!((sta) & NTB_LNK_STA_ACTIVE_BIT))
#define NTB_LNK_STA_WIDTH_MASK      0x03f0
#define NTB_LNK_STA_SPEED_MASK      0x000f

/* PPD register definitions (inferred from typical Intel NTB) */
#define XEON_PPD_TOPO_MASK          0x0F
#define XEON_PPD_TOPO_B2B_USD       0x01
#define XEON_PPD_TOPO_B2B_DSD       0x02
#define XEON_PPD_TOPO_PRI_USD       0x03
#define XEON_PPD_TOPO_PRI_DSD       0x04
#define XEON_PPD_TOPO_SEC_USD       0x05
#define XEON_PPD_TOPO_SEC_DSD       0x06
#define XEON_PPD_SPLIT_BAR_MASK     BIT(5)

#define XEON_DB_MSIX_VECTOR_SHIFT   5
#define XEON_DB_MSIX_VECTOR_COUNT   4
#define XEON_DB_TOTAL_SHIFT         16
#define XEON_DB_COUNT               15
#define XEON_DB_LINK_BIT            BIT(15) /* guessed */
#define XEON_SPAD_COUNT             16
#define XEON_MW_COUNT               2

/* BAR types */
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
    uint32_t intr_status;   /* unused, but kept for potential debug */
    uint32_t intr_mask;

    struct {
        uint32_t ntb_ctl;
        uint8_t  ppd;
        uint16_t link_status;
        uint32_t sbar0_base;
        uint32_t sbar2_limit;
        uint32_t sbar2_xlat;
        uint32_t sbar4_limit;
        uint32_t sbar4_xlat;
        uint32_t sbar5_limit;
        uint32_t sbar5_xlat;
        uint32_t pbar2_limit;
        uint32_t pbar2_xlat;
        uint32_t pbar4_limit;
        uint32_t pbar4_xlat;
        uint32_t pbar5_limit;
        uint32_t pbar5_xlat;
        uint16_t db_bell[2];  /* 0=PDOORBELL, 1=SDOORBELL */
        uint16_t db_mask[2];  /* 0=PDBMSK, 1=SDBMSK */
        uint32_t spad[XEON_SPAD_COUNT];
        uint32_t b2b_spad[XEON_SPAD_COUNT];
        uint16_t b2b_db;
        uint32_t devsts;
        uint32_t uncerrsts;
        uint32_t corerrsts;
    } regs;

    uint32_t status;
    bool reset_active;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t db_pending = (uint64_t)s->regs.db_bell[0] & s->regs.db_mask[0];
    int vec_count = XEON_DB_MSIX_VECTOR_COUNT;
    int shift = XEON_DB_MSIX_VECTOR_SHIFT;
    uint64_t vec_mask;
    int i;

    if (msix_enabled(pdev)) {
        for (i = 0; i < vec_count; i++) {
            vec_mask = ((1ULL << shift) - 1) << (shift * i);
            if (db_pending & vec_mask) {
                msix_notify(pdev, i);
            }
        }
    } else if (msi_enabled(pdev)) {
        if (db_pending) {
            msi_notify(pdev, 0);
        }
    } else {
        /* INTx emulation: raise if pending, lower if not */
        if (db_pending) {
            pci_set_irq(pdev, 1);
        } else {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case XEON_NTBCNTL_OFFSET:
        if (size == 4) val = s->regs.ntb_ctl;
        else val = ~0ULL;
        break;
    case XEON_PDOORBELL_OFFSET:
        if (size == 2) val = s->regs.db_bell[0];
        else val = ~0ULL;
        break;
    case XEON_PDBMSK_OFFSET:
        if (size == 2) val = s->regs.db_mask[0];
        else val = ~0ULL;
        break;
    case XEON_SDOORBELL_OFFSET:
        if (size == 2) val = s->regs.db_bell[1];
        else val = ~0ULL;
        break;
    case XEON_SDBMSK_OFFSET:
        if (size == 2) val = s->regs.db_mask[1];
        else val = ~0ULL;
        break;
    case XEON_SPAD_OFFSET ... XEON_SPAD_OFFSET + XEON_SPAD_COUNT * 4 - 1:
        if (size == 4) {
            uint32_t idx = (addr - XEON_SPAD_OFFSET) >> 2;
            if (idx < XEON_SPAD_COUNT)
                val = s->regs.spad[idx];
            else
                val = ~0ULL;
        } else {
            val = ~0ULL;
        }
        break;
    case XEON_B2B_SPAD_OFFSET ... XEON_B2B_SPAD_OFFSET + XEON_SPAD_COUNT * 4 - 1:
        if (size == 4) {
            uint32_t idx = (addr - XEON_B2B_SPAD_OFFSET) >> 2;
            if (idx < XEON_SPAD_COUNT)
                val = s->regs.b2b_spad[idx];
            else
                val = ~0ULL;
        } else {
            val = ~0ULL;
        }
        break;
    case XEON_B2B_DOORBELL_OFFSET:
        if (size == 2) val = s->regs.b2b_db;
        else val = ~0ULL;
        break;
    case XEON_SBAR0BASE_OFFSET:
        if (size == 8) val = s->regs.sbar0_base;
        else val = ~0ULL;
        break;
    case XEON_SBAR23LMT_OFFSET:
        if (size == 8) val = s->regs.sbar2_limit;
        else val = ~0ULL;
        break;
    case XEON_SBAR23XLAT_OFFSET:
        if (size == 8) val = s->regs.sbar2_xlat;
        else val = ~0ULL;
        break;
    case XEON_SBAR4LMT_OFFSET:
        if (size == 8) val = s->regs.sbar4_limit;
        else val = ~0ULL;
        break;
    case XEON_SBAR4XLAT_OFFSET:
        if (size == 8) val = s->regs.sbar4_xlat;
        else val = ~0ULL;
        break;
    case XEON_SBAR5LMT_OFFSET:
        if (size == 8) val = s->regs.sbar5_limit;
        else val = ~0ULL;
        break;
    case XEON_SBAR5XLAT_OFFSET:
        if (size == 8) val = s->regs.sbar5_xlat;
        else val = ~0ULL;
        break;
    case XEON_PBAR23LMT_OFFSET:
        if (size == 8) val = s->regs.pbar2_limit;
        else val = ~0ULL;
        break;
    case XEON_PBAR23XLAT_OFFSET:
        if (size == 8) val = s->regs.pbar2_xlat;
        else val = ~0ULL;
        break;
    case XEON_PBAR45LMT_OFFSET:
        if (size == 8) val = s->regs.pbar4_limit;
        else val = ~0ULL;
        break;
    case XEON_PBAR45XLAT_OFFSET:
        if (size == 8) val = s->regs.pbar4_xlat;
        else val = ~0ULL;
        break;
    case XEON_PBAR5LMT_OFFSET:
        if (size == 8) val = s->regs.pbar5_limit;
        else val = ~0ULL;
        break;
    case XEON_PBAR5XLAT_OFFSET:
        if (size == 8) val = s->regs.pbar5_xlat;
        else val = ~0ULL;
        break;
    default:
        val = ~0ULL;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case XEON_NTBCNTL_OFFSET:
        if (size == 4) s->regs.ntb_ctl = val;
        break;
    case XEON_PDOORBELL_OFFSET:
        if (size == 2) {
            s->regs.db_bell[0] &= ~((uint16_t)val); /* W1C */
            pcibase_update_irq(s);
        }
        break;
    case XEON_PDBMSK_OFFSET:
        if (size == 2) {
            s->regs.db_mask[0] = val;
            pcibase_update_irq(s);
        }
        break;
    case XEON_SDOORBELL_OFFSET:
        if (size == 2) s->regs.db_bell[1] &= ~((uint16_t)val);
        break;
    case XEON_SDBMSK_OFFSET:
        if (size == 2) s->regs.db_mask[1] = val;
        break;
    case XEON_SPAD_OFFSET ... XEON_SPAD_OFFSET + XEON_SPAD_COUNT * 4 - 1:
        if (size == 4) {
            uint32_t idx = (addr - XEON_SPAD_OFFSET) >> 2;
            if (idx < XEON_SPAD_COUNT)
                s->regs.spad[idx] = val;
        }
        break;
    case XEON_B2B_SPAD_OFFSET ... XEON_B2B_SPAD_OFFSET + XEON_SPAD_COUNT * 4 - 1:
        if (size == 4) {
            uint32_t idx = (addr - XEON_B2B_SPAD_OFFSET) >> 2;
            if (idx < XEON_SPAD_COUNT)
                s->regs.b2b_spad[idx] = val;
        }
        break;
    case XEON_B2B_DOORBELL_OFFSET:
        if (size == 2) s->regs.b2b_db &= ~((uint16_t)val);
        break;
    case XEON_SBAR0BASE_OFFSET:
        if (size == 8) s->regs.sbar0_base = val;
        else if (size == 4) s->regs.sbar0_base = (s->regs.sbar0_base & ~0xffffffffULL) | val;
        break;
    case XEON_SBAR23LMT_OFFSET:
        if (size == 8) s->regs.sbar2_limit = val;
        else if (size == 4) s->regs.sbar2_limit = (s->regs.sbar2_limit & ~0xffffffffULL) | val;
        break;
    case XEON_SBAR23XLAT_OFFSET:
        if (size == 8) s->regs.sbar2_xlat = val;
        else if (size == 4) s->regs.sbar2_xlat = (s->regs.sbar2_xlat & ~0xffffffffULL) | val;
        break;
    case XEON_SBAR4LMT_OFFSET:
        if (size == 8) s->regs.sbar4_limit = val;
        else if (size == 4) s->regs.sbar4_limit = (s->regs.sbar4_limit & ~0xffffffffULL) | val;
        break;
    case XEON_SBAR4XLAT_OFFSET:
        if (size == 8) s->regs.sbar4_xlat = val;
        else if (size == 4) s->regs.sbar4_xlat = (s->regs.sbar4_xlat & ~0xffffffffULL) | val;
        break;
    case XEON_SBAR5LMT_OFFSET:
        if (size == 8) s->regs.sbar5_limit = val;
        else if (size == 4) s->regs.sbar5_limit = (s->regs.sbar5_limit & ~0xffffffffULL) | val;
        break;
    case XEON_SBAR5XLAT_OFFSET:
        if (size == 8) s->regs.sbar5_xlat = val;
        else if (size == 4) s->regs.sbar5_xlat = (s->regs.sbar5_xlat & ~0xffffffffULL) | val;
        break;
    case XEON_PBAR23LMT_OFFSET:
        if (size == 8) s->regs.pbar2_limit = val;
        else if (size == 4) s->regs.pbar2_limit = (s->regs.pbar2_limit & ~0xffffffffULL) | val;
        break;
    case XEON_PBAR23XLAT_OFFSET:
        if (size == 8) s->regs.pbar2_xlat = val;
        else if (size == 4) s->regs.pbar2_xlat = (s->regs.pbar2_xlat & ~0xffffffffULL) | val;
        break;
    case XEON_PBAR45LMT_OFFSET:
        if (size == 8) s->regs.pbar4_limit = val;
        else if (size == 4) s->regs.pbar4_limit = (s->regs.pbar4_limit & ~0xffffffffULL) | val;
        break;
    case XEON_PBAR45XLAT_OFFSET:
        if (size == 8) s->regs.pbar4_xlat = val;
        else if (size == 4) s->regs.pbar4_xlat = (s->regs.pbar4_xlat & ~0xffffffffULL) | val;
        break;
    case XEON_PBAR5LMT_OFFSET:
        if (size == 8) s->regs.pbar5_limit = val;
        else if (size == 4) s->regs.pbar5_limit = (s->regs.pbar5_limit & ~0xffffffffULL) | val;
        break;
    case XEON_PBAR5XLAT_OFFSET:
        if (size == 8) s->regs.pbar5_xlat = val;
        else if (size == 4) s->regs.pbar5_xlat = (s->regs.pbar5_xlat & ~0xffffffffULL) | val;
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

    /* Set initial register values to emulate a primary-side device with link up */
    s->regs.ntb_ctl = 0;  /* disabled initially */
    s->regs.ppd = XEON_PPD_TOPO_PRI_USD;
    s->regs.link_status = 0x2022; /* Active, Gen2, x8 */
    s->regs.sbar0_base = 0;
    s->regs.sbar2_limit = 0;
    s->regs.sbar2_xlat = 0;
    s->regs.sbar4_limit = 0;
    s->regs.sbar4_xlat = 0;
    s->regs.sbar5_limit = 0;
    s->regs.sbar5_xlat = 0;
    s->regs.pbar2_limit = 0;
    s->regs.pbar2_xlat = 0;
    s->regs.pbar4_limit = 0;
    s->regs.pbar4_xlat = 0;
    s->regs.pbar5_limit = 0;
    s->regs.pbar5_xlat = 0;
    s->regs.db_bell[0] = 0;
    s->regs.db_bell[1] = 0;
    s->regs.db_mask[0] = BIT(15);  /* mask all except link bit? Set full mask later, but init to valid mask */
    s->regs.db_mask[1] = 0;
    memset(s->regs.spad, 0, sizeof(s->regs.spad));
    memset(s->regs.b2b_spad, 0, sizeof(s->regs.b2b_spad));
    s->regs.b2b_db = 0;
    s->regs.devsts = 0;
    s->regs.uncerrsts = 0;
    s->regs.corerrsts = 0;

    s->status = 0;
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

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    switch (addr) {
    case XEON_PPD_OFFSET:
        if (len == 1) return s->regs.ppd;
        break;
    case XEON_LINK_STATUS_OFFSET:
        if (len == 2) return s->regs.link_status;
        break;
    default:
        break;
    }
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    switch (addr) {
    case XEON_PPD_OFFSET:
        if (len == 1) s->regs.ppd = val;
        return;
    case XEON_LINK_STATUS_OFFSET:
        if (len == 2) s->regs.link_status = val;
        return;
    default:
        break;
    }
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_NTB_B2B_JSF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_NTB);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Override config read/write to handle special registers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* BAR Configuration */
    memset(s->bar_info, 0, sizeof(s->bar_info));
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "ntb-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "msix-table" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM, .size = 0x100000, .name = "bar2" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_NONE };  /* part of 64-bit BAR2 */
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_RAM, .size = 0x100000, .name = "bar4" };
    s->bar_info[5] = (BARInfo){ .index = 5, .type = BAR_TYPE_NONE };  /* part of 64-bit BAR4 */

    s->num_bars = 6;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    if (msix_init(pdev, XEON_DB_MSIX_VECTOR_COUNT,
                  &s->bar_regions[1], 1, 0,
                  &s->bar_regions[1], 1, 0x200,
                  0, errp)) {
        return;
    }
    msix_set_vector_notifiers(pdev, NULL, NULL, NULL);

    /* Initial state setup */
    pcibase_reset(DEVICE(pdev));
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
    .name = "ntb_hw_intel_pci",
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
