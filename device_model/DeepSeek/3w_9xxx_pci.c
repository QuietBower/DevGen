/* 3w-9xxx QEMU PCI Device Model - Phase 2 Implementation */

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

#define TYPE_PCIBASE_DEVICE "3w_9xxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs extracted from driver */
#define VENDOR_ID 0x13C1
#define DEVICE_ID 0x1002
#define CLASS_ID  0x0000

/* Register offsets from BAR1 base */
#define REG_CONTROL           0x0
#define REG_STATUS            0x4
#define REG_COMMAND_QUEUE     0x8
#define REG_RESPONSE_QUEUE    0xC
#define REG_COMMAND_QUEUE_LARGE  0x20
#define REG_RESPONSE_QUEUE_LARGE 0x30

/* Control Register Bits */
#define TW_CONTROL_MASK_COMMAND_INTERRUPT       0x00020000
#define TW_CONTROL_CLEAR_HOST_INTERRUPT         0x00080000
#define TW_CONTROL_CLEAR_ATTENTION_INTERRUPT    0x00040000
#define TW_CONTROL_UNMASK_COMMAND_INTERRUPT     0x00008000
#define TW_CONTROL_DISABLE_INTERRUPTS           0x00000040
#define TW_CONTROL_ENABLE_INTERRUPTS            0x00000080
#define TW_CONTROL_UNMASK_RESPONSE_INTERRUPT    0x00004000
#define TW_CONTROL_ISSUE_SOFT_RESET             0x00000100
#define TW_CONTROL_CLEAR_ERROR_STATUS           0x00000200
#define TW_CONTROL_MASK_RESPONSE_INTERRUPT      0x00010000

/* Status Register Bits */
#define TW_STATUS_RESPONSE_INTERRUPT       0x00010000
#define TW_STATUS_HOST_INTERRUPT           0x00080000
#define TW_STATUS_COMMAND_INTERRUPT        0x00020000
#define TW_STATUS_ATTENTION_INTERRUPT      0x00040000
#define TW_STATUS_VALID_INTERRUPT          0x00DF0000
#define TW_STATUS_EXPECTED_BITS            0x00002000
#define TW_STATUS_UNEXPECTED_BITS          0x00F00000
#define TW_STATUS_RESPONSE_QUEUE_EMPTY     0x00004000
#define TW_STATUS_COMMAND_QUEUE_FULL       0x00008000
#define TW_STATUS_MICROCONTROLLER_READY    0x00002000

/* Opcodes guessed from driver context */
#define TW_OP_INIT_CONNECTION  0x4
#define TW_OP_GET_PARAM        0x2
#define TW_OP_SET_PARAM        0x3
#define TW_OP_EXECUTE_SCSI     0x10

/* Init connection result bit */
#define TW_CTLR_FW_COMPATIBLE  0x1

/* Command area offset added to physical address before writing to CMD queue */
#define TW_COMMAND_OFFSET      128

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware register shadows */
    uint32_t control_reg;
    uint32_t status_reg;
    uint32_t response_queue_value;
    bool response_pending;
    bool irq_enabled;
};

/* Packed structures for DMA interaction (guessed layout) */
typedef struct __attribute__((packed)) {
    uint8_t opcode;
    uint8_t request_id;
    uint16_t message_credits;
    uint32_t features;
    uint16_t size;
    uint16_t fw_srl;
    uint16_t fw_arch_id;
    uint16_t fw_branch;
    uint16_t fw_build;
    uint32_t result;
} TW_InitConnect_Command;

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending_ints = s->status_reg & TW_STATUS_VALID_INTERRUPT;
    if (s->irq_enabled && pending_ints) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_process_command(PCIBaseState *s, hwaddr command_phys)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t opcode = 0;
    uint64_t sgl_addr = 0;
    uint32_t request_id = 0;
    bool need_response = true;

    /* Read opcode byte at start of command area */
    pci_dma_read(pdev, command_phys, &opcode, 1);

    switch (opcode) {
    case TW_OP_INIT_CONNECTION:
        {
            TW_InitConnect_Command initconn;
            pci_dma_read(pdev, command_phys, &initconn, sizeof(initconn));
            request_id = initconn.request_id;
            /* Set fields to indicate compatible firmware */
            initconn.fw_srl = cpu_to_le16(0x1234);  /* dummy, driver only checks result */
            initconn.fw_arch_id = cpu_to_le16(0x5678);
            initconn.fw_branch = cpu_to_le16(0x9abc);
            initconn.fw_build = cpu_to_le16(0xdef0);
            initconn.result = cpu_to_le32(TW_CTLR_FW_COMPATIBLE);
            pci_dma_write(pdev, command_phys, &initconn, sizeof(initconn));
        }
        break;

    case TW_OP_GET_PARAM:
        {
            /* Read request_id from known offset? Use offset 1 for request_id */
            pci_dma_read(pdev, command_phys + 1, &request_id, 1);
            /* Read SGL address at offset 8 (byte8_offset.param.sgl[0].address) */
            pci_dma_read(pdev, command_phys + 8, &sgl_addr, sizeof(sgl_addr));
            /* Write dummy parameter data (zero-filled 16 bytes) to the generic buffer */
            uint8_t buf[16] = {0};
            pci_dma_write(pdev, sgl_addr, buf, sizeof(buf));
        }
        break;

    case TW_OP_EXECUTE_SCSI:
        {
            /* Set status to 0 at offset 0 of new command */
            uint8_t status = 0;
            pci_dma_write(pdev, command_phys, &status, 1);
            /* Read SGL address for generic buffer at offset 16 (newcommand.sg_list[0].address) */
            pci_dma_read(pdev, command_phys + 16, &sgl_addr, sizeof(sgl_addr));
            /* Clear error in response header at offset 4 of generic buffer */
            uint16_t zero_error = 0;
            pci_dma_write(pdev, sgl_addr + 4, &zero_error, sizeof(zero_error));
            /* Read request_id for response queue */
            pci_dma_read(pdev, command_phys + 1, &request_id, 1);
        }
        break;

    default:
        /* Unknown opcode, do nothing */
        need_response = false;
        break;
    }

    if (need_response) {
        /* Queue response: low 16 bits = request_id */
        s->response_queue_value = request_id & 0xFFFF;
        s->response_pending = true;
        /* Update status: response queue not empty, response interrupt */
        s->status_reg &= ~TW_STATUS_RESPONSE_QUEUE_EMPTY;
        s->status_reg |= TW_STATUS_RESPONSE_INTERRUPT;
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_CONTROL:
        val = s->control_reg;
        break;
    case REG_STATUS:
        /* Always ensure expected bits are set */
        s->status_reg |= TW_STATUS_MICROCONTROLLER_READY;
        /* Clear unexpected bits */
        s->status_reg &= ~TW_STATUS_UNEXPECTED_BITS;
        val = s->status_reg;
        break;
    case REG_COMMAND_QUEUE:
        /* Writing to this register triggers command processing, reading not expected */
        break;
    case REG_RESPONSE_QUEUE:
        if (s->response_pending) {
            val = s->response_queue_value;
            /* Consume the response */
            s->response_pending = false;
            s->status_reg |= TW_STATUS_RESPONSE_QUEUE_EMPTY;
            s->status_reg &= ~TW_STATUS_RESPONSE_INTERRUPT;
            pcibase_update_irq(s);
        } else {
            /* Return 0 if no response, but driver will likely poll correctly */
            val = 0;
        }
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = val;

    switch (addr) {
    case REG_CONTROL:
        /* Update interrupt enable */
        if (val32 & TW_CONTROL_ENABLE_INTERRUPTS) {
            s->irq_enabled = true;
        }
        if (val32 & TW_CONTROL_DISABLE_INTERRUPTS) {
            s->irq_enabled = false;
        }
        /* Clear interrupts */
        if (val32 & TW_CONTROL_CLEAR_HOST_INTERRUPT) {
            s->status_reg &= ~TW_STATUS_HOST_INTERRUPT;
        }
        if (val32 & TW_CONTROL_CLEAR_ATTENTION_INTERRUPT) {
            s->status_reg &= ~TW_STATUS_ATTENTION_INTERRUPT;
        }
        /* Soft reset: just set microcontroller ready and clear status */
        if (val32 & TW_CONTROL_ISSUE_SOFT_RESET) {
            s->status_reg = TW_STATUS_MICROCONTROLLER_READY | TW_STATUS_RESPONSE_QUEUE_EMPTY;
            s->response_pending = false;
            s->irq_enabled = false;
        }
        s->control_reg = val32;
        pcibase_update_irq(s);
        break;
    case REG_COMMAND_QUEUE:
        /* Process command immediately */
        pcibase_process_command(s, val);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0; /* No PIO used */
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO used */
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
    s->status_reg = TW_STATUS_MICROCONTROLLER_READY | TW_STATUS_RESPONSE_QUEUE_EMPTY;
    s->response_pending = false;
    s->irq_enabled = false;
    s->control_reg = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->has_msi = true;
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        /* MSI init failed, will use legacy interrupts */
        s->has_msi = false;
    }

    /* BAR1 MMIO */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = 0x100,
        .name = "3w-9xxx-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initial status */
    s->status_reg = TW_STATUS_MICROCONTROLLER_READY | TW_STATUS_RESPONSE_QUEUE_EMPTY;
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "3w_9xxx_pci",
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
