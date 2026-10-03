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


#define TYPE_PCIBASE_DEVICE "hinic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_HUAWEI 0x19e5
#define HINIC_DEV_ID_QUAD_PORT_25GE 0x1822

#define HINIC_PCI_CFG_REGS_BAR 0
#define HINIC_PCI_INTR_REGS_BAR 2
#define HINIC_PCI_DB_BAR 4
#define HINIC_DB_SIZE 0x400000 /* SZ_4M */

#define HINIC_CSR_FUNC_ATTR4_ADDR 0x10

#define HINIC_FUNC_CSR_MAILBOX_DATA_OFF 0x80
#define HINIC_FUNC_CSR_MAILBOX_CONTROL_OFF 0x0100
#define HINIC_FUNC_CSR_MAILBOX_INT_OFFSET_OFF 0x0104
#define HINIC_FUNC_CSR_MAILBOX_RESULT_H_OFF 0x0108
#define HINIC_FUNC_CSR_MAILBOX_RESULT_L_OFF 0x010C

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
    uint32_t func_attr4;
    uint32_t mailbox_data;
    uint32_t mailbox_control;
    uint32_t mailbox_int_offset;
    uint32_t mailbox_result_h;
    uint32_t mailbox_result_l;

    /* DMA Context */
    dma_addr_t ci_dma_base;
    dma_addr_t wb_status_paddr;

    uint32_t status;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case HINIC_CSR_FUNC_ATTR4_ADDR:
        val = s->func_attr4;
        break;
    case HINIC_FUNC_CSR_MAILBOX_DATA_OFF:
        val = s->mailbox_data;
        break;
    case HINIC_FUNC_CSR_MAILBOX_CONTROL_OFF:
        val = s->mailbox_control;
        break;
    case HINIC_FUNC_CSR_MAILBOX_INT_OFFSET_OFF:
        val = s->mailbox_int_offset;
        break;
    case HINIC_FUNC_CSR_MAILBOX_RESULT_H_OFF:
        val = s->mailbox_result_h;
        break;
    case HINIC_FUNC_CSR_MAILBOX_RESULT_L_OFF:
        val = s->mailbox_result_l;
        break;
    default:
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HINIC_CSR_FUNC_ATTR4_ADDR:
        s->func_attr4 = val;
        break;
    case HINIC_FUNC_CSR_MAILBOX_DATA_OFF:
        s->mailbox_data = val;
        break;
    case HINIC_FUNC_CSR_MAILBOX_CONTROL_OFF:
        s->mailbox_control = val;
        break;
    case HINIC_FUNC_CSR_MAILBOX_INT_OFFSET_OFF:
        s->mailbox_int_offset = val;
        break;
    case HINIC_FUNC_CSR_MAILBOX_RESULT_H_OFF:
        s->mailbox_result_h = val;
        break;
    case HINIC_FUNC_CSR_MAILBOX_RESULT_L_OFF:
        s->mailbox_result_l = val;
        break;
    default:
        break;
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

    s->func_attr4 = 0;
    s->mailbox_data = 0;
    s->mailbox_control = 0;
    s->mailbox_int_offset = 0;
    s->mailbox_result_h = 0;
    s->mailbox_result_l = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_HUAWEI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  HINIC_DEV_ID_QUAD_PORT_25GE );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0] = (BARInfo){ .index = HINIC_PCI_CFG_REGS_BAR, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "hinic-cfg" };
    s->bar_info[1] = (BARInfo){ .index = HINIC_PCI_INTR_REGS_BAR, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "hinic-intr" };
    s->bar_info[2] = (BARInfo){ .index = HINIC_PCI_DB_BAR, .type = BAR_TYPE_MMIO, .size = HINIC_DB_SIZE, .name = "hinic-db" };  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;
    msix_init(pdev, 32, &s->bar_regions[HINIC_PCI_CFG_REGS_BAR], HINIC_PCI_CFG_REGS_BAR, 0,
              &s->bar_regions[HINIC_PCI_CFG_REGS_BAR], HINIC_PCI_CFG_REGS_BAR, 0x800, 0, errp);  
      
      
      
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
    .name = "hinic_pci",
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
