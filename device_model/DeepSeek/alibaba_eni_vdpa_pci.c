/*
 * QEMU PCI device model for Alibaba ENI vDPA (eni-vdpa)
 * Based on Linux driver: drivers/vdpa/alibaba/eni_vdpa.c
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

#define TYPE_PCIBASE_DEVICE "alibaba_eni_vdpa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1AF4
#define DEVICE_ID 0x1000
#define CLASS_ID  0x0200

/* BAR sizes */
#define BAR0_SIZE  0x200   /* PIO for legacy virtio registers + config */
#define BAR1_SIZE  0x1000  /* MSI-X MMIO BAR */

/* Supplementary defines from driver source */
#define VIRTIO_PCI_QUEUE_NOTIFY     16
#define VIRTIO_MSI_NO_VECTOR        0xffff

/* Feature bits */
#define VIRTIO_NET_F_MAC         5
#define VIRTIO_NET_F_MRG_RXBUF   15
#define VIRTIO_NET_F_CTRL_VQ     17
#define VIRTIO_NET_F_MQ          22
#define VIRTIO_F_ACCESS_PLATFORM 33
#define VIRTIO_F_ORDER_PLATFORM  34

/* Host features offered to the driver */
#define HOST_FEATURES ( \
    (1ULL << VIRTIO_NET_F_MAC) | \
    (1ULL << VIRTIO_NET_F_MRG_RXBUF) | \
    (1ULL << VIRTIO_NET_F_CTRL_VQ) | \
    (1ULL << VIRTIO_NET_F_MQ) | \
    (1ULL << VIRTIO_F_ACCESS_PLATFORM) | \
    (1ULL << VIRTIO_F_ORDER_PLATFORM) \
)

/* Maximum number of virtqueues supported */
#define VIRTIO_PCI_MAX_QUEUES 64
#define CONFIG_SIZE 256

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[2];

    /* Legacy virtio registers */
    uint32_t host_features;
    uint32_t guest_features;
    uint16_t queue_sel;
    uint8_t device_status;
    uint8_t isr;
    uint16_t config_vector;
    uint16_t queue_vector[VIRTIO_PCI_MAX_QUEUES];
    uint32_t queue_pfn[VIRTIO_PCI_MAX_QUEUES];
    uint16_t queue_size[VIRTIO_PCI_MAX_QUEUES];
    uint8_t config[CONFIG_SIZE];
};

/* Default device-specific configuration: emulate virtio-net config */
static const uint8_t default_net_config[CONFIG_SIZE] = {
    /* mac */ 0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
    /* status */ 0x00, 0x00,
    /* max_virtqueue_pairs = 1 */ 0x01, 0x00,
    /* mtu */ 0x00, 0x00,
    /* speed */ 0x00, 0x00, 0x00, 0x00,
    /* duplex (uint8_t) + padding */ 0x00,
    /* remaining bytes zero */
};

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = &s->parent_obj;
    uint64_t val = 0;
    bool msix_en = msix_enabled(pdev);
    hwaddr config_offset = msix_en ? 0x18 : 0x14;

    if (addr >= config_offset && addr + size <= config_offset + CONFIG_SIZE) {
        switch (size) {
        case 1:
            val = s->config[addr - config_offset];
            break;
        case 2:
            val = lduw_le_p(&s->config[addr - config_offset]);
            break;
        case 4:
            val = ldl_le_p(&s->config[addr - config_offset]);
            break;
        default:
            break;
        }
        return val;
    }

    switch (addr) {
    case 0x00: /* HostFeatures */
        if (size == 4) val = s->host_features;
        break;
    case 0x04: /* GuestFeatures */
        if (size == 4) val = s->guest_features;
        break;
    case 0x08: /* QueuePFN */
        if (size == 4) val = s->queue_pfn[s->queue_sel];
        break;
    case 0x0C: /* QueueSize */
        if (size == 2) val = s->queue_size[s->queue_sel];
        break;
    case 0x0E: /* QueueSelect */
        if (size == 2) val = s->queue_sel;
        break;
    case 0x10: /* QueueNotify (read undefined) */
        break;
    case 0x12: /* DeviceStatus */
        if (size == 1) val = s->device_status;
        break;
    case 0x13: /* ISR */
        if (size == 1) {
            val = s->isr;
            s->isr = 0;
        }
        break;
    case 0x14: /* ConfigVector */
        if (size == 2) val = s->config_vector;
        break;
    case 0x16: /* QueueVector */
        if (size == 2) val = s->queue_vector[s->queue_sel];
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = &s->parent_obj;
    bool msix_en = msix_enabled(pdev);
    hwaddr config_offset = msix_en ? 0x18 : 0x14;

    if (addr >= config_offset && addr + size <= config_offset + CONFIG_SIZE) {
        switch (size) {
        case 1:
            s->config[addr - config_offset] = val;
            break;
        case 2:
            stw_le_p(&s->config[addr - config_offset], val);
            break;
        case 4:
            stl_le_p(&s->config[addr - config_offset], val);
            break;
        default:
            break;
        }
        return;
    }

    switch (addr) {
    case 0x04: /* GuestFeatures */
        if (size == 4) s->guest_features = val;
        break;
    case 0x08: /* QueuePFN */
        if (size == 4) s->queue_pfn[s->queue_sel] = val;
        break;
    case 0x0E: /* QueueSelect */
        if (size == 2) s->queue_sel = val;
        break;
    case 0x10: /* QueueNotify - kick queue, no-op in emulation */
        break;
    case 0x12: /* DeviceStatus */
        if (size == 1) s->device_status = val;
        break;
    case 0x14: /* ConfigVector */
        if (size == 2) {
            if (s->config_vector != VIRTIO_MSI_NO_VECTOR) {
                msix_vector_unuse(pdev, s->config_vector);
            }
            s->config_vector = val;
            if (val != VIRTIO_MSI_NO_VECTOR) {
                msix_vector_use(pdev, val);
            }
        }
        break;
    case 0x16: /* QueueVector */
        if (size == 2) {
            int idx = s->queue_sel;
            if (s->queue_vector[idx] != VIRTIO_MSI_NO_VECTOR) {
                msix_vector_unuse(pdev, s->queue_vector[idx]);
            }
            s->queue_vector[idx] = val;
            if (val != VIRTIO_MSI_NO_VECTOR) {
                msix_vector_use(pdev, val);
            }
        }
        break;
    default:
        break;
    }
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

    s->guest_features = 0;
    s->queue_sel = 0;
    s->device_status = 0;
    s->isr = 0;
    s->config_vector = VIRTIO_MSI_NO_VECTOR;
    for (int i = 0; i < VIRTIO_PCI_MAX_QUEUES; i++) {
        s->queue_vector[i] = VIRTIO_MSI_NO_VECTOR;
        s->queue_pfn[i] = 0;
    }
    memcpy(s->config, default_net_config, CONFIG_SIZE);
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

    /* Legacy IO BAR */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_pio_ops, s,
                          "eni-vdpa-pio", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[0]);

    /* MSI-X BAR */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), NULL, NULL,
                          "eni-vdpa-msix", BAR1_SIZE);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);
    if (msix_init_exclusive_bar(pdev, 64, 1, errp) < 0) {
        return;
    }

    /* Initialize device state */
    s->host_features = (uint32_t)HOST_FEATURES;
    s->config_vector = VIRTIO_MSI_NO_VECTOR;
    for (int i = 0; i < VIRTIO_PCI_MAX_QUEUES; i++) {
        s->queue_size[i] = 256;
        s->queue_vector[i] = VIRTIO_MSI_NO_VECTOR;
    }
    memcpy(s->config, default_net_config, CONFIG_SIZE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    msix_uninit_exclusive_bar(pdev);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "alibaba_eni_vdpa_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(host_features, PCIBaseState),
        VMSTATE_UINT32(guest_features, PCIBaseState),
        VMSTATE_UINT16(queue_sel, PCIBaseState),
        VMSTATE_UINT8(device_status, PCIBaseState),
        VMSTATE_UINT8(isr, PCIBaseState),
        VMSTATE_UINT16(config_vector, PCIBaseState),
        VMSTATE_UINT16_ARRAY(queue_vector, PCIBaseState, VIRTIO_PCI_MAX_QUEUES),
        VMSTATE_UINT32_ARRAY(queue_pfn, PCIBaseState, VIRTIO_PCI_MAX_QUEUES),
        VMSTATE_UINT16_ARRAY(queue_size, PCIBaseState, VIRTIO_PCI_MAX_QUEUES),
        VMSTATE_BUFFER(config, PCIBaseState),
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

type_init(pcibase_register_types)
