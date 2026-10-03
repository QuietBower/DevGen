/*
 * QEMU PCI device model for pata_hpt366.
 * Based on Linux driver: /home/eely/linux-7.1/drivers/ata/pata_hpt366.c
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

#define TYPE_PCIBASE_DEVICE "pata_hpt366_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_TTI            0x1103
#define PCI_DEVICE_ID_TTI_HPT366     0x0004
#define PCI_CLASS_STORAGE_IDE        0x0101
#define HPT366_ENABLE_REG            0x50
#define HPT366_PCI_CLOCK_REG         0x40
#define HPT366_CLOCK_MASK            0xf00
#define HPT366_CLOCK_SHIFT           8
#define HPT366_CLOCK_40MHZ           9
#define HPT366_CLOCK_25MHZ           5

/* ATA transfer mode constants */
#define XFER_UDMA_0     0x40
#define XFER_UDMA_1     0x41
#define XFER_UDMA_2     0x42
#define XFER_UDMA_3     0x43
#define XFER_UDMA_4     0x44
#define XFER_MW_DMA_0   0x20
#define XFER_MW_DMA_1   0x21
#define XFER_MW_DMA_2   0x22
#define XFER_PIO_0     0x10
#define XFER_PIO_1     0x11
#define XFER_PIO_2     0x12
#define XFER_PIO_3     0x13
#define XFER_PIO_4     0x14

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
};

struct hpt_clock {
    uint8_t xfer_mode;
    uint32_t timing;
};

static const struct hpt_clock hpt366_40[] = {
    { XFER_UDMA_4, 0x900fd943 },
    { XFER_UDMA_3, 0x900ad943 },
    { XFER_UDMA_2, 0x900bd943 },
    { XFER_UDMA_1, 0x9008d943 },
    { XFER_UDMA_0, 0x9008d943 },
    { XFER_MW_DMA_2, 0xa008d943 },
    { XFER_MW_DMA_1, 0xa010d955 },
    { XFER_MW_DMA_0, 0xa010d9fc },
    { XFER_PIO_4, 0xc008d963 },
    { XFER_PIO_3, 0xc010d974 },
    { XFER_PIO_2, 0xc010d997 },
    { XFER_PIO_1, 0xc010d9c7 },
    { XFER_PIO_0, 0xc018d9d9 },
    { 0, 0x0120d9d9 }
};

static const struct hpt_clock hpt366_33[] = {
    { XFER_UDMA_4, 0x90c9a731 },
    { XFER_UDMA_3, 0x90cfa731 },
    { XFER_UDMA_2, 0x90caa731 },
    { XFER_UDMA_1, 0x90cba731 },
    { XFER_UDMA_0, 0x90c8a731 },
    { XFER_MW_DMA_2, 0xa0c8a731 },
    { XFER_MW_DMA_1, 0xa0c8a732 },
    { XFER_MW_DMA_0, 0xa0c8a797 },
    { XFER_PIO_4, 0xc0c8a731 },
    { XFER_PIO_3, 0xc0c8a742 },
    { XFER_PIO_2, 0xc0d0a753 },
    { XFER_PIO_1, 0xc0d0a7a3 },
    { XFER_PIO_0, 0xc0d0a7aa },
    { 0, 0x0120a7a7 }
};

static const struct hpt_clock hpt366_25[] = {
    { XFER_UDMA_4, 0x90c98521 },
    { XFER_UDMA_3, 0x90cf8521 },
    { XFER_UDMA_2, 0x90cf8521 },
    { XFER_UDMA_1, 0x90cb8521 },
    { XFER_UDMA_0, 0x90cb8521 },
    { XFER_MW_DMA_2, 0xa0ca8521 },
    { XFER_MW_DMA_1, 0xa0ca8532 },
    { XFER_MW_DMA_0, 0xa0ca8575 },
    { XFER_PIO_4, 0xc0ca8521 },
    { XFER_PIO_3, 0xc0ca8532 },
    { XFER_PIO_2, 0xc0ca8542 },
    { XFER_PIO_1, 0xc0d08572 },
    { XFER_PIO_0, 0xc0d08585 },
    { 0, 0x01208585 }
};

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Unused: driver uses only PCI config space */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Unused: driver uses only PCI config space */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Unused: driver uses only PCI config space */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Unused: driver uses only PCI config space */
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    pci_device_reset(pdev);
    /* Set initial config values expected by the driver */
    pdev->config[0x50] = 0x30;   /* Enable both channels */
    /* 0x40 clock bits default to 0 -> 33 MHz table */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TTI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_TTI_HPT366 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE );
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x85);   /* Native IDE with Bus Master */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize BARs */
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
    .name = "pata_hpt366_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_instance_init(Object *obj)
{
    PCIBaseState *s = PCIBASE_DEVICE(obj);
    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 8, .name = "ide-cmd0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 4, .name = "ide-ctl0" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 8, .name = "ide-cmd1" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 4, .name = "ide-ctl1" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 16, .name = "ide-bmdma" };
}

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
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
