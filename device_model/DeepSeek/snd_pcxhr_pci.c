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

#define TYPE_PCIBASE_DEVICE "snd_pcxhr_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID       0x10b5
#define DEVICE_ID       0x9656
#define CLASS_ID        PCI_CLASS_MULTIMEDIA_AUDIO

/* Register offsets from driver */
#define IO_NUM_REG_CONT         0
#define IO_NUM_REG_GENCLK       1
#define IO_NUM_REG_MUTE_OUT     2
#define IO_NUM_SPEED_RATIO      4
#define IO_NUM_REG_STATUS       5
#define REG_STATUS_OPTIONS      0
#define REG_STATUS_AES_SYNC     8
#define REG_STATUS_AES_1        9
#define REG_STATUS_AES_2        10
#define REG_STATUS_AES_3        11
#define REG_STATUS_AES_4        12
#define REG_STATUS_WORD_CLOCK   13
#define IO_NUM_REG_IN_ANA_LEVEL 21
#define IO_NUM_REG_OUT_ANA_LEVEL 20

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

typedef struct {
    PCIBaseState *s;
    int bar_idx;
} PCIBaseBarOpaque;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    PCIBaseBarOpaque bar_opaque[6];

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x100];           /* MMIO register file */
    uint32_t plx_regs[0x100];       /* PLX PIO registers (BAR1) */
    uint32_t mbox_regs[0x100];      /* Mailbox PIO registers (BAR2) */

    /* Operational status flags */
    uint32_t hw_status;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* #IRQ_Update_Logic# */
    /* Simple IRQ signaling based on interrupt status */
    if (s->intr_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* #MMIO_Read_Func# */
    if (addr < sizeof(s->regs)) {
        unsigned int idx = addr / sizeof(uint32_t);
        if (idx < ARRAY_SIZE(s->regs)) {
            val = s->regs[idx];
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* #MMIO_Write_Func# */
    if (addr < sizeof(s->regs)) {
        unsigned int idx = addr / sizeof(uint32_t);
        if (idx < ARRAY_SIZE(s->regs)) {
            s->regs[idx] = val;
        }
    }
}

/* PLX PIO handlers (BAR1) */
static uint64_t pcibase_plx_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseBarOpaque *bo = opaque;
    PCIBaseState *s = bo->s;
    uint64_t val = 0;

    if (addr < sizeof(s->plx_regs)) {
        unsigned int idx = addr / sizeof(uint32_t);
        if (idx < ARRAY_SIZE(s->plx_regs)) {
            val = s->plx_regs[idx];
        }
    }
    return val;
}

static void pcibase_plx_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseBarOpaque *bo = opaque;
    PCIBaseState *s = bo->s;

    if (addr < sizeof(s->plx_regs)) {
        unsigned int idx = addr / sizeof(uint32_t);
        if (idx < ARRAY_SIZE(s->plx_regs)) {
            s->plx_regs[idx] = val;
        }
    }
}

/* Mailbox PIO handlers (BAR2) */
static uint64_t pcibase_mbox_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseBarOpaque *bo = opaque;
    PCIBaseState *s = bo->s;
    uint64_t val = 0;

    if (addr < sizeof(s->mbox_regs)) {
        unsigned int idx = addr / sizeof(uint32_t);
        if (idx < ARRAY_SIZE(s->mbox_regs)) {
            val = s->mbox_regs[idx];
        }
    }
    return val;
}

static void pcibase_mbox_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseBarOpaque *bo = opaque;
    PCIBaseState *s = bo->s;

    if (addr < sizeof(s->mbox_regs)) {
        unsigned int idx = addr / sizeof(uint32_t);
        if (idx < ARRAY_SIZE(s->mbox_regs)) {
            s->mbox_regs[idx] = val;
        }
    }
}

/* Unused PIO fallback (kept for compatibility) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* #PIO_Read_Func# */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* #PIO_Write_Func# */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_plx_ops = {
    .read = pcibase_plx_read,
    .write = pcibase_plx_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_mbox_ops = {
    .read = pcibase_mbox_read,
    .write = pcibase_mbox_write,
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* #Reset_Func# */
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->plx_regs, 0, sizeof(s->plx_regs));
    memset(s->mbox_regs, 0, sizeof(s->mbox_regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->hw_status = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, 
                                 MemoryRegionOps *ops, void *opaque, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, opaque, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, opaque, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "pcxhr-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 0x100, .name = "pcxhr-plx" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 0x100, .name = "pcxhr-mbox" };
    for (int i = 0; i < s->num_bars; i++) {
        s->bar_opaque[i].s = s;
        s->bar_opaque[i].bar_idx = i;
        MemoryRegionOps *ops;
        void *opaque = &s->bar_opaque[i];
        switch (i) {
        case 0: ops = &pcibase_mmio_ops; break;
        case 1: ops = &pcibase_plx_ops; break;
        case 2: ops = &pcibase_mbox_ops; break;
        default: ops = NULL;
        }
        pcibase_register_bar(pdev, s, &s->bar_info[i], ops, opaque, errp);
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
    .name = "snd_pcxhr_pci",
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

type_init(pcibase_register_types)
