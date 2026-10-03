/*
 * QEMU PCI device model for Broadcom brcmfmac wireless adapter.
 * Generated from driver: drivers/net/wireless/broadcom/brcm80211/brcmfmac/pcie.c
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
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

/* Driver-specific definitions (static extraction) */
#define BRCMF_PCIE_REG_MAP_SIZE         (32 * 1024)       /* 0x8000 */
#define BRCMF_PCIE_BAR0_REG_SIZE        0x1000
#define BRCMF_PCIE_BAR0_WINDOW          0x70
#define BRCMF_PCIE_BAR0_WRAPPERBASE      0x70
#define BRCMF_PCIE_BARO_PCIE_ENUM_OFFSET 0x2000

/* Register offsets (MMIO, BAR0 relative) */
#define BRCMF_PCIE_REG_INTSTATUS            0x90
#define BRCMF_PCIE_REG_INTMASK              0x94
#define BRCMF_PCIE_REG_SBMBX                0x98
#define BRCMF_PCIE_REG_LINK_STATUS_CTRL     0xBC

#define BRCMF_PCIE_PCIE2REG_INTMASK         0x24
#define BRCMF_PCIE_PCIE2REG_MAILBOXINT      0x48
#define BRCMF_PCIE_PCIE2REG_MAILBOXMASK     0x4C
#define BRCMF_PCIE_PCIE2REG_CONFIGADDR      0x120
#define BRCMF_PCIE_PCIE2REG_CONFIGDATA      0x124
#define BRCMF_PCIE_PCIE2REG_H2D_MAILBOX_0   0x140
#define BRCMF_PCIE_PCIE2REG_H2D_MAILBOX_1   0x144

#define BRCMF_PCIE_64_PCIE2REG_INTMASK      0xC14
#define BRCMF_PCIE_64_PCIE2REG_MAILBOXINT   0xC30
#define BRCMF_PCIE_64_PCIE2REG_MAILBOXMASK  0xC34
#define BRCMF_PCIE_64_PCIE2REG_H2D_MAILBOX_0 0xA20
#define BRCMF_PCIE_64_PCIE2REG_H2D_MAILBOX_1 0xA24

/* Additional registers from driver */
#define BRCMF_PCIE_PCIE2REG_ARMCR4REG_BANKIDX  0x40
#define BRCMF_PCIE_PCIE2REG_ARMCR4REG_BANKPDA  0x4C

/* New definitions from supplementary source */
#define SI_ENUM_BASE_DEFAULT	0x18000000
#define CORE_CC_REG(base, field) \
		(base + offsetof(struct chipcregs, field))

#define CID_ID_MASK		0x0000ffff
#define CID_REV_MASK		0x000f0000
#define CID_REV_SHIFT		16
#define CID_TYPE_MASK		0xf0000000
#define CID_TYPE_SHIFT		28
#define SOCI_SB		0
#define SOCI_AI		1

/* PCI IDs */
#define VENDOR_ID 0x14e4
#define DEVICE_ID 0x43a0
#define CLASS_ID  PCI_CLASS_NETWORK_OTHER

#define TYPE_PCIBASE_DEVICE "brcmfmac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Forward declarations */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

typedef struct {
    int index;
    int type; /* 0=none, 1=MMIO, 3=RAM */
    hwaddr size;
    const char *name;
} BARInfo;

// Main device state
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    uint32_t mailbox_int;
    uint32_t mailbox_mask;

    /* MMIO register storage (32 KB) - byte accessible */
    uint8_t regs[BRCMF_PCIE_REG_MAP_SIZE];

    /* Operational state */
    uint32_t pcie_state; /* enum brcmf_pcie_state */

    /* Window register for BAR0 register wrapper */
    uint32_t window_base;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    if (s->mailbox_int & s->mailbox_mask) {
        if (msi_enabled(&s->parent_obj)) {
            msi_notify(&s->parent_obj, 0);
        } else {
            pci_set_irq(&s->parent_obj, 1);
        }
    } else {
        if (!msi_enabled(&s->parent_obj)) {
            pci_set_irq(&s->parent_obj, 0);
        }
    }
}

/* Helper to read from byte-aligned register array with given size */
static uint64_t regs_read(uint8_t *regs, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    if (addr + size > BRCMF_PCIE_REG_MAP_SIZE) {
        return 0;
    }
    switch (size) {
    case 1:
        val = regs[addr];
        break;
    case 2:
        val = lduw_le_p(regs + addr);
        break;
    case 4:
        val = ldl_le_p(regs + addr);
        break;
    case 8:
        val = ldq_le_p(regs + addr);
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

/* Helper to write to byte-aligned register array with given size */
static void regs_write(uint8_t *regs, hwaddr addr, uint64_t val, unsigned size)
{
    if (addr + size > BRCMF_PCIE_REG_MAP_SIZE) {
        return;
    }
    switch (size) {
    case 1:
        regs[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(regs + addr, (uint16_t)val);
        break;
    case 4:
        stl_le_p(regs + addr, (uint32_t)val);
        break;
    case 8:
        stq_le_p(regs + addr, val);
        break;
    default:
        break;
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case BRCMF_PCIE_REG_INTSTATUS:
        val = s->mailbox_int;
        break;
    case BRCMF_PCIE_REG_INTMASK:
        val = s->mailbox_mask;
        break;
    case BRCMF_PCIE_REG_SBMBX:
        val = regs_read(s->regs, addr, size);
        break;
    case BRCMF_PCIE_REG_LINK_STATUS_CTRL:
        val = regs_read(s->regs, addr, size);
        break;
    case BRCMF_PCIE_PCIE2REG_INTMASK:
        val = regs_read(s->regs, addr, size);
        break;
    case BRCMF_PCIE_PCIE2REG_MAILBOXINT:
        val = s->mailbox_int;
        break;
    case BRCMF_PCIE_PCIE2REG_MAILBOXMASK:
        val = s->mailbox_mask;
        break;
    case BRCMF_PCIE_PCIE2REG_CONFIGADDR:
        val = regs_read(s->regs, addr, size);
        break;
    case BRCMF_PCIE_PCIE2REG_CONFIGDATA:
        val = regs_read(s->regs, addr, size);
        break;
    case BRCMF_PCIE_PCIE2REG_H2D_MAILBOX_0:
        val = regs_read(s->regs, addr, size);
        break;
    case BRCMF_PCIE_PCIE2REG_H2D_MAILBOX_1:
        val = regs_read(s->regs, addr, size);
        break;
    case BRCMF_PCIE_64_PCIE2REG_INTMASK:
        val = regs_read(s->regs, addr, size);
        break;
    case BRCMF_PCIE_64_PCIE2REG_MAILBOXINT:
        val = s->mailbox_int;
        break;
    case BRCMF_PCIE_64_PCIE2REG_MAILBOXMASK:
        val = s->mailbox_mask;
        break;
    case BRCMF_PCIE_64_PCIE2REG_H2D_MAILBOX_0:
        val = regs_read(s->regs, addr, size);
        break;
    case BRCMF_PCIE_64_PCIE2REG_H2D_MAILBOX_1:
        val = regs_read(s->regs, addr, size);
        break;
    default:
        if (addr >= 0x70 && addr < 0x70 + 0x1000) {
            int64_t mapped = (int64_t)s->window_base - SI_ENUM_BASE_DEFAULT + (addr - 0x70);
            if (mapped >= 0 && mapped < sizeof(s->regs)) {
                val = regs_read(s->regs, mapped, size);
            } else {
                val = 0;
            }
        } else if (addr < sizeof(s->regs)) {
            val = regs_read(s->regs, addr, size);
        }
        break;
    }
    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case BRCMF_PCIE_REG_INTSTATUS:
        s->mailbox_int &= ~(uint32_t)val; /* W1C */
        pcibase_update_irq(s);
        break;
    case BRCMF_PCIE_REG_INTMASK:
        s->mailbox_mask = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case BRCMF_PCIE_REG_SBMBX:
        regs_write(s->regs, addr, val, size);
        break;
    case BRCMF_PCIE_REG_LINK_STATUS_CTRL:
        regs_write(s->regs, addr, val, size);
        break;
    case BRCMF_PCIE_PCIE2REG_INTMASK:
        regs_write(s->regs, addr, val, size);
        break;
    case BRCMF_PCIE_PCIE2REG_MAILBOXINT:
        s->mailbox_int &= ~(uint32_t)val; /* W1C */
        pcibase_update_irq(s);
        break;
    case BRCMF_PCIE_PCIE2REG_MAILBOXMASK:
        s->mailbox_mask = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case BRCMF_PCIE_PCIE2REG_CONFIGADDR:
        regs_write(s->regs, addr, val, size);
        break;
    case BRCMF_PCIE_PCIE2REG_CONFIGDATA:
        regs_write(s->regs, addr, val, size);
        break;
    case BRCMF_PCIE_PCIE2REG_H2D_MAILBOX_0:
        regs_write(s->regs, addr, val, size);
        /* Writing to H2D mailbox could trigger firmware action; for now, no simulation */
        break;
    case BRCMF_PCIE_PCIE2REG_H2D_MAILBOX_1:
        regs_write(s->regs, addr, val, size);
        break;
    case BRCMF_PCIE_64_PCIE2REG_INTMASK:
        regs_write(s->regs, addr, val, size);
        break;
    case BRCMF_PCIE_64_PCIE2REG_MAILBOXINT:
        s->mailbox_int &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case BRCMF_PCIE_64_PCIE2REG_MAILBOXMASK:
        s->mailbox_mask = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case BRCMF_PCIE_64_PCIE2REG_H2D_MAILBOX_0:
        regs_write(s->regs, addr, val, size);
        break;
    case BRCMF_PCIE_64_PCIE2REG_H2D_MAILBOX_1:
        regs_write(s->regs, addr, val, size);
        break;
    case BRCMF_PCIE_BAR0_WINDOW:
        s->window_base = (uint32_t)val;
        break;
    default:
        if (addr >= 0x70 && addr < 0x70 + 0x1000) {
            int64_t mapped = (int64_t)s->window_base - SI_ENUM_BASE_DEFAULT + (addr - 0x70);
            if (mapped >= 0 && mapped < sizeof(s->regs)) {
                regs_write(s->regs, mapped, val, size);
            }
        } else if (addr < sizeof(s->regs)) {
            regs_write(s->regs, addr, val, size);
        }
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->mailbox_int = 0;
    s->mailbox_mask = 0;
    s->pcie_state = 0; /* DOWN */
    s->window_base = SI_ENUM_BASE_DEFAULT; /* default to chipcommon base */

    /* Pre-fill chip identification at both BAR0 offset 0 and enumeration offset */
    /* Construct a valid chip ID: type AI, rev 1, chip ID 0x4360 */
    uint32_t chip_id_val = (SOCI_AI << CID_TYPE_SHIFT) | (1 << CID_REV_SHIFT) | 0x4360;
    stl_le_p(s->regs, chip_id_val);                       /* offset 0x0000 */
    stl_le_p(s->regs + BRCMF_PCIE_BARO_PCIE_ENUM_OFFSET, chip_id_val); /* offset 0x2000 */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == 0) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == 1) { /* MMIO */
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == 3) { /* RAM (TCM) */
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 0: MMIO register space (32 KB) */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = 1; /* MMIO */
    s->bar_info[0].size = BRCMF_PCIE_REG_MAP_SIZE;
    s->bar_info[0].name = "brcmfmac-mem";

    /* BAR 2: TCM memory (size set to 512KB as a plausible value) */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = 3; /* RAM for TCM */
    s->bar_info[2].size = 0x80000; /* 512 KB */
    s->bar_info[2].name = "brcmfmac-tcm";
    s->num_bars = 2;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI support */
    s->has_msi = true;
    s->has_msix = false;
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }

    /* Ensure initial state is set before guest access */
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "brcmfmac_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(mailbox_int, PCIBaseState),
        VMSTATE_UINT32(mailbox_mask, PCIBaseState),
        VMSTATE_BUFFER(regs, PCIBaseState),
        VMSTATE_UINT32(pcie_state, PCIBaseState),
        VMSTATE_UINT32(window_base, PCIBaseState),
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
