/*
 * QEMU device model for Marvell SATA controller (sata_mv)
 * This is a placeholder implementation; register offsets must be provided.
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

#define TYPE_PCIBASE_DEVICE "sata_mv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor/Device IDs are already defined in pci_ids.h, use them directly */
#define PCI_DEVICE_ID_MV5040 0x5040
#define MV_CLASS 0x0106

/* BAR definitions */
#define MV_PRIMARY_BAR 0

/* Register offsets need to be defined from sata_mv.h */
/* Placeholder values (all zero) - proper offsets must be supplied */
#define EDMA_CFG 0x0
#define EDMA_REQ_Q_BASE_HI 0x0
#define EDMA_REQ_Q_IN_PTR 0x0
#define EDMA_REQ_Q_OUT_PTR 0x0
#define EDMA_RSP_Q_BASE_HI 0x0
#define EDMA_RSP_Q_IN_PTR 0x0
#define EDMA_RSP_Q_OUT_PTR 0x0
#define EDMA_CMD 0x0
#define EDMA_STATUS 0x0
#define EDMA_ERR_IRQ_CAUSE 0x0
#define EDMA_ERR_IRQ_MASK 0x0
#define FISCFG 0x0
#define LTMODE 0x0
#define EDMA_HALTCOND 0x0
#define EDMA_UNKNOWN_RSVD 0x0
#define BMDMA_CMD 0x0
#define BMDMA_PRD_HIGH 0x0
#define BMDMA_PRD_LOW 0x0
#define BMDMA_STATUS 0x0
#define SATA_STATUS 0x0
#define SATA_ACTIVE 0x0
#define SATA_IFCTL 0x0
#define VENDOR_UNIQUE_FIS 0x0
#define SATA_IFSTAT 0x0
#define SATA_IFCFG 0x0
#define SATA_TESTCTL 0x0
#define LP_PHY_CTL 0x0
#define PHY_MODE2 0x0
#define PHY_MODE3 0x0
#define PHY_MODE4 0x0
#define PHY_MODE9_GEN2 0x0
#define PHY_MODE9_GEN1 0x0
#define PHYCFG_OFS 0x0
#define HC_CFG 0x0
#define HC_IRQ_CAUSE 0x0
#define HC_IRQ_COAL_TIME_THRESHOLD 0x0
#define HC_IRQ_COAL_IO_THRESHOLD 0x0
#define IRQ_COAL_TIME_THRESHOLD 0x0
#define IRQ_COAL_IO_THRESHOLD 0x0
#define IRQ_COAL_CAUSE 0x0
#define SOC_LED_CTRL 0x0
#define GPIO_PORT_CTL 0x0
#define FLASH_CTL 0x0
#define MV_PCI_EXP_ROM_BAR_CTL 0x0
#define MV_PCI_MODE 0x0
#define MV_PCI_DISC_TIMER 0x0
#define MV_PCI_MSI_TRIGGER 0x0
#define MV_PCI_XBAR_TMOUT 0x0
#define MV_PCI_SERR_MASK 0x0
#define MV_PCI_ERR_LOW_ADDRESS 0x0
#define MV_PCI_ERR_HIGH_ADDRESS 0x0
#define MV_PCI_ERR_ATTRIBUTE 0x0
#define MV_PCI_ERR_COMMAND 0x0
#define RESET_CFG 0x0
#define WINDOW_CTRL(i) (0x20030 + ((i) << 4))
#define WINDOW_BASE(i) (0x20034 + ((i) << 4))
#define MV5_PHY_MODE 0x0
#define MV5_LTMODE 0x0
#define MV5_PHY_CTL 0x0

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

    /* Flat register file for the entire MMIO space (temporary) */
    uint8_t regs[0x10000];

    /* DMA context */
    dma_addr_t dma_base;
    size_t dma_size;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > 0x10000) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds read at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return ~0ULL;
    }

    /* Simple flat register file, all reads return current stored value */
    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        val = lduw_le_p(&s->regs[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->regs[addr]);
        break;
    case 8:
        val = ldq_le_p(&s->regs[addr]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > 0x10000) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds write at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return;
    }

    /* Simple flat register file, store value */
    switch (size) {
    case 1:
        s->regs[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(&s->regs[addr], (uint16_t)val);
        break;
    case 4:
        stl_le_p(&s->regs[addr], (uint32_t)val);
        break;
    case 8:
        stq_le_p(&s->regs[addr], val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
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
    /* Reset all registers to zero */
    memset(s->regs, 0, sizeof(s->regs));
    /* Set known reset values for critical registers (offsets TBD) */
    /* EDMA_CMD: clear, EDMA_STATUS: cache empty and idle? */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MARVELL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_MV5040);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, MV_CLASS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Configure BAR0: MMIO, 64KB */
    s->num_bars = 1;
    s->bar_info[0].index = MV_PRIMARY_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI capable */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        /* fallback to INTx */
    }
    s->has_msi = msi_enabled(pdev);
    s->has_msix = false;

    /* Initialize registers to reset state */
    memset(s->regs, 0, sizeof(s->regs));
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
    .name = "sata_mv_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(regs, PCIBaseState, 0x10000),
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
