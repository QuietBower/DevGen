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

#define TYPE_PCIBASE_DEVICE "xillybus_pcie_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define XILLYBUS_VENDOR_ID 0x10ee
#define XILLYBUS_DEVICE_ID 0xebeb
#define XILLYBUS_CLASS_ID 0x0000
#define FPGA_ENDIAN_REG      0x0040
#define FPGA_MSG_CTRL_REG    0x0008
#define FPGA_DMA_CONTROL_REG 0x0020
#define FPGA_MSG_BUF_ADDR_LOW  0x0C
#define FPGA_MSG_BUF_ADDR_HIGH 0x10
#define FPGA_BUF_CTRL_REG     0x0030
#define XILLYMSG_OPCODE_QUIESCEACK  2
#define XILLYMSG_OPCODE_IDT  1
#define XILLY_DEFAULT_IDT_LEN 128

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

    uint32_t endian_reg;
    uint32_t msg_ctrl_reg;
    uint32_t dma_control_reg;
    uint32_t msg_buf_addr_low;
    uint32_t msg_buf_addr_high;
    uint32_t buf_ctrl_reg;
    bool quiesceack_sent;
    uint32_t message_counter;
};

static void xillybus_send_quiesceack(PCIBaseState *s)
{
    uint64_t dma_addr = ((uint64_t)s->msg_buf_addr_high << 32) | s->msg_buf_addr_low;
    if (dma_addr == 0) {
        return;
    }
    uint32_t msg[2] = {
        [0] = (XILLYMSG_OPCODE_QUIESCEACK << 24),
        [1] = (s->message_counter << 28) | (4096 & 0xfffffff)
    };
    pci_dma_write(&s->parent_obj, dma_addr, msg, sizeof(msg));
    s->buf_ctrl_reg = 1;
    msi_notify(&s->parent_obj, 0);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case FPGA_ENDIAN_REG:
        val = s->endian_reg;
        break;
    case FPGA_MSG_CTRL_REG:
        val = s->msg_ctrl_reg;
        break;
    case FPGA_DMA_CONTROL_REG:
        val = s->dma_control_reg;
        break;
    case FPGA_MSG_BUF_ADDR_LOW:
        val = s->msg_buf_addr_low;
        break;
    case FPGA_MSG_BUF_ADDR_HIGH:
        val = s->msg_buf_addr_high;
        break;
    case FPGA_BUF_CTRL_REG:
        val = s->buf_ctrl_reg;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "xillybus: unknown MMIO read at addr 0x%"HWADDR_PRIx"\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case FPGA_ENDIAN_REG:
        s->endian_reg = val;
        break;
    case FPGA_MSG_CTRL_REG:
        s->msg_ctrl_reg = val;
        if (val == 0x04) {
            s->buf_ctrl_reg = 0;
            s->message_counter = 0;
        } else if (val == 0x03) {
            s->message_counter = (s->message_counter + 1) & 0xf;
        } else if (val == 0x01) {
            /* NACK: no action required */
        }
        break;
    case FPGA_DMA_CONTROL_REG:
        s->dma_control_reg = val;
        if (!s->quiesceack_sent && (s->msg_buf_addr_low || s->msg_buf_addr_high)) {
            xillybus_send_quiesceack(s);
            s->quiesceack_sent = true;
        }
        break;
    case FPGA_MSG_BUF_ADDR_LOW:
        s->msg_buf_addr_low = val;
        break;
    case FPGA_MSG_BUF_ADDR_HIGH:
        s->msg_buf_addr_high = val;
        break;
    case FPGA_BUF_CTRL_REG:
        s->buf_ctrl_reg = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "xillybus: unknown MMIO write at addr 0x%"HWADDR_PRIx" = 0x%"PRIx64"\n", addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    qemu_log_mask(LOG_GUEST_ERROR, "xillybus: unexpected PIO read\n");
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    qemu_log_mask(LOG_GUEST_ERROR, "xillybus: unexpected PIO write\n");
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
    s->endian_reg = 0;
    s->msg_ctrl_reg = 0;
    s->dma_control_reg = 0;
    s->msg_buf_addr_low = 0;
    s->msg_buf_addr_high = 0;
    s->buf_ctrl_reg = 0;
    s->quiesceack_sent = false;
    s->message_counter = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  XILLYBUS_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  XILLYBUS_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, XILLYBUS_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        error_setg(errp, "msi_init failed");
        return;
    }

    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 256, .name = "xillybus-mmio" };
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "xillybus_pcie_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(endian_reg, PCIBaseState),
        VMSTATE_UINT32(msg_ctrl_reg, PCIBaseState),
        VMSTATE_UINT32(dma_control_reg, PCIBaseState),
        VMSTATE_UINT32(msg_buf_addr_low, PCIBaseState),
        VMSTATE_UINT32(msg_buf_addr_high, PCIBaseState),
        VMSTATE_UINT32(buf_ctrl_reg, PCIBaseState),
        VMSTATE_BOOL(quiesceack_sent, PCIBaseState),
        VMSTATE_UINT32(message_counter, PCIBaseState),
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
