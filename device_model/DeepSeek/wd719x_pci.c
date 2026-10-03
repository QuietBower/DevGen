/*
 * QEMU WD719x PCI SCSI Controller Emulation
 * Generated for QEMU 8.2.10 based on Linux driver wd719x.c
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

#define TYPE_PCIBASE_DEVICE "wd719x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_WD 0x110c
#define VENDOR_ID PCI_VENDOR_ID_WD
#define DEVICE_ID 0x3296
#define CLASS_ID 0x0100

/* Driver defines */
#define WD719X_CMD_READY                  0x00
#define WD719X_WAIT_FOR_CMD_READY       500
#define WD719X_AMR_COMMAND                0x00
#define WD719X_SUE_RESET                  0x11
#define WD719X_AMR_INT_STATUS             0x1f
#define WD719X_AMR_SCB_ERROR              0x1e
#define WD719X_SUE_TERM                   0x04
#define WD719X_INT_NOERRORS               0x01
#define WD719X_AMR_CMD_PARAM              0x01
#define WD719X_CMD_ABORT                  0x11
#define WD719X_CMD_READ_FIRMVER           0x04
#define WD719X_AMR_CMD_PARAM_3            0x03
#define WD719X_CMD_RESET                  0x12
#define WD719X_INT_NONE                   0x00
#define WD719X_AMR_SCB_IN                 0x04
#define WD719X_DISABLE_INT                0x80
#define WD719X_AMR_CMD_PARAM_2            0x02
#define WD719X_CMD_ABORT_TAG              0x10
#define WD719X_CMD_BUSRESET               0x03
#define WD719X_PCI_MODE_SELECT            0x3F
#define WD719X_HASH_TABLE_SIZE            4096
#define WD719X_WAIT_FOR_RISC             2000
#define WD719X_CMD_SLEEP                  0x0a
#define WD719X_CMD_PROCESS_SCB            0x80
#define WD719X_AMR_BIOS_SHARE_INT         0x0f
#define WD719X_START_CHANNEL2_3DMA        0x17
#define WD719X_START_CHANNEL2_3ABORT      0x20
#define WD719X_AMR_SCB_OUT                0x18
#define WD719X_PRAM_BASE_ADDR             0x00
#define WD719X_PCI_EXTERNAL_ADDR          0x60
#define WD719X_START_CHANNEL2_3DONE       0x01
#define WD719X_CMD_SET_PARAM              0x09
#define WD719X_CMD_INIT_RISC              0x01
#define WD719X_PCI_PORT_RESET             0x3E
#define WD719X_WAIT_FOR_SCSI_RESET    3000000
#define WD719X_PCI_CHANNEL2_3CMD          0x68
#define WD719X_CMD_INIT_SCAM              0x13
#define WD719X_PCI_INTERNAL_ADDR          0x64
#define WD719X_PCI_DMA_TRANSFER_SIZE      0x66
#define WD719X_ENABLE_ADVANCE_MODE        0x01
#define WD719X_PCI_CHANNEL2_3STATUS       0x69
#define WD719X_PCI_RESET                  0x01
#define WD719X_SUE_WRONGTAGS              0x19
#define WD719X_SUE_TIMEOUT                0x10
#define WD719X_SUE_IGNORED                0x18
#define WD719X_SUE_SCBQFULL               0x02
#define WD719X_SUE_CHAN23ABORT            0x08
#define WD719X_SUE_ARSDONE                0x17
#define WD719X_SUE_BUSFREE                0x16
#define WD719X_SUE_NOSCAMID               0x1b
#define WD719X_SUE_TOOLONG                0x15
#define WD719X_SUE_CHAN1ABORT             0x06
#define WD719X_SUE_BUSERROR               0x12
#define WD719X_SUE_REJECTED               0x01
#define WD719X_SUE_CHAN23PAR              0x07
#define WD719X_SUE_BADPHASE               0x14
#define WD719X_SUE_CHAN1PAR               0x05
#define WD719X_SUE_NOERRORS               0x00
#define WD719X_SUE_WRONGWAY               0x13
#define WD719X_SUE_BADTAGS                0x1a
#define WD719X_AMR_OP_CODE                0x1c
#define WD719X_INT_SPIDERFAILED           0x05
#define WD719X_INT_ERRORSLOGGED           0x04
#define WD719X_INT_PIOREADY               0xf0
#define WD719X_INT_LINKNOERRORS           0x02
#define WD719X_INT_LINKNOSTATUS           0x03
#define WD719X_INT_BADINT                 0x80
#define WD719X_EE_DO                      (1 << 4)
#define WD719X_PCI_GPIO_DATA              0x3D
#define WD719X_EE_CLK                     (1 << 3)
#define WD719X_EE_CS                      (1 << 2)
#define WD719X_EE_DI                      (1 << 1)
#define WD719X_PCI_GPIO_CONTROL           0x3C
#define WD719X_GPIO_ID_BITS               0x0a
#define WD719X_EE_SCSI_ID_MASK            0xf

enum wd719x_card_type {
    WD719X_TYPE_UNKNOWN = 0,
    WD719X_TYPE_7193,
    WD719X_TYPE_7197,
    WD719X_TYPE_7296,
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
    uint8_t intr_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t mmio[256];

    /* DMA Context */
    uint32_t scb_in; /* Last written SCB address for processing */
};

/* Forward declarations */
static void pcibase_update_irq(PCIBaseState *s);
static void pcibase_reset(DeviceState *dev);
static void wd719x_handle_command(PCIBaseState *s, uint8_t opcode);
static void wd719x_dma_start(PCIBaseState *s);

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t int_status = s->mmio[WD719X_AMR_INT_STATUS];
    if (int_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x100) {
        return ~0ULL;
    }

    switch (size) {
    case 1:
        val = s->mmio[addr];
        break;
    case 2:
        val = lduw_le_p(&s->mmio[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->mmio[addr]);
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x100) {
        return;
    }

    /* Write to shadow registers first */
    switch (size) {
    case 1:
        s->mmio[addr] = val;
        break;
    case 2:
        stw_le_p(&s->mmio[addr], val);
        break;
    case 4:
        stl_le_p(&s->mmio[addr], val);
        break;
    default:
        break;
    }

    /* Handle side-effects */
    switch (addr) {
    case WD719X_AMR_COMMAND: /* 0x00 */
        wd719x_handle_command(s, val & 0xFF);
        /* The handler sets the command register back to READY */
        break;
    case WD719X_AMR_INT_STATUS: /* 0x1F */
        /* Driver writes INT_NONE (0x00) to clear */
        s->mmio[WD719X_AMR_INT_STATUS] = 0;
        pcibase_update_irq(s);
        break;
    case WD719X_PCI_PORT_RESET: /* 0x3E */
        if (val & WD719X_PCI_RESET) {
            pcibase_reset(DEVICE(s));
        }
        break;
    case WD719X_PCI_CHANNEL2_3CMD: /* 0x68 */
        if (val == WD719X_START_CHANNEL2_3DMA) {
            wd719x_dma_start(s);
        }
        break;
    case WD719X_PCI_CHANNEL2_3STATUS: /* 0x69 */
        /* Driver writes 0 to clear status; we keep it 0 */
        s->mmio[WD719X_PCI_CHANNEL2_3STATUS] = 0;
        break;
    case WD719X_AMR_SCB_IN: /* 0x04, 0x05, 0x06, 0x07 */
        /* If a 32-bit write, capture the SCB address */
        if (size == 4) {
            s->scb_in = ldl_le_p(&s->mmio[WD719X_AMR_SCB_IN]);
        }
        break;
    }
}

static void wd719x_handle_command(PCIBaseState *s, uint8_t opcode)
{
    /* Mark command register as ready */
    s->mmio[WD719X_AMR_COMMAND] = WD719X_CMD_READY;

    /* Set interrupt status to indicate completion */
    s->mmio[WD719X_AMR_INT_STATUS] = WD719X_INT_NOERRORS;

    /* Set AMR_OP_CODE register with appropriate status */
    {
        uint8_t int_code = WD719X_INT_NOERRORS;
        uint8_t sue = 0;
        uint8_t scsi_status = 0;
        uint32_t op_code_val;

        if (opcode == WD719X_CMD_PROCESS_SCB) {
            /* For SCB commands, we return REJECTED to fail quickly */
            sue = WD719X_SUE_REJECTED;
            /* Set SCB_OUT to the address the driver wrote to SCB_IN */
            stl_le_p(&s->mmio[WD719X_AMR_SCB_OUT], s->scb_in);
        } else if (opcode == WD719X_CMD_READ_FIRMVER) {
            /* Provide a dummy firmware version: minor=0x01, major=0x00 */
            stl_le_p(&s->mmio[WD719X_AMR_SCB_OUT], 0x00000001);
        } /* else default SUE=0 */

        op_code_val = (scsi_status << 24) | (int_code << 16) | (sue << 8) | opcode;
        stl_le_p(&s->mmio[WD719X_AMR_OP_CODE], op_code_val);
    }

    /* Raise IRQ */
    pcibase_update_irq(s);
}

static void wd719x_dma_start(PCIBaseState *s)
{
    /* The driver writes external addr, internal addr, size before starting */
    /* We don't need to actually perform the DMA. Just set status to DONE */
    s->mmio[WD719X_PCI_CHANNEL2_3STATUS] = WD719X_START_CHANNEL2_3DONE;
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

    /* Clear all register shadows */
    memset(s->mmio, 0, sizeof(s->mmio));
    /* Specific power-on defaults */
    s->mmio[WD719X_AMR_COMMAND] = WD719X_CMD_READY; /* Ready */
    s->mmio[WD719X_PCI_CHANNEL2_3STATUS] = 0; /* DMA not active */
    s->scb_in = 0;

    /* Lower IRQ */
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 256, .name = "wd719x-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize register state */
    pcibase_reset(DEVICE(s));
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

    /* Additional cleanup if needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "wd719x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(mmio, PCIBaseState, 256),
        VMSTATE_UINT32(scb_in, PCIBaseState),
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
