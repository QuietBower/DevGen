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

#define TYPE_PCIBASE_DEVICE "pata_sil680_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_CMD		0x1095
#define PCI_DEVICE_ID_SII_680		0x0680

#define SIL680_MMIO_BAR 5

#define SIL680_REG_80 0x80
#define SIL680_REG_84 0x84
#define SIL680_REG_8A 0x8A
#define SIL680_REG_A1 0xA1
#define SIL680_REG_A2 0xA2
#define SIL680_REG_A4 0xA4
#define SIL680_REG_A8 0xA8
#define SIL680_REG_AC 0xAC
#define SIL680_REG_B1 0xB1
#define SIL680_REG_B2 0xB2
#define SIL680_REG_B4 0xB4
#define SIL680_REG_B8 0xB8
#define SIL680_REG_BC 0xBC

#define SIL680_MMIO_PORT0_BMDMA 0x00
#define SIL680_MMIO_PORT0_CMD   0x80
#define SIL680_MMIO_PORT0_CTL   0x8A
#define SIL680_MMIO_PORT1_BMDMA 0x08
#define SIL680_MMIO_PORT1_CMD   0xC0
#define SIL680_MMIO_PORT1_CTL   0xCA

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

    /* Shadow registers for MMIO BAR */
    uint8_t mmio_regs[0x100];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size <= sizeof(s->mmio_regs)) {
        for (unsigned i = 0; i < size; i++) {
            val |= ((uint64_t)s->mmio_regs[addr + i]) << (i * 8);
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size <= sizeof(s->mmio_regs)) {
        for (unsigned i = 0; i < size; i++) {
            s->mmio_regs[addr + i] = (val >> (i * 8)) & 0xFF;
        }
    }
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    
    pci_device_reset(pdev);
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));

    /* Re-apply default config space values that might be cleared by pci_device_reset */
    pci_set_byte(pdev->config + SIL680_REG_8A, 0x11); /* BA5_EN = 1, clock = 133MHz (0x10) */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CMD );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SII_680 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE );
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8f); /* Native mode, bus master */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize driver-specific config registers */
    pci_set_byte(pci_conf + SIL680_REG_8A, 0x11); /* BA5_EN = 1, clock = 133MHz (0x10) */

    /* Make driver-accessed config registers writable */
    pci_set_byte(pdev->wmask + PCI_CACHE_LINE_SIZE, 0xff);
    pci_set_byte(pdev->wmask + SIL680_REG_80, 0xff);
    pci_set_byte(pdev->wmask + SIL680_REG_84, 0xff);
    pci_set_byte(pdev->wmask + SIL680_REG_8A, 0xff);
    
    /* Port 0 timing/control */
    pci_set_byte(pdev->wmask + 0xA0, 0xff);
    pci_set_byte(pdev->wmask + SIL680_REG_A1, 0xff);
    pci_set_word(pdev->wmask + SIL680_REG_A2, 0xffff);
    pci_set_long(pdev->wmask + SIL680_REG_A4, 0xffffffff);
    pci_set_long(pdev->wmask + SIL680_REG_A8, 0xffffffff);
    pci_set_long(pdev->wmask + SIL680_REG_AC, 0xffffffff);

    /* Port 1 timing/control */
    pci_set_byte(pdev->wmask + 0xB0, 0xff);
    pci_set_byte(pdev->wmask + SIL680_REG_B1, 0xff);
    pci_set_word(pdev->wmask + SIL680_REG_B2, 0xffff);
    pci_set_long(pdev->wmask + SIL680_REG_B4, 0xffffffff);
    pci_set_long(pdev->wmask + SIL680_REG_B8, 0xffffffff);
    pci_set_long(pdev->wmask + SIL680_REG_BC, 0xffffffff);

    /* 
     * REMOVED PCIe capability initialization.
     * The Silicon Image 680 is a conventional PCI IDE controller.
     * Exposing it as a PCIe device causes modern guest firmwares (like OVMF)
     * to skip allocating IO space for it, which leads to "BAR 0 not assigned"
     * errors during driver probe.
     */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 6;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "sil680.pio0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 4;
    s->bar_info[1].name = "sil680.pio1";

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 8;
    s->bar_info[2].name = "sil680.pio2";

    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = 4;
    s->bar_info[3].name = "sil680.pio3";

    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_PIO;
    s->bar_info[4].size = 16;
    s->bar_info[4].name = "sil680.bmdma";

    s->bar_info[5].index = SIL680_MMIO_BAR;
    s->bar_info[5].type = BAR_TYPE_MMIO;
    s->bar_info[5].size = 0x100;
    s->bar_info[5].name = "sil680.mmio";

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
    .name = "pata_sil680_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(mmio_regs, PCIBaseState, 0x100),
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
