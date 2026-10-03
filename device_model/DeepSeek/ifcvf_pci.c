/*
 * QEMU IFCVF PCI Device Model for ifcvf_main.c driver
 *
 * Emulates a VirtIO PCI device (network type, device_id=0x1041, vendor=0x1af4)
 * with three MMIO BARs: BAR0 (device config), BAR2 (ISR+notify), BAR4 (common config).
 * BAR1 holds the MSI-X table and PBA for QEMU's msix_init.
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

/* Include standard VirtIO headers available in QEMU */
#include "standard-headers/linux/virtio_pci.h"
#include "standard-headers/linux/virtio_net.h"
#include "standard-headers/linux/virtio_config.h"

#define TYPE_PCIBASE_DEVICE "ifcvf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware Identifiers */
#define VENDOR_ID 0x1af4
#define DEVICE_ID 0x1041
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET
#define MSIX_MAX_VECTORS 16

/* VirtIO PCI capability types */
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

/* Maximum number of virtqueues */
#define MAX_VQ 32

/* Page size for notify multiplier fallback */
#define IFCVF_PAGE_SIZE 4096

/* Common configuration register offsets (Virtio 1.0 spec) */
#define CFG_DEVICE_FEATURE_SELECT  0x0
#define CFG_DEVICE_FEATURE         0x4
#define CFG_DRIVER_FEATURE_SELECT  0x8
#define CFG_DRIVER_FEATURE         0xC
#define CFG_MSIX_CONFIG            0x10
#define CFG_NUM_QUEUES             0x12
#define CFG_DEVICE_STATUS          0x14
#define CFG_CONFIG_GENERATION      0x15
#define CFG_QUEUE_SELECT           0x16
#define CFG_QUEUE_SIZE             0x18
#define CFG_QUEUE_MSIX_VECTOR      0x1A
#define CFG_QUEUE_ENABLE           0x1C
#define CFG_QUEUE_NOTIFY_OFF       0x1E
#define CFG_QUEUE_DESC_LO          0x20
#define CFG_QUEUE_DESC_HI          0x24
#define CFG_QUEUE_AVAIL_LO         0x28
#define CFG_QUEUE_AVAIL_HI         0x2C
#define CFG_QUEUE_USED_LO          0x30
#define CFG_QUEUE_USED_HI          0x34

/* Structure for live migration cfg as defined in driver */
struct ifcvf_lm_cfg {
    uint64_t control;
    uint64_t status;
    uint64_t lm_mem_log_start_addr;
    uint64_t lm_mem_log_end_addr;
    uint16_t vq_state_region;
};

/* Per virtqueue state */
typedef struct VringState {
    bool ready;
    uint32_t num;
    uint64_t desc;
    uint64_t avail;
    uint64_t used;
    uint16_t msix_vector;
    uint16_t notify_off;
} VringState;

/* Main device state */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];
    int num_bars;

    /* Interrupt state */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* VirtIO common config shadow */
    struct virtio_pci_common_cfg common_cfg;
    uint16_t queue_select_index;

    /* Per virtqueue data */
    VringState vrings[MAX_VQ];
    uint16_t num_vqs;

    /* Feature negotiation */
    uint64_t device_features;
    uint64_t driver_features;
    uint32_t feature_select; /* 0 or 1 for device-feature access */

    /* Device-specific configuration (network) */
    struct virtio_net_config net_config;

    /* ISR byte (write-1-to-clear is not used; ISR clears on read) */
    uint8_t isr;

    /* Notification region layout */
    uint32_t notify_off_multiplier; /* 0 means use PAGE_SIZE */
    hwaddr notify_base;             /* offset within BAR2 */

    /* MSI-X vector for config changes */
    uint16_t config_msix_vector;

    /* Live migration (unused) */
    struct ifcvf_lm_cfg lm_cfg;
    uint8_t device_status;
    uint16_t nr_vring;
    uint64_t features;
    uint32_t num_msix_vectors;
    bool reset;
};

/* ------------------------------------------------------------------ */
/* MMIO handlers for each BAR */

/* BAR0: Device-specific configuration (virtio_net_config) */
static uint64_t pcibase_device_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint8_t *config = (uint8_t *)&s->net_config;

    if (addr + size > sizeof(s->net_config)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read beyond config space at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return 0;
    }

    memcpy(&val, config + addr, MIN(size, sizeof(val)));
    return val;
}

static void pcibase_device_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *config = (uint8_t *)&s->net_config;

    if (addr + size > sizeof(s->net_config)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write beyond config space at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return;
    }

    memcpy(config + addr, &val, size);
    /* Increment config generation after any write */
    s->common_cfg.config_generation++;
}

static const MemoryRegionOps pcibase_device_ops = {
    .read = pcibase_device_read,
    .write = pcibase_device_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* BAR2: ISR (read at offset 0) + notification area (writes at higher offsets) */
static uint64_t pcibase_isr_notify_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == 0 && size == 1) {
        uint8_t val = s->isr;
        s->isr = 0; /* clear on read */
        return val;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: unexpected read at 0x%" HWADDR_PRIx " size %u\n",
                  __func__, addr, size);
    return 0;
}

static void pcibase_isr_notify_write(void *opaque, hwaddr addr, uint64_t val,
                                      unsigned size)
{
    PCIBaseState *s = opaque;

    /* Writes to the notification area are queue kicks */
    if (addr >= s->notify_base) {
        hwaddr off = addr - s->notify_base;
        uint32_t multiplier = s->notify_off_multiplier;
        uint16_t qid;

        if (multiplier == 0) {
            multiplier = IFCVF_PAGE_SIZE;
        }

        qid = off / multiplier;

        if (qid < s->num_vqs) {
            /* Queue notification: nothing to do in probe phase, just log */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: queue %u kicked (val 0x%" PRIx64 ")\n",
                          __func__, qid, val);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: kick for invalid queue %u (offset 0x%" HWADDR_PRIx ")\n",
                          __func__, qid, addr);
        }
        return;
    }

    if (addr == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to ISR ignored (read-only)\n", __func__);
        return;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: unexpected write at 0x%" HWADDR_PRIx " size %u\n",
                  __func__, addr, size);
}

static const MemoryRegionOps pcibase_isr_notify_ops = {
    .read = pcibase_isr_notify_read,
    .write = pcibase_isr_notify_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* BAR4: Common configuration */
static uint64_t pcibase_common_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *cfg = (uint8_t *)&s->common_cfg;
    uint64_t val = 0;

    if (addr + size > sizeof(s->common_cfg)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read beyond common_cfg at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return 0;
    }

    /* Virtio 1.0 common config fields (see virtio_pci_common_cfg) */
    /* Some fields require special handling */
    switch (addr) {
    case CFG_DEVICE_FEATURE_SELECT:
        /* 32-bit read */
        if (size == 4) {
            return s->feature_select;
        }
        break;
    case CFG_DEVICE_FEATURE:
        /* 32-bit, returns feature bits depending on feature_select */
        if (size == 4) {
            if (s->feature_select == 0) {
                return s->device_features & 0xFFFFFFFF;
            } else if (s->feature_select == 1) {
                return s->device_features >> 32;
            }
        }
        break;
    case CFG_DRIVER_FEATURE_SELECT:
        if (size == 4) {
            return s->feature_select;
        }
        break;
    case CFG_DRIVER_FEATURE:
        if (size == 4) {
            if (s->feature_select == 0) {
                return s->driver_features & 0xFFFFFFFF;
            } else if (s->feature_select == 1) {
                return s->driver_features >> 32;
            }
        }
        break;
    case CFG_MSIX_CONFIG:
        if (size == 2) {
            return s->config_msix_vector;
        }
        break;
    case CFG_NUM_QUEUES:
        if (size == 2) {
            return s->num_vqs;
        }
        break;
    case CFG_DEVICE_STATUS:
        if (size == 1) {
            return s->device_status;
        }
        break;
    case CFG_CONFIG_GENERATION:
        if (size == 1) {
            return s->common_cfg.config_generation;
        }
        break;
    case CFG_QUEUE_SELECT:
        if (size == 2) {
            return s->queue_select_index;
        }
        break;
    case CFG_QUEUE_SIZE:
        if (size == 2 && s->queue_select_index < s->num_vqs) {
            return s->vrings[s->queue_select_index].num;
        }
        break;
    case CFG_QUEUE_MSIX_VECTOR:
        if (size == 2 && s->queue_select_index < s->num_vqs) {
            return s->vrings[s->queue_select_index].msix_vector;
        }
        break;
    case CFG_QUEUE_ENABLE:
        if (size == 2 && s->queue_select_index < s->num_vqs) {
            return s->vrings[s->queue_select_index].ready ? 1 : 0;
        }
        break;
    case CFG_QUEUE_NOTIFY_OFF:
        if (size == 2 && s->queue_select_index < s->num_vqs) {
            return s->vrings[s->queue_select_index].notify_off;
        }
        break;
    case CFG_QUEUE_DESC_LO:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            return (uint32_t)s->vrings[s->queue_select_index].desc;
        }
        break;
    case CFG_QUEUE_DESC_HI:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            return (uint32_t)(s->vrings[s->queue_select_index].desc >> 32);
        }
        break;
    case CFG_QUEUE_AVAIL_LO:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            return (uint32_t)s->vrings[s->queue_select_index].avail;
        }
        break;
    case CFG_QUEUE_AVAIL_HI:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            return (uint32_t)(s->vrings[s->queue_select_index].avail >> 32);
        }
        break;
    case CFG_QUEUE_USED_LO:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            return (uint32_t)s->vrings[s->queue_select_index].used;
        }
        break;
    case CFG_QUEUE_USED_HI:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            return (uint32_t)(s->vrings[s->queue_select_index].used >> 32);
        }
        break;
    }

    /* General fallback: read raw bytes */
    memcpy(&val, cfg + addr, MIN(size, sizeof(val)));
    return val;
}

static void pcibase_common_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > sizeof(s->common_cfg)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write beyond common_cfg at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return;
    }

    switch (addr) {
    case CFG_DEVICE_FEATURE_SELECT:
        if (size == 4) {
            s->feature_select = val;
        }
        break;
    case CFG_DRIVER_FEATURE_SELECT:
        if (size == 4) {
            s->feature_select = val;
        }
        break;
    case CFG_DRIVER_FEATURE:
        if (size == 4) {
            uint64_t mask;
            if (s->feature_select == 0) {
                mask = 0xFFFFFFFF;
            } else if (s->feature_select == 1) {
                mask = 0xFFFFFFFF00000000ULL;
            } else {
                break;
            }
            s->driver_features = (s->driver_features & ~mask) |
                                  ((uint64_t)val << (32 * s->feature_select));
        }
        break;
    case CFG_MSIX_CONFIG:
        if (size == 2) {
            s->config_msix_vector = val;
        }
        break;
    case CFG_DEVICE_STATUS:
        if (size == 1) {
            s->device_status = val;
        }
        break;
    case CFG_QUEUE_SELECT:
        if (size == 2) {
            s->queue_select_index = val;
        }
        break;
    case CFG_QUEUE_SIZE:
        if (size == 2 && s->queue_select_index < s->num_vqs) {
            s->vrings[s->queue_select_index].num = val;
        }
        break;
    case CFG_QUEUE_MSIX_VECTOR:
        if (size == 2 && s->queue_select_index < s->num_vqs) {
            s->vrings[s->queue_select_index].msix_vector = val;
        }
        break;
    case CFG_QUEUE_ENABLE:
        if (size == 2 && s->queue_select_index < s->num_vqs) {
            s->vrings[s->queue_select_index].ready = (val != 0);
        }
        break;
    case CFG_QUEUE_DESC_LO:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            s->vrings[s->queue_select_index].desc = (s->vrings[s->queue_select_index].desc & 0xFFFFFFFF00000000ULL) | (uint32_t)val;
        }
        break;
    case CFG_QUEUE_DESC_HI:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            s->vrings[s->queue_select_index].desc = (s->vrings[s->queue_select_index].desc & 0xFFFFFFFF) | ((uint64_t)val << 32);
        }
        break;
    case CFG_QUEUE_AVAIL_LO:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            s->vrings[s->queue_select_index].avail = (s->vrings[s->queue_select_index].avail & 0xFFFFFFFF00000000ULL) | (uint32_t)val;
        }
        break;
    case CFG_QUEUE_AVAIL_HI:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            s->vrings[s->queue_select_index].avail = (s->vrings[s->queue_select_index].avail & 0xFFFFFFFF) | ((uint64_t)val << 32);
        }
        break;
    case CFG_QUEUE_USED_LO:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            s->vrings[s->queue_select_index].used = (s->vrings[s->queue_select_index].used & 0xFFFFFFFF00000000ULL) | (uint32_t)val;
        }
        break;
    case CFG_QUEUE_USED_HI:
        if (size == 4 && s->queue_select_index < s->num_vqs) {
            s->vrings[s->queue_select_index].used = (s->vrings[s->queue_select_index].used & 0xFFFFFFFF) | ((uint64_t)val << 32);
        }
        break;
    default:
        /* Guest should not write to read-only fields; ignore */
        break;
    }
}

static const MemoryRegionOps pcibase_common_ops = {
    .read = pcibase_common_read,
    .write = pcibase_common_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* ------------------------------------------------------------------ */
/* Device lifecycle */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->common_cfg, 0, sizeof(s->common_cfg));
    s->feature_select = 0;
    s->driver_features = 0;
    s->config_msix_vector = VIRTIO_MSI_NO_VECTOR;
    s->device_status = 0;
    s->isr = 0;
    s->queue_select_index = 0;

    for (int i = 0; i < s->num_vqs; i++) {
        s->vrings[i].ready = false;
        s->vrings[i].num = 0;
        s->vrings[i].desc = 0;
        s->vrings[i].avail = 0;
        s->vrings[i].used = 0;
        s->vrings[i].msix_vector = VIRTIO_MSI_NO_VECTOR;
        s->vrings[i].notify_off = i; /* default: each queue gets its own offset */
    }

    s->common_cfg.config_generation = 1;
    s->common_cfg.num_queues = s->num_vqs;

    /* network config defaults */
    memset(&s->net_config, 0, sizeof(s->net_config));

    s->reset = true;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int cap_off;
    Error *local_err = NULL;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Make it a PCI Express endpoint */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Add Power Management capability */
    cap_off = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (cap_off > 0) {
        pci_set_word(pci_conf + cap_off + PCI_PM_PMC, 0x0003);
    }

    /* Add VirtIO PCI capabilities (vendor-specific) */
    /* Common configuration: BAR4, offset 0 */
    cap_off = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, 16, &local_err);
    if (cap_off < 0) {
        error_propagate(errp, local_err);
        return;
    }
    pci_set_byte(pci_conf + cap_off + 3, VIRTIO_PCI_CAP_COMMON_CFG);  /* cfg_type */
    pci_set_byte(pci_conf + cap_off + 4, 4);                           /* bar = 4 */
    pci_set_long(pci_conf + cap_off + 8, 0);                           /* offset */
    pci_set_long(pci_conf + cap_off + 12, sizeof(struct virtio_pci_common_cfg)); /* length */

    /* ISR configuration: BAR2, offset 0 */
    cap_off = pci_add_capability(pdev, PCI_CAP_ID_VNDR, cap_off, 16, &local_err);
    if (cap_off < 0) {
        error_propagate(errp, local_err);
        return;
    }
    pci_set_byte(pci_conf + cap_off + 3, VIRTIO_PCI_CAP_ISR_CFG);
    pci_set_byte(pci_conf + cap_off + 4, 2);
    pci_set_long(pci_conf + cap_off + 8, 0);
    pci_set_long(pci_conf + cap_off + 12, 1);  /* length = 1 byte */

    /* Device configuration: BAR0, offset 0 */
    cap_off = pci_add_capability(pdev, PCI_CAP_ID_VNDR, cap_off, 16, &local_err);
    if (cap_off < 0) {
        error_propagate(errp, local_err);
        return;
    }
    pci_set_byte(pci_conf + cap_off + 3, VIRTIO_PCI_CAP_DEVICE_CFG);
    pci_set_byte(pci_conf + cap_off + 4, 0);
    pci_set_long(pci_conf + cap_off + 8, 0);
    pci_set_long(pci_conf + cap_off + 12, sizeof(struct virtio_net_config));

    /* Notification configuration: BAR2, offset 0x1000, length = BAR2_size - notify_base */
    s->notify_base = 0x1000;
    cap_off = pci_add_capability(pdev, PCI_CAP_ID_VNDR, cap_off, 16, &local_err);
    if (cap_off < 0) {
        error_propagate(errp, local_err);
        return;
    }
    pci_set_byte(pci_conf + cap_off + 3, VIRTIO_PCI_CAP_NOTIFY_CFG);
    pci_set_byte(pci_conf + cap_off + 4, 2);
    pci_set_long(pci_conf + cap_off + 8, s->notify_base);
    pci_set_long(pci_conf + cap_off + 12, 0x3000); /* e.g. 3 pages for 3 queues */

    /* Set number of virtqueues (num_queues) */
    s->num_vqs = 2; /* default: one TX, one RX */

    /* BAR allocation */
    /* BAR0: device-specific config (MMIO) */
    s->num_bars = 5;
    MemoryRegion *mr;
    mr = &s->bar_regions[0];
    memory_region_init_io(mr, OBJECT(s), &pcibase_device_ops, s, "ifcvf-devcfg", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);

    /* BAR1: MSI-X table and PBA (no guest access) */
    mr = &s->bar_regions[1];
    memory_region_init_io(mr, OBJECT(s), NULL, NULL, "ifcvf-msix", 0x1000);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);

    /* BAR2: ISR + notification */
    mr = &s->bar_regions[2];
    memory_region_init_io(mr, OBJECT(s), &pcibase_isr_notify_ops, s, "ifcvf-isr-notify", 0x4000);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);

    /* BAR3: unused */
    /* BAR4: common configuration */
    mr = &s->bar_regions[4];
    memory_region_init_io(mr, OBJECT(s), &pcibase_common_ops, s, "ifcvf-common", 0x1000);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);

    /* MSI-X initialization (uses BAR1) */
    if (msix_init(pdev, MSIX_MAX_VECTORS,
                  &s->bar_regions[1], /* table bar */
                  0, 0,               /* table offset, bir */
                  &s->bar_regions[1], /* PBA bar */
                  0, 0,               /* PBA offset, bir */
                  0, errp)) {
        error_setg(errp, "MSI-X initialization failed");
        return;
    }

    s->has_msix = true;

    /* Device features for a network card */
    s->device_features = (1ULL << VIRTIO_F_VERSION_1) |
                         (1ULL << VIRTIO_F_ACCESS_PLATFORM) |
                         (1ULL << VIRTIO_NET_F_MAC) |
                         (1ULL << VIRTIO_NET_F_STATUS);

    /* Notify multiplier: 0 means PAGE_SIZE per queue */
    s->notify_off_multiplier = 0;

    /* Reset the device to initial state */
    device_cold_reset(DEVICE(pdev)); /* calls pcibase_reset */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* BARs are cleaned up by QEMU */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ifcvf_pci",
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