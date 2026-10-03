/*
 * QEMU Nitro Enclaves PCI device model
 * Based on Linux driver ne_pci_dev.c
 * QEMU 8.2.10
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

#define TYPE_PCIBASE_DEVICE "nitro_enclaves_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID         0x1d0f
#define DEVICE_ID         0xe4c1
#define CLASS_ID          0x00ff00

#define NE_ENABLE         0x0000
#define NE_VERSION        0x0002
#define NE_COMMAND        0x0004
#define NE_SEND_DATA      0x0010
#define NE_RECV_DATA      0x0100
#define NE_SEND_DATA_SIZE  240
#define NE_RECV_DATA_SIZE  240

/* Hardware constants */
#define NE_VERSION_MAX    1
#define NE_ENABLE_ON      1
#define NE_ENABLE_OFF     0

/* MSI-X vector indices */
#define NE_VEC_REPLY      0
#define NE_VEC_EVENT      1

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
    uint32_t irq_status;

    /* Hardware Register Shadows */
    uint32_t reg_enable;
    uint16_t reg_version;
    uint32_t reg_command;
    uint8_t send_data[NE_SEND_DATA_SIZE];
    uint8_t recv_data[NE_RECV_DATA_SIZE];
    uint32_t reg_status; /* placeholder */

    /* Command processing */
    QEMUBH *cmd_bh;
};

/* Command type enumeration (from driver) */
enum ne_pci_dev_cmd_type {
    INVALID_CMD = 0,
    ENCLAVE_START = 1,
    ENCLAVE_GET_SLOT = 2,
    ENCLAVE_STOP = 3,
    SLOT_ALLOC = 4,
    SLOT_FREE = 5,
    SLOT_ADD_MEM = 6,
    SLOT_ADD_VCPU = 7,
    SLOT_COUNT = 8,
    NEXT_SLOT = 9,
    SLOT_INFO = 10,
    SLOT_ADD_BULK_VCPUS = 11,
    MAX_CMD,
};

/* Command reply structure (from driver) */
struct ne_pci_dev_cmd_reply {
    int32_t rc;
    uint8_t padding0[4];
    uint64_t slot_uid;
    uint64_t enclave_cid;
    uint64_t slot_count;
    uint64_t mem_regions;
    uint64_t mem_size;
    uint64_t nr_vcpus;
    uint64_t flags;
    uint16_t state;
    uint8_t padding1[6];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->irq_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO Read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case NE_ENABLE:
        if (size == 1) val = s->reg_enable;
        break;
    case NE_VERSION:
        if (size == 2) val = s->reg_version;
        break;
    case NE_COMMAND:
        if (size == 4) val = s->reg_command;
        break;
    default:
        if (addr >= NE_SEND_DATA && addr + size <= NE_SEND_DATA + NE_SEND_DATA_SIZE) {
            memcpy(&val, s->send_data + (addr - NE_SEND_DATA), MIN(size, sizeof(val)));
        } else if (addr >= NE_RECV_DATA && addr + size <= NE_RECV_DATA + NE_RECV_DATA_SIZE) {
            memcpy(&val, s->recv_data + (addr - NE_RECV_DATA), MIN(size, sizeof(val)));
        }
        break;
    }
    return val;
}

/* MMIO Write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case NE_ENABLE:
        if (size == 1) {
            s->reg_enable = val;
        }
        break;
    case NE_VERSION:
        if (size == 2) {
            s->reg_version = val;
        }
        break;
    case NE_COMMAND:
        if (size == 4) {
            s->reg_command = val;
            /* Trigger command processing if enabled and valid command */
            if (s->reg_enable == NE_ENABLE_ON && val > INVALID_CMD && val < MAX_CMD) {
                qemu_bh_schedule(s->cmd_bh);
            }
        }
        break;
    default:
        if (addr >= NE_SEND_DATA && addr + size <= NE_SEND_DATA + NE_SEND_DATA_SIZE) {
            memcpy(s->send_data + (addr - NE_SEND_DATA), &val, MIN(size, sizeof(val)));
        } else if (addr >= NE_RECV_DATA && addr + size <= NE_RECV_DATA + NE_RECV_DATA_SIZE) {
            memcpy(s->recv_data + (addr - NE_RECV_DATA), &val, MIN(size, sizeof(val)));
        }
        break;
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

static void ne_cmd_bh(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    struct ne_pci_dev_cmd_reply reply = {0};
    
    /* Dummy success reply */
    reply.rc = 0;
    
    /* Copy reply into device memory */
    memcpy(s->recv_data, &reply, sizeof(reply));
    
    /* Signal completion via MSI-X reply vector */
    if (msix_enabled(pdev)) {
        msix_notify(pdev, NE_VEC_REPLY);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->reg_enable = 0;
    s->reg_version = 0;
    s->reg_command = 0;
    s->irq_status = 0;
    memset(s->send_data, 0, sizeof(s->send_data));
    memset(s->recv_data, 0, sizeof(s->recv_data));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    MemoryRegion *bar3_mr, *cmd_mr;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR3: combined MMIO regs and MSI-X tables */
    bar3_mr = g_new0(MemoryRegion, 1);
    memory_region_init(bar3_mr, OBJECT(s), "ne-bar3", 0x1000);
    
    cmd_mr = g_new0(MemoryRegion, 1);
    memory_region_init_io(cmd_mr, OBJECT(s), &pcibase_mmio_ops, s, "ne-cmd", 0x800);
    memory_region_add_subregion(bar3_mr, 0x0, cmd_mr);
    
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, bar3_mr);
    
    if (msix_init(pdev, 2, bar3_mr, 3, 0x800, bar3_mr, 3, 0x900, 0, errp) < 0) {
        error_setg(errp, "Failed to init MSI-X");
        return;
    }

    s->cmd_bh = qemu_bh_new(ne_cmd_bh, s);
    
    s->reg_version = NE_VERSION_MAX;
    s->reg_enable = 0;
    s->reg_command = 0;
    s->irq_status = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    
    qemu_bh_delete(s->cmd_bh);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "nitro_enclaves_pci",
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
