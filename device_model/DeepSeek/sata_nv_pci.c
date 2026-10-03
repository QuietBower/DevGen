/* 
 * QEMU model of NVIDIA SATA controller (sata_nv.c driver). 
 * This model provides minimal PCI BARs and flat register regions 
 * to allow the driver to probe and bind. Phase 2: functional skeleton with 
 * PIO handlers for legacy IDE BARs and a simple MMIO BAR5. 
 * No specific register logic is implemented yet due to missing offset macros; 
 * the regions act as plain memory to satisfy basic driver operations. 
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
 
#define TYPE_PCIBASE_DEVICE "sata_nv_pci" 
typedef struct PCIBaseState PCIBaseState; 
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE) 
 
#define VENDOR_ID 0x10DE 
#define DEVICE_ID 0x008E 
#define CLASS_ID 0x0106 
 
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
 
/* BAR definitions for a typical NVIDIA SATA controller (6 BARs). */ 
#define BAR0_INDEX 0 
#define BAR1_INDEX 1 
#define BAR2_INDEX 2 
#define BAR3_INDEX 3 
#define BAR4_INDEX 4 
#define BAR5_INDEX 5 
 
#define BAR0_SIZE 8 
#define BAR1_SIZE 4 
#define BAR2_SIZE 8 
#define BAR3_SIZE 4 
#define BAR4_SIZE 16 
#define BAR5_SIZE 0x10000   /* 64KB MMIO region */ 
 
struct PCIBaseState { 
    PCIDevice parent_obj; 
 
    MemoryRegion bar_regions[6]; 
    BARInfo bar_info[6]; 
    int num_bars; 
 
    bool has_msi; 
    bool has_msix; 
    uint32_t intr_status; 
    uint32_t intr_mask; 
 
    /* Flat storage for I/O BARs (taskfile and BMDMA) */ 
    uint8_t bar0_data[BAR0_SIZE]; 
    uint8_t bar1_data[BAR1_SIZE]; 
    uint8_t bar2_data[BAR2_SIZE]; 
    uint8_t bar3_data[BAR3_SIZE]; 
    uint8_t bar4_data[BAR4_SIZE]; 
 
    uint8_t pm_status; 
}; 
 
/* --- IRQ helper --- */ 
static void pcibase_update_irq(PCIBaseState *s) 
{ 
    PCIDevice *pdev = PCI_DEVICE(s); 
    if (s->intr_status & s->intr_mask) { 
        if (s->has_msi && msi_enabled(pdev)) { 
            msi_notify(pdev, 0); 
        } else { 
            pci_set_irq(pdev, 1); 
        } 
    } else { 
        if (!s->has_msi || !msi_enabled(pdev)) { 
            pci_set_irq(pdev, 0); 
        } 
    } 
} 
 
/* --- PIO handlers for BAR0 (primary command) --- */ 
static uint64_t pcibase_pio_read_bar0(void *opaque, hwaddr addr, unsigned size) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(opaque); 
    uint64_t val = 0; 
    if (addr < sizeof(s->bar0_data)) { 
        memcpy(&val, &s->bar0_data[addr], MIN(size, sizeof(s->bar0_data) - addr)); 
    } 
    return val; 
} 
 
static void pcibase_pio_write_bar0(void *opaque, hwaddr addr, uint64_t val, unsigned size) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(opaque); 
    if (addr < sizeof(s->bar0_data)) { 
        memcpy(&s->bar0_data[addr], &val, MIN(size, sizeof(s->bar0_data) - addr)); 
    } 
} 
 
static const MemoryRegionOps pcibase_pio0_ops = { 
    .read = pcibase_pio_read_bar0, 
    .write = pcibase_pio_write_bar0, 
    .endianness = DEVICE_LITTLE_ENDIAN, 
    .valid = { .min_access_size = 1, .max_access_size = 4 }, 
    .impl  = { .min_access_size = 1, .max_access_size = 4 }, 
}; 
 
/* --- PIO handlers for BAR1 (primary control) --- */ 
static uint64_t pcibase_pio_read_bar1(void *opaque, hwaddr addr, unsigned size) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(opaque); 
    uint64_t val = 0; 
    if (addr < sizeof(s->bar1_data)) { 
        memcpy(&val, &s->bar1_data[addr], MIN(size, sizeof(s->bar1_data) - addr)); 
    } 
    return val; 
} 
 
static void pcibase_pio_write_bar1(void *opaque, hwaddr addr, uint64_t val, unsigned size) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(opaque); 
    if (addr < sizeof(s->bar1_data)) { 
        memcpy(&s->bar1_data[addr], &val, MIN(size, sizeof(s->bar1_data) - addr)); 
    } 
} 
 
static const MemoryRegionOps pcibase_pio1_ops = { 
    .read = pcibase_pio_read_bar1, 
    .write = pcibase_pio_write_bar1, 
    .endianness = DEVICE_LITTLE_ENDIAN, 
    .valid = { .min_access_size = 1, .max_access_size = 4 }, 
    .impl  = { .min_access_size = 1, .max_access_size = 4 }, 
}; 
 
/* --- PIO handlers for BAR2 (secondary command) --- */ 
static uint64_t pcibase_pio_read_bar2(void *opaque, hwaddr addr, unsigned size) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(opaque); 
    uint64_t val = 0; 
    if (addr < sizeof(s->bar2_data)) { 
        memcpy(&val, &s->bar2_data[addr], MIN(size, sizeof(s->bar2_data) - addr)); 
    } 
    return val; 
} 
 
static void pcibase_pio_write_bar2(void *opaque, hwaddr addr, uint64_t val, unsigned size) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(opaque); 
    if (addr < sizeof(s->bar2_data)) { 
        memcpy(&s->bar2_data[addr], &val, MIN(size, sizeof(s->bar2_data) - addr)); 
    } 
} 
 
static const MemoryRegionOps pcibase_pio2_ops = { 
    .read = pcibase_pio_read_bar2, 
    .write = pcibase_pio_write_bar2, 
    .endianness = DEVICE_LITTLE_ENDIAN, 
    .valid = { .min_access_size = 1, .max_access_size = 4 }, 
    .impl  = { .min_access_size = 1, .max_access_size = 4 }, 
}; 
 
/* --- PIO handlers for BAR3 (secondary control) --- */ 
static uint64_t pcibase_pio_read_bar3(void *opaque, hwaddr addr, unsigned size) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(opaque); 
    uint64_t val = 0; 
    if (addr < sizeof(s->bar3_data)) { 
        memcpy(&val, &s->bar3_data[addr], MIN(size, sizeof(s->bar3_data) - addr)); 
    } 
    return val; 
} 
 
static void pcibase_pio_write_bar3(void *opaque, hwaddr addr, uint64_t val, unsigned size) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(opaque); 
    if (addr < sizeof(s->bar3_data)) { 
        memcpy(&s->bar3_data[addr], &val, MIN(size, sizeof(s->bar3_data) - addr)); 
    } 
} 
 
static const MemoryRegionOps pcibase_pio3_ops = { 
    .read = pcibase_pio_read_bar3, 
    .write = pcibase_pio_write_bar3, 
    .endianness = DEVICE_LITTLE_ENDIAN, 
    .valid = { .min_access_size = 1, .max_access_size = 4 }, 
    .impl  = { .min_access_size = 1, .max_access_size = 4 }, 
}; 
 
/* --- PIO handlers for BAR4 (BMDMA) --- */ 
static uint64_t pcibase_pio_read_bar4(void *opaque, hwaddr addr, unsigned size) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(opaque); 
    uint64_t val = 0; 
    if (addr < sizeof(s->bar4_data)) { 
        memcpy(&val, &s->bar4_data[addr], MIN(size, sizeof(s->bar4_data) - addr)); 
    } 
    return val; 
} 
 
static void pcibase_pio_write_bar4(void *opaque, hwaddr addr, uint64_t val, unsigned size) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(opaque); 
    if (addr < sizeof(s->bar4_data)) { 
        memcpy(&s->bar4_data[addr], &val, MIN(size, sizeof(s->bar4_data) - addr)); 
    } 
} 
 
static const MemoryRegionOps pcibase_pio4_ops = { 
    .read = pcibase_pio_read_bar4, 
    .write = pcibase_pio_write_bar4, 
    .endianness = DEVICE_LITTLE_ENDIAN, 
    .valid = { .min_access_size = 1, .max_access_size = 4 }, 
    .impl  = { .min_access_size = 1, .max_access_size = 4 }, 
}; 
 
/* --- Reset handler --- */ 
static void pcibase_reset(DeviceState *dev) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(dev); 
    pci_device_reset(PCI_DEVICE(dev)); 
 
    memset(s->bar0_data, 0, sizeof(s->bar0_data)); 
    memset(s->bar1_data, 0, sizeof(s->bar1_data)); 
    memset(s->bar2_data, 0, sizeof(s->bar2_data)); 
    memset(s->bar3_data, 0, sizeof(s->bar3_data)); 
    memset(s->bar4_data, 0, sizeof(s->bar4_data)); 
 
    s->intr_status = 0; 
    s->intr_mask = 0; 
} 
 
/* --- BAR registration helper --- */ 
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp) 
{ 
    if (!bi || bi->type == BAR_TYPE_NONE) { 
        return; 
    } 
 
    hwaddr aligned_size = pow2ceil(bi->size); 
    MemoryRegion *mr = &s->bar_regions[bi->index]; 
 
    switch (bi->type) { 
    case BAR_TYPE_PIO: 
        if (bi->index == BAR0_INDEX) { 
            memory_region_init_io(mr, OBJECT(s), &pcibase_pio0_ops, s, bi->name, aligned_size); 
        } else if (bi->index == BAR1_INDEX) { 
            memory_region_init_io(mr, OBJECT(s), &pcibase_pio1_ops, s, bi->name, aligned_size); 
        } else if (bi->index == BAR2_INDEX) { 
            memory_region_init_io(mr, OBJECT(s), &pcibase_pio2_ops, s, bi->name, aligned_size); 
        } else if (bi->index == BAR3_INDEX) { 
            memory_region_init_io(mr, OBJECT(s), &pcibase_pio3_ops, s, bi->name, aligned_size); 
        } else if (bi->index == BAR4_INDEX) { 
            memory_region_init_io(mr, OBJECT(s), &pcibase_pio4_ops, s, bi->name, aligned_size); 
        } else { 
            error_setg(errp, "unsupported PIO BAR index %d", bi->index); 
            return; 
        } 
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr); 
        break; 
 
    case BAR_TYPE_RAM: 
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp); 
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr); 
        break; 
 
    default: 
        error_setg(errp, "unsupported BAR type %d", bi->type); 
        break; 
    } 
} 
 
/* --- Realize function --- */ 
static void pcibase_realize(PCIDevice *pdev, Error **errp) 
{ 
    PCIBaseState *s = PCIBASE_DEVICE(pdev); 
    uint8_t *pci_conf = pdev->config; 
 
    /* Static PCI configuration */ 
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID); 
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID); 
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID); 
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01); 
    pci_config_set_interrupt_pin(pci_conf, 1); 
 
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS; 
    pcie_endpoint_cap_init(pdev, 0x80); 
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp); 
    if (pm_pos > 0) { 
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    } 
 
    /* Set up BAR info */ 
    s->num_bars = 6; 
    s->bar_info[BAR0_INDEX] = (BARInfo){ .index = BAR0_INDEX, .type = BAR_TYPE_PIO, .size = BAR0_SIZE, .name = "nv-bmdma0" }; 
    s->bar_info[BAR1_INDEX] = (BARInfo){ .index = BAR1_INDEX, .type = BAR_TYPE_PIO, .size = BAR1_SIZE, .name = "nv-bmdma1" }; 
    s->bar_info[BAR2_INDEX] = (BARInfo){ .index = BAR2_INDEX, .type = BAR_TYPE_PIO, .size = BAR2_SIZE, .name = "nv-bmdma2" }; 
    s->bar_info[BAR3_INDEX] = (BARInfo){ .index = BAR3_INDEX, .type = BAR_TYPE_PIO, .size = BAR3_SIZE, .name = "nv-bmdma3" }; 
    s->bar_info[BAR4_INDEX] = (BARInfo){ .index = BAR4_INDEX, .type = BAR_TYPE_PIO, .size = BAR4_SIZE, .name = "nv-bmdma4" }; 
    s->bar_info[BAR5_INDEX] = (BARInfo){ .index = BAR5_INDEX, .type = BAR_TYPE_RAM, .size = BAR5_SIZE, .name = "nv-mmio" }; 
 
    for (int i = 0; i < s->num_bars; i++) { 
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp); 
        if (*errp) { 
            return; 
        } 
    } 
 
    /* MSI initialization */ 
    s->has_msi = true; 
    s->has_msix = false; 
    if (s->has_msi) { 
        msi_init(pdev, 0, 1, true, false, errp); 
    } 
 
    s->pm_status = 0; 
    s->intr_status = 0; 
    s->intr_mask = 0; 
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
 
static const VMStateDescription vmstate_pcibase = { 
    .name = "sata_nv_pci", 
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
