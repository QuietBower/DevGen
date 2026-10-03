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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "gpio_thunderx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_CAVIUM
#define PCI_VENDOR_ID_CAVIUM 0x177d
#endif

#define GPIO_RX_DAT	0x0
#define GPIO_TX_SET	0x8
#define GPIO_TX_CLR	0x10
#define GPIO_CONST	0x90
#define GPIO_CONST_GPIOS_MASK 0xff
#define GPIO_BIT_CFG	0x400
#define GPIO_BIT_CFG_TX_OE		BIT(0)
#define GPIO_BIT_CFG_PIN_XOR		BIT(1)
#define GPIO_BIT_CFG_INT_EN		BIT(2)
#define GPIO_BIT_CFG_INT_TYPE		BIT(3)
#define GPIO_BIT_CFG_FIL_MASK		GENMASK(11, 4)
#define GPIO_BIT_CFG_FIL_CNT_SHIFT	4
#define GPIO_BIT_CFG_FIL_SEL_SHIFT	8
#define GPIO_BIT_CFG_TX_OD		BIT(12)
#define GPIO_BIT_CFG_PIN_SEL_MASK	GENMASK(25, 16)
#define GPIO_INTR	0x800
#define GPIO_INTR_INTR			BIT(0)
#define GPIO_INTR_INTR_W1S		BIT(1)
#define GPIO_INTR_ENA_W1C		BIT(2)
#define GPIO_INTR_ENA_W1S		BIT(3)
#define GPIO_2ND_BANK	0x1400
#define GLITCH_FILTER_400NS ((4u << GPIO_BIT_CFG_FIL_SEL_SHIFT) | \
			     (9u << GPIO_BIT_CFG_FIL_CNT_SHIFT))

static inline unsigned int bit_cfg_reg(unsigned int line)
{
	return 8 * line + GPIO_BIT_CFG;
}

static inline unsigned int intr_reg(unsigned int line)
{
	return 8 * line + GPIO_INTR;
}

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
    uint64_t rx_dat;
    uint64_t tx_set;
    uint64_t tx_clr;
    uint64_t gpio_const;
    uint64_t bit_cfg[100];
    uint64_t intr[100];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!msix_enabled(pdev)) {
        return;
    }

    for (int i = 0; i < 50; i++) {
        if ((s->intr[i] & GPIO_INTR_INTR) && (s->intr[i] & GPIO_INTR_ENA_W1S)) {
            msix_notify(pdev, 48 + 2 * i);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == GPIO_RX_DAT) {
        val = s->rx_dat;
    } else if (addr == GPIO_CONST) {
        val = s->gpio_const;
    } else if (addr >= GPIO_BIT_CFG && addr < GPIO_BIT_CFG + 8 * 100) {
        val = s->bit_cfg[(addr - GPIO_BIT_CFG) / 8];
    } else if (addr >= GPIO_INTR && addr < GPIO_INTR + 8 * 100) {
        val = s->intr[(addr - GPIO_INTR) / 8];
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == GPIO_TX_SET) {
        s->rx_dat |= val;
    } else if (addr == GPIO_TX_CLR) {
        s->rx_dat &= ~val;
    } else if (addr >= GPIO_BIT_CFG && addr < GPIO_BIT_CFG + 8 * 100) {
        s->bit_cfg[(addr - GPIO_BIT_CFG) / 8] = val;
    } else if (addr >= GPIO_INTR && addr < GPIO_INTR + 8 * 100) {
        int line = (addr - GPIO_INTR) / 8;
        
        if (val & GPIO_INTR_INTR) {
            s->intr[line] &= ~GPIO_INTR_INTR; /* W1C */
        }
        if (val & GPIO_INTR_INTR_W1S) {
            s->intr[line] |= GPIO_INTR_INTR;
        }
        if (val & GPIO_INTR_ENA_W1C) {
            s->intr[line] &= ~GPIO_INTR_ENA_W1S;
        }
        if (val & GPIO_INTR_ENA_W1S) {
            s->intr[line] |= GPIO_INTR_ENA_W1S;
        }
        
        pcibase_update_irq(s);
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

    s->rx_dat = 0;
    s->tx_set = 0;
    s->tx_clr = 0;
    s->gpio_const = 50 | (48 << 8); /* ngpio = 50, base_msi = 48 */
    memset(s->bit_cfg, 0, sizeof(s->bit_cfg));
    memset(s->intr, 0, sizeof(s->intr));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CAVIUM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0xA00A );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_CAVIUM);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0xA10A);
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
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "gpio-thunderx-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true; 
    if (s->has_msix) {
        msix_init(pdev, 256, &s->bar_regions[0], 0, 0x8000, &s->bar_regions[0], 0, 0x9000, 0, errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "gpio_thunderx_pci",
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
