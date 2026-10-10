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

#define TYPE_PCIBASE_DEVICE "nitro_enclaves_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_AMAZON
#define PCI_VENDOR_ID_AMAZON 0x1d0f
#endif

#define PCI_DEVICE_ID_NE 0xe4c1
#define PCI_BAR_NE 0x03

#define NE_ENABLE 0x0000
#define NE_VERSION 0x0002
#define NE_COMMAND 0x0004
#define NE_SEND_DATA 0x0010
#define NE_RECV_DATA 0x0100

#define NE_ENABLE_ON 0x01
#define NE_ENABLE_OFF 0x00
#define NE_VERSION_MAX 0x0001

#define NE_SEND_DATA_SIZE 240
#define NE_RECV_DATA_SIZE 240

#define NE_VEC_REPLY 0
#define NE_VEC_EVENT 1

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t enable;
    uint16_t version;
    uint32_t command;
    uint8_t send_data[NE_SEND_DATA_SIZE];
    uint8_t recv_data[NE_RECV_DATA_SIZE];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->has_msix) {
        msix_notify(pdev, NE_VEC_REPLY);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == NE_ENABLE) {
        val = s->enable;
    } else if (addr == NE_VERSION) {
        val = s->version;
    } else if (addr >= NE_SEND_DATA && addr + size <= NE_SEND_DATA + NE_SEND_DATA_SIZE) {
        memcpy(&val, &s->send_data[addr - NE_SEND_DATA], size);
    } else if (addr >= NE_RECV_DATA && addr + size <= NE_RECV_DATA + NE_RECV_DATA_SIZE) {
        memcpy(&val, &s->recv_data[addr - NE_RECV_DATA], size);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == NE_ENABLE) {
        s->enable = val & 0xFF;
    } else if (addr == NE_VERSION) {
        s->version = val & 0xFFFF;
    } else if (addr == NE_COMMAND) {
        s->command = val;
        
        /* Process command: mock a successful reply */
        memset(s->recv_data, 0, NE_RECV_DATA_SIZE);
        int32_t rc = 0;
        memcpy(s->recv_data, &rc, sizeof(rc));
        
        /* Trigger reply interrupt */
        pcibase_update_irq(s);
    } else if (addr >= NE_SEND_DATA && addr + size <= NE_SEND_DATA + NE_SEND_DATA_SIZE) {
        memcpy(&s->send_data[addr - NE_SEND_DATA], &val, size);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->enable = NE_ENABLE_OFF;
    s->version = 0;
    s->command = 0;
    memset(s->send_data, 0, NE_SEND_DATA_SIZE);
    memset(s->recv_data, 0, NE_RECV_DATA_SIZE);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMAZON );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NE );
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
    s->num_bars = 1;
    s->bar_info[0].index = PCI_BAR_NE;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* Size set to 0x2000 to accommodate MSI-X tables at 0x1000 and 0x1800 */
    s->bar_info[0].size = 0x2000;
    s->bar_info[0].name = "nitro_enclaves";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X setup requested via ne_setup_msix */  
    if (msix_init(pdev, 2, &s->bar_regions[PCI_BAR_NE], PCI_BAR_NE, 0x1000, &s->bar_regions[PCI_BAR_NE], PCI_BAR_NE, 0x1800, 0, errp) == 0) {
        s->has_msix = true;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[PCI_BAR_NE], &s->bar_regions[PCI_BAR_NE]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
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
