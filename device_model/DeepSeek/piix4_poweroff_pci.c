/*
 * QEMU PIIX4 Poweroff PCI Device Model
 * Derived from Linux driver: drivers/power/reset/piix4-poweroff.c
 *
 * This device emulates the Power Management I/O registers of the
 * Intel PIIX4 (82371AB/EB) southbridge, specifically the PM I/O space
 * at function 3. It is designed to satisfy the piix4-poweroff driver's
 * probe requirements and provide the expected I/O port behavior.
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

#ifndef BIT
#define BIT(n) (1U << (n))
#endif

#define TYPE_PCIBASE_DEVICE "piix4_poweroff_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_82371AB_3 0x7113
#define PCI_CLASS_ACPI 0x068000

/* Register offsets */
enum piix4_pm_io_reg {
    PIIX4_FUNC3IO_PMSTS = 0x00,
#define PIIX4_FUNC3IO_PMSTS_PWRBTN_STS BIT(8)
    PIIX4_FUNC3IO_PMCNTRL = 0x04,
#define PIIX4_FUNC3IO_PMCNTRL_SUS_EN BIT(13)
#define PIIX4_FUNC3IO_PMCNTRL_SUS_TYP_SOFF (0x0 << 10)
};
#define PIIX4_SUSPEND_MAGIC 0x00120002

/* PM I/O space size (typical PIIX4 PM block is 64 bytes) */
#define PIIX4_PM_IO_SIZE 0x40

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

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows */
    struct {
        uint16_t pmsts;
        uint16_t pmcntrl;
    } regs;
};

/* Forward declarations to avoid compile errors */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

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

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No MMIO registers implemented */
    (void)s; /* suppress unused variable warning */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO registers implemented */
    (void)s; /* suppress unused variable warning */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 2) {
        qemu_log_mask(LOG_UNIMP, "PIIX4 PM: unsupported PIO read size %d at 0x"
                      HWADDR_FMT_plx "\n", size, addr);
        return 0;
    }

    switch (addr) {
    case PIIX4_FUNC3IO_PMSTS:
        val = s->regs.pmsts;
        break;
    case PIIX4_FUNC3IO_PMCNTRL:
        val = s->regs.pmcntrl;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "PIIX4 PM: unknown PIO read at 0x"
                      HWADDR_FMT_plx "\n", addr);
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 2) {
        qemu_log_mask(LOG_UNIMP, "PIIX4 PM: unsupported PIO write size %d at 0x"
                      HWADDR_FMT_plx "\n", size, addr);
        return;
    }

    switch (addr) {
    case PIIX4_FUNC3IO_PMSTS:
        /* W1C (Write-1-to-Clear) for PWRBTN_STS */
        if (val & PIIX4_FUNC3IO_PMSTS_PWRBTN_STS) {
            s->regs.pmsts &= ~PIIX4_FUNC3IO_PMSTS_PWRBTN_STS;
        }
        break;
    case PIIX4_FUNC3IO_PMCNTRL:
        s->regs.pmcntrl = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "PIIX4 PM: unknown PIO write at 0x"
                      HWADDR_FMT_plx "\n", addr);
        break;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->regs.pmsts = 0;
    s->regs.pmcntrl = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x7113);
    pci_config_set_class(pci_conf, 0x068000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Enable I/O and bus mastering in the command register to ensure resources are active */
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MASTER);

    /* Set subsystem IDs to the same as vendor/device for driver matching consistency */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x7113);

    /* BAR Initialization */
    s->num_bars = 0;
    s->bar_info[s->num_bars].index = 0;
    s->bar_info[s->num_bars].type = BAR_TYPE_PIO;
    s->bar_info[s->num_bars].size = PIIX4_PM_IO_SIZE;
    s->bar_info[s->num_bars].name = "piix4-pm-io";
    s->num_bars++;

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
    .name = "piix4_poweroff_pci",
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
