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


#define TYPE_PCIBASE_DEVICE "mt7615e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define MT_PDMA_SLP_PROT 0x154
#define MT_PDMA_BUSY_STATUS 0x168
#define MT_PCIE_IRQ_ENABLE 0x188
#define MT_PSE_PG_INFO 0x194
#define MT_MCU2HOST_INT_ENABLE 0x1f4
#define MT_WPDMA_TX_RING0_CTRL0 0x300
#define MT_WPDMA_TX_RING0_CTRL1 0x304
#define MT_WPDMA_GLO_CFG1 0x500
#define MT_WPDMA_TX_PRE_CFG 0x510
#define MT_WPDMA_RX_PRE_CFG 0x520
#define MT_WPDMA_ABT_CFG 0x530
#define MT_WPDMA_ABT_CFG1 0x534
#define MT_HIF2_BASE 0xf0000
#define MT_MCU_CIRQ_BASE 0xc0000
#define MT_DMASHDL_BASE 0x5000a000
#define MT_MCU_PTA_BASE 0x81060000

#define MT_HW_CHIPID 0x70010200
#define MT_HW_REV 0x70010204
#define MT_INT_MASK_CSR 0x0204

#define PCI_VENDOR_ID_MEDIATEK 0x14c3

#define MT_PDMA_AXI_SLPPROT_ENABLE (1 << 0)
#define MT_PDMA_AXI_SLPPROT_RDY    (1 << 16)

struct mt76_queue_regs {
    uint32_t desc_base;
    uint32_t ring_size;
    uint32_t cpu_idx;
    uint32_t dma_idx;
};

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
    uint32_t pcie_irq_enable;
    uint32_t mcu2host_int_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t pdma_slp_prot;
    uint32_t pdma_busy_status;
    uint32_t pse_pg_info;
    uint32_t wpdma_glo_cfg1;
    uint32_t wpdma_tx_pre_cfg;
    uint32_t wpdma_rx_pre_cfg;
    uint32_t wpdma_abt_cfg;
    uint32_t wpdma_abt_cfg1;
    uint32_t hw_chipid;
    uint32_t hw_rev;
    uint32_t int_mask_csr;

    /* DMA Context */
    struct mt76_queue_regs tx_ring0;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MT_PDMA_SLP_PROT:
        val = s->pdma_slp_prot;
        break;
    case MT_PDMA_BUSY_STATUS:
        val = s->pdma_busy_status;
        break;
    case MT_PCIE_IRQ_ENABLE:
        val = s->pcie_irq_enable;
        break;
    case MT_PSE_PG_INFO:
        val = s->pse_pg_info;
        break;
    case MT_MCU2HOST_INT_ENABLE:
        val = s->mcu2host_int_enable;
        break;
    case MT_WPDMA_TX_RING0_CTRL0:
        val = s->tx_ring0.desc_base;
        break;
    case MT_WPDMA_TX_RING0_CTRL1:
        val = s->tx_ring0.ring_size;
        break;
    case MT_WPDMA_GLO_CFG1:
        val = s->wpdma_glo_cfg1;
        break;
    case MT_WPDMA_TX_PRE_CFG:
        val = s->wpdma_tx_pre_cfg;
        break;
    case MT_WPDMA_RX_PRE_CFG:
        val = s->wpdma_rx_pre_cfg;
        break;
    case MT_WPDMA_ABT_CFG:
        val = s->wpdma_abt_cfg;
        break;
    case MT_WPDMA_ABT_CFG1:
        val = s->wpdma_abt_cfg1;
        break;
    case MT_HW_CHIPID:
        val = s->hw_chipid;
        break;
    case MT_HW_REV:
        val = s->hw_rev;
        break;
    case MT_INT_MASK_CSR:
        val = s->int_mask_csr;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MT_PDMA_SLP_PROT:
        s->pdma_slp_prot = val;
        if (val & MT_PDMA_AXI_SLPPROT_ENABLE) {
            s->pdma_slp_prot |= MT_PDMA_AXI_SLPPROT_RDY;
        } else {
            s->pdma_slp_prot &= ~MT_PDMA_AXI_SLPPROT_RDY;
        }
        break;
    case MT_PCIE_IRQ_ENABLE:
        s->pcie_irq_enable = val;
        break;
    case MT_PSE_PG_INFO:
        s->pse_pg_info = val;
        break;
    case MT_MCU2HOST_INT_ENABLE:
        s->mcu2host_int_enable = val;
        break;
    case MT_WPDMA_TX_RING0_CTRL0:
        s->tx_ring0.desc_base = val;
        break;
    case MT_WPDMA_TX_RING0_CTRL1:
        s->tx_ring0.ring_size = val;
        break;
    case MT_WPDMA_GLO_CFG1:
        s->wpdma_glo_cfg1 = val;
        break;
    case MT_WPDMA_TX_PRE_CFG:
        s->wpdma_tx_pre_cfg = val;
        break;
    case MT_WPDMA_RX_PRE_CFG:
        s->wpdma_rx_pre_cfg = val;
        break;
    case MT_WPDMA_ABT_CFG:
        s->wpdma_abt_cfg = val;
        break;
    case MT_WPDMA_ABT_CFG1:
        s->wpdma_abt_cfg1 = val;
        break;
    case MT_INT_MASK_CSR:
        s->int_mask_csr = val;
        break;
    default:
        break;
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

    s->hw_chipid = 0x7615;
    s->hw_rev = 0;
    s->pdma_slp_prot = 0;
    s->pdma_busy_status = 0;
    s->pcie_irq_enable = 0;
    s->mcu2host_int_enable = 0;
    s->wpdma_glo_cfg1 = 0;
    s->wpdma_tx_pre_cfg = 0;
    s->wpdma_rx_pre_cfg = 0;
    s->wpdma_abt_cfg = 0;
    s->wpdma_abt_cfg1 = 0;
    s->int_mask_csr = 0;
    s->tx_ring0.desc_base = 0;
    s->tx_ring0.ring_size = 0;
    s->tx_ring0.cpu_idx = 0;
    s->tx_ring0.dma_idx = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE || bi->size == 0) {
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MEDIATEK );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x7615 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].name = "mt7615-bar0";
    s->bar_info[0].size = 0x100000; /* 1MB */
      
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
    .name = "mt7615e_pci",
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
