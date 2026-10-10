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

#define TYPE_PCIBASE_DEVICE "snd_cs5535audio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NS		0x100b
#define PCI_DEVICE_ID_NS_CS5535_AUDIO	0x002e

#define ACC_GPIO_STATUS         0x00
#define ACC_CODEC_STATUS        0x08
#define ACC_CODEC_CNTL          0x0C
#define ACC_IRQ_STATUS          0x12
#define ACC_BM0_CMD             0x20
#define ACC_BM0_STATUS          0x21
#define ACC_BM0_PRD             0x24
#define ACC_BM1_CMD             0x28
#define ACC_BM1_STATUS          0x29
#define ACC_BM1_PRD             0x2C
#define ACC_BM0_PNTR            0x60
#define ACC_BM1_PNTR            0x64

#define CMD_NEW                 0x00010000
#define STS_NEW                 0x00020000
#define ACC_CODEC_CNTL_RD_CMD   0x80000000
#define ACC_CODEC_CNTL_WR_CMD   (~0x80000000)
#define CMD_MASK                0xFF00FFFF
#define EOP                     (1<<0)
#define BM1_IRQ_STS             3
#define IRQ_STS                 0
#define BM0_IRQ_STS             2
#define WU_IRQ_STS              1
#define BM_CTL_EN               0x01
#define BM_CTL_PAUSE            0x03

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
    uint32_t acc_gpio_status;
    uint32_t acc_codec_status;
    uint32_t acc_codec_cntl;
    uint32_t acc_irq_status;
    uint8_t acc_bm0_cmd;
    uint8_t acc_bm0_status;
    uint32_t acc_bm0_prd;
    uint32_t acc_bm0_pntr;
    uint8_t acc_bm1_cmd;
    uint8_t acc_bm1_status;
    uint32_t acc_bm1_prd;
    uint32_t acc_bm1_pntr;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    pci_set_irq(pdev, s->acc_irq_status != 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA logic not yet implemented */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ACC_GPIO_STATUS:
        val = s->acc_gpio_status;
        break;
    case ACC_CODEC_STATUS:
        val = s->acc_codec_status;
        break;
    case ACC_CODEC_CNTL:
        val = s->acc_codec_cntl;
        break;
    case ACC_IRQ_STATUS:
        val = s->acc_irq_status;
        break;
    case ACC_BM0_STATUS:
        val = s->acc_bm0_status;
        break;
    case ACC_BM1_STATUS:
        val = s->acc_bm1_status;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
    case ACC_CODEC_CNTL:
        s->acc_codec_cntl = val;
        if (val & CMD_NEW) {
            s->acc_codec_cntl &= ~CMD_NEW;
            /* Mask out the read command bit (0x80) to get the true register index */
            uint32_t reg = (val >> 24) & 0x7F;
            if (val & ACC_CODEC_CNTL_RD_CMD) {
                uint16_t data = 0;
                switch (reg) {
                case 0x00: data = 0x0900; break; /* AC97 capabilities */
                case 0x7c: data = 0x4e53; break; /* Vendor ID1 */
                case 0x7e: data = 0x4352; break; /* Vendor ID2 */
                default:   data = 0x0000; break;
                }
                s->acc_codec_status = (reg << 24) | STS_NEW | data;
            } else {
                s->acc_codec_status = (reg << 24) | STS_NEW;
            }
        }
        break;
    case ACC_BM0_STATUS:
        s->acc_bm0_status = val;
        break;
    case ACC_BM1_STATUS:
        s->acc_bm1_status = val;
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

    s->acc_gpio_status = 0;
    s->acc_codec_status = 0;
    s->acc_codec_cntl = 0;
    s->acc_irq_status = 0;
    s->acc_bm0_cmd = 0;
    s->acc_bm0_status = 0;
    s->acc_bm0_prd = 0;
    s->acc_bm0_pntr = 0;
    s->acc_bm1_cmd = 0;
    s->acc_bm1_status = 0;
    s->acc_bm1_prd = 0;
    s->acc_bm1_pntr = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NS_CS5535_AUDIO );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
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
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100; /* Placeholder size, need actual size */
    s->bar_info[0].name = "cs5535audio-pio";
      
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
    .name = "snd_cs5535audio_pci",
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
