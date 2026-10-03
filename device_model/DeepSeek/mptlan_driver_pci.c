/*
 * QEMU model of LSI Fusion-MPT LAN controller (mptlan)
 *
 * Copyright (c) 2024
 * Licensed under the MIT license.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/pci_regs.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "pci-mptlan"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Typedefs introduced by Fusion-MPT headers */
typedef uint8_t U8;
typedef uint64_t U64;

/* Firmware Version structure from mptlan.h */
typedef struct _MPI_FW_VERSION_STRUCT
{
    U8                      Dev;                        /* 00h */
    U8                      Unit;                       /* 01h */
    U8                      Minor;                      /* 02h */
    U8                      Major;                      /* 03h */
} MPI_FW_VERSION_STRUCT;

/* Doorbell offset in MMIO BAR */
#define DOORBELL_OFFSET 0x00

/* BAR information container */
typedef struct BARInfo {
    int bar_index;
    int type;
    uint64_t size;
    int memtype;
    MemoryRegion mr;
} BARInfo;

/* Device state */
struct PCIBaseState {
    PCIDevice pdev;
    MemoryRegion mmio;
    MemoryRegion pio;
    bool reply_pending;
    qemu_irq irq;
    BARInfo bar_info[6];
};

/* Forward declaration of Ops */
static const MemoryRegionOps pcibase_mmio_ops;
static const MemoryRegionOps pcibase_pio_ops;

/* PCI vendor/device IDs (LSI53C1030) */
#define PCI_VENDOR_ID_LSI_LOGIC 0x1000
#define PCI_DEVICE_ID_LSI_53C1030 0x0030

static void pcibase_update_irq(PCIBaseState *s)
{
    pci_set_irq(PCI_DEVICE(s), s->reply_pending);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    (void)s; /* unused for now */

    switch (addr) {
    case DOORBELL_OFFSET:
        /* Doorbell is write-only, ignore read */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from doorbell\n", __func__);
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown MMIO read at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case DOORBELL_OFFSET:
        /* Doorbell write triggers a reply */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: doorbell write 0x%" PRIx64 " (processing not implemented)\n",
                      __func__, val);
        s->reply_pending = true;
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown MMIO write at 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n",
                      __func__, addr, val);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s; /* unused for now */

    qemu_log_mask(LOG_GUEST_ERROR, "%s: PIO read at 0x%" HWADDR_PRIx " size %u\n",
                  __func__, addr, size);
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s; /* unused for now */

    qemu_log_mask(LOG_GUEST_ERROR, "%s: PIO write at 0x%" HWADDR_PRIx " val 0x%" PRIx64 " size %u\n",
                  __func__, addr, val, size);
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    /* Choose ops based on bar type */
    const MemoryRegionOps *ops;
    if (bi->type == PCI_BASE_ADDRESS_SPACE_MEMORY) {
        ops = &pcibase_mmio_ops;
    } else if (bi->type == PCI_BASE_ADDRESS_SPACE_IO) {
        ops = &pcibase_pio_ops;
    } else {
        error_setg(errp, "Unknown BAR type %d", bi->type);
        return;
    }

    memory_region_init_io(&bi->mr, OBJECT(pdev), ops, s, "pcibase", bi->size);
    pci_register_bar(pdev, bi->bar_index, bi->type, &bi->mr);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Initialize BARs */
    s->bar_info[0] = (BARInfo) {
        .bar_index = 0,
        .type = PCI_BASE_ADDRESS_SPACE_MEMORY,
        .size = 0x4000,
        .memtype = PCI_BASE_ADDRESS_MEM_TYPE_32,
    };
    s->bar_info[1] = (BARInfo) {
        .bar_index = 1,
        .type = PCI_BASE_ADDRESS_SPACE_IO,
        .size = 256,
        .memtype = 0, /* Not used for IO BAR */
    };

    for (int i = 0; i < 2; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (*errp) {
            return;
        }
    }

    /* Initialize interrupt */
    s->irq = pci_allocate_irq(pdev);
    s->reply_pending = false;

    /* Enable MSI if available */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
}

static void pcibase_uninit(DeviceState *dev)
{
    PCIDevice *pdev = PCI_DEVICE(dev);
    msi_uninit(pdev);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    dc->unrealize = pcibase_uninit;
    k->realize = pcibase_realize;
    k->vendor_id = PCI_VENDOR_ID_LSI_LOGIC;
    k->device_id = PCI_DEVICE_ID_LSI_53C1030;
    k->revision = 1;
    k->class_id = 0x0100; /* SCSI controller */
    k->subsystem_vendor_id = PCI_VENDOR_ID_LSI_LOGIC;
    k->subsystem_id = PCI_DEVICE_ID_LSI_53C1030;
    dc->desc = "LSI53C1030 Fusion-MPT LAN controller";
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)
