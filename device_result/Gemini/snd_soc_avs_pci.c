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

#define TYPE_PCIBASE_DEVICE "snd_soc_avs_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define AZX_PCIREG_PGCTL 0x44
#define AZX_PCIREG_CGCTL 0x48
#define AZX_VS_EM2_L1SEN BIT(13)
#define AZX_VS_EM2_DUM BIT(23)
#define AVS_MAIN_CORE_MASK BIT(0)
#define AVS_MAILBOX_SIZE SZ_4K
#define SKL_ADSP_SRAM_WINDOW_SIZE 0x2000
#define SKL_ADSP_SRAM_BASE_OFFSET 0x8000
#define APL_ADSP_SRAM_BASE_OFFSET 0x80000
#define APL_ADSP_SRAM_WINDOW_SIZE 0x20000
#define MTL_ADSP_SRAM_WINDOW_SIZE 0x8000
#define MTL_ADSP_SRAM_BASE_OFFSET 0x180000
#define SKL_ADSP_HIPCIE_DONE BIT(30)
#define SKL_ADSP_HIPCT_BUSY BIT(31)
#define SKL_ADSP_IPC_BASE 0x40
#define SKL_ADSP_REG_HIPCCTL (SKL_ADSP_IPC_BASE + 0x10)
#define SKL_ADSP_REG_HIPCIE (SKL_ADSP_IPC_BASE + 0x0C)
#define SKL_ADSP_REG_HIPCI (SKL_ADSP_IPC_BASE + 0x08)
#define SKL_ADSP_REG_HIPCT (SKL_ADSP_IPC_BASE + 0x00)
#define SKL_ADSP_REG_HIPCTE (SKL_ADSP_IPC_BASE + 0x04)
#define SKL_ADSP_HIPCI_BUSY BIT(31)
#define CNL_ADSP_IPC_BASE 0xC0
#define CNL_ADSP_REG_HIPCCTL (CNL_ADSP_IPC_BASE + 0x28)
#define CNL_ADSP_HIPCTDR_BUSY BIT(31)
#define CNL_ADSP_HIPCIDA_DONE BIT(31)
#define CNL_ADSP_REG_HIPCIDD (CNL_ADSP_IPC_BASE + 0x18)
#define CNL_ADSP_REG_HIPCIDR (CNL_ADSP_IPC_BASE + 0x10)
#define CNL_ADSP_REG_HIPCTDR (CNL_ADSP_IPC_BASE + 0x00)
#define CNL_ADSP_REG_HIPCIDA (CNL_ADSP_IPC_BASE + 0x14)
#define CNL_ADSP_REG_HIPCTDD (CNL_ADSP_IPC_BASE + 0x08)
#define CNL_ADSP_REG_HIPCTDA (CNL_ADSP_IPC_BASE + 0x04)
#define CNL_ADSP_HIPCIDR_BUSY BIT(31)
#define CNL_ADSP_HIPCTDA_DONE BIT(31)
#define MTL_HfIPCxIDR_BUSY BIT(31)
#define MTL_HfIPCxIDA_DONE BIT(31)
#define MTL_HfIPCxTDR_BUSY BIT(31)
#define LNL_REG_HfDFR(x) (0x160200 + (x) * 0x8)
#define AVS_ADSP_GEN_BASE 0x0
#define AVS_ADSP_REG_ADSPIS (AVS_ADSP_GEN_BASE + 0x0C)
#define AVS_ADSP_ADSPIS_IPC BIT(0)
#define AVS_ADSP_HIPCCTL_BUSY BIT(0)
#define AVS_ADSP_REG_ADSPIC (AVS_ADSP_GEN_BASE + 0x08)
#define AVS_ADSP_HIPCCTL_DONE BIT(1)
#define AVS_ADSP_ADSPIC_IPC BIT(0)
#define AVS_ADSP_REG_ADSPCS (AVS_ADSP_GEN_BASE + 0x04)
#define AVS_ADSP_ADSPIS_CLDMA BIT(1)
#define AVS_ADSP_ADSPIC_CLDMA BIT(1)
#define MTL_DWICTL_BASE 0x1800
#define MTL_DWICTL_REG_FINALSTATUSL (MTL_DWICTL_BASE + 0x30)
#define MTL_DWICTL_REG_INTENL (MTL_DWICTL_BASE + 0x0)
#define MTL_HfIPC_BASE 0x73000

#define AZX_INT_GLOBAL_EN	0x80000000
#define AZX_PPCTL_PIE			(1<<31)
#define RIRB_INT_MASK		0x05
#define RIRB_INT_RESPONSE	0x01
#define INTCTL          		0x18
#define AZX_REG_PP_PPSTS		0x08
#define AZX_REG_ML_LOSIDV		0x08

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
    uint32_t adspcs;
    uint32_t adspic;
    uint32_t adspis;
    uint32_t hipc_req;
    uint32_t hipc_ack;
    uint32_t hipc_rsp;
    uint32_t hipc_ctl;

    uint32_t gcap;
    uint32_t intsts;
    uint32_t intctl;
    uint32_t rirbsts;
    uint32_t vs_em2;
    uint32_t ppsts;
};

union avs_module_msg {
    uint64_t val;
    struct {
        union {
            uint32_t primary;
            struct {
                uint32_t module_id:16;
                uint32_t instance_id:8;
                uint32_t module_msg_type:5;
                uint32_t msg_direction:1;
                uint32_t msg_target:1;
            };
        };
        union {
            uint32_t val;
            struct {
                uint32_t param_block_size:16;
                uint32_t ppl_instance_id:8;
                uint32_t core_id:4;
                uint32_t proc_domain:1;
            } init_instance;
            struct {
                uint32_t data_off_size:20;
                uint32_t large_param_id:8;
                uint32_t final_block:1;
                uint32_t init_block:1;
            } large_config;
            struct {
                uint32_t dst_module_id:16;
                uint32_t dst_instance_id:8;
                uint32_t dst_queue:3;
                uint32_t src_queue:3;
            } bind_unbind;
            struct {
                uint32_t wake:1;
                uint32_t streaming:1;
                uint32_t prevent_pg:1;
                uint32_t prevent_local_cg:1;
            } set_d0ix;
        } ext;
    };
};

union avs_notify_msg {
    uint64_t val;
    struct {
        union {
            uint32_t primary;
            struct {
                uint32_t rsvd:16;
                uint32_t notify_msg_type:8;
                uint32_t global_msg_type:5;
                uint32_t msg_direction:1;
                uint32_t msg_target:1;
            };
            struct {
                uint16_t rsvd:12;
                uint16_t core:4;
            } log;
        };
        union {
            uint32_t val;
            struct {
                uint32_t core_id:2;
                uint32_t stack_dump_size:16;
            } coredump;
        } ext;
    };
};

union avs_global_msg {
    uint64_t val;
    struct {
        union {
            uint32_t primary;
            struct {
                uint32_t rsvd:24;
                uint32_t global_msg_type:5;
                uint32_t msg_direction:1;
                uint32_t msg_target:1;
            };
            struct {
                uint32_t rom_ctrl_msg_type:9;
                uint32_t dma_id:5;
                uint32_t purge_request:1;
            } boot_cfg;
            struct {
                uint32_t mod_cnt:8;
            } load_multi_mods;
            struct {
                uint32_t ppl_mem_size:11;
                uint32_t ppl_priority:5;
                uint32_t instance_id:8;
            } create_ppl;
            struct {
                uint32_t rsvd:16;
                uint32_t instance_id:8;
            } ppl;
            struct {
                uint32_t state:16;
                uint32_t ppl_id:8;
            } set_ppl_state;
            struct {
                uint32_t ppl_id:8;
            } get_ppl_state;
            struct {
                uint32_t dma_id:5;
                uint32_t rsvd:11;
                uint32_t lib_id:4;
            } load_lib;
        };
        union {
            uint32_t val;
            struct {
                uint32_t lp:1;
                uint32_t rsvd:3;
                uint32_t attributes:16;
            } create_ppl;
        } ext;
    };
};

union avs_reply_msg {
    uint64_t val;
    struct {
        union {
            uint32_t primary;
            struct {
                uint32_t status:24;
                uint32_t global_msg_type:5;
                uint32_t msg_direction:1;
                uint32_t msg_target:1;
            };
        };
        union {
            uint32_t val;
            struct {
                uint32_t err_mod_id:16;
            } load_multi_mods;
            struct {
                uint32_t state:5;
            } get_ppl_state;
            struct {
                uint32_t data_off_size:20;
                uint32_t large_param_id:8;
                uint32_t final_block:1;
                uint32_t init_block:1;
            } large_config;
        } ext;
    };
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = false;

    if (s->intsts & s->intctl) {
        level = true;
    }

    if (s->has_msi) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case INTCTL:
        val = s->intctl;
        break;
    case SKL_ADSP_REG_HIPCI:
        val = s->hipc_req;
        break;
    case SKL_ADSP_REG_HIPCIE:
        val = s->hipc_ack;
        break;
    case SKL_ADSP_REG_HIPCT:
        val = s->hipc_rsp;
        break;
    case SKL_ADSP_REG_HIPCCTL:
        val = s->hipc_ctl;
        break;
    case AVS_ADSP_REG_ADSPCS:
        val = s->adspcs;
        break;
    case AVS_ADSP_REG_ADSPIC:
        val = s->adspic;
        break;
    case AVS_ADSP_REG_ADSPIS:
        val = s->adspis;
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
    case INTCTL:
        s->intctl = val;
        pcibase_update_irq(s);
        break;
    case SKL_ADSP_REG_HIPCI:
        s->hipc_req = val;
        if (val & SKL_ADSP_HIPCI_BUSY) {
            /* Emulate immediate completion */
            s->hipc_ack |= SKL_ADSP_HIPCIE_DONE;
            s->adspis |= AVS_ADSP_ADSPIS_IPC;
            s->intsts |= 1; /* Trigger IRQ */
            pcibase_update_irq(s);
        }
        break;
    case SKL_ADSP_REG_HIPCIE:
        s->hipc_ack = val;
        break;
    case SKL_ADSP_REG_HIPCT:
        s->hipc_rsp = val;
        break;
    case SKL_ADSP_REG_HIPCCTL:
        s->hipc_ctl = val;
        break;
    case AVS_ADSP_REG_ADSPCS:
        s->adspcs = val;
        break;
    case AVS_ADSP_REG_ADSPIC:
        s->adspic = val;
        break;
    case AVS_ADSP_REG_ADSPIS:
        s->adspis = val;
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

    s->gcap = 0x4400; /* 4 capture, 4 playback streams */
    s->intsts = 0;
    s->intctl = 0;
    s->rirbsts = 0;
    s->vs_em2 = 0;
    s->ppsts = 0;
    s->hipc_req = 0;
    s->hipc_ack = 0;
    s->hipc_rsp = 0;
    s->hipc_ctl = 0;
    s->adspcs = 0;
    s->adspic = 0;
    s->adspis = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    /* HDA_SKL_LP is assumed to be defined elsewhere or passed via build flags, preserving original logic */
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x9d70 ); /* Fallback to typical SKL LP ID if macro missing */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0403 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x4000, "avs-hda-core"};
    s->bar_info[1] = (BARInfo){4, BAR_TYPE_MMIO, 0x200000, "avs-dsp"};

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* msi_init or msix_init calls */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_soc_avs_pci",
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
