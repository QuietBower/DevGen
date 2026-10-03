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
#include "hw/pci/pcie_sriov.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"
#include <stdint.h>

/* Additional include files retrieved from driver context */

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t __be32;
typedef uint64_t dma_addr_t;

#define TYPE_PCIBASE_DEVICE "thunder_cpt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x177d
#define DEVICE_ID 0xa040
#define CLASS_ID  0x1080  /* PCI_CLASS_CRYPT_OTHER */

/* Cavium CPT PF driver register definitions */
#define DRV_NAME        "thunder-cpt"
#define DRV_VERSION     "1.0"
#define CPT_PF_INT_VEC_E_MBOXX(a) (0x02 + (a))
#define CPT_PF_MSIX_VECTORS 3
#define CPT_UCODE_VERSION_SZ 32
#define CPTX_PF_GX_EN(a, b) \
    (0x600ll + ((u64)(a) << 36) + ((b) << 3))
#define CSR_DELAY 30
#define CPTX_PF_EXE_CTL(a) (0x4000000ll + ((u64)(a) << 36))
#define CPTX_PF_EXEC_BUSY(a) (0x800ll + ((u64)(a) << 36))
#define CPTX_PF_MBOX_ENA_W1CX(a, b) \
    (0x440ll + ((u64)(a) << 36) + ((b) << 3))
#define CPTX_PF_ECC0_ENA_W1C(a) (0x250ll + ((u64)(a) << 36))
#define CPTX_PF_EXEC_ENA_W1C(a) (0x540ll + ((u64)(a) << 36))
#define CPTX_PF_MBOX_ENA_W1SX(a, b) \
    (0x460ll + ((u64)(a) << 36) + ((b) << 3))
#define CPT_MAX_TOTAL_CORES (CPT_MAX_SE_CORES + CPT_MAX_AE_CORES)
#define CPT_MAX_SE_CORES 10
#define CPTX_PF_ENGX_UCODE_BASE(a, b) \
    (0x4002000ll + ((u64)(a) << 36) + ((b) << 3))
#define CPT_FLAG_DEVICE_READY BIT(3)
#define CPT_MAX_CORE_GROUPS 8
#define CPTX_PF_RESET(a) (0x100ll + ((u64)(a) << 36))
#define CPTX_PF_CONSTANTS(a) (0x0ll + ((u64)(a) << 36))
#define CPTX_PF_BIST_STATUS(a) (0x160ll + ((u64)(a) << 36))
#define CPTX_PF_EXE_BIST_STATUS(a) (0x4000028ll + ((u64)(a) << 36))
#define CPT_FLAG_SRIOV_ENABLED BIT(1)
#define CPT_81XX_PCI_PF_DEVICE_ID 0xa040
#define CPT_MAX_VF_NUM 16
#define CPT_MAX_AE_CORES 6
#define CPTX_PF_MBOX_INTX(a, b) \
    (0x400ll + ((u64)(a) << 36) + ((b) << 3))
#define CPTX_PF_VFX_MBOXX(a, b, c) \
    (0x8001000ll + ((u64)(a) << 36) + ((b) << 20) + ((c) << 8))
#define VF_STATE_DOWN 0
#define VF_STATE_UP 1
#define CPT_MBOX_MSG_TYPE_ACK 1
#define CPTX_PF_QX_CTL(a, b) \
    (0x8000000ll + ((u64)(a) << 36) + ((b) << 20))

#define BAR0_SIZE (128 * MiB)

enum vftype {
    AE_TYPES = 1,
    SE_TYPES = 2,
    BAD_CPT_TYPES,
};

struct ucode_header {
    u8 version[CPT_UCODE_VERSION_SZ];
    __be32 code_length;
    u32 data_length;
    u64 sram_address;
};

struct microcode {
    u8 is_mc_valid;
    u8 is_ae;
    u8 group;
    u8 num_cores;
    u32 code_size;
    u64 core_mask;
    u8 version[CPT_UCODE_VERSION_SZ];
    /* Base info */
    dma_addr_t phys_base;
    void *code;
};

struct cpt_mbox {
    u64 msg; /* Message type MBOX[0] */
    u64 data;/* Data         MBOX[1] */
};

union cptx_pf_constants {
    u64 u;
    struct cptx_pf_constants_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
        u64 reserved_40_63:24;
        u64 epcis:8;
        u64 grps:8;
        u64 ae:8;
        u64 se:8;
        u64 vq:8;
#else /* Word 0 - Little Endian */
        u64 vq:8;
        u64 se:8;
        u64 ae:8;
        u64 grps:8;
        u64 epcis:8;
        u64 reserved_40_63:24;
#endif /* Word 0 - End */
    } s;
};

union cptx_pf_bist_status {
    u64 u;
    struct cptx_pf_bist_status_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
        u64 reserved_30_63:34;
        u64 bstatus:30;
#else /* Word 0 - Little Endian */
        u64 bstatus:30;
        u64 reserved_30_63:34;
#endif /* Word 0 - End */
    } s;
};

union cptx_pf_exe_bist_status {
    u64 u;
    struct cptx_pf_exe_bist_status_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
        u64 reserved_48_63:16;
        u64 bstatus:48;
#else /* Word 0 - Little Endian */
        u64 bstatus:48;
        u64 reserved_48_63:16;
#endif /* Word 0 - End */
    } s;
};

union cptx_pf_qx_ctl {
    u64 u;
    struct cptx_pf_qx_ctl_s {
#if defined(__BIG_ENDIAN_BITFIELD) /* Word 0 - Big Endian */
        u64 reserved_60_63:4;
        u64 aura:12;
        u64 reserved_45_47:3;
        u64 size:13;
        u64 reserved_11_31:21;
        u64 cont_err:1;
        u64 inst_free:1;
        u64 inst_be:1;
        u64 iqb_ldwb:1;
        u64 reserved_4_6:3;
        u64 grp:3;
        u64 pri:1;
#else /* Word 0 - Little Endian */
        u64 pri:1;
        u64 grp:3;
        u64 reserved_4_6:3;
        u64 iqb_ldwb:1;
        u64 inst_be:1;
        u64 inst_free:1;
        u64 cont_err:1;
        u64 reserved_11_31:21;
        u64 size:13;
        u64 reserved_45_47:3;
        u64 aura:12;
        u64 reserved_60_63:4;
#endif /* Word 0 - End */
    } s;
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
    uint32_t irq_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint64_t pf_constants;
    uint64_t pf_bist_status;
    uint64_t pf_exe_bist_status;
    uint64_t pf_exe_ctl;
    uint64_t pf_exec_busy;
    uint64_t pf_gx_en[CPT_MAX_CORE_GROUPS];
    uint64_t pf_engx_ucode_base[CPT_MAX_TOTAL_CORES];
    uint64_t pf_mbox_ena;
    uint64_t pf_ecc_ena;
    uint64_t pf_exec_ena;

    uint32_t flags;      /* Operational status flags */
    bool resetting;      /* Reset state */
};

/* Internal helper for resetting CPT registers */
static void cpt_reset_registers(PCIBaseState *s)
{
    s->pf_constants = 0x08060A10ULL;
    s->pf_bist_status = 0;
    s->pf_exe_bist_status = 0;
    s->pf_exe_ctl = 0;
    s->pf_exec_busy = 0;
    for (int i = 0; i < CPT_MAX_CORE_GROUPS; i++) {
        s->pf_gx_en[i] = 0;
    }
    for (int i = 0; i < CPT_MAX_TOTAL_CORES; i++) {
        s->pf_engx_ucode_base[i] = 0;
    }
    s->pf_mbox_ena = 0;
    s->pf_ecc_ena = 0;
    s->pf_exec_ena = 0;
    s->flags = 0;
    s->resetting = false;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Access out of BAR0 range: 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }

    if (size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unexpected read size %d at 0x%"HWADDR_PRIx"\n", __func__, size, addr);
        return 0;
    }

    switch (addr) {
    case CPTX_PF_CONSTANTS(0):
        val = s->pf_constants;
        break;
    case CPTX_PF_BIST_STATUS(0):
        val = s->pf_bist_status;
        break;
    case CPTX_PF_EXE_BIST_STATUS(0):
        val = s->pf_exe_bist_status;
        break;
    case CPTX_PF_EXE_CTL(0):
        val = s->pf_exe_ctl;
        break;
    case CPTX_PF_EXEC_BUSY(0):
        val = s->pf_exec_busy;
        break;
    default:
        if (addr >= CPTX_PF_GX_EN(0, 0) && addr <= CPTX_PF_GX_EN(0, CPT_MAX_CORE_GROUPS - 1)) {
            int grp = (addr - CPTX_PF_GX_EN(0, 0)) >> 3;
            if (grp >= 0 && grp < CPT_MAX_CORE_GROUPS) {
                val = s->pf_gx_en[grp];
                break;
            }
        }
        if (addr >= CPTX_PF_ENGX_UCODE_BASE(0, 0) && addr <= CPTX_PF_ENGX_UCODE_BASE(0, CPT_MAX_TOTAL_CORES - 1)) {
            int core = (addr - CPTX_PF_ENGX_UCODE_BASE(0, 0)) >> 3;
            if (core >= 0 && core < CPT_MAX_TOTAL_CORES) {
                val = s->pf_engx_ucode_base[core];
                break;
            }
        }
        if (addr == CPTX_PF_MBOX_ENA_W1CX(0, 0) || addr == CPTX_PF_MBOX_ENA_W1SX(0, 0)) {
            val = s->pf_mbox_ena;
            break;
        }
        if (addr == CPTX_PF_ECC0_ENA_W1C(0)) {
            val = s->pf_ecc_ena;
            break;
        }
        if (addr == CPTX_PF_EXEC_ENA_W1C(0)) {
            val = s->pf_exec_ena;
            break;
        }
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented read at 0x%"HWADDR_PRIx"\n", __func__, addr);
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Access out of BAR0 range: 0x%"HWADDR_PRIx"\n", __func__, addr);
        return;
    }

    if (size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Unexpected write size %d at 0x%"HWADDR_PRIx"\n", __func__, size, addr);
        return;
    }

    switch (addr) {
    case CPTX_PF_CONSTANTS(0):
        /* read-only */
        break;
    case CPTX_PF_BIST_STATUS(0):
        /* read-only */
        break;
    case CPTX_PF_EXE_BIST_STATUS(0):
        /* read-only */
        break;
    case CPTX_PF_RESET(0):
        if (val & 1) {
            cpt_reset_registers(s);
        }
        break;
    case CPTX_PF_EXE_CTL(0):
        s->pf_exe_ctl = val;
        break;
    case CPTX_PF_EXEC_BUSY(0):
        /* read-only */
        break;
    default:
        if (addr >= CPTX_PF_GX_EN(0, 0) && addr <= CPTX_PF_GX_EN(0, CPT_MAX_CORE_GROUPS - 1)) {
            int grp = (addr - CPTX_PF_GX_EN(0, 0)) >> 3;
            if (grp >= 0 && grp < CPT_MAX_CORE_GROUPS) {
                s->pf_gx_en[grp] = val;
                break;
            }
        }
        if (addr >= CPTX_PF_ENGX_UCODE_BASE(0, 0) && addr <= CPTX_PF_ENGX_UCODE_BASE(0, CPT_MAX_TOTAL_CORES - 1)) {
            int core = (addr - CPTX_PF_ENGX_UCODE_BASE(0, 0)) >> 3;
            if (core >= 0 && core < CPT_MAX_TOTAL_CORES) {
                s->pf_engx_ucode_base[core] = val;
                break;
            }
        }
        if (addr == CPTX_PF_MBOX_ENA_W1CX(0, 0)) {
            s->pf_mbox_ena &= ~val;
            break;
        }
        if (addr == CPTX_PF_MBOX_ENA_W1SX(0, 0)) {
            s->pf_mbox_ena |= val;
            break;
        }
        if (addr == CPTX_PF_ECC0_ENA_W1C(0)) {
            s->pf_ecc_ena &= ~val;
            break;
        }
        if (addr == CPTX_PF_EXEC_ENA_W1C(0)) {
            s->pf_exec_ena &= ~val;
            break;
        }
        qemu_log_mask(LOG_UNIMP, "%s: Unimplemented write at 0x%"HWADDR_PRIx" with value 0x%"PRIx64"\n", __func__, addr, val);
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
    cpt_reset_registers(s);
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
    uint16_t sriov_offset;

    /* Static PCI configuration */
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

    /* Add SRIOV capability to satisfy driver's cpt_sriov_init */
    sriov_offset = pcie_add_capability(pdev, PCI_EXT_CAP_ID_SRIOV, 1, 0, 0x40, errp);
    if (sriov_offset) {
        pcie_sriov_pf_init(pdev, sriov_offset, NULL, DEVICE_ID, 0, CPT_MAX_VF_NUM, errp);
    }

    /* BAR Initialization */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "cpt-mmio";
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    if (msix_init(pdev, CPT_PF_MSIX_VECTORS, NULL, 0, 0, NULL, 0, 0, 0, errp)) {
        return;
    }
    s->has_msix = true;

    /* Initialize hardware register defaults */
    cpt_reset_registers(s);
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "thunder_cpt_pci",
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
