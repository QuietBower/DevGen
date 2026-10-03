/*
 * QEMU model for Philips SAA7146-based Audiowerk2 sound card
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

#define TYPE_PCIBASE_DEVICE "snd_aw2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_PHILIPS 0x1131
#define PCI_DEVICE_ID_PHILIPS_SAA7146 0x7146

#define MC1         0xFC
#define IER         0xDC
#define MRST_N      (1UL << 15)
#define WS2_CTRL    (1UL << 10)
#define AUDIO_MODE  (1UL << 29)
#define PCI_BT_A    0x4C
#define A2_CLKSRC   (1UL << 22)
#define A1_SWAP     (1UL << 21)
#define WS1_CTRL    (1UL << 14)
#define TSL1        0x180
#define A2_SWAP     (1UL << 20)
#define ACON2       0xF8
#define IIC_E       (1UL << 16)
#define WS4_CTRL    (1UL << 2)
#define EAP         (1UL << 9)
#define IIC_S       (1UL << 17)
#define WS3_CTRL    (1UL << 6)
#define TSL2        0x1C0
#define EI2C        (1UL << 8)
#define BCLK1_OEN   (1UL << 19)
#define ACON1       0xF4
#define TR_E_A2_OUT (1UL << 3)
#define TR_E_A1_OUT (1UL << 1)
#define TR_E_A1_IN  (1UL << 0)

/* Corrected register offsets from driver header */
#define ISR         0x00
#define IICSTA      0x90
#define PageA2_out  0xC0
#define BaseA2_out  0xB8
#define ProtA2_out  0xBC
#define PageA1_out  0xA8
#define BaseA1_out  0xA0
#define ProtA1_out  0xA4
#define PageA1_in   0x9C
#define BaseA1_in   0x94
#define ProtA1_in   0x98

#define PCI_ADP1    0x12C
#define PCI_ADP3    0x134
#define PCI_ADP2    0x130
#define GPIO_CTRL   0xE0

/* BAR0 size: SAA7146 register space typically 512 bytes */
#define BAR0_SIZE   0x200

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
    struct {
        uint32_t mc1;
        uint32_t ier;
        uint32_t acon1;
        uint32_t acon2;
        uint32_t gpio_ctrl;
        uint32_t pci_bt_a;
        uint32_t tsl1[8];
        uint32_t tsl2[8];
        uint32_t pci_adp1;
        uint32_t pci_adp2;
        uint32_t pci_adp3;
        uint32_t isr;
        uint32_t iicsta;
        uint32_t page_a2_out;
        uint32_t base_a2_out;
        uint32_t prot_a2_out;
        uint32_t page_a1_out;
        uint32_t base_a1_out;
        uint32_t prot_a1_out;
        uint32_t page_a1_in;
        uint32_t base_a1_in;
        uint32_t prot_a1_in;
    } regs;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* SAA7146 registers are 32-bit; reject non-32-bit accesses */
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read size %u at 0x" HWADDR_FMT_plx "\n", __func__, size, addr);
        return ~0ULL;
    }

    switch (addr) {
    case MC1:
        val = s->regs.mc1;
        break;
    case IER:
        val = s->regs.ier;
        break;
    case ACON1:
        val = s->regs.acon1;
        break;
    case ACON2:
        val = s->regs.acon2;
        break;
    case GPIO_CTRL:
        val = s->regs.gpio_ctrl;
        break;
    case PCI_BT_A:
        val = s->regs.pci_bt_a;
        break;
    case PCI_ADP1:
        val = s->regs.pci_adp1;
        break;
    case PCI_ADP2:
        val = s->regs.pci_adp2;
        break;
    case PCI_ADP3:
        val = s->regs.pci_adp3;
        break;
    case ISR:
        val = s->regs.isr;
        break;
    case IICSTA:
        val = s->regs.iicsta;
        break;
    case PageA2_out:
        val = s->regs.page_a2_out;
        break;
    case BaseA2_out:
        val = s->regs.base_a2_out;
        break;
    case ProtA2_out:
        val = s->regs.prot_a2_out;
        break;
    case PageA1_out:
        val = s->regs.page_a1_out;
        break;
    case BaseA1_out:
        val = s->regs.base_a1_out;
        break;
    case ProtA1_out:
        val = s->regs.prot_a1_out;
        break;
    case PageA1_in:
        val = s->regs.page_a1_in;
        break;
    case BaseA1_in:
        val = s->regs.base_a1_in;
        break;
    case ProtA1_in:
        val = s->regs.prot_a1_in;
        break;
    default:
        /* Handle TSL1 array */
        if (addr >= TSL1 && addr <= TSL1 + 0x1C) {
            int index = (addr - TSL1) >> 2;
            val = s->regs.tsl1[index];
        } else if (addr >= TSL2 && addr <= TSL2 + 0x1C) {
            int index = (addr - TSL2) >> 2;
            val = s->regs.tsl2[index];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown register read at 0x" HWADDR_FMT_plx "\n", __func__, addr);
            val = 0;
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write size %u at 0x" HWADDR_FMT_plx "\n", __func__, size, addr);
        return;
    }

    switch (addr) {
    case MC1:
        /* MC1 uses masked write: upper 16 bits select bits to modify */
        s->regs.mc1 = (s->regs.mc1 & ~((uint32_t)val >> 16)) | ((uint32_t)val & ((uint32_t)val >> 16));
        break;
    case IER:
        s->regs.ier = val;
        break;
    case ACON1:
        s->regs.acon1 = val;
        break;
    case ACON2:
        s->regs.acon2 = val;
        break;
    case GPIO_CTRL:
        s->regs.gpio_ctrl = val;
        break;
    case PCI_BT_A:
        s->regs.pci_bt_a = val;
        break;
    case PCI_ADP1:
        /* Read-only in hardware, but for simplicity store it */
        s->regs.pci_adp1 = val;
        break;
    case PCI_ADP2:
        s->regs.pci_adp2 = val;
        break;
    case PCI_ADP3:
        s->regs.pci_adp3 = val;
        break;
    case ISR:
        /* Write-1-to-clear */
        s->regs.isr &= ~val;
        break;
    case IICSTA:
        s->regs.iicsta = val;
        break;
    case PageA2_out:
        s->regs.page_a2_out = val;
        break;
    case BaseA2_out:
        s->regs.base_a2_out = val;
        break;
    case ProtA2_out:
        s->regs.prot_a2_out = val;
        break;
    case PageA1_out:
        s->regs.page_a1_out = val;
        break;
    case BaseA1_out:
        s->regs.base_a1_out = val;
        break;
    case ProtA1_out:
        s->regs.prot_a1_out = val;
        break;
    case PageA1_in:
        s->regs.page_a1_in = val;
        break;
    case BaseA1_in:
        s->regs.base_a1_in = val;
        break;
    case ProtA1_in:
        s->regs.prot_a1_in = val;
        break;
    default:
        /* Handle TSL1 array */
        if (addr >= TSL1 && addr <= TSL1 + 0x1C) {
            int index = (addr - TSL1) >> 2;
            s->regs.tsl1[index] = val;
        } else if (addr >= TSL2 && addr <= TSL2 + 0x1C) {
            int index = (addr - TSL2) >> 2;
            s->regs.tsl2[index] = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown register write at 0x" HWADDR_FMT_plx ", val 0x%" PRIx64 "\n", __func__, addr, val);
        }
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.mc1 = MRST_N;  /* chip out of reset */
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
        /* Not used in this model */
        return;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1131 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x7146 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0401 );
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
    s->bar_info[0].name = "aw2-mmio";
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
    .name = "snd_aw2_pci",
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
