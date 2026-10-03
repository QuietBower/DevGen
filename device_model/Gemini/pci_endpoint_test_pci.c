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
#define PCI_VENDOR_ID_TI		0x104c
#define PCI_DEVICE_ID_TI_DRA74x		0xb500

#define TYPE_PCIBASE_DEVICE "pci_endpoint_test_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_ENDPOINT_TEST_MAGIC			0x0
#define PCI_ENDPOINT_TEST_COMMAND		0x4
#define PCI_ENDPOINT_TEST_STATUS		0x8
#define PCI_ENDPOINT_TEST_LOWER_SRC_ADDR	0x0c
#define PCI_ENDPOINT_TEST_UPPER_SRC_ADDR	0x10
#define PCI_ENDPOINT_TEST_LOWER_DST_ADDR	0x14
#define PCI_ENDPOINT_TEST_UPPER_DST_ADDR	0x18
#define PCI_ENDPOINT_TEST_SIZE			0x1c
#define PCI_ENDPOINT_TEST_CHECKSUM		0x20
#define PCI_ENDPOINT_TEST_IRQ_TYPE		0x24
#define PCI_ENDPOINT_TEST_IRQ_NUMBER		0x28
#define PCI_ENDPOINT_TEST_FLAGS			0x2c
#define PCI_ENDPOINT_TEST_CAPS			0x30
#define PCI_ENDPOINT_TEST_DB_BAR		0x34
#define PCI_ENDPOINT_TEST_DB_OFFSET		0x38
#define PCI_ENDPOINT_TEST_DB_DATA		0x3c

#define COMMAND_RAISE_INTX_IRQ			BIT(0)
#define COMMAND_RAISE_MSI_IRQ			BIT(1)
#define COMMAND_RAISE_MSIX_IRQ			BIT(2)
#define COMMAND_READ				BIT(3)
#define COMMAND_WRITE				BIT(4)
#define COMMAND_COPY				BIT(5)
#define COMMAND_ENABLE_DOORBELL			BIT(6)
#define COMMAND_DISABLE_DOORBELL		BIT(7)
#define COMMAND_BAR_SUBRANGE_SETUP		BIT(8)
#define COMMAND_BAR_SUBRANGE_CLEAR		BIT(9)

#define STATUS_READ_SUCCESS			BIT(0)
#define STATUS_READ_FAIL			BIT(1)
#define STATUS_WRITE_SUCCESS			BIT(2)
#define STATUS_WRITE_FAIL			BIT(3)
#define STATUS_COPY_SUCCESS			BIT(4)
#define STATUS_COPY_FAIL			BIT(5)
#define STATUS_IRQ_RAISED			BIT(6)
#define STATUS_SRC_ADDR_INVALID			BIT(7)
#define STATUS_DST_ADDR_INVALID			BIT(8)
#define STATUS_DOORBELL_SUCCESS			BIT(9)
#define STATUS_DOORBELL_ENABLE_SUCCESS		BIT(10)
#define STATUS_DOORBELL_ENABLE_FAIL		BIT(11)
#define STATUS_DOORBELL_DISABLE_SUCCESS		BIT(12)
#define STATUS_DOORBELL_DISABLE_FAIL		BIT(13)
#define STATUS_BAR_SUBRANGE_SETUP_SUCCESS	BIT(14)
#define STATUS_BAR_SUBRANGE_SETUP_FAIL		BIT(15)
#define STATUS_BAR_SUBRANGE_CLEAR_SUCCESS	BIT(16)
#define STATUS_BAR_SUBRANGE_CLEAR_FAIL		BIT(17)
#define STATUS_NO_RESOURCE			BIT(18)

#define FLAG_USE_DMA				BIT(0)

#define CAP_UNALIGNED_ACCESS			BIT(0)
#define CAP_MSI					BIT(1)
#define CAP_MSIX				BIT(2)
#define CAP_INTX				BIT(3)
#define CAP_SUBRANGE_MAPPING			BIT(4)
#define CAP_DYNAMIC_INBOUND_MAPPING		BIT(5)
#define CAP_BAR0_RESERVED			BIT(6)
#define CAP_BAR1_RESERVED			BIT(7)
#define CAP_BAR2_RESERVED			BIT(8)
#define CAP_BAR3_RESERVED			BIT(9)
#define CAP_BAR4_RESERVED			BIT(10)
#define CAP_BAR5_RESERVED			BIT(11)

#define PCITEST_IRQ_TYPE_INTX		0
#define PCITEST_IRQ_TYPE_MSI		1
#define PCITEST_IRQ_TYPE_MSIX		2

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
    uint32_t irq_type;
    uint32_t irq_number;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t magic;
    uint32_t command;
    uint32_t status;
    uint32_t flags;
    uint32_t caps;
    uint32_t db_bar;
    uint32_t db_offset;
    uint32_t db_data;

    /* DMA Context */
    uint32_t lower_src_addr;
    uint32_t upper_src_addr;
    uint32_t lower_dst_addr;
    uint32_t upper_dst_addr;
    uint32_t size;
    uint32_t checksum;

};

/* Helper to compute standard CRC32 matching Linux crc32_le */
static uint32_t simple_crc32_le(uint32_t crc, const void *buf, size_t size) {
    const uint8_t *p = buf;
    for (size_t i = 0; i < size; i++) {
        crc ^= p[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320 : 0);
        }
    }
    return crc;
}

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->irq_type == PCITEST_IRQ_TYPE_INTX) {
        pci_set_irq(pdev, 1);
        pci_set_irq(pdev, 0);
    } else if (s->irq_type == PCITEST_IRQ_TYPE_MSI) {
        msi_notify(pdev, s->irq_number - 1);
    } else if (s->irq_type == PCITEST_IRQ_TYPE_MSIX) {
        msix_notify(pdev, s->irq_number - 1);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t src = ((uint64_t)s->upper_src_addr << 32) | s->lower_src_addr;
    uint64_t dst = ((uint64_t)s->upper_dst_addr << 32) | s->lower_dst_addr;
    void *buf;

    if (s->size == 0) return;

    buf = g_malloc0(s->size);

    if (s->command & COMMAND_READ) {
        pci_dma_read(pdev, src, buf, s->size);
        s->status |= STATUS_READ_SUCCESS;
    } else if (s->command & COMMAND_WRITE) {
        memset(buf, 0xAB, s->size);
        s->checksum = simple_crc32_le(0xffffffff, buf, s->size);
        pci_dma_write(pdev, dst, buf, s->size);
        s->status |= STATUS_WRITE_SUCCESS;
    } else if (s->command & COMMAND_COPY) {
        pci_dma_read(pdev, src, buf, s->size);
        pci_dma_write(pdev, dst, buf, s->size);
        s->status |= STATUS_COPY_SUCCESS;
    }

    g_free(buf);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PCI_ENDPOINT_TEST_MAGIC: val = s->magic; break;
    case PCI_ENDPOINT_TEST_COMMAND: val = s->command; break;
    case PCI_ENDPOINT_TEST_STATUS: val = s->status; break;
    case PCI_ENDPOINT_TEST_LOWER_SRC_ADDR: val = s->lower_src_addr; break;
    case PCI_ENDPOINT_TEST_UPPER_SRC_ADDR: val = s->upper_src_addr; break;
    case PCI_ENDPOINT_TEST_LOWER_DST_ADDR: val = s->lower_dst_addr; break;
    case PCI_ENDPOINT_TEST_UPPER_DST_ADDR: val = s->upper_dst_addr; break;
    case PCI_ENDPOINT_TEST_SIZE: val = s->size; break;
    case PCI_ENDPOINT_TEST_CHECKSUM: val = s->checksum; break;
    case PCI_ENDPOINT_TEST_IRQ_TYPE: val = s->irq_type; break;
    case PCI_ENDPOINT_TEST_IRQ_NUMBER: val = s->irq_number; break;
    case PCI_ENDPOINT_TEST_FLAGS: val = s->flags; break;
    case PCI_ENDPOINT_TEST_CAPS: val = s->caps; break;
    case PCI_ENDPOINT_TEST_DB_BAR: val = s->db_bar; break;
    case PCI_ENDPOINT_TEST_DB_OFFSET: val = s->db_offset; break;
    case PCI_ENDPOINT_TEST_DB_DATA: val = s->db_data; break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == 0x100 && val == 0xDBDBDBDB) {
        s->status |= STATUS_DOORBELL_SUCCESS | STATUS_IRQ_RAISED;
        pcibase_update_irq(s);
        return;
    }

    switch (addr) {
    case PCI_ENDPOINT_TEST_COMMAND:
        s->command = val;
        if (val & COMMAND_RAISE_INTX_IRQ) {
            s->status |= STATUS_IRQ_RAISED;
            pcibase_update_irq(s);
        } else if (val & COMMAND_RAISE_MSI_IRQ) {
            s->status |= STATUS_IRQ_RAISED;
            pcibase_update_irq(s);
        } else if (val & COMMAND_RAISE_MSIX_IRQ) {
            s->status |= STATUS_IRQ_RAISED;
            pcibase_update_irq(s);
        } else if (val & COMMAND_READ || val & COMMAND_WRITE || val & COMMAND_COPY) {
            pcibase_do_dma(s, false);
            s->status |= STATUS_IRQ_RAISED;
            pcibase_update_irq(s);
        } else if (val & COMMAND_ENABLE_DOORBELL) {
            s->db_bar = 0;
            s->db_offset = 0x100;
            s->db_data = 0xDBDBDBDB;
            s->status |= STATUS_DOORBELL_ENABLE_SUCCESS | STATUS_IRQ_RAISED;
            pcibase_update_irq(s);
        } else if (val & COMMAND_DISABLE_DOORBELL) {
            s->status |= STATUS_DOORBELL_DISABLE_SUCCESS | STATUS_IRQ_RAISED;
            pcibase_update_irq(s);
        } else if (val & COMMAND_BAR_SUBRANGE_SETUP) {
            s->status |= STATUS_BAR_SUBRANGE_SETUP_SUCCESS | STATUS_IRQ_RAISED;
            pcibase_update_irq(s);
        } else if (val & COMMAND_BAR_SUBRANGE_CLEAR) {
            s->status |= STATUS_BAR_SUBRANGE_CLEAR_SUCCESS | STATUS_IRQ_RAISED;
            pcibase_update_irq(s);
        }
        break;
    case PCI_ENDPOINT_TEST_STATUS: s->status = val; break;
    case PCI_ENDPOINT_TEST_LOWER_SRC_ADDR: s->lower_src_addr = val; break;
    case PCI_ENDPOINT_TEST_UPPER_SRC_ADDR: s->upper_src_addr = val; break;
    case PCI_ENDPOINT_TEST_LOWER_DST_ADDR: s->lower_dst_addr = val; break;
    case PCI_ENDPOINT_TEST_UPPER_DST_ADDR: s->upper_dst_addr = val; break;
    case PCI_ENDPOINT_TEST_SIZE: s->size = val; break;
    case PCI_ENDPOINT_TEST_CHECKSUM: s->checksum = val; break;
    case PCI_ENDPOINT_TEST_IRQ_TYPE: s->irq_type = val; break;
    case PCI_ENDPOINT_TEST_IRQ_NUMBER: s->irq_number = val; break;
    case PCI_ENDPOINT_TEST_FLAGS: s->flags = val; break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    s->caps = CAP_MSI | CAP_MSIX | CAP_INTX | CAP_DYNAMIC_INBOUND_MAPPING;
    s->magic = 0;
    s->command = 0;
    s->status = 0;
    s->lower_src_addr = 0;
    s->upper_src_addr = 0;
    s->lower_dst_addr = 0;
    s->upper_dst_addr = 0;
    s->size = 0;
    s->checksum = 0;
    s->irq_type = 0;
    s->irq_number = 0;
    s->flags = 0;
    s->db_bar = 0;
    s->db_offset = 0;
    s->db_data = 0;
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_TI_DRA74x );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x10000, "pci_endpoint_test.reg"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_RAM, 0x100000, "pci_endpoint_test.mem"};
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* msi_init or msix_init calls */
    msi_init(pdev, 0, 32, true, false, errp);
    msix_init(pdev, 32, &s->bar_regions[0], 0, 0x8000, &s->bar_regions[0], 0, 0x9000, 0, errp);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pci_endpoint_test_pci",
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
