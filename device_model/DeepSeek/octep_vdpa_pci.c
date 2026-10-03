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

#define TYPE_PCIBASE_DEVICE "octep_vdpa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define OCTEP_VDPA_DEVID_CN106K_PF 0xb900
#define OCTEP_HW_MBOX_BAR 0
#define OCTEP_DEV_READY_SIGNATURE 0xBABABABA
#define OCTEP_VF_MBOX_DATA(x) (0x00010210 | ((x) << 17))
#define OCTEP_VF_IN_CTRL(x)        (0x00010000 | ((x) << 17))
#define OCTEP_MAX_CB_INTR          8
#define OCTEP_VF_IN_CTRL_RPVF(val) (((val) >> 48) & 0xF)
#define OCTEP_PF_MBOX_DATA(x) (0x00022000 | ((x) << 4))
#define OCTEP_EPF_RINFO(x) (0x000209f0 | ((x) << 25))
#define OCTEP_FW_READY_SIGNATURE0  0xFEEDFEED
#define OCTEP_FW_READY_SIGNATURE1  0x3355ffaa

/* Virtio feature bits required by driver -- taken from Linux uapi */
#define VIRTIO_F_VERSION_1          32
#define VIRTIO_F_RING_PACKED        34
#define VIRTIO_F_NOTIFICATION_DATA  36

/* Virtio PCI capability types */
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_DEVICE_CFG   3
#define VIRTIO_PCI_CAP_ISR_CFG      4
#define VIRTIO_PCI_CAP_VENDOR_CFG   5

/* Offsets within common configuration structure */
#define COMMON_CFG_DEVICE_FEATURE_SELECT  0x00
#define COMMON_CFG_DEVICE_FEATURE         0x04
#define COMMON_CFG_GUEST_FEATURE_SELECT   0x08
#define COMMON_CFG_GUEST_FEATURE          0x0C
#define COMMON_CFG_MSIX_CONFIG            0x10
#define COMMON_CFG_NUM_QUEUES             0x12
#define COMMON_CFG_DEVICE_STATUS          0x14
#define COMMON_CFG_CONFIG_GENERATION      0x15
#define COMMON_CFG_QUEUE_SELECT           0x16
#define COMMON_CFG_QUEUE_SIZE             0x18
#define COMMON_CFG_QUEUE_MSIX_VECTOR      0x1A
#define COMMON_CFG_QUEUE_ENABLE           0x1C
#define COMMON_CFG_QUEUE_NOTIFY_OFF       0x1E
#define COMMON_CFG_QUEUE_DESC_LO          0x20
#define COMMON_CFG_QUEUE_DESC_HI          0x24
#define COMMON_CFG_QUEUE_AVAIL_LO         0x28
#define COMMON_CFG_QUEUE_AVAIL_HI         0x2C
#define COMMON_CFG_QUEUE_USED_LO          0x30
#define COMMON_CFG_QUEUE_USED_HI          0x34

/* Offsets within BAR1 for the different structures */
#define CFG_BAR_COMMON_BASE   0x0
#define CFG_BAR_ISR_BASE      0x40
#define CFG_BAR_DEVICE_BASE   0x44
#define CFG_BAR_NOTIFY_BASE   0x200
#define CFG_BAR_NOTIFY_SIZE   0x1000

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

/* Virtio common configuration state */
struct virtio_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;  /* read-only, computed */
    uint32_t guest_feature_select;
    uint64_t guest_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t device_status;
    uint8_t config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint32_t queue_desc_lo;
    uint32_t queue_desc_hi;
    uint32_t queue_avail_lo;
    uint32_t queue_avail_hi;
    uint32_t queue_used_lo;
    uint32_t queue_used_hi;
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* MBOX BAR state */
    uint64_t pf_mbox_data[256]; /* store writes to PF_MBOX_DATA */
    uint64_t epf_rinfo;         /* value for EPF_RINFO(0) */

    /* CAPS BAR state */
    struct virtio_common_cfg common_cfg;
    uint8_t isr_status;
    uint64_t dev_features;  /* complete 64-bit device features */
    uint16_t notify_off_multiplier;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */

/* MBOX MMIO handlers */
static uint64_t pcibase_mbox_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x22000 && addr < 0x22000 + sizeof(s->pf_mbox_data)) {
        int idx = (addr - 0x22000) >> 4;
        val = s->pf_mbox_data[idx];
    } else if (addr == OCTEP_EPF_RINFO(0)) {
        val = s->epf_rinfo;
    }
    /* other addresses return 0 */
    return val;
}

static void pcibase_mbox_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x22000 && addr < 0x22000 + sizeof(s->pf_mbox_data)) {
        int idx = (addr - 0x22000) >> 4;
        s->pf_mbox_data[idx] = val;
    }
    /* other writes ignored */
}

static const MemoryRegionOps pcibase_mbox_mmio_ops = {
    .read = pcibase_mbox_mmio_read,
    .write = pcibase_mbox_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* CAPS MMIO handlers */
static uint64_t pcibase_caps_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < CFG_BAR_COMMON_BASE + 0x40) {
        /* Common configuration */
        uint32_t offset = addr - CFG_BAR_COMMON_BASE;
        switch (offset) {
        case COMMON_CFG_DEVICE_FEATURE_SELECT:
            val = s->common_cfg.device_feature_select;
            break;
        case COMMON_CFG_DEVICE_FEATURE:
            /* Return feature bits for the selected page */
            if (s->common_cfg.device_feature_select == 0) {
                val = (uint32_t)s->dev_features;
            } else if (s->common_cfg.device_feature_select == 1) {
                val = (uint32_t)(s->dev_features >> 32);
            }
            break;
        case COMMON_CFG_GUEST_FEATURE_SELECT:
            val = s->common_cfg.guest_feature_select;
            break;
        case COMMON_CFG_GUEST_FEATURE:
            if (s->common_cfg.guest_feature_select == 0) {
                val = (uint32_t)s->common_cfg.guest_feature;
            } else if (s->common_cfg.guest_feature_select == 1) {
                val = (uint32_t)(s->common_cfg.guest_feature >> 32);
            }
            break;
        case COMMON_CFG_MSIX_CONFIG:
            val = s->common_cfg.msix_config;
            break;
        case COMMON_CFG_NUM_QUEUES:
            val = s->common_cfg.num_queues;
            break;
        case COMMON_CFG_DEVICE_STATUS:
            val = s->common_cfg.device_status;
            break;
        case COMMON_CFG_CONFIG_GENERATION:
            val = s->common_cfg.config_generation;
            break;
        case COMMON_CFG_QUEUE_SELECT:
            val = s->common_cfg.queue_select;
            break;
        case COMMON_CFG_QUEUE_SIZE:
            val = s->common_cfg.queue_size;
            break;
        case COMMON_CFG_QUEUE_MSIX_VECTOR:
            val = s->common_cfg.queue_msix_vector;
            break;
        case COMMON_CFG_QUEUE_ENABLE:
            val = s->common_cfg.queue_enable;
            break;
        case COMMON_CFG_QUEUE_NOTIFY_OFF:
            val = s->common_cfg.queue_notify_off;
            break;
        case COMMON_CFG_QUEUE_DESC_LO:
            val = s->common_cfg.queue_desc_lo;
            break;
        case COMMON_CFG_QUEUE_DESC_HI:
            val = s->common_cfg.queue_desc_hi;
            break;
        case COMMON_CFG_QUEUE_AVAIL_LO:
            val = s->common_cfg.queue_avail_lo;
            break;
        case COMMON_CFG_QUEUE_AVAIL_HI:
            val = s->common_cfg.queue_avail_hi;
            break;
        case COMMON_CFG_QUEUE_USED_LO:
            val = s->common_cfg.queue_used_lo;
            break;
        case COMMON_CFG_QUEUE_USED_HI:
            val = s->common_cfg.queue_used_hi;
            break;
        default:
            val = 0;
            break;
        }
    } else if (addr == CFG_BAR_ISR_BASE) {
        /* ISR: read and clear */
        val = s->isr_status;
        s->isr_status = 0;
    } else if (addr >= CFG_BAR_DEVICE_BASE && addr < CFG_BAR_DEVICE_BASE + 0x100) {
        /* Device-specific config: return 0 for now */
        val = 0;
    } else if (addr >= CFG_BAR_NOTIFY_BASE && addr < CFG_BAR_NOTIFY_BASE + CFG_BAR_NOTIFY_SIZE) {
        /* Notify region: write-only in practice, reads return 0 */
        val = 0;
    }
    return val;
}

static void pcibase_caps_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < CFG_BAR_COMMON_BASE + 0x40) {
        /* Common configuration */
        uint32_t offset = addr - CFG_BAR_COMMON_BASE;
        switch (offset) {
        case COMMON_CFG_DEVICE_FEATURE_SELECT:
            s->common_cfg.device_feature_select = val;
            break;
        case COMMON_CFG_DEVICE_FEATURE:
            /* read-only, ignore write */
            break;
        case COMMON_CFG_GUEST_FEATURE_SELECT:
            s->common_cfg.guest_feature_select = val;
            break;
        case COMMON_CFG_GUEST_FEATURE:
            if (s->common_cfg.guest_feature_select == 0) {
                s->common_cfg.guest_feature = (s->common_cfg.guest_feature & ~0xFFFFFFFFULL) | (val & 0xFFFFFFFF);
            } else if (s->common_cfg.guest_feature_select == 1) {
                s->common_cfg.guest_feature = (s->common_cfg.guest_feature & 0xFFFFFFFF) | ((uint64_t)val << 32);
            }
            break;
        case COMMON_CFG_MSIX_CONFIG:
            s->common_cfg.msix_config = val;
            break;
        case COMMON_CFG_NUM_QUEUES:
            /* read-only? or ignore */
            break;
        case COMMON_CFG_DEVICE_STATUS:
            s->common_cfg.device_status = val & 0xFF;
            break;
        case COMMON_CFG_CONFIG_GENERATION:
            /* read-only */
            break;
        case COMMON_CFG_QUEUE_SELECT:
            s->common_cfg.queue_select = val;
            break;
        case COMMON_CFG_QUEUE_SIZE:
            s->common_cfg.queue_size = val;
            break;
        case COMMON_CFG_QUEUE_MSIX_VECTOR:
            s->common_cfg.queue_msix_vector = val;
            break;
        case COMMON_CFG_QUEUE_ENABLE:
            s->common_cfg.queue_enable = val;
            break;
        case COMMON_CFG_QUEUE_NOTIFY_OFF:
            s->common_cfg.queue_notify_off = val;
            break;
        case COMMON_CFG_QUEUE_DESC_LO:
            s->common_cfg.queue_desc_lo = val;
            break;
        case COMMON_CFG_QUEUE_DESC_HI:
            s->common_cfg.queue_desc_hi = val;
            break;
        case COMMON_CFG_QUEUE_AVAIL_LO:
            s->common_cfg.queue_avail_lo = val;
            break;
        case COMMON_CFG_QUEUE_AVAIL_HI:
            s->common_cfg.queue_avail_hi = val;
            break;
        case COMMON_CFG_QUEUE_USED_LO:
            s->common_cfg.queue_used_lo = val;
            break;
        case COMMON_CFG_QUEUE_USED_HI:
            s->common_cfg.queue_used_hi = val;
            break;
        default:
            break;
        }
    } else if (addr == CFG_BAR_ISR_BASE) {
        /* ISR: write ignored (or maybe clear), driver doesn't write */
    } else if (addr >= CFG_BAR_DEVICE_BASE && addr < CFG_BAR_DEVICE_BASE + 0x100) {
        /* Device config: write ignored for now */
    } else if (addr >= CFG_BAR_NOTIFY_BASE && addr < CFG_BAR_NOTIFY_BASE + CFG_BAR_NOTIFY_SIZE) {
        /* Notify kick: compute queue index and potentially raise MSI-X */
        /* Set ISR bit 0 to indicate a queue interrupt */
        s->isr_status |= 0x1;
        /* Raise MSI-X if vector is configured. Since we only have a single vector
         * global for all queues, we use the stored msix_config? Actually, proper
         * emulation would use per-queue vector, but for this probe stage we skip
         * actual interrupt delivery.
         * If needed, could call msix_notify(&s->parent_obj, vector).
         */
    }
}

static const MemoryRegionOps pcibase_caps_mmio_ops = {
    .read = pcibase_caps_mmio_read,
    .write = pcibase_caps_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* PIO handlers (not used, deleted) */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Reset MBOX state */
    s->epf_rinfo = 0x1;  /* valid non-zero value */
    memset(s->pf_mbox_data, 0, sizeof(s->pf_mbox_data));
    s->pf_mbox_data[0] = OCTEP_FW_READY_SIGNATURE0;
    s->pf_mbox_data[1] = OCTEP_FW_READY_SIGNATURE1;

    /* Reset CAPS state */
    memset(&s->common_cfg, 0, sizeof(s->common_cfg));
    s->common_cfg.num_queues = 1;
    s->common_cfg.config_generation = 1;
    s->dev_features = (1ULL << VIRTIO_F_VERSION_1) |
                      (1ULL << VIRTIO_F_RING_PACKED) |
                      (1ULL << VIRTIO_F_NOTIFICATION_DATA);
    s->isr_status = 0;
    s->notify_off_multiplier = 1;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x177d );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0xb900 );
    pci_config_set_class(pci_conf, 0x020000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize MSI-X with max vectors */
    if (msix_init(pdev, OCTEP_MAX_CB_INTR, NULL, 0, 0, NULL, 0, 0, 0, errp)) {
        return;
    }
    s->has_msix = true;

    /* Add Virtio vendor capabilities in config space */
    {
        int pos;
        uint8_t cap[20];

        /* Common configuration capability */
        memset(cap, 0, sizeof(cap));
        cap[0] = 0x09; /* PCI_CAP_ID_VENDOR_SPECIFIC */
        cap[2] = VIRTIO_PCI_CAP_COMMON_CFG;  /* cfg_type */
        cap[3] = 1;                          /* bar = 1 (CAPS BAR) */
        /* id = 0, padding = 0 */
        *(uint32_t *)(cap + 7) = cpu_to_le32(CFG_BAR_COMMON_BASE); /* offset */
        *(uint32_t *)(cap + 11) = cpu_to_le32(0x38);             /* length */
        pos = pci_add_capability(pdev, 0x09, 0, 16, errp);
        if (pos > 0) {
            for (int i = 0; i < 16; i++) {
                pci_set_byte(pci_conf + pos + i, cap[i]);
            }
        }

        /* Notify capability */
        memset(cap, 0, sizeof(cap));
        cap[0] = 0x09;
        cap[2] = VIRTIO_PCI_CAP_NOTIFY_CFG;
        cap[3] = 1;
        *(uint32_t *)(cap + 7) = cpu_to_le32(CFG_BAR_NOTIFY_BASE);
        *(uint32_t *)(cap + 11) = cpu_to_le32(CFG_BAR_NOTIFY_SIZE);
        /* Multiplier follows the capability structure */
        *(uint32_t *)(cap + 15) = cpu_to_le32(1); /* notify_off_multiplier */
        pos = pci_add_capability(pdev, 0x09, 0, 20, errp);
        if (pos > 0) {
            for (int i = 0; i < 20; i++) {
                pci_set_byte(pci_conf + pos + i, cap[i]);
            }
        }

        /* Device configuration capability */
        memset(cap, 0, sizeof(cap));
        cap[0] = 0x09;
        cap[2] = VIRTIO_PCI_CAP_DEVICE_CFG;
        cap[3] = 1;
        *(uint32_t *)(cap + 7) = cpu_to_le32(CFG_BAR_DEVICE_BASE);
        *(uint32_t *)(cap + 11) = cpu_to_le32(0x100);
        pos = pci_add_capability(pdev, 0x09, 0, 16, errp);
        if (pos > 0) {
            for (int i = 0; i < 16; i++) {
                pci_set_byte(pci_conf + pos + i, cap[i]);
            }
        }

        /* ISR capability */
        memset(cap, 0, sizeof(cap));
        cap[0] = 0x09;
        cap[2] = VIRTIO_PCI_CAP_ISR_CFG;
        cap[3] = 1;
        *(uint32_t *)(cap + 7) = cpu_to_le32(CFG_BAR_ISR_BASE);
        *(uint32_t *)(cap + 11) = cpu_to_le32(1); /* one byte */
        pos = pci_add_capability(pdev, 0x09, 0, 16, errp);
        if (pos > 0) {
            for (int i = 0; i < 16; i++) {
                pci_set_byte(pci_conf + pos + i, cap[i]);
            }
        }

        /* Vendor-specific capability is NOT added because the driver requires
         * octep_vndr_data_process() which is now provided, but we still need
         * the OCTEP_PCI_VNDR_CFG_TYPE_VIRTIO_ID constant to set the id field.
         * Once that is known, a vendor capability with cfg_type 5, bar 1, and
         * the appropriate id/data can be added.
         */
    }

    /* BAR Initialization */
    s->num_bars = 2;

    /* BAR0: MBOX */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x40000;  /* 256KB, enough for highest offset */
    s->bar_info[0].name = "octep-mbox";

    /* BAR1: CAPS */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = CFG_BAR_NOTIFY_BASE + CFG_BAR_NOTIFY_SIZE; /* enough to cover all regions */
    s->bar_info[1].name = "octep-caps";

    /* Register BAR0 with mbox ops, BAR1 with caps ops */
    for (int i = 0; i < s->num_bars; i++) {
        MemoryRegion *mr = &s->bar_regions[i];
        const MemoryRegionOps *ops;
        if (i == 0) {
            ops = &pcibase_mbox_mmio_ops;
        } else {
            ops = &pcibase_caps_mmio_ops;
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, s->bar_info[i].name, pow2ceil(s->bar_info[i].size));
        if (s->bar_info[i].type == BAR_TYPE_MMIO) {
            pci_register_bar(pdev, i, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
        }
        /* other types not used */
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
    .name = "octep_vdpa_pci",
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