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

#define TYPE_PCIBASE_DEVICE "lynx_pci_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_TI_PCILYNX 0x8000
#define PCILYNX_MAX_REGISTER 0xfff

#define RCV_BUFFER_SIZE (16 * 1024)
#define PHY_PACKET_SIZE 12
#define TCODE_PHY_PACKET 0x10

#define MISC_CONTROL 0x40
#define MISC_CONTROL_SWRESET (1<<0)

#define PCI_INT_STATUS 0x48
#define PCI_INT_ENABLE 0x4c
#define PCI_INT_INT_PEND (1<<31)
#define PCI_INT_P1394_INT (1<<16)
#define PCI_INT_DMA0_HLT (1<<0)
#define PCI_INT_DMA_ALL 0x3ff

#define DMA0_CURRENT_PCL 0x104
#define DMA0_CHAN_CTRL 0x110
#define DMA_CHAN_CTRL_ENABLE (1<<31)
#define DMA_CHAN_CTRL_LINK (1<<29)

#define FIFO_SIZES 0xa00
#define DMA_GLOBAL_REGISTER 0x908

#define LINK_CONTROL 0xf04
#define LINK_CONTROL_SNOOP_ENABLE (1<<6)

#define LINK_PHY 0xf0c
#define LINK_PHY_WRITE (1<<30)
#define LINK_PHY_ADDR(addr)               ((addr)<<24)
#define LINK_PHY_WDATA(data)              ((data)<<16)

#define OHCI1394_PhyControl                   0x0EC
#define OHCI_LOOP_COUNT		100

#define LINK_INT_STATUS 0xf14
#define LINK_INT_ENABLE 0xf18
#define LINK_INT_PHY_BUSRESET (1<<28)
#define LINK_INT_PHY_REG_RCVD (1<<29)
#define LINK_INT_PHY_TIME_OUT (1<<30)
#define LINK_INT_TC_ERR (1<<15)
#define LINK_INT_IT_STUCK (1<<20)
#define LINK_INT_AT_STUCK (1<<19)
#define LINK_INT_SNTRJ (1<<17)
#define LINK_INT_GRF_OVER_FLOW (1<<5)
#define LINK_INT_ATF_UNDER_FLOW (1<<3)
#define LINK_INT_ITF_UNDER_FLOW (1<<4)

#define PCL_CMD_RCV (0x1<<24)
#define PCL_LAST_BUFF (1<<18)
#define PCL_BIGENDIAN (1<<16)
#define PCL_NEXT_INVALID (1<<0)

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
    uint32_t pci_int_status;
    uint32_t pci_int_enable;
    uint32_t link_int_status;
    uint32_t link_int_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t misc_control;
    uint32_t dma_global_register;
    uint32_t fifo_sizes;
    uint32_t link_control;
    uint32_t link_phy;

    /* DMA Context */
    uint32_t dma0_current_pcl;
    uint32_t dma0_chan_ctrl;
};

struct pcl {
    uint32_t next;
    uint32_t async_error_next;
    uint32_t user_data;
    uint32_t pcl_status;
    uint32_t remaining_transfer_count;
    uint32_t next_data_buffer;
    struct {
        uint32_t control;
        uint32_t pointer;
    } buffer[13];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;

    if (s->link_int_status & s->link_int_enable) {
        s->pci_int_status |= PCI_INT_P1394_INT;
    } else {
        s->pci_int_status &= ~PCI_INT_P1394_INT;
    }

    if (s->pci_int_status & s->pci_int_enable & ~PCI_INT_INT_PEND) {
        s->pci_int_status |= PCI_INT_INT_PEND;
        raise = true;
    } else {
        s->pci_int_status &= ~PCI_INT_INT_PEND;
    }

    pci_set_irq(pdev, raise ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MISC_CONTROL:
        val = s->misc_control;
        break;
    case PCI_INT_STATUS:
        val = s->pci_int_status;
        break;
    case PCI_INT_ENABLE:
        val = s->pci_int_enable;
        break;
    case DMA0_CURRENT_PCL:
        val = s->dma0_current_pcl;
        break;
    case DMA0_CHAN_CTRL:
        val = s->dma0_chan_ctrl;
        break;
    case FIFO_SIZES:
        val = s->fifo_sizes;
        break;
    case DMA_GLOBAL_REGISTER:
        val = s->dma_global_register;
        break;
    case LINK_CONTROL:
        val = s->link_control;
        break;
    case LINK_PHY:
        val = s->link_phy;
        break;
    case LINK_INT_STATUS:
        val = s->link_int_status;
        break;
    case LINK_INT_ENABLE:
        val = s->link_int_enable;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MISC_CONTROL:
        s->misc_control = val;
        if (val & MISC_CONTROL_SWRESET) {
            s->pci_int_status = 0;
            s->pci_int_enable = 0;
            s->link_int_status = 0;
            s->link_int_enable = 0;
            s->dma0_chan_ctrl = 0;
            s->dma_global_register = 0;
            s->fifo_sizes = 0;
            s->link_control = 0;
            s->link_phy = 0;
            pcibase_update_irq(s);
        }
        break;
    case PCI_INT_STATUS:
        s->pci_int_status &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case PCI_INT_ENABLE:
        s->pci_int_enable = val;
        pcibase_update_irq(s);
        break;
    case DMA0_CURRENT_PCL:
        s->dma0_current_pcl = val;
        break;
    case DMA0_CHAN_CTRL:
        s->dma0_chan_ctrl = val;
        break;
    case FIFO_SIZES:
        s->fifo_sizes = val;
        break;
    case DMA_GLOBAL_REGISTER:
        s->dma_global_register = val;
        break;
    case LINK_CONTROL:
        s->link_control = val;
        break;
    case LINK_PHY:
        s->link_phy = val;
        break;
    case LINK_INT_STATUS:
        s->link_int_status &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case LINK_INT_ENABLE:
        s->link_int_enable = val;
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by driver */
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

    s->pci_int_status = 0;
    s->pci_int_enable = 0;
    s->link_int_status = 0;
    s->link_int_enable = 0;
    s->misc_control = 0;
    s->dma_global_register = 0;
    s->fifo_sizes = 0;
    s->link_control = 0;
    s->link_phy = 0;
    s->dma0_current_pcl = 0;
    s->dma0_chan_ctrl = 0;
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_TI_PCILYNX );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_FIREWIRE );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "pcilynx-mmio";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "lynx_pci_driver_pci",
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
