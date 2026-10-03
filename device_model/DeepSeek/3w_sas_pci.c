/* This template provides a robust skeleton for hardware emulation.
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

/* Hardware command structures from driver (supplementary source) */
#define TW_SENSE_DATA_LENGTH 18
#define TW_MAX_SGL_LENGTH              62
#define TW_APACHE_MAX_SGL_LENGTH (sizeof(dma_addr_t) > 4 ? 72 : 109)
#define TW_PADDING_LENGTH (sizeof(dma_addr_t) > 4 ? 8 : 0)

typedef struct TAG_TW_SG_Entry {
    uint32_t address;
    uint32_t length;
} TW_SG_Entry;

typedef struct TAG_TW_Command_Apache_Header {
    unsigned char sense_data[TW_SENSE_DATA_LENGTH];
    struct {
        uint8_t reserved[4];
        uint16_t error;
        uint8_t padding;
        uint8_t severity__reserved;
    } status_block;
    unsigned char err_specific_desc[98];
    struct {
        uint8_t size_header;
        uint8_t reserved[2];
        uint8_t size_sense;
    } header_desc;
} TW_Command_Apache_Header;

struct TW_Command {
    uint8_t opcode__sgloffset;
    uint8_t size;
    uint8_t request_id;
    uint8_t unit__hostid;
    /* Second DWORD */
    uint8_t status;
    uint8_t flags;
    union {
        unsigned short block_count;
        unsigned short parameter_count;
        unsigned short message_credits;
    } byte6;
    union {
        struct {
            uint32_t lba;
            TW_SG_Entry sgl[TW_MAX_SGL_LENGTH];
            uint32_t padding;    /* pad to 512 bytes */
        } io;
        struct {
            TW_SG_Entry sgl[TW_MAX_SGL_LENGTH];
            uint32_t padding[2];
        } param;
        struct {
            uint32_t response_queue_pointer;
            uint32_t padding[125];
        } init_connection;
        struct {
            char version[504];
        } ioctl_miniport_version;
    } byte8;
};

typedef struct TAG_TW_Command_Apache {
    uint8_t opcode__reserved;
    uint8_t unit;
    uint16_t request_id__lunl;   /* __le16 in kernel */
    uint8_t status;
    uint8_t sgl_offset;
    uint16_t sgl_entries__lunh;  /* __le16 in kernel */
    uint8_t cdb[16];
    TW_SG_Entry sg_list[TW_APACHE_MAX_SGL_LENGTH];
    uint8_t padding[TW_PADDING_LENGTH];
} TW_Command_Apache;

typedef struct TAG_TW_Command_Full {
    TW_Command_Apache_Header header;
    union {
        struct TW_Command oldcommand;
        TW_Command_Apache newcommand;
    } command;
} TW_Command_Full;


typedef struct TAG_TW_SG_Entry_ISO {
    dma_addr_t address;
    dma_addr_t length;
} TW_SG_Entry_ISO;


#define TYPE_PCIBASE_DEVICE "3w_sas_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_3WARE 0x13C1
#define PCI_DEVICE_ID_3WARE_9750 0x1010
#define PCI_CLASS_ID PCI_CLASS_STORAGE_RAID

/* Register offsets from driver (3w-sas.c) */
#define TWL_STATUS       0x00
#define TWL_HIBDB        0x20
#define TWL_HISTAT       0x30
#define TWL_HIMASK       0x34
#define TWL_HOBDB        0x9C
#define TWL_HOBDBC       0xA0
#define TWL_SCRPD3       0xBC
#define TWL_HIBQPL       0xC0
#define TWL_HIBQPH       0xC4
#define TWL_HOBQPL       0xC8
#define TWL_HOBQPH       0xCC

/* Status register bits */
#define TWL_CONTROLLER_READY           0x2000
#define TWL_STATUS_OVERRUN_SUBMIT      0x2000
#define TWL_HISTATUS_VALID_INTERRUPT   0xC
#define TWL_HISTATUS_ATTENTION_INTERRUPT 0x4
#define TWL_HISTATUS_RESPONSE_INTERRUPT  0x8
#define TWL_DOORBELL_ATTENTION_INTERRUPT 0x40000
#define TWL_DOORBELL_CONTROLLER_ERROR    0x200000
#define TWL_ISSUE_SOFT_RESET           0x100

/* Command and SGL constants */
#define TW_MAX_SLOT         32
#define TW_LIBERATOR_MAX_SGL_LENGTH (sizeof(dma_addr_t) > 4 ? 46 : 92)
#define TW_Q_LENGTH         256
#define TW_COMMAND_SIZE     (sizeof(dma_addr_t) > 4 ? 6 : 4)
#define TW_COMMAND_OFFSET   128
#define TW_MAX_CDB_LEN      16

#define BAR0_SIZE           0x1000  /* Estimated from register offsets */

/* Opcode and AEN definitions from driver headers */
#define TW_OP_INIT_CONNECTION   0x1
#define TW_OP_EXECUTE_SCSI      0x10
#define TW_AEN_QUEUE_EMPTY      0x0000

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[BAR0_SIZE / sizeof(uint32_t)];

    /* DMA Context */
    uint8_t dma_buf[4096];  /* placeholder until command packet structures are known */

    /* Operational status flags */
    uint32_t status;

    /* State used to handle reset sequences */
    bool soft_reset_pending;
    int soft_reset_countdown;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* Inbound queue high part */
    uint32_t hibq_high;
    /* Sense buffer posting high part */
    uint32_t sense_buf_high;
    /* Outbound response queue */
    bool has_response;
    uint64_t response_val;
    /* Sense buffer pool */
    dma_addr_t sense_buf_addrs[TW_Q_LENGTH];
    int sense_buf_count;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->has_response && (s->regs[TWL_HIMASK >> 2] == 0)) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Command processing function */
static void process_inbound_command(PCIBaseState *s, uint64_t addr)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t opcode;
    pci_dma_read(pdev, addr, &opcode, 1);
    if (opcode == TW_OP_INIT_CONNECTION) {
        uint8_t request_id;
        pci_dma_read(pdev, addr + 2, &request_id, 1);
        s->response_val = request_id;
        s->has_response = true;
        pcibase_update_irq(s);
    } else if (opcode == TW_OP_EXECUTE_SCSI) {
        uint8_t cdb[16];
        pci_dma_read(pdev, addr + 8, cdb, 16);
        if (cdb[0] == 0x03) { /* REQUEST SENSE */
            if (s->sense_buf_count > 0) {
                dma_addr_t sense_buf_addr = s->sense_buf_addrs[0];
                TW_Command_Apache_Header sense_header;
                memset(&sense_header, 0, sizeof(sense_header));
                sense_header.status_block.error = cpu_to_le16(TW_AEN_QUEUE_EMPTY);
                pci_dma_write(pdev, sense_buf_addr, &sense_header, sizeof(sense_header));
                s->response_val = sense_buf_addr | (1ULL << 31);
                s->has_response = true;
                pcibase_update_irq(s);
            }
        } else {
            uint16_t req_lun;
            pci_dma_read(pdev, addr + 2, &req_lun, 2);
            uint8_t request_id = le16_to_cpu(req_lun) & 0xFFF;
            s->response_val = request_id;
            s->has_response = true;
            pcibase_update_irq(s);
        }
    } else {
        /* Unknown command, just complete with request_id from offset 2 */
        uint8_t request_id;
        pci_dma_read(pdev, addr + 2, &request_id, 1);
        s->response_val = request_id;
        s->has_response = true;
        pcibase_update_irq(s);
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "3w-sas: read out-of-bounds at 0x%"HWADDR_PRIx"\n", addr);
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    if (size != 4 && size != 8) {
        qemu_log_mask(LOG_UNIMP, "3w-sas: unsupported read size %u at 0x%"HWADDR_PRIx"\n", size, addr);
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    switch (addr) {
    case TWL_STATUS:
        val = 0; /* No status flags set */
        break;
    case TWL_SCRPD3:
        if (s->soft_reset_countdown > 0) {
            val = 0;
            s->soft_reset_countdown--;
        } else {
            val = TWL_CONTROLLER_READY;
        }
        break;
    case TWL_HISTAT:
        val = 0;
        if (s->has_response)
            val |= TWL_HISTATUS_RESPONSE_INTERRUPT | TWL_HISTATUS_VALID_INTERRUPT;
        break;
    case TWL_HOBQPL:
        if (s->has_response) {
            val = (uint32_t)(s->response_val & 0xFFFFFFFF);
            if (size == 4)
                s->has_response = false;
        }
        break;
    case TWL_HOBQPH:
        if (s->has_response) {
            val = (uint32_t)(s->response_val >> 32);
        }
        break;
    case TWL_HOBDB:
        val = 0;
        break;
    case TWL_HOBDBC:
        val = 0;
        break;
    default:
        if (addr < sizeof(s->regs)) {
            val = s->regs[addr >> 2];
        } else {
            val = 0;
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "3w-sas: write out-of-bounds at 0x%"HWADDR_PRIx"\n", addr);
        return;
    }

    if (size != 4 && size != 8) {
        qemu_log_mask(LOG_UNIMP, "3w-sas: unsupported write size %u at 0x%"HWADDR_PRIx"\n", size, addr);
        return;
    }

    switch (addr) {
    case TWL_HIMASK:
        s->regs[addr >> 2] = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case TWL_HIBQPH:
        s->hibq_high = (uint32_t)val;
        break;
    case TWL_HIBQPL:
        {
            uint64_t cmd_addr = ((uint64_t)s->hibq_high << 32) | ((uint32_t)val & 0xFFFFFFFF);
            process_inbound_command(s, cmd_addr);
        }
        break;
    case TWL_HOBQPH:
        s->sense_buf_high = (uint32_t)val;
        break;
    case TWL_HOBQPL:
        {
            uint64_t sense_addr = ((uint64_t)s->sense_buf_high << 32) | ((uint32_t)val & 0xFFFFFFFF);
            if (s->sense_buf_count < TW_Q_LENGTH) {
                s->sense_buf_addrs[s->sense_buf_count++] = sense_addr;
            }
        }
        break;
    case TWL_HOBDB:
    case TWL_HOBDBC:
        /* Dummy */
        break;
    case TWL_STATUS:
        if ((uint32_t)val == TWL_ISSUE_SOFT_RESET) {
            s->soft_reset_countdown = 2; /* Simulate brief non-ready period */
        } else {
            s->regs[addr >> 2] = (uint32_t)val;
        }
        break;
    default:
        if (addr < sizeof(s->regs)) {
            s->regs[addr >> 2] = (uint32_t)val;
        }
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

    /* Reset registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[TWL_STATUS >> 2] = TWL_CONTROLLER_READY;
    s->status = 0;
    s->soft_reset_pending = false;
    s->soft_reset_countdown = 0;
    s->pm_state = 0;
    s->hibq_high = 0;
    s->sense_buf_high = 0;
    s->has_response = false;
    s->response_val = 0;
    s->sense_buf_count = 0;
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
        /* Not used */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_3WARE);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_3WARE_9750);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID);
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
    s->bar_info[0] = (BARInfo) {
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "3w-sas-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialisation */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;
    s->has_msix = false;

    /* DMA configuration */
    /* No specific DMA mask needed */

    /* Timers */

    /* Final initialisation */
    pcibase_reset(DEVICE(pdev));
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

    /* No additional uninit needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "3w_sas_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(regs, PCIBaseState, BAR0_SIZE / sizeof(uint32_t)),
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
