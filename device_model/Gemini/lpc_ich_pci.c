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

enum lpc_gpio_versions {
    ICH_I3100_GPIO,
    ICH_V5_GPIO,
    ICH_V6_GPIO,
    ICH_V7_GPIO,
    ICH_V9_GPIO,
    ICH_V10CORP_GPIO,
    ICH_V10CONS_GPIO,
    AVOTON_GPIO,
};

enum intel_spi_type {
    INTEL_SPI_BYT = 1,
    INTEL_SPI_LPT,
    INTEL_SPI_BXT,
    INTEL_SPI_CNL,
};

#define TYPE_PCIBASE_DEVICE "lpc_ich_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define DEVICE_ID_LPC_BAYTRAIL 0x0f1c

#define ACPIBASE		0x40
#define ACPIBASE_GPE_OFF	0x28
#define ACPIBASE_GPE_END	0x2f
#define ACPIBASE_SMI_OFF	0x30
#define ACPIBASE_SMI_END	0x33
#define ACPIBASE_PMC_OFF	0x08
#define ACPIBASE_PMC_END	0x0c
#define ACPIBASE_TCO_OFF	0x60
#define ACPIBASE_TCO_END	0x7f
#define ACPICTRL_PMCBASE	0x44
#define ACPIBASE_GCS_OFF	0x3410
#define ACPIBASE_GCS_END	0x3414
#define SPIBASE_BYT		0x54
#define SPIBASE_BYT_SZ		512
#define SPIBASE_BYT_EN		(1 << 1)
#define BYT_BCR			0xfc
#define BYT_BCR_WPD		(1 << 0)
#define SPIBASE_LPT		0x3800
#define SPIBASE_LPT_SZ		512
#define BCR			0xdc
#define BCR_WPD			(1 << 0)
#define GPIOBASE_ICH0		0x58
#define GPIOCTRL_ICH0		0x5C
#define GPIOBASE_ICH6		0x48
#define GPIOCTRL_ICH6		0x4C
#define RCBABASE		0xf0

#define ICH_RES_MEM_OFF		2

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
    uint32_t acpibase;
    uint32_t acpictrl_pmcbase;
    uint32_t gpiobase;
    uint32_t gpioctrl;
    uint32_t rcbabase;
    uint32_t spibase;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    uint64_t val = 0;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
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
    (void)s;
    pci_device_reset(PCI_DEVICE(dev));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID_LPC_BAYTRAIL );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_ISA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize LPC/ICH specific config space registers to satisfy driver probe */
    
    /* ACPI Base (0x40) - Needs to be non-zero and aligned to 0x80 */
    pci_set_long(pci_conf + ACPIBASE, 0x00001000);
    pci_set_long(pdev->wmask + ACPIBASE, 0xFFFFFFFF);

    /* PMC Base (0x44) - Needs to be non-zero and aligned to 0x200 */
    pci_set_long(pci_conf + ACPICTRL_PMCBASE, 0x00002000);
    pci_set_long(pdev->wmask + ACPICTRL_PMCBASE, 0xFFFFFFFF);

    /* GPIO Base ICH6 (0x48) - Needs to be non-zero and aligned to 0x80 */
    pci_set_long(pci_conf + GPIOBASE_ICH6, 0x00003000);
    pci_set_long(pdev->wmask + GPIOBASE_ICH6, 0xFFFFFFFF);

    /* GPIO Control ICH6 (0x4C) */
    pci_set_long(pci_conf + GPIOCTRL_ICH6, 0x00000000);
    pci_set_long(pdev->wmask + GPIOCTRL_ICH6, 0xFFFFFFFF);

    /* GPIO Base ICH0 (0x58) */
    pci_set_long(pci_conf + GPIOBASE_ICH0, 0x00003000);
    pci_set_long(pdev->wmask + GPIOBASE_ICH0, 0xFFFFFFFF);

    /* GPIO Control ICH0 (0x5C) */
    pci_set_long(pci_conf + GPIOCTRL_ICH0, 0x00000000);
    pci_set_long(pdev->wmask + GPIOCTRL_ICH0, 0xFFFFFFFF);

    /* SPI Base (0x54) - Needs SPIBASE_BYT_EN (0x2) set */
    pci_set_long(pci_conf + SPIBASE_BYT, 0x00004000 | SPIBASE_BYT_EN);
    pci_set_long(pdev->wmask + SPIBASE_BYT, 0xFFFFFFFF);

    /* RCBA Base (0xF0) - Needs bit 0 set */
    pci_set_long(pci_conf + RCBABASE, 0x00005000 | 1);
    pci_set_long(pdev->wmask + RCBABASE, 0xFFFFFFFF);

    /* BCR (0xDC) */
    pci_set_long(pci_conf + BCR, 0x00000000);
    pci_set_long(pdev->wmask + BCR, 0xFFFFFFFF);

    /* BAR Initialization */
    s->num_bars = 0;  
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
    .name = "lpc_ich_pci",
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
