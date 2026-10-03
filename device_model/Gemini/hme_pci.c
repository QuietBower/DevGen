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


#define TYPE_PCIBASE_DEVICE "hme_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define TCVR_BBCLOCK    0x00UL
#define TCVR_BBDATA     0x04UL
#define TCVR_BBOENAB    0x08UL
#define TCVR_FRAME      0x0cUL
#define TCVR_CFG        0x10UL
#define GREG_SWRESET    0x000UL
#define GREG_CFG        0x004UL
#define GREG_STAT       0x100UL
#define GREG_IMASK      0x104UL
#define ERX_CFG         0x00UL
#define ERX_RING        0x04UL
#define ETX_PENDING     0x00UL
#define ETX_CFG         0x04UL
#define ETX_RING        0x08UL
#define ETX_RSIZE       0x2cUL
#define BMAC_XIFCFG     0x0000UL
#define BMAC_TXSWRESET  0x208UL
#define BMAC_TXCFG      0x20cUL
#define BMAC_IGAP1      0x210UL
#define BMAC_IGAP2      0x214UL
#define BMAC_ALIMIT     0x218UL
#define BMAC_JSIZE      0x22cUL
#define BMAC_TXMAX      0x230UL
#define BMAC_EXCTR      0x248UL
#define BMAC_LTCTR      0x24cUL
#define BMAC_RSEED      0x250UL
#define BMAC_RXSWRESET  0x308UL
#define BMAC_RXCFG      0x30cUL
#define BMAC_RXMAX      0x310UL
#define BMAC_MACADDR2   0x318UL
#define BMAC_MACADDR1   0x31cUL
#define BMAC_MACADDR0   0x320UL
#define BMAC_GLECTR     0x328UL
#define BMAC_UNALECTR   0x32cUL
#define BMAC_RCRCECTR   0x330UL
#define BMAC_HTABLE3    0x340UL
#define BMAC_HTABLE2    0x344UL
#define BMAC_HTABLE1    0x348UL
#define BMAC_HTABLE0    0x34cUL

#define GREG_RESET_ALL         0x03
#define ETX_TP_DMAWAKEUP       0x00000001
#define GREG_STAT_TXALL        0x02000000
#define TXFLAG_OWN             0x80000000

#define MII_BMCR		0x00
#define MII_BMSR		0x01
#define MII_PHYSID1		0x02
#define MII_PHYSID2		0x03
#define MII_ADVERTISE		0x04
#define MII_LPA			0x05
#define BMCR_SPEED100		0x2000
#define BMCR_FULLDPLX		0x0100
#define BMSR_LSTATUS		0x0004
#define BMSR_ANEGCOMPLETE	0x0020
#define BMSR_100FULL		0x4000
#define ADVERTISE_10HALF	0x0020
#define ADVERTISE_10FULL	0x0040
#define ADVERTISE_100HALF	0x0080
#define ADVERTISE_100FULL	0x0100
#define LPA_10HALF		0x0020
#define LPA_10FULL		0x0040
#define LPA_100HALF		0x0080
#define LPA_100FULL		0x0100
#define DP83840_CSCONFIG        0x17

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
    uint32_t greg_imask;
    uint32_t greg_stat;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t gregs[0x108 / 4 + 1];
    uint32_t etxregs[0x2c / 4 + 1];
    uint32_t erxregs[0x08 / 4 + 1];
    uint32_t bigmacregs[0x34c / 4 + 1];
    uint32_t tcvregs[0x10 / 4 + 1];

    /* DMA Context */
    uint32_t erx_ring;
    uint32_t etx_ring;

    uint32_t etx_pending;
};

typedef uint32_t hme32;

struct happy_meal_rxd {
    hme32 rx_flags;
    hme32 rx_addr;
};

struct happy_meal_txd {
    hme32 tx_flags;
    hme32 tx_addr;
};

#define RX_RING_MAXSIZE    256
#define TX_RING_MAXSIZE    256

struct hmeal_init_block {
    struct happy_meal_rxd happy_meal_rxd[RX_RING_MAXSIZE];
    struct happy_meal_txd happy_meal_txd[TX_RING_MAXSIZE];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t stat = s->gregs[GREG_STAT / 4];
    uint32_t imask = s->gregs[GREG_IMASK / 4];

    if (stat & ~imask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (is_write) {
        uint32_t tx_ring = s->etxregs[ETX_RING / 4];
        if (tx_ring != 0) {
            struct happy_meal_txd txd;
            for (int i = 0; i < TX_RING_MAXSIZE; i++) {
                pci_dma_read(pdev, tx_ring + i * sizeof(txd), &txd, sizeof(txd));
                if (txd.tx_flags & TXFLAG_OWN) {
                    txd.tx_flags &= ~TXFLAG_OWN;
                    pci_dma_write(pdev, tx_ring + i * sizeof(txd), &txd, sizeof(txd));
                    s->gregs[GREG_STAT / 4] |= GREG_STAT_TXALL;
                    pcibase_update_irq(s);
                }
            }
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x2000) {
        val = s->gregs[addr / 4];
        if (addr == GREG_STAT) {
            s->gregs[GREG_STAT / 4] = 0;
            pcibase_update_irq(s);
        }
    } else if (addr < 0x4000) {
        val = s->etxregs[(addr - 0x2000) / 4];
    } else if (addr < 0x6000) {
        val = s->erxregs[(addr - 0x4000) / 4];
    } else if (addr < 0x7000) {
        val = s->bigmacregs[(addr - 0x6000) / 4];
    } else if (addr < 0x8000) {
        val = s->tcvregs[(addr - 0x7000) / 4];
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x2000) {
        s->gregs[addr / 4] = val;
        if (addr == GREG_SWRESET) {
            if (val & GREG_RESET_ALL) {
                s->gregs[GREG_SWRESET / 4] &= ~GREG_RESET_ALL;
            }
        } else if (addr == GREG_IMASK) {
            pcibase_update_irq(s);
        }
    } else if (addr < 0x4000) {
        s->etxregs[(addr - 0x2000) / 4] = val;
        if (addr == 0x2000 + ETX_PENDING) {
            if (val == ETX_TP_DMAWAKEUP) {
                pcibase_do_dma(s, true);
            }
        }
    } else if (addr < 0x6000) {
        s->erxregs[(addr - 0x4000) / 4] = val;
    } else if (addr < 0x7000) {
        s->bigmacregs[(addr - 0x6000) / 4] = val;
        if (addr == 0x6000 + BMAC_TXSWRESET) {
            s->bigmacregs[BMAC_TXSWRESET / 4] &= ~1;
        } else if (addr == 0x6000 + BMAC_RXSWRESET) {
            s->bigmacregs[BMAC_RXSWRESET / 4] &= ~1;
        }
    } else if (addr < 0x8000) {
        s->tcvregs[(addr - 0x7000) / 4] = val;
        if (addr == 0x7000 + TCVR_FRAME) {
            uint32_t op = (val >> 28) & 0xf;
            uint32_t reg = (val >> 18) & 0x1f;
            if (op == 6) { /* Read */
                uint32_t res = 0xffff;
                switch (reg) {
                    case MII_BMCR: res = BMCR_SPEED100 | BMCR_FULLDPLX; break;
                    case MII_BMSR: res = BMSR_LSTATUS | BMSR_ANEGCOMPLETE | BMSR_100FULL; break;
                    case MII_PHYSID1: res = 0x0000; break;
                    case MII_PHYSID2: res = 0x0000; break;
                    case MII_ADVERTISE: res = ADVERTISE_100FULL | ADVERTISE_100HALF | ADVERTISE_10FULL | ADVERTISE_10HALF; break;
                    case MII_LPA: res = LPA_100FULL | LPA_100HALF | LPA_10FULL | LPA_10HALF; break;
                    case DP83840_CSCONFIG: res = 0; break;
                }
                s->tcvregs[TCVR_FRAME / 4] = (val & 0xffff0000) | 0x20000 | res;
            } else if (op == 5) { /* Write */
                s->tcvregs[TCVR_FRAME / 4] = val | 0x20000;
            }
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    return val;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->gregs, 0, sizeof(s->gregs));
    memset(s->etxregs, 0, sizeof(s->etxregs));
    memset(s->erxregs, 0, sizeof(s->erxregs));
    memset(s->bigmacregs, 0, sizeof(s->bigmacregs));
    memset(s->tcvregs, 0, sizeof(s->tcvregs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SUN );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SUN_HME );
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
    s->bar_info[0].size = 0x8000;
    s->bar_info[0].name = "hme-regs";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "hme_pci",
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
