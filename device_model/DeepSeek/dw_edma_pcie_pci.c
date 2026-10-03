/* 
 * QEMU model of Synopsys DesignWare eDMA PCIe controller
 * Based on dw-edma-pcie.c Linux driver.
 * QEMU 8.2.10
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

#define TYPE_PCIBASE_DEVICE "dw_edma_pcie_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x16C3
#define DEVICE_ID 0xedda
#define CLASS_ID 0x0000

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
};

typedef enum pci_barno { BAR_0 = 0, BAR_1 = 1, BAR_2 = 2, BAR_3 = 3, BAR_4 = 4, BAR_5 = 5 } pci_barno;

enum dw_edma_dir {
    EDMA_DIR_WRITE = 0,
    EDMA_DIR_READ
};
enum dw_edma_request {
    EDMA_REQ_NONE = 0,
    EDMA_REQ_STOP,
    EDMA_REQ_PAUSE
};
enum dw_edma_status {
    EDMA_ST_IDLE = 0,
    EDMA_ST_PAUSE,
    EDMA_ST_BUSY
};
enum dw_edma_xfer_type {
    EDMA_XFER_SCATTER_GATHER = 0,
    EDMA_XFER_CYCLIC,
    EDMA_XFER_INTERLEAVED
};

enum dw_edma_map_format {
    EDMA_MF_EDMA_UNROLL = 0
};

struct dw_edma_block {
    enum pci_barno          bar;
    off_t                   off;
    size_t                  sz;
};

#define EDMA_MAX_WR_CH 2
#define EDMA_MAX_RD_CH 2

static const struct dw_edma_pcie_data {
    struct dw_edma_block       rg;
    struct dw_edma_block       ll_wr[EDMA_MAX_WR_CH];
    struct dw_edma_block       ll_rd[EDMA_MAX_RD_CH];
    struct dw_edma_block       dt_wr[EDMA_MAX_WR_CH];
    struct dw_edma_block       dt_rd[EDMA_MAX_RD_CH];
    enum dw_edma_map_format    mf;
    uint8_t                     irqs;
    uint16_t                    wr_ch_cnt;
    uint16_t                    rd_ch_cnt;
    uint64_t                    devmem_phys_off;
} __attribute__((unused)) snps_edda_data = {
    .rg.bar                     = BAR_0,
    .rg.off                     = 0x00001000,
    .rg.sz                      = 0x00002000,
    .ll_wr = {
        { BAR_2, 0x00000000, 0x00000800 },
        { BAR_2, 0x00200000, 0x00000800 },
    },
    .ll_rd = {
        { BAR_2, 0x00400000, 0x00000800 },
        { BAR_2, 0x00600000, 0x00000800 },
    },
    .dt_wr = {
        { BAR_2, 0x00800000, 0x00000800 },
        { BAR_2, 0x00900000, 0x00000800 },
    },
    .dt_rd = {
        { BAR_2, 0x00a00000, 0x00000800 },
        { BAR_2, 0x00b00000, 0x00000800 },
    },
    .mf                         = 0, /* EDMA_MF_EDMA_UNROLL */
    .irqs                       = 1,
    .wr_ch_cnt                  = 2,
    .rd_ch_cnt                  = 2,
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    qemu_log_mask(LOG_UNIMP, "%s: MMIO read at 0x%"HWADDR_PRIx" size %u\n",
                  __func__, addr, size);
    /* FIXME: Real register emulation requires dw-edma core register map */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: MMIO write at 0x%"HWADDR_PRIx" size %u val 0x%"PRIx64"\n",
                  __func__, addr, size, val);
    /* FIXME: Real register emulation requires dw-edma core register map */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    qemu_log_mask(LOG_UNIMP, "%s: PIO read at 0x%"HWADDR_PRIx" size %u\n",
                  __func__, addr, size);
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: PIO write at 0x%"HWADDR_PRIx" size %u val 0x%"PRIx64"\n",
                  __func__, addr, size, val);
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

    s->intr_status = 0;
    s->intr_mask = 0;
    /* Note: Full register reset requires core register knowledge */
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

    /* Initialize MSI */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_NONE, .size = 0, .name = "bar1" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM, .size = 0x1000000, .name = "bar2" };
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
    .name = "dw_edma_pcie_pci",
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
