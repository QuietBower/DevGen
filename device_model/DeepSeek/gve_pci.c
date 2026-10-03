/*
 * QEMU GVE Virtual Device Model (QEMU 8.2.10)
 * Implements MMIO registers, admin queue doorbell, MSI-X, and driver version write.
 * Phase 4: Fixed BAR assignments to match driver's expected register and doorbell indices.
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

#define TYPE_PCIBASE_DEVICE "gve_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define GVE_ADMINQ_BUFFER_SIZE 4096

#define PCI_VENDOR_ID_GOOGLE 0x1ae0
#define PCI_DEV_ID_GVNIC     0x0042
#define PCI_CLASS_ID_NET     0x0200

/* Register offsets */
#define REG_DEVICE_STATUS         0x00
#define REG_DRIVER_STATUS         0x04
#define REG_MAX_TX_QUEUES         0x08
#define REG_MAX_RX_QUEUES         0x0c
#define REG_ADMINQ_PFN            0x10
#define REG_ADMINQ_DOORBELL       0x14
#define REG_ADMINQ_EVENT_COUNTER  0x18
#define REG_DRIVER_VERSION        0x20
#define REG_ADMINQ_BASE_ADDR_HI   0x24
#define REG_ADMINQ_BASE_ADDR_LO   0x28
#define REG_ADMINQ_LENGTH         0x2c

/* Placeholder admin queue opcodes (to be verified against driver definitions) */
#define GVE_ADMINQ_VERIFY_DRIVER_COMPATIBILITY    0x01
#define GVE_ADMINQ_DESCRIBE_DEVICE                0x02
#define GVE_ADMINQ_CONFIGURE_DEVICE_RESOURCES     0x03
#define GVE_DRIVER_STATUS_RUN_MASK                0x1
#define GVE_ADMINQ_DEVICE_DESCRIPTOR_VERSION      0x1

#define GVE_IRQ_MASK   BIT(30)
#define GVE_IRQ_EVENT  BIT(29)
#define GVE_IRQ_ACK    BIT(31)

enum gve_queue_format {
    GVE_QUEUE_FORMAT_UNSPECIFIED = 0x0,
    GVE_GQI_RDA_FORMAT           = 0x1,
    GVE_GQI_QPL_FORMAT          = 0x2,
    GVE_DQO_RDA_FORMAT          = 0x3,
    GVE_DQO_QPL_FORMAT          = 0x4,
};

struct gve_device_descriptor {
    __be64 max_registered_pages;
    __be16 reserved1;
    __be16 tx_queue_entries;
    __be16 rx_queue_entries;
    __be16 default_num_queues;
    __be16 mtu;
    __be16 counters;
    __be16 tx_pages_per_qpl;
    __be16 rx_pages_per_qpl;
    uint8_t mac[6];
    __be16 num_device_options;
    __be16 total_length;
    uint8_t reserved2[6];
};

/*
 * The admin command format matches the union gve_adminq_command used by the driver.
 * The first 64 bytes are used; the union overlays various command-specific payloads
 * after the common opcode/status header.
 */
typedef struct GVEAdminQCommand {
    uint32_t opcode;   /* big-endian */
    uint32_t status;   /* big-endian */
    uint8_t reserved[56];
} GVEAdminQCommand;

typedef struct GVERegisters {
    uint32_t device_status;
    uint32_t driver_status;
    uint32_t max_tx_queues;
    uint32_t max_rx_queues;
    uint32_t adminq_pfn;
    uint32_t adminq_doorbell;
    uint32_t adminq_event_counter;
    uint8_t  reserved2[3];
    uint8_t  driver_version;
    uint32_t adminq_base_address_hi;
    uint32_t adminq_base_address_lo;
    uint16_t adminq_length;
} QEMU_PACKED GVERegisters;

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;
    GVERegisters regs;
    QEMUTimer adminq_timer;
    bool adminq_pending;
    uint32_t adminq_event_counter;
    char driver_version_buf[128];
    int driver_version_len;
    uint64_t adminq_base_addr;  /* combined 64-bit base address */
    uint32_t adminq_head;       /* admin queue head pointer (in command slots) */
};

static void pcibase_update_adminq_base_addr(PCIBaseState *s)
{
    s->adminq_base_addr = ((uint64_t)s->regs.adminq_base_address_hi << 32)
                          | s->regs.adminq_base_address_lo;
}

static void pcibase_process_adminq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t addr = s->adminq_base_addr;
    uint32_t len = s->regs.adminq_length;
    if (!addr || !len) {
        return;
    }
    uint32_t slot_size = sizeof(GVEAdminQCommand);
    uint32_t num_slots = len / slot_size;
    if (num_slots == 0) {
        return;
    }
    uint32_t head = s->adminq_head % num_slots;
    dma_addr_t dma_addr = addr + head * slot_size;
    GVEAdminQCommand cmd;
    pci_dma_read(pdev, dma_addr, &cmd, sizeof(cmd));
    uint32_t opcode = be32_to_cpu(cmd.opcode);
    switch (opcode) {
    case GVE_ADMINQ_VERIFY_DRIVER_COMPATIBILITY:
        /* No data to write back; just set status to success */
        cmd.status = cpu_to_be32(0);
        break;
    case GVE_ADMINQ_DESCRIBE_DEVICE:
        /*
         * TODO: Write device descriptor to DMA buffer using
         * command's response address from gve_adminq_describe_device.
         * Need the definition of struct gve_adminq_describe_device.
         */
        cmd.status = cpu_to_be32(0);
        break;
    case GVE_ADMINQ_CONFIGURE_DEVICE_RESOURCES:
        cmd.status = cpu_to_be32(0);
        break;
    default:
        cmd.status = cpu_to_be32(0);  /* unknown opcode: succeed for now */
        break;
    }
    pci_dma_write(pdev, dma_addr, &cmd, sizeof(cmd));
    s->adminq_head++;
    s->adminq_event_counter++;
}

static void pcibase_adminq_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    s->adminq_pending = false;
    msix_notify(pdev, 2);
}

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (msix_enabled(pdev)) {
        msix_notify(pdev, 0);
    } else if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, !!(s->intr_status & ~s->intr_mask));
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x20 && addr < 0x20 + sizeof(s->driver_version_buf)) {
        uint32_t offset = addr - 0x20;
        if (offset < s->driver_version_len) {
            val = s->driver_version_buf[offset];
        }
    } else {
        switch (addr) {
        case REG_DEVICE_STATUS:
            val = s->regs.device_status;
            break;
        case REG_DRIVER_STATUS:
            val = s->regs.driver_status;
            break;
        case REG_MAX_TX_QUEUES:
            val = s->regs.max_tx_queues;
            break;
        case REG_MAX_RX_QUEUES:
            val = s->regs.max_rx_queues;
            break;
        case REG_ADMINQ_PFN:
            val = s->regs.adminq_pfn;
            break;
        case REG_ADMINQ_DOORBELL:
            val = 0;
            break;
        case REG_ADMINQ_EVENT_COUNTER:
            val = s->adminq_event_counter;
            break;
        case REG_ADMINQ_BASE_ADDR_HI:
            val = s->regs.adminq_base_address_hi;
            break;
        case REG_ADMINQ_BASE_ADDR_LO:
            val = s->regs.adminq_base_address_lo;
            break;
        case REG_ADMINQ_LENGTH:
            val = s->regs.adminq_length;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "GVE: unknown MMIO read 0x%x\n", (unsigned)addr);
            break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x20 && addr < 0x20 + sizeof(s->driver_version_buf)) {
        if (size == 1) {
            s->driver_version_buf[addr - 0x20] = (uint8_t)val;
            if ((int)(addr - 0x20 + 1) > s->driver_version_len) {
                s->driver_version_len = addr - 0x20 + 1;
            }
        }
        return;
    }

    switch (addr) {
    case REG_DEVICE_STATUS:
        s->regs.device_status = val;
        break;
    case REG_DRIVER_STATUS:
        s->regs.driver_status = val;
        break;
    case REG_ADMINQ_PFN:
        s->regs.adminq_pfn = val;
        break;
    case REG_ADMINQ_DOORBELL:
        /* Process pending admin command */
        pcibase_process_adminq(s);
        if (!s->adminq_pending) {
            s->adminq_pending = true;
            timer_mod(&s->adminq_timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + 100);
        }
        break;
    case REG_ADMINQ_EVENT_COUNTER:
        /* read-only; ignore writes */
        break;
    case REG_ADMINQ_BASE_ADDR_HI:
        s->regs.adminq_base_address_hi = val;
        pcibase_update_adminq_base_addr(s);
        break;
    case REG_ADMINQ_BASE_ADDR_LO:
        s->regs.adminq_base_address_lo = val;
        pcibase_update_adminq_base_addr(s);
        break;
    case REG_ADMINQ_LENGTH:
        s->regs.adminq_length = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "GVE: unknown MMIO write 0x%x val 0x%lx\n", (unsigned)addr, val);
        break;
    }
}

static uint64_t pcibase_db_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_db_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "GVE: doorbell write slot %d val 0x%lx\n", (int)(addr/4), val);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_db_mmio_ops = {
    .read = pcibase_db_mmio_read,
    .write = pcibase_db_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.max_tx_queues = 4;   /* restore default maximum transmit queues */
    s->regs.max_rx_queues = 4;   /* restore default maximum receive queues */
    s->regs.device_status = 0;
    s->regs.driver_status = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->adminq_event_counter = 0;
    s->adminq_pending = false;
    timer_del(&s->adminq_timer);
    memset(s->driver_version_buf, 0, sizeof(s->driver_version_buf));
    s->driver_version_len = 0;
    s->adminq_base_addr = 0;
    s->adminq_head = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_GOOGLE);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEV_ID_GVNIC);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID_NET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 0: registers */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops, s, "bar0-regs", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR 2: doorbell */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_db_mmio_ops, s, "bar2-db", 0x1000);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);

    /* BAR 4: MSI-X */
    memory_region_init_ram(&s->bar_regions[4], OBJECT(s), "bar4-msix", 0x1000, &error_abort);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[4]);

    if (msix_init_exclusive_bar(pdev, 3, 4, errp)) {
        if (msi_init(pdev, 0, 1, true, false, errp)) {
            return;
        }
        s->has_msi = true;
    } else {
        s->has_msix = true;
    }

    timer_init_us(&s->adminq_timer, QEMU_CLOCK_VIRTUAL, pcibase_adminq_timer, s);

    s->regs.device_status = 0;
    s->regs.driver_status = 0;
    s->regs.max_tx_queues = 4;
    s->regs.max_rx_queues = 4;
    s->regs.adminq_pfn = 0;
    s->regs.adminq_doorbell = 0;
    s->regs.adminq_event_counter = 0;
    s->regs.driver_version = 0;
    s->regs.adminq_base_address_hi = 0;
    s->regs.adminq_base_address_lo = 0;
    s->regs.adminq_length = 0;
    s->adminq_base_addr = 0;
    s->adminq_head = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(&s->adminq_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "gve_pci",
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
