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

#define TYPE_PCIBASE_DEVICE "icp_multi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_ICP		0x104c

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ICP_MULTI_ADC_CSR	0x00
#define ICP_MULTI_ADC_CSR_ST	BIT(0)
#define ICP_MULTI_ADC_CSR_BSY	BIT(0)
#define ICP_MULTI_ADC_CSR_BI	BIT(4)
#define ICP_MULTI_ADC_CSR_RA	BIT(5)
#define ICP_MULTI_ADC_CSR_DI	BIT(6)
#define ICP_MULTI_ADC_CSR_DI_CHAN(x) (((x) & 0x7) << 9)
#define ICP_MULTI_ADC_CSR_SE_CHAN(x) (((x) & 0xf) << 8)
#define ICP_MULTI_AI		2
#define ICP_MULTI_DAC_CSR	0x04
#define ICP_MULTI_DAC_CSR_ST	BIT(0)
#define ICP_MULTI_DAC_CSR_BSY	BIT(0)
#define ICP_MULTI_DAC_CSR_BI	BIT(4)
#define ICP_MULTI_DAC_CSR_RA	BIT(5)
#define ICP_MULTI_DAC_CSR_CHAN(x) (((x) & 0x3) << 8)
#define ICP_MULTI_AO		6
#define ICP_MULTI_DI		8
#define ICP_MULTI_DO		0x0A
#define ICP_MULTI_INT_EN	0x0c
#define ICP_MULTI_INT_STAT	0x0e
#define ICP_MULTI_INT_ADC_RDY	BIT(0)
#define ICP_MULTI_INT_DAC_RDY	BIT(1)
#define ICP_MULTI_INT_DOUT_ERR	BIT(2)
#define ICP_MULTI_INT_DIN_STAT	BIT(3)
#define ICP_MULTI_INT_CIE0	BIT(4)
#define ICP_MULTI_INT_CIE1	BIT(5)
#define ICP_MULTI_INT_CIE2	BIT(6)
#define ICP_MULTI_INT_CIE3	BIT(7)
#define ICP_MULTI_INT_MASK	0xff
#define ICP_MULTI_CNTR0		0x10
#define ICP_MULTI_CNTR1		0x12
#define ICP_MULTI_CNTR2		0x14
#define ICP_MULTI_CNTR3		0x16

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
    uint16_t int_en;
    uint16_t int_stat;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t adc_csr;
    uint16_t ai;
    uint16_t dac_csr;
    uint16_t ao;
    uint16_t di;
    uint16_t do_reg;
    uint16_t cntr0;
    uint16_t cntr1;
    uint16_t cntr2;
    uint16_t cntr3;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->int_stat & s->int_en) != 0;
    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ICP_MULTI_ADC_CSR:
        /* Clear BSY bit so driver doesn't hang in comedi_timeout */
        val = s->adc_csr & ~ICP_MULTI_ADC_CSR_BSY;
        break;
    case ICP_MULTI_AI:
        val = s->ai;
        break;
    case ICP_MULTI_DAC_CSR:
        /* Clear BSY bit so driver doesn't hang in comedi_timeout */
        val = s->dac_csr & ~ICP_MULTI_DAC_CSR_BSY;
        break;
    case ICP_MULTI_AO:
        val = s->ao;
        break;
    case ICP_MULTI_DI:
        val = s->di;
        break;
    case ICP_MULTI_DO:
        val = s->do_reg;
        break;
    case ICP_MULTI_INT_EN:
        val = s->int_en;
        break;
    case ICP_MULTI_INT_STAT:
        val = s->int_stat;
        break;
    case ICP_MULTI_CNTR0:
        val = s->cntr0;
        break;
    case ICP_MULTI_CNTR1:
        val = s->cntr1;
        break;
    case ICP_MULTI_CNTR2:
        val = s->cntr2;
        break;
    case ICP_MULTI_CNTR3:
        val = s->cntr3;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ICP_MULTI_ADC_CSR:
        s->adc_csr = val;
        if (val & ICP_MULTI_ADC_CSR_ST) {
            /* Auto-clear start bit and simulate immediate completion */
            s->adc_csr &= ~ICP_MULTI_ADC_CSR_ST;
            s->ai = 0x0123 << 4; /* Dummy ADC data */
        }
        break;
    case ICP_MULTI_DAC_CSR:
        s->dac_csr = val;
        if (val & ICP_MULTI_DAC_CSR_ST) {
            /* Auto-clear start bit */
            s->dac_csr &= ~ICP_MULTI_DAC_CSR_ST;
        }
        break;
    case ICP_MULTI_AO:
        s->ao = val;
        break;
    case ICP_MULTI_DO:
        s->do_reg = val;
        break;
    case ICP_MULTI_INT_EN:
        s->int_en = val;
        pcibase_update_irq(s);
        break;
    case ICP_MULTI_INT_STAT:
        s->int_stat &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case ICP_MULTI_CNTR0:
        s->cntr0 = val;
        break;
    case ICP_MULTI_CNTR1:
        s->cntr1 = val;
        break;
    case ICP_MULTI_CNTR2:
        s->cntr2 = val;
        break;
    case ICP_MULTI_CNTR3:
        s->cntr3 = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by this driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by this driver */
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

    s->int_en = 0;
    s->int_stat = 0;
    s->adc_csr = 0;
    s->ai = 0;
    s->dac_csr = 0;
    s->ao = 0;
    s->di = 0;
    s->do_reg = 0;
    s->cntr0 = 0;
    s->cntr1 = 0;
    s->cntr2 = 0;
    s->cntr3 = 0;

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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ICP );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8000 ); /* Placeholder until pci_device_id is provided */
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
    s->num_bars = 3;
    s->bar_info[2] = (BARInfo){
        .index = 2,
        .type = BAR_TYPE_MMIO,
        .size = 0x1000, /* Defaulting to 4K, exact size not in current source */
        .name = "icp_multi-bar2"
    };

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
    .name = "icp_multi_pci",
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
