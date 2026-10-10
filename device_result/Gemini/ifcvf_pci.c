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
struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t guest_feature_select;
    uint32_t guest_feature;
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

struct virtio_pci_cap {
    uint8_t cap_vndr;
    uint8_t cap_next;
    uint8_t cap_len;
    uint8_t cfg_type;
    uint8_t bar;
    uint8_t id;
    uint8_t padding[2];
    uint32_t offset;
    uint32_t length;
};

struct ifcvf_lm_cfg {
    uint64_t control;
    uint64_t status;
    uint64_t lm_mem_log_start_addr;
    uint64_t lm_mem_log_end_addr;
    uint16_t vq_state_region;
};

#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4
#define VIRTIO_F_ACCESS_PLATFORM    33

#define ETH_ALEN 6

struct virtio_net_config {
    uint8_t mac[ETH_ALEN];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
    uint16_t mtu;
    uint32_t speed;
    uint8_t duplex;
    uint8_t rss_max_key_size;
    uint16_t rss_max_indirection_table_length;
    uint32_t supported_hash_types;
};

struct virtio_blk_geometry {
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;
};

struct virtio_blk_zoned_characteristics {
    uint32_t zone_sectors;
    uint32_t max_open_zones;
    uint32_t max_active_zones;
    uint32_t max_append_sectors;
    uint32_t write_granularity;
    uint8_t model;
    uint8_t unused2[3];
};

struct virtio_blk_config {
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;
    struct virtio_blk_geometry geometry;
    uint32_t blk_size;
    uint8_t physical_block_exp;
    uint8_t alignment_offset;
    uint16_t min_io_size;
    uint32_t opt_io_size;
    uint8_t wce;
    uint8_t unused;
    uint16_t num_queues;
    uint32_t max_discard_sectors;
    uint32_t max_discard_seg;
    uint32_t discard_sector_alignment;
    uint32_t max_write_zeroes_sectors;
    uint32_t max_write_zeroes_seg;
    uint8_t write_zeroes_may_unmap;
    uint8_t unused1[3];
    uint32_t max_secure_erase_sectors;
    uint32_t max_secure_erase_seg;
    uint32_t secure_erase_sector_alignment;
    struct virtio_blk_zoned_characteristics zoned;
};

#define VIRTIO_ID_NET 1
#define VIRTIO_DEV_ANY_ID 0xffffffff
#define VIRTIO_ID_BLOCK 2
#define VIRTIO_TRANS_ID_NET 0x1000
#define VIRTIO_TRANS_ID_BLOCK 0x1001
#define PCI_VENDOR_ID_REDHAT_QUMRANET 0x1af4

#define TYPE_PCIBASE_DEVICE "ifcvf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define N3000_SUBSYS_DEVICE_ID 0x001A
#define N3000_DEVICE_ID 0x1041
#define IFCVF_LM_BAR 4
#define IFCVF_PCI_MAX_RESOURCE 6
#define IFCVF_MIN_VQ_SIZE 64
#define IFCVF_QUEUE_ALIGNMENT 4096
#define MSIX_VECTOR_PER_VQ_AND_CONFIG 1
#define MSIX_VECTOR_DEV_SHARED 3
#define MSIX_VECTOR_SHARED_VQ_AND_CONFIG 2

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
    uint8_t msix_vector_status;
    uint32_t num_msix_vectors;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t notify_bar;
    uint32_t config_size;
    uint32_t notify_off_multiplier;
    uint32_t dev_type;
    uint64_t hw_features;
    uint64_t dev_features;
    uint16_t nr_vring;
    uint32_t cap_dev_config_size;
    
    uint32_t device_feature_select;
    uint16_t queue_select;

    /* Live migration config */
    uint64_t lm_control;
    uint64_t lm_status;
    uint64_t lm_mem_log_start_addr;
    uint64_t lm_mem_log_end_addr;
    uint16_t lm_vq_state_region;

    /* DMA Context */
    struct {
        uint16_t last_avail_idx;
        uint64_t notify_pa;
        uint32_t irq;
    } vring[IFCVF_MIN_VQ_SIZE];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 0x1000) {
        switch (addr) {
            case offsetof(struct virtio_pci_common_cfg, device_feature_select):
                return s->device_feature_select;
            case offsetof(struct virtio_pci_common_cfg, device_feature):
                if (s->device_feature_select == 0) return (uint32_t)s->hw_features;
                if (s->device_feature_select == 1) return (uint32_t)(s->hw_features >> 32);
                return 0;
            case offsetof(struct virtio_pci_common_cfg, num_queues):
                return s->nr_vring;
            case offsetof(struct virtio_pci_common_cfg, queue_select):
                return s->queue_select;
            case offsetof(struct virtio_pci_common_cfg, queue_size):
                return IFCVF_MIN_VQ_SIZE;
            case offsetof(struct virtio_pci_common_cfg, queue_notify_off):
                return s->queue_select;
        }
    }
    return 0;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 0x1000) {
        switch (addr) {
            case offsetof(struct virtio_pci_common_cfg, device_feature_select):
                s->device_feature_select = val;
                break;
            case offsetof(struct virtio_pci_common_cfg, queue_select):
                s->queue_select = val;
                break;
        }
    }
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t pcibase_bar4_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_bar4_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_bar4_ops = {
    .read = pcibase_bar4_read,
    .write = pcibase_bar4_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

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
    s->device_feature_select = 0;
    s->queue_select = 0;
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
        const MemoryRegionOps *ops = &pcibase_bar0_ops;
        if (bi->index == 2) ops = &pcibase_bar2_ops;
        if (bi->index == 4) ops = &pcibase_bar4_ops;
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void add_virtio_pci_cap(PCIDevice *pdev, uint8_t cfg_type, uint8_t bar, uint32_t offset, uint32_t length, uint32_t mult)
{
    uint8_t size = (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) ? 20 : 16;
    int pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, size, NULL);
    if (pos < 0) {
        return;
    }

    pci_set_byte(pdev->config + pos + 2, size);
    pci_set_byte(pdev->config + pos + 3, cfg_type);
    pci_set_byte(pdev->config + pos + 4, bar);
    pci_set_byte(pdev->config + pos + 5, 0); /* id */
    pci_set_long(pdev->config + pos + 8, offset);
    pci_set_long(pdev->config + pos + 12, length);

    if (cfg_type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
        pci_set_long(pdev->config + pos + 16, mult);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1af4 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  N3000_DEVICE_ID );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, N3000_SUBSYS_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    add_virtio_pci_cap(pdev, VIRTIO_PCI_CAP_COMMON_CFG, 0, 0x0000, 0x1000, 0);
    add_virtio_pci_cap(pdev, VIRTIO_PCI_CAP_ISR_CFG,    0, 0x1000, 0x1000, 0);
    add_virtio_pci_cap(pdev, VIRTIO_PCI_CAP_DEVICE_CFG, 0, 0x2000, sizeof(struct virtio_net_config), 0);
    add_virtio_pci_cap(pdev, VIRTIO_PCI_CAP_NOTIFY_CFG, 2, 0x0000, 0x1000, 0x1000);

    s->nr_vring = 2;
    s->hw_features = (1ULL << VIRTIO_F_ACCESS_PLATFORM);

    /* BAR Initialization */
    s->num_bars = IFCVF_PCI_MAX_RESOURCE;  
    
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x3000;
    s->bar_info[0].name = "bar0";

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x1000;
    s->bar_info[2].name = "bar2";

    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_MMIO;
    s->bar_info[4].size = 0x1000;
    s->bar_info[4].name = "bar4";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;  
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
