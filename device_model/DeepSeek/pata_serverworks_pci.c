/*
 * QEMU model of ServerWorks OSB4 IDE controller
 * based on pata_serverworks.c driver analysis
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

#define TYPE_PCIBASE_DEVICE "pata_serverworks_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1166  /* PCI_VENDOR_ID_SERVERWORKS */
#define DEVICE_ID 0x0211  /* PCI_DEVICE_ID_SERVERWORKS_OSB4IDE */
#define CLASS_ID  0x018A  /* PCI_CLASS_STORAGE_IDE with prog-if 0x8A (dual-channel IDE, bus master) */

/* BMDMA register offsets */
#define ATA_DMA_CMD       0x0
#define ATA_DMA_STATUS    0x2
#define ATA_DMA_PRD_ADDR  0x4

#define ATA_DMA_CMD_START  0x01
#define ATA_DMA_CMD_SIMPLEX 0x80
#define ATA_DMA_STATUS_DMA0 0x20  /* drive 0 DMA capable */
#define ATA_DMA_STATUS_DMA1 0x40  /* drive 1 DMA capable */

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

/* opaque for per-BAR I/O dispatch */
typedef struct PioBarOpaque {
    PCIBaseState *base;
    int bar_index;
} PioBarOpaque;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Per-BAR opaque data for I/O regions */
    PioBarOpaque pio_opaque[6];

    /* BMDMA registers (two channels) */
    uint8_t bmdma_cmd[2];
    uint8_t bmdma_status[2];
    uint32_t bmdma_prd_addr[2];
};

/* MMIO/PIO Handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PioBarOpaque *pio = opaque;
    PCIBaseState *s = pio->base;
    int bar = pio->bar_index;

    if (bar == 4) {
        /* BMDMA registers */
        int channel = (addr < 8) ? 0 : 1;
        hwaddr reg = addr & 0x7;

        switch (reg) {
        case ATA_DMA_CMD:
            return s->bmdma_cmd[channel];
        case ATA_DMA_STATUS:
            return s->bmdma_status[channel];
        case ATA_DMA_PRD_ADDR:
            return s->bmdma_prd_addr[channel];
        default:
            return 0;
        }
    } else {
        /* BAR0-3: IDE task file registers, return 0 */
        return 0;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PioBarOpaque *pio = opaque;
    PCIBaseState *s = pio->base;
    int bar = pio->bar_index;

    if (bar == 4) {
        int channel = (addr < 8) ? 0 : 1;
        hwaddr reg = addr & 0x7;

        switch (reg) {
        case ATA_DMA_CMD:
            s->bmdma_cmd[channel] = val & 0xFF;
            /* No actual DMA emulation; clear START bit if set */
            if (val & ATA_DMA_CMD_START) {
                s->bmdma_cmd[channel] &= ~ATA_DMA_CMD_START;
            }
            break;
        case ATA_DMA_STATUS:
            /* W1C for interrupt/error bits (bits 1:2) */
            s->bmdma_status[channel] &= ~(val & 0x06);
            break;
        case ATA_DMA_PRD_ADDR:
            s->bmdma_prd_addr[channel] = (uint32_t)val;
            break;
        default:
            break;
        }
    } else {
        /* BAR0-3 writes ignored */
    }
}

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

    /* Reset BMDMA state */
    s->bmdma_cmd[0] = 0;
    s->bmdma_cmd[1] = 0;
    s->bmdma_status[0] = ATA_DMA_STATUS_DMA0 | ATA_DMA_STATUS_DMA1;
    s->bmdma_status[1] = ATA_DMA_STATUS_DMA0 | ATA_DMA_STATUS_DMA1;
    s->bmdma_prd_addr[0] = 0;
    s->bmdma_prd_addr[1] = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        /* Use opaque that includes base state and bar index */
        s->pio_opaque[bi->index].base = s;
        s->pio_opaque[bi->index].bar_index = bi->index;
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, &s->pio_opaque[bi->index],
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_MMIO) {
        /* Not used yet */
        memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x00);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set command register to enable I/O and bus mastering */
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MASTER);

    /* BAR Initialization */
    s->num_bars = 5;
    /* BAR0: primary command block (8 bytes) */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 8, .name = "ide-primary-cmd" };
    /* BAR1: primary control block (4 bytes) */
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 4, .name = "ide-primary-ctl" };
    /* BAR2: secondary command block (8 bytes) */
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 8, .name = "ide-secondary-cmd" };
    /* BAR3: secondary control block (4 bytes) */
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 4, .name = "ide-secondary-ctl" };
    /* BAR4: bus master IDE (16 bytes) */
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 16, .name = "ide-bmdma" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Assign fixed I/O port addresses to BARs so the kernel can find them */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_0, 0x1f0 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_1, 0x3f4 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_2, 0x170 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_3, 0x374 | PCI_BASE_ADDRESS_SPACE_IO);
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_4, 0x1000 | PCI_BASE_ADDRESS_SPACE_IO);

    /* Initialize BMDMA registers */
    s->bmdma_status[0] = ATA_DMA_STATUS_DMA0 | ATA_DMA_STATUS_DMA1;
    s->bmdma_status[1] = ATA_DMA_STATUS_DMA0 | ATA_DMA_STATUS_DMA1;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No MSI/MSI-X to clean up */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_serverworks_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(bmdma_cmd, PCIBaseState, 2),
        VMSTATE_UINT8_ARRAY(bmdma_status, PCIBaseState, 2),
        VMSTATE_UINT32_ARRAY(bmdma_prd_addr, PCIBaseState, 2),
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

type_init(pcibase_register_types)
