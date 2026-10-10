/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and PCIe driver probing.
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

#define TYPE_PCIBASE_DEVICE "dwmac_loongson_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_LOONGSON 0x0014
#define PCI_DEVICE_ID_LOONGSON_GMAC1 0x7a03

#define DRIVER_NAME "dwmac-loongson-pci"

#define MAC_CTRL_REG 0x00000000
#define GMAC_MII_ADDR 0x00000010
#define GMAC_MII_DATA 0x00000014
#define GMAC_VERSION 0x00000020
#define DMA_BUS_MODE 0x00001000
#define DMA_STATUS 0x00001014
#define DMA_INTR_ENA 0x0000101c
#define DMA_CHAN_BASE_OFFSET 0x100

#define DWMAC_CORE_MULTICHAN_V1 0x10
#define DWMAC_CORE_MULTICHAN_V2 0x12
#define DMA_BUS_MODE_SFT_RESET 0x00000001
#define DMA_STATUS_NIS_TX_LOONGSON 0x00040000
#define DMA_STATUS_NIS_RX_LOONGSON 0x00020000
#define DMA_STATUS_AIS_TX_LOONGSON 0x00010000
#define DMA_STATUS_AIS_RX_LOONGSON 0x00008000
#define DMA_STATUS_FBI_TX_LOONGSON 0x00002000
#define DMA_STATUS_FBI_RX_LOONGSON 0x00001000
#define DMA_STATUS_UNF 0x00000020
#define DMA_STATUS_TJT 0x00000008
#define DMA_STATUS_OVF 0x00000010
#define DMA_STATUS_RU 0x00000080
#define DMA_STATUS_RPS 0x00000100
#define DMA_STATUS_RWT 0x00000200
#define DMA_STATUS_ETI 0x00000400
#define DMA_STATUS_TPS 0x00000002
#define DMA_STATUS_RI 0x00000040
#define DMA_STATUS_TI 0x00000001
#define DMA_STATUS_ERI 0x00004000
#define DMA_STATUS_GPI 0x10000000
#define DMA_STATUS_GMI 0x08000000
#define DMA_STATUS_GLI 0x04000000
#define DMA_INTR_ENA_RIE 0x00000040
#define GMAC_CONTROL_PS 0x00008000

#define DMA_STATUS_TU 0x00000004
#define DMA_STATUS_MSK_COMMON_LOONGSON (DMA_STATUS_NIS_TX_LOONGSON | \
					 DMA_STATUS_NIS_RX_LOONGSON | \
					 DMA_STATUS_AIS_TX_LOONGSON | \
					 DMA_STATUS_AIS_RX_LOONGSON | \
					 DMA_STATUS_FBI_TX_LOONGSON | \
					 DMA_STATUS_FBI_RX_LOONGSON)

#define DMA_INTR_ENA_NIE_TX_LOONGSON	0x00040000
#define DMA_INTR_ENA_NIE_RX_LOONGSON	0x00020000
#define DMA_INTR_ENA_TIE 0x00000001
#define DMA_INTR_ENA_AIE_TX_LOONGSON	0x00010000
#define DMA_INTR_ENA_AIE_RX_LOONGSON	0x00008000
#define DMA_INTR_ENA_FBE 0x00002000
#define DMA_INTR_ENA_UNE 0x00000020

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

    uint32_t mac_ctrl;
    uint32_t gmac_mii_addr;
    uint32_t gmac_mii_data;
    uint32_t gmac_version;
    uint32_t dma_bus_mode;
    uint32_t dma_status;
    uint32_t dma_intr_ena;
};

static inline uint32_t dma_chan_base_addr(uint32_t base, uint32_t chan)
{
    return base + chan * DMA_CHAN_BASE_OFFSET;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MAC_CTRL_REG:
        val = s->mac_ctrl;
        break;
    case GMAC_VERSION:
        val = s->gmac_version;
        break;
    case DMA_BUS_MODE:
        val = s->dma_bus_mode;
        break;
    case DMA_STATUS:
        val = s->dma_status;
        break;
    case DMA_INTR_ENA:
        val = s->dma_intr_ena;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented read at 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MAC_CTRL_REG:
        s->mac_ctrl = val;
        break;
    case DMA_BUS_MODE:
        s->dma_bus_mode = val;
        if (val & DMA_BUS_MODE_SFT_RESET) {
            s->dma_bus_mode &= ~DMA_BUS_MODE_SFT_RESET;
        }
        break;
    case DMA_STATUS:
        s->dma_status &= ~val; /* W1C behavior typical for status */
        break;
    case DMA_INTR_ENA:
        s->dma_intr_ena = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented write at 0x%" HWADDR_PRIx "\n", __func__, addr);
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

    s->mac_ctrl = 0;
    s->gmac_version = DWMAC_CORE_MULTICHAN_V1;
    s->dma_bus_mode = 0;
    s->dma_status = 0;
    s->dma_intr_ena = 0;
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
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_LOONGSON );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_LOONGSON_GMAC1 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = DRIVER_NAME;
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 32, true, false, errp) == 0) {
        s->has_msi = true;
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

type_init(pcibase_register_types);
