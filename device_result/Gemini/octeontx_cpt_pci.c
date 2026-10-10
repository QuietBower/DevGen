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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "octeontx_cpt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_CAVIUM
#define PCI_VENDOR_ID_CAVIUM 0x177d
#endif

#define OTX_CPT_PCI_PF_DEVICE_ID 0xa040
#define OTX_CPT_PCI_PF_SUBSYS_ID 0xa340
#define OTX_CPT_PF_PCI_CFG_BAR 0
#define OTX_CPT_PF_MSIX_VECTORS 4

#define OTX_CPT_MAX_ENGINE_GROUPS 8

#define OTX_CPT_PF_CONSTANTS        0x0ull
#define OTX_CPT_PF_RESET            0x100ull
#define OTX_CPT_PF_BIST_STATUS      0x160ull
#define OTX_CPT_PF_MBOX_INTX(b)     (0x400ull | (uint64_t)(b) << 3)
#define OTX_CPT_PF_MBOX_ENA_W1CX(b) (0x440ull | (uint64_t)(b) << 3)
#define OTX_CPT_PF_MBOX_ENA_W1SX(b) (0x460ull | (uint64_t)(b) << 3)
#define OTX_CPT_PF_GX_EN(b)         (0x600ull | (uint64_t)(b) << 3)
#define OTX_CPT_PF_EXEC_BUSY        0x800ull
#define OTX_CPT_PF_EXE_CTL          0x4000000ull
#define OTX_CPT_PF_ENGX_UCODE_BASE(b) (0x4002000ull | (uint64_t)(b) << 3)
#define OTX_CPT_PF_EXE_BIST_STATUS  0x4000028ull
#define OTX_CPT_PF_QX_CTL(b)        (0x8000000ull | (uint64_t)(b) << 20)
#define OTX_CPT_PF_VFX_MBOXX(b, c)  (0x8001000ull | (uint64_t)(b) << 20 | (uint64_t)(c) << 8)

#define OTX_CPT_PF_MBOX_INT 3
#define OTX_CPT_PF_INT_VEC_E_MBOXX(x, a) ((x) + (a))

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint64_t pf_constants;
    uint64_t pf_bist_status;
    uint64_t pf_exe_bist_status;
    uint64_t pf_exe_ctl;
    uint64_t pf_exec_busy;
    uint64_t pf_gx_en[OTX_CPT_MAX_ENGINE_GROUPS];
};

union otx_cptx_pf_constants {
    uint64_t u;
    struct {
        uint64_t vq:8;
        uint64_t se:8;
        uint64_t ae:8;
        uint64_t grps:8;
        uint64_t epcis:8;
        uint64_t reserved_40_63:24;
    } s;
};

union otx_cptx_pf_bist_status {
    uint64_t u;
    struct {
        uint64_t bstatus:30;
        uint64_t reserved_30_63:34;
    } s;
};

union otx_cptx_pf_exe_bist_status {
    uint64_t u;
    struct {
        uint64_t bstatus:48;
        uint64_t reserved_48_63:16;
    } s;
};

union otx_cptx_pf_qx_ctl {
    uint64_t u;
    struct {
        uint64_t pri:1;
        uint64_t grp:3;
        uint64_t reserved_4_6:3;
        uint64_t iqb_ldwb:1;
        uint64_t inst_be:1;
        uint64_t inst_free:1;
        uint64_t cont_err:1;
        uint64_t reserved_11_31:21;
        uint64_t size:13;
        uint64_t reserved_45_47:3;
        uint64_t aura:12;
        uint64_t reserved_60_63:4;
    } s;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intr_status & s->intr_mask) != 0;

    if (level) {
        if (s->has_msix) {
            msix_notify(pdev, OTX_CPT_PF_MBOX_INT);
        } else if (s->has_msi) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msix && !s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
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
    case OTX_CPT_PF_MBOX_INTX(0):
        val = s->intr_status;
        break;
    case OTX_CPT_PF_MBOX_ENA_W1SX(0):
    case OTX_CPT_PF_MBOX_ENA_W1CX(0):
        val = s->intr_mask;
        break;
    case OTX_CPT_PF_EXEC_BUSY:
        val = s->pf_exec_busy;
        break;
    case OTX_CPT_PF_EXE_CTL:
        val = s->pf_exe_ctl;
        break;
    default:
        if (addr >= OTX_CPT_PF_GX_EN(0) && addr <= OTX_CPT_PF_GX_EN(OTX_CPT_MAX_ENGINE_GROUPS - 1)) {
            int b = (addr - OTX_CPT_PF_GX_EN(0)) >> 3;
            if (b >= 0 && b < OTX_CPT_MAX_ENGINE_GROUPS) {
                val = s->pf_gx_en[b];
                break;
            }
        }
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented read at 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case OTX_CPT_PF_RESET:
        if (val == 1) {
            /* Device reset triggered by driver */
            device_cold_reset(DEVICE(s));
        }
        break;
    case OTX_CPT_PF_MBOX_ENA_W1SX(0):
        s->intr_mask |= val;
        pcibase_update_irq(s);
        break;
    case OTX_CPT_PF_MBOX_ENA_W1CX(0):
        s->intr_mask &= ~val;
        pcibase_update_irq(s);
        break;
    case OTX_CPT_PF_MBOX_INTX(0):
        s->intr_status &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case OTX_CPT_PF_EXE_CTL:
        s->pf_exe_ctl = val;
        break;
    default:
        if (addr >= OTX_CPT_PF_GX_EN(0) && addr <= OTX_CPT_PF_GX_EN(OTX_CPT_MAX_ENGINE_GROUPS - 1)) {
            int b = (addr - OTX_CPT_PF_GX_EN(0)) >> 3;
            if (b >= 0 && b < OTX_CPT_MAX_ENGINE_GROUPS) {
                s->pf_gx_en[b] = val;
                break;
            }
        }
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented write at 0x%" HWADDR_PRIx "\n", __func__, addr);
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

    union otx_cptx_pf_constants cnsts = {0};
    cnsts.s.se = 4; /* Provide non-zero SE cores */
    cnsts.s.ae = 4; /* Provide non-zero AE cores */
    s->pf_constants = cnsts.u;

    s->pf_bist_status = 0; /* 0 indicates success */
    s->pf_exe_bist_status = 0; /* 0 indicates success */
    s->intr_mask = 0;
    s->intr_status = 0;
    s->pf_exec_busy = 0;
    s->pf_exe_ctl = 0;
    for (int i = 0; i < OTX_CPT_MAX_ENGINE_GROUPS; i++) {
        s->pf_gx_en[i] = 0;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CAVIUM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  OTX_CPT_PCI_PF_DEVICE_ID );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, OTX_CPT_PCI_PF_SUBSYS_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
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
    s->bar_info[0].index = OTX_CPT_PF_PCI_CFG_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000000; /* 256MB to cover max offset 0x8001000 */
    s->bar_info[0].name = "otx-cpt-pf-bar0";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msix_init(pdev, OTX_CPT_PF_MSIX_VECTORS, &s->bar_regions[0], 0, 0x0FFF0000, &s->bar_regions[0], 0, 0x0FFF1000, 0, errp) == 0) {
        s->has_msix = true;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (s->has_msix) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }

}

/* Minimal VMState to satisfy QEMU migration subsystems */
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
