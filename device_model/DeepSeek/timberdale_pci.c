/*
 * QEMU model of Timberdale PCIe FPGA device
 * Derived from Linux driver drivers/mfd/timberdale.c.
 *
 * Copyright (c) 2024 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
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

#define TYPE_PCIBASE_DEVICE "timberdale_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_TIMB  0x10ee
#define PCI_DEVICE_ID_TIMB  0xa123
#define PCI_CLASS_ID        PCI_CLASS_OTHERS   /* will be refined if exact class known */

/* Chip control register offsets (relative to BAR0 control region) */
#define TIMB_REV_MAJOR      0x00
#define TIMB_REV_MINOR      0x04
#define TIMB_HW_CONFIG      0x08
#define TIMB_SW_RST         0x40

#define TIMB_HW_VER0        0x00
#define TIMB_HW_VER1        0x01
#define TIMB_HW_VER2        0x02
#define TIMB_HW_VER3        0x03
#define TIMB_HW_VER_MASK    0x0f
#define TIMB_REQUIRED_MINOR 8
#define TIMB_SUPPORTED_MAJOR 3

#define TIMB_HW_CONFIG_SPI_8BIT  0x80

/* BAR sizes (placeholder until provided by hardware specs) */
#ifndef BAR0_SIZE
#define BAR0_SIZE  0x2000000   /* 32 MiB, sufficient for control region at offset 0x800 */
#endif
#ifndef BAR1_SIZE
#define BAR1_SIZE  0x100
#endif
#ifndef BAR2_SIZE
#define BAR2_SIZE  0x100
#endif
#ifndef BAR4_SIZE
#define BAR4_SIZE  0x1000      /* 4 KB for MSI-X table */
#endif

/* MSI-X vector count as requested by driver; will be taken from TIMBERDALE_NR_IRQS */
#ifndef TIMBERDALE_MSIX_VECTORS
#define TIMBERDALE_MSIX_VECTORS  16
#endif

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

#define CHIPCTLOFFSET 0x800
#define CHIPCTLEND    0x8ff
#define CHIPCTLSIZE   (CHIPCTLEND - CHIPCTLOFFSET + 1)

struct TimberdaleCtrl {
    uint8_t regs[CHIPCTLSIZE]; /* raw copy of chip control registers */
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Hardware Register Shadows */
    struct TimberdaleCtrl chipctl;
};

/* Return default values for the firmware version and hardware config */
#define DEFAULT_MAJOR  TIMB_SUPPORTED_MAJOR    /* 3 */
#define DEFAULT_MINOR  TIMB_REQUIRED_MINOR    /* 8 */
#define DEFAULT_CONFIG (TIMB_HW_VER0)          /* version 0, SPI 16-bit */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Handle chip control region (offset 0x800 - 0x8FF) */
    if (addr >= CHIPCTLOFFSET && addr < CHIPCTLOFFSET + CHIPCTLSIZE) {
        hwaddr local = addr - CHIPCTLOFFSET;
        /* Only 32-bit accesses are used by the driver, support unaligned? */
        if (size == 4 && (local & 3) == 0) {
            switch (local) {
            case TIMB_REV_MAJOR:
                val = DEFAULT_MAJOR;
                break;
            case TIMB_REV_MINOR:
                val = DEFAULT_MINOR;
                break;
            case TIMB_HW_CONFIG:
                val = DEFAULT_CONFIG;
                break;
            default:
                /* For other registers, return the stored value */
                memcpy(&val, &s->chipctl.regs[local], sizeof(val));
                break;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "pcibase_mmio_read: unexpected access size %u at addr 0x%" HWADDR_PRIx "\n",
                          size, addr);
        }
        return val;
    }

    /* Log access to unimplemented regions */
    qemu_log_mask(LOG_UNIMP,
                  "pcibase_mmio_read: unimplemented access at addr 0x%" HWADDR_PRIx ", size %u\n",
                  addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle chip control region */
    if (addr >= CHIPCTLOFFSET && addr < CHIPCTLOFFSET + CHIPCTLSIZE) {
        hwaddr local = addr - CHIPCTLOFFSET;
        if (size == 4 && (local & 3) == 0) {
            switch (local) {
            case TIMB_SW_RST:
                /* Software reset: driver writes 0x1 to reset FPGA peripherals.
                 * We ignore the actual reset mechanism as it does not affect the
                 * success of probe. */
                qemu_log_mask(LOG_GUEST_ERROR,
                              "pcibase_mmio_write: SW reset written (0x%" PRIx64 "), ignored\n", val);
                /* Store the value for completeness */
                memcpy(&s->chipctl.regs[local], &val, sizeof(val));
                break;
            default:
                /* For other writable registers, store the value */
                memcpy(&s->chipctl.regs[local], &val, sizeof(val));
                break;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "pcibase_mmio_write: unexpected access size %u at addr 0x%" HWADDR_PRIx "\n",
                          size, addr);
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP,
                  "pcibase_mmio_write: unimplemented access at addr 0x%" HWADDR_PRIx ", size %u, val 0x%" PRIx64 "\n",
                  addr, size, val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "pcibase_pio_read: unimplemented at addr 0x%" HWADDR_PRIx "\n", addr);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "pcibase_pio_write: unimplemented at addr 0x%" HWADDR_PRIx "\n", addr);
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
    /* Clear chip control registers to zero, then set default version/config */
    memset(s->chipctl.regs, 0, CHIPCTLSIZE);
    /* Pre-configure version registers for a successful probe */
    uint32_t major_val = DEFAULT_MAJOR;
    uint32_t minor_val = DEFAULT_MINOR;
    uint32_t config_val = DEFAULT_CONFIG;
    memcpy(&s->chipctl.regs[TIMB_REV_MAJOR], &major_val, sizeof(major_val));
    memcpy(&s->chipctl.regs[TIMB_REV_MINOR], &minor_val, sizeof(minor_val));
    memcpy(&s->chipctl.regs[TIMB_HW_CONFIG], &config_val, sizeof(config_val));
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TIMB );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_TIMB );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization (BAR0, BAR1, BAR2) */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){.index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "bar0" };
    s->bar_info[1] = (BARInfo){.index = 1, .type = BAR_TYPE_MMIO, .size = BAR1_SIZE, .name = "bar1" };
    s->bar_info[2] = (BARInfo){.index = 2, .type = BAR_TYPE_MMIO, .size = BAR2_SIZE, .name = "bar2" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization: driver expects TIMBERDALE_NR_IRQS vectors.
     * We place the MSI-X table in BAR4. */
    BARInfo msix_bar = { .index = 4, .type = BAR_TYPE_MMIO, .size = BAR4_SIZE, .name = "msix-bar" };
    pcibase_register_bar(pdev, s, &msix_bar, errp);

    if (msix_init(pdev, TIMBERDALE_MSIX_VECTORS,
                  &s->bar_regions[4], 4, 0,
                  &s->bar_regions[4], 4, 0x200,
                  0, errp)) {
        /* Failed to initialize MSI-X; the error will be propagated */
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "timberdale_pci",
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
