/*
 * QEMU PCI device model for hpilo driver
 * Implementation phase - functional behavior
 * Based on provided driver source
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

/* Additional include files retrieved from driver context */
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "hpilo_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ILO_DEVICE_ID 0xB204
#define ILO_CLASS_ID PCI_CLASS_OTHERS

/* Supplementary defines from driver source */
#define MAX_CCB       24
#define MIN_CCB       8
#define ILOHW_CCB_SZ  128
#define PCI_REV_ID_NECHES 7
#define L2_DB_SIZE    14
#define ONE_DB_SIZE   (1 << L2_DB_SIZE)

/* Register offsets (updated per driver source) */
#define DB_OUT   0xD4
#define DB_IRQ   0xB2
/* DB_RESET is a bit number, not a register offset; unused in this model for now */

/* FIFO related defines – obtained from new driver source */
#define ILO_CACHE_SZ 128
#define FIFOHANDLESIZE ILO_CACHE_SZ
#define NR_QENTRY     4
#define ENTRY_BITS_C             1
#define ENTRY_BITPOS_C           22
#define ENTRY_BITS_O             1
#define ENTRY_BITPOS_O           23
#define ENTRY_MASK_C   (1ULL << ENTRY_BITPOS_C)
#define ENTRY_MASK_O   (1ULL << ENTRY_BITPOS_O)

/*
 * struct fifo: FIFO queue header layout.
 * Verbatim from driver source.
 */
struct fifo {
    uint64_t nrents;	/* user requested number of fifo entries */
    uint64_t imask;   /* mask to extract valid fifo index */
    uint64_t merge;	/*  O/C bits to merge in during enqueue operation */
    uint64_t reset;	/* set to non-zero when the target device resets */
    uint8_t  pad_0[ILO_CACHE_SZ - (sizeof(uint64_t) * 4)];

    uint64_t head;
    uint8_t  pad_1[ILO_CACHE_SZ - (sizeof(uint64_t))];

    uint64_t tail;
    uint8_t  pad_2[ILO_CACHE_SZ - (sizeof(uint64_t))];

    uint64_t fifobar[];  /* flexible array member */
};

/*
 * struct ccb: channel control block layout (verbatim from driver source).
 * Size padded to ILOHW_CCB_SZ (128 bytes) to match hardware.
 */
struct ccb {
    union {
        char *send_fifobar;
        uint64_t send_fifobar_pa;
    } ccb_u1;
    union {
        char *send_desc;
        uint64_t send_desc_pa;
    } ccb_u2;
    uint64_t send_ctrl;

    union {
        char *recv_fifobar;
        uint64_t recv_fifobar_pa;
    } ccb_u3;
    union {
        char *recv_desc;
        uint64_t recv_desc_pa;
    } ccb_u4;
    uint64_t recv_ctrl;

    union {
        uint64_t padding5;
    } ccb_u5;

    uint64_t channel;

    /* unused context area (64 bytes) – included to match hardware size */
    uint8_t context[64];
};

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
    const MemoryRegionOps *ops;  /* ops pointer for MMIO regions */
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    /* Registers */
    uint32_t db_out;
    uint8_t  db_irq;

    /* Doorbell ops (separate from MMIO) */
    MemoryRegionOps doorbell_ops;
};

/* Forward declarations */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_doorbell_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_doorbell_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_doorbell_ops = {
    .read = pcibase_doorbell_read,
    .write = pcibase_doorbell_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* MMIO Read Handler for BAR1 (control registers) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DB_OUT:
        val = s->db_out;
        break;
    case DB_IRQ:
        val = s->db_irq;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
    return val;
}

/* MMIO Write Handler for BAR1 (control registers) */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case DB_OUT:
        /* Write-1-to-clear behavior */
        s->db_out &= ~val;
        break;
    case DB_IRQ:
        s->db_irq = val & 1;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

/* Doorbell Read Handler for BAR3 (doorbell aperture) */
static uint64_t pcibase_doorbell_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Doorbell is write-only, return 0 on reads */
    return 0;
}

/* Doorbell Write Handler for BAR3 (doorbell aperture) */
static void pcibase_doorbell_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    int channel = addr >> L2_DB_SIZE;
    uint32_t val32 = val;  /* only low byte matters if size=1 */

    if (val32 == 1) {
        /* Doorbell set: process a send FIFO entry for this channel */
        /* Get the physical address of the CCB in BAR5 (RAM) */
        uint64_t bar5_addr = pci_get_bar_addr(pdev, 5);
        if (!bar5_addr) {
            return;
        }
        /* CCB area starts at top 8 KB of BAR5; we assume BAR5 size >= 0x2000 + max_ccb*ILOHW_CCB_SZ */
        hwaddr ccb_offset = 0x2000 + channel * ILOHW_CCB_SZ;
        uint64_t ccb_phys = bar5_addr + ccb_offset;

        /* Read the send_fifobar_pa from the CCB using DMA */
        struct ccb ccb;
        pci_dma_read(pdev, ccb_phys, &ccb, sizeof(ccb));
        uint64_t send_fifobar_pa = ccb.ccb_u1.send_fifobar_pa;

        /* No valid FIFO? */
        if (!send_fifobar_pa) {
            return;
        }

        /* Read the imask from the FIFO header (offset 8) */
        uint64_t imask;
        pci_dma_read(pdev, send_fifobar_pa + 8, &imask, sizeof(imask));

        /* Read the head from the FIFO header (offset 128) */
        uint64_t head;
        pci_dma_read(pdev, send_fifobar_pa + 128, &head, sizeof(head));

        /* Compute the entry offset based on the actual fifo structure layout */
        uint64_t entry_offset = offsetof(struct fifo, fifobar) + (head & imask) * sizeof(uint64_t);
        uint64_t entry;
        pci_dma_read(pdev, send_fifobar_pa + entry_offset, &entry, sizeof(entry));

        /* Set the completion bit (ENTRY_MASK_C) */
        entry |= ENTRY_MASK_C;
        pci_dma_write(pdev, send_fifobar_pa + entry_offset, &entry, sizeof(entry));

        /* Set the corresponding pending bit in DB_OUT and raise IRQ if enabled */
        s->db_out |= (1 << channel);
        if (s->db_irq & 1) {
            pci_set_irq(pdev, 1);
        }
    } else if (val32 == 2) {
        /* Doorbell clear: does nothing in our model */
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset register state */
    s->db_out = 0;
    s->db_irq = 0;
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
        memory_region_init_io(mr, OBJECT(s), bi->ops ? bi->ops : &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x0e11); /* PCI_VENDOR_ID_COMPAQ */
    pci_set_word(pci_conf + PCI_DEVICE_ID, ILO_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ILO_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, PCI_REV_ID_NECHES);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization based on driver mapping: ilo_map_device() */
    /* BAR0: unused */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_NONE;
    s->bar_info[0].size = 0;
    s->bar_info[0].name = "bar0";
    s->bar_info[0].ops = NULL;

    /* BAR1: MMIO registers */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;  /* sufficient for register space */
    s->bar_info[1].name = "mmio";
    s->bar_info[1].ops = &pcibase_mmio_ops;

    /* BAR2: Legacy RAM (unused by driver when revision >= NECHES, but keep for compatibility) */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_RAM;
    s->bar_info[2].size = MAX_CCB * ILOHW_CCB_SZ;
    s->bar_info[2].name = "ram_legacy";
    s->bar_info[2].ops = NULL;

    /* BAR3: Doorbell aperture */
    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_MMIO;
    s->bar_info[3].size = MAX_CCB * ONE_DB_SIZE;
    s->bar_info[3].name = "doorbell";
    s->bar_info[3].ops = &pcibase_doorbell_ops;

    /* BAR5: RAM for CCBs (used when revision >= NECHES) */
    s->bar_info[5].index = 5;
    s->bar_info[5].type = BAR_TYPE_RAM;
    s->bar_info[5].size = 0x4000;  /* 16 KB, CCB area at top 8 KB */
    s->bar_info[5].name = "ram";
    s->bar_info[5].ops = NULL;

    s->num_bars = 6;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X, no DMA */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No driver-specific teardown needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "hpilo_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(db_out, PCIBaseState),
        VMSTATE_UINT8(db_irq, PCIBaseState),
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
