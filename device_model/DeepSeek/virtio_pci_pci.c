/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
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

#define TYPE_PCIBASE_DEVICE "virtio_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_ANY_ID 0xFFFF

#define VENDOR_ID PCI_VENDOR_ID_REDHAT_QUMRANET
#define DEVICE_ID PCI_ANY_ID
#define CLASS_ID 0x00FF00

/* Virtio PCI capability types */
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4
#define VIRTIO_PCI_CAP_PCI_CFG      5

/* Legacy virtio PCI register offsets (not used in modern, included for completeness) */
#define VIRTIO_PCI_GUEST_FEATURES     4
#define VIRTIO_PCI_QUEUE_PFN          8
#define VIRTIO_PCI_QUEUE_NUM         12
#define VIRTIO_PCI_QUEUE_SEL         14
#define VIRTIO_PCI_QUEUE_NOTIFY      16
#define VIRTIO_PCI_STATUS            18
#define VIRTIO_PCI_ISR               19

/* Struct definitions for modern virtio PCI capabilities */
struct virtio_pci_cap {
    uint8_t cap_vndr;          /* Generic PCI field: PCI_CAP_ID_VNDR */
    uint8_t cap_next;          /* Generic PCI field: next ptr. */
    uint8_t cap_len;           /* Generic PCI field: capability length */
    uint8_t cfg_type;          /* Identifies the structure. */
    uint8_t bar;               /* Where to find it. */
    uint8_t id;                /* Multiple capabilities of the same type */
    uint8_t padding[2];        /* Pad to full dword. */
    uint32_t offset;           /* Offset within bar. */
    uint32_t length;           /* Length of the structure, in bytes. */
};

struct virtio_pci_common_cfg {
    /* About the whole device. */
    uint32_t device_feature_select;   /* read-write */
    uint32_t device_feature;          /* read-only */
    uint32_t guest_feature_select;    /* read-write */
    uint32_t guest_feature;           /* read-write */
    uint16_t msix_config;             /* read-write */
    uint16_t num_queues;              /* read-only */
    uint8_t device_status;            /* read-write */
    uint8_t config_generation;        /* read-only */

    /* About a specific virtqueue. */
    uint16_t queue_select;            /* read-write */
    uint16_t queue_size;              /* read-write */
    uint16_t queue_msix_vector;       /* read-write */
    uint16_t queue_enable;            /* read-write */
    uint16_t queue_notify_off;        /* read-only */
    uint32_t queue_desc_lo;           /* read-write */
    uint32_t queue_desc_hi;           /* read-write */
    uint32_t queue_avail_lo;          /* read-write */
    uint32_t queue_avail_hi;          /* read-write */
    uint32_t queue_used_lo;           /* read-write */
    uint32_t queue_used_hi;           /* read-write */
};

struct virtio_pci_notify_cap {
    struct virtio_pci_cap cap;
    uint32_t notify_off_multiplier;   /* Multiplier for queue_notify_off. */
};

#define VIRTIO_MSI_NO_VECTOR 0xFFFF
#define VIRTIO_CONFIG_S_DRIVER_OK 0x04

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
    MemoryRegion bar0;  /* Container for BAR0 */
    MemoryRegion mmio;  /* Our device-specific MMIO region */

    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint8_t isr; /* Interrupt Status Register */

    /* Global virtio config fields */
    uint32_t device_feature_select;
    uint32_t guest_feature_select;
    uint64_t guest_features[2]; /* stored as two 32-bit halves */
    uint16_t msix_config;
    uint16_t config_msix_vector; /* vector for config interrupts */
    uint8_t device_status;
    uint8_t config_generation;

    /* Queue select */
    uint16_t queue_select;
    #define VIRTIO_PCI_MAX_VQ 64
    struct {
        uint16_t size;
        uint16_t msix_vector;
        bool enable;
        uint16_t notify_off;
        uint64_t desc;
        uint64_t avail;
        uint64_t used;
    } vqs[VIRTIO_PCI_MAX_VQ];

    /* Notify multiplier */
    uint32_t notify_off_multiplier;

    /* Interrupt control */
    bool intx_enabled;

    /* Operational status and PM */
    bool reset_active;
    uint8_t pm_state;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->isr) {
        if (msix_enabled(pdev)) {
            /* Use config vector for config changes (bit 0) */
            if (s->isr & 0x1) {
                msix_notify(pdev, s->config_msix_vector);
            }
            /* For queue interrupts, they are raised directly via per-VQ vector,
               so we don't need to send here. */
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    union { uint8_t buf[8]; uint64_t val; } tmp = {0};

    if (addr < 0x1000) {
        /* Common configuration region */
        uint16_t qsel = s->queue_select;
        bool valid_queue = (qsel < VIRTIO_PCI_MAX_VQ);

        switch (addr) {
        case 0x00: /* device_feature_select */
            stl_le_p(tmp.buf, s->device_feature_select);
            break;
        case 0x04: /* device_feature */
            if (s->device_feature_select == 0) {
                stl_le_p(tmp.buf, 0);
            } else if (s->device_feature_select == 1) {
                /* Set bit 0 for VIRTIO_F_VERSION_1 (feature bit 32) */
                stl_le_p(tmp.buf, 1);
            } else {
                stl_le_p(tmp.buf, 0);
            }
            break;
        case 0x08: /* guest_feature_select */
            stl_le_p(tmp.buf, s->guest_feature_select);
            break;
        case 0x0C: /* guest_feature */
            if (s->guest_feature_select == 0) {
                stl_le_p(tmp.buf, (uint32_t)s->guest_features[0]);
            } else if (s->guest_feature_select == 1) {
                stl_le_p(tmp.buf, (uint32_t)s->guest_features[1]);
            } else {
                stl_le_p(tmp.buf, 0);
            }
            break;
        case 0x10: /* msix_config */
            stw_le_p(tmp.buf, s->msix_config);
            break;
        case 0x12: /* num_queues */
            stw_le_p(tmp.buf, VIRTIO_PCI_MAX_VQ);
            break;
        case 0x14: /* device_status */
            tmp.buf[0] = s->device_status;
            break;
        case 0x15: /* config_generation */
            tmp.buf[0] = s->config_generation;
            break;
        case 0x16: /* queue_select */
            stw_le_p(tmp.buf, s->queue_select);
            break;
        case 0x18: /* queue_size */
            if (valid_queue) stw_le_p(tmp.buf, s->vqs[qsel].size);
            break;
        case 0x1A: /* queue_msix_vector */
            if (valid_queue) stw_le_p(tmp.buf, s->vqs[qsel].msix_vector);
            break;
        case 0x1C: /* queue_enable */
            if (valid_queue) stw_le_p(tmp.buf, s->vqs[qsel].enable ? 1 : 0);
            break;
        case 0x1E: /* queue_notify_off */
            if (valid_queue) stw_le_p(tmp.buf, s->vqs[qsel].notify_off);
            break;
        case 0x20: /* queue_desc_lo */
            if (valid_queue) stl_le_p(tmp.buf, (uint32_t)s->vqs[qsel].desc);
            break;
        case 0x24: /* queue_desc_hi */
            if (valid_queue) stl_le_p(tmp.buf, (uint32_t)(s->vqs[qsel].desc >> 32));
            break;
        case 0x28: /* queue_avail_lo */
            if (valid_queue) stl_le_p(tmp.buf, (uint32_t)s->vqs[qsel].avail);
            break;
        case 0x2C: /* queue_avail_hi */
            if (valid_queue) stl_le_p(tmp.buf, (uint32_t)(s->vqs[qsel].avail >> 32));
            break;
        case 0x30: /* queue_used_lo */
            if (valid_queue) stl_le_p(tmp.buf, (uint32_t)s->vqs[qsel].used);
            break;
        case 0x34: /* queue_used_hi */
            if (valid_queue) stl_le_p(tmp.buf, (uint32_t)(s->vqs[qsel].used >> 32));
            break;
        default:
            break;
        }
        val = tmp.val;
    } else if (addr < 0x2000) {
        /* ISR region */
        if (addr == 0x1000) {
            val = s->isr;
            s->isr = 0; /* reading clears */
            pcibase_update_irq(s);
        }
    } else if (addr < 0x3000) {
        /* Notify region is write-only */
        val = 0;
    } else {
        val = 0;
    }
    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 0x1000) {
        /* Common configuration region */
        uint16_t qsel = s->queue_select;
        bool valid_queue = (qsel < VIRTIO_PCI_MAX_VQ);

        switch (addr) {
        case 0x00: /* device_feature_select */
            s->device_feature_select = val;
            break;
        case 0x04: /* device_feature (read-only) */
            break;
        case 0x08: /* guest_feature_select */
            s->guest_feature_select = val;
            break;
        case 0x0C: /* guest_feature */
            if (s->guest_feature_select == 0)
                s->guest_features[0] = val;
            else if (s->guest_feature_select == 1)
                s->guest_features[1] = val;
            break;
        case 0x10: /* msix_config */
            s->msix_config = val;
            s->config_msix_vector = val & 0xFFFF;
            break;
        case 0x12: /* num_queues (read-only) */
            break;
        case 0x14: /* device_status */
            s->device_status = val & 0xFF;
            if (s->device_status & VIRTIO_CONFIG_S_DRIVER_OK) {
                s->isr |= 0x1; /* VIRTIO_PCI_ISR_CONFIG */
                s->config_generation++;
                pcibase_update_irq(s);
            }
            break;
        case 0x15: /* config_generation (read-only) */
            break;
        case 0x16: /* queue_select */
            s->queue_select = val;
            break;
        case 0x18: /* queue_size */
            if (valid_queue) s->vqs[qsel].size = val;
            break;
        case 0x1A: /* queue_msix_vector */
            if (valid_queue) s->vqs[qsel].msix_vector = val;
            break;
        case 0x1C: /* queue_enable */
            if (valid_queue) s->vqs[qsel].enable = val & 1;
            break;
        case 0x1E: /* queue_notify_off (read-only) */
            break;
        case 0x20: /* queue_desc_lo */
            if (valid_queue) s->vqs[qsel].desc = (s->vqs[qsel].desc & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFF);
            break;
        case 0x24: /* queue_desc_hi */
            if (valid_queue) s->vqs[qsel].desc = (s->vqs[qsel].desc & 0xFFFFFFFFULL) | ((val & 0xFFFFFFFF) << 32);
            break;
        case 0x28: /* queue_avail_lo */
            if (valid_queue) s->vqs[qsel].avail = (s->vqs[qsel].avail & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFF);
            break;
        case 0x2C: /* queue_avail_hi */
            if (valid_queue) s->vqs[qsel].avail = (s->vqs[qsel].avail & 0xFFFFFFFFULL) | ((val & 0xFFFFFFFF) << 32);
            break;
        case 0x30: /* queue_used_lo */
            if (valid_queue) s->vqs[qsel].used = (s->vqs[qsel].used & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFF);
            break;
        case 0x34: /* queue_used_hi */
            if (valid_queue) s->vqs[qsel].used = (s->vqs[qsel].used & 0xFFFFFFFFULL) | ((val & 0xFFFFFFFF) << 32);
            break;
        default:
            break;
        }
    } else if (addr >= 0x2000 && addr < 0x3000) {
        /* Notify region: write triggers queue notification */
        uint32_t qidx = (addr - 0x2000) / 4; /* assuming multiplier = 4 */
        if (qidx < VIRTIO_PCI_MAX_VQ && s->vqs[qidx].enable) {
            s->isr |= 0x2; /* VIRTIO_PCI_ISR_QUEUE */
            /* Send MSI-X if enabled and vector assigned */
            if (msix_enabled(PCI_DEVICE(s))) {
                uint16_t vector = s->vqs[qidx].msix_vector;
                if (vector != VIRTIO_MSI_NO_VECTOR) {
                    msix_notify(PCI_DEVICE(s), vector);
                }
            }
            pcibase_update_irq(s);
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* PIO handlers not used in modern virtio, removed. */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to power-on defaults */
    s->isr = 0;
    s->device_feature_select = 0;
    s->guest_feature_select = 0;
    memset(s->guest_features, 0, sizeof(s->guest_features));
    s->msix_config = 0;
    s->config_msix_vector = VIRTIO_MSI_NO_VECTOR;
    s->device_status = 0;
    s->config_generation = 0;
    s->queue_select = 0;
    memset(s->vqs, 0, sizeof(s->vqs));
    for (int i = 0; i < VIRTIO_PCI_MAX_VQ; i++) {
        s->vqs[i].notify_off = i; /* set basic notify offset */
        s->vqs[i].msix_vector = VIRTIO_MSI_NO_VECTOR;
    }
    s->notify_off_multiplier = 4;
    s->reset_active = false;
    s->pm_state = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Initialize BAR0 as a container for virtio structures and MSI-X */
    memory_region_init(&s->bar0, OBJECT(s), "virtio-bar0", 0x4000);
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "virtio-mmio", 0x3000);
    memory_region_add_subregion(&s->bar0, 0x0, &s->mmio);

    /* Initialize MSI-X with 4 vectors, table at 0x3000, PBA at 0x3200 within BAR0 */
    if (msix_init(pdev, 4, &s->bar0, 0, 0x3000, &s->bar0, 0, 0x3200, 0, errp)) {
        return;
    }

    /* Register BAR0 */
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    /* Add virtio PCI capabilities */
    /* Common configuration capability */
    struct virtio_pci_cap common_cap = {0};
    common_cap.cap_vndr = PCI_CAP_ID_VNDR;
    common_cap.cap_len = sizeof(common_cap);
    common_cap.cfg_type = VIRTIO_PCI_CAP_COMMON_CFG;
    common_cap.bar = 0;
    common_cap.offset = cpu_to_le32(0x0);
    common_cap.length = cpu_to_le32(sizeof(struct virtio_pci_common_cfg));
    uint8_t pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(common_cap), NULL);
    uint8_t next = pdev->config[pos + 1];
    memcpy(pdev->config + pos, &common_cap, sizeof(common_cap));
    pdev->config[pos + 1] = next;

    /* ISR capability */
    struct virtio_pci_cap isr_cap = {0};
    isr_cap.cap_vndr = PCI_CAP_ID_VNDR;
    isr_cap.cap_len = sizeof(isr_cap);
    isr_cap.cfg_type = VIRTIO_PCI_CAP_ISR_CFG;
    isr_cap.bar = 0;
    isr_cap.offset = cpu_to_le32(0x1000);
    isr_cap.length = cpu_to_le32(1);
    pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(isr_cap), NULL);
    next = pdev->config[pos + 1];
    memcpy(pdev->config + pos, &isr_cap, sizeof(isr_cap));
    pdev->config[pos + 1] = next;

    /* Notify capability (with multiplier) */
    struct virtio_pci_notify_cap notify_cap = {0};
    notify_cap.cap.cap_vndr = PCI_CAP_ID_VNDR;
    notify_cap.cap.cap_len = sizeof(notify_cap);
    notify_cap.cap.cfg_type = VIRTIO_PCI_CAP_NOTIFY_CFG;
    notify_cap.cap.bar = 0;
    notify_cap.cap.offset = cpu_to_le32(0x2000);
    notify_cap.cap.length = cpu_to_le32(0x1000);
    notify_cap.notify_off_multiplier = cpu_to_le32(4);
    pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(notify_cap), NULL);
    next = pdev->config[pos + 1];
    memcpy(pdev->config + pos, &notify_cap, sizeof(notify_cap));
    pdev->config[pos + 1] = next;

    /* Device configuration capability (empty) */
    struct virtio_pci_cap device_cap = {0};
    device_cap.cap_vndr = PCI_CAP_ID_VNDR;
    device_cap.cap_len = sizeof(device_cap);
    device_cap.cfg_type = VIRTIO_PCI_CAP_DEVICE_CFG;
    device_cap.bar = 0;
    device_cap.offset = cpu_to_le32(0x4000); /* beyond BAR0 size, but okay */
    device_cap.length = cpu_to_le32(0);
    pos = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(device_cap), NULL);
    next = pdev->config[pos + 1];
    memcpy(pdev->config + pos, &device_cap, sizeof(device_cap));
    pdev->config[pos + 1] = next;

    /* Initialize state */
    s->isr = 0;
    s->device_feature_select = 0;
    s->guest_feature_select = 0;
    memset(s->guest_features, 0, sizeof(s->guest_features));
    s->msix_config = 0;
    s->config_msix_vector = VIRTIO_MSI_NO_VECTOR;
    s->device_status = 0;
    s->config_generation = 0;
    s->queue_select = 0;
    memset(s->vqs, 0, sizeof(s->vqs));
    for (int i = 0; i < VIRTIO_PCI_MAX_VQ; i++) {
        s->vqs[i].notify_off = i;
        s->vqs[i].msix_vector = VIRTIO_MSI_NO_VECTOR;
    }
    s->notify_off_multiplier = 4;
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
    .name = "virtio_pci_pci",
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
