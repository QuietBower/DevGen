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

/*
 * VirtIO PCI vendor-specific capability structure (from Linux driver).
 * Each capability points to a region within a PCI BAR.
 */
struct virtio_pci_cap {
    uint8_t cap_vndr;    /* Generic PCI field: PCI_CAP_ID_VNDR */
    uint8_t cap_next;    /* Generic PCI field: next ptr. */
    uint8_t cap_len;     /* Generic PCI field: capability length */
    uint8_t cfg_type;    /* Identifies the structure. */
    uint8_t bar;         /* Where to find it. */
    uint8_t id;          /* Multiple capabilities of the same type */
    uint8_t padding[2];  /* Pad to full dword. */
    uint32_t offset;     /* Offset within bar. */
    uint32_t length;     /* Length of the structure, in bytes. */
};

/* VirtIO PCI capability configuration types (from supplementary driver source) */
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4
#define VIRTIO_PCI_CAP_PCI_CFG      5

#define TYPE_PCIBASE_DEVICE "virtio_vfio_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI Vendor and Device IDs from virtiovf_pci_table first entry: vendor = REDHAT (0x1af4), device = 0x1041 (virtio-net) */
#define PCI_VENDOR_ID_REDHAT_QUMRANET 0x1af4
#define VENDOR_ID PCI_VENDOR_ID_REDHAT_QUMRANET
#define DEVICE_ID 0x1041

/* No register offset macros found in provided driver source. */

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

/* Maximum number of VirtIO PCI capabilities */
#define VIRTIO_PCI_CAP_MAX 5

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* VirtIO PCI capabilities */
    struct virtio_pci_cap caps[VIRTIO_PCI_CAP_MAX];

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t common_cfg[128];  /* Shadow for common configuration structure */
    uint8_t isr_status;      /* 1 byte for ISR status */

    /* DMA Context */

    /* Operational status flags */

    /* State used to handle reset sequences */

    /* Power management state (D0-D3) */

    /* Other additional info */
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Access to common configuration structure (offset 0x00 - 0xFF) */
    if (addr < 0x100) {
        if (addr + size > sizeof(s->common_cfg)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: read out of bounds common cfg at 0x%" HWADDR_PRIx ", size %u\n",
                          __func__, addr, size);
            return 0;
        }
        memcpy(&val, s->common_cfg + addr, size);
        return val;
    }
    /* ISR status region at offset 0x200 */
    else if (addr >= 0x200 && addr < 0x201) {
        val = s->isr_status;
        /* Reading ISR clears the bits (as per VirtIO spec) */
        s->isr_status = 0;
        return val;
    }

    qemu_log_mask(LOG_UNIMP, "%s: unimplemented read at 0x%" HWADDR_PRIx ", size %u\n",
                  __func__, addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x100) {
        /* Write to common configuration */
        if (addr + size > sizeof(s->common_cfg)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write out of bounds common cfg at 0x%" HWADDR_PRIx ", size %u\n",
                          __func__, addr, size);
            return;
        }
        memcpy(s->common_cfg + addr, &val, size);
    } else if (addr >= 0x200 && addr < 0x201) {
        /* ISR status is read-only in general; ignore writes */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: ignoring write to ISR status\n", __func__);
    } else {
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write at 0x%" HWADDR_PRIx ", size %u, value 0x%" PRIx64 "\n",
                      __func__, addr, size, val);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    /* Logic for Port I/O (Legacy support) - not used */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Logic for Port I/O (Legacy support) - not used */
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

    /* Revert registers to power-on defaults */
    memset(s->common_cfg, 0, sizeof(s->common_cfg));
    s->isr_status = 0;
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
    int ret;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    /* PCIe endpoint capability must be at offset 0x100 per spec */
    pcie_endpoint_cap_init(pdev, 0x100);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Add VirtIO PCI vendor-specific capabilities */
    /* Capability pointers: we'll add 4 capabilities in sequence */
    int cap_common, cap_notify, cap_isr, cap_device;

    /* Common configuration capability */
    cap_common = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(struct virtio_pci_cap), errp);
    if (cap_common < 0) {
        error_setg(errp, "Failed to add common config capability");
        return;
    }
    s->caps[0].cap_vndr = PCI_CAP_ID_VNDR;
    s->caps[0].cap_len = sizeof(struct virtio_pci_cap);
    s->caps[0].cfg_type = VIRTIO_PCI_CAP_COMMON_CFG;
    s->caps[0].bar = 0;  /* BAR0 */
    s->caps[0].id = 0;
    s->caps[0].offset = 0x00;
    s->caps[0].length = 0x38;  /* Minimal common cfg size: 56 bytes */
    memset(s->caps[0].padding, 0, 2);
    memcpy(pci_conf + cap_common + PCI_CAP_FLAGS, &s->caps[0], sizeof(struct virtio_pci_cap));

    /* Notify capability */
    cap_notify = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(struct virtio_pci_cap), errp);
    if (cap_notify < 0) {
        error_setg(errp, "Failed to add notify config capability");
        return;
    }
    s->caps[1].cap_vndr = PCI_CAP_ID_VNDR;
    s->caps[1].cap_len = sizeof(struct virtio_pci_cap);
    s->caps[1].cfg_type = VIRTIO_PCI_CAP_NOTIFY_CFG;
    s->caps[1].bar = 0;
    s->caps[1].id = 0;
    s->caps[1].offset = 0x100;
    s->caps[1].length = 0x2;   /* Just one queue notify */
    memset(s->caps[1].padding, 0, 2);
    memcpy(pci_conf + cap_notify + PCI_CAP_FLAGS, &s->caps[1], sizeof(struct virtio_pci_cap));

    /* ISR capability */
    cap_isr = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(struct virtio_pci_cap), errp);
    if (cap_isr < 0) {
        error_setg(errp, "Failed to add ISR config capability");
        return;
    }
    s->caps[2].cap_vndr = PCI_CAP_ID_VNDR;
    s->caps[2].cap_len = sizeof(struct virtio_pci_cap);
    s->caps[2].cfg_type = VIRTIO_PCI_CAP_ISR_CFG;
    s->caps[2].bar = 0;
    s->caps[2].id = 0;
    s->caps[2].offset = 0x200;
    s->caps[2].length = 0x1;
    memset(s->caps[2].padding, 0, 2);
    memcpy(pci_conf + cap_isr + PCI_CAP_FLAGS, &s->caps[2], sizeof(struct virtio_pci_cap));

    /* Device-specific configuration capability */
    cap_device = pci_add_capability(pdev, PCI_CAP_ID_VNDR, 0, sizeof(struct virtio_pci_cap), errp);
    if (cap_device < 0) {
        error_setg(errp, "Failed to add device config capability");
        return;
    }
    s->caps[3].cap_vndr = PCI_CAP_ID_VNDR;
    s->caps[3].cap_len = sizeof(struct virtio_pci_cap);
    s->caps[3].cfg_type = VIRTIO_PCI_CAP_DEVICE_CFG;
    s->caps[3].bar = 0;
    s->caps[3].id = 0;
    s->caps[3].offset = 0x300;
    s->caps[3].length = 0x100; /* Arbitrary: 256 bytes device config */
    memset(s->caps[3].padding, 0, 2);
    memcpy(pci_conf + cap_device + PCI_CAP_FLAGS, &s->caps[3], sizeof(struct virtio_pci_cap));

    /* Set capability linked list */
    /* The capabilities were added with next_ptr = 0 initially; we link them manually */
    pci_set_byte(pci_conf + cap_common + PCI_CAP_LIST_NEXT, cap_notify);
    pci_set_byte(pci_conf + cap_notify + PCI_CAP_LIST_NEXT, cap_isr);
    pci_set_byte(pci_conf + cap_isr + PCI_CAP_LIST_NEXT, cap_device);
    pci_set_byte(pci_conf + cap_device + PCI_CAP_LIST_NEXT, 0); /* End of list */

    /* BAR Initialization */
    /* BAR0: MMIO region for virtio structures, size 0x1000 */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "virtio-mmio-bar0";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* MSI-X initialization (required for modern virtio-pci) */
    /* Use exclusive BAR to prevent memory assertion crashes */
    ret = msix_init_exclusive_bar(pdev, 2, 1, errp);  /* 2 vectors, 1 bar */
    if (ret < 0) {
        error_setg(errp, "Failed to initialize MSI-X");
        return;
    }
    s->has_msix = true;

    /* No DMA configuration inferred. */
    /* No timer initialization inferred. */
    /* No additional field initialization inferred. */

    /* Initialize common config default values */
    memset(s->common_cfg, 0, sizeof(s->common_cfg));
    /* Set some initial device feature bits? Maybe not needed. */
    s->isr_status = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "virtio_vfio_pci_pci",
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
