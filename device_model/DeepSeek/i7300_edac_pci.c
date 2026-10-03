/*
 * QEMU model for Intel 7300 Memory Controller (EDAC) - Function 0, 1, and 2
 * Based on driver: /home/eely/linux-7.1/drivers/edac/i7300_edac.c
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

#define TYPE_PCIBASE_DEVICE "i7300_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define TYPE_PCIBASE_DEVICE_FUNC1 "i7300_edac_pci_func1"
typedef struct PCIBaseStateFunc1 PCIBaseStateFunc1;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseStateFunc1, PCIBASE_DEVICE_FUNC1)

#define TYPE_PCIBASE_DEVICE_FUNC2 "i7300_edac_pci_func2"
typedef struct PCIBaseStateFunc2 PCIBaseStateFunc2;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseStateFunc2, PCIBASE_DEVICE_FUNC2)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_I7300 0x8086
#define PCI_DEVICE_ID_I7300 0x360c
#define PCI_CLASS_I7300 0x058000

/* Register offsets from driver */
#define AMBASE          0x48
#define MAXCH           0x56
#define MAXDIMMPERCH    0x57
#define TOLM            0x6C
#define REDMEMB         0x7c
#define MIR0            0x80
#define MIR1            0x84
#define MIR2            0x88
#define FERR_FAT_FBD    0x98
#define FERR_NF_FBD     0xa0
#define EMASK_FBD       0xa8
#define AMBPRESENT_0    0x64
#define AMBPRESENT_1    0x66
#define NRECMEMA        0xbe
#define NRECMEMB        0xc0
#define REDMEMA         0xdc
#define RECMEMA         0xe0
#define RECMEMB         0xe4
#define MC_SETTINGS     0x40
#define MC_SETTINGS_A   0x58
#define FERR_GLOBAL_HI  0x48
#define FERR_GLOBAL_LO  0x40

/* Bit masks and shifts (kept for reference, may be used in future expansions) */
#define MTR_DIMMS_PRESENT(mtr)      ((mtr) & (1 << 8))
#define MTR_DIMMS_ETHROTTLE(mtr)    ((mtr) & (1 << 7))
#define MTR_DRAM_WIDTH(mtr)         (((mtr) & (1 << 6)) ? 8 : 4)
#define MTR_DRAM_BANKS(mtr)         (((mtr) & (1 << 5)) ? 8 : 4)
#define MTR_DRAM_BANKS_ADDR_BITS    2
#define MTR_DIMM_ROWS(mtr)          (((mtr) >> 2) & 0x3)
#define MTR_DIMM_ROWS_ADDR_BITS(mtr) (MTR_DIMM_ROWS(mtr) + 13)
#define MTR_DIMM_COLS(mtr)          ((mtr) & 0x3)
#define MTR_DIMM_COLS_ADDR_BITS(mtr) (MTR_DIMM_COLS(mtr) + 10)
#define MTR_DIMM_RANKS(mtr)         (((mtr) & (1 << 4)) ? 1 : 0)

#define FERR_FAT_FBD_ERR_MASK ((1 << 0) | (1 << 1) | (1 << 2) | (1 << 22))
#define FERR_NF_FBD_ERR_MASK ((1 << 24) | (1 << 23) | (1 << 22) | (1 << 21) | \
                              (1 << 18) | (1 << 17) | (1 << 16) | (1 << 15) | \
                              (1 << 14) | (1 << 13) | (1 << 11) | (1 << 10) | \
                              (1 << 9)  | (1 << 8)  | (1 << 7)  | (1 << 6)  | \
                              (1 << 5)  | (1 << 4)  | (1 << 3)  | (1 << 2)  | \
                              (1 << 1)  | (1 << 0))
#define EMASK_FBD_ERR_MASK ((1 << 27) | (1 << 26) | (1 << 25) | (1 << 24) | \
                            (1 << 22) | (1 << 21) | (1 << 20) | (1 << 19) | \
                            (1 << 18) | (1 << 17) | (1 << 16) | (1 << 14) | \
                            (1 << 13) | (1 << 12) | (1 << 11) | (1 << 10) | \
                            (1 << 9)  | (1 << 8)  | (1 << 7)  | (1 << 6)  | \
                            (1 << 5)  | (1 << 4)  | (1 << 3)  | (1 << 2)  | \
                            (1 << 1)  | (1 << 0))

#define NRECMEMA_BANK(v)    (((v) >> 12) & 7)
#define NRECMEMA_RANK(v)    (((v) >> 8) & 15)
#define NRECMEMB_IS_WR(v)   ((v) & (1 << 31))
#define NRECMEMB_CAS(v)     (((v) >> 16) & 0x1fff)
#define NRECMEMB_RAS(v)     ((v) & 0xffff)

#define RECMEMA_BANK(v)     (((v) >> 12) & 7)
#define RECMEMA_RANK(v)     (((v) >> 8) & 15)
#define RECMEMB_IS_WR(v)    ((v) & (1 << 31))
#define RECMEMB_CAS(v)      (((v) >> 16) & 0x1fff)
#define RECMEMB_RAS(v)      ((v) & 0xffff)

#define GET_FBD_FAT_IDX(fbderr)     (((fbderr) >> 28) & 3)
#define GET_FBD_NF_IDX(fbderr)      (((fbderr) >> 28) & 3)

#define IS_MIRRORED(mc)         ((mc) & (1 << 16))
#define IS_ECC_ENABLED(mc)      ((mc) & (1 << 5))
#define IS_RETRY_ENABLED(mc)    ((mc) & (1 << 31))
#define IS_SCRBALGO_ENHANCED(mc) ((mc) & (1 << 8))
#define IS_SINGLE_MODE(mca)     ((mca) & (1 << 14))

#define MAX_SLOTS   8
#define MAX_BRANCHES 2
#define MAX_CH_PER_BRANCH 2
#define MAX_MIR 3
#define MAX_CHANNELS (MAX_CH_PER_BRANCH * MAX_BRANCHES)

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

    bool has_msi;
    bool has_msix;

    uint8_t cfg_regs[256];

    uint32_t ambase;            /* Shadow for AMBASE (offset 0x48) */
    /* Function 1 and 2 are separate QEMU devices, not children */
};

struct PCIBaseStateFunc1 {
    PCIDevice parent_obj;
};

struct PCIBaseStateFunc2 {
    PCIDevice parent_obj;
};

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i7300_edac_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pcibase_func1 = {
    .name = "i7300_edac_pci_func1",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseStateFunc1),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pcibase_func2 = {
    .name = "i7300_edac_pci_func2",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseStateFunc2),
        VMSTATE_END_OF_LIST()
    }
};

/* MMIO/PIO Handlers (unused, but kept for completeness) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size) {
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size) {
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
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

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len) {
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr < 0x40) {
        return pci_default_read_config(pdev, addr, len);
    }

    switch (addr) {
    case AMBASE:
        if (len == 4) {
            return s->ambase;
        }
        break;
    }
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len) {
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr < 0x40) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    switch (addr) {
    case AMBASE:
        if (len == 4) {
            s->ambase = val;
        }
        break;
    default:
        pci_default_write_config(pdev, addr, val, len);
        break;
    }
}

static void pcibase_reset(DeviceState *dev) {
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->cfg_regs, 0, sizeof(s->cfg_regs));
    s->ambase = 0;
}

static void pcibase_func1_reset(DeviceState *dev) {
    pci_device_reset(PCI_DEVICE(dev));
}

static void pcibase_func2_reset(DeviceState *dev) {
    pci_device_reset(PCI_DEVICE(dev));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp) {
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

static void pcibase_realize(PCIDevice *pdev, Error **errp) {
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x360c);
    /* Set class code: base=0x05, sub=0x80, prog-if=0x00 */
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x00);
    pci_set_byte(pci_conf + PCI_CLASS_DEVICE, 0x80);
    pci_set_byte(pci_conf + 0x0b, 0x05);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Mark as multifunction device to support function 1 and 2 */
    pci_set_byte(pci_conf + PCI_HEADER_TYPE,
                 pci_get_byte(pci_conf + PCI_HEADER_TYPE) | 0x80);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Functions 1 and 2 are realized by the bus when plugged into the same slot */

    /* No BARs needed for this device */
    s->num_bars = 0;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Device does not use MSI or MSI-X */
}

static void pcibase_func1_realize(PCIDevice *pdev, Error **errp) {
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x360c);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x00);
    pci_set_byte(pci_conf + PCI_CLASS_DEVICE, 0x80);
    pci_set_byte(pci_conf + 0x0b, 0x05);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    /* Function 1 is not designed to use MSI/MSI-X or power management */
}

static void pcibase_func2_realize(PCIDevice *pdev, Error **errp) {
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x360c);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x00);
    pci_set_byte(pci_conf + PCI_CLASS_DEVICE, 0x80);
    pci_set_byte(pci_conf + 0x0b, 0x05);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    /* Function 2 is not designed to use MSI/MSI-X or power management */
}

static void pcibase_uninit(PCIDevice *pdev) {
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* No child functions to unparent */

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static void pcibase_instance_init(Object *obj) {
    PCIBaseState *s = PCIBASE_DEVICE(obj);
    object_property_set_bool(obj, "multifunction", true, &error_abort);
    s->num_bars = 0;
    s->ambase = 0;
}

static void pcibase_func1_instance_init(Object *obj) {
    /* No extra initialization needed for function 1 */
}

static void pcibase_func2_instance_init(Object *obj) {
    /* No extra initialization needed for function 2 */
}

static void pcibase_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_func1_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_func1_realize;
    k->exit    = NULL;
    dc->reset  = pcibase_func1_reset;
    dc->vmsd   = &vmstate_pcibase_func1;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_func2_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_func2_realize;
    k->exit    = NULL;
    dc->reset  = pcibase_func2_reset;
    dc->vmsd   = &vmstate_pcibase_func2;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void) {
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    static const TypeInfo pcibase_func1_info = {
        .name = TYPE_PCIBASE_DEVICE_FUNC1,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseStateFunc1),
        .instance_init = pcibase_func1_instance_init,
        .class_init = pcibase_func1_class_init,
        .interfaces = interfaces,
    };

    static const TypeInfo pcibase_func2_info = {
        .name = TYPE_PCIBASE_DEVICE_FUNC2,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseStateFunc2),
        .instance_init = pcibase_func2_instance_init,
        .class_init = pcibase_func2_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
    type_register_static(&pcibase_func1_info);
    type_register_static(&pcibase_func2_info);
}

type_init(pcibase_register_types);
