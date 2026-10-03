/*
 * QEMU device model for Conexant CX8802 MPEG controller
 * Based on driver cx88-mpeg.c
 * Phase 2: Functional implementation with MMIO and IRQ handling
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

#define TYPE_PCIBASE_DEVICE "cx88_mpeg_driver_manager_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define VENDOR_ID 0x14f1
#define DEVICE_ID 0x8802
#define CLASS_ID  0x0400  /* Multimedia Video */

/* Register Offsets (byte offsets within BAR0) */
#define MO_PCI_INTMSK       0x200040
#define MO_PCI_INTSTAT      0x200044
#define MO_TS_INTMSK        0x200070
#define MO_TS_INTSTAT       0x200074
#define MO_TS_DMACNTRL      0x33C040
#define MO_TS_GPCNTRL       0x33C030
#define MO_TS_GPCNT         0x33C020

/* Define BAR types */
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
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Flat memory backing the entire 4 MB MMIO region */
    uint8_t mmio_ram[4 * MiB];

    bool dma_running;   /* derived from TS_DMACNTRL */
    bool reset_active;
};

static inline void pcibase_write_ram(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    uint8_t *p = &s->mmio_ram[addr];
    switch (size) {
    case 1:
        *p = (uint8_t)val;
        break;
    case 2:
        stw_le_p(p, (uint16_t)val);
        break;
    case 4:
        stl_le_p(p, (uint32_t)val);
        break;
    case 8:
        stq_le_p(p, val);
        break;
    }
}

static inline uint64_t pcibase_read_ram(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint8_t *p = &s->mmio_ram[addr];
    switch (size) {
    case 1:
        return *p;
    case 2:
        return lduw_le_p(p);
    case 4:
        return ldl_le_p(p);
    case 8:
        return ldq_le_p(p);
    }
    return 0;
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pci_stat = pcibase_read_ram(s, MO_PCI_INTSTAT, 4) &
                        pcibase_read_ram(s, MO_PCI_INTMSK, 4);
    uint32_t ts_stat  = pcibase_read_ram(s, MO_TS_INTSTAT, 4) &
                        pcibase_read_ram(s, MO_TS_INTMSK, 4);
    bool raise = (pci_stat != 0) || (ts_stat != 0);

    if (raise) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Placeholder: DMA not needed for basic probe; driver sets up RISC later. */
    qemu_log_mask(LOG_UNIMP, "cx88-mpeg: DMA not implemented\n");
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return pcibase_read_ram(s, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Write value directly to backing store */
    pcibase_write_ram(s, addr, val, size);

    /* Handle side effects for special registers */
    switch (addr) {
    case MO_TS_DMACNTRL:
        s->dma_running = (val & 0x11) ? true : false;
        break;
    case MO_TS_GPCNTRL:
        if (val == 0x0000) {  /* GP_COUNT_CONTROL_RESET */
            pcibase_write_ram(s, MO_TS_GPCNT, 0, 4);
        }
        break;
    case MO_PCI_INTMSK:
    case MO_TS_INTMSK:
        pcibase_update_irq(s);
        break;
    case MO_PCI_INTSTAT:
    case MO_TS_INTSTAT:
        {
            /* Write-1-to-clear semantics */
            uint32_t old_val = pcibase_read_ram(s, addr, 4);
            uint32_t new_val = old_val & ~((uint32_t)val);
            pcibase_write_ram(s, addr, new_val, 4);
            pcibase_update_irq(s);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return 0;
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
    memset(s->mmio_ram, 0, sizeof(s->mmio_ram));
    s->dma_running = false;
    s->reset_active = false;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0: MMIO region covering all registers (size up to 4MB) */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 4 * MiB, .name = "mmio" };
    s->num_bars = 1;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X needed as driver uses legacy IRQ */
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
    .name = "cx88_mpeg_driver_manager_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_BUFFER(mmio_ram, PCIBaseState),
        VMSTATE_BOOL(dma_running, PCIBaseState),
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
