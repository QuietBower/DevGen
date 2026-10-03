#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "ixp4xx_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* IXP4XX PCI Controller Registers */
#define IXP4XX_PCI_NP_AD		0x00
#define IXP4XX_PCI_NP_CBE		0x04
#define IXP4XX_PCI_NP_WDATA		0x08
#define IXP4XX_PCI_NP_RDATA		0x0c
#define IXP4XX_PCI_CRP_AD_CBE		0x10
#define IXP4XX_PCI_CRP_WDATA		0x14
#define IXP4XX_PCI_CRP_RDATA		0x18
#define IXP4XX_PCI_CSR			0x1c
#define IXP4XX_PCI_ISR			0x20
#define IXP4XX_PCI_INTEN		0x24
#define IXP4XX_PCI_DMACTRL		0x28
#define IXP4XX_PCI_AHBMEMBASE		0x2c
#define IXP4XX_PCI_AHBIOBASE		0x30
#define IXP4XX_PCI_PCIMEMBASE		0x34
#define IXP4XX_PCI_AHBDOORBELL		0x38
#define IXP4XX_PCI_PCIDOORBELL		0x3c
#define IXP4XX_PCI_ATPDMA0_AHBADDR	0x40
#define IXP4XX_PCI_ATPDMA0_PCIADDR	0x44
#define IXP4XX_PCI_ATPDMA0_LENADDR	0x48
#define IXP4XX_PCI_ATPDMA1_AHBADDR	0x4c
#define IXP4XX_PCI_ATPDMA1_PCIADDR	0x50
#define IXP4XX_PCI_ATPDMA1_LENADDR	0x54
#define IXP4XX_PCI_CSR_HOST		BIT(0)
#define IXP4XX_PCI_CSR_ARBEN		BIT(1)
#define IXP4XX_PCI_CSR_ADS		BIT(2)
#define IXP4XX_PCI_CSR_PDS		BIT(3)
#define IXP4XX_PCI_CSR_ABE		BIT(4)
#define IXP4XX_PCI_CSR_DBT		BIT(5)
#define IXP4XX_PCI_CSR_ASE		BIT(8)
#define IXP4XX_PCI_CSR_IC		BIT(15)
#define IXP4XX_PCI_CSR_PRST		BIT(16)
#define IXP4XX_PCI_ISR_PSE		BIT(0)
#define IXP4XX_PCI_ISR_PFE		BIT(1)
#define IXP4XX_PCI_ISR_PPE		BIT(2)
#define IXP4XX_PCI_ISR_AHBE		BIT(3)
#define IXP4XX_PCI_ISR_APDC		BIT(4)
#define IXP4XX_PCI_ISR_PADC		BIT(5)
#define IXP4XX_PCI_ISR_ADB		BIT(6)
#define IXP4XX_PCI_ISR_PDB		BIT(7)
#define IXP4XX_PCI_INTEN_PSE		BIT(0)
#define IXP4XX_PCI_INTEN_PFE		BIT(1)
#define IXP4XX_PCI_INTEN_PPE		BIT(2)
#define IXP4XX_PCI_INTEN_AHBE		BIT(3)
#define IXP4XX_PCI_INTEN_APDC		BIT(4)
#define IXP4XX_PCI_INTEN_PADC		BIT(5)
#define IXP4XX_PCI_INTEN_ADB		BIT(6)
#define IXP4XX_PCI_INTEN_PDB		BIT(7)
#define IXP4XX_PCI_NP_CBE_BESL		4
#define NP_CMD_IOREAD			0x2
#define NP_CMD_IOWRITE			0x3
#define NP_CMD_CONFIGREAD		0xa
#define NP_CMD_CONFIGWRITE		0xb
#define NP_CMD_MEMREAD			0x6
#define NP_CMD_MEMWRITE			0x7
#define CRP_AD_CBE_BESL         20
#define CRP_AD_CBE_WRITE	0x00010000
#define IXP4XX_PCI_RTOTTO		0x40
#define PCI_CONF1_ENABLE	BIT(31)
#define PCI_CONF1_ADDRESS(bus, dev, func, reg) \
	(PCI_CONF1_ENABLE | \
	 PCI_CONF1_BUS(bus) | \
	 PCI_CONF1_DEV(dev) | \
	 PCI_CONF1_FUNC(func) | \
	 PCI_CONF1_REG(reg))
#define PCI_CONF1_FUNC(x)	(((x) & PCI_CONF1_FUNC_MASK) << PCI_CONF1_FUNC_SHIFT)
#define PCI_CONF1_DEV(x)	(((x) & PCI_CONF1_DEV_MASK) << PCI_CONF1_DEV_SHIFT)
#define PCI_CONF1_REG(x)	((x) & PCI_CONF1_REG_MASK)
#define PCI_CONF1_BUS(x)	(((x) & PCI_CONF1_BUS_MASK) << PCI_CONF1_BUS_SHIFT)
#define PCI_CONF1_FUNC_SHIFT	8
#define PCI_CONF1_FUNC_MASK	0x7
#define PCI_CONF1_DEV_MASK	0x1f
#define PCI_CONF1_DEV_SHIFT	11
#define PCI_CONF1_REG_MASK	0xfc
#define PCI_CONF1_BUS_MASK	0xff
#define PCI_CONF1_BUS_SHIFT	16

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
    uint32_t isr;
    uint32_t inten;

    struct IXP4xxPCIState {
        uint32_t np_ad;
        uint32_t np_cbe;
        uint32_t np_wdata;
        uint32_t np_rdata;
        uint32_t crp_ad_cbe;
        uint32_t crp_wdata;
        uint32_t crp_rdata;
        uint32_t csr;
        uint32_t isr;
        uint32_t inten;
        uint32_t dmactrl;
        uint32_t ahbmembase;
        uint32_t ahbiobase;
        uint32_t pcimembase;
        uint32_t ahbdoorbell;
        uint32_t pcidoorbell;
        struct {
            uint32_t ahb_addr;
            uint32_t pci_addr;
            uint32_t len_addr;
        } atpdma[2];
    } regs;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case IXP4XX_PCI_NP_AD:
        val = s->regs.np_ad;
        break;
    case IXP4XX_PCI_NP_CBE:
        val = s->regs.np_cbe;
        break;
    case IXP4XX_PCI_NP_WDATA:
        val = s->regs.np_wdata;
        break;
    case IXP4XX_PCI_NP_RDATA:
        /* Always return all-ones to indicate no device on bus */
        val = 0xffffffff;
        break;
    case IXP4XX_PCI_CRP_AD_CBE:
        val = s->regs.crp_ad_cbe;
        break;
    case IXP4XX_PCI_CRP_WDATA:
        val = s->regs.crp_wdata;
        break;
    case IXP4XX_PCI_CRP_RDATA:
        val = s->regs.crp_rdata;
        break;
    case IXP4XX_PCI_CSR:
        val = s->regs.csr;
        break;
    case IXP4XX_PCI_ISR:
        val = s->regs.isr;
        break;
    case IXP4XX_PCI_INTEN:
        val = s->regs.inten;
        break;
    case IXP4XX_PCI_DMACTRL:
        val = s->regs.dmactrl;
        break;
    case IXP4XX_PCI_AHBMEMBASE:
        val = s->regs.ahbmembase;
        break;
    case IXP4XX_PCI_AHBIOBASE:
        val = s->regs.ahbiobase;
        break;
    case IXP4XX_PCI_PCIMEMBASE:
        val = s->regs.pcimembase;
        break;
    case IXP4XX_PCI_AHBDOORBELL:
        val = s->regs.ahbdoorbell;
        break;
    case IXP4XX_PCI_PCIDOORBELL:
        val = s->regs.pcidoorbell;
        break;
    case IXP4XX_PCI_ATPDMA0_AHBADDR:
        val = s->regs.atpdma[0].ahb_addr;
        break;
    case IXP4XX_PCI_ATPDMA0_PCIADDR:
        val = s->regs.atpdma[0].pci_addr;
        break;
    case IXP4XX_PCI_ATPDMA0_LENADDR:
        val = s->regs.atpdma[0].len_addr;
        break;
    case IXP4XX_PCI_ATPDMA1_AHBADDR:
        val = s->regs.atpdma[1].ahb_addr;
        break;
    case IXP4XX_PCI_ATPDMA1_PCIADDR:
        val = s->regs.atpdma[1].pci_addr;
        break;
    case IXP4XX_PCI_ATPDMA1_LENADDR:
        val = s->regs.atpdma[1].len_addr;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    switch (addr) {
    case IXP4XX_PCI_NP_AD:
        s->regs.np_ad = val;
        break;
    case IXP4XX_PCI_NP_CBE:
        s->regs.np_cbe = val;
        break;
    case IXP4XX_PCI_NP_WDATA:
        s->regs.np_wdata = val;
        /* If the previous CBE indicated a config write, do nothing */
        break;
    case IXP4XX_PCI_CRP_AD_CBE:
        s->regs.crp_ad_cbe = val;
        break;
    case IXP4XX_PCI_CRP_WDATA:
    {
        uint32_t crp = s->regs.crp_ad_cbe;
        if (crp & CRP_AD_CBE_WRITE) {
            uint32_t where = crp & ~3;
            int byte_en = (crp >> CRP_AD_CBE_BESL) & 0xf;
            int size = 0;
            if (byte_en == 0) {
                size = 4;
            } else {
                int ones = ctpop8(byte_en);
                if (ones == 3) size = 1;
                else if (ones == 2) size = 2;
            }
            if (size > 0) {
                pci_default_write_config(pdev, where & 0xff, val, size);
            }
        }
        s->regs.crp_wdata = val;
        break;
    }
    case IXP4XX_PCI_CSR:
        s->regs.csr = val;
        break;
    case IXP4XX_PCI_ISR:
        /* Write-1-to-clear */
        s->regs.isr &= ~val;
        break;
    case IXP4XX_PCI_INTEN:
        s->regs.inten = val;
        break;
    case IXP4XX_PCI_DMACTRL:
        s->regs.dmactrl = val;
        break;
    case IXP4XX_PCI_AHBMEMBASE:
        s->regs.ahbmembase = val;
        break;
    case IXP4XX_PCI_AHBIOBASE:
        s->regs.ahbiobase = val;
        break;
    case IXP4XX_PCI_PCIMEMBASE:
        s->regs.pcimembase = val;
        break;
    case IXP4XX_PCI_AHBDOORBELL:
        s->regs.ahbdoorbell = val;
        break;
    case IXP4XX_PCI_PCIDOORBELL:
        s->regs.pcidoorbell = val;
        break;
    case IXP4XX_PCI_ATPDMA0_AHBADDR:
        s->regs.atpdma[0].ahb_addr = val;
        break;
    case IXP4XX_PCI_ATPDMA0_PCIADDR:
        s->regs.atpdma[0].pci_addr = val;
        break;
    case IXP4XX_PCI_ATPDMA0_LENADDR:
        s->regs.atpdma[0].len_addr = val;
        break;
    case IXP4XX_PCI_ATPDMA1_AHBADDR:
        s->regs.atpdma[1].ahb_addr = val;
        break;
    case IXP4XX_PCI_ATPDMA1_PCIADDR:
        s->regs.atpdma[1].pci_addr = val;
        break;
    case IXP4XX_PCI_ATPDMA1_LENADDR:
        s->regs.atpdma[1].len_addr = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.csr = IXP4XX_PCI_CSR_HOST;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1057);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x01);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x1000,
        .name = "ixp4xx-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.csr = IXP4XX_PCI_CSR_HOST;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "ixp4xx_pci_pci",
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
