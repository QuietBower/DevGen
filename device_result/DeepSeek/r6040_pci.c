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

#define TYPE_PCIBASE_DEVICE "r6040_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x17F3
#define DEVICE_ID 0x6040
#define CLASS_ID 0x020000

#define DRV_NAME	"r6040"
#define DRV_VERSION	"0.29"
#define DRV_RELDATE	"04Jul2016"
#define TX_TIMEOUT	(6000 * HZ / 1000)
#define RX_FIFO_FULL	0x0004
#define MDIO_READ	0x2000
#define MDIO_WRITE	0x4000
#define MAX_BUF_SIZE	0x600
#define TX_DESC_SIZE	(TX_DCNT * sizeof(struct r6040_descriptor))
#define RX_DESC_SIZE	(RX_DCNT * sizeof(struct r6040_descriptor))
#define R6040_IO_SIZE	256
#define MAX_MAC		2
#define MCR0		0x00
#define MCR0_RCVEN	0x0002
#define MCR0_PROMISC	0x0020
#define MCR0_HASH_EN	0x0100
#define MCR0_XMTEN	0x1000
#define MCR0_FD	0x8000
#define MCR1		0x04
#define MAC_RST	0x0001
#define MBCR		0x08
#define MT_ICR		0x0C
#define MR_ICR		0x10
#define MTPR		0x14
#define TM2TX		0x0001
#define MR_BSR		0x18
#define MR_DCR		0x1A
#define MLSR		0x1C
#define TX_FIFO_UNDR	0x0200
#define TX_EXCEEDC	0x2000
#define TX_LATEC	0x4000
#define MMDIO		0x20
#define MMRD		0x24
#define MMWD		0x28
#define MTD_SA0		0x2C
#define MTD_SA1		0x30
#define MRD_SA0		0x34
#define MRD_SA1		0x38
#define MISR		0x3C
#define MIER		0x40
#define MSK_INT	0x0000
#define RX_FINISH	0x0001
#define RX_NO_DESC	0x0002
#define RX_EARLY	0x0008
#define TX_FINISH	0x0010
#define TX_EARLY	0x0080
#define EVENT_OVRFL	0x0100
#define LINK_CHANGED	0x0200
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
#define SCEN		0x8000
#define PHYAD_SHIFT	8
#define TMRDIV_SHIFT	0
#define PHY_ST		0x8A
#define MAC_SM		0xAC
#define MAC_SM_RST	0x0002
#define MD_CSC		0xb6
#define MD_CSC_DEFAULT	0x0030
#define MAC_ID		0xBE
#define TX_DCNT		0x80
#define RX_DCNT		0x80
#define MBCR_DEFAULT	0x012A
#define MCAST_MAX	3
#define MAC_DEF_TIMEOUT	2048
#define DSC_OWNER_MAC	0x8000
#define DSC_RX_OK	0x4000
#define DSC_RX_ERR	0x0800
#define DSC_RX_ERR_DRI	0x0400
#define DSC_RX_ERR_BUF	0x0200
#define DSC_RX_ERR_LONG	0x0100
#define DSC_RX_ERR_RUNT	0x0080
#define DSC_RX_ERR_CRC	0x0040
#define DSC_RX_BCAST	0x0020
#define DSC_RX_MCAST	0x0010
#define DSC_RX_MCH_HIT	0x0008
#define DSC_RX_MIDH_HIT	0x0004
#define DSC_RX_IDX_MID_MASK 3
#define RX_INTS			(RX_FIFO_FULL | RX_NO_DESC | RX_FINISH)
#define TX_INTS			(TX_FINISH)
#define INT_MASK		(RX_INTS | TX_INTS)

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
    uint16_t intr_status;
    uint16_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[256];

    /* DMA Context */
    uint32_t tx_desc_addr;
    uint32_t rx_desc_addr;

    uint16_t mcr0;

    bool reset_active;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 256) return 0;

    switch (addr) {
    case MISR: /* Interrupt Status Register - clear-on-read */
        val = s->intr_status;
        s->intr_status = 0;
        pcibase_update_irq(s);
        break;
    default:
        if (size == 1) {
            val = s->regs[addr];
        } else if (size == 2) {
            val = s->regs[addr] | (s->regs[addr+1] << 8);
        } else if (size == 4) {
            val = s->regs[addr] | (s->regs[addr+1] << 8) | (s->regs[addr+2] << 16) | (s->regs[addr+3] << 24);
        } else {
            /* unsupported size */
        }
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 256) return;

    /* Store value into regs first, to simplify default behavior */
    if (size == 1) {
        s->regs[addr] = val & 0xFF;
    } else if (size == 2) {
        s->regs[addr] = val & 0xFF;
        s->regs[addr+1] = (val >> 8) & 0xFF;
    } else if (size == 4) {
        s->regs[addr] = val & 0xFF;
        s->regs[addr+1] = (val >> 8) & 0xFF;
        s->regs[addr+2] = (val >> 16) & 0xFF;
        s->regs[addr+3] = (val >> 24) & 0xFF;
    } else {
        return;
    }

    switch (addr) {
    case MMDIO: /* MDIO Command Register */
        {
            uint16_t cmd = s->regs[MMDIO] | (s->regs[MMDIO+1] << 8);
            uint8_t phy_addr = (cmd >> 8) & 0x1F;
            uint8_t reg = cmd & 0x1F;

            if (cmd & MDIO_READ) {
                /* Emulate read from PHY address 1 */
                uint16_t result = 0xFFFF;
                if (phy_addr == 1) {
                    switch (reg) {
                    case 1:  result = 0x786D; break; /* BMSR */
                    case 2:  result = 0x0040; break; /* PHY ID1 */
                    case 3:  result = 0x0040; break; /* PHY ID2 */
                    default: result = 0x0000; break;
                    }
                }
                s->regs[MMRD]   = result & 0xFF;
                s->regs[MMRD+1] = (result >> 8) & 0xFF;
                /* Clear MDIO_READ bit */
                s->regs[MMDIO+1] &= (uint8_t)~0x20;
            } else if (cmd & MDIO_WRITE) {
                /* MDIO write: just acknowledge, data already placed in MMWD by prior write */
                s->regs[MMDIO+1] &= (uint8_t)~0x40;
            }
        }
        break;
    case MIER: /* Interrupt Enable Register */
        s->intr_mask = val & 0xFFFF;
        pcibase_update_irq(s);
        break;
    default:
        break;
    }
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

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->mcr0 = 0;
    s->reset_active = false;

    /* Set default MAC address to 00:11:22:33:44:55 */
    s->regs[MID_0L]   = 0x00;
    s->regs[MID_0L+1] = 0x11;
    s->regs[MID_0M]   = 0x22;
    s->regs[MID_0M+1] = 0x33;
    s->regs[MID_0H]   = 0x44;
    s->regs[MID_0H+1] = 0x55;
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
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 256, .name = "r6040-pio" };
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