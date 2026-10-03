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

#ifndef PCI_PM_SIZEOF
#define PCI_PM_SIZEOF 8
#endif

#define TYPE_PCIBASE_DEVICE "icp_multi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ICP_MULTI_ADC_CSR       0x00
#define ICP_MULTI_ADC_CSR_ST    BIT(0)
#define ICP_MULTI_ADC_CSR_BSY   BIT(0)
#define ICP_MULTI_ADC_CSR_BI    BIT(4)
#define ICP_MULTI_ADC_CSR_RA    BIT(5)
#define ICP_MULTI_ADC_CSR_DI    BIT(6)
#define ICP_MULTI_ADC_CSR_DI_CHAN(x) (((x) & 0x7) << 9)
#define ICP_MULTI_ADC_CSR_SE_CHAN(x) (((x) & 0xf) << 8)
#define ICP_MULTI_AI            2
#define ICP_MULTI_DAC_CSR       0x04
#define ICP_MULTI_DAC_CSR_ST    BIT(0)
#define ICP_MULTI_DAC_CSR_BSY   BIT(0)
#define ICP_MULTI_DAC_CSR_BI    BIT(4)
#define ICP_MULTI_DAC_CSR_RA    BIT(5)
#define ICP_MULTI_DAC_CSR_CHAN(x) (((x) & 0x3) << 8)
#define ICP_MULTI_AO            6
#define ICP_MULTI_DI            8
#define ICP_MULTI_DO            0x0A
#define ICP_MULTI_INT_EN        0x0c
#define ICP_MULTI_INT_STAT      0x0e
#define ICP_MULTI_INT_ADC_RDY   BIT(0)
#define ICP_MULTI_INT_DAC_RDY   BIT(1)
#define ICP_MULTI_INT_DOUT_ERR  BIT(2)
#define ICP_MULTI_INT_DIN_STAT  BIT(3)
#define ICP_MULTI_INT_CIE0      BIT(4)
#define ICP_MULTI_INT_CIE1      BIT(5)
#define ICP_MULTI_INT_CIE2      BIT(6)
#define ICP_MULTI_INT_CIE3      BIT(7)
#define ICP_MULTI_INT_MASK      0xff
#define ICP_MULTI_CNTR0         0x10
#define ICP_MULTI_CNTR1         0x12
#define ICP_MULTI_CNTR2         0x14
#define ICP_MULTI_CNTR3         0x16

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
    uint16_t intr_status;
    uint16_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint16_t adc_csr;
        uint16_t dac_csr;
        uint16_t ai_data;
        uint16_t ao_data;
        uint16_t di_data;
        uint16_t do_data;
        uint16_t int_enable;
        uint16_t int_status;
        uint16_t cntr[4];
    } regs;

};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ICP_MULTI_ADC_CSR:
        val = s->regs.adc_csr;
        break;
    case ICP_MULTI_AI:
        val = s->regs.ai_data;
        break;
    case ICP_MULTI_DAC_CSR:
        val = s->regs.dac_csr;
        break;
    case ICP_MULTI_AO:
        val = s->regs.ao_data;
        break;
    case ICP_MULTI_DI:
        val = 0x0000; /* Fixed input levels */
        break;
    case ICP_MULTI_DO:
        val = s->regs.do_data;
        break;
    case ICP_MULTI_INT_EN:
        val = s->regs.int_enable;
        break;
    case ICP_MULTI_INT_STAT:
        val = s->regs.int_status;
        break;
    case ICP_MULTI_CNTR0:
    case ICP_MULTI_CNTR1:
    case ICP_MULTI_CNTR2:
    case ICP_MULTI_CNTR3:
        val = s->regs.cntr[(addr - ICP_MULTI_CNTR0) / 2];
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

    switch (addr) {
    case ICP_MULTI_ADC_CSR:
        s->regs.adc_csr = val;
        if (val & ICP_MULTI_ADC_CSR_ST) {
            /* Start conversion: immediately complete */
            s->regs.adc_csr &= ~ICP_MULTI_ADC_CSR_BSY; /* Clear busy flag */
            s->regs.ai_data = 0x0abc; /* Fixed conversion result */
        }
        break;
    case ICP_MULTI_DAC_CSR:
        s->regs.dac_csr = val;
        if (val & ICP_MULTI_DAC_CSR_ST) {
            /* Start conversion: immediately complete */
            s->regs.dac_csr &= ~ICP_MULTI_DAC_CSR_BSY;
        }
        break;
    case ICP_MULTI_AI:
        /* Read-only register; ignore writes */
        break;
    case ICP_MULTI_AO:
        s->regs.ao_data = val;
        break;
    case ICP_MULTI_DO:
        s->regs.do_data = val;
        break;
    case ICP_MULTI_INT_EN:
        s->regs.int_enable = val;
        break;
    case ICP_MULTI_INT_STAT:
        /* Write-1-to-clear */
        s->regs.int_status &= ~val;
        break;
    case ICP_MULTI_CNTR0:
    case ICP_MULTI_CNTR1:
    case ICP_MULTI_CNTR2:
    case ICP_MULTI_CNTR3:
        s->regs.cntr[(addr - ICP_MULTI_CNTR0) / 2] = val;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Revert registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x104c );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8000 ); /* FIXME: missing device ID */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x000000 );
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
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "icp_multi-mmio";

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
