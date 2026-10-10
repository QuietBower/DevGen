/*
 * QEMU PCI device model for Linux comedi 8255_pci driver
 * Target QEMU: 8.2.x
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "8255_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_NI 0x1093
#define PCI_VENDOR_ID_CB 0x1307
#define PCI_CLASS_OTHERS 0xff
#define MITE_IODWBSR 0xc0
#define WENAB (1U << 7)
#define I8255_SIZE 0x04

/* Minimal definitions to satisfy compiler, based on Linux driver's usage */
struct pci_device_id {
    uint32_t vendor;
    uint32_t device;
    uint32_t subvendor;
    uint32_t subdevice;
    uint32_t class_;
    uint32_t class_mask;
    unsigned long driver_data;
};

#define PCI_ANY_ID 0xffffffff

#define PCI_DEVICE_SUB(vend, dev, svend, sdev) \
    .vendor = (vend), .device = (dev), .subvendor = (svend), \
    .subdevice = (sdev), .class_ = 0, .class_mask = 0

#define PCI_VDEVICE(vendor_prefix, dev) \
    .vendor = PCI_VENDOR_ID_ ## vendor_prefix, .device = (dev), \
    .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, \
    .class_ = 0, .class_mask = 0

enum pci_8255_boardid {
#ifdef CONFIG_HAS_IOPORT
    BOARD_ADLINK_PCI7224,
    BOARD_ADLINK_PCI7248,
    BOARD_ADLINK_PCI7296,
    BOARD_CB_PCIDIO24,
    BOARD_CB_PCIDIO24H,
    BOARD_CB_PCIDIO48H_OLD,
    BOARD_CB_PCIDIO48H_NEW,
    BOARD_CB_PCIDIO96H,
#endif /* CONFIG_HAS_IOPORT */
    BOARD_NI_PCIDIO96,
    BOARD_NI_PCIDIO96B,
    BOARD_NI_PXI6508,
    BOARD_NI_PCI6503,
    BOARD_NI_PCI6503B,
    BOARD_NI_PCI6503X,
    BOARD_NI_PXI_6503,
};

struct pci_8255_boardinfo {
    const char *name;
    int dio_badr;
    int n_8255;
    unsigned int has_mite:1;
};

/* Board info table kept for reference; currently unused in this model. */
static const struct pci_8255_boardinfo pci_8255_boards[] __attribute__((unused)) = {
#ifdef CONFIG_HAS_IOPORT
    [BOARD_ADLINK_PCI7224] = {
        .name      = "adl_pci-7224",
        .dio_badr  = 2,
        .n_8255    = 1,
    },
    [BOARD_ADLINK_PCI7248] = {
        .name      = "adl_pci-7248",
        .dio_badr  = 2,
        .n_8255    = 2,
    },
    [BOARD_ADLINK_PCI7296] = {
        .name      = "adl_pci-7296",
        .dio_badr  = 2,
        .n_8255    = 4,
    },
    [BOARD_CB_PCIDIO24] = {
        .name      = "cb_pci-dio24",
        .dio_badr  = 2,
        .n_8255    = 1,
    },
    [BOARD_CB_PCIDIO24H] = {
        .name      = "cb_pci-dio24h",
        .dio_badr  = 2,
        .n_8255    = 1,
    },
    [BOARD_CB_PCIDIO48H_OLD] = {
        .name      = "cb_pci-dio48h",
        .dio_badr  = 1,
        .n_8255    = 2,
    },
    [BOARD_CB_PCIDIO48H_NEW] = {
        .name      = "cb_pci-dio48h",
        .dio_badr  = 2,
        .n_8255    = 2,
    },
    [BOARD_CB_PCIDIO96H] = {
        .name      = "cb_pci-dio96h",
        .dio_badr  = 2,
        .n_8255    = 4,
    },
#endif /* CONFIG_HAS_IOPORT */
    [BOARD_NI_PCIDIO96] = {
        .name      = "ni_pci-dio-96",
        .dio_badr  = 1,
        .n_8255    = 4,
        .has_mite  = 1,
    },
    [BOARD_NI_PCIDIO96B] = {
        .name      = "ni_pci-dio-96b",
        .dio_badr  = 1,
        .n_8255    = 4,
        .has_mite  = 1,
    },
    [BOARD_NI_PXI6508] = {
        .name      = "ni_pxi-6508",
        .dio_badr  = 1,
        .n_8255    = 4,
        .has_mite  = 1,
    },
    [BOARD_NI_PCI6503] = {
        .name      = "ni_pci-6503",
        .dio_badr  = 1,
        .n_8255    = 1,
        .has_mite  = 1,
    },
    [BOARD_NI_PCI6503B] = {
        .name      = "ni_pci-6503b",
        .dio_badr  = 1,
        .n_8255    = 1,
        .has_mite  = 1,
    },
    [BOARD_NI_PCI6503X] = {
        .name      = "ni_pci-6503x",
        .dio_badr  = 1,
        .n_8255    = 1,
        .has_mite  = 1,
    },
    [BOARD_NI_PXI_6503] = {
        .name      = "ni_pxi-6503",
        .dio_badr  = 1,
        .n_8255    = 1,
        .has_mite  = 1,
    },
};

/* The Linux driver's pci_device_id table is not used directly by QEMU. */

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Simple 8255 emulation state: up to 4 chips (for NI_pci-dio-96) */
    uint8_t ppi_port[4][3]; /* ports A,B,C for each 8255 */
    uint8_t ppi_ctrl[4];    /* control word per 8255 */
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                     */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                               */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* 8255 core helpers (simple, based only on I/O map from driver)      */
/* ------------------------------------------------------------------ */

static uint8_t pci_8255_port_read(PCIBaseState *s, unsigned chip, unsigned port)
{
    if (chip >= 4 || port >= 3) {
        return 0xff;
    }
    return s->ppi_port[chip][port];
}

static void pci_8255_port_write(PCIBaseState *s, unsigned chip, unsigned port, uint8_t val)
{
    if (chip >= 4 || port >= 3) {
        return;
    }
    s->ppi_port[chip][port] = val;
}

static uint8_t pci_8255_ctrl_read(PCIBaseState *s, unsigned chip)
{
    if (chip >= 4) {
        return 0x9b; /* typical power-on, but not specified; arbitrary */
    }
    return s->ppi_ctrl[chip];
}

static void pci_8255_ctrl_write(PCIBaseState *s, unsigned chip, uint8_t val)
{
    if (chip >= 4) {
        return;
    }
    s->ppi_ctrl[chip] = val;
}

/* Map offset to chip/port/index: driver uses subdev_8255_*_init(dev, s, i * I8255_SIZE).
 * Each 8255 chip occupies I8255_SIZE bytes: 3 ports + 1 control.
 */
static bool pci_8255_decode_offset(hwaddr addr, unsigned *chip, unsigned *is_ctrl, unsigned *port)
{
    unsigned idx = addr / I8255_SIZE; /* chip index */
    unsigned off = addr % I8255_SIZE; /* 0..3 */

    if (idx >= 4) {
        return false;
    }
    *chip = idx;
    if (off < 3) {
        *is_ctrl = 0;
        *port = off; /* 0=A,1=B,2=C */
    } else {
        *is_ctrl = 1;
        *port = 0;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned chip, is_ctrl, port;

    if (size != 1) {
        /* subdev_8255_mm_init uses byte accesses */
        return 0xff;
    }

    if (!pci_8255_decode_offset(addr, &chip, &is_ctrl, &port)) {
        return 0xff;
    }

    if (is_ctrl) {
        return pci_8255_ctrl_read(s, chip);
    }
    return pci_8255_port_read(s, chip, port);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned chip, is_ctrl, port;

    if (size != 1) {
        return;
    }

    if (!pci_8255_decode_offset(addr, &chip, &is_ctrl, &port)) {
        return;
    }

    if (is_ctrl) {
        pci_8255_ctrl_write(s, chip, (uint8_t)val);
    } else {
        pci_8255_port_write(s, chip, port, (uint8_t)val);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned chip, is_ctrl, port;

    if (size != 1) {
        return 0xff;
    }

    if (!pci_8255_decode_offset(addr, &chip, &is_ctrl, &port)) {
        return 0xff;
    }

    if (is_ctrl) {
        return pci_8255_ctrl_read(s, chip);
    }
    return pci_8255_port_read(s, chip, port);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned chip, is_ctrl, port;

    if (size != 1) {
        return;
    }

    if (!pci_8255_decode_offset(addr, &chip, &is_ctrl, &port)) {
        return;
    }

    if (is_ctrl) {
        pci_8255_ctrl_write(s, chip, (uint8_t)val);
    } else {
        pci_8255_port_write(s, chip, port, (uint8_t)val);
    }
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    int i, j;

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    /* Clear 8255 state */
    for (i = 0; i < 4; i++) {
        s->ppi_ctrl[i] = 0x9b; /* arbitrary default, not specified by driver */
        for (j = 0; j < 3; j++) {
            s->ppi_port[i][j] = 0x00;
        }
    }
}

/* ------------------------------------------------------------------ */
/* DMA initialize (not used by driver)                               */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                            */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                              */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int i;

    /* Use first entry NI 0x0160 (BOARD_NI_PCIDIO96) */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x144a);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x7224);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR layout implied by driver for NI boards:
     * BAR0: MITE registers (memory, 256 bytes)
     * BAR1: 8255 I/O space, used as MMIO via dev->mmio, 4 * I8255_SIZE bytes
     */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x100; /* MITE, driver only writes at offset 0xC0 */
    s->bar_info[0].name  = "mite-bar0";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 4 * I8255_SIZE; /* up to 4 8255 chips */
    s->bar_info[1].name  = "dio-bar1";
    s->bar_info[1].sparse = false;

    for (i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* No MSI/MSI-X usage visible in driver; leave disabled. */

    /* Initialize 8255 state */
    pcibase_reset(DEVICE(pdev));
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                     */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                     */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

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

