/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 1: fill register macros, BAR list, BAR sizes, structs, enums.
 * Phase 2: implement MMIO/PIO read/write, DMA init, IRQ init, reset, config hooks
 * Phase 3 (Repair Phase)：correct syntax, types, includes, missing symbols, remove dead code
 * Phase 4 (Debug & Update Phase)：Based on actual kernel logs from QEMU boot, repair the virtual hardware behavior
 *
 * Replace #PLACEHOLDER# blocks with device-specific code/data.
 * NOTE:
 * - Only essential fixes added (TYPE macro, PIO BAR handling, MMIO impl, DMA init, IRQ).
 * - No optional or advanced features added.
 * - Do not directly fill this file with the Linux kernel source code
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

/* ------------------------------------------------------------------ */
/* Additional include files (Stage1 optional)                          */
/* ------------------------------------------------------------------ */


#define TYPE_PCIBASE_DEVICE "ie31200_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define PCI_VENDOR_ID_INTEL               0x8086
#define PCI_DEVICE_ID_INTEL_IE31200_HB_1  0x0108

#define IE31200_MCHBAR_LOW      0x48
#define IE31200_MCHBAR_HIGH     0x4c
#define IE31200_ERRSTS          0xc8
#define IE31200_ERRSTS_UE       0x02 /* BIT(1) */
#define IE31200_ERRSTS_CE       0x01 /* BIT(0) */
#define IE31200_ERRSTS_BITS     (IE31200_ERRSTS_UE | IE31200_ERRSTS_CE)
#define IE31200_CAPID0          0xe4
#define IE31200_CAPID0_PDCD     0x10 /* BIT(4) */
#define IE31200_CAPID0_DDPCD    0x40 /* BIT(6) */
#define IE31200_CAPID0_ECC      0x02 /* BIT(1) */

#define IE31200_RANKS_PER_CHANNEL       8
#define IE31200_DIMMS_PER_CHANNEL       2
#define IE31200_CHANNELS                2
#define IE31200_IMC_NUM                 2

/* Fixed MCHBAR address for QEMU emulation */
#define MCHBAR_BASE_ADDR                0xfed10000
#define MCHBAR_SIZE                     0x8000 /* 32KB */

/* SNB Specific Offsets */
#define SNB_ECCERRLOG_C0                0x40c8
#define SNB_ECCERRLOG_C1                0x44c8
#define SNB_MADDIMM_C0                  0x5004
#define SNB_MADDIMM_C1                  0x5008

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */

/* Forward declaration of structs used in state */
struct res_config;
struct ie31200_error_info;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* MCHBAR Memory Region (not a standard PCI BAR) */
    MemoryRegion mchbar_mr;

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* Register shadows */
    struct res_config *cfg;

    /* Status fields */
    struct ie31200_error_info *error_info;
};

enum ie31200_chips {
    IE31200 = 0,
    IE31200_1 = 1,
};

enum mem_type {
    MEM_EMPTY = 0,
    MEM_RESERVED,
    MEM_UNKNOWN,
    MEM_FPM,
    MEM_EDO,
    MEM_BEDO,
    MEM_SDR,
    MEM_RDR,
    MEM_DDR,
    MEM_RDDR,
    MEM_RMBS,
    MEM_DDR2,
    MEM_FB_DDR2,
    MEM_RDDR2,
    MEM_XDR,
    MEM_DDR3,
    MEM_RDDR3,
    MEM_LRDDR3,
    MEM_LPDDR3,
    MEM_DDR4,
    MEM_RDDR4,
    MEM_LRDDR4,
    MEM_LPDDR4,
    MEM_DDR5,
    MEM_RDDR5,
    MEM_LRDDR5,
    MEM_NVDIMM,
    MEM_WIO2,
    MEM_HBM2,
    MEM_HBM3,
};

struct res_config {
    enum mem_type mtype;
    bool cmci;
    int imc_num;
    /* Host MMIO configuration register */
    uint64_t reg_mchbar_mask;
    uint64_t reg_mchbar_window_size;
    /* ECC error log register */
    uint64_t reg_eccerrlog_offset[IE31200_CHANNELS];
    uint64_t reg_eccerrlog_ce_mask;
    uint64_t reg_eccerrlog_ce_ovfl_mask;
    uint64_t reg_eccerrlog_ue_mask;
    uint64_t reg_eccerrlog_ue_ovfl_mask;
    uint64_t reg_eccerrlog_rank_mask;
    uint64_t reg_eccerrlog_syndrome_mask;
    /* MSR to clear ECC error log register */
    uint32_t msr_clear_eccerrlog_offset;
    /* DIMM characteristics register */
    uint64_t reg_mad_dimm_size_granularity;
    uint64_t reg_mad_dimm_offset[IE31200_CHANNELS];
    uint32_t reg_mad_dimm_size_mask[IE31200_DIMMS_PER_CHANNEL];
    uint32_t reg_mad_dimm_rank_mask[IE31200_DIMMS_PER_CHANNEL];
    uint32_t reg_mad_dimm_width_mask[IE31200_DIMMS_PER_CHANNEL];
};

struct ie31200_error_info {
    uint16_t errsts;
    uint16_t errsts2;
    uint64_t eccerrlog[IE31200_CHANNELS];
    uint64_t erraddr;
};

struct ie31200_dev_info {
    const char *ctl_name;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* MCHBAR Read Handler */
    /* PCIBaseState *s = opaque; */
    uint64_t val = 0;

    switch (addr) {
    case SNB_ECCERRLOG_C0:
    case SNB_ECCERRLOG_C1:
        /* No errors by default */
        val = 0;
        break;
    case SNB_MADDIMM_C0:
    case SNB_MADDIMM_C1:
        /* 
         * Simulate DIMM configuration for SNB.
         * Size: 4GB (0x10 * 256MB)
         * Rank: 0 (1 rank)
         * Width: 0 (x8)
         * Value: 0x10
         */
        val = 0x10;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mchbar_read addr=0x%" PRIx64 " size=%u\n", 
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* MCHBAR Write Handler */
    /* PCIBaseState *s = opaque; */

    switch (addr) {
    case SNB_ECCERRLOG_C0:
    case SNB_ECCERRLOG_C1:
        /* Driver might write to clear errors, ignore for now */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mchbar_write addr=0x%" PRIx64 " val=0x%" PRIx64 " size=%u\n", 
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used by this driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used by this driver */
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    /* Reset MCHBAR config registers to default */
    pci_set_long(pdev->config + IE31200_MCHBAR_LOW, MCHBAR_BASE_ADDR | 1);
    pci_set_long(pdev->config + IE31200_MCHBAR_HIGH, 0);

    /* Reset ERRSTS */
    pci_set_word(pdev->config + IE31200_ERRSTS, 0);

    /* Reset CAPID0 to indicate ECC enabled, Dual Channel */
    /* Byte 1 (0xe5): PDCD=0 (Dual Channel) */
    /* Byte 3 (0xe7): ECC=0 (ECC Enabled) */
    pci_set_long(pdev->config + IE31200_CAPID0, 0);

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    /* No DMA used by this driver */
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    
    /* 
     * The driver reads MCHBAR from config space.
     * Ensure we return the fixed address we are emulating.
     */
    if (addr == IE31200_MCHBAR_LOW && len == 4) {
        val = MCHBAR_BASE_ADDR | 1; /* Bit 0 is enable */
    }

    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    /* Handle W1C for ERRSTS */
    if (addr == IE31200_ERRSTS && len == 2) {
        uint16_t current = pci_get_word(pdev->config + IE31200_ERRSTS);
        /* Clear bits that are set in val */
        current &= ~(val & IE31200_ERRSTS_BITS);
        pci_set_word(pdev->config + IE31200_ERRSTS, current);
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_IE31200_HB_1 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0); /* No IRQ pin used by default */
    
    /* Initialize MCHBAR Memory Region */
    memory_region_init_io(&s->mchbar_mr, OBJECT(s), &pcibase_mmio_ops, s, 
                          "ie31200-mchbar", MCHBAR_SIZE);
    
    /* Map MCHBAR into system memory at fixed address */
    memory_region_add_subregion(get_system_memory(), MCHBAR_BASE_ADDR, &s->mchbar_mr);

    /* register BARs (none standard) */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    /* Unmap MCHBAR */
    if (memory_region_is_mapped(&s->mchbar_mr)) {
        memory_region_del_subregion(get_system_memory(), &s->mchbar_mr);
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

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