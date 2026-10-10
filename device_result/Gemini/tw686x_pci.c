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


#define TYPE_PCIBASE_DEVICE "tw686x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_TECHWELL
#define PCI_VENDOR_ID_TECHWELL 0x1797
#endif

#define TW686X_DMA_MODE_MEMCPY      0
#define TW686X_DMA_MODE_CONTIG      1
#define TW686X_DMA_MODE_SG          2
#define DMA_CMD                     0x02
#define DMA_CHANNEL_ENABLE          0x0a
#define DMA_CMD_ENABLE              (1 << 31)
#define VIDEO_FIFO_STATUS           0x03
#define INT_STATUS_DMA_TOUT         (1 << 17)
#define INT_STATUS                  0x00
#define PB_STATUS                   0x01
#define SYS_SOFT_RST                0x06
#define DMA_CHANNEL_TIMEOUT         0x0d
#define DMA_TIMER_INTERVAL          0x0c
#define DMA_CONFIG                  0x0b
#define PHASE_REF                   0x2e
#define VIDEO_SIZE                  0x4A
#define AUDIO_CONTROL1              0x2c
#define VIDEO_CONTROL1              0x2a
#define TW686X_FRAME_MODE           0x2
#define SYS_MODE_DMA_SHIFT          13
#define TW686X_SG_MODE              0x0
#define AUDIO_DMA_SIZE_MAX          4096
#define TW686X_VIDSTAT_HLOCK        (1 << 6)
#define TW686X_VIDSTAT_VDLOSS       (1 << 7)
#define TW686X_STD_PAL_CN           5
#define TW686X_STD_NTSC_M           0
#define TW686X_STD_NTSC_443         3
#define TW686X_STD_PAL              1
#define TW686X_STD_SECAM            2
#define TW686X_STD_PAL_M            4
#define TW686X_STD_PAL_60           6

#define TW686X_FIFO_ERROR(x)        ((x) & ~(0xff))

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
    uint32_t int_status;
    uint32_t int_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t dma_cmd;
    uint32_t dma_channel_enable;
    uint32_t video_fifo_status;
    uint32_t int_status_reg;
    uint32_t pb_status;
    uint32_t sys_soft_rst;
    uint32_t dma_channel_timeout;
    uint32_t dma_timer_interval;
    uint32_t dma_config;
    uint32_t phase_ref;
    uint32_t video_size;
    uint32_t audio_control1;
    uint32_t video_control1;

    /* DMA Context */
    uint32_t dma_mode;

    uint32_t pending_dma_en;
    uint32_t pending_dma_cmd;
};

struct tw686x_sg_desc {
    uint32_t flags_length;
    uint32_t phys;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->int_status_reg) {
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

    switch (addr) {
    case INT_STATUS:
        val = s->int_status_reg;
        s->int_status_reg = 0; /* cleared on read */
        pcibase_update_irq(s);
        break;
    case PB_STATUS:
        val = s->pb_status;
        break;
    case VIDEO_FIFO_STATUS:
        val = s->video_fifo_status;
        break;
    case DMA_CMD:
        val = s->dma_cmd;
        break;
    case DMA_CHANNEL_ENABLE:
        val = s->dma_channel_enable;
        break;
    case SYS_SOFT_RST:
        val = s->sys_soft_rst;
        break;
    case DMA_CHANNEL_TIMEOUT:
        val = s->dma_channel_timeout;
        break;
    case DMA_TIMER_INTERVAL:
        val = s->dma_timer_interval;
        break;
    case DMA_CONFIG:
        val = s->dma_config;
        break;
    case PHASE_REF:
        val = s->phase_ref;
        break;
    case VIDEO_SIZE:
        val = s->video_size;
        break;
    case AUDIO_CONTROL1:
        val = s->audio_control1;
        break;
    case VIDEO_CONTROL1:
        val = s->video_control1;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SYS_SOFT_RST:
        s->sys_soft_rst = val;
        break;
    case DMA_CMD:
        s->dma_cmd = val;
        break;
    case DMA_CHANNEL_ENABLE:
        s->dma_channel_enable = val;
        break;
    case DMA_CONFIG:
        s->dma_config = val;
        break;
    case DMA_CHANNEL_TIMEOUT:
        s->dma_channel_timeout = val;
        break;
    case DMA_TIMER_INTERVAL:
        s->dma_timer_interval = val;
        break;
    case PHASE_REF:
        s->phase_ref = val;
        break;
    case VIDEO_SIZE:
        s->video_size = val;
        break;
    case AUDIO_CONTROL1:
        s->audio_control1 = val;
        break;
    case VIDEO_CONTROL1:
        s->video_control1 = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
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

    s->int_status_reg = 0;
    s->pb_status = 0;
    s->video_fifo_status = 0;
    s->dma_cmd = 0;
    s->dma_channel_enable = 0;
    s->sys_soft_rst = 0;
    s->dma_channel_timeout = 0;
    s->dma_timer_interval = 0;
    s->dma_config = 0;
    s->phase_ref = 0;
    s->video_size = 0;
    s->audio_control1 = 0;
    s->video_control1 = 0;
    
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TECHWELL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x6864 );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "tw686x_mmio";
    
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "tw686x_pci",
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
