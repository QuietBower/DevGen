/*
 * QEMU vp_vdpa PCI device model (QEMU 8.2.10)
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
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
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "vp_vdpa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* virtio_pci_cap flags */
#define VIRTIO_PCI_CAP_COMMON_CFG      1
#define VIRTIO_PCI_CAP_NOTIFY_CFG      2
#define VIRTIO_PCI_CAP_ISR_CFG         3
#define VIRTIO_PCI_CAP_DEVICE_CFG      4
#define VIRTIO_PCI_CAP_PCI_CFG         5

/* packed structs matching kernel UAPI */
typedef struct __attribute__((packed)) {
    uint8_t cap_vndr;
    uint8_t cap_next;
    uint8_t cap_len;
    uint8_t cfg_type;
    uint8_t bar;
    uint8_t id;
    uint8_t padding[2];
    uint32_t offset;
    uint32_t length;
} virtio_pci_cap;

typedef struct __attribute__((packed)) {
    virtio_pci_cap cap;
    uint32_t notify_off_multiplier;
} virtio_pci_notify_cap;

/* Register offsets within the virtio_pci_common_cfg region */
#define VIRTIO_COMMON_CFG_OFFSET       0x0
#define VIRTIO_COMMON_CFG_SIZE         56

/* Mirror of the hardware register layout */
typedef struct VirtIOPCICommonCfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t guest_feature_select;
    uint32_t guest_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;
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
} VirtIOPCICommonCfg;

typedef struct PCIQueueState {
    uint16_t size;
    uint16_t msix_vector;
    uint16_t enable;
    uint16_t notify_off;
    uint32_t desc_lo;
    uint32_t desc_hi;
    uint32_t avail_lo;
    uint32_t avail_hi;
    uint32_t used_lo;
    uint32_t used_hi;
} PCIQueueState;

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

    VirtIOPCICommonCfg common_cfg;
    uint8_t device_status;
    PCIQueueState queues[16];
    uint16_t num_queues;
    uint64_t device_features;
    uint64_t guest_features;
    uint8_t isr_status;
    uint8_t device_cfg[256];
};

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x38) {
        /* Common config area */
        switch (addr) {
        case 0x00:
            val = s->common_cfg.device_feature_select;
            break;
        case 0x04:
            if (s->common_cfg.device_feature_select == 0) {
                val = s->device_features & 0xFFFFFFFFULL;
            } else if (s->common_cfg.device_feature_select == 1) {
                val = (s->device_features >> 32) & 0xFFFFFFFFULL;
            }
            break;
        case 0x08:
            val = s->common_cfg.guest_feature_select;
            break;
        case 0x0C:
            if (s->common_cfg.guest_feature_select == 0) {
                val = s->guest_features & 0xFFFFFFFFULL;
            } else if (s->common_cfg.guest_feature_select == 1) {
                val = (s->guest_features >> 32) & 0xFFFFFFFFULL;
            }
            break;
        case 0x10:
            val = s->common_cfg.msix_config;
            break;
        case 0x12:
            val = s->common_cfg.num_queues;
            break;
        case 0x14:
            val = s->common_cfg.device_status;
            break;
        case 0x15:
            val = s->common_cfg.config_generation;
            break;
        case 0x16:
            val = s->common_cfg.queue_select;
            break;
        case 0x18:
            val = s->common_cfg.queue_size;
            break;
        case 0x1A:
            val = s->common_cfg.queue_msix_vector;
            break;
        case 0x1C:
            val = s->common_cfg.queue_enable;
            break;
        case 0x1E:
            val = s->common_cfg.queue_notify_off;
            break;
        case 0x20:
            val = s->common_cfg.queue_desc_lo;
            break;
        case 0x24:
            val = s->common_cfg.queue_desc_hi;
            break;
        case 0x28:
            val = s->common_cfg.queue_avail_lo;
            break;
        case 0x2C:
            val = s->common_cfg.queue_avail_hi;
            break;
        case 0x30:
            val = s->common_cfg.queue_used_lo;
            break;
        case 0x34:
            val = s->common_cfg.queue_used_hi;
            break;
        default:
            val = 0;
            break;
        }
    } else if (addr >= 0x100 && addr < 0x200) {
        /* Notify area */
        val = 0;
    } else if (addr == 0x200) {
        /* ISR status */
        val = s->isr_status;
        s->isr_status = 0;
    } else if (addr >= 0x300 && addr < 0x400) {
        /* Device-specific config */
        uint32_t offset = addr - 0x300;
        if (offset + size <= 256) {
            switch (size) {
            case 1: val = s->device_cfg[offset]; break;
            case 2: val = lduw_le_p(&s->device_cfg[offset]); break;
            case 4: val = ldl_le_p(&s->device_cfg[offset]); break;
            case 8: val = ldq_le_p(&s->device_cfg[offset]); break;
            default: val = 0; break;
            }
        }
    } else if (addr >= 0x400 && addr < 0x420) {
        /* PCI config capability area */
        val = 0;
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x38) {
        switch (addr) {
        case 0x00:
            s->common_cfg.device_feature_select = val;
            break;
        case 0x04:
            break;
        case 0x08:
            s->common_cfg.guest_feature_select = val;
            break;
        case 0x0C:
            if (s->common_cfg.guest_feature_select == 0) {
                s->guest_features = (s->guest_features & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFFULL);
            } else if (s->common_cfg.guest_feature_select == 1) {
                s->guest_features = (s->guest_features & 0xFFFFFFFFULL) | ((val & 0xFFFFFFFFULL) << 32);
            }
            break;
        case 0x10:
            s->common_cfg.msix_config = val;
            break;
        case 0x12:
            break;
        case 0x14:
            s->common_cfg.device_status = val;
            break;
        case 0x15:
            break;
        case 0x16:
            s->common_cfg.queue_select = val;
            if (val < s->num_queues) {
                s->common_cfg.queue_size = s->queues[val].size;
                s->common_cfg.queue_msix_vector = s->queues[val].msix_vector;
                s->common_cfg.queue_enable = s->queues[val].enable;
                s->common_cfg.queue_notify_off = s->queues[val].notify_off;
                s->common_cfg.queue_desc_lo = s->queues[val].desc_lo;
                s->common_cfg.queue_desc_hi = s->queues[val].desc_hi;
                s->common_cfg.queue_avail_lo = s->queues[val].avail_lo;
                s->common_cfg.queue_avail_hi = s->queues[val].avail_hi;
                s->common_cfg.queue_used_lo = s->queues[val].used_lo;
                s->common_cfg.queue_used_hi = s->queues[val].used_hi;
            } else {
                s->common_cfg.queue_size = 0;
                s->common_cfg.queue_msix_vector = 0;
                s->common_cfg.queue_enable = 0;
                s->common_cfg.queue_notify_off = 0;
                s->common_cfg.queue_desc_lo = 0;
                s->common_cfg.queue_desc_hi = 0;
                s->common_cfg.queue_avail_lo = 0;
                s->common_cfg.queue_avail_hi = 0;
                s->common_cfg.queue_used_lo = 0;
                s->common_cfg.queue_used_hi = 0;
            }
            break;
        case 0x18:
            s->common_cfg.queue_size = val;
            if (s->common_cfg.queue_select < s->num_queues) {
                s->queues[s->common_cfg.queue_select].size = val;
            }
            break;
        case 0x1A:
            s->common_cfg.queue_msix_vector = val;
            if (s->common_cfg.queue_select < s->num_queues) {
                s->queues[s->common_cfg.queue_select].msix_vector = val;
            }
            break;
        case 0x1C:
            s->common_cfg.queue_enable = val;
            if (s->common_cfg.queue_select < s->num_queues) {
                s->queues[s->common_cfg.queue_select].enable = val;
            }
            break;
        case 0x1E:
            break;
        case 0x20:
            s->common_cfg.queue_desc_lo = val;
            if (s->common_cfg.queue_select < s->num_queues) {
                s->queues[s->common_cfg.queue_select].desc_lo = val;
            }
            break;
        case 0x24:
            s->common_cfg.queue_desc_hi = val;
            if (s->common_cfg.queue_select < s->num_queues) {
                s->queues[s->common_cfg.queue_select].desc_hi = val;
            }
            break;
        case 0x28:
            s->common_cfg.queue_avail_lo = val;
            if (s->common_cfg.queue_select < s->num_queues) {
                s->queues[s->common_cfg.queue_select].avail_lo = val;
            }
            break;
        case 0x2C:
            s->common_cfg.queue_avail_hi = val;
            if (s->common_cfg.queue_select < s->num_queues) {
                s->queues[s->common_cfg.queue_select].avail_hi = val;
            }
            break;
        case 0x30:
            s->common_cfg.queue_used_lo = val;
            if (s->common_cfg.queue_select < s->num_queues) {
                s->queues[s->common_cfg.queue_select].used_lo = val;
            }
            break;
        case 0x34:
            s->common_cfg.queue_used_hi = val;
            if (s->common_cfg.queue_select < s->num_queues) {
                s->queues[s->common_cfg.queue_select].used_hi = val;
            }
            break;
        }
    } else if (addr >= 0x100 && addr < 0x200) {
        /* Notify area: any write is a kick; ignore */
    } else if (addr == 0x200) {
        /* ISR status: write ignored */
    } else if (addr >= 0x300 && addr < 0x400) {
        uint32_t offset = addr - 0x300;
        if (offset + size <= 256) {
            switch (size) {
            case 1: s->device_cfg[offset] = val; break;
            case 2: stw_le_p(&s->device_cfg[offset], val); break;
            case 4: stl_le_p(&s->device_cfg[offset], val); break;
            case 8: stq_le_p(&s->device_cfg[offset], val); break;
            }
        }
    } else if (addr >= 0x400 && addr < 0x420) {
        /* PCI config capability area: ignored */
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

    memset(&s->common_cfg, 0, sizeof(s->common_cfg));
    memset(s->queues, 0, sizeof(s->queues));
    s->num_queues = 4;
    s->device_features = (1ULL << 32);
    s->guest_features = 0;
    s->isr_status = 0;
    memset(s->device_cfg, 0, sizeof(s->device_cfg));
    s->common_cfg.num_queues = s->num_queues;
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1AF4);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x1040);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Add VirtIO PCI vendor-specific capabilities */
    {
        int pos;
        virtio_pci_cap cap;
        virtio_pci_notify_cap notify_cap;

        /* Common configuration */
        memset(&cap, 0, sizeof(cap));
        cap.cap_vndr = PCI_CAP_ID_VNDR;
        cap.cap_len = sizeof(cap);
        cap.cfg_type = VIRTIO_PCI_CAP_COMMON_CFG;
        cap.bar = 0;
        cap.offset = 0;
        cap.length = VIRTIO_COMMON_CFG_SIZE;
        pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(cap), errp);
        if (pos > 0) {
            memcpy(pci_conf + pos, &cap, sizeof(cap));
        }

        /* Notify configuration */
        memset(&notify_cap, 0, sizeof(notify_cap));
        notify_cap.cap.cap_vndr = PCI_CAP_ID_VNDR;
        notify_cap.cap.cap_len = sizeof(notify_cap);
        notify_cap.cap.cfg_type = VIRTIO_PCI_CAP_NOTIFY_CFG;
        notify_cap.cap.bar = 0;
        notify_cap.cap.offset = 0x100;
        notify_cap.cap.length = 0x100;
        notify_cap.notify_off_multiplier = 4;
        pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(notify_cap), errp);
        if (pos > 0) {
            memcpy(pci_conf + pos, &notify_cap, sizeof(notify_cap));
        }

        /* ISR configuration */
        memset(&cap, 0, sizeof(cap));
        cap.cap_vndr = PCI_CAP_ID_VNDR;
        cap.cap_len = sizeof(cap);
        cap.cfg_type = VIRTIO_PCI_CAP_ISR_CFG;
        cap.bar = 0;
        cap.offset = 0x200;
        cap.length = 1;
        pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(cap), errp);
        if (pos > 0) {
            memcpy(pci_conf + pos, &cap, sizeof(cap));
        }

        /* Device-specific configuration */
        memset(&cap, 0, sizeof(cap));
        cap.cap_vndr = PCI_CAP_ID_VNDR;
        cap.cap_len = sizeof(cap);
        cap.cfg_type = VIRTIO_PCI_CAP_DEVICE_CFG;
        cap.bar = 0;
        cap.offset = 0x300;
        cap.length = 0x100;
        pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(cap), errp);
        if (pos > 0) {
            memcpy(pci_conf + pos, &cap, sizeof(cap));
        }

        /* PCI configuration access */
        memset(&cap, 0, sizeof(cap));
        cap.cap_vndr = PCI_CAP_ID_VNDR;
        cap.cap_len = sizeof(cap);
        cap.cfg_type = VIRTIO_PCI_CAP_PCI_CFG;
        cap.bar = 0;
        cap.offset = 0x400;
        cap.length = 0x20;
        pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(cap), errp);
        if (pos > 0) {
            memcpy(pci_conf + pos, &cap, sizeof(cap));
        }
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 64 * KiB, .name = "vp-vdpa-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = 8 * KiB, .name = "vp-vdpa-msix" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    if (msix_init_exclusive_bar(pdev, 16, 1, errp)) {
        error_propagate(errp, NULL);
        return;
    }
    s->has_msix = true;
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
    .name = "vp_vdpa_pci",
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
