/*
 * QEMU model for Intel 3200 Memory Controller (EDAC)
 * Based on driver analysis: registers, BAR, config space, MMIO reads only.
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

#define TYPE_PCIBASE_DEVICE "i3200_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL              0x8086
#define PCI_DEVICE_ID_INTEL_3200_HB      0x29f0

#define I3200_MMR_WINDOW_SIZE           16384

#define I3200_MCHBAR_LOW                 0x48
#define I3200_MCHBAR_HIGH                0x4c
#define I3200_MCHBAR_MASK               0xfffffc000ULL
#define I3200_TOM                        0xa0
#define I3200_TOM_MASK                   0x3ff
#define I3200_TOM_SHIFT                  26
#define I3200_ERRSTS                     0xc8
#define I3200_ERRSTS_UE                  0x0002
#define I3200_ERRSTS_CE                  0x0001
#define I3200_ERRSTS_BITS               (I3200_ERRSTS_UE | I3200_ERRSTS_CE)
#define I3200_C0DRB                     0x200
#define I3200_C1DRB                     0x600
#define I3200_DRB_MASK                   0x3ff
#define I3200_DRB_SHIFT                  26
#define I3200_C0ECCERRLOG               0x280
#define I3200_C1ECCERRLOG               0x680
#define I3200_ECCERRLOG_CE               0x1
#define I3200_ECCERRLOG_UE               0x2
#define I3200_ECCERRLOG_RANK_BITS       0x18000000
#define I3200_ECCERRLOG_RANK_SHIFT      27
#define I3200_ECCERRLOG_SYNDROME_BITS   0xff0000
#define I3200_ECCERRLOG_SYNDROME_SHIFT   16
#define I3200_CAPID0                     0xe0

/* Constants from driver not provided in headers */
#define I3200_RANKS_PER_CHANNEL          2

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t mmr[I3200_MMR_WINDOW_SIZE];

    /* Config space shadow registers */
    uint16_t errsts;
    uint16_t tom;
    uint8_t capid0_8;
};

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > I3200_MMR_WINDOW_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds at 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }

    switch (size) {
    case 1:
        val = s->mmr[addr];
        break;
    case 2:
        val = lduw_le_p(&s->mmr[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->mmr[addr]);
        break;
    case 8:
        val = ldq_le_p(&s->mmr[addr]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read size %d at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        val = 0;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* The driver does not write to the MMIO region; ignore. */
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

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (address) {
    case I3200_MCHBAR_LOW:
        return pci_get_long(pdev->config + PCI_BASE_ADDRESS_0);
    case I3200_MCHBAR_HIGH:
        return pci_get_long(pdev->config + PCI_BASE_ADDRESS_0 + 4);
    case I3200_TOM:
        return s->tom;
    case I3200_ERRSTS:
        return s->errsts;
    case I3200_CAPID0 + 8:
        return s->capid0_8;
    default:
        return pci_default_read_config(pdev, address, len);
    }
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address,
                                 uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (address) {
    case I3200_ERRSTS:
        /* W1C: clear bits that are set in val (driver writes value=mask=bits to clear) */
        s->errsts &= ~(val & I3200_ERRSTS_BITS);
        pci_set_word(pdev->config + I3200_ERRSTS, s->errsts);
        break;
    default:
        pci_default_write_config(pdev, address, val, len);
        break;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->mmr, 0, sizeof(s->mmr));
    s->errsts = 0;
    s->tom = 0x0020; /* 32 units of 64MB = 2GB, non-stacked for dual channel */
    s->capid0_8 = 0x00; /* dual channel enabled, both channels filled */

    /* Initialize DRB values: 1GB per rank, both channels identical */
    uint16_t drb = 0x0010; /* 16 * 64MB = 1GB */
    for (int i = 0; i < I3200_RANKS_PER_CHANNEL; i++) {
        stw_le_p(&s->mmr[I3200_C0DRB + 2*i], drb);
        stw_le_p(&s->mmr[I3200_C1DRB + 2*i], drb);
    }
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
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64, mr);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_3200_HB);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0600);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: 64-bit memory BAR for MCHBAR */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = I3200_MMR_WINDOW_SIZE;
    s->bar_info[0].name = "mchbar";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MMIO region contents and config shadows */
    memset(s->mmr, 0, sizeof(s->mmr));
    s->errsts = 0;
    s->tom = 0x0020;
    s->capid0_8 = 0x00;

    uint16_t drb = 0x0010;
    for (int i = 0; i < I3200_RANKS_PER_CHANNEL; i++) {
        stw_le_p(&s->mmr[I3200_C0DRB + 2*i], drb);
        stw_le_p(&s->mmr[I3200_C1DRB + 2*i], drb);
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

/* VMState for migration */
static const VMStateDescription vmstate_pcibase = {
    .name = "i3200_edac_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(mmr, PCIBaseState, I3200_MMR_WINDOW_SIZE),
        VMSTATE_UINT16(errsts, PCIBaseState),
        VMSTATE_UINT16(tom, PCIBaseState),
        VMSTATE_UINT8(capid0_8, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
