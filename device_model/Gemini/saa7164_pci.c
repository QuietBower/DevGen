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

#define TYPE_PCIBASE_DEVICE "saa7164_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SAA7164_VENDOR_ID 0x1131
#define SAA7164_DEVICE_ID 0x7164

#define SAA_DEVICE_VERSION 0x30
#define SAA_DOWNLOAD_FLAGS 0x34
#define SAA_BOOTLOADERERROR_FLAGS 0x44
#define SAA_DEVICE_2ND_VERSION 0x50
#define SAA_DEVICE_2ND_DOWNLOADFLAG_OFFSET 0x54
#define SAA_SECONDSTAGEERROR_FLAGS 0x64
#define SAA_DEVICE_DEADLOCK_DETECTED_OFFSET 0x6C
#define SAA_DEVICE_SYSINIT_STATUS 0x70
#define SAA_DEVICE_SYSINIT_MODE 0x74
#define SAA_DEVICE_SYSINIT_SPEC 0x78
#define SAA_DEVICE_SYSINIT_INST 0x7C
#define SAA_DEVICE_SYSINIT_CPULOAD 0x80
#define SAA_DEVICE_SYSINIT_REMAINHEAP 0x84

/* Interrupt and ack register locations offset of bmmio */
#define SAA_INT_STATUS_OFFSET 0x183f80
#define SAA_INT_ACK_OFFSET 0x183f90

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
    uint32_t int_status[4];

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* 2MB BAR size requires 0x200000 bytes, which is 524288 uint32_t elements */
    uint32_t regs[0x200000 / 4];

    /* DMA Context */
    uint64_t pt_dma;
    uint32_t pt_size;

    uint32_t firmwareloaded;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->int_status[0] != 0 || s->int_status[1] != 0 || 
                  s->int_status[2] != 0 || s->int_status[3] != 0);

    if (s->has_msi) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* MMIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= SAA_INT_STATUS_OFFSET && addr < SAA_INT_STATUS_OFFSET + 16) {
        int idx = (addr - SAA_INT_STATUS_OFFSET) / 4;
        val = s->int_status[idx];
    } else if (addr < sizeof(s->regs)) {
        val = s->regs[addr / 4];
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= SAA_INT_ACK_OFFSET && addr < SAA_INT_ACK_OFFSET + 16) {
        int idx = (addr - SAA_INT_ACK_OFFSET) / 4;
        s->int_status[idx] &= ~val; /* W1C logic */
        pcibase_update_irq(s);
    } else if (addr < sizeof(s->regs)) {
        s->regs[addr / 4] = val;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->int_status, 0, sizeof(s->int_status));
    memset(s->regs, 0, sizeof(s->regs));

    /* Default values to allow probe to progress */
    s->regs[SAA_DEVICE_VERSION / 4] = 0x00000000;
    s->regs[SAA_DEVICE_SYSINIT_STATUS / 4] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  SAA7164_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SAA7164_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0400 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    /* BAR sizes set to 2MB to cover the 0x183f90 offset accessed by the driver */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 2 * MiB, .name = "saa7164-bar0" };
    s->bar_info[1] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 2 * MiB, .name = "saa7164-bar2" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
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
    .name = "saa7164_pci",
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
