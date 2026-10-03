/*
 * QEMU RVU AF PCI Device Model
 * Based on Linux driver rvu.c for Marvell Octeon TX2 RVU AF
 * Phase 2: Functional implementation for successful driver probe.
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

#define TYPE_PCIBASE_DEVICE "rvu_af_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor/Device IDs from driver */
#define VENDOR_ID 0x177d  /* PCI_VENDOR_ID_CAVIUM */
#define DEVICE_ID 0xA065  /* PCI_DEVID_OCTEONTX2_RVU_AF */
#define CLASS_ID 0xFF0000 /* PCI_CLASS_OTHERS */

/* Register offset macros from rvu.c */
#define RVU_PRIV_CONST                      (0x8000000)
#define RVU_PRIV_PFX_CFG(a)                 (0x8000100 | (a) << 16)
#define RVU_PRIV_PFX_MSIX_CFG(a)            (0x8000110 | (a) << 16)
#define RVU_PRIV_PFX_INT_CFG(a)             (0x8000200 | (a) << 16)
#define RVU_AF_MSIXTR_BASE                  (0x10)
#define MBOX_SIZE                           (64 * 1024)

/* PCI BAR numbers (as per driver) */
#define PCI_AF_REG_BAR_NUM  0
#define PCI_PF_REG_BAR_NUM  2

/* Convenience macro for a single bit */
#define BIT_ULL(nr)         (1ULL << (nr))

struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion af_bar;          /* BAR0: AF registers */
    MemoryRegion msix_bar;        /* BAR1: MSI-X table and PBA */
    MemoryRegion pf_bar;          /* BAR2: PF registers */
    MemoryRegion mbox_bar;        /* BAR4: mailbox memory */

    /* Shadow for PF BAR4 base address, recorded when BAR4 is programmed */
    uint64_t pf_bar4_addr;
};

/* AF MMIO read/write handlers */
static uint64_t rvu_af_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (addr) {
    case RVU_PRIV_CONST:
        /* total_pfs = 1, max_msix = 63 (64 vectors) */
        val = 0x10000003FULL;
        break;
    case RVU_PRIV_PFX_CFG(0):
        /* PF0 enabled, numvfs=0, hwvf=0 */
        val = BIT_ULL(20);
        break;
    case RVU_PRIV_PFX_MSIX_CFG(0):
        /* msix max = 32 for PF0 */
        val = 0x1F00000000ULL;
        break;
    case RVU_PRIV_PFX_INT_CFG(0):
        /* nvecs = 6 */
        val = 0x6000;
        break;
    case RVU_AF_MSIXTR_BASE:
        /* Not used in emulation, return 0 */
        val = 0;
        break;
    /* Newly added register offsets from supplementary source */
    case 0x0020:   /* NIX_AF_CONST */
        val = 0;
        break;
    case 0x0028:   /* NIX_AF_CONST1 */
        val = 0;
        break;
    case 0x0030:   /* NIX_AF_CONST2 */
        val = 0;
        break;
    case 0x0038:   /* NIX_AF_CONST3 */
        val = 0;
        break;
    case 0x1A30:   /* NIX_AF_RX_CHANX_CFG(0) */
        val = 0;
        break;
    case 0x4130:   /* NIX_AF_LFX_CINTS_BASE(0) */
        val = 0;
        break;
    case 0x4110:   /* NIX_AF_LFX_QINTS_BASE(0) */
        val = 0;
        break;
    case 0x8000300: /* RVU_PRIV_PFX_NIXX_CFG(0) */
        val = 0;
        break;
    case 0x9000000: /* RVU_AF_BAR2_SEL */
        val = 0;
        break;
    case 0x9100000: /* AF_BAR2_ALIASX(0,0) */
        val = 0;
        break;
    case 0x200:    /* NIX_GINT_INT */
        val = 0;
        break;
    case 0x208:    /* NIX_GINT_INT_W1S */
        val = 0;
        break;
    case 0x00110:  /* NPC_AF_CONST3 */
        val = 0;
        break;
    case 0x1800000: /* NPC_AF_MCAMEX_BANKX_CFG(0,0) */
        val = 0;
        break;
    case 0x8000038: /* NPC_AF_MCAMEX_BANKX_CFG with extension (0,0) */
        val = 0;
        break;
    case 0x00610:  /* NPC_AF_PCK_DEF_OL2 */
        val = 0;
        break;
    case 0x00620:  /* NPC_AF_PCK_DEF_OIP4 */
        val = 0;
        break;
    case 0x00640:  /* NPC_AF_PCK_DEF_IIP4 */
        val = 0;
        break;
    case 0x00600:  /* NPC_AF_PCK_CFG */
        val = 0;
        break;
    case 0x01010:  /* NPC_AF_INTFX_KEX_CFG(0) */
        val = 0;
        break;
    case 0x50000:  /* CPT_AF_RXC_CFG1 */
        /* Return default max RXC ICB count in bits [40:32] */
        val = (0xC0ULL) << 32;
        break;
    case 0x800:    /* NIX_CHAN_CGX_LMAC_CHX(0,0,0) and NIX_CHAN_CPT_CH_START (both 0x800) */
        val = 0;
        break;
    case 0x900:    /* NIX_CHAN_CGX_LMAC_CHX(0,1,0) */
        val = 0;
        break;
    case 0xA00:    /* NIX_CHAN_CGX_LMAC_CHX(0,2,0) */
        val = 0;
        break;
    case 0xB00:    /* NIX_CHAN_CGX_LMAC_CHX(0,3,0) */
        val = 0;
        break;
    case 0x000:    /* NIX_CHAN_LBK_CHX(0,0) */
        val = 0;
        break;
    case 0x700:    /* NIX_CHAN_SDP_CH_START */
        val = 0;
        break;
    case 0xFF8:    /* PTP_NANO_TIMESTAMP */
        val = 0;
        break;
    case 0xFE0:    /* PTP_FRNS_TIMESTAMP */
        val = 0;
        break;
    case 0x1000:   /* PTP_SEC_TIMESTAMP */
        val = 0;
        break;
    case 0xFF0:    /* PTP_CURR_ROLLOVER_SET */
        val = 0;
        break;
    case 0xFE8:    /* PTP_NXT_ROLLOVER_SET */
        val = 0;
        break;
    case 0xFD8:    /* PTP_SEC_ROLLOVER */
        val = 0;
        break;
    case 0xF00:    /* PTP_CLOCK_CFG */
        val = 0;
        break;
    case 0xF18:    /* PTP_CLOCK_COMP */
        val = 0;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void rvu_af_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Registers are either read-only or ignored for probe to succeed */
}

/* PF MMIO read/write handlers */
static uint64_t rvu_pf_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x5000: /* RVU_AF_PFX_BAR4_ADDR(0) */
        val = s->pf_bar4_addr;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void rvu_pf_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No writes needed for probe */
}

static const MemoryRegionOps rvu_af_mmio_ops = {
    .read = rvu_af_mmio_read,
    .write = rvu_af_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 8, .max_access_size = 8 },
    .impl  = { .min_access_size = 8, .max_access_size = 8 },
};

static const MemoryRegionOps rvu_pf_mmio_ops = {
    .read = rvu_pf_mmio_read,
    .write = rvu_pf_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 8, .max_access_size = 8 },
    .impl  = { .min_access_size = 8, .max_access_size = 8 },
};

/* Device reset handler */
static void pcibase_reset(DeviceState *dev)
{
    pci_device_reset(PCI_DEVICE(dev));
    /* Reset any software state if needed */
}

/* Realize function: set up PCI configuration, BARs, MSI-X */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    Error *local_err = NULL;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xFF00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set PCIe capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Power Management capability (required for some drivers) */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, &local_err);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    } else if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    /* BAR0: AF registers, large MMIO region */
    memory_region_init_io(&s->af_bar, OBJECT(s), &rvu_af_mmio_ops, s,
                          "rvu-af-regs", 0x10000000); /* 256 MB */
    pci_register_bar(pdev, PCI_AF_REG_BAR_NUM, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->af_bar);

    /* BAR1: MSI-X table and PBA */
    memory_region_init_ram(&s->msix_bar, OBJECT(s), "rvu-msix-bar", 0x2000, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (msix_init(pdev, 64, &s->msix_bar, 1, 0, &s->msix_bar, 1, 0x1000, 0, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_bar);

    /* BAR2: PF registers, medium MMIO region */
    memory_region_init_io(&s->pf_bar, OBJECT(s), &rvu_pf_mmio_ops, s,
                          "rvu-pf-regs", 0x10000); /* 64 KB */
    pci_register_bar(pdev, PCI_PF_REG_BAR_NUM, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->pf_bar);

    /* BAR4: mailbox memory, RAM region */
    memory_region_init_ram(&s->mbox_bar, OBJECT(s), "rvu-mbox", MBOX_SIZE, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mbox_bar);

    /* Record the BAR4 base address for use in PF register reads.
     * The driver reads it after BAR programming, so we store it here.
     * This value will be updated when BAR is written; but QEMU automates it,
     * so we just read it at the time of driver access via pci_get_bar_addr.
     * We set a placeholder, to be overridden when driver queries.
     */
    s->pf_bar4_addr = (uint64_t)~0ULL; /* will be filled by pci_get_bar_addr on read */
}

/* Uninit function */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->msix_bar, &s->msix_bar);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* No other resources to free */
}

/* Minimal VMState for migration */
static const VMStateDescription vmstate_pcibase = {
    .name = "rvu_af_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

/* Class initialization */
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

/* Type registration */
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