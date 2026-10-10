/*
 * QEMU model of IXGBE VF (82599) network card
 * Based on Linux ixgbevf_main.c driver
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

#define TYPE_PCIBASE_DEVICE "ixgbevf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID          0x8086
#define DEVICE_ID          0x10ED
#define CLASS_ID           0x0200

/* Register Offsets */
#define IXGBE_VFCTRL           0x00000
#define IXGBE_VFSTATUS         0x00008
#define IXGBE_VFLINKS          0x00010
#define IXGBE_VFFRTIMER        0x00048
#define IXGBE_VTEICR           0x00100
#define IXGBE_VTEICS           0x00104
#define IXGBE_VTEIMS           0x00108
#define IXGBE_VTEIMC           0x0010C
#define IXGBE_VTEIAC           0x00110
#define IXGBE_VTEIAM           0x00114
#define IXGBE_VTIVAR_MISC      0x00140
#define IXGBE_VTIVAR(x)        (0x00120 + (4 * (x)))
#define IXGBE_VTEITR(x)        (0x00820 + (4 * (x)))
#define IXGBE_VFPSRTYPE        0x00300
#define IXGBE_VFRDBAL(x)       (0x01000 + (0x40 * (x)))
#define IXGBE_VFRDBAH(x)       (0x01004 + (0x40 * (x)))
#define IXGBE_VFRDLEN(x)       (0x01008 + (0x40 * (x)))
#define IXGBE_VFDCA_RXCTRL(x)  (0x0100C + (0x40 * (x)))
#define IXGBE_VFRDH(x)         (0x01010 + (0x40 * (x)))
#define IXGBE_VFSRRCTL(x)      (0x01014 + (0x40 * (x)))
#define IXGBE_VFRDT(x)         (0x01018 + (0x40 * (x)))
#define IXGBE_VFGORC_LSB       0x01020
#define IXGBE_VFGORC_MSB       0x01024
#define IXGBE_VFRXDCTL(x)      (0x01028 + (0x40 * (x)))
#define IXGBE_VFMPRC           0x01034
#define IXGBE_VFGPRC           0x0101C
#define IXGBE_VFTDBAL(x)       (0x02000 + (0x40 * (x)))
#define IXGBE_VFTDBAH(x)       (0x02004 + (0x40 * (x)))
#define IXGBE_VFTDLEN(x)       (0x02008 + (0x40 * (x)))
#define IXGBE_VFDCA_TXCTRL(x)  (0x0200c + (0x40 * (x)))
#define IXGBE_VFTDH(x)         (0x02010 + (0x40 * (x)))
#define IXGBE_VFTDT(x)         (0x02018 + (0x40 * (x)))
#define IXGBE_VFGPTC           0x0201C
#define IXGBE_VFGOTC_LSB       0x02020
#define IXGBE_VFGOTC_MSB       0x02024
#define IXGBE_VFTXDCTL(x)      (0x02028 + (0x40 * (x)))
#define IXGBE_VFTDWBAL(x)      (0x02038 + (0x40 * (x)))
#define IXGBE_VFTDWBAH(x)      (0x0203C + (0x40 * (x)))
#define IXGBE_VFMRQC           0x3000
#define IXGBE_VFRSSRK(x)       (0x3100 + ((x) * 4))
#define IXGBE_VFRETA(x)        (0x3200 + ((x) * 4))
#define IXGBE_VFRXMEMWRAP     0x03190

/* Supplementary defines from driver source */
#define IXGBEVF_VFRSSRK_REGS       10
#define IXGBEVF_X550_VFRETA_SIZE   64

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
    uint32_t eicr;
    uint32_t eims;
    uint32_t eimc;
    uint32_t eiam;
    uint32_t eitr[8];      /* up to 8 vectors */
    uint32_t ivar[8];
    uint32_t ivar_misc;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t ctrl;
        uint32_t status;
        uint32_t links;
        uint32_t frtimer;
        uint32_t psrtype;
        uint32_t mrqc;
        uint32_t rssrk[IXGBEVF_VFRSSRK_REGS];
        uint8_t  reta[IXGBEVF_X550_VFRETA_SIZE];
        uint32_t rxmwrap;
        /* Per-queue registers */
        struct {
            uint32_t rxdctl;
            uint32_t rbal;
            uint32_t rbah;
            uint32_t rlen;
            uint32_t rdca;
            uint32_t rdh;
            uint32_t rdt;
            uint32_t srrctl;
        } rx[8];
        struct {
            uint32_t txdctl;
            uint32_t tbal;
            uint32_t tbah;
            uint32_t tlen;
            uint32_t tdca;
            uint32_t tdh;
            uint32_t tdt;
            uint32_t twbal;
            uint32_t twbah;
        } tx[8];
        /* Statistics counters (simplified) */
        uint64_t gprc;
        uint64_t gptc;
        uint64_t gorc;
        uint64_t gotc;
        uint64_t mprc;
    } regs;

    /* DMA Context (not used for static phase) */

    /* Operational status flags */
    bool link_up;
    uint32_t link_speed;

    /* Probe/Reset state */
    bool reset_triggered;

    /* Power management state */
    uint8_t power_state;  /* D0-D3 */
};

static void pcibase_update_irq(PCIBaseState *s);

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* Handle per-queue Rx registers (0x01000 - 0x013FF) */
    if (addr >= 0x01000 && addr < 0x01000 + 8*0x40) {
        int idx = (addr - 0x01000) / 0x40;
        int sub = (addr - 0x01000) % 0x40;
        if (idx >= 8) return 0;
        switch (sub) {
            case 0x00: val = s->regs.rx[idx].rbal; break;
            case 0x04: val = s->regs.rx[idx].rbah; break;
            case 0x08: val = s->regs.rx[idx].rlen; break;
            case 0x0C: val = s->regs.rx[idx].rdca; break;
            case 0x10: val = s->regs.rx[idx].rdh; break;
            case 0x14: val = s->regs.rx[idx].srrctl; break;
            case 0x18: val = s->regs.rx[idx].rdt; break;
            case 0x28: val = s->regs.rx[idx].rxdctl; break;
            default: break;
        }
        return val;
    }

    /* Handle per-queue Tx registers (0x02000 - 0x023FF) */
    if (addr >= 0x02000 && addr < 0x02000 + 8*0x40) {
        int idx = (addr - 0x02000) / 0x40;
        int sub = (addr - 0x02000) % 0x40;
        if (idx >= 8) return 0;
        switch (sub) {
            case 0x00: val = s->regs.tx[idx].tbal; break;
            case 0x04: val = s->regs.tx[idx].tbah; break;
            case 0x08: val = s->regs.tx[idx].tlen; break;
            case 0x0C: val = s->regs.tx[idx].tdca; break;
            case 0x10: val = s->regs.tx[idx].tdh; break;
            case 0x18: val = s->regs.tx[idx].tdt; break;
            case 0x28: val = s->regs.tx[idx].txdctl; break;
            case 0x38: val = s->regs.tx[idx].twbal; break;
            case 0x3C: val = s->regs.tx[idx].twbah; break;
            default: break;
        }
        return val;
    }

    /* Handle other specific registers */
    switch (addr) {
        case 0x00000: val = s->regs.ctrl; break;
        case 0x00008: val = s->regs.status; break;
        case 0x00010: val = s->regs.links; break;
        case 0x00048: val = s->regs.frtimer; break;
        case 0x00100: /* VTEICR: read then clear */
            val = s->eicr;
            s->eicr = 0;
            pcibase_update_irq(s);
            break;
        case 0x00104: val = 0; break; /* VTEICS write-only */
        case 0x00108: val = s->eims; break;
        case 0x0010C: val = 0; break; /* VTEIMC write-only */
        case 0x00110: val = 0; break; /* VTEIAC write-only */
        case 0x00114: val = s->eiam; break;
        case 0x00120 ... 0x0013C: /* VTIVAR(0..7) */
            {
                int idx = (addr - 0x00120) / 4;
                if (idx < 8) val = s->ivar[idx];
            }
            break;
        case 0x00140: val = s->ivar_misc; break;
        case 0x00300: val = s->regs.psrtype; break;
        case 0x00820 ... 0x0083C: /* VTEITR(x) */
            {
                int idx = (addr - 0x00820) / 4;
                if (idx < 8) val = s->eitr[idx];
            }
            break;
        case 0x0101C: val = s->regs.gprc & 0xFFFFFFFF; break;
        case 0x01020: val = s->regs.gorc & 0xFFFFFFFF; break;
        case 0x01024: val = (s->regs.gorc >> 32) & 0xFFFFFFFF; break;
        case 0x01034: val = s->regs.mprc & 0xFFFFFFFF; break;
        case 0x0201C: val = s->regs.gptc & 0xFFFFFFFF; break;
        case 0x02020: val = s->regs.gotc & 0xFFFFFFFF; break;
        case 0x02024: val = (s->regs.gotc >> 32) & 0xFFFFFFFF; break;
        case 0x03000: val = s->regs.mrqc; break;
        case 0x03100 ... 0x03124: /* VFRSSRK(0..9) */
            {
                int idx = (addr - 0x03100) / 4;
                if (idx < IXGBEVF_VFRSSRK_REGS) val = s->regs.rssrk[idx];
            }
            break;
        case 0x03190: val = s->regs.rxmwrap; break;
        case 0x03200 ... 0x032FC: /* VFRETA(0..63) */
            {
                int idx = (addr - 0x03200) / 4;
                if (idx < 64) {
                    val = s->regs.reta[idx*4] | (s->regs.reta[idx*4+1] << 8) |
                          (s->regs.reta[idx*4+2] << 16) | (s->regs.reta[idx*4+3] << 24);
                }
            }
            break;
        default:
            val = 0; /* unknown registers return 0 */
            break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle per-queue Rx registers (0x01000 - 0x013FF) */
    if (addr >= 0x01000 && addr < 0x01000 + 8*0x40) {
        int idx = (addr - 0x01000) / 0x40;
        int sub = (addr - 0x01000) % 0x40;
        if (idx >= 8) return;
        switch (sub) {
            case 0x00: s->regs.rx[idx].rbal = val; break;
            case 0x04: s->regs.rx[idx].rbah = val; break;
            case 0x08: s->regs.rx[idx].rlen = val; break;
            case 0x0C: s->regs.rx[idx].rdca = val; break;
            case 0x10: s->regs.rx[idx].rdh = val; break;
            case 0x14: s->regs.rx[idx].srrctl = val; break;
            case 0x18: s->regs.rx[idx].rdt = val; break;
            case 0x28: s->regs.rx[idx].rxdctl = val; break;
            default: break;
        }
        return;
    }

    /* Handle per-queue Tx registers (0x02000 - 0x023FF) */
    if (addr >= 0x02000 && addr < 0x02000 + 8*0x40) {
        int idx = (addr - 0x02000) / 0x40;
        int sub = (addr - 0x02000) % 0x40;
        if (idx >= 8) return;
        switch (sub) {
            case 0x00: s->regs.tx[idx].tbal = val; break;
            case 0x04: s->regs.tx[idx].tbah = val; break;
            case 0x08: s->regs.tx[idx].tlen = val; break;
            case 0x0C: s->regs.tx[idx].tdca = val; break;
            case 0x10: s->regs.tx[idx].tdh = val; break;
            case 0x18: s->regs.tx[idx].tdt = val; break;
            case 0x28: s->regs.tx[idx].txdctl = val; break;
            case 0x38: s->regs.tx[idx].twbal = val; break;
            case 0x3C: s->regs.tx[idx].twbah = val; break;
            default: break;
        }
        return;
    }

    /* Handle other specific registers */
    switch (addr) {
        case 0x00000: s->regs.ctrl = val; break;
        case 0x00008: s->regs.status = val; break;
        case 0x00010: s->regs.links = val; break;
        case 0x00048: s->regs.frtimer = val; break;
        case 0x00100: break; /* VTEICR read-only, ignore write */
        case 0x00104: /* VTEICS: set bits */
            s->eicr |= val;
            pcibase_update_irq(s);
            break;
        case 0x00108: /* VTEIMS: set enable mask */
            s->eims = val;
            pcibase_update_irq(s);
            break;
        case 0x0010C: /* VTEIMC: clear enable mask */
            s->eims &= ~val;
            break;
        case 0x00110: /* VTEIAC: auto-clear, ignore */ break;
        case 0x00114: s->eiam = val; break;
        case 0x00120 ... 0x0013C: /* VTIVAR(0..7) */
            {
                int idx = (addr - 0x00120) / 4;
                if (idx < 8) s->ivar[idx] = val;
            }
            break;
        case 0x00140: s->ivar_misc = val; break;
        case 0x00300: s->regs.psrtype = val; break;
        case 0x00820 ... 0x0083C: /* VTEITR(x) */
            {
                int idx = (addr - 0x00820) / 4;
                if (idx < 8) s->eitr[idx] = val;
            }
            break;
        case 0x0101C: s->regs.gprc = (s->regs.gprc & 0xFFFFFFFF00000000ULL) | val; break;
        case 0x01020: s->regs.gorc = (s->regs.gorc & 0xFFFFFFFF00000000ULL) | val; break;
        case 0x01024: s->regs.gorc = (s->regs.gorc & 0xFFFFFFFF) | ((uint64_t)val << 32); break;
        case 0x01034: s->regs.mprc = (s->regs.mprc & 0xFFFFFFFF00000000ULL) | val; break;
        case 0x0201C: s->regs.gptc = (s->regs.gptc & 0xFFFFFFFF00000000ULL) | val; break;
        case 0x02020: s->regs.gotc = (s->regs.gotc & 0xFFFFFFFF00000000ULL) | val; break;
        case 0x02024: s->regs.gotc = (s->regs.gotc & 0xFFFFFFFF) | ((uint64_t)val << 32); break;
        case 0x03000: s->regs.mrqc = val; break;
        case 0x03100 ... 0x03124: /* VFRSSRK(0..9) */
            {
                int idx = (addr - 0x03100) / 4;
                if (idx < IXGBEVF_VFRSSRK_REGS) s->regs.rssrk[idx] = val;
            }
            break;
        case 0x03190: s->regs.rxmwrap = val; break;
        case 0x03200 ... 0x032FC: /* VFRETA(0..63) */
            {
                int idx = (addr - 0x03200) / 4;
                if (idx < 64) {
                    s->regs.reta[idx*4] = val & 0xFF;
                    s->regs.reta[idx*4+1] = (val >> 8) & 0xFF;
                    s->regs.reta[idx*4+2] = (val >> 16) & 0xFF;
                    s->regs.reta[idx*4+3] = (val >> 24) & 0xFF;
                }
            }
            break;
        default:
            /* unknown registers ignored */
            break;
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

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;
    uint32_t causes = s->eicr & s->eims;
    for (i = 0; i < 3; i++) {
        if (causes & (1 << i)) {
            msix_notify(pdev, i);
        }
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

    /* Reset register defaults */
    s->regs.ctrl = 0;
    s->regs.status = 0;
    s->regs.links = 0;
    s->regs.frtimer = 0;
    s->regs.psrtype = 0;
    s->regs.mrqc = 0;
    memset(s->regs.rssrk, 0, sizeof(s->regs.rssrk));
    memset(s->regs.reta, 0, sizeof(s->regs.reta));
    s->regs.rxmwrap = 0;
    for (int i = 0; i < 8; i++) {
        s->regs.rx[i].rxdctl = 0;
        s->regs.rx[i].rbal = 0;
        s->regs.rx[i].rbah = 0;
        s->regs.rx[i].rlen = 0;
        s->regs.rx[i].rdca = 0;
        s->regs.rx[i].rdh = 0;
        s->regs.rx[i].rdt = 0;
        s->regs.rx[i].srrctl = 0;
    }
    for (int i = 0; i < 8; i++) {
        s->regs.tx[i].txdctl = 0;
        s->regs.tx[i].tbal = 0;
        s->regs.tx[i].tbah = 0;
        s->regs.tx[i].tlen = 0;
        s->regs.tx[i].tdca = 0;
        s->regs.tx[i].tdh = 0;
        s->regs.tx[i].tdt = 0;
        s->regs.tx[i].twbal = 0;
        s->regs.tx[i].twbah = 0;
    }
    s->regs.gprc = 0;
    s->regs.gptc = 0;
    s->regs.gorc = 0;
    s->regs.gotc = 0;
    s->regs.mprc = 0;
    s->eicr = 0;
    s->eims = 0;
    s->eimc = 0;
    s->eiam = 0;
    s->ivar_misc = 0;
    memset(s->ivar, 0, sizeof(s->ivar));
    memset(s->eitr, 0, sizeof(s->eitr));
    s->link_up = false;
    s->link_speed = 0;
    s->reset_triggered = false;
    s->power_state = 0;
    s->has_msix = true; /* device supports MSI-X */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0); /* MSI-X does not use INTx */

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
    s->bar_info[0].size = 0x4000;  /* 16KB typical for ixgbe VF */
    s->bar_info[0].name = "ixgbevf-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X Initialization */
    s->has_msix = true;
    if (msix_init(pdev, 3, &s->bar_regions[0], 0, 0, &s->bar_regions[0], 0, 0x1000, 0, errp)) {
        error_setg(errp, "Failed to init MSI-X");
        return;
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
    .name = "ixgbevf_pci",
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