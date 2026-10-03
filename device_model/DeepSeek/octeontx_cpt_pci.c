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

#define TYPE_PCIBASE_DEVICE "octeontx_cpt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets from driver */
#define PCI_VENDOR_ID_CAVIUM         0x177d
#define OTX_CPT_PCI_PF_DEVICE_ID     0xa040
#define OTX_CPT_PCI_PF_SUBSYS_ID     0xa340
#define PCI_CLASS_CRYPT_OTHER        0x1080

#define OTX_CPT_PF_CONSTANTS         0x0
#define OTX_CPT_PF_RESET             0x100
#define OTX_CPT_PF_BIST_STATUS       0x160
#define OTX_CPT_PF_EXE_BIST_STATUS   0x4000028
#define OTX_CPT_PF_EXE_CTL           0x4000000
#define OTX_CPT_PF_EXEC_BUSY         0x800
#define OTX_CPT_PF_MBOX_INTX(b)      (0x400 | (uint64_t)(b) << 3)
#define OTX_CPT_PF_MBOX_ENA_W1CX(b)  (0x440 | (uint64_t)(b) << 3)
#define OTX_CPT_PF_MBOX_ENA_W1SX(b)  (0x460 | (uint64_t)(b) << 3)
#define OTX_CPT_PF_GX_EN(b)          (0x600 | (uint64_t)(b) << 3)
#define OTX_CPT_PF_ENGX_UCODE_BASE(b) (0x4002000 | (uint64_t)(b) << 3)
#define OTX_CPT_PF_QX_CTL(b)         (0x8000000 | (uint64_t)(b) << 20)
#define OTX_CPT_PF_VFX_MBOXX(b, c)   (0x8001000 | (uint64_t)(b) << 20 | (uint64_t)(c) << 8)

#define OTX_CPT_MAX_ENGINE_GROUPS    8
#define OTX_CPT_MAX_ENGINES          64
#define OTX_CPT_ENGS_BITMASK_LEN     (OTX_CPT_MAX_ENGINES/(8 * sizeof(unsigned long)))
#define OTX_CPT_PF_MSIX_VECTORS      4
#define OTX_CPT_PF_MBOX_INT          3
#define OTX_CPT_PF_INT_VEC_E_MBOXX(x, a) ((x) + (a))

#define OTX_CPT_PF_PCI_CFG_BAR       0

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
    uint64_t mbox_int_status;
    uint64_t mbox_ena;

    uint64_t pf_constants;
    uint64_t pf_bist_status;
    uint64_t pf_exe_bist_status;
    uint64_t pf_reset;
    uint64_t pf_exec_busy;
    uint64_t pf_exe_ctl;

    bool in_reset;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->mbox_int_status & s->mbox_ena) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, OTX_CPT_PF_MBOX_INT);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case OTX_CPT_PF_CONSTANTS:
        val = s->pf_constants;
        break;
    case OTX_CPT_PF_BIST_STATUS:
        val = s->pf_bist_status;
        break;
    case OTX_CPT_PF_EXE_BIST_STATUS:
        val = s->pf_exe_bist_status;
        break;
    case OTX_CPT_PF_EXEC_BUSY:
        val = s->pf_exec_busy;
        break;
    case OTX_CPT_PF_EXE_CTL:
        val = s->pf_exe_ctl;
        break;
    case OTX_CPT_PF_RESET:
        val = s->pf_reset;
        break;
    case OTX_CPT_PF_MBOX_INTX(0):
        val = s->mbox_int_status;
        break;
    case OTX_CPT_PF_MBOX_ENA_W1CX(0):
    case OTX_CPT_PF_MBOX_ENA_W1SX(0):
        val = s->mbox_ena;
        break;
    default:
        if (addr >= OTX_CPT_PF_GX_EN(0) && addr < OTX_CPT_PF_GX_EN(OTX_CPT_MAX_ENGINE_GROUPS)) {
            val = 0;
        } else if (addr >= OTX_CPT_PF_ENGX_UCODE_BASE(0) && addr < OTX_CPT_PF_ENGX_UCODE_BASE(OTX_CPT_MAX_ENGINES)) {
            val = 0;
        } else if (addr >= OTX_CPT_PF_QX_CTL(0) && addr < OTX_CPT_PF_QX_CTL(OTX_CPT_MAX_ENGINES)) {
            val = 0;
        } else if (addr >= OTX_CPT_PF_VFX_MBOXX(0,0) && addr < OTX_CPT_PF_VFX_MBOXX(OTX_CPT_MAX_ENGINE_GROUPS-1, 255)) {
            val = 0;
        } else {
            qemu_log_mask(LOG_UNIMP, "Unimplemented MMIO read at 0x%"HWADDR_PRIx"\n", addr);
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case OTX_CPT_PF_BIST_STATUS:
        s->pf_bist_status = val;
        break;
    case OTX_CPT_PF_EXE_BIST_STATUS:
        s->pf_exe_bist_status = val;
        break;
    case OTX_CPT_PF_EXEC_BUSY:
        s->pf_exec_busy = val;
        break;
    case OTX_CPT_PF_EXE_CTL:
        s->pf_exe_ctl = val;
        break;
    case OTX_CPT_PF_RESET:
        s->pf_reset = val;
        if (val & 1) {
            s->mbox_ena = 0;
            s->mbox_int_status = 0;
            pcibase_update_irq(s);
        }
        break;
    case OTX_CPT_PF_MBOX_INTX(0):
        s->mbox_int_status &= ~val;
        pcibase_update_irq(s);
        break;
    case OTX_CPT_PF_MBOX_ENA_W1CX(0):
        s->mbox_ena &= ~val;
        pcibase_update_irq(s);
        break;
    case OTX_CPT_PF_MBOX_ENA_W1SX(0):
        s->mbox_ena |= val;
        pcibase_update_irq(s);
        break;
    default:
        if ((addr >= OTX_CPT_PF_GX_EN(0) && addr < OTX_CPT_PF_GX_EN(OTX_CPT_MAX_ENGINE_GROUPS)) ||
            (addr >= OTX_CPT_PF_ENGX_UCODE_BASE(0) && addr < OTX_CPT_PF_ENGX_UCODE_BASE(OTX_CPT_MAX_ENGINES)) ||
            (addr >= OTX_CPT_PF_QX_CTL(0) && addr < OTX_CPT_PF_QX_CTL(OTX_CPT_MAX_ENGINES)) ||
            (addr >= OTX_CPT_PF_VFX_MBOXX(0,0) && addr < OTX_CPT_PF_VFX_MBOXX(OTX_CPT_MAX_ENGINE_GROUPS-1, 255))) {
            /* Accept writes to these ranges without side effects */
        } else {
            qemu_log_mask(LOG_UNIMP, "Unimplemented MMIO write at 0x%"HWADDR_PRIx" val 0x%"PRIx64"\n", addr, val);
        }
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

    s->mbox_ena = 0;
    s->mbox_int_status = 0;
    s->in_reset = false;
    s->pf_bist_status = 0;
    s->pf_exe_bist_status = 0;
    s->pf_reset = 0;
    s->pf_exec_busy = 0;
    s->pf_exe_ctl = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, OTX_CPT_PCI_PF_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_CRYPT_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, OTX_CPT_PCI_PF_SUBSYS_ID);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x10000000,
        .name = "cpt-pf-bar0"
    };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msix_init_exclusive_bar(pdev, OTX_CPT_PF_MSIX_VECTORS, 0, errp)) {
        return;
    }

    /* Set PF constants to report 1 AE engine and 1 engine group */
    s->pf_constants = 0x110000;
    s->pf_bist_status = 0;
    s->pf_exe_bist_status = 0;
    s->pf_reset = 0;
    s->pf_exec_busy = 0;
    s->pf_exe_ctl = 0;
    s->mbox_ena = 0;
    s->mbox_int_status = 0;
    s->in_reset = false;
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
    .name = "octeontx_cpt_pci",
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
