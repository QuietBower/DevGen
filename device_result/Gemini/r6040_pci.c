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

/* Additional include files retrieved from driver context */
#define PCI_VENDOR_ID_RDC		0x17f3

#define TYPE_PCIBASE_DEVICE "r6040_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define R6040_IO_SIZE	256
#define MCR0		0x00
#define MCR1		0x04
#define MBCR		0x08
#define MT_ICR		0x0C
#define MR_ICR		0x10
#define MTPR		0x14
#define MR_BSR		0x18
#define MR_DCR		0x1A
#define MLSR		0x1C
#define MMDIO		0x20
#define MMRD		0x24
#define MMWD		0x28
#define MTD_SA0		0x2C
#define MTD_SA1		0x30
#define MRD_SA0		0x34
#define MRD_SA1		0x38
#define MISR		0x3C
#define MIER		0x40
#define ME_CISR		0x44
#define ME_CIER		0x48
#define MR_CNT		0x50
#define ME_CNT0		0x52
#define ME_CNT1		0x54
#define ME_CNT2		0x56
#define ME_CNT3		0x58
#define MT_CNT		0x5A
#define ME_CNT4		0x5C
#define MP_CNT		0x5E
#define MAR0		0x60
#define MAR1		0x62
#define MAR2		0x64
#define MAR3		0x66
#define MID_0L		0x68
#define MID_0M		0x6A
#define MID_0H		0x6C
#define MID_1L		0x70
#define MID_1M		0x72
#define MID_1H		0x74
#define MID_2L		0x78
#define MID_2M		0x7A
#define MID_2H		0x7C
#define MID_3L		0x80
#define MID_3M		0x82
#define MID_3H		0x84
#define PHY_CC		0x88
#define PHY_ST		0x8A
#define MAC_SM		0xAC
#define MD_CSC		0xb6
#define MAC_ID		0xBE

#define MAC_RST	0x0001
#define TM2TX		0x0001
#define MDIO_READ	0x2000
#define MDIO_WRITE	0x4000
#define DSC_OWNER_MAC	0x8000
#define TX_FINISH	0x0010
#define TX_INTS			(TX_FINISH)

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
    uint16_t misr;
    uint16_t mier;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t mcr0;
    uint16_t mcr1;
    uint16_t mbcr;
    uint16_t mt_icr;
    uint16_t mr_icr;
    uint16_t mtpr;
    uint16_t mr_bsr;
    uint16_t mr_dcr;
    uint16_t mlsr;
    uint16_t mmdio;
    uint16_t mmrd;
    uint16_t mmwd;
    uint16_t me_cisr;
    uint16_t me_cier;
    uint16_t phy_cc;
    uint16_t phy_st;
    uint16_t mac_sm;
    uint16_t md_csc;
    uint16_t mac_id;
    uint16_t mar[4];
    uint16_t mid[12];
    uint16_t cnt[8];

    /* DMA Context */
    uint32_t mtd_sa0;
    uint32_t mtd_sa1;
    uint32_t mrd_sa0;
    uint32_t mrd_sa1;
};

struct r6040_descriptor {
    uint16_t status;
    uint16_t len;
    uint32_t buf;
    uint32_t ndesc;
    uint32_t rev1;
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->misr & s->mier) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (is_write) {
        uint32_t desc_addr = ((uint32_t)s->mtd_sa1 << 16) | s->mtd_sa0;
        struct r6040_descriptor desc;
        int count = 0;

        while (count++ < 64) {
            pci_dma_read(pdev, desc_addr, &desc, sizeof(desc));
            if (!(le16_to_cpu(desc.status) & DSC_OWNER_MAC)) {
                break;
            }

            /* Clear owner bit to hand back to CPU */
            desc.status = cpu_to_le16(le16_to_cpu(desc.status) & ~DSC_OWNER_MAC);
            pci_dma_write(pdev, desc_addr, &desc, sizeof(desc));

            s->misr |= TX_INTS;

            desc_addr = le32_to_cpu(desc.ndesc);
            s->mtd_sa0 = desc_addr & 0xFFFF;
            s->mtd_sa1 = (desc_addr >> 16) & 0xFFFF;
        }
        pcibase_update_irq(s);
    }
}

/* MMIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= MAR0 && addr <= MAR3 + 1) {
        return s->mar[(addr - MAR0) / 2];
    }
    if (addr >= ME_CNT0 && addr <= MP_CNT + 1) {
        return s->cnt[(addr - ME_CNT0) / 2];
    }

    switch (addr) {
    case MCR0: val = s->mcr0; break;
    case MCR1: val = s->mcr1; break;
    case MBCR: val = s->mbcr; break;
    case MT_ICR: val = s->mt_icr; break;
    case MR_ICR: val = s->mr_icr; break;
    case MTPR: val = s->mtpr; break;
    case MR_BSR: val = s->mr_bsr; break;
    case MR_DCR: val = s->mr_dcr; break;
    case MLSR: val = s->mlsr; break;
    case MMDIO: val = s->mmdio; break;
    case MMRD: val = s->mmrd; break;
    case MMWD: val = s->mmwd; break;
    case MTD_SA0: val = s->mtd_sa0; break;
    case MTD_SA1: val = s->mtd_sa1; break;
    case MRD_SA0: val = s->mrd_sa0; break;
    case MRD_SA1: val = s->mrd_sa1; break;
    case MISR: 
        val = s->misr; 
        s->misr = 0; /* Read to clear */
        pcibase_update_irq(s);
        break;
    case MIER: val = s->mier; break;
    case ME_CISR: val = s->me_cisr; break;
    case ME_CIER: val = s->me_cier; break;
    case PHY_CC: val = s->phy_cc; break;
    case PHY_ST: val = s->phy_st; break;
    case MAC_SM: val = s->mac_sm; break;
    case MD_CSC: val = s->md_csc; break;
    case MAC_ID: val = s->mac_id; break;
    case MID_0L: val = s->mid[0]; break;
    case MID_0M: val = s->mid[1]; break;
    case MID_0H: val = s->mid[2]; break;
    case MID_1L: val = s->mid[3]; break;
    case MID_1M: val = s->mid[4]; break;
    case MID_1H: val = s->mid[5]; break;
    case MID_2L: val = s->mid[6]; break;
    case MID_2M: val = s->mid[7]; break;
    case MID_2H: val = s->mid[8]; break;
    case MID_3L: val = s->mid[9]; break;
    case MID_3M: val = s->mid[10]; break;
    case MID_3H: val = s->mid[11]; break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= MAR0 && addr <= MAR3 + 1) {
        s->mar[(addr - MAR0) / 2] = val;
        return;
    }
    if (addr >= ME_CNT0 && addr <= MP_CNT + 1) {
        s->cnt[(addr - ME_CNT0) / 2] = val;
        return;
    }

    switch (addr) {
    case MCR0: s->mcr0 = val; break;
    case MCR1: 
        s->mcr1 = val; 
        if (val & MAC_RST) {
            s->mcr1 &= ~MAC_RST; /* Auto-clear reset */
        }
        break;
    case MBCR: s->mbcr = val; break;
    case MT_ICR: s->mt_icr = val; break;
    case MR_ICR: s->mr_icr = val; break;
    case MTPR: 
        s->mtpr = val; 
        if (val == TM2TX) {
            pcibase_do_dma(s, true);
        }
        break;
    case MR_BSR: s->mr_bsr = val; break;
    case MR_DCR: s->mr_dcr = val; break;
    case MLSR: s->mlsr = val; break;
    case MMDIO: 
        s->mmdio = val; 
        if (val & MDIO_READ) {
            s->mmdio &= ~MDIO_READ; /* Auto-clear */
            int phy = (val >> 8) & 0x1F;
            int reg = val & 0x1F;
            if (phy == 0 || phy == 1) {
                if (reg == 1) s->mmrd = 0x786d; /* BMSR: link up, auto-neg complete */
                else if (reg == 2) s->mmrd = 0x0022; /* PHY ID 1 */
                else if (reg == 3) s->mmrd = 0x1512; /* PHY ID 2 */
                else s->mmrd = 0;
            } else {
                s->mmrd = 0xFFFF;
            }
        }
        if (val & MDIO_WRITE) {
            s->mmdio &= ~MDIO_WRITE; /* Auto-clear */
        }
        break;
    case MMRD: s->mmrd = val; break;
    case MMWD: s->mmwd = val; break;
    case MTD_SA0: s->mtd_sa0 = val & 0xFFFF; break;
    case MTD_SA1: s->mtd_sa1 = val & 0xFFFF; break;
    case MRD_SA0: s->mrd_sa0 = val & 0xFFFF; break;
    case MRD_SA1: s->mrd_sa1 = val & 0xFFFF; break;
    case MISR: 
        /* Driver reads to clear, ignore writes */
        break;
    case MIER: 
        s->mier = val; 
        pcibase_update_irq(s);
        break;
    case ME_CISR: s->me_cisr = val; break;
    case ME_CIER: s->me_cier = val; break;
    case PHY_CC: s->phy_cc = val; break;
    case PHY_ST: s->phy_st = val; break;
    case MAC_SM: s->mac_sm = val; break;
    case MD_CSC: s->md_csc = val; break;
    case MAC_ID: s->mac_id = val; break;
    case MID_0L: s->mid[0] = val; break;
    case MID_0M: s->mid[1] = val; break;
    case MID_0H: s->mid[2] = val; break;
    case MID_1L: s->mid[3] = val; break;
    case MID_1M: s->mid[4] = val; break;
    case MID_1H: s->mid[5] = val; break;
    case MID_2L: s->mid[6] = val; break;
    case MID_2M: s->mid[7] = val; break;
    case MID_2H: s->mid[8] = val; break;
    case MID_3L: s->mid[9] = val; break;
    case MID_3M: s->mid[10] = val; break;
    case MID_3H: s->mid[11] = val; break;
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
    
    s->mcr0 = 0;
    s->mcr1 = 0;
    s->mbcr = 0;
    s->misr = 0;
    s->mier = 0;
    s->phy_cc = 0;
    s->md_csc = 0;

    /* Provide a default MAC address to avoid uninitialized warning */
    s->mid[0] = 0x5452;
    s->mid[1] = 0x1200;
    s->mid[2] = 0x3456;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_RDC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x6040 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = R6040_IO_SIZE;
    s->bar_info[0].name = "r6040-bar0";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
    .name = "r6040_pci",
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
