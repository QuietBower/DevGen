/*
 * QEMU PCI device model skeleton for ATTO ExpressSAS esas2r
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
/* #HeadFile# - no additional driver headers needed at this phase */

#define TYPE_PCIBASE_DEVICE "esas2r_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* #Related_Config_Info# */
/*
 * PCI identification: use first entry of esas2r_pci_table
 * Vendor: ATTO_VENDOR_ID = 0x117C
 * Device: 0x0049
 */
#define ESAS2R_PCI_VENDOR_ID   0x117C
#define ESAS2R_PCI_DEVICE_ID   0x0049

/* SCSI RAID/HBA class; use generic mass storage if not explicitly specified */
#define ESAS2R_PCI_CLASS_ID    0x0100

/* Minimal register offsets used by macros in driver */
#define MW_REG_OFFSET_HWREG    0x00000000u
#define MVR_PCI_WIN1_REMAP     0x00008438u
#define MW_DATA_WINDOW_SIZE    0x00020000u
#define MVRPW1R_ENABLE         0x00000001u

#define MU_DOORBELL_OUT        0x00010480u
#define MU_OUT_LIST_INT_STAT   0x00004088u
#define MU_INT_STATUS_OUT      0x00010200u
#define MU_OUT_LIST_COPY       0x0000406Cu
#define MU_OUT_LIST_COPY_PTR_LO 0x00004058u
#define MU_OUT_LIST_COPY_PTR_HI 0x0000405Cu
#define MU_OUT_LIST_ADDR_LO    0x00004050u
#define MU_OUT_LIST_ADDR_HI    0x00004054u
#define MU_OUT_LIST_WRITE      0x00004068u
#define MU_OUT_LIST_CONFIG     0x0000407Cu
#define MU_OUT_LIST_INT_MASK   0x0000408Cu
#define MU_OUT_LIST_IFC_CONFIG 0x00004078u

#define MU_INT_MASK_OUT        0x0001020Cu
#define MU_INTSTAT_MASK        0x00001010u
#define MU_INTSTAT_POST_OUT    0x00000010u
#define MU_OLIS_INT            0x00000001u
#define MU_DOORBELL_IN         0x00010460u
#define MU_DOORBELL_IN_ENB     0x00010464u
#define MU_DOORBELL_OUT_ENB    0x00010484u

#define MU_IN_LIST_ADDR_LO     0x00004000u
#define MU_IN_LIST_ADDR_HI     0x00004004u
#define MU_IN_LIST_WRITE       0x00004018u
#define MU_IN_LIST_READ        0x0000401Cu
#define MU_IN_LIST_IFC_CONFIG  0x00004028u
#define MU_IN_LIST_CONFIG      0x0000402Cu

#define MU_ILC_ENABLE          0x00000001u
#define MU_ILC_ENTRY_MASK      0x000000F0u
#define MU_ILC_ENTRY_4_DW      0x00000020u
#define MU_ILC_NUMBER_MASK     0x7FFF0000u
#define MU_ILC_NUMBER_SHIFT    16
#define MU_ILC_DYNAMIC_SRC     0x00008000u
#define MU_ILC_TOGGLE          0x00004000u
#define MU_ILIC_LIST           0x0000000Fu
#define MU_ILIC_LIST_F0        0x00000000u
#define MU_ILIC_DEST_DDR       0x00000200u
#define MU_ILIC_DEST           0x00000F00u
#define MU_ILW_TOGGLE          0x00004000u

#define MU_OLC_ENABLE          0x00000001u
#define MU_OLC_ENTRY_MASK      0x000000F0u
#define MU_OLC_ENTRY_4_DW      0x00000020u
#define MU_OLC_NUMBER_MASK     0x7FFF0000u
#define MU_OLC_NUMBER_SHIFT    16
#define MU_OLC_TOGGLE          0x00004000u
#define MU_OLC_WRT_PTR         0x00003FFFu
#define MU_OLIC_LIST           0x0000000Fu
#define MU_OLIC_LIST_F0        0x00000000u
#define MU_OLIC_SOURCE_DDR     0x00000200u
#define MU_OLIC_SOURCE         0x00000F00u
#define MU_OLIS_MASK           0x00000001u

#define MU_CTL_STATUS_IN       0x00010108u
#define MU_CTL_STATUS_IN_B2    0x00010130u
#define MU_CTL_IN_FULL_RST     0x00000020u
#define MU_CTL_IN_FULL_RST2    0x80000000u

#define MW_DATA_ADDR_SRAM      0xF4000000u
#define MW_DATA_ADDR_PAR_FLASH 0xFC000000u
#define MW_DATA_ADDR_SER_FLASH 0xEC000000u

#define ESAS2R_BAR0_WINDOW_SIZE MW_DATA_WINDOW_SIZE

/* Interrupt enable/disable masks referenced by esas2r_enable/disable_chip_interrupts */
#define ESAS2R_INT_ENB_MASK    0x00000000u
#define ESAS2R_INT_DIS_MASK    0xFFFFFFFFu

/* Bit used in MU_INT_STATUS_OUT for doorbell according to driver MU_INTSTAT_DRBL */
#define MU_INTSTAT_DRBL        0x00000020u

/* Simple model constants */
#define ESAS2R_MAX_OUTBOUND_ENTRIES  64
#define ESAS2R_MAX_INBOUND_ENTRIES   64

#define ESAS2R_FW_VER_0 0x00000000u
#define ESAS2R_FW_VER_1 0x00010000u

#define DRBL_FORCE_INT   0x00000001u
#define DRBL_FW_VER_MSK  0x000000F0u
#define DRBL_MSG_IFC_DOWN 0x00000002u
#define DRBL_MSG_IFC_INIT 0x00000004u
#define DRBL_RESET_BUS    0x00000008u
#define DRBL_FW_RESET     0x00000010u
#define DRBL_POWER_DOWN   0x00000020u
#define DRBL_DRV_VER      0x00000040u
#define DRBL_ENB_MASK     (DRBL_FORCE_INT | DRBL_MSG_IFC_DOWN | DRBL_MSG_IFC_INIT | DRBL_RESET_BUS | DRBL_FW_RESET | DRBL_POWER_DOWN)

#define ESAS2R_INT_STS_MASK (MU_INTSTAT_POST_OUT | MU_INTSTAT_DRBL)


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

    /* Interrupt/status shadow registers */
    uint32_t mu_int_status_out;
    uint32_t mu_int_mask_out_reg;
    uint32_t mu_out_list_int_stat;
    uint32_t mu_out_list_int_mask;
    uint32_t mu_doorbell_in;
    uint32_t mu_doorbell_out;
    uint32_t mu_doorbell_in_enb;
    uint32_t mu_doorbell_out_enb;

    /* Communication list configuration */
    uint32_t mu_in_list_config;
    uint32_t mu_out_list_config;
    uint32_t mu_in_list_ifc_config;
    uint32_t mu_out_list_ifc_config;
    uint32_t mu_in_list_write;
    uint32_t mu_in_list_read;
    uint32_t mu_out_list_write;
    uint32_t mu_out_list_copy;
    uint32_t mu_out_list_addr_lo;
    uint32_t mu_out_list_addr_hi;
    uint32_t mu_out_list_copy_ptr_lo;
    uint32_t mu_out_list_copy_ptr_hi;
    uint32_t mu_in_list_addr_lo;
    uint32_t mu_in_list_addr_hi;

    /* Window remap */
    uint32_t mvr_pci_win1_remap;

    /* Data window backing store (BAR0) */
    uint8_t *data_window_ram;
    uint32_t window_base;

    /* Simple outbound ring modeled in RAM (not real DMA) */
    uint32_t outbound_entries[ESAS2R_MAX_OUTBOUND_ENTRIES];
    uint32_t outbound_write_index; /* firmware write pointer */
    uint32_t outbound_read_index;  /* driver read pointer */

    /* Simple inbound ring placeholder (not used by this model) */
    uint32_t inbound_entries[ESAS2R_MAX_INBOUND_ENTRIES];
    uint32_t inbound_write_index;
    uint32_t inbound_read_index;

    /* Internal state */
    bool irq_asserted;
};


static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* interrupt is pending if masked status has any enabled bit set */
    uint32_t pending = s->mu_int_status_out & ~s->mu_int_mask_out_reg;

    if (pending && !s->irq_asserted) {
        s->irq_asserted = true;
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else if (!pending && s->irq_asserted) {
        s->irq_asserted = false;
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The real hardware uses DMA rings; this simplified model does not
     * implement actual DMA as the driver never inspects ring contents in
     * this file. All firmware protocol is abstracted away in other driver
     * units, which are not modeled here. */
    (void)pdev;
    (void)is_write;
}

static inline uint32_t pcibase_mmio_read32_raw(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case MU_INT_STATUS_OUT:
        return s->mu_int_status_out & MU_INTSTAT_MASK;
    case MU_INT_MASK_OUT:
        return s->mu_int_mask_out_reg;
    case MU_OUT_LIST_INT_STAT:
        return s->mu_out_list_int_stat;
    case MU_OUT_LIST_INT_MASK:
        return s->mu_out_list_int_mask;
    case MU_DOORBELL_IN:
        return s->mu_doorbell_in;
    case MU_DOORBELL_OUT:
        return s->mu_doorbell_out;
    case MU_DOORBELL_IN_ENB:
        return s->mu_doorbell_in_enb;
    case MU_DOORBELL_OUT_ENB:
        return s->mu_doorbell_out_enb;
    case MU_IN_LIST_CONFIG:
        return s->mu_in_list_config;
    case MU_OUT_LIST_CONFIG:
        return s->mu_out_list_config;
    case MU_IN_LIST_IFC_CONFIG:
        return s->mu_in_list_ifc_config;
    case MU_OUT_LIST_IFC_CONFIG:
        return s->mu_out_list_ifc_config;
    case MU_IN_LIST_WRITE:
        return s->mu_in_list_write;
    case MU_IN_LIST_READ:
        return s->mu_in_list_read;
    case MU_OUT_LIST_WRITE:
        return s->mu_out_list_write;
    case MU_OUT_LIST_COPY:
        return s->mu_out_list_copy;
    case MU_OUT_LIST_ADDR_LO:
        return s->mu_out_list_addr_lo;
    case MU_OUT_LIST_ADDR_HI:
        return s->mu_out_list_addr_hi;
    case MU_OUT_LIST_COPY_PTR_LO:
        return s->mu_out_list_copy_ptr_lo;
    case MU_OUT_LIST_COPY_PTR_HI:
        return s->mu_out_list_copy_ptr_hi;
    case MU_IN_LIST_ADDR_LO:
        return s->mu_in_list_addr_lo;
    case MU_IN_LIST_ADDR_HI:
        return s->mu_in_list_addr_hi;
    case MVR_PCI_WIN1_REMAP:
        return s->mvr_pci_win1_remap;
    default:
        return 0;
    }
}

static inline void pcibase_mmio_write32_raw(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;

    switch (addr) {
    case MU_INT_MASK_OUT:
        s->mu_int_mask_out_reg = val;
        break;
    case MU_OUT_LIST_INT_MASK:
        s->mu_out_list_int_mask = val;
        break;
    case MU_OUT_LIST_INT_STAT:
        /* W1C for OUT_LIST interrupt bit */
        if (val & MU_OLIS_INT) {
            s->mu_out_list_int_stat &= ~MU_OLIS_INT;
            /* also clear corresponding bit in main status */
            s->mu_int_status_out &= ~MU_INTSTAT_POST_OUT;
        }
        break;
    case MU_DOORBELL_IN:
        s->mu_doorbell_in = val;
        /* reflect into outbound doorbell when enabled, as done by firmware */
        if (s->mu_doorbell_out_enb & DRBL_ENB_MASK) {
            s->mu_doorbell_out |= val & DRBL_ENB_MASK;
            /* generate doorbell interrupt */
            s->mu_int_status_out |= MU_INTSTAT_DRBL;
        }
        break;
    case MU_DOORBELL_OUT:
        /* driver writes to clear bits */
        s->mu_doorbell_out &= ~val;
        if ((s->mu_doorbell_out & DRBL_ENB_MASK) == 0) {
            s->mu_int_status_out &= ~MU_INTSTAT_DRBL;
        }
        break;
    case MU_DOORBELL_IN_ENB:
        s->mu_doorbell_in_enb = val;
        break;
    case MU_DOORBELL_OUT_ENB:
        s->mu_doorbell_out_enb = val;
        break;
    case MU_IN_LIST_CONFIG:
        s->mu_in_list_config = val;
        break;
    case MU_OUT_LIST_CONFIG:
        s->mu_out_list_config = val;
        break;
    case MU_IN_LIST_IFC_CONFIG:
        s->mu_in_list_ifc_config = val;
        break;
    case MU_OUT_LIST_IFC_CONFIG:
        s->mu_out_list_ifc_config = val;
        break;
    case MU_IN_LIST_WRITE:
        s->mu_in_list_write = val;
        break;
    case MU_IN_LIST_READ:
        s->mu_in_list_read = val;
        break;
    case MU_OUT_LIST_WRITE:
        s->mu_out_list_write = val;
        break;
    case MU_OUT_LIST_COPY:
        s->mu_out_list_copy = val;
        break;
    case MU_OUT_LIST_ADDR_LO:
        s->mu_out_list_addr_lo = val;
        break;
    case MU_OUT_LIST_ADDR_HI:
        s->mu_out_list_addr_hi = val;
        break;
    case MU_OUT_LIST_COPY_PTR_LO:
        s->mu_out_list_copy_ptr_lo = val;
        break;
    case MU_OUT_LIST_COPY_PTR_HI:
        s->mu_out_list_copy_ptr_hi = val;
        break;
    case MU_IN_LIST_ADDR_LO:
        s->mu_in_list_addr_lo = val;
        break;
    case MU_IN_LIST_ADDR_HI:
        s->mu_in_list_addr_hi = val;
        break;
    case MVR_PCI_WIN1_REMAP:
        s->mvr_pci_win1_remap = val;
        s->window_base = val & ~(MW_DATA_WINDOW_SIZE - 1);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* BAR2 register space */
    if (addr < 0x00020000) {
        if (size == 4) {
            val = pcibase_mmio_read32_raw(s, addr);
        } else if (size == 1 || size == 2 || size == 8) {
            /* Allow but only wired as 32-bit; smaller sizes read from 32-bit value */
            uint32_t tmp = pcibase_mmio_read32_raw(s, addr & ~0x3u);
            unsigned shift = (addr & 0x3u) * 8;
            uint64_t mask = ((size == 8) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << (size * 8)) - 1));
            val = (tmp >> shift) & mask;
        }
    } else {
        /* BAR0 data window (BAR0 is separate region, but we reused same ops for simplicity)
         * In our BAR mapping we only expose BAR0 and BAR2 via same handler; distinguish
         * by BAR registration: BAR0 starts at 0 in its region, BAR2 also starts at 0.
         * To match driver usage, we model windowed memory in BAR0 region only; here we
         * assume MMIO for BAR2 only, so any offset >= 0x20000 would not be used.
         */
    }

    (void)s;
    (void)addr;
    (void)size;

    /* Update IRQ line based on new status */
    pcibase_update_irq(s);

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x00020000) {
        if (size == 4) {
            pcibase_mmio_write32_raw(s, addr, (uint32_t)val);
        } else if (size == 1 || size == 2 || size == 8) {
            /* Partial writes emulate typical behavior by updating whole 32-bit word */
            hwaddr base = addr & ~0x3u;
            uint32_t cur = pcibase_mmio_read32_raw(s, base);
            unsigned shift = (addr & 0x3u) * 8;
            uint32_t mask = ((size == 8) ? 0xFFFFFFFFu : ((1u << (size * 8)) - 1u)) << shift;
            uint32_t newv = (cur & ~mask) | (((uint32_t)val << shift) & mask);
            pcibase_mmio_write32_raw(s, base, newv);
        }
    } else {
        /* no-op for out-of-range within this BAR */
    }

    pcibase_update_irq(s);

    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Clear interrupt and doorbell state */
    s->mu_int_status_out = 0;
    s->mu_int_mask_out_reg = ESAS2R_INT_DIS_MASK;
    s->mu_out_list_int_stat = 0;
    s->mu_out_list_int_mask = 0;
    s->mu_doorbell_in = 0;
    s->mu_doorbell_out = 0;
    s->mu_doorbell_in_enb = 0;
    s->mu_doorbell_out_enb = DRBL_ENB_MASK;

    s->mu_in_list_config = 0;
    s->mu_out_list_config = 0;
    s->mu_in_list_ifc_config = 0;
    s->mu_out_list_ifc_config = 0;
    s->mu_in_list_write = 0;
    s->mu_in_list_read = 0;
    s->mu_out_list_write = 0;
    s->mu_out_list_copy = 0;
    s->mu_out_list_addr_lo = 0;
    s->mu_out_list_addr_hi = 0;
    s->mu_out_list_copy_ptr_lo = 0;
    s->mu_out_list_copy_ptr_hi = 0;
    s->mu_in_list_addr_lo = 0;
    s->mu_in_list_addr_hi = 0;

    s->mvr_pci_win1_remap = 0;
    s->window_base = 0;

    s->outbound_write_index = 0;
    s->outbound_read_index = 0;
    s->inbound_write_index = 0;
    s->inbound_read_index = 0;

    s->irq_asserted = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ESAS2R_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ESAS2R_PCI_DEVICE_ID );

    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, ESAS2R_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, ESAS2R_PCI_DEVICE_ID);

    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ESAS2R_PCI_CLASS_ID );
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

    /* BAR0: data_window */
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = ESAS2R_BAR0_WINDOW_SIZE;
    s->bar_info[0].name  = "esas2r-bar0-data";

    /* BAR1: unused */
    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_NONE;
    s->bar_info[1].size  = 0;
    s->bar_info[1].name  = "";

    /* BAR2: regs */
    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_MMIO;
    s->bar_info[2].size  = 0x10000; /* enough to cover all used offsets */
    s->bar_info[2].name  = "esas2r-bar2-regs";

    for (int i = 3; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI capability for one vector (driver may enable MSI) */
    if (!msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = true;
    }

    s->has_msix = false;

    /* Allocate backing storage for data window (not strictly required for probe) */
    s->data_window_ram = g_malloc0(ESAS2R_BAR0_WINDOW_SIZE);

    /* Reset device state */
    pcibase_reset(DEVICE(pdev));
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

    if (s->data_window_ram) {
        g_free(s->data_window_ram);
        s->data_window_ram = NULL;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "esas2r_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(mu_int_status_out, PCIBaseState),
        VMSTATE_UINT32(mu_int_mask_out_reg, PCIBaseState),
        VMSTATE_UINT32(mu_out_list_int_stat, PCIBaseState),
        VMSTATE_UINT32(mu_out_list_int_mask, PCIBaseState),
        VMSTATE_UINT32(mu_doorbell_in, PCIBaseState),
        VMSTATE_UINT32(mu_doorbell_out, PCIBaseState),
        VMSTATE_UINT32(mu_doorbell_in_enb, PCIBaseState),
        VMSTATE_UINT32(mu_doorbell_out_enb, PCIBaseState),
        VMSTATE_UINT32(mu_in_list_config, PCIBaseState),
        VMSTATE_UINT32(mu_out_list_config, PCIBaseState),
        VMSTATE_UINT32(mu_in_list_ifc_config, PCIBaseState),
        VMSTATE_UINT32(mu_out_list_ifc_config, PCIBaseState),
        VMSTATE_UINT32(mu_in_list_write, PCIBaseState),
        VMSTATE_UINT32(mu_in_list_read, PCIBaseState),
        VMSTATE_UINT32(mu_out_list_write, PCIBaseState),
        VMSTATE_UINT32(mu_out_list_copy, PCIBaseState),
        VMSTATE_UINT32(mu_out_list_addr_lo, PCIBaseState),
        VMSTATE_UINT32(mu_out_list_addr_hi, PCIBaseState),
        VMSTATE_UINT32(mu_out_list_copy_ptr_lo, PCIBaseState),
        VMSTATE_UINT32(mu_out_list_copy_ptr_hi, PCIBaseState),
        VMSTATE_UINT32(mu_in_list_addr_lo, PCIBaseState),
        VMSTATE_UINT32(mu_in_list_addr_hi, PCIBaseState),
        VMSTATE_UINT32(mvr_pci_win1_remap, PCIBaseState),
        VMSTATE_UINT32(window_base, PCIBaseState),
        VMSTATE_BOOL(irq_asserted, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->vendor_id = ESAS2R_PCI_VENDOR_ID;
    k->device_id = ESAS2R_PCI_DEVICE_ID;
    k->revision = 0x01;
    k->class_id = ESAS2R_PCI_CLASS_ID;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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

