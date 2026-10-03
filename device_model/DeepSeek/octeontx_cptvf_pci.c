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

#define TYPE_PCIBASE_DEVICE "octeontx_cptvf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define VENDOR_ID PCI_VENDOR_ID_CAVIUM
#define DEVICE_ID 0xa041
#define CLASS_ID 0x0000 /* TODO: request define:OTX_CPT_VF_CLASS_ID */
#define BAR0_SIZE (1024*1024) /* TODO: request define:OTX_CPT_VF_BAR0_SIZE */

/* Register offsets for VF queue 0 */
#define OTX_CPT_VQX_CTL         0x100
#define OTX_CPT_VQX_DOORBELL    0x600
#define OTX_CPT_VQX_INPROG      0x410
#define OTX_CPT_VQX_DONE_WAIT   0x400
#define OTX_CPT_VQX_MISC_ENA_W1S 0x510
#define OTX_CPT_VQX_DONE_ENA_W1S  0x470
#define OTX_CPT_VQX_MISC_INT     0x500
#define OTX_CPT_VQX_DONE         0x420
#define OTX_CPT_VQX_DONE_ACK     0x440
#define OTX_CPT_VQX_SADDR        0x200
#define OTX_CPT_VFX_PF_MBOXX_MSG(c) (0x1000 + (c) * 16)
#define OTX_CPT_VFX_PF_MBOXX_DATA(c) (0x1000 + (c) * 16 + 0x8)
#define OTX_CPT_NUM_MAILBOXES 2
#define OTX_CPT_VF_MSIX_VECTORS 2

/* Interrupt mask bits from driver */
#define OTX_CPT_VF_INTR_MBOX_MASK BIT(0)
#define OTX_CPT_VF_INTR_DOVF_MASK BIT(1)
#define OTX_CPT_VF_INTR_IRDE_MASK BIT(2)
#define OTX_CPT_VF_INTR_NWRP_MASK BIT(3)
#define OTX_CPT_VF_INTR_SERR_MASK BIT(4)

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
    uint64_t reg_misc_ena;
    uint64_t reg_done_ena;
    uint64_t reg_misc_int;
    uint64_t reg_done;
    uint64_t reg_done_ack;
    QEMUTimer *irq_timer;
    QEMUTimer *mbox_timer;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint64_t reg_ctl;
    uint64_t reg_doorbell;
    uint64_t reg_inprog;
    uint64_t reg_done_wait;
    uint64_t reg_saddr;
    uint64_t reg_mbox_msg[OTX_CPT_NUM_MAILBOXES];
    uint64_t reg_mbox_data[OTX_CPT_NUM_MAILBOXES];

    /* VF-PF mailbox outgoing message */
    uint64_t vf_mbox_msg;
    uint64_t vf_mbox_data;

    /* Device identity */
    uint8_t vfid;
    uint8_t num_vfs;
    uint8_t vf_type;     /* AE or SE */
    uint8_t vf_grp;
    uint8_t priority;

    /* DMA Context */
    struct {
        /* Command queue and pending queue state for DMA */
        /* to be implemented in Phase 2 */
    } dma;

    uint32_t flags;      /* Operational status flags */

    int reset_count;     /* State used to handle reset sequences */

    uint8_t pm_state;    /* Power management state (D0-D3) */
};

/* Register bitfield definitions (from driver, little-endian) */
typedef union {
    uint64_t u;
    struct {
        uint64_t ena:1;
        uint64_t reserved_1_63:63;
    } s;
} otx_cptx_vqx_ctl_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t dbell_cnt:20;
        uint64_t reserved_20_63:44;
    } s;
} otx_cptx_vqx_doorbell_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t inflight:8;
        uint64_t reserved_8_63:56;
    } s;
} otx_cptx_vqx_inprog_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t num_wait:20;
        uint64_t reserved_20_31:12;
        uint64_t time_wait:16;
        uint64_t reserved_48_63:16;
    } s;
} otx_cptx_vqx_done_wait_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t mbox:1;
        uint64_t dovf:1;
        uint64_t irde:1;
        uint64_t nwrp:1;
        uint64_t swerr:1;
        uint64_t reserved_5_63:59;
    } s;
} otx_cptx_vqx_misc_ena_w1s_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t done:1;
        uint64_t reserved_1_63:63;
    } s;
} otx_cptx_vqx_done_ena_w1s_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t mbox:1;
        uint64_t dovf:1;
        uint64_t irde:1;
        uint64_t nwrp:1;
        uint64_t swerr:1;
        uint64_t reserved_5_63:59;
    } s;
} otx_cptx_vqx_misc_int_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t done:20;
        uint64_t reserved_20_63:44;
    } s;
} otx_cptx_vqx_done_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t done_ack:20;
        uint64_t reserved_20_63:44;
    } s;
} otx_cptx_vqx_done_ack_t;

typedef union {
    uint64_t u;
    struct {
        uint64_t reserved_0_5:6;
        uint64_t ptr:43;
        uint64_t reserved_49_63:15;
    } s;
} otx_cptx_vqx_saddr_t;

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t misc_pending = s->reg_misc_int & s->reg_misc_ena;
    uint64_t done_pending = s->reg_done & s->reg_done_ena;

    if (s->has_msix && msix_enabled(pdev)) {
        if (misc_pending != 0) {
            msix_notify(pdev, 0);  /* MISC vector 0 */
        }
        if (done_pending != 0) {
            msix_notify(pdev, 1);  /* DONE vector 1 */
        }
    } else if (msi_enabled(pdev)) {
        /* MSI case: raise if any interrupt pending */
        if (misc_pending != 0 || done_pending != 0) {
            msi_notify(pdev, 0);  /* single MSI vector */
        }
    } else {
        bool irq = (misc_pending != 0) || (done_pending != 0);
        pci_set_irq(pdev, irq ? 1 : 0);
    }
}

/* Mailbox processing timer callback */
static void pcibase_mbox_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    uint64_t msg = s->vf_mbox_msg;
    uint64_t resp_msg = 0; /* TODO: OTX_CPT_MSG_ACK not yet defined */
    uint64_t resp_data = 0;

    /* Process message */
    switch (msg) {
    case 0: /* OTX_CPT_MSG_READY placeholder */
        resp_msg = 0;  /* OTX_CPT_MSG_READY, value to be defined */
        resp_data = s->vfid;
        break;
    case 1: /* OTX_CPT_MSG_VF_UP placeholder */
        resp_msg = 1;  /* OTX_CPT_MSG_VF_UP */
        resp_data = s->num_vfs;
        break;
    case 2: /* OTX_CPT_MSG_QBIND_GRP placeholder */
        resp_msg = 2;  /* OTX_CPT_MSG_QBIND_GRP */
        resp_data = s->vf_type; /* OTX_CPT_SE_TYPES or OTX_CPT_AE_TYPES */
        break;
    case 3: /* OTX_CPT_MSG_QLEN */
    case 4: /* OTX_CPT_MSG_VQ_PRIORITY */
    case 5: /* OTX_CPT_MSG_VF_DOWN */
        resp_msg = 6;  /* OTX_CPT_MSG_ACK placeholder */
        resp_data = 0;
        break;
    default:
        resp_msg = 7;  /* OTX_CPT_MSG_NACK placeholder */
        resp_data = 0;
        break;
    }

    /* Write response into mailbox 0 registers */
    s->reg_mbox_msg[0] = resp_msg;
    s->reg_mbox_data[0] = resp_data;

    /* Trigger mailbox interrupt */
    s->reg_misc_int |= OTX_CPT_VF_INTR_MBOX_MASK;
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case OTX_CPT_VQX_CTL:
        val = s->reg_ctl;
        break;
    case OTX_CPT_VQX_DOORBELL:
        val = s->reg_doorbell;
        break;
    case OTX_CPT_VQX_INPROG:
        val = s->reg_inprog;
        break;
    case OTX_CPT_VQX_DONE_WAIT:
        val = s->reg_done_wait;
        break;
    case OTX_CPT_VQX_MISC_ENA_W1S:
        val = s->reg_misc_ena;
        break;
    case OTX_CPT_VQX_DONE_ENA_W1S:
        val = s->reg_done_ena;
        break;
    case OTX_CPT_VQX_MISC_INT:
        val = s->reg_misc_int;
        break;
    case OTX_CPT_VQX_DONE:
        val = s->reg_done;
        break;
    case OTX_CPT_VQX_DONE_ACK:
        val = s->reg_done_ack;
        break;
    case OTX_CPT_VQX_SADDR:
        val = s->reg_saddr;
        break;
    default:
        /* Handle PF mailbox registers (2 per mailbox, message and data) */
        if (addr >= OTX_CPT_VFX_PF_MBOXX_MSG(0) &&
            addr <= OTX_CPT_VFX_PF_MBOXX_DATA(OTX_CPT_NUM_MAILBOXES - 1)) {
            int mb = (addr - OTX_CPT_VFX_PF_MBOXX_MSG(0)) / 16;
            int reg = (addr & 0x8) ? 1 : 0;  /* 0: msg, 1: data */
            if (reg == 0) {
                val = s->reg_mbox_msg[mb];
            } else {
                val = s->reg_mbox_data[mb];
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: read from unknown address 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
            val = ~0ULL;
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case OTX_CPT_VQX_CTL:
        s->reg_ctl = val;
        break;
    case OTX_CPT_VQX_DOORBELL:
        s->reg_doorbell = val;
        /* Simulate processing: after doorbell, eventually increment done count */
        if (val != 0) {
            timer_mod(s->irq_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        }
        break;
    case OTX_CPT_VQX_INPROG:
        s->reg_inprog = val;
        break;
    case OTX_CPT_VQX_DONE_WAIT:
        s->reg_done_wait = val;
        break;
    case OTX_CPT_VQX_MISC_ENA_W1S:
        /* Write-1-to-set */
        s->reg_misc_ena |= val;
        pcibase_update_irq(s);
        break;
    case OTX_CPT_VQX_DONE_ENA_W1S:
        s->reg_done_ena |= val;
        pcibase_update_irq(s);
        break;
    case OTX_CPT_VQX_MISC_INT:
        /* Write-1-to-clear */
        s->reg_misc_int &= ~val;
        pcibase_update_irq(s);
        break;
    case OTX_CPT_VQX_DONE:
        /* Read-only, ignore write */
        break;
    case OTX_CPT_VQX_DONE_ACK:
        {
            /* Write acknowledgment count: decrement done count by ack */
            uint64_t ack = val & 0xFFFFF; /* 20-bit field */
            if (s->reg_done >= ack) {
                s->reg_done -= ack;
            } else {
                s->reg_done = 0;
            }
            pcibase_update_irq(s);
        }
        break;
    case OTX_CPT_VQX_SADDR:
        s->reg_saddr = val;
        break;
    default:
        if (addr >= OTX_CPT_VFX_PF_MBOXX_MSG(0) &&
            addr <= OTX_CPT_VFX_PF_MBOXX_DATA(OTX_CPT_NUM_MAILBOXES - 1)) {
            int mb = (addr - OTX_CPT_VFX_PF_MBOXX_MSG(0)) / 16;
            int reg = (addr & 0x8) ? 1 : 0;
            if (reg == 0) {
                s->reg_mbox_msg[mb] = val;
                /* Only mailbox 0 triggers PF message processing */
                if (mb == 0) {
                    s->vf_mbox_msg = val;
                    timer_mod(s->mbox_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
                }
            } else {
                s->reg_mbox_data[mb] = val;
                if (mb == 0) {
                    s->vf_mbox_data = val;
                }
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to unknown address 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n",
                          __func__, addr, val);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    /* PIO not used by driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by driver */
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
    /* Reset registers to power-on defaults */
    s->reg_ctl = 0;
    s->reg_doorbell = 0;
    s->reg_inprog = 0;
    s->reg_done_wait = 0;
    s->reg_misc_ena = 0;
    s->reg_done_ena = 0;
    s->reg_misc_int = 0;
    s->reg_done = 0;
    s->reg_done_ack = 0;
    s->reg_saddr = 0;
    memset(s->reg_mbox_msg, 0, sizeof(s->reg_mbox_msg));
    memset(s->reg_mbox_data, 0, sizeof(s->reg_mbox_data));
    s->vf_mbox_msg = 0;
    s->vf_mbox_data = 0;
    s->flags = 0;
    s->pm_state = 0;
    s->reset_count = 0;
    s->vfid = 0;
    s->num_vfs = 1;
    s->vf_type = 2;  /* OTX_CPT_SE_TYPES placeholder */
    s->vf_grp = 0;
    s->priority = 0;
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

static void pcibase_irq_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    /* Simulate completion of doorbell commands: increment done count by doorbell count */
    uint64_t db_cnt = s->reg_doorbell & 0xFFFFF; /* 20-bit doorbell count */
    s->reg_done += db_cnt;
    pcibase_update_irq(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    Error *local_err = NULL;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x177d);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0xa041);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x1000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;  /* BAR0: MMIO, BAR1: MSI-X table */
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "bar0"
    };
    s->bar_info[1] = (BARInfo){
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = 4 * 1024,  /* 4KB for MSI-X table */
        .name = "msix-bar"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    if (msix_init(pdev, OTX_CPT_VF_MSIX_VECTORS,
                  &s->bar_regions[1], 1, 0,
                  &s->bar_regions[1], 1, 0x1000,
                  0, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }
    s->has_msix = true;

    /* Timers */
    s->irq_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_irq_timer, s);
    s->mbox_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_mbox_timer, s);

    /* Final state initialization */
    s->flags = 0;
    s->reg_ctl = 0;
    s->reg_doorbell = 0;
    s->reg_inprog = 0;
    s->reg_done_wait = 0;
    s->reg_misc_ena = 0;
    s->reg_done_ena = 0;
    s->reg_misc_int = 0;
    s->reg_done = 0;
    s->reg_done_ack = 0;
    s->reg_saddr = 0;
    memset(s->reg_mbox_msg, 0, sizeof(s->reg_mbox_msg));
    memset(s->reg_mbox_data, 0, sizeof(s->reg_mbox_data));
    s->vf_mbox_msg = 0;
    s->vf_mbox_data = 0;
    s->pm_state = 0;
    s->reset_count = 0;
    s->vfid = 0;
    s->num_vfs = 1;
    s->vf_type = 2;  /* OTX_CPT_SE_TYPES placeholder */
    s->vf_grp = 0;
    s->priority = 0;
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
    timer_del(s->irq_timer);
    timer_free(s->irq_timer);
    timer_del(s->mbox_timer);
    timer_free(s->mbox_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "octeontx_cptvf_pci",
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
