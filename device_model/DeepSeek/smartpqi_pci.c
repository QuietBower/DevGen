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

#define TYPE_PCIBASE_DEVICE "smartpqi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_SMARTPQI 0x9005
#define PCI_DEVICE_ID_SMARTPQI 0x028f
#define CLASS_ID 0x0104
#define BAR0_SIZE 0x10000  /* 64KB: covers all ctrl registers + PQI */

/* Updated constants from supplementary driver source */
#define PQI_DEVICE_SIGNATURE "PQI DREG"
#define PQI_STATUS_IDLE 0x0
#define PQI_DEVICE_STATE_ALL_REGISTERS_READY 0x2
#define PQI_CREATE_ADMIN_QUEUE_PAIR 1

/* Register structures (packed to match hardware offsets) */

struct QEMU_PACKED smartpqi_pqi_regs {
    uint64_t signature;                               /* 0x00 */
    uint8_t  function_and_status_code;                /* 0x08 */
    uint8_t  reserved[7];                              /* 0x09-0x0f */
    uint8_t  max_admin_iq_elements;                   /* 0x10 */
    uint8_t  max_admin_oq_elements;                   /* 0x11 */
    uint8_t  admin_iq_element_length;                 /* 0x12 */
    uint8_t  admin_oq_element_length;                 /* 0x13 */
    uint16_t max_reset_timeout;                        /* 0x14 */
    uint8_t  reserved1[2];                             /* 0x16-0x17 */
    uint32_t legacy_intx_status;                      /* 0x18 */
    uint32_t legacy_intx_mask_set;                    /* 0x1c */
    uint32_t legacy_intx_mask_clear;                  /* 0x20 */
    uint8_t  reserved2[28];                            /* 0x24-0x3f */
    uint32_t device_status;                           /* 0x40 */
    uint8_t  reserved3[4];                             /* 0x44-0x47 */
    uint64_t admin_iq_pi_offset;                      /* 0x48 */
    uint64_t admin_oq_ci_offset;                      /* 0x50 */
    uint64_t admin_iq_element_array_addr;             /* 0x58 */
    uint64_t admin_oq_element_array_addr;             /* 0x60 */
    uint64_t admin_iq_ci_addr;                        /* 0x68 */
    uint64_t admin_oq_pi_addr;                        /* 0x70 */
    uint8_t  admin_iq_num_elements;                   /* 0x78 */
    uint8_t  admin_oq_num_elements;                   /* 0x79 */
    uint16_t admin_queue_int_msg_num;                 /* 0x7a */
    uint8_t  reserved4[4];                             /* 0x7c-0x7f */
    uint32_t device_error;                            /* 0x80 */
    uint8_t  reserved5[4];                             /* 0x84-0x87 */
    uint64_t error_details;                           /* 0x88 */
    uint32_t device_reset;                            /* 0x90 */
    uint32_t power_action;                            /* 0x94 */
    uint8_t  reserved6[104];                           /* 0x98-0xff */
};

struct QEMU_PACKED smartpqi_ctrl_regs {
    uint8_t  reserved[0x20];                           /* 0x00-0x1f */
    uint32_t sis_host_to_ctrl_doorbell;               /* 0x20 */
    uint8_t  reserved1[0x10];                          /* 0x24-0x33 */
    uint32_t sis_interrupt_mask;                      /* 0x34 */
    uint8_t  reserved2[0x64];                          /* 0x38-0x9b */
    uint32_t sis_ctrl_to_host_doorbell;               /* 0x9c */
    uint32_t sis_ctrl_to_host_doorbell_clear;         /* 0xa0 */
    uint8_t  reserved4[0xc];                           /* 0xa4-0xaf */
    uint32_t sis_driver_scratch;                      /* 0xb0 */
    uint32_t sis_product_identifier;                  /* 0xb4 */
    uint8_t  reserved5[0x4];                           /* 0xb8-0xbb */
    uint32_t sis_firmware_status;                     /* 0xbc */
    uint8_t  reserved6[0xc];                           /* 0xc0-0xcb */
    uint32_t sis_ctrl_shutdown_reason_code;           /* 0xcc */
    uint8_t  reserved7[0xf30];                         /* 0xd0-0xfff */
    uint32_t sis_mailbox[8];                          /* 0x1000 */
    uint8_t  reserved8[0x2fe0];                        /* 0x1020-0x3fff */
    struct   smartpqi_pqi_regs pqi_regs;              /* 0x4000 */
};

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
    struct smartpqi_ctrl_regs regs;

    /* DMA Context */
    dma_addr_t admin_iq_dma;
    dma_addr_t admin_oq_dma;
    dma_addr_t operational_q_dma;

    uint32_t device_status;      /* Operational status flags */
    uint8_t soft_reset_status;   /* State used to handle reset sequences */
    uint8_t power_state;         /* Power management state (D0-D3) */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (msi_enabled(pdev)) {
        if (s->intr_status & s->intr_mask) {
            msi_notify(pdev, 0);
        }
    } else if (msix_enabled(pdev)) {
        if (s->intr_status & s->intr_mask) {
            msix_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, (s->intr_status & s->intr_mask) ? 1 : 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Not used in the probe path, placeholder for future I/O */
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < offsetof(struct smartpqi_ctrl_regs, pqi_regs)) {
        /* SIS area reads: return 0 */
        return 0;
    }

    addr -= offsetof(struct smartpqi_ctrl_regs, pqi_regs);
    if (addr >= sizeof(s->regs.pqi_regs)) {
        return 0;
    }

    switch (addr) {
    case offsetof(struct smartpqi_pqi_regs, signature):
        memcpy(&val, PQI_DEVICE_SIGNATURE, 8);
        break;
    case offsetof(struct smartpqi_pqi_regs, function_and_status_code):
        val = s->regs.pqi_regs.function_and_status_code;
        break;
    case offsetof(struct smartpqi_pqi_regs, max_admin_iq_elements):
        val = s->regs.pqi_regs.max_admin_iq_elements;
        break;
    case offsetof(struct smartpqi_pqi_regs, max_admin_oq_elements):
        val = s->regs.pqi_regs.max_admin_oq_elements;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_iq_element_length):
        val = s->regs.pqi_regs.admin_iq_element_length;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_oq_element_length):
        val = s->regs.pqi_regs.admin_oq_element_length;
        break;
    case offsetof(struct smartpqi_pqi_regs, max_reset_timeout):
        val = s->regs.pqi_regs.max_reset_timeout;
        break;
    case offsetof(struct smartpqi_pqi_regs, legacy_intx_status):
        val = s->regs.pqi_regs.legacy_intx_status;
        break;
    case offsetof(struct smartpqi_pqi_regs, legacy_intx_mask_set):
        val = s->regs.pqi_regs.legacy_intx_mask_set;
        break;
    case offsetof(struct smartpqi_pqi_regs, legacy_intx_mask_clear):
        val = s->regs.pqi_regs.legacy_intx_mask_clear;
        break;
    case offsetof(struct smartpqi_pqi_regs, device_status):
        val = s->regs.pqi_regs.device_status;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_iq_pi_offset):
        val = s->regs.pqi_regs.admin_iq_pi_offset;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_oq_ci_offset):
        val = s->regs.pqi_regs.admin_oq_ci_offset;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_iq_element_array_addr):
        val = s->regs.pqi_regs.admin_iq_element_array_addr;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_oq_element_array_addr):
        val = s->regs.pqi_regs.admin_oq_element_array_addr;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_iq_ci_addr):
        val = s->regs.pqi_regs.admin_iq_ci_addr;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_oq_pi_addr):
        val = s->regs.pqi_regs.admin_oq_pi_addr;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_iq_num_elements):
        val = s->regs.pqi_regs.admin_iq_num_elements;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_oq_num_elements):
        val = s->regs.pqi_regs.admin_oq_num_elements;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_queue_int_msg_num):
        val = s->regs.pqi_regs.admin_queue_int_msg_num;
        break;
    case offsetof(struct smartpqi_pqi_regs, device_error):
        val = s->regs.pqi_regs.device_error;
        break;
    case offsetof(struct smartpqi_pqi_regs, error_details):
        val = s->regs.pqi_regs.error_details;
        break;
    case offsetof(struct smartpqi_pqi_regs, device_reset):
        val = s->regs.pqi_regs.device_reset;
        break;
    case offsetof(struct smartpqi_pqi_regs, power_action):
        val = s->regs.pqi_regs.power_action;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < offsetof(struct smartpqi_ctrl_regs, pqi_regs)) {
        /* SIS area writes: ignore */
        return;
    }

    addr -= offsetof(struct smartpqi_ctrl_regs, pqi_regs);
    if (addr >= sizeof(s->regs.pqi_regs)) {
        return;
    }

    switch (addr) {
    case offsetof(struct smartpqi_pqi_regs, signature):
        break; /* read-only */
    case offsetof(struct smartpqi_pqi_regs, function_and_status_code):
        s->regs.pqi_regs.function_and_status_code = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, max_admin_iq_elements):
        s->regs.pqi_regs.max_admin_iq_elements = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, max_admin_oq_elements):
        s->regs.pqi_regs.max_admin_oq_elements = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_iq_element_length):
        s->regs.pqi_regs.admin_iq_element_length = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_oq_element_length):
        s->regs.pqi_regs.admin_oq_element_length = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, max_reset_timeout):
        s->regs.pqi_regs.max_reset_timeout = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, legacy_intx_status):
        s->regs.pqi_regs.legacy_intx_status = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, legacy_intx_mask_set):
        s->regs.pqi_regs.legacy_intx_mask_set = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, legacy_intx_mask_clear):
        s->regs.pqi_regs.legacy_intx_mask_clear = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, device_status):
        s->regs.pqi_regs.device_status = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_iq_pi_offset):
        s->regs.pqi_regs.admin_iq_pi_offset = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_oq_ci_offset):
        s->regs.pqi_regs.admin_oq_ci_offset = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_iq_element_array_addr):
        s->regs.pqi_regs.admin_iq_element_array_addr = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_oq_element_array_addr):
        s->regs.pqi_regs.admin_oq_element_array_addr = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_iq_ci_addr):
        s->regs.pqi_regs.admin_iq_ci_addr = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_oq_pi_addr):
        s->regs.pqi_regs.admin_oq_pi_addr = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_iq_num_elements):
        s->regs.pqi_regs.admin_iq_num_elements = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_oq_num_elements):
        s->regs.pqi_regs.admin_oq_num_elements = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, admin_queue_int_msg_num):
        s->regs.pqi_regs.admin_queue_int_msg_num = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, device_error):
        s->regs.pqi_regs.device_error = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, error_details):
        s->regs.pqi_regs.error_details = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, device_reset):
        s->regs.pqi_regs.device_reset = val;
        break;
    case offsetof(struct smartpqi_pqi_regs, power_action):
        s->regs.pqi_regs.power_action = val;
        break;
    default:
        break;
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
    memset(&s->regs, 0, sizeof(s->regs));
    s->device_status = 0;
    s->soft_reset_status = 0;
    s->power_state = 0;
    /* Set initial ready state for PQI mode */
    s->regs.pqi_regs.device_status = PQI_DEVICE_STATE_ALL_REGISTERS_READY;
    s->regs.pqi_regs.function_and_status_code = PQI_STATUS_IDLE;
    /* Set some default capabilities */
    s->regs.pqi_regs.max_admin_iq_elements = 255;
    s->regs.pqi_regs.max_admin_oq_elements = 255;
    s->regs.pqi_regs.admin_iq_element_length = 64 / 16; /* 64 bytes */
    s->regs.pqi_regs.admin_oq_element_length = 64 / 16;
    s->regs.pqi_regs.max_reset_timeout = 1000; /* 1000 * 100ms = 100s? */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_SMARTPQI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_SMARTPQI);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "smartpqi-bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Enable MSI-x or MSI */
    if (!msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = true;
    } else {
        int msix_cap = pci_add_capability(pdev, PCI_CAP_ID_MSIX, 0, 12, errp);
        if (msix_cap >= 0) {
            if (msix_init(pdev, 1, &s->bar_regions[0], 0, 0, &s->bar_regions[0], 0, 0, msix_cap, errp) == 0) {
                s->has_msix = true;
            }
        }
    }
    pcibase_reset(DEVICE(s));
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
    .name = "smartpqi_pci",
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