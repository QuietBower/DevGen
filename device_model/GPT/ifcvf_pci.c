/*
 * IFCVF vDPA PCI Device Emulation (minimal skeleton for driver probing)
 *
 * NOTE:
 *  - This model only implements enough behavior for the Linux ifcvf vDPA
 *    driver to probe, initialize, and bind.
 *  - The full hw behavior (virtqueues, config space semantics, etc.) is
 *    implemented in the kernel's ifcvf_hw layer, which is not available
 *    here; therefore this model only exposes generic PCI resources and a
 *    dummy MSI-X capability so the driver can set up IRQ vectors.
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
#include "standard-headers/linux/virtio_ids.h"

#define TYPE_PCIBASE_DEVICE "ifcvf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_REDHAT_QUMRANET    0x1af4
#define PCI_CLASS_OTHERS                 0xff

#define IFCVF_PCI_VENDOR_ID   PCI_VENDOR_ID_REDHAT_QUMRANET
#define IFCVF_PCI_DEVICE_ID   0x1041
#define IFCVF_PCI_CLASS_ID    PCI_CLASS_OTHERS

#define N3000_DEVICE_ID       0x1041
#define N3000_SUBSYS_DEVICE_ID 0x001A
#define IFCVF_LM_BAR          4
#define IFCVF_PCI_MAX_RESOURCE 6

/* Minimal virtio-pci common configuration layout needed by ifcvf_hw helpers.
 * Offsets are chosen to match the standard virtio 1.0 PCI common config
 * structure insofar as it is accessed by the driver code we see.
 */

typedef struct VirtioPciCommonCfg {
    uint32_t device_feature_select;   /* 0x00 */
    uint32_t device_feature;          /* 0x04 */
    uint32_t guest_feature_select;    /* 0x08 */
    uint32_t guest_feature;           /* 0x0c */
    uint16_t msix_config;             /* 0x10 */
    uint16_t num_queues;              /* 0x12 */
    uint8_t  device_status;           /* 0x14 */
    uint8_t  config_generation;       /* 0x15 */
    uint16_t queue_select;            /* 0x16 */
    uint16_t queue_size;              /* 0x18 */
    uint16_t queue_msix_vector;       /* 0x1a */
    uint16_t queue_enable;            /* 0x1c */
    uint16_t queue_notify_off;        /* 0x1e */
    uint64_t queue_desc;              /* 0x20 */
    uint64_t queue_avail;             /* 0x28 */
    uint64_t queue_used;              /* 0x30 */
} VirtioPciCommonCfg;

/* Simple vendor capability header as used by the driver via ifcvf_read_config_range */

typedef struct VirtioPciCap {
    uint8_t cap_vndr;      /* PCI_CAP_ID_VNDR */
    uint8_t cap_next;
    uint8_t cap_len;
    uint8_t cfg_type;
    uint8_t bar;
    uint8_t padding[3];
    uint32_t offset;
    uint32_t length;
} VirtioPciCap;

/* cfg_type values from virtio-pci */
#ifndef VIRTIO_PCI_CAP_COMMON_CFG
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#endif
#ifndef VIRTIO_PCI_CAP_NOTIFY_CFG
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#endif
#ifndef VIRTIO_PCI_CAP_ISR_CFG
#define VIRTIO_PCI_CAP_ISR_CFG      3
#endif
#ifndef VIRTIO_PCI_CAP_DEVICE_CFG
#define VIRTIO_PCI_CAP_DEVICE_CFG   4
#endif

#define VIRTIO_PCI_ISR_CONFIG 0x2

/* Register layout in our BARs */
#define IFCVF_BAR_SIZE              0x1000

/* Place the common_cfg in BAR0 at offset 0 */
#define IFCVF_COMMON_CFG_BAR        0
#define IFCVF_COMMON_CFG_OFFSET     0x000

/* Notify capability will point into BAR2; multiplier stored in PCI config */
#define IFCVF_NOTIFY_BAR            2
#define IFCVF_NOTIFY_OFFSET         0x000
#define IFCVF_NOTIFY_MULT_DEFAULT   4

/* ISR capability in BAR0 */
#define IFCVF_ISR_BAR               0
#define IFCVF_ISR_OFFSET            0x100

/* Device config capability in BAR4 */
#define IFCVF_DEV_CFG_BAR           4
#define IFCVF_DEV_CFG_OFFSET        0x000
#define IFCVF_DEV_CFG_SIZE          0x100

/* LM config in BAR4 (separate region for vq_state_region in ifcvf_lm_cfg) */
#define IFCVF_LM_OFFSET             0x200
#define IFCVF_LM_REGION_SIZE        0x200

/* For simplicity expose a small, fixed number of queues */
#define IFCVF_NUM_QUEUES            8

#define BAR_TYPE_NONE 0
#define BAR_TYPE_MMIO 1
#define BAR_TYPE_PIO  2
#define BAR_TYPE_RAM  3

typedef struct {
    int     index;    /* BAR index 0-5 */
    int     type;
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

    /* Backing storage for BARs */
    MemoryRegion bar0_mem;
    uint8_t *bar0_buf;
    MemoryRegion bar2_mem;
    uint8_t *bar2_buf;
    MemoryRegion bar4_mem;
    uint8_t *bar4_buf;

    /* Pointers into BAR windows for convenience */
    VirtioPciCommonCfg *common_cfg;
    uint8_t *isr_status;
    uint8_t *dev_cfg;
    uint8_t *lm_region;

    /* Feature negotiation state */
    uint64_t device_features;
    uint64_t driver_features;

    /* VQ notify addresses (for vp_iowrite16 via notify_addr) */
    uint64_t vring_notify_offset[IFCVF_NUM_QUEUES];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

static MemoryRegion *pcibase_get_bar_region(PCIBaseState *s, int bar)
{
    switch (bar) {
    case 0:
        return &s->bar0_mem;
    case 2:
        return &s->bar2_mem;
    case 4:
        return &s->bar4_mem;
    default:
        return NULL;
    }
}

static uint8_t *pcibase_get_bar_buf(PCIBaseState *s, int bar)
{
    switch (bar) {
    case 0:
        return s->bar0_buf;
    case 2:
        return s->bar2_buf;
    case 4:
        return s->bar4_buf;
    default:
        return NULL;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Determine which BAR this MMIO belongs to based on region */
    /* BAR0 */
    if (memory_region_is_mapped(&s->bar0_mem) &&
        addr < IFCVF_BAR_SIZE) {
        uint32_t off = addr;
        if (s->common_cfg && off >= IFCVF_COMMON_CFG_OFFSET &&
            off < IFCVF_COMMON_CFG_OFFSET + sizeof(VirtioPciCommonCfg)) {
            uint8_t *p = (uint8_t *)s->common_cfg + (off - IFCVF_COMMON_CFG_OFFSET);
            memcpy(&val, p, size);
            return val;
        }
        if (s->isr_status && off == IFCVF_ISR_OFFSET && size == 1) {
            /* Reading ISR clears it per virtio-pci spec */
            val = *s->isr_status;
            *s->isr_status = 0;
            return val;
        }
        /* Fallback raw read from BAR0 backing */
        if (s->bar0_buf) {
            memcpy(&val, s->bar0_buf + off, size);
            return val;
        }
    }

    /* BAR2 notify area: driver only writes here, no reads expected */
    if (memory_region_is_mapped(&s->bar2_mem) &&
        addr >= IFCVF_BAR_SIZE && addr < 2 * IFCVF_BAR_SIZE) {
        uint32_t off = addr - IFCVF_BAR_SIZE;
        if (s->bar2_buf) {
            memcpy(&val, s->bar2_buf + off, size);
            return val;
        }
    }

    /* BAR4: device config and LM region */
    if (memory_region_is_mapped(&s->bar4_mem) &&
        addr >= 2 * IFCVF_BAR_SIZE && addr < 3 * IFCVF_BAR_SIZE) {
        uint32_t off = addr - 2 * IFCVF_BAR_SIZE;
        if (s->dev_cfg && off >= IFCVF_DEV_CFG_OFFSET &&
            off < IFCVF_DEV_CFG_OFFSET + IFCVF_DEV_CFG_SIZE) {
            uint8_t *p = s->dev_cfg + (off - IFCVF_DEV_CFG_OFFSET);
            memcpy(&val, p, size);
            return val;
        }
        if (s->lm_region && off >= IFCVF_LM_OFFSET &&
            off < IFCVF_LM_OFFSET + IFCVF_LM_REGION_SIZE) {
            uint8_t *p = s->lm_region + (off - IFCVF_LM_OFFSET);
            memcpy(&val, p, size);
            return val;
        }
        if (s->bar4_buf) {
            memcpy(&val, s->bar4_buf + off, size);
            return val;
        }
    }

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* BAR0 */
    if (memory_region_is_mapped(&s->bar0_mem) &&
        addr < IFCVF_BAR_SIZE) {
        uint32_t off = addr;
        if (s->common_cfg && off >= IFCVF_COMMON_CFG_OFFSET &&
            off < IFCVF_COMMON_CFG_OFFSET + sizeof(VirtioPciCommonCfg)) {
            uint8_t *p = (uint8_t *)s->common_cfg + (off - IFCVF_COMMON_CFG_OFFSET);
            memcpy(p, &val, size);
            return;
        }
        /* Writes to ISR are not expected from driver; ignore */
        if (s->bar0_buf) {
            memcpy(s->bar0_buf + off, &val, size);
            return;
        }
    }

    /* BAR2 notify area: driver writes queue id here via ifcvf_notify_queue */
    if (memory_region_is_mapped(&s->bar2_mem) &&
        addr >= IFCVF_BAR_SIZE && addr < 2 * IFCVF_BAR_SIZE) {
        uint32_t off = addr - IFCVF_BAR_SIZE;
        if (size == 2) {
            /* queue notification; we do not emulate queues, so just store */
        }
        if (s->bar2_buf) {
            memcpy(s->bar2_buf + off, &val, size);
            return;
        }
    }

    /* BAR4: device config and LM region */
    if (memory_region_is_mapped(&s->bar4_mem) &&
        addr >= 2 * IFCVF_BAR_SIZE && addr < 3 * IFCVF_BAR_SIZE) {
        uint32_t off = addr - 2 * IFCVF_BAR_SIZE;
        if (s->dev_cfg && off >= IFCVF_DEV_CFG_OFFSET &&
            off < IFCVF_DEV_CFG_OFFSET + IFCVF_DEV_CFG_SIZE) {
            uint8_t *p = s->dev_cfg + (off - IFCVF_DEV_CFG_OFFSET);
            memcpy(p, &val, size);
            return;
        }
        if (s->lm_region && off >= IFCVF_LM_OFFSET &&
            off < IFCVF_LM_OFFSET + IFCVF_LM_REGION_SIZE) {
            uint8_t *p = s->lm_region + (off - IFCVF_LM_OFFSET);
            memcpy(p, &val, size);
            return;
        }
        if (s->bar4_buf) {
            memcpy(s->bar4_buf + off, &val, size);
            return;
        }
    }

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Reset device-visible state */
    if (s->common_cfg) {
        memset(s->common_cfg, 0, sizeof(VirtioPciCommonCfg));
        s->common_cfg->num_queues = cpu_to_le16(IFCVF_NUM_QUEUES);
    }
    if (s->isr_status) {
        *s->isr_status = 0;
    }

    (void)s;
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

static void pcibase_init_bar_backing(PCIBaseState *s)
{
    /* Allocate backing buffers for BAR0, BAR2, BAR4 */
    s->bar0_buf = g_malloc0(IFCVF_BAR_SIZE);
    memory_region_init_ram(&s->bar0_mem, OBJECT(s), "ifcvf-bar0-backing", IFCVF_BAR_SIZE, NULL);

    s->bar2_buf = g_malloc0(IFCVF_BAR_SIZE);
    memory_region_init_ram(&s->bar2_mem, OBJECT(s), "ifcvf-bar2-backing", IFCVF_BAR_SIZE, NULL);

    s->bar4_buf = g_malloc0(IFCVF_BAR_SIZE);
    memory_region_init_ram(&s->bar4_mem, OBJECT(s), "ifcvf-bar4-backing", IFCVF_BAR_SIZE, NULL);

    /* Map internal pointers */
    s->common_cfg = (VirtioPciCommonCfg *)(s->bar0_buf + IFCVF_COMMON_CFG_OFFSET);
    s->isr_status = s->bar0_buf + IFCVF_ISR_OFFSET;
    s->dev_cfg = s->bar4_buf + IFCVF_DEV_CFG_OFFSET;
    s->lm_region = s->bar4_buf + IFCVF_LM_OFFSET;

    memset(s->common_cfg, 0, sizeof(VirtioPciCommonCfg));
    s->common_cfg->num_queues = cpu_to_le16(IFCVF_NUM_QUEUES);
    *s->isr_status = 0;
}

static void pcibase_setup_virtio_caps(PCIDevice *pdev, PCIBaseState *s)
{
    uint8_t *pci_conf = pdev->config;

    /* Enable PCI capability list */
    pci_conf[PCI_STATUS] |= PCI_STATUS_CAP_LIST;
    pci_conf[PCI_CAPABILITY_LIST] = 0x50; /* first capability pointer */

    /* Common config capability at 0x50 */
    VirtioPciCap common_cap = (VirtioPciCap){ 0 };
    common_cap.cap_vndr = PCI_CAP_ID_VNDR;
    common_cap.cap_next = 0x60;
    common_cap.cap_len = sizeof(VirtioPciCap);
    common_cap.cfg_type = VIRTIO_PCI_CAP_COMMON_CFG;
    common_cap.bar = IFCVF_COMMON_CFG_BAR;
    common_cap.offset = cpu_to_le32(IFCVF_COMMON_CFG_OFFSET);
    common_cap.length = cpu_to_le32(sizeof(VirtioPciCommonCfg));
    memcpy(pci_conf + 0x50, &common_cap, sizeof(common_cap));

    /* Notify capability at 0x60 */
    VirtioPciCap notify_cap = (VirtioPciCap){ 0 };
    notify_cap.cap_vndr = PCI_CAP_ID_VNDR;
    notify_cap.cap_next = 0x70;
    notify_cap.cap_len = sizeof(VirtioPciCap);
    notify_cap.cfg_type = VIRTIO_PCI_CAP_NOTIFY_CFG;
    notify_cap.bar = IFCVF_NOTIFY_BAR;
    notify_cap.offset = cpu_to_le32(IFCVF_NOTIFY_OFFSET);
    notify_cap.length = cpu_to_le32(IFCVF_BAR_SIZE);
    memcpy(pci_conf + 0x60, &notify_cap, sizeof(notify_cap));

    /* Store notify_off_multiplier immediately after capability, as driver expects
     * at pos + sizeof(cap) via pci_read_config_dword().
     */
    {
        uint32_t mult = cpu_to_le32(IFCVF_NOTIFY_MULT_DEFAULT);
        memcpy(pci_conf + 0x60 + sizeof(VirtioPciCap), &mult, sizeof(mult));
    }

    /* ISR capability at 0x70 */
    VirtioPciCap isr_cap = (VirtioPciCap){ 0 };
    isr_cap.cap_vndr = PCI_CAP_ID_VNDR;
    isr_cap.cap_next = 0x80;
    isr_cap.cap_len = sizeof(VirtioPciCap);
    isr_cap.cfg_type = VIRTIO_PCI_CAP_ISR_CFG;
    isr_cap.bar = IFCVF_ISR_BAR;
    isr_cap.offset = cpu_to_le32(IFCVF_ISR_OFFSET);
    isr_cap.length = cpu_to_le32(1);
    memcpy(pci_conf + 0x70, &isr_cap, sizeof(isr_cap));

    /* Device config capability at 0x80 */
    VirtioPciCap dev_cap = (VirtioPciCap){ 0 };
    dev_cap.cap_vndr = PCI_CAP_ID_VNDR;
    dev_cap.cap_next = 0x00; /* last capability */
    dev_cap.cap_len = sizeof(VirtioPciCap);
    dev_cap.cfg_type = VIRTIO_PCI_CAP_DEVICE_CFG;
    dev_cap.bar = IFCVF_DEV_CFG_BAR;
    dev_cap.offset = cpu_to_le32(IFCVF_DEV_CFG_OFFSET);
    dev_cap.length = cpu_to_le32(IFCVF_DEV_CFG_SIZE);
    memcpy(pci_conf + 0x80, &dev_cap, sizeof(dev_cap));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  IFCVF_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IFCVF_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IFCVF_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Present the device as PCIe endpoint */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x90);

    /* Power management capability (generic) */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Allocate internal BAR backings and map subregions */
    pcibase_init_bar_backing(s);

    /* Configure BARs: expose BAR0, BAR2, BAR4 as MMIO regions. */
    s->num_bars = 3;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "ifcvf-bar";
    }

    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = IFCVF_BAR_SIZE; /* minimal page */
    s->bar_info[0].name = "ifcvf-bar0";

    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = IFCVF_BAR_SIZE;
    s->bar_info[2].name = "ifcvf-bar2";

    s->bar_info[4].type = BAR_TYPE_MMIO;
    s->bar_info[4].size = IFCVF_BAR_SIZE;
    s->bar_info[4].name = "ifcvf-bar4";

    /* Register only the bars we actually set up */
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
    pcibase_register_bar(pdev, s, &s->bar_info[2], errp);
    pcibase_register_bar(pdev, s, &s->bar_info[4], errp);

    /* Initialize MSI-X, since the driver exclusively relies on MSI-X
     * vectors via pci_alloc_irq_vectors(..., PCI_IRQ_MSIX | ...).
     */
    s->has_msix = false;
    s->has_msi = false;

    Error *local_err = NULL;
    int ret = msix_init_exclusive_bar(pdev, 32, 0, &local_err);
    if (ret == 0) {
        s->has_msix = true;
    } else {
        if (local_err) {
            error_propagate(errp, local_err);
        }
    }

    /* Initialize feature bits: expose ACCESS_PLATFORM at minimum */
    s->device_features = (1ULL << 33); /* VIRTIO_F_ACCESS_PLATFORM */
    s->driver_features = 0;

    /* Program virtio-pci vendor capabilities that ifcvf_init_hw() enumerates */
    pcibase_setup_virtio_caps(pdev, s);
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

    if (s->bar0_buf) {
        g_free(s->bar0_buf);
        s->bar0_buf = NULL;
    }
    if (s->bar2_buf) {
        g_free(s->bar2_buf);
        s->bar2_buf = NULL;
    }
    if (s->bar4_buf) {
        g_free(s->bar4_buf);
        s->bar4_buf = NULL;
    }

    (void)s;
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
