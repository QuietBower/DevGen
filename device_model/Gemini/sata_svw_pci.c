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

#define TYPE_PCIBASE_DEVICE "sata_svw_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_SERVERWORKS 0x1166
#define PCI_DEVICE_ID_SERVERWORKS_SVW4 0x0240

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
    
    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t sicr1;
    uint32_t scr_error;
    uint32_t sim;
    uint32_t scr[8][16];
    uint8_t dma_cmd[8];
    uint32_t dma_table[8];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

#ifdef K2_SATA_SICR1_OFFSET
    if (addr == K2_SATA_SICR1_OFFSET) {
        return s->sicr1;
    }
#endif
#ifdef K2_SATA_SCR_ERROR_OFFSET
    if (addr == K2_SATA_SCR_ERROR_OFFSET) {
        return s->scr_error;
    }
#endif
#ifdef K2_SATA_SIM_OFFSET
    if (addr == K2_SATA_SIM_OFFSET) {
        return s->sim;
    }
#endif
#if defined(K2_SATA_PORT_OFFSET) && defined(K2_SATA_DMA_CMD_OFFSET) && defined(ATA_DMA_CMD)
    for (int i = 0; i < 8; i++) {
        hwaddr port_base = i * K2_SATA_PORT_OFFSET;
        if (addr >= port_base && addr < port_base + K2_SATA_PORT_OFFSET) {
            hwaddr offset = addr - port_base;
            if (offset == K2_SATA_DMA_CMD_OFFSET + ATA_DMA_CMD) {
                return s->dma_cmd[i];
            }
#if defined(K2_SATA_SCR_STATUS_OFFSET) && defined(SCR_CONTROL)
            if (offset >= K2_SATA_SCR_STATUS_OFFSET && offset <= K2_SATA_SCR_STATUS_OFFSET + (SCR_CONTROL * 4)) {
                int sc_reg = (offset - K2_SATA_SCR_STATUS_OFFSET) / 4;
                return s->scr[i][sc_reg];
            }
#endif
        }
    }
#endif

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

#ifdef K2_SATA_SICR1_OFFSET
    if (addr == K2_SATA_SICR1_OFFSET) {
        s->sicr1 = val;
        return;
    }
#endif
#ifdef K2_SATA_SCR_ERROR_OFFSET
    if (addr == K2_SATA_SCR_ERROR_OFFSET) {
        s->scr_error = val;
        return;
    }
#endif
#ifdef K2_SATA_SIM_OFFSET
    if (addr == K2_SATA_SIM_OFFSET) {
        s->sim = val;
        return;
    }
#endif
#if defined(K2_SATA_PORT_OFFSET) && defined(K2_SATA_DMA_CMD_OFFSET) && defined(ATA_DMA_CMD)
    for (int i = 0; i < 8; i++) {
        hwaddr port_base = i * K2_SATA_PORT_OFFSET;
        if (addr >= port_base && addr < port_base + K2_SATA_PORT_OFFSET) {
            hwaddr offset = addr - port_base;
            if (offset == K2_SATA_DMA_CMD_OFFSET + ATA_DMA_CMD) {
                s->dma_cmd[i] = val;
                return;
            }
#ifdef ATA_DMA_TABLE_OFS
            if (offset == K2_SATA_DMA_CMD_OFFSET + ATA_DMA_TABLE_OFS) {
                s->dma_table[i] = val;
                return;
            }
#endif
#if defined(K2_SATA_SCR_STATUS_OFFSET) && defined(SCR_CONTROL)
            if (offset >= K2_SATA_SCR_STATUS_OFFSET && offset <= K2_SATA_SCR_STATUS_OFFSET + (SCR_CONTROL * 4)) {
                int sc_reg = (offset - K2_SATA_SCR_STATUS_OFFSET) / 4;
                s->scr[i][sc_reg] = val;
                return;
            }
#endif
        }
    }
#endif
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

    s->sicr1 = 0;
    s->scr_error = 0;
    s->sim = 0;
    memset(s->scr, 0, sizeof(s->scr));
    memset(s->dma_cmd, 0, sizeof(s->dma_cmd));
    memset(s->dma_table, 0, sizeof(s->dma_table));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SERVERWORKS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SERVERWORKS_SVW4 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SATA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 6;
    s->bar_info[5].index = 5;
    s->bar_info[5].type = BAR_TYPE_MMIO;
    s->bar_info[5].size = 0x2000; /* Placeholder size */
    s->bar_info[5].name = "mmio";
      
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
    .name = "sata_svw_pci",
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
