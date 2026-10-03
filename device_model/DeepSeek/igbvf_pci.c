/*
 * QEMU PCI device model for Intel IGB Virtual Function (igbvf)
 * Based on Linux driver netdev.c
 * This device emulates the VF registers and basic behavior for driver probing.
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
#include "hw/pci/pci_ids.h"

/* Additional register offsets from e1000_regs.h */
#define E1000_EIAC    0x01510
#define E1000_EIAM    0x01514
#define E1000_DCA_TXCTRL 0x0381C
#define E1000_RAL     0x05400
#define E1000_RAH     0x05404
#define E1000_VFMBX(_n) (0x00C00 + 4 * (_n))

#define TYPE_PCIBASE_DEVICE "igbvf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID  0x8086
#define DEVICE_ID  0x10CA
#define CLASS_ID   PCI_CLASS_NETWORK_ETHERNET

/* BAR0 size must cover all VF registers */
#define IGBVF_BAR0_SIZE 0x8000

/* Register offset macros from netdev.c */
#define E1000_TDT(n)  ((n)<4 ? (0x03818+((n)*0x100)) : (0x0E018+((n)*0x40)))
#define E1000_TDH(n)  ((n)<4 ? (0x03810+((n)*0x100)) : (0x0E010+((n)*0x40)))
#define E1000_RDT(n)  ((n)<4 ? (0x02818+((n)*0x100)) : (0x0C018+((n)*0x40)))
#define E1000_RDH(n)  ((n)<4 ? (0x02810+((n)*0x100)) : (0x0C010+((n)*0x40)))
#define E1000_EITR(n) (0x01680 + (0x4*(n)))
#define E1000_CTRL     0x00000
#define E1000_STATUS   0x00008
#define E1000_V2PMAILBOX(_n)	(0x00C40 + (4 * (_n)))
#define E1000_IVAR	0x000E4
#define E1000_EICR     0x01580
#define E1000_EIMS     0x01524
#define E1000_EIMC     0x01528
#define E1000_IMS      0x000D0
#define E1000_IMC      0x000D8
#define E1000_ICR      0x000C0
#define E1000_TDBAL    0x03800
#define E1000_TDBAH    0x03804
#define E1000_TDLEN    0x03808
#define E1000_RDBAL    0x02800
#define E1000_RDBAH    0x02804
#define E1000_RDLEN    0x02808
#define E1000_TXDCTL   0x03828
#define E1000_RXDCTL   0x02828
#define E1000_SRRCTL(_n)  ((_n) < 4 ? (0x0280C + ((_n) * 0x100)) \
				    : (0x0C00C + ((_n) * 0x40)))
#define E1000_TPR      0x040D0
#define E1000_TPT      0x040D4
#define E1000_RA       0x05400
#define E1000_MTA      0x05200

/* Register structure */
typedef struct IGBVFRegs {
    uint32_t ctrl;
    uint32_t status;
    uint32_t icr;
    uint32_t ims;
    uint32_t imc;
    uint32_t ivar[2];  /* IVAR0 and IVAR_MISC */
    uint32_t eicr;
    uint32_t eims;
    uint32_t eimc;
    uint32_t eiac;
    uint32_t eiam;
    uint32_t eitr[3];
    uint32_t tdbal;
    uint32_t tdbal_hi;
    uint32_t tdlen;
    uint32_t tdh;
    uint32_t tdt;
    uint32_t txdctl;
    uint32_t rdbal;
    uint32_t rdbal_hi;
    uint32_t rdlen;
    uint32_t rdh;
    uint32_t rdt;
    uint32_t rxdctl;
    uint32_t srrctl;
    uint32_t dca_txctrl;
    uint32_t v2pmailbox[4];
    uint32_t vfmbx[4];
    uint32_t ral;
    uint32_t rah;
    uint32_t mta[128];
    uint32_t tpr;
    uint32_t tpt;
} IGBVFRegs;

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    IGBVFRegs regs;

    MemoryRegion msix_bar;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->regs.eicr & s->regs.eims;

    for (int i = 0; i < 3; i++) {
        if (active & (1 << i)) {
            msix_notify(pdev, i);
        }
    }
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad access size %u\n", __func__, size);
        return ~0ULL;
    }

    switch (addr & 0x7FFF) {
    case E1000_CTRL:
        val = s->regs.ctrl;
        break;
    case E1000_STATUS:
        val = s->regs.status;
        break;
    case E1000_ICR:
        val = s->regs.icr;
        break;
    case E1000_IMS:
        val = s->regs.ims;
        break;
    case E1000_IMC: /* write-only, read as 0 */
        val = 0;
        break;
    case E1000_IVAR:
        val = s->regs.ivar[0];
        break;
    case 0x000E8: /* IVAR_MISC */
        val = s->regs.ivar[1];
        break;
    case E1000_EICR:
        val = s->regs.eicr;
        break;
    case E1000_EIMS:
        val = s->regs.eims;
        break;
    case E1000_EIMC: /* write-only */
        val = 0;
        break;
    case E1000_EIAC:
        val = s->regs.eiac;
        break;
    case E1000_EIAM:
        val = s->regs.eiam;
        break;
    case 0x01680: /* EITR(0) */
    case 0x01684: /* EITR(1) */
    case 0x01688: /* EITR(2) */
        val = s->regs.eitr[(addr - 0x01680) / 4];
        break;
    case E1000_TDBAL:
        val = s->regs.tdbal;
        break;
    case E1000_TDBAH:
        val = s->regs.tdbal_hi;
        break;
    case E1000_TDLEN:
        val = s->regs.tdlen;
        break;
    case E1000_TDH(0):
        val = s->regs.tdh;
        break;
    case E1000_TDT(0):
        val = s->regs.tdt;
        break;
    case E1000_TXDCTL:
        val = s->regs.txdctl;
        break;
    case E1000_DCA_TXCTRL:
        val = s->regs.dca_txctrl;
        break;
    case E1000_RDBAL:
        val = s->regs.rdbal;
        break;
    case E1000_RDBAH:
        val = s->regs.rdbal_hi;
        break;
    case E1000_RDLEN:
        val = s->regs.rdlen;
        break;
    case E1000_RDH(0):
        val = s->regs.rdh;
        break;
    case E1000_RDT(0):
        val = s->regs.rdt;
        break;
    case E1000_RXDCTL:
        val = s->regs.rxdctl;
        break;
    case E1000_SRRCTL(0):
        val = s->regs.srrctl;
        break;
    case E1000_V2PMAILBOX(0):
    case E1000_V2PMAILBOX(1):
    case E1000_V2PMAILBOX(2):
    case E1000_V2PMAILBOX(3):
        val = s->regs.v2pmailbox[(addr - E1000_V2PMAILBOX(0)) / 4];
        break;
    case E1000_VFMBX(0):
    case E1000_VFMBX(1):
    case E1000_VFMBX(2):
    case E1000_VFMBX(3):
        val = s->regs.vfmbx[(addr - E1000_VFMBX(0)) / 4];
        break;
    case E1000_RAL:
        val = s->regs.ral;
        break;
    case E1000_RAH:
        val = s->regs.rah;
        break;
    case E1000_MTA ... E1000_MTA + 0x1FF: /* MTA is 0x5200, size 128*4 */
        val = s->regs.mta[(addr - E1000_MTA) / 4];
        break;
    case E1000_TPR:
        val = s->regs.tpr;
        break;
    case E1000_TPT:
        val = s->regs.tpt;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown read offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        val = 0;
        break;
    }

    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad access size %u\n", __func__, size);
        return;
    }

    addr &= 0x7FFF;
    switch (addr) {
    case E1000_CTRL:
        s->regs.ctrl = val;
        if (val & (1 << 26)) { /* CTRL.RST */
            /* Reset device */
            memset(&s->regs, 0, sizeof(s->regs));
            /* Power-on defaults */
            s->regs.status = 0x00000002; /* Link Up */
            s->regs.ral = 0x00123456;
            s->regs.rah = 0x80005254;
            s->regs.vfmbx[0] = 0x1; /* Mailbox ready */
        }
        break;
    case E1000_STATUS:
        /* Read-only? Possibly some bits can be cleared */
        s->regs.status = val;
        break;
    case E1000_ICR:
        s->regs.icr &= ~val;
        break;
    case E1000_IMS:
        s->regs.ims |= val;
        pcibase_update_irq(s);
        break;
    case E1000_IMC:
        s->regs.ims &= ~val;
        pcibase_update_irq(s);
        break;
    case E1000_IVAR:
        s->regs.ivar[0] = val;
        break;
    case 0x000E8: /* IVAR_MISC */
        s->regs.ivar[1] = val;
        break;
    case E1000_EICR:
        s->regs.eicr &= ~val;
        pcibase_update_irq(s);
        break;
    case E1000_EIMS:
        s->regs.eims |= val;
        pcibase_update_irq(s);
        break;
    case E1000_EIMC:
        s->regs.eims &= ~val;
        pcibase_update_irq(s);
        break;
    case E1000_EIAC:
        s->regs.eiac = val;
        break;
    case E1000_EIAM:
        s->regs.eiam = val;
        break;
    case 0x01680: case 0x01684: case 0x01688:
        s->regs.eitr[(addr - 0x01680) / 4] = val;
        break;
    case E1000_TDBAL:
        s->regs.tdbal = val;
        break;
    case E1000_TDBAH:
        s->regs.tdbal_hi = val;
        break;
    case E1000_TDLEN:
        s->regs.tdlen = val;
        break;
    case E1000_TDH(0):
        s->regs.tdh = val;
        break;
    case E1000_TDT(0):
        s->regs.tdt = val;
        break;
    case E1000_TXDCTL:
        s->regs.txdctl = val;
        break;
    case E1000_DCA_TXCTRL:
        s->regs.dca_txctrl = val;
        break;
    case E1000_RDBAL:
        s->regs.rdbal = val;
        break;
    case E1000_RDBAH:
        s->regs.rdbal_hi = val;
        break;
    case E1000_RDLEN:
        s->regs.rdlen = val;
        break;
    case E1000_RDH(0):
        s->regs.rdh = val;
        break;
    case E1000_RDT(0):
        s->regs.rdt = val;
        break;
    case E1000_RXDCTL:
        s->regs.rxdctl = val;
        break;
    case E1000_SRRCTL(0):
        s->regs.srrctl = val;
        break;
    case E1000_V2PMAILBOX(0):
    case E1000_V2PMAILBOX(1):
    case E1000_V2PMAILBOX(2):
    case E1000_V2PMAILBOX(3):
        s->regs.v2pmailbox[(addr - E1000_V2PMAILBOX(0)) / 4] = val;
        /* Fake mailbox acknowledge: set VFMBX[0] to indicate success */
        if (addr == E1000_V2PMAILBOX(0)) {
            s->regs.vfmbx[0] = 0x1;
        }
        break;
    case E1000_VFMBX(0):
    case E1000_VFMBX(1):
    case E1000_VFMBX(2):
    case E1000_VFMBX(3):
        s->regs.vfmbx[(addr - E1000_VFMBX(0)) / 4] = val;
        break;
    case E1000_RAL:
        s->regs.ral = val;
        break;
    case E1000_RAH:
        s->regs.rah = val;
        break;
    case E1000_MTA ... E1000_MTA + 0x1FF:
        s->regs.mta[(addr - E1000_MTA) / 4] = val;
        break;
    case E1000_TPR:
        s->regs.tpr = val;
        break;
    case E1000_TPT:
        s->regs.tpt = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown write offset 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", __func__, addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
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

    /* Clear registers and set power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.status = 0x00000002;      /* Link Up */
    s->regs.ral = 0x00123456;          /* Default MAC low */
    s->regs.rah = 0x80005254;          /* Default MAC high, valid bit set */
    s->regs.vfmbx[0] = 0x1;           /* Mailbox ready */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x10CA);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: MMIO */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = IGBVF_BAR0_SIZE, .name = "igbvf-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X BAR */
    memory_region_init(&s->msix_bar, OBJECT(s), "msix-bar", 4096);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_bar);
    if (msix_init(pdev, 3, &s->msix_bar, 1, 0, &s->msix_bar, 1, 0, 0x70, errp)) {
        error_setg(errp, "Failed to init MSI-X");
        return;
    }

    /* Initial state */
    s->intr_status = 0;
    s->intr_mask = 0;

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.status = 0x00000002;
    s->regs.ral = 0x00123456;
    s->regs.rah = 0x80005254;
    s->regs.vfmbx[0] = 0x1;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "igbvf_pci",
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
