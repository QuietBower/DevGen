/*
 * QEMU PCI device model for igen6_edac (EHL SKU5)
 * Implements config space registers and MCHBAR MMIO region
 * to allow the Linux igen6_edac driver to probe and bind.
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

#define TYPE_PCIBASE_DEVICE "igen6_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL              0x8086
#define PCI_DEVICE_ID_IGEN6_EHL_SKU5     0x4514
#define PCI_CLASS_MEMORY_CONTROLLER      0x0580

#define MCHBAR_SIZE                      0x10000

/* Register offsets in MCHBAR MMIO region (based on EHL configuration) */
#define TOM_OFFSET                       0xa0
#define TOUUD_OFFSET                     0xa8
#define TOLUD_OFFSET                     0xbc
#define CAPID_C_OFFSET                   0xec
#define CAPID_E_OFFSET                   0xf0
#define ERRSTS_OFFSET                    0xc8
#define ERRCMD_OFFSET                    0xca
#define IBECC_BASE                       0xdc00
#define IBECC_ACTIVATE_OFFSET            IBECC_BASE
#define ECC_ERROR_LOG_OFFSET             (IBECC_BASE + 0x170)
#define IMC_BASE                         0x5000
#define MAD_INTER_CHANNEL_OFFSET         IMC_BASE
#define MAD_INTRA_CH0_OFFSET             (IMC_BASE + 4)
#define MAD_DIMM_CH0_OFFSET              (IMC_BASE + 0xc)
#define MAD_MC_HASH_OFFSET               (IMC_BASE + 0x1b8)
#define CHANNEL_HASH_OFFSET              (IMC_BASE + 0x24)
#define CHANNEL_EHASH_OFFSET             (IMC_BASE + 0x28)

/* Bit masks and control bits */
#define ERRSTS_CE                        BIT_ULL(6)
#define ERRSTS_UE                        BIT_ULL(7)
#define ERRCMD_CE                        BIT_ULL(6)
#define ERRCMD_UE                        BIT_ULL(7)
#define IBECC_ACTIVATE_EN                BIT(0)
#define ECC_ERROR_LOG_CE                 BIT_ULL(62)
#define ECC_ERROR_LOG_UE                 BIT_ULL(63)
#define CAPID_C_IBECC                    BIT(15)
#define CAPID_E_IBECC                    BIT(12)
#define CAPID_E_IBECC_BIT18              BIT(18)

/* QEMU-compatible bit macros */
#ifndef BIT
#define BIT(nr) (1ULL << (nr))
#endif
#ifndef BIT_ULL
#define BIT_ULL BIT
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
    struct {
        uint32_t capid_c;         /* 0xec */
        uint32_t capid_e;         /* 0xf0 */
        uint64_t errsts;          /* 0xc8 */
        uint16_t errcmd;          /* 0xca */
        uint32_t ibecc_activate;  /* 0xdc00 */
        uint64_t ecc_error_log;   /* 0xdd70 */
        uint32_t mad_inter_channel; /* 0x5000 */
        uint32_t mad_intra_ch0;    /* 0x5004 */
        uint32_t mad_dimm_ch0;     /* 0x500c */
        uint32_t mad_mc_hash;      /* 0x51b8 */
        uint32_t channel_hash;     /* 0x5024 */
        uint32_t channel_ehash;    /* 0x5028 */
        /* Additional registers will be added as needed during behavioral modeling */
    } regs;

    /* Custom config space registers */
    uint32_t capid_c;
    uint32_t capid_e;
    uint16_t errsts;
    uint16_t errcmd;
    uint64_t tom;
    uint32_t tolud;
    uint64_t touud; /* for debug */

    /* MMIO shadow registers for MCHBAR window */
    struct {
        uint32_t mad_inter;
        uint32_t mad_intra[2];
        uint32_t mad_dimm[2];
        uint32_t channel_hash;
        uint32_t channel_ehash;
        uint32_t mad_mc_hash;
        uint32_t ibecc_activate;
        uint64_t ecc_error_log;
    } mmio;
};

/* Custom PCI config read handler */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;

    /* MCHBAR register at offset 0x48 (lower 32 bits) */
    if (address == 0x48) {
        val = pci_get_long(pdev->config + PCI_BASE_ADDRESS_0);
        val |= 1; /* MCHBAR_EN bit 0 */
        return val;
    }
    /* MCHBAR upper 32 bits at offset 0x4C (if BAR0 is 64-bit) */
    else if (address == 0x4C) {
        if (pci_get_long(pdev->config + PCI_BASE_ADDRESS_0) & PCI_BASE_ADDRESS_MEM_TYPE_64) {
            val = pci_get_long(pdev->config + PCI_BASE_ADDRESS_0 + 4);
            return val;
        } else {
            return 0;
        }
    }
    /* CAPID_C */
    else if (address == 0xec) {
        return s->capid_c;
    }
    /* CAPID_E */
    else if (address == 0xf0) {
        return s->capid_e;
    }
    /* ERRSTS */
    else if (address == 0xc8) {
        return s->errsts;
    }
    /* ERRCMD */
    else if (address == 0xca) {
        return s->errcmd;
    }
    /* TOM (64-bit, low at 0xa0, high at 0xa4) */
    else if (address == 0xa0) {
        return (uint32_t)(s->tom & 0xFFFFFFFF);
    }
    else if (address == 0xa4) {
        return (uint32_t)(s->tom >> 32);
    }
    /* TOLUD */
    else if (address == 0xbc) {
        return s->tolud;
    }
    /* TOUUD (low/high) for debug */
    else if (address == 0xa8) {
        return (uint32_t)(s->touud & 0xFFFFFFFF);
    }
    else if (address == 0xac) {
        return (uint32_t)(s->touud >> 32);
    }
    else {
        return pci_default_read_config(pdev, address, len);
    }
}

/* Custom PCI config write handler */
static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (address == 0xca) { /* ERRCMD */
        s->errcmd = val & 0xFFFF;
    } else if (address == 0xc8) { /* ERRSTS - write 1 to clear */
        s->errsts &= ~val;
    } else if (address == 0xa0 || address == 0xa4 || address == 0xbc ||
               address == 0xec || address == 0xf0 || address == 0xa8 ||
               address == 0xac) {
        /* Read-only registers */
    } else {
        pci_default_write_config(pdev, address, val, len);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MAD_INTER_CHANNEL_OFFSET:
        val = s->mmio.mad_inter;
        break;
    case MAD_INTRA_CH0_OFFSET:
        val = s->mmio.mad_intra[0];
        break;
    case MAD_INTRA_CH0_OFFSET + 4:
        val = s->mmio.mad_intra[1];
        break;
    case MAD_DIMM_CH0_OFFSET:
        val = s->mmio.mad_dimm[0];
        break;
    case MAD_DIMM_CH0_OFFSET + 4:
        val = s->mmio.mad_dimm[1];
        break;
    case CHANNEL_HASH_OFFSET:
        val = s->mmio.channel_hash;
        break;
    case CHANNEL_EHASH_OFFSET:
        val = s->mmio.channel_ehash;
        break;
    case MAD_MC_HASH_OFFSET:
        val = s->mmio.mad_mc_hash;
        break;
    case IBECC_ACTIVATE_OFFSET:
        val = s->mmio.ibecc_activate;
        break;
    case ECC_ERROR_LOG_OFFSET:
        val = s->mmio.ecc_error_log;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ECC_ERROR_LOG_OFFSET:
        /* Driver writes ECClog to clear CE/UE bits; just store it */
        s->mmio.ecc_error_log = val;
        break;
    default:
        /* Other registers are read-only */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO used by this driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO used by this driver */
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

    /* Reset config space registers to power-on defaults */
    s->capid_c = 0x8000; /* bit15 set (IBECC available) */
    s->capid_e = 0;
    s->errsts = 0;
    s->errcmd = 0;
    s->tom = 0x200000000ULL;   /* 8GB */
    s->tolud = 0x80000000;     /* 2GB */
    s->touud = 0;

    /* Reset MMIO register shadows */
    s->mmio.mad_inter = 0x00010001;   /* DDR4, non-zero channel sizes */
    s->mmio.mad_intra[0] = 0x00000001;
    s->mmio.mad_intra[1] = 0x00000001;
    s->mmio.mad_dimm[0] = 0x00010001;
    s->mmio.mad_dimm[1] = 0x00010001;
    s->mmio.channel_hash = 0x00000001;
    s->mmio.channel_ehash = 0x00000001;
    s->mmio.mad_mc_hash = 0x00000001;
    s->mmio.ibecc_activate = 0x1;     /* ECC enabled */
    s->mmio.ecc_error_log = 0;
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
        /* Use 64-bit BAR to accommodate the driver's MCHBAR access */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x4514 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0580 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = MCHBAR_SIZE, .name = "mchbar" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Override config read/write for custom registers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* Final state initialization before the device is 'live' */
    pcibase_reset(DEVICE(s));
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

    /* Free resources (none in this model) */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "igen6_edac_pci",
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
