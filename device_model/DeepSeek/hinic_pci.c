/*
 * QEMU model for Huawei HiNIC PCIe Ethernet controller
 * Based on driver hinic_main.c, QEMU 8.2.10
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

#define TYPE_PCIBASE_DEVICE "hinic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_HUAWEI 0x19e5
#define HINIC_DEV_ID_QUAD_PORT_25GE 0x1822
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

#define HINIC_PCI_CFG_REGS_BAR    0
#define HINIC_PCI_INTR_REGS_BAR   2
#define HINIC_PCI_DB_BAR          4
#define HINIC_DB_SIZE             (4 * 1024 * 1024) /* 4 MiB */

#define HINIC_CFG_REGS_BAR_SIZE   (0x1000)     /* 4 KiB */
#define HINIC_INTR_REGS_BAR_SIZE  (0x1000)     /* 4 KiB */
#define HINIC_MAX_MSIX_VECTORS    32

/* Mailbox register offsets (from BAR0) */
#define HINIC_FUNC_CSR_MAILBOX_DATA_OFF        0x80
#define HINIC_FUNC_CSR_MAILBOX_CONTROL_OFF     0x0100
#define HINIC_FUNC_CSR_MAILBOX_INT_OFFSET_OFF  0x0104
#define HINIC_FUNC_CSR_MAILBOX_RESULT_H_OFF    0x0108
#define HINIC_FUNC_CSR_MAILBOX_RESULT_L_OFF    0x010C

/* Additional register offsets */
#define HINIC_CSR_FUNC_ATTR4_ADDR               0x10

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

    /* BAR0 configuration registers shadow */
    uint32_t cfg_regs[HINIC_CFG_REGS_BAR_SIZE / 4];

    /* No DMA state; not explicitly required by provided driver source */
};

/* BAR0 MMIO read/write (configuration registers) */
static uint64_t pcibase_cfg_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < HINIC_CFG_REGS_BAR_SIZE && size == 4) {
        val = s->cfg_regs[addr >> 2];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read at 0x%" HWADDR_PRIx ", size %d\n",
                      __func__, addr, size);
    }
    return val;
}

static void pcibase_cfg_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < HINIC_CFG_REGS_BAR_SIZE && size == 4) {
        s->cfg_regs[addr >> 2] = val;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write at 0x%" HWADDR_PRIx ", size %d\n",
                      __func__, addr, size);
    }
}

static const MemoryRegionOps pcibase_cfg_ops = {
    .read = pcibase_cfg_read,
    .write = pcibase_cfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* Generic MMIO/PIO handlers (unused for now) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
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
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->cfg_regs, 0, sizeof(s->cfg_regs));
    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi,
                                 Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == HINIC_PCI_CFG_REGS_BAR) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_cfg_ops, s,
                                  bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                                  bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_HUAWEI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, HINIC_DEV_ID_QUAD_PORT_25GE);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF,
                                    errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){
        .index = HINIC_PCI_CFG_REGS_BAR,
        .type = BAR_TYPE_MMIO,
        .size = HINIC_CFG_REGS_BAR_SIZE,
        .name = "hinic-cfg"
    };
    s->bar_info[1] = (BARInfo){
        .index = HINIC_PCI_INTR_REGS_BAR,
        .type = BAR_TYPE_RAM,   /* MSI-X table lives here */
        .size = HINIC_INTR_REGS_BAR_SIZE,
        .name = "hinic-intr"
    };
    s->bar_info[2] = (BARInfo){
        .index = HINIC_PCI_DB_BAR,
        .type = BAR_TYPE_RAM,   /* doorbell region */
        .size = HINIC_DB_SIZE,
        .name = "hinic-db"
    };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (*errp) {
            return;
        }
    }

    /* Add MSI-X capability before initializing */
    int msix_cap = pci_add_capability(pdev, PCI_CAP_ID_MSIX, 0,
                                      PCI_MSIX_SIZEOF, errp);
    if (msix_cap < 0) {
        return;
    }

    if (msix_init(pdev, HINIC_MAX_MSIX_VECTORS,
                  &s->bar_regions[HINIC_PCI_INTR_REGS_BAR],
                  HINIC_PCI_INTR_REGS_BAR, 0,
                  &s->bar_regions[HINIC_PCI_INTR_REGS_BAR],
                  HINIC_PCI_INTR_REGS_BAR, 0x200,
                  msix_cap, errp)) {
        error_setg(errp, "Failed to init MSI-X");
        return;
    }

    memset(s->cfg_regs, 0, sizeof(s->cfg_regs));
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
    .name = "hinic_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(cfg_regs, PCIBaseState,
                             HINIC_CFG_REGS_BAR_SIZE / 4),
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
