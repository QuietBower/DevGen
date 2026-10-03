/*
 * QEMU VMCI guest device emulation for driver probe success.
 * Based on Linux driver vmci_guest.c.
 * This device models MMIO registers, MSI-X, and DMA datagram support.
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

#define PAGE_SIZE 4096

#define TYPE_PCIBASE_DEVICE "vmw_vmci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets from new driver source */
#define VMCI_ICR_ADDR           0x08
#define VMCI_IMR_ADDR           0x0c
#define VMCI_RESULT_LOW_ADDR    0x1c
#define VMCI_CONTROL_ADDR       0x04
#define VMCI_CAPS_ADDR          0x18
#define VMCI_GUEST_PAGE_SHIFT   0x34
#define VMCI_DATA_IN_LOW_ADDR   0x2c
#define VMCI_DATA_IN_HIGH_ADDR  0x30
#define VMCI_DATA_OUT_LOW_ADDR  0x24
#define VMCI_DATA_OUT_HIGH_ADDR 0x28
#define VMCI_DATA_IN_ADDR       0x14
#define VMCI_DATA_OUT_ADDR      0x10

/* Capabilities bits */
#define VMCI_CAPS_DATAGRAM      (1 << 2)
#define VMCI_CAPS_PPN64         (1 << 4)
#define VMCI_CAPS_NOTIFICATIONS (1 << 3)
#define VMCI_CAPS_DMA_DATAGRAM  (1 << 5)

/* Interrupt Cause bits */
#define VMCI_ICR_DATAGRAM      (1 << 0)
#define VMCI_ICR_NOTIFICATION  (1 << 1)
#define VMCI_ICR_DMA_DATAGRAM  (1 << 2)

/* Interrupt Mask bits */
#define VMCI_IMR_DATAGRAM      (1 << 0)
#define VMCI_IMR_NOTIFICATION  (1 << 1)
#define VMCI_IMR_DMA_DATAGRAM  (1 << 2)

/* Control bits */
#define VMCI_CONTROL_INT_ENABLE   (1 << 1)
#define VMCI_CONTROL_RESET        (1 << 0)

/* Maximum MSI-X vectors */
#define VMCI_MAX_INTRS             3
#define VMCI_MAX_INTRS_NOTIFICATION 2

/* DMA buffer sizes */
#define VMCI_DG_HEADERSIZE sizeof(struct vmci_datagram)
#define VMCI_MAX_DG_SIZE (17 * 4096)
#define VMCI_DMA_DG_BUFFER_SIZE (VMCI_MAX_DG_SIZE + PAGE_SIZE)

/* BAR1 MMIO region */
#define VMCI_WITH_MMIO_ACCESS_BAR_SIZE ((size_t)(256 * 1024))
#define VMCI_MMIO_ACCESS_OFFSET        ((size_t)(128 * 1024))
#define VMCI_MMIO_ACCESS_SIZE          ((size_t)(64 * 1024))

/* BAR0 I/O region */
#define VMCI_IO_SIZE                0x100

/* VMCI protocol constants */
#define VMCI_HYPERVISOR_CONTEXT_ID  0
#define VMCI_INVALID_ID             0xFFFFFFFF
#define VMCI_ANON_SRC_HANDLE        0 /* placeholder, needed */
#define VMCI_GET_CONTEXT_ID         0x01
#define VMCI_RESOURCES_QUERY        0x02
#define VMCI_EVENT_HANDLER          0x00
#define VMCI_SUCCESS                0

/* Datagram header size (from driver: VMCI_DG_HEADERSIZE) */
/* placeholder for vmci_handle if not provided */
struct vmci_handle {
    uint32_t context;
    uint32_t resource;
};

/* Structures from driver headers */
struct vmci_data_in_out_header {
    uint32_t busy;
    uint32_t opcode;
    uint32_t size;
    uint32_t rsvd;
    uint64_t result;
};

struct vmci_sg_elem {
    uint64_t addr;
    uint64_t size;
};

struct vmci_datagram {
    struct vmci_handle dst;
    struct vmci_handle src;
    uint64_t payload_size;
};

struct vmci_event_payld_ctx {
    uint32_t context_id;
    uint32_t _pad;
};

struct vmci_resource_query_msg {
    uint32_t num_resources;
    uint32_t _padding;
    uint32_t resources[1];
};

typedef struct BARInfo {
    int index;
    enum { BAR_TYPE_NONE = 0, BAR_TYPE_PIO, BAR_TYPE_MMIO, BAR_TYPE_RAM } type;
    hwaddr size;
    const char *name;
} BARInfo;

#define VENDOR_ID         0x15AD
#define DEVICE_ID         0x0740
#define CLASS_ID          0x0880

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;
    uint32_t control;
    uint32_t caps;
    uint32_t guest_page_shift;
    uint32_t result;
    uint64_t data_in_high_addr;
    uint64_t data_out_high_addr;

    /* Incoming datagram buffer */
    uint8_t incoming_dgram_buf[VMCI_DMA_DG_BUFFER_SIZE];
    size_t incoming_dgram_size;
    bool incoming_dgram_pending;

    bool exclusive_vectors;
    uint32_t guest_context_id; /* assigned context id */

    /* BAR1 container and MMIO subregion */
    MemoryRegion bar1_mr;
    MemoryRegion mmio_region;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & ~s->intr_mask;

    if (!pending) {
        if (msix_enabled(pdev)) {
            /* MSI-X lower all vectors */
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 0);
        }
        return;
    }

    if (msix_enabled(pdev)) {
        if (pending & VMCI_ICR_DATAGRAM)
            msix_notify(pdev, 0);
        if (pending & VMCI_ICR_NOTIFICATION)
            msix_notify(pdev, 1);
        if (pending & VMCI_ICR_DMA_DATAGRAM)
            msix_notify(pdev, 2);
    } else if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

static uint64_t combine_addr(uint32_t low, uint64_t high)
{
    return ((uint64_t)high << 32) | low;
}

static void process_outgoing_datagram(PCIBaseState *s, uint64_t dma_addr)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct vmci_data_in_out_header header;
    struct vmci_datagram dg;
    uint8_t *buffer = g_malloc(VMCI_DMA_DG_BUFFER_SIZE);

    /* Read header */
    pci_dma_read(pdev, dma_addr, &header, sizeof(header));

    /* Read the datagram and payload (header.size bytes) */
    if (header.size > VMCI_DMA_DG_BUFFER_SIZE) {
        g_free(buffer);
        return;
    }
    pci_dma_read(pdev, dma_addr + sizeof(header), buffer, header.size);

    memcpy(&dg, buffer, sizeof(dg)); /* copy datagram header */

    uint32_t result = VMCI_SUCCESS;
    bool respond = false;

    if (dg.dst.context == VMCI_HYPERVISOR_CONTEXT_ID && dg.dst.resource == 0) {
        /* Datagram to hypervisor */
        if (header.opcode == VMCI_GET_CONTEXT_ID) {
            /* Assign context id and prepare response */
            s->guest_context_id = 1; /* simple assignment */
            result = VMCI_SUCCESS;
            respond = true;
        } else if (header.opcode == VMCI_RESOURCES_QUERY) {
            /* Resource query - just succeed */
            result = VMCI_GET_CONTEXT_ID; /* driver expects this? */
            respond = true;
        }
    }

    /* Update header and write back */
    header.busy = 0;
    header.result = result;
    pci_dma_write(pdev, dma_addr, &header, sizeof(header));
    s->result = (uint32_t)result;

    if (respond) {
        /* Build response datagram */
        struct vmci_data_in_out_header resp_header = {
            .busy = 0,  /* will be set by handle_data_in when DMA completed */
            .opcode = header.opcode,
            .size = sizeof(struct vmci_datagram) + sizeof(s->guest_context_id),
            .rsvd = 0,
            .result = 0
        };
        struct vmci_datagram resp_dg = {
            .dst = dg.src,       /* reply to sender */
            .src = { .context = VMCI_HYPERVISOR_CONTEXT_ID, .resource = 0 },
            .payload_size = sizeof(s->guest_context_id)
        };

        /* Copy header, datagram, and payload into incoming buffer */
        size_t offset = 0;
        memcpy(s->incoming_dgram_buf + offset, &resp_header, sizeof(resp_header));
        offset += sizeof(resp_header);
        memcpy(s->incoming_dgram_buf + offset, &resp_dg, sizeof(resp_dg));
        offset += sizeof(resp_dg);
        memcpy(s->incoming_dgram_buf + offset, &s->guest_context_id, sizeof(s->guest_context_id));
        offset += sizeof(s->guest_context_id);
        s->incoming_dgram_size = offset;
        s->incoming_dgram_pending = true;

        /* Signal datagram available interrupt */
        s->intr_status |= VMCI_ICR_DATAGRAM;
        pcibase_update_irq(s);
    }

    g_free(buffer);
}

static void handle_data_in(PCIBaseState *s, uint64_t dma_addr)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct vmci_data_in_out_header header;
    struct vmci_sg_elem sg;

    if (!s->incoming_dgram_pending) {
        /* No datagram, just set busy=0 */
        pci_dma_read(pdev, dma_addr, &header, sizeof(header));
        header.busy = 0;
        pci_dma_write(pdev, dma_addr, &header, sizeof(header));
        return;
    }

    /* Read SG from guest */
    pci_dma_read(pdev, dma_addr + sizeof(header), &sg, sizeof(sg));

    /* Write incoming datagram buffer to guest at sg.addr */
    pci_dma_write(pdev, sg.addr, s->incoming_dgram_buf, s->incoming_dgram_size);

    /* Update header: busy=1, size = datagram size */
    header.busy = 1;
    header.size = s->incoming_dgram_size - sizeof(header);
    pci_dma_write(pdev, dma_addr, &header, sizeof(header));

    s->incoming_dgram_pending = false;

    /* Signal DMA completion */
    s->intr_status |= VMCI_ICR_DMA_DATAGRAM;
    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    switch (addr) {
    case VMCI_ICR_ADDR:
        val = s->intr_status;
        s->intr_status = 0;
        pcibase_update_irq(s);
        break;
    case VMCI_IMR_ADDR:
        val = s->intr_mask;
        break;
    case VMCI_RESULT_LOW_ADDR:
        val = s->result;
        break;
    case VMCI_CONTROL_ADDR:
        val = s->control;
        break;
    case VMCI_CAPS_ADDR:
        val = s->caps;
        break;
    case VMCI_GUEST_PAGE_SHIFT:
        val = s->guest_page_shift;
        break;
    case VMCI_DATA_IN_LOW_ADDR:
        val = 0; /* write-only */
        break;
    case VMCI_DATA_IN_HIGH_ADDR:
        val = (uint32_t)(s->data_in_high_addr >> 32);
        break;
    case VMCI_DATA_OUT_LOW_ADDR:
        val = 0;
        break;
    case VMCI_DATA_OUT_HIGH_ADDR:
        val = (uint32_t)(s->data_out_high_addr >> 32);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "vmw_vmci: MMIO read unknown reg 0x%" HWADDR_PRIx ", size %d\n", addr, size);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case VMCI_ICR_ADDR:
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case VMCI_IMR_ADDR:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case VMCI_RESULT_LOW_ADDR:
        s->result = val;
        break;
    case VMCI_CONTROL_ADDR:
        s->control = val;
        if (val & VMCI_CONTROL_RESET) {
            /* Reset device */
            s->intr_status = 0;
            s->intr_mask = 0;
            s->control = 0;
            s->caps = VMCI_CAPS_DATAGRAM | VMCI_CAPS_DMA_DATAGRAM | VMCI_CAPS_NOTIFICATIONS | VMCI_CAPS_PPN64;
            s->guest_page_shift = 12;
            s->result = 0;
            s->data_in_high_addr = 0;
            s->data_out_high_addr = 0;
            s->incoming_dgram_pending = false;
            pcibase_update_irq(s);
        }
        break;
    case VMCI_CAPS_ADDR:
        /* Accept write but capabilities read-only */
        break;
    case VMCI_GUEST_PAGE_SHIFT:
        s->guest_page_shift = val;
        break;
    case VMCI_DATA_IN_LOW_ADDR:
    {
        uint64_t dma_addr = combine_addr((uint32_t)val, s->data_in_high_addr);
        handle_data_in(s, dma_addr);
        break;
    }
    case VMCI_DATA_IN_HIGH_ADDR:
        s->data_in_high_addr = (uint64_t)val << 32;
        break;
    case VMCI_DATA_OUT_LOW_ADDR:
    {
        uint64_t dma_addr = combine_addr((uint32_t)val, s->data_out_high_addr);
        process_outgoing_datagram(s, dma_addr);
        break;
    }
    case VMCI_DATA_OUT_HIGH_ADDR:
        s->data_out_high_addr = (uint64_t)val << 32;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "vmw_vmci: MMIO write unknown reg 0x%" HWADDR_PRIx ", val 0x%" PRIx64 ", size %d\n", addr, val, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (size == 4) {
        switch (addr) {
        case VMCI_ICR_ADDR:
            val = s->intr_status;
            s->intr_status = 0;
            pcibase_update_irq(s);
            break;
        case VMCI_IMR_ADDR:
            val = s->intr_mask;
            break;
        case VMCI_RESULT_LOW_ADDR:
            val = s->result;
            break;
        case VMCI_CONTROL_ADDR:
            val = s->control;
            break;
        case VMCI_CAPS_ADDR:
            val = s->caps;
            break;
        case VMCI_GUEST_PAGE_SHIFT:
            val = s->guest_page_shift;
            break;
        case VMCI_DATA_IN_ADDR:
            val = 0;
            break;
        case VMCI_DATA_OUT_ADDR:
            val = 0;
            break;
        default:
            val = 0;
            break;
        }
    } else if (size == 1) {
        if (addr == VMCI_DATA_IN_ADDR || addr == VMCI_DATA_OUT_ADDR) {
            val = 0;
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (size == 4) {
        switch (addr) {
        case VMCI_ICR_ADDR:
            s->intr_status &= ~val;
            pcibase_update_irq(s);
            break;
        case VMCI_IMR_ADDR:
            s->intr_mask = val;
            pcibase_update_irq(s);
            break;
        case VMCI_RESULT_LOW_ADDR:
            s->result = val;
            break;
        case VMCI_CONTROL_ADDR:
            s->control = val;
            if (val & VMCI_CONTROL_RESET) {
                s->intr_status = 0;
                s->intr_mask = 0;
                s->control = 0;
                s->caps = VMCI_CAPS_DATAGRAM | VMCI_CAPS_DMA_DATAGRAM | VMCI_CAPS_NOTIFICATIONS | VMCI_CAPS_PPN64;
                s->guest_page_shift = 12;
                s->result = 0;
                s->incoming_dgram_pending = false;
                pcibase_update_irq(s);
            }
            break;
        case VMCI_CAPS_ADDR:
            break;
        case VMCI_GUEST_PAGE_SHIFT:
            s->guest_page_shift = val;
            break;
        case VMCI_DATA_IN_ADDR:
        case VMCI_DATA_OUT_ADDR:
            break;
        default:
            break;
        }
    } else if (size == 1) {
        /* byte writes ignored */
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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
    s->intr_status = 0;
    s->intr_mask = 0;
    s->control = 0;
    s->caps = VMCI_CAPS_DATAGRAM | VMCI_CAPS_DMA_DATAGRAM | VMCI_CAPS_NOTIFICATIONS | VMCI_CAPS_PPN64;
    s->guest_page_shift = 12;
    s->result = 0;
    s->data_in_high_addr = 0;
    s->data_out_high_addr = 0;
    s->incoming_dgram_pending = false;
    s->guest_context_id = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: PIO */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = VMCI_IO_SIZE, .name = "vmci-io" };
    hwaddr aligned_size0 = pow2ceil(s->bar_info[0].size);
    MemoryRegion *mr0 = &s->bar_regions[0];
    memory_region_init_io(mr0, OBJECT(s), &pcibase_pio_ops, s, "vmci-io", aligned_size0);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, mr0);

    /* BAR1: MMIO container with subregion for registers and MSI-X */
    memory_region_init(&s->bar1_mr, OBJECT(s), "vmci-bar1", VMCI_WITH_MMIO_ACCESS_BAR_SIZE);
    memory_region_init_io(&s->mmio_region, OBJECT(s), &pcibase_mmio_ops, s, "vmci-mmio", VMCI_MMIO_ACCESS_SIZE);
    memory_region_add_subregion(&s->bar1_mr, VMCI_MMIO_ACCESS_OFFSET, &s->mmio_region);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar1_mr);

    int max_vectors = VMCI_MAX_INTRS;
    if (msix_init(pdev, max_vectors,
                  &s->bar1_mr, 1, 0,  /* MSI-X table at start of BAR1 */
                  &s->bar1_mr, 1, VMCI_MMIO_ACCESS_OFFSET + VMCI_MMIO_ACCESS_SIZE,  /* PBA at offset 192KB */
                  0, errp)) {
        if (msi_init(pdev, 0, 1, true, false, errp)) {
        }
        s->exclusive_vectors = false;
    } else {
        s->exclusive_vectors = true;
    }

    pcibase_reset(DEVICE(pdev));
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
    .name = "vmw_vmci_pci",
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
