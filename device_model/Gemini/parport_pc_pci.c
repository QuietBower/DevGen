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

#define TYPE_PCIBASE_DEVICE "parport_pc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_VIA 0x1106
#define PCI_DEVICE_ID_VIA_686A 0x0686

#define ECR_SPP 0x00
#define ECR_PS2 0x01
#define ECR_PPF 0x02
#define ECR_ECP 0x03
#define ECR_EPP 0x04
#define ECR_VND 0x05
#define ECR_TST 0x06
#define ECR_CNF 0x07
#define ECR_MODE_MASK 0xe0

#define VIA_FUNCTION_PARPORT_SPP     0x00
#define VIA_PARPORT_BIDIR  0x80
#define VIA_FUNCTION_PARPORT_EPP     0x02
#define VIA_FUNCTION_PARPORT_ECP     0x01
#define VIA_PARPORT_ECPEPP 0X20
#define VIA_FUNCTION_PROBE           0xFF
#define VIA_CONFIG_INDEX 0x3F0
#define VIA_CONFIG_DATA  0x3F1
#define VIA_FUNCTION_PARPORT_DISABLE 0x03
#define VIA_IRQCONTROL_PARALLEL 0xF0
#define VIA_DMACONTROL_PARALLEL 0x0C

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
    int irq;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    unsigned char ecr_writable;
    unsigned int mode_mask;

    /* DMA Context */
    int dma;

    /* Parport Shadow Registers */
    uint8_t data;
    uint8_t status;
    uint8_t control;
    uint8_t epp_addr;
    uint8_t epp_data;
    uint8_t ecr;
    uint8_t configa;
    uint8_t configb;
    int fifo_depth;
};

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
    uint64_t val = 0xFF;

    switch (addr) {
    case 0x0: val = s->data; break;
    case 0x1: val = s->status; break;
    case 0x2: val = s->control; break;
    case 0x3: val = s->epp_addr; break;
    case 0x4: val = s->epp_data; break;
    case 0x400:
        if ((s->ecr & ECR_MODE_MASK) == (ECR_CNF << 5)) {
            val = s->configa;
        } else {
            if (s->fifo_depth > 0) s->fifo_depth--;
            val = 0; /* FIFO read */
        }
        break;
    case 0x401: val = s->configb; break;
    case 0x402:
        val = s->ecr & 0xFC;
        if (s->fifo_depth == 0) val |= 0x01; /* FIFO Empty */
        if (s->fifo_depth >= 16) val |= 0x02; /* FIFO Full */
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x0: s->data = val; break;
    case 0x1: s->status = (s->status & ~val) | (val & 0x01); break;
    case 0x2: s->control = val; break;
    case 0x3: s->epp_addr = val; break;
    case 0x4: s->epp_data = val; break;
    case 0x400:
        if ((s->ecr & ECR_MODE_MASK) != (ECR_CNF << 5)) {
            if (s->fifo_depth < 16) s->fifo_depth++;
        }
        break;
    case 0x402:
        s->ecr = val;
        if ((val & ECR_MODE_MASK) == (ECR_SPP << 5)) {
            s->fifo_depth = 0; /* Reset FIFO in SPP mode */
        }
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

    s->data = 0;
    s->status = 0xDE; /* No timeout, paper out, selected */
    s->control = 0x0C;
    s->epp_addr = 0;
    s->epp_data = 0;
    s->ecr = 0x15;
    s->configa = 0x10; /* 8-bit pword */
    s->configb = 0x38; /* IRQ 7 */
    s->fifo_depth = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1106 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0686 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0701 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* VIA SuperIO specific config to satisfy sio_via_probe */
    pci_conf[0x85] = 0x02; /* via_pci_superio_config_data */
    pci_conf[0xE2] = 0x01; /* ECP mode */
    pci_conf[0xF0] = 0x00; /* Parport control */
    pci_conf[0xE6] = 0xDE; /* Base 0x378 >> 2 */
    pci_conf[0x51] = 0x70; /* IRQ 7 */
    pci_conf[0x50] = 0x0C; /* DMA 3 */

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 0x1000, "parport_pio"};
    s->num_bars = 1;
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
    .name = "parport_pc_pci",
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
