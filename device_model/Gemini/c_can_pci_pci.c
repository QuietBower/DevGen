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


#define TYPE_PCIBASE_DEVICE "c_can_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_STMICRO      0x104A
#define PCI_DEVICE_ID_STMICRO_CAN  0xCC11
#define PCH_PCI_SOFT_RESET         0x01fc

enum reg {
    C_CAN_CTRL_REG = 0,
    C_CAN_CTRL_EX_REG,
    C_CAN_STS_REG,
    C_CAN_ERR_CNT_REG,
    C_CAN_BTR_REG,
    C_CAN_INT_REG,
    C_CAN_TEST_REG,
    C_CAN_BRPEXT_REG,
    C_CAN_IF1_COMREQ_REG,
    C_CAN_IF1_COMMSK_REG,
    C_CAN_IF1_MASK1_REG,
    C_CAN_IF1_MASK2_REG,
    C_CAN_IF1_ARB1_REG,
    C_CAN_IF1_ARB2_REG,
    C_CAN_IF1_MSGCTRL_REG,
    C_CAN_IF1_DATA1_REG,
    C_CAN_IF1_DATA2_REG,
    C_CAN_IF1_DATA3_REG,
    C_CAN_IF1_DATA4_REG,
    C_CAN_IF2_COMREQ_REG,
    C_CAN_IF2_COMMSK_REG,
    C_CAN_IF2_MASK1_REG,
    C_CAN_IF2_MASK2_REG,
    C_CAN_IF2_ARB1_REG,
    C_CAN_IF2_ARB2_REG,
    C_CAN_IF2_MSGCTRL_REG,
    C_CAN_IF2_DATA1_REG,
    C_CAN_IF2_DATA2_REG,
    C_CAN_IF2_DATA3_REG,
    C_CAN_IF2_DATA4_REG,
    C_CAN_TXRQST1_REG,
    C_CAN_TXRQST2_REG,
    C_CAN_NEWDAT1_REG,
    C_CAN_NEWDAT2_REG,
    C_CAN_INTPND1_REG,
    C_CAN_INTPND2_REG,
    C_CAN_INTPND3_REG,
    C_CAN_MSGVAL1_REG,
    C_CAN_MSGVAL2_REG,
    C_CAN_FUNCTION_REG,
};

enum c_can_dev_id {
    BOSCH_C_CAN,
    BOSCH_D_CAN,
};

enum c_can_pci_reg_align {
    C_CAN_REG_ALIGN_16,
    C_CAN_REG_ALIGN_32,
    C_CAN_REG_32,
};

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
    
    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t regs[0x200];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        switch (size) {
        case 1:
            val = ((uint8_t *)s->regs)[addr];
            break;
        case 2:
            val = lduw_le_p((uint8_t *)s->regs + addr);
            break;
        case 4:
            val = ldl_le_p((uint8_t *)s->regs + addr);
            break;
        }
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == PCH_PCI_SOFT_RESET) {
        if (val == 1) {
            /* Soft reset */
            memset(s->regs, 0, sizeof(s->regs));
        }
    }

    if (addr < sizeof(s->regs)) {
        switch (size) {
        case 1:
            ((uint8_t *)s->regs)[addr] = val;
            break;
        case 2:
            stw_le_p((uint8_t *)s->regs + addr, val);
            break;
        case 4:
            stl_le_p((uint8_t *)s->regs + addr, val);
            break;
        }
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

    memset(s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_STMICRO );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_STMICRO_CAN );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->has_msi = true;
    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "c_can_bar0";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "c_can_pci_pci",
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
