/*
 * QEMU model for Opti 82C700 IDE controller (pata_optidma)
 * This file has been generated for Phase 4 - Runtime Refinement
 * Vendor ID: 0x1045, Device ID: 0xD568
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "pata_optidma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1045
#define DEVICE_ID 0xD568
#define CLASS_ID  0x0101  /* PCI_CLASS_STORAGE_IDE */

/* Legacy IDE port bases */
#define LEGACY_PRIMARY_BASE   0x1F0
#define LEGACY_SECONDARY_BASE 0x170

/* BAR Sizes */
#define BAR0_SIZE 8    /* primary cmd */
#define BAR1_SIZE 4    /* primary ctl */
#define BAR2_SIZE 8    /* secondary cmd */
#define BAR3_SIZE 4    /* secondary ctl */
#define BAR4_SIZE 16   /* bus master DMA */

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

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Bus master DMA registers (BAR4) */
    uint8_t bmdma_regs[16];

    /* Custom PCI configuration space (offsets 0x40-0xFF) */
    uint8_t custom_cfg[256];

    /* Fixed legacy I/O ports for chipset magic */
    MemoryRegion ioport_1f1;
    MemoryRegion ioport_1f5;
};

/* ---------- Fixed legacy I/O port handlers (0x1F1, 0x1F5) ---------- */

static uint64_t ioport_1f1_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "ioport_1f1_read: addr=" HWADDR_FMT_plx " size=%u\n", addr, size);
    return 0;
}

static void ioport_1f1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "ioport_1f1_write: addr=" HWADDR_FMT_plx " val=0x%" PRIx64 " size=%u\n", addr, val, size);
}

static const MemoryRegionOps ioport_1f1_ops = {
    .read = ioport_1f1_read,
    .write = ioport_1f1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static uint64_t ioport_1f5_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "ioport_1f5_read: addr=" HWADDR_FMT_plx " size=%u\n", addr, size);
    return 0;
}

static void ioport_1f5_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "ioport_1f5_write: addr=" HWADDR_FMT_plx " val=0x%" PRIx64 " size=%u\n", addr, val, size);
}

static const MemoryRegionOps ioport_1f5_ops = {
    .read = ioport_1f5_read,
    .write = ioport_1f5_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------- BAR0: Primary command block (8 bytes) ---------- */

static uint64_t prim_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    if (addr == 7) { /* status register */
        val = 0x00; /* no drive, BSY=0, DRDY=0 */
    }
    qemu_log_mask(LOG_UNIMP, "prim_cmd_read: addr=" HWADDR_FMT_plx " val=0x%lx size=%u\n", addr, val, size);
    return val;
}

static void prim_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "prim_cmd_write: addr=" HWADDR_FMT_plx " val=0x%lx size=%u\n", addr, val, size);
}

static const MemoryRegionOps prim_cmd_ops = {
    .read = prim_cmd_read,
    .write = prim_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------- BAR1: Primary control block (4 bytes) ---------- */

static uint64_t prim_ctl_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    qemu_log_mask(LOG_UNIMP, "prim_ctl_read: addr=" HWADDR_FMT_plx " val=0x%lx size=%u\n", addr, val, size);
    return val;
}

static void prim_ctl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "prim_ctl_write: addr=" HWADDR_FMT_plx " val=0x%lx size=%u\n", addr, val, size);
}

static const MemoryRegionOps prim_ctl_ops = {
    .read = prim_ctl_read,
    .write = prim_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------- BAR2: Secondary command block (8 bytes) ---------- */

static uint64_t sec_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    if (addr == 7) {
        val = 0x00;
    }
    qemu_log_mask(LOG_UNIMP, "sec_cmd_read: addr=" HWADDR_FMT_plx " val=0x%lx size=%u\n", addr, val, size);
    return val;
}

static void sec_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "sec_cmd_write: addr=" HWADDR_FMT_plx " val=0x%lx size=%u\n", addr, val, size);
}

static const MemoryRegionOps sec_cmd_ops = {
    .read = sec_cmd_read,
    .write = sec_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------- BAR3: Secondary control block (4 bytes) ---------- */

static uint64_t sec_ctl_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    qemu_log_mask(LOG_UNIMP, "sec_ctl_read: addr=" HWADDR_FMT_plx " val=0x%lx size=%u\n", addr, val, size);
    return val;
}

static void sec_ctl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "sec_ctl_write: addr=" HWADDR_FMT_plx " val=0x%lx size=%u\n", addr, val, size);
}

static const MemoryRegionOps sec_ctl_ops = {
    .read = sec_ctl_read,
    .write = sec_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------- BAR4: Bus master DMA (16 bytes) ---------- */

static uint64_t bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr < 16) {
        val = ldn_le_p(&s->bmdma_regs[addr], size);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "bmdma_read: addr out of range: " HWADDR_FMT_plx "\n", addr);
    }
    qemu_log_mask(LOG_UNIMP, "bmdma_read: addr=" HWADDR_FMT_plx " val=0x%lx size=%u\n", addr, val, size);
    return val;
}

static void bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < 16) {
        stn_le_p(&s->bmdma_regs[addr], size, val);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "bmdma_write: addr out of range: " HWADDR_FMT_plx "\n", addr);
    }
    qemu_log_mask(LOG_UNIMP, "bmdma_write: addr=" HWADDR_FMT_plx " val=0x%lx size=%u\n", addr, val, size);
}

static const MemoryRegionOps bmdma_ops = {
    .read = bmdma_read,
    .write = bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------- PCI configuration space handling ---------- */

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (addr >= 0x40 && addr + len <= 256) {
        return ldn_le_p(&s->custom_cfg[addr], len);
    }
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (addr >= 0x40 && addr + len <= 256) {
        stn_le_p(&s->custom_cfg[addr], len, val);
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

/* ---------- Reset handler ---------- */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->bmdma_regs, 0, sizeof(s->bmdma_regs));
    memset(s->custom_cfg, 0, sizeof(s->custom_cfg));
}

/* ---------- BAR registration helper ---------- */

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    const MemoryRegionOps *ops = NULL;

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 0) ops = &prim_cmd_ops;
        else if (bi->index == 1) ops = &prim_ctl_ops;
        else if (bi->index == 2) ops = &sec_cmd_ops;
        else if (bi->index == 3) ops = &sec_ctl_ops;
        else if (bi->index == 4) ops = &bmdma_ops;
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        if (bi->index == 0) ops = &prim_cmd_ops;
        else if (bi->index == 1) ops = &prim_ctl_ops;
        else if (bi->index == 2) ops = &sec_cmd_ops;
        else if (bi->index == 3) ops = &sec_ctl_ops;
        else if (bi->index == 4) ops = &bmdma_ops;
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ---------- Device realize/uninit ---------- */

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    memset(s->custom_cfg, 0, sizeof(s->custom_cfg));
    memset(s->bmdma_regs, 0, sizeof(s->bmdma_regs));

    s->num_bars = 5;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0-primary-cmd";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = BAR1_SIZE;
    s->bar_info[1].name = "bar1-primary-ctl";

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = BAR2_SIZE;
    s->bar_info[2].name = "bar2-secondary-cmd";

    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = BAR3_SIZE;
    s->bar_info[3].name = "bar3-secondary-ctl";

    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_PIO;
    s->bar_info[4].size = BAR4_SIZE;
    s->bar_info[4].name = "bar4-bmdma";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Pre-assign default I/O addresses to ensure BARs are not unassigned */
    /* Use a safe I/O range typically free in QEMU */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_0, 0xC001);  /* BAR0: addr 0xC000, I/O bit set */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_1, 0xC009);  /* BAR1: addr 0xC008 */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_2, 0xC011);  /* BAR2: addr 0xC010 */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_3, 0xC015);  /* BAR3: addr 0xC014 */
    pci_set_long(pci_conf + PCI_BASE_ADDRESS_4, 0xC021);  /* BAR4: addr 0xC020 */

    /* Enable I/O space and bus master */
    pci_set_word(pci_conf + PCI_COMMAND,
                 PCI_COMMAND_IO | PCI_COMMAND_MASTER);

    /* Add fixed I/O port responses for chipset-specific registers */
    memory_region_init_io(&s->ioport_1f1, OBJECT(s), &ioport_1f1_ops, s, "optidma-1f1", 2);
    memory_region_add_subregion(get_system_io(), 0x1f1, &s->ioport_1f1);

    memory_region_init_io(&s->ioport_1f5, OBJECT(s), &ioport_1f5_ops, s, "optidma-1f5", 1);
    memory_region_add_subregion(get_system_io(), 0x1f5, &s->ioport_1f5);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* Nothing special to clean up beyond default PCI cleanup */
}

/* ---------- VMState description ---------- */

static const VMStateDescription vmstate_pcibase = {
    .name = "pata_optidma_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(bmdma_regs, PCIBaseState, 16),
        VMSTATE_UINT8_ARRAY(custom_cfg, PCIBaseState, 256),
        VMSTATE_END_OF_LIST()
    }
};

/* ---------- Class initialization ---------- */

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
