
/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Register-level modeling for skge Ethernet controller.
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
#include "hw/pci/pci_ids.h" /* for PCI_VENDOR_ID_3COM */

/* Vendor ID macro missing from pci_ids.h */
#define PCI_VENDOR_ID_3COM 0x10b7

/* Register offset defines (corrected for actual SKGE memory map) */
#define B0_CTST       0x0000
#define B0_LED        0x0006
#define B0_POWER_CTRL 0x0002
#define B0_IMSK       0x000c
#define B0_SP_ISRC    0x0004
#define B0_ISRC       0x0010
#define B0_HWE_ISRC   0x0014
#define B0_HWE_IMSK   0x0018
#define B0_R1_CSR     0x0070
#define B0_R2_CSR     0x0074

#define B2_CHIP_ID    0x0208
#define B2_E_1        0x0209
#define B2_PMD_TYP    0x020A
#define B2_MAC_CFG    0x020B
#define B2_E_0        0x020C
#define B2_GP_IO      0x020D
#define B2_BSC_INI    0x0210
#define B2_BSC_CTRL   0x0211
#define B2_IRQM_CTRL  0x0212
#define B2_IRQM_MSK   0x0213
#define B2_IRQM_INI   0x0214
#define B2_TI_CTRL    0x0215
#define B2_TST_CTRL1  0x0216
#define B2_TST_CTRL2  0x0217

#define B3_MA_TO_CTRL 0x0318
#define B3_MA_TOINI_RX1 0x0319
#define B3_MA_TOINI_RX2 0x031A
#define B3_MA_TOINI_TX1 0x031B
#define B3_MA_TOINI_TX2 0x031C
#define B3_MA_RCINI_RX1 0x031D
#define B3_MA_RCINI_RX2 0x031E
#define B3_MA_RCINI_TX1 0x031F
#define B3_MA_RCINI_TX2 0x0320
#define B3_PA_CTRL    0x0321
#define B3_PA_TOINI_RX1 0x0322
#define B3_PA_TOINI_TX1 0x0323
#define B3_PA_TOINI_RX2 0x0324
#define B3_PA_TOINI_TX2 0x0325
#define B3_RI_CTRL    0x0326
#define B3_RI_WTO_R1  0x0327
#define B3_RI_WTO_XA1 0x0328
#define B3_RI_WTO_XS1 0x0329
#define B3_RI_RTO_R1  0x032A
#define B3_RI_RTO_XA1 0x032B
#define B3_RI_RTO_XS1 0x032C
#define B3_RI_WTO_R2  0x032D
#define B3_RI_WTO_XA2 0x032E
#define B3_RI_WTO_XS2 0x032F
#define B3_RI_RTO_R2  0x0330
#define B3_RI_RTO_XA2 0x0331
#define B3_RI_RTO_XS2 0x0332
#define B3_RAM_ADDR   0x0333

#define Q_R1 0
#define Q_R2 1
#define Q_XA1 2
#define Q_XA2 3
#define Q_ADDR(q, reg) (0x1000 + (q) * 0x200 + (reg))
#define RB_ADDR(q, reg) (0x2000 + (q) * 0x100 + (reg))

#define CSR_START    (1L<<4)
#define CSR_STOP     2
#define CSR_IRQ_CL_F (1L<<1)
#define CSR_CLR_RESET 0 /* placeholder: sub-defines unknown */
#define CSR_SET_RESET 0 /* placeholder */
#define CSR_IRQ_CL_P (1L<<3)

#define RB_RST_CLR 0
#define RB_ENA_STFWD 1
#define RB_ENA_OP_MD 2
#define RB_DIS_OP_MD 4
#define RB_RST_SET 8

#define SK_REG(port, reg) (0x3000 + (port) * 0x200 + (reg))
#define SK_GMAC_REG(port, reg) SK_REG(port, reg)
#define SK_XMAC_REG(port, reg) SK_REG(port, reg)

#define B2_MAC_1 (0x0101)
#define B2_MAC_2 (0x0102)

#define PCI_DEV_REG2 0x44
#define PCI_VPD_ROM_SZ 0x00007000

#define IS_R1_F     (1L<<13)
#define IS_R2_F     (1L<<9)

#define TYPE_PCIBASE_DEVICE "skge_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define VENDOR_ID PCI_VENDOR_ID_3COM
#define DEVICE_ID 0x1700
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

#define BAR0_SIZE 0x4000

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

/* Hardware Register Shadows */
#define REG_SHADOW_SIZE BAR0_SIZE

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

    /* Register Flat Shadow */
    uint8_t regs[REG_SHADOW_SIZE];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->regs)) {
        return ~0ULL;
    }

    /* Handle special registers with side effects */
    if (addr == B0_SP_ISRC) {
        val = s->intr_status;
        /* Clear interrupt status on read */
        s->intr_status = 0;
        pcibase_update_irq(s);
        return val;
    }

    /* General read from shadow array */
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
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read size %u\n", __func__, size);
        return 0;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        return;
    }

    /* Handle special registers */
    if (addr == B0_IMSK) {
        s->intr_mask = val;
        pcibase_update_irq(s);
        /* Also store in regs array for consistency */
        stl_le_p(&s->regs[addr], val);
        return;
    }

    /* General write to shadow array */
    switch (size) {
    case 1:
        s->regs[addr] = val;
        break;
    case 2:
        stw_le_p(&s->regs[addr], val);
        break;
    case 4:
        stl_le_p(&s->regs[addr], val);
        break;
    case 8:
        stq_le_p(&s->regs[addr], val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write size %u\n", __func__, size);
        return;
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

    /* Reset device-specific state */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;

    /* Set chip identification values to defaults */
    s->regs[B2_CHIP_ID] = 0xB0; /* CHIP_ID_YUKON */
    s->regs[B2_MAC_CFG] = (1 << 4) | 2; /* chip_rev 1, dual-port (2 ports) */
    s->regs[B2_PMD_TYP] = 'T'; /* copper */
    s->regs[B2_E_0] = 8; /* 8 * 4KB = 32KB RAM */
    s->regs[B2_E_1] = 0; /* phy_type = SK_PHY_MARV_COPPER */
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

    /* Set up VPD-related vendor-specific register (placeholder; actual VPD capability not fully implemented) */
    uint32_t reg2 = 0x4000; /* bit 14 set (enables VPD ROM of some size?) */
    pci_set_long(pci_conf + PCI_DEV_REG2, reg2);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "skge-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize chip identification registers to known values (same as reset) */
    s->regs[B2_CHIP_ID] = 0xB0; /* CHIP_ID_YUKON */
    s->regs[B2_MAC_CFG] = (1 << 4) | 2; /* chip_rev 1, dual-port (2 ports) */
    s->regs[B2_PMD_TYP] = 'T'; /* copper */
    s->regs[B2_E_0] = 8; /* 8 * 4KB = 32KB RAM */
    s->regs[B2_E_1] = 0; /* phy_type = SK_PHY_MARV_COPPER */
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
    .name = "skge_pci",
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
