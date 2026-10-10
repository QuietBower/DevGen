/*
 * QEMU PCI device model for Intel IAVF VF (minimal register-level emulation)
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

#define TYPE_PCIBASE_DEVICE "iavf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IAVF_VENDOR_ID_INTEL      PCI_VENDOR_ID_INTEL
#define IAVF_DEVICE_ID_VF         0x154C
#define IAVF_PCI_CLASS_NETWORK    PCI_CLASS_NETWORK_ETHERNET

#define IAVF_VFINT_DYN_CTL01      0x00005C00
#define IAVF_VFINT_ICR0_ENA1      0x00005000
#define IAVF_VFINT_DYN_CTLN1(_INTVF) (0x00003800 + ((_INTVF) * 4))
#define IAVF_VFINT_ICR01          0x00004800
#define IAVF_VFINT_ITRN1(_i, _INTVF) (0x00002800 + ((_i) * 64 + (_INTVF) * 4))
#define IAVF_QTX_TAIL1(_Q)        (0x00000000 + ((_Q) * 4))
#define IAVF_QRX_TAIL1(_Q)        (0x00002000 + ((_Q) * 4))
#define IAVF_VFQF_HKEY(_i)        (0x0000CC00 + ((_i) * 4))
#define IAVF_VFQF_HLUT(_i)        (0x0000D000 + ((_i) * 4))
#define IAVF_VFQF_HENA(_i)        (0x0000C400 + ((_i) * 4))
#define IAVF_VFGEN_RSTAT          0x00008800
#define IAVF_VF_ARQLEN1           0x00008000
#define IAVF_VF_ATQLEN1           0x00006800
#define IAVF_VF_ATQH1             0x00006400
#define IAVF_VF_ARQT1             0x00007000
#define IAVF_VF_ARQH1             0x00007400
#define IAVF_VF_ATQBAH1           0x00007800
#define IAVF_VF_ATQT1             0x00008400
#define IAVF_VF_ATQBAL1           0x00007C00
#define IAVF_VF_ARQBAH1           0x00006000
#define IAVF_VF_ARQBAL1           0x00006C00

#define IAVF_BAR0_SIZE            (256 * 1024) /* 256 KB BAR0 as a reasonable upper bound for VF regs */

/* Bits used by driver, from iavf_main.c context (minimal subset) */
#define IAVF_VFGEN_RSTAT_VFR_STATE_MASK 0x3
#define VIRTCHNL_VFR_VFACTIVE           0x1
#define VIRTCHNL_VFR_COMPLETED          0x2

#define IAVF_VF_ARQLEN1_ARQENABLE_MASK  0x1
#define IAVF_VF_ARQLEN1_ARQVFE_MASK     0x40000000
#define IAVF_VF_ARQLEN1_ARQOVFL_MASK    0x80000000
#define IAVF_VF_ARQLEN1_ARQCRIT_MASK    0x20000000

#define IAVF_VF_ATQLEN1_ATQENABLE_MASK  0x1
#define IAVF_VF_ATQLEN1_ATQVFE_MASK     0x40000000
#define IAVF_VF_ATQLEN1_ATQOVFL_MASK    0x80000000
#define IAVF_VF_ATQLEN1_ATQCRIT_MASK    0x20000000

/* Interrupt control bits are used as masks; exact values are PF-specific.
 * We only need them as opaque bits that get stored/checked by driver, so
 * model them as generic bitmasks without semantics.
 */
#define IAVF_VFINT_DYN_CTL01_INTENA_MASK   0x00000001
#define IAVF_VFINT_DYN_CTL01_ITR_INDX_MASK 0x0000000E
#define IAVF_VFINT_DYN_CTLN1_INTENA_MASK   0x00000001
#define IAVF_VFINT_DYN_CTLN1_ITR_INDX_MASK 0x0000000E
#define IAVF_VFINT_ICR0_ENA1_ADMINQ_MASK   0x00000001


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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x10000 / sizeof(uint32_t)];

    /* Simple admin queue / reset emulation state */
    uint32_t vfgen_rstat;
    uint32_t vf_arqlen1;
    uint32_t vf_atqlen1;
    uint32_t vf_arqbal1;
    uint32_t vf_arqbah1;
    uint32_t vf_atqbal1;
    uint32_t vf_atqbah1;

    uint32_t vfint_dyn_ctl01;
    uint32_t vfint_icr0_ena1;

    /* queue dynamic controls; model a reasonable upper bound */
    uint32_t vfint_dyn_ctln1[32];
    uint32_t vfint_itrn1[2][32]; /* [ITR index][vector] */

    /* RSS configuration */
    uint32_t vfqf_hena[2];

    /* tail registers for a few queues */
    uint32_t qtx_tail[64];
    uint32_t qrx_tail[64];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!s->intr_mask) {
        return;
    }
    if (s->intr_status & s->intr_mask) {
        if (s->has_msix && msix_enabled(pdev)) {
            /* Only misc/admin vector (0) is modeled */
            msix_notify(pdev, 0);
        } else if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!(s->has_msix && msix_enabled(pdev)) &&
            !(s->has_msi && msi_enabled(pdev))) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The iavf VF driver uses host-initiated DMA (device is bus master),
     * but this file does not have the PF adminq/virtchnl implementation.
     * Without PF logic we cannot model actual DMA transfers here.
     *
     * Leave this function empty: we will not initiate DMA on our own.
     */
    (void)s;
    (void)is_write;
}

static uint32_t pcibase_mmio_readl(PCIBaseState *s, hwaddr addr)
{
    /* Admin queue / status registers with special behavior */
    switch (addr) {
    case IAVF_VFGEN_RSTAT:
        return s->vfgen_rstat;
    case IAVF_VF_ARQLEN1:
        return s->vf_arqlen1;
    case IAVF_VF_ATQLEN1:
        return s->vf_atqlen1;
    case IAVF_VF_ARQBAL1:
        return s->vf_arqbal1;
    case IAVF_VF_ARQBAH1:
        return s->vf_arqbah1;
    case IAVF_VF_ATQBAL1:
        return s->vf_atqbal1;
    case IAVF_VF_ATQBAH1:
        return s->vf_atqbah1;

    case IAVF_VFINT_DYN_CTL01:
        return s->vfint_dyn_ctl01;
    case IAVF_VFINT_ICR0_ENA1:
        return s->vfint_icr0_ena1;

    default:
        break;
    }

    /* Interrupt dynamic control per-vector */
    if (addr >= IAVF_VFINT_DYN_CTLN1(0) &&
        addr < IAVF_VFINT_DYN_CTLN1(32)) {
        unsigned int idx = (addr - IAVF_VFINT_DYN_CTLN1(0)) / 4;
        if (idx < 32) {
            return s->vfint_dyn_ctln1[idx];
        }
    }

    /* Interrupt ITR per vector / ITR index (RX/TX) */
    if (addr >= IAVF_VFINT_ITRN1(0, 0) &&
        addr < IAVF_VFINT_ITRN1(2, 32)) {
        unsigned int offset = addr - IAVF_VFINT_ITRN1(0, 0);
        unsigned int group = offset / 64; /* each _i block 64 bytes */
        unsigned int within = offset % 64;
        unsigned int vec = within / 4;
        if (group < 2 && vec < 32) {
            return s->vfint_itrn1[group][vec];
        }
    }

    /* RSS configuration */
    if (addr == IAVF_VFQF_HENA(0)) {
        return s->vfqf_hena[0];
    }
    if (addr == IAVF_VFQF_HENA(1)) {
        return s->vfqf_hena[1];
    }

    /* Tail registers for TX and RX queues */
    if (addr >= IAVF_QTX_TAIL1(0) && addr < IAVF_QTX_TAIL1(64)) {
        unsigned int q = (addr - IAVF_QTX_TAIL1(0)) / 4;
        if (q < 64) {
            return s->qtx_tail[q];
        }
    }
    if (addr >= IAVF_QRX_TAIL1(0) && addr < IAVF_QRX_TAIL1(64)) {
        unsigned int q = (addr - IAVF_QRX_TAIL1(0)) / 4;
        if (q < 64) {
            return s->qrx_tail[q];
        }
    }

    /* Fallback to generic backing array within 64KB */
    if (addr < sizeof(s->regs)) {
        return s->regs[addr >> 2];
    }

    return 0xffffffffu;
}

static void pcibase_mmio_writel(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case IAVF_VFGEN_RSTAT:
        /* Driver only reads this; writes are ignored */
        s->vfgen_rstat = val;
        return;

    case IAVF_VF_ARQLEN1:
        /* length and enable bits; mirror, but also clear error bits on write */
        s->vf_arqlen1 = val & ~(IAVF_VF_ARQLEN1_ARQVFE_MASK |
                                IAVF_VF_ARQLEN1_ARQOVFL_MASK |
                                IAVF_VF_ARQLEN1_ARQCRIT_MASK);
        return;

    case IAVF_VF_ATQLEN1:
        s->vf_atqlen1 = val & ~(IAVF_VF_ATQLEN1_ATQVFE_MASK |
                                IAVF_VF_ATQLEN1_ATQOVFL_MASK |
                                IAVF_VF_ATQLEN1_ATQCRIT_MASK);
        return;

    case IAVF_VF_ARQBAL1:
        s->vf_arqbal1 = val;
        return;
    case IAVF_VF_ARQBAH1:
        s->vf_arqbah1 = val;
        return;
    case IAVF_VF_ATQBAL1:
        s->vf_atqbal1 = val;
        return;
    case IAVF_VF_ATQBAH1:
        s->vf_atqbah1 = val;
        return;

    case IAVF_VFINT_DYN_CTL01:
        s->vfint_dyn_ctl01 = val;
        if (!(val & IAVF_VFINT_DYN_CTL01_INTENA_MASK)) {
            /* disable misc interrupt */
            s->intr_mask &= ~0x1u;
        } else {
            s->intr_mask |= 0x1u;
        }
        pcibase_update_irq(s);
        return;

    case IAVF_VFINT_ICR0_ENA1:
        s->vfint_icr0_ena1 = val;
        /* AdminQ interrupt enable controls bit 0 mask */
        if (val & IAVF_VFINT_ICR0_ENA1_ADMINQ_MASK) {
            s->intr_mask |= 0x1u;
        } else {
            s->intr_mask &= ~0x1u;
        }
        pcibase_update_irq(s);
        return;

    default:
        break;
    }

    /* per-vector dynamic control */
    if (addr >= IAVF_VFINT_DYN_CTLN1(0) &&
        addr < IAVF_VFINT_DYN_CTLN1(32)) {
        unsigned int idx = (addr - IAVF_VFINT_DYN_CTLN1(0)) / 4;
        if (idx < 32) {
            s->vfint_dyn_ctln1[idx] = val;
        }
        return;
    }

    /* per-vector ITR */
    if (addr >= IAVF_VFINT_ITRN1(0, 0) &&
        addr < IAVF_VFINT_ITRN1(2, 32)) {
        unsigned int offset = addr - IAVF_VFINT_ITRN1(0, 0);
        unsigned int group = offset / 64;
        unsigned int within = offset % 64;
        unsigned int vec = within / 4;
        if (group < 2 && vec < 32) {
            s->vfint_itrn1[group][vec] = val;
        }
        return;
    }

    /* RSS configuration */
    if (addr == IAVF_VFQF_HENA(0)) {
        s->vfqf_hena[0] = val;
        return;
    }
    if (addr == IAVF_VFQF_HENA(1)) {
        s->vfqf_hena[1] = val;
        return;
    }

    /* Tail registers for TX and RX queues */
    if (addr >= IAVF_QTX_TAIL1(0) && addr < IAVF_QTX_TAIL1(64)) {
        unsigned int q = (addr - IAVF_QTX_TAIL1(0)) / 4;
        if (q < 64) {
            s->qtx_tail[q] = val;
        }
        return;
    }
    if (addr >= IAVF_QRX_TAIL1(0) && addr < IAVF_QRX_TAIL1(64)) {
        unsigned int q = (addr - IAVF_QRX_TAIL1(0)) / 4;
        if (q < 64) {
            s->qrx_tail[q] = val;
        }
        return;
    }

    if (addr < sizeof(s->regs)) {
        s->regs[addr >> 2] = val;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 4) {
        val = pcibase_mmio_readl(s, addr);
    } else if (size == 2) {
        uint32_t v = pcibase_mmio_readl(s, addr & ~0x3);
        unsigned shift = (addr & 0x3) * 8;
        val = (v >> shift) & 0xffffu;
    } else if (size == 1) {
        uint32_t v = pcibase_mmio_readl(s, addr & ~0x3);
        unsigned shift = (addr & 0x3) * 8;
        val = (v >> shift) & 0xffu;
    } else if (size == 8) {
        uint32_t lo = pcibase_mmio_readl(s, addr);
        uint32_t hi = pcibase_mmio_readl(s, addr + 4);
        val = ((uint64_t)hi << 32) | lo;
    } else {
        /* unsupported size, return all-ones like hardware often does */
        val = ~0ULL;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        pcibase_mmio_writel(s, addr, (uint32_t)val);
    } else if (size == 2) {
        uint32_t old = pcibase_mmio_readl(s, addr & ~0x3);
        unsigned shift = (addr & 0x3) * 8;
        uint32_t mask = 0xffffu << shift;
        uint32_t nv = (old & ~mask) | (((uint32_t)val & 0xffffu) << shift);
        pcibase_mmio_writel(s, addr & ~0x3, nv);
    } else if (size == 1) {
        uint32_t old = pcibase_mmio_readl(s, addr & ~0x3);
        unsigned shift = (addr & 0x3) * 8;
        uint32_t mask = 0xffu << shift;
        uint32_t nv = (old & ~mask) | (((uint32_t)val & 0xffu) << shift);
        pcibase_mmio_writel(s, addr & ~0x3, nv);
    } else if (size == 8) {
        pcibase_mmio_writel(s, addr, (uint32_t)val);
        pcibase_mmio_writel(s, addr + 4, (uint32_t)(val >> 32));
    } else {
        /* ignore unsupported sizes */
    }
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

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;

    s->vfgen_rstat = VIRTCHNL_VFR_VFACTIVE;
    s->vf_arqlen1 = 0;
    s->vf_atqlen1 = 0;
    s->vf_arqbal1 = 0;
    s->vf_arqbah1 = 0;
    s->vf_atqbal1 = 0;
    s->vf_atqbah1 = 0;

    s->vfint_dyn_ctl01 = 0;
    s->vfint_icr0_ena1 = 0;

    memset(s->vfint_dyn_ctln1, 0, sizeof(s->vfint_dyn_ctln1));
    memset(s->vfint_itrn1, 0, sizeof(s->vfint_itrn1));

    s->vfqf_hena[0] = 0;
    s->vfqf_hena[1] = 0;

    memset(s->qtx_tail, 0, sizeof(s->qtx_tail));
    memset(s->qrx_tail, 0, sizeof(s->qrx_tail));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  IAVF_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IAVF_DEVICE_ID_VF );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IAVF_PCI_CLASS_NETWORK );
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
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = IAVF_BAR0_SIZE;
    s->bar_info[0].name  = "iavf-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msix_init_exclusive_bar(pdev, 1, 0, NULL) == 0) {
        s->has_msix = true;
    } else if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* Initialize internal state to power-on defaults */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "iavf_pci",
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

