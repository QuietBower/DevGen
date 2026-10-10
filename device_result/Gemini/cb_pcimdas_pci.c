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

#define TYPE_PCIBASE_DEVICE "cb_pcimdas_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_CB		0x1307

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIMDAS_AI_REG			0x00
#define PCIMDAS_AI_SOFTTRIG_REG		0x00
#define PCIMDAS_AO_REG(x)		(0x02 + ((x) * 2))
#define PCIMDAS_MUX_REG			0x00
#define PCIMDAS_MUX(_lo, _hi)		((_lo) | ((_hi) << 4))
#define PCIMDAS_DI_DO_REG		0x01
#define PCIMDAS_STATUS_REG		0x02
#define PCIMDAS_STATUS_EOC		BIT(7)
#define PCIMDAS_STATUS_UB		BIT(6)
#define PCIMDAS_STATUS_MUX		BIT(5)
#define PCIMDAS_STATUS_CLK		BIT(4)
#define PCIMDAS_STATUS_TO_CURR_MUX(x)	((x) & 0xf)
#define PCIMDAS_CONV_STATUS_REG		0x03
#define PCIMDAS_CONV_STATUS_EOC		BIT(7)
#define PCIMDAS_CONV_STATUS_EOB		BIT(6)
#define PCIMDAS_CONV_STATUS_EOA		BIT(5)
#define PCIMDAS_CONV_STATUS_FNE		BIT(4)
#define PCIMDAS_CONV_STATUS_FHF		BIT(3)
#define PCIMDAS_CONV_STATUS_OVERRUN	BIT(2)
#define PCIMDAS_IRQ_REG			0x04
#define PCIMDAS_IRQ_INTE		BIT(7)
#define PCIMDAS_IRQ_INT			BIT(6)
#define PCIMDAS_IRQ_OVERRUN		BIT(4)
#define PCIMDAS_IRQ_EOA			BIT(3)
#define PCIMDAS_IRQ_EOA_INT_SEL		BIT(2)
#define PCIMDAS_IRQ_INTSEL(x)		((x) << 0)
#define PCIMDAS_IRQ_INTSEL_EOC		PCIMDAS_IRQ_INTSEL(0)
#define PCIMDAS_IRQ_INTSEL_FNE		PCIMDAS_IRQ_INTSEL(1)
#define PCIMDAS_IRQ_INTSEL_EOB		PCIMDAS_IRQ_INTSEL(2)
#define PCIMDAS_IRQ_INTSEL_FHF_EOA	PCIMDAS_IRQ_INTSEL(3)
#define PCIMDAS_PACER_REG		0x05
#define PCIMDAS_PACER_GATE_STATUS	BIT(6)
#define PCIMDAS_PACER_GATE_POL		BIT(5)
#define PCIMDAS_PACER_GATE_LATCH	BIT(4)
#define PCIMDAS_PACER_GATE_EN		BIT(3)
#define PCIMDAS_PACER_EXT_PACER_POL	BIT(2)
#define PCIMDAS_PACER_SRC(x)		((x) << 0)
#define PCIMDAS_PACER_SRC_POLLED	PCIMDAS_PACER_SRC(0)
#define PCIMDAS_PACER_SRC_EXT		PCIMDAS_PACER_SRC(2)
#define PCIMDAS_PACER_SRC_INT		PCIMDAS_PACER_SRC(3)
#define PCIMDAS_PACER_SRC_MASK		(3 << 0)
#define PCIMDAS_BURST_REG		0x06
#define PCIMDAS_BURST_BME		BIT(1)
#define PCIMDAS_BURST_CONV_EN		BIT(0)
#define PCIMDAS_GAIN_REG		0x07
#define PCIMDAS_8254_BASE		0x08
#define PCIMDAS_USER_CNTR_REG		0x0c
#define PCIMDAS_USER_CNTR_CTR1_CLK_SEL	BIT(0)
#define PCIMDAS_RESIDUE_MSB_REG		0x0d
#define PCIMDAS_RESIDUE_LSB_REG		0x0e
#define PCIMDAS_8255_BASE		0x00

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
    uint8_t irq_reg;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t ai_reg;
    uint8_t ao_reg[2];
    uint8_t mux_reg;
    uint8_t di_do_reg;
    uint8_t status_reg;
    uint8_t conv_status_reg;
    uint8_t pacer_reg;
    uint8_t burst_reg;
    uint8_t gain_reg;
    uint8_t user_cntr_reg;
    uint8_t residue_msb_reg;
    uint8_t residue_lsb_reg;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
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

    if (size == 2) {
        switch (addr) {
        case PCIMDAS_AI_REG:
            val = 0x7fff;
            break;
        }
    } else if (size == 1) {
        switch (addr) {
        case PCIMDAS_DI_DO_REG:
            val = s->di_do_reg;
            break;
        case PCIMDAS_STATUS_REG:
            val = s->status_reg;
            break;
        case PCIMDAS_PACER_REG:
            val = s->pacer_reg;
            break;
        case PCIMDAS_USER_CNTR_REG:
            val = s->user_cntr_reg;
            break;
        }
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    if (size == 2) {
        switch (addr) {
        case PCIMDAS_AI_SOFTTRIG_REG:
            s->status_reg &= ~PCIMDAS_STATUS_EOC;
            break;
        case 0x02: /* PCIMDAS_AO_REG(0) */
            s->ao_reg[0] = val & 0xff;
            break;
        case 0x04: /* PCIMDAS_AO_REG(1) */
            s->ao_reg[1] = val & 0xff;
            break;
        }
    } else if (size == 1) {
        switch (addr) {
        case PCIMDAS_MUX_REG:
            s->mux_reg = val;
            break;
        case PCIMDAS_DI_DO_REG:
            s->di_do_reg = val;
            break;
        case PCIMDAS_PACER_REG:
            s->pacer_reg = val;
            break;
        case PCIMDAS_BURST_REG:
            s->burst_reg = val;
            break;
        case PCIMDAS_GAIN_REG:
            s->gain_reg = val;
            break;
        case PCIMDAS_USER_CNTR_REG:
            s->user_cntr_reg = val;
            break;
        }
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

    s->ai_reg = 0;
    s->ao_reg[0] = 0;
    s->ao_reg[1] = 0;
    s->mux_reg = 0;
    s->di_do_reg = 0;
    s->status_reg = 0;
    s->conv_status_reg = 0;
    s->pacer_reg = 0;
    s->burst_reg = 0;
    s->gain_reg = 0;
    s->user_cntr_reg = 0;
    s->residue_msb_reg = 0;
    s->residue_lsb_reg = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CB );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0056 );
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
    s->num_bars = 5;
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_PIO, 16, "daqio"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_PIO, 16, "BADR3"};
    s->bar_info[4] = (BARInfo){4, BAR_TYPE_PIO, 16, "iobase"};
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cb_pcimdas_pci",
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
