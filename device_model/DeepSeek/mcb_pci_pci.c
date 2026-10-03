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

#define TYPE_PCIBASE_DEVICE "mcb_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_MEN 0x1a88
#define PCI_DEVICE_ID_MEN_CHAMELEON 0x4d45
#define CHAM_HEADER_SIZE 0x200
#define CHAMELEONV2_MAGIC 0xabce
#define CHAMELEON_BAR_MAX 6
#define BAR_DESC_SIZE(x) ((x) * sizeof(struct chameleon_bar) + sizeof(uint32_t))
#define BAR_CNT(x) ((x) & 0x07)
#define CHAMELEON_FILENAME_LEN 12

#define GDD_DEV(x) (((x) >> 18) & 0x3ff)
#define GDD_BAR(x) ((x) & 0x7)
#define GDD_IRQ(x) ((x) & 0x1f)
#define GDD_REV(x) (((x) >> 5) & 0x3f)
#define GDD_INS(x) (((x) >> 3) & 0x3f)
#define GDD_VAR(x) (((x) >> 11) & 0x3f)
#define GDD_GRP(x) (((x) >> 9) & 0x3f)

struct chameleon_bar {
    uint32_t addr;
    uint32_t size;
};

struct chameleon_fpga_header {
    uint8_t revision;
    char model;
    uint8_t minor;
    uint8_t bus_type;
    uint16_t magic;
    uint16_t reserved;
    char filename[CHAMELEON_FILENAME_LEN];
} __attribute__((packed));

struct chameleon_bdd {
    unsigned int irq:6;
    unsigned int rev:6;
    unsigned int var:6;
    unsigned int dev:10;
    unsigned int dtype:4;
    unsigned int bar:3;
    unsigned int inst:6;
    unsigned int dbar:3;
    unsigned int group:6;
    unsigned int reserved:14;
    uint32_t chamoff;
    uint32_t offset;
    uint32_t size;
} __attribute__((packed));

struct chameleon_gdd {
    uint32_t reg1;
    uint32_t reg2;
    uint32_t offset;
    uint32_t size;
} __attribute__((packed));

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

    /* Chameleon table contents to feed the driver parser */
    uint8_t chameleon_table[CHAM_HEADER_SIZE];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < CHAM_HEADER_SIZE && addr + size <= CHAM_HEADER_SIZE) {
        memcpy(&val, &s->chameleon_table[addr], MIN(size, sizeof(val)));
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: MMIO read out of bounds: addr=0x%" HWADDR_PRIx " size=%u\n", __func__, addr, size);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No writes expected from driver – silently ignore */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    ;
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

static void build_chameleon_table(PCIBaseState *s)
{
    /* Build a minimal but valid Chameleon table sufficient for driver probing.
     * The table starts with a fake FPGA header, followed by BAR descriptors
     * and a single GDD cell configured to be skipped (dev=0x3FF) to avoid
     * duplicate device registration, terminated by an END marker.
     */
    uint8_t *p = s->chameleon_table;
    uint32_t *wp;
    struct chameleon_fpga_header header;
    struct chameleon_gdd gdd;

    memset(p, 0, CHAM_HEADER_SIZE);

    /* FPGA Header */
    header.revision = 1;
    header.model = 'Q';          /* arbitrary */
    header.minor = 0;
    header.bus_type = 0;
    header.magic = cpu_to_le16(CHAMELEONV2_MAGIC);
    header.reserved = 0;
    strncpy(header.filename, "QEMU_MCB", CHAMELEON_FILENAME_LEN);
    memcpy(p, &header, sizeof(header));
    p += sizeof(header);

    /* BAR descriptors: count = 0 (no additional BARs) */
    wp = (uint32_t *)p;
    *wp++ = 0;                     /* bar_count = 0, no descriptor follows */
    p = (uint8_t *)wp;

    /* Single GDD cell with dev = 0x3FF (no device) to skip registration */
    gdd.reg1 = cpu_to_le32(0x0FFC0000);   /* dev = 0x3FF, bar=0, ins=0, var=0, grp=0, rev=0 */
    gdd.reg2 = cpu_to_le32(0x00000000);
    gdd.offset = cpu_to_le32(0x00000000);
    gdd.size = cpu_to_le32(0x00001000);   /* arbitrary region size (ignored) */
    memcpy(p, &gdd, sizeof(gdd));
    p += sizeof(gdd);

    /* END cell: Mark with dtype 0xF in both reg1 and reg2 to ensure parsing stops */
    struct chameleon_gdd end_cell = {
        .reg1 = cpu_to_le32(0xF0000000),
        .reg2 = cpu_to_le32(0xF0000000),
        .offset = 0,
        .size = 0,
    };
    memcpy(p, &end_cell, sizeof(end_cell));
    p += sizeof(end_cell);

    /* Fill remaining space with 0xFF to prevent accidental parsing of trailing zeroes */
    memset(p, 0xFF, CHAM_HEADER_SIZE - (p - s->chameleon_table));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1a88 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x4d45 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = CHAM_HEADER_SIZE,
        .name = "bar0"
    };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Prepare the chameleon table content that the driver will parse */
    build_chameleon_table(s);
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
    .name = "mcb_pci_pci",
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
