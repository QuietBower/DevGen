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

#define BIT(x) (1 << (x))

#define TYPE_PCIBASE_DEVICE "lpc_ich_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x0f1c
#define CLASS_ID 0x0000

/* Register offsets from driver */
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
#define SPIBASE_BYT_EN		BIT(1)
#define BYT_BCR			0xfc
#define BYT_BCR_WPD		BIT(0)
#define SPIBASE_LPT		0x3800
#define SPIBASE_LPT_SZ		512
#define BCR			0xdc
#define BCR_WPD			BIT(0)
#define GPIOBASE_ICH0		0x58
#define GPIOCTRL_ICH0		0x5C
#define GPIOBASE_ICH6		0x48
#define GPIOCTRL_ICH6		0x4C
#define RCBABASE		0xf0
#define wdt_io_res(i) wdt_res(0, i)
#define wdt_mem_res(i) wdt_res(ICH_RES_MEM_OFF, i)
#define wdt_res(b, i) (&wdt_ich_res[(b) + (i)])
#define INTEL_GPIO_RESOURCE_SIZE	0x1000

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

enum lpc_chipsets {
    LPC_ICH = 0,	/* ICH */
    LPC_ICH0,	/* ICH0 */
    LPC_ICH2,	/* ICH2 */
    LPC_ICH2M,	/* ICH2-M */
    LPC_ICH3,	/* ICH3-S */
    LPC_ICH3M,	/* ICH3-M */
    LPC_ICH4,	/* ICH4 */
    LPC_ICH4M,	/* ICH4-M */
    LPC_CICH,	/* C-ICH */
    LPC_ICH5,	/* ICH5 & ICH5R */
    LPC_6300ESB,	/* 6300ESB */
    LPC_ICH6,	/* ICH6 & ICH6R */
    LPC_ICH6M,	/* ICH6-M */
    LPC_ICH6W,	/* ICH6W & ICH6RW */
    LPC_631XESB,	/* 631xESB/632xESB */
    LPC_ICH7,	/* ICH7 & ICH7R */
    LPC_ICH7DH,	/* ICH7DH */
    LPC_ICH7M,	/* ICH7-M & ICH7-U */
    LPC_ICH7MDH,	/* ICH7-M DH */
    LPC_NM10,	/* NM10 */
    LPC_ICH8,	/* ICH8 & ICH8R */
    LPC_ICH8DH,	/* ICH8DH */
    LPC_ICH8DO,	/* ICH8DO */
    LPC_ICH8M,	/* ICH8M */
    LPC_ICH8ME,	/* ICH8M-E */
    LPC_ICH9,	/* ICH9 */
    LPC_ICH9R,	/* ICH9R */
    LPC_ICH9DH,	/* ICH9DH */
    LPC_ICH9DO,	/* ICH9DO */
    LPC_ICH9M,	/* ICH9M */
    LPC_ICH9ME,	/* ICH9M-E */
    LPC_ICH10,	/* ICH10 */
    LPC_ICH10R,	/* ICH10R */
    LPC_ICH10D,	/* ICH10D */
    LPC_ICH10DO,	/* ICH10DO */
    LPC_PCH,	/* PCH Desktop Full Featured */
    LPC_PCHM,	/* PCH Mobile Full Featured */
    LPC_P55,	/* P55 */
    LPC_PM55,	/* PM55 */
    LPC_H55,	/* H55 */
    LPC_QM57,	/* QM57 */
    LPC_H57,	/* H57 */
    LPC_HM55,	/* HM55 */
    LPC_Q57,	/* Q57 */
    LPC_HM57,	/* HM57 */
    LPC_PCHMSFF,	/* PCH Mobile SFF Full Featured */
    LPC_QS57,	/* QS57 */
    LPC_3400,	/* 3400 */
    LPC_3420,	/* 3420 */
    LPC_3450,	/* 3450 */
    LPC_EP80579,	/* EP80579 */
    LPC_CPT,	/* Cougar Point */
    LPC_CPTD,	/* Cougar Point Desktop */
    LPC_CPTM,	/* Cougar Point Mobile */
    LPC_PBG,	/* Patsburg */
    LPC_DH89XXCC,	/* DH89xxCC */
    LPC_PPT,	/* Panther Point */
    LPC_LPT,	/* Lynx Point */
    LPC_LPT_LP,	/* Lynx Point-LP */
    LPC_WBG,	/* Wellsburg */
    LPC_AVN,	/* Avoton SoC */
    LPC_BAYTRAIL,   /* Bay Trail SoC */
    LPC_COLETO,	/* Coleto Creek */
    LPC_WPT_LP,	/* Wildcat Point-LP */
    LPC_BRASWELL,	/* Braswell SoC */
    LPC_LEWISBURG,	/* Lewisburg */
    LPC_9S,		/* 9 Series */
    LPC_APL,	/* Apollo Lake SoC */
    LPC_DNV,	/* Denverton SoC */
    LPC_GLK,	/* Gemini Lake SoC */
    LPC_COUGARMOUNTAIN,/* Cougar Mountain SoC */
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t irq_status;
    uint32_t irq_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t abase;            /* 0x40 */
    uint32_t actrl_pbase;      /* 0x44 */
    uint32_t gbase;            /* 0x48 */
    uint32_t gctrl;            /* 0x4C */
    uint32_t spibase_byt;      /* 0x54 */
    uint32_t rcba_base;        /* 0xF0 */

    /* DMA Context */

    enum lpc_chipsets chipset;
};

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

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

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

/* Custom PCI config space handlers for register shadows */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    switch (addr) {
    case 0x40: return s->abase;
    case 0x44: return s->actrl_pbase;
    case 0x48: return s->gbase;
    case 0x4C: return s->gctrl;
    case 0x54: return s->spibase_byt;
    case 0xF0: return s->rcba_base;
    default: return pci_default_read_config(pdev, addr, len);
    }
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t *reg = NULL;
    switch (addr) {
    case 0x40: reg = &s->abase; break;
    case 0x44: reg = &s->actrl_pbase; break;
    case 0x48: reg = &s->gbase; break;
    case 0x4C: reg = &s->gctrl; break;
    case 0x54: reg = &s->spibase_byt; break;
    case 0xF0: reg = &s->rcba_base; break;
    }
    if (reg) {
        uint32_t mask = 0;
        if (len == 1) mask = 0xff;
        else if (len == 2) mask = 0xffff;
        else if (len == 4) mask = 0xffffffff;
        *reg = (*reg & ~mask) | (val & mask);
    } else {
        pci_default_write_config(pdev, addr, val, len);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->abase = 0x00001000;            /* I/O base, enable bit initially cleared */
    s->actrl_pbase = 0xfed00000;      /* Memory base, enable bits cleared */
    s->gbase = 0x00002000;            /* I/O base for GPIO */
    s->gctrl = 0x00000000;            /* GPIO control, no enable */
    s->spibase_byt = 0x00010000 | 0x2; /* SPI base with enable bit set */
    s->rcba_base = 0x00000000;        /* RCBA disabled */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 0;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Override config space handlers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    s->chipset = LPC_BAYTRAIL; /* derived from first pci_device_id entry */
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
