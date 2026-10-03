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


#define TYPE_PCIBASE_DEVICE "ke_counter_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_KOLTER
#define PCI_VENDOR_ID_KOLTER 0x1001
#endif

#define KE_RESET_REG(x)			(0x00 + ((x) * 0x20))
#define KE_LATCH_REG(x)			(0x00 + ((x) * 0x20))
#define KE_LSB_REG(x)			(0x04 + ((x) * 0x20))
#define KE_MID_REG(x)			(0x08 + ((x) * 0x20))
#define KE_MSB_REG(x)			(0x0c + ((x) * 0x20))
#define KE_SIGN_REG(x)			(0x10 + ((x) * 0x20))
#define KE_OSC_SEL_REG			0xf8
#define KE_OSC_SEL_CLK(x)		(((x) & 0x3) << 0)
#define KE_OSC_SEL_EXT			KE_OSC_SEL_CLK(1)
#define KE_OSC_SEL_4MHZ			KE_OSC_SEL_CLK(2)
#define KE_OSC_SEL_20MHZ		KE_OSC_SEL_CLK(3)
#define KE_DO_REG			0xfc

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t osc_sel;
    uint8_t do_reg;
    uint8_t lsb[3];
    uint8_t mid[3];
    uint8_t msb[3];
    uint8_t sign[3];
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
    int chan;

    if (addr == KE_OSC_SEL_REG) {
        val = s->osc_sel;
    } else if (addr == KE_DO_REG) {
        val = s->do_reg;
    } else {
        chan = addr / 0x20;
        if (chan < 3) {
            hwaddr offset = addr % 0x20;
            switch (offset) {
            case 0x00: /* KE_LATCH_REG */
                val = 0;
                break;
            case 0x04: /* KE_LSB_REG */
                val = s->lsb[chan];
                break;
            case 0x08: /* KE_MID_REG */
                val = s->mid[chan];
                break;
            case 0x0c: /* KE_MSB_REG */
                val = s->msb[chan];
                break;
            case 0x10: /* KE_SIGN_REG */
                val = s->sign[chan];
                break;
            }
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int chan;
    
    if (addr == KE_OSC_SEL_REG) {
        s->osc_sel = val;
    } else if (addr == KE_DO_REG) {
        s->do_reg = val;
    } else {
        chan = addr / 0x20;
        if (chan < 3) {
            hwaddr offset = addr % 0x20;
            switch (offset) {
            case 0x00: /* KE_RESET_REG */
                s->lsb[chan] = 0;
                s->mid[chan] = 0;
                s->msb[chan] = 0;
                s->sign[chan] = 0;
                break;
            case 0x04: /* KE_LSB_REG */
                s->lsb[chan] = val;
                break;
            case 0x08: /* KE_MID_REG */
                s->mid[chan] = val;
                break;
            case 0x0c: /* KE_MSB_REG */
                s->msb[chan] = val;
                break;
            case 0x10: /* KE_SIGN_REG */
                s->sign[chan] = val;
                break;
            }
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

    s->osc_sel = KE_OSC_SEL_20MHZ;
    s->do_reg = 0;
    for (int i = 0; i < 3; i++) {
        s->lsb[i] = 0;
        s->mid[i] = 0;
        s->msb[i] = 0;
        s->sign[i] = 0;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_KOLTER );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0014 );
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
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = 0x100,
        .name = "ke_counter_pio"
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
    .name = "ke_counter_pci",
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
