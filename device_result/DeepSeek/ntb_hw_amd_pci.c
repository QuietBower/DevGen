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

#define TYPE_PCIBASE_DEVICE "ntb_hw_amd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI vendor and device IDs from first entry in pci_device_id table */
#define VENDOR_ID  0x1022  /* AMD */
#define DEVICE_ID  0x145b
#define CLASS_ID   0x0680  /* Non-Transparent Bridge */

/* Register offset macros (based on typical AMD NTB hardware layout) */
#define AMD_CNTL_OFFSET         0x000
#define AMD_SIDEINFO_OFFSET     0x004
#define AMD_SMUACK_OFFSET       0x008
#define AMD_PMESTAT_OFFSET      0x00C
#define AMD_INTMASK_OFFSET      0x010
#define AMD_INTSTAT_OFFSET      0x014
#define AMD_DBMASK_OFFSET       0x018
#define AMD_DBSTAT_OFFSET       0x01C
#define AMD_DBREQ_OFFSET        0x020
#define AMD_SPAD_OFFSET         0x024

#define AMD_BAR1XLAT_OFFSET     0x200
#define AMD_BAR1LMT_OFFSET      0x208
#define AMD_BAR23XLAT_OFFSET    0x210
#define AMD_BAR23LMT_OFFSET     0x220
#define AMD_BAR45XLAT_OFFSET    0x230
#define AMD_BAR45LMT_OFFSET     0x240

#define AMD_PEER_OFFSET         0x1000

/* Event bit definitions and mask */
#define AMD_PEER_FLUSH_EVENT    BIT(0)
#define AMD_PEER_RESET_EVENT    BIT(1)
#define AMD_LINK_DOWN_EVENT     BIT(2)
#define AMD_PEER_D3_EVENT       BIT(3)
#define AMD_PEER_PMETO_EVENT    BIT(4)
#define AMD_LINK_UP_EVENT       BIT(5)
#define AMD_PEER_D0_EVENT       BIT(6)
#define AMD_EVENT_INTMASK       (AMD_PEER_FLUSH_EVENT | AMD_PEER_RESET_EVENT | \
                                 AMD_LINK_DOWN_EVENT | AMD_PEER_D3_EVENT | \
                                 AMD_PEER_PMETO_EVENT | AMD_LINK_UP_EVENT | \
                                 AMD_PEER_D0_EVENT)

/* SIDEINFO register bits */
#define AMD_SIDE_READY          BIT(0)
#define AMD_SIDE_MASK           BIT(1)

/* Control register bits */
#define PMM_REG_CTL             BIT(0)
#define SMM_REG_CTL             BIT(1)

/* Constant counts */
#define AMD_DB_CNT              16
#define AMD_MSIX_VECTOR_CNT     17   /* 16 doorbell vectors + 1 event vector */
#define AMD_SPADS_CNT           64

/* BAR0 size: enough for self + peer registers and MSI-X tables */
#define BAR0_SIZE               0x4000

/* Internal register bank for one NTB side */
typedef struct NTBSideRegs {
    uint32_t cntl;
    uint32_t sideinfo;
    uint32_t smuack;
    uint32_t pmestat;
    uint32_t int_mask;
    uint32_t int_stat;
    uint16_t db_mask;
    uint16_t db_stat;
    uint16_t db_req;  /* write to send doorbell to other side */
    uint16_t pad;
    uint32_t spad[AMD_SPADS_CNT];
    uint64_t bar1_xlat;
    uint32_t bar1_lmt;
    uint32_t bar1_lmt_pad;
    uint64_t bar2_xlat;
    uint64_t bar3_xlat;
    uint64_t bar2_lmt;
    uint64_t bar3_lmt;
    uint64_t bar4_xlat;
    uint64_t bar5_xlat;
    uint64_t bar4_lmt;
    uint64_t bar5_lmt;
} NTBSideRegs;

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

    NTBSideRegs self;
    NTBSideRegs peer;

    QEMUTimer *hb_timer;  /* Heartbeat timer (unused) */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool event_pending = false;
    int i;

    if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
        /* INTx: level-triggered, assert if any unmasked interrupt */
        bool pending = (s->self.int_stat & ~s->self.int_mask) != 0;
        pending = pending || ((s->self.db_stat & ~s->self.db_mask) != 0);
        pci_set_irq(pdev, pending ? 1 : 0);
        return;
    }

    /* MSI or MSI-X */
    if (msix_enabled(pdev)) {
        /* Event vector (16) */
        if (s->self.int_stat & ~s->self.int_mask) {
            msix_notify(pdev, 16);
            event_pending = true;
        }

        /* Doorbell vectors (0..14) */
        uint16_t db_stat = s->self.db_stat;
        uint16_t db_mask = s->self.db_mask;
        for (i = 0; i < AMD_DB_CNT - 1; i++) {
            if ((db_stat & (1 << i)) && !(db_mask & (1 << i))) {
                msix_notify(pdev, i);
            }
        }

        if (!event_pending && !(db_stat & ~db_mask)) {
            /* No pending, nothing to deassert (MSI-X is edge) */
        }
    } else if (msi_enabled(pdev)) {
        /* MSI single vector: fire if any unmasked status */
        if ((s->self.int_stat & ~s->self.int_mask) ||
            (s->self.db_stat & ~s->self.db_mask)) {
            msi_notify(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    NTBSideRegs *side;
    hwaddr offset;

    if (addr < AMD_PEER_OFFSET) {
        side = &s->self;
        offset = addr;
    } else {
        side = &s->peer;
        offset = addr - AMD_PEER_OFFSET;
    }

    uint64_t val = 0;

    switch (offset) {
    case AMD_CNTL_OFFSET:
        val = side->cntl;
        break;
    case AMD_SIDEINFO_OFFSET:
        val = side->sideinfo;
        break;
    case AMD_SMUACK_OFFSET:
        val = side->smuack;
        break;
    case AMD_PMESTAT_OFFSET:
        val = side->pmestat;
        break;
    case AMD_INTMASK_OFFSET:
        val = side->int_mask;
        break;
    case AMD_INTSTAT_OFFSET:
        val = side->int_stat;
        break;
    case AMD_DBMASK_OFFSET:
        if (size == 2) {
            val = side->db_mask;
        } else {
            val = 0xffffffff;
        }
        break;
    case AMD_DBSTAT_OFFSET:
        if (size == 2) {
            val = side->db_stat;
        } else {
            val = 0xffffffff;
        }
        break;
    case AMD_DBREQ_OFFSET:
        /* Write-only, read as 0 */
        val = 0;
        break;
    default:
        if (offset >= AMD_SPAD_OFFSET && offset < AMD_SPAD_OFFSET + AMD_SPADS_CNT * 4) {
            int idx = (offset - AMD_SPAD_OFFSET) >> 2;
            if (idx < AMD_SPADS_CNT) {
                val = side->spad[idx];
            }
        } else if (offset >= AMD_BAR1XLAT_OFFSET && offset < AMD_BAR1XLAT_OFFSET + 8) {
            val = side->bar1_xlat;
        } else if (offset == AMD_BAR1LMT_OFFSET) {
            val = side->bar1_lmt;
        } else if (offset >= AMD_BAR23XLAT_OFFSET && offset < AMD_BAR23XLAT_OFFSET + 16) {
            int idx = (offset - AMD_BAR23XLAT_OFFSET) >> 3;
            if (idx < 2) {
                val = (idx == 0) ? side->bar2_xlat : side->bar3_xlat;
            }
        } else if (offset >= AMD_BAR23LMT_OFFSET && offset < AMD_BAR23LMT_OFFSET + 16) {
            int idx = (offset - AMD_BAR23LMT_OFFSET) >> 3;
            if (idx < 2) {
                val = (idx == 0) ? side->bar2_lmt : side->bar3_lmt;
            }
        } else if (offset >= AMD_BAR45XLAT_OFFSET && offset < AMD_BAR45XLAT_OFFSET + 16) {
            int idx = (offset - AMD_BAR45XLAT_OFFSET) >> 3;
            if (idx < 2) {
                val = (idx == 0) ? side->bar4_xlat : side->bar5_xlat;
            }
        } else if (offset >= AMD_BAR45LMT_OFFSET && offset < AMD_BAR45LMT_OFFSET + 16) {
            int idx = (offset - AMD_BAR45LMT_OFFSET) >> 3;
            if (idx < 2) {
                val = (idx == 0) ? side->bar4_lmt : side->bar5_lmt;
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "pcibase_mmio_read: unknown offset 0x%lx\n", (unsigned long)offset);
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    NTBSideRegs *side;
    hwaddr offset;

    if (addr < AMD_PEER_OFFSET) {
        side = &s->self;
        offset = addr;
    } else {
        side = &s->peer;
        offset = addr - AMD_PEER_OFFSET;
    }

    switch (offset) {
    case AMD_CNTL_OFFSET:
        side->cntl = val;
        break;
    case AMD_SIDEINFO_OFFSET:
        side->sideinfo = val;
        break;
    case AMD_SMUACK_OFFSET:
        side->smuack = val;
        break;
    case AMD_PMESTAT_OFFSET:
        side->pmestat = val;
        break;
    case AMD_INTMASK_OFFSET:
        side->int_mask = val;
        pcibase_update_irq(s);
        break;
    case AMD_INTSTAT_OFFSET:
        /* Write-1-to-clear */
        side->int_stat &= ~val;
        pcibase_update_irq(s);
        break;
    case AMD_DBMASK_OFFSET:
        if (size >= 2) {
            side->db_mask = val & 0xFFFF;
        } else {
            side->db_mask = (side->db_mask & 0xFF00) | (val & 0xFF);
        }
        pcibase_update_irq(s);
        break;
    case AMD_DBSTAT_OFFSET:
        if (size >= 2) {
            side->db_stat &= ~val;  /* W1C */
        }
        pcibase_update_irq(s);
        break;
    case AMD_DBREQ_OFFSET:
    {
        /* Writing to DBREQ on one side sets the opposite side's DBSTAT */
        NTBSideRegs *other = (side == &s->self) ? &s->peer : &s->self;
        other->db_stat |= (val & 0xFFFF);
        /* Trigger IRQ on the side that received the doorbell */
        if (other == &s->self) {
            pcibase_update_irq(s);
        } else {
            /* Peer side's IRQ is not emulated separately; use self for simplicity */
            pcibase_update_irq(s);
        }
        break;
    }
    default:
        if (offset >= AMD_SPAD_OFFSET && offset < AMD_SPAD_OFFSET + AMD_SPADS_CNT * 4) {
            int idx = (offset - AMD_SPAD_OFFSET) >> 2;
            if (idx < AMD_SPADS_CNT) {
                side->spad[idx] = val;
            }
        } else if (offset >= AMD_BAR1XLAT_OFFSET && offset < AMD_BAR1XLAT_OFFSET + 8) {
            side->bar1_xlat = val;
        } else if (offset == AMD_BAR1LMT_OFFSET) {
            side->bar1_lmt = val;
        } else if (offset >= AMD_BAR23XLAT_OFFSET && offset < AMD_BAR23XLAT_OFFSET + 16) {
            int idx = (offset - AMD_BAR23XLAT_OFFSET) >> 3;
            if (idx < 2) {
                if (idx == 0) side->bar2_xlat = val;
                else side->bar3_xlat = val;
            }
        } else if (offset >= AMD_BAR23LMT_OFFSET && offset < AMD_BAR23LMT_OFFSET + 16) {
            int idx = (offset - AMD_BAR23LMT_OFFSET) >> 3;
            if (idx < 2) {
                if (idx == 0) side->bar2_lmt = val;
                else side->bar3_lmt = val;
            }
        } else if (offset >= AMD_BAR45XLAT_OFFSET && offset < AMD_BAR45XLAT_OFFSET + 16) {
            int idx = (offset - AMD_BAR45XLAT_OFFSET) >> 3;
            if (idx < 2) {
                if (idx == 0) side->bar4_xlat = val;
                else side->bar5_xlat = val;
            }
        } else if (offset >= AMD_BAR45LMT_OFFSET && offset < AMD_BAR45LMT_OFFSET + 16) {
            int idx = (offset - AMD_BAR45LMT_OFFSET) >> 3;
            if (idx < 2) {
                if (idx == 0) side->bar4_lmt = val;
                else side->bar5_lmt = val;
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "pcibase_mmio_write: unknown offset 0x%lx\n", (unsigned long)offset);
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->self, 0, sizeof(s->self));
    memset(&s->peer, 0, sizeof(s->peer));
    /* Set initial SIDEINFO: primary side (SIDE_MASK = 0), not ready yet */
    s->self.sideinfo = 0;
    s->peer.sideinfo = 0;
    /* Set initial INTMASK to EVENT_INTMASK, as done in ndev_init_struct */
    s->self.int_mask = AMD_EVENT_INTMASK;
    s->peer.int_mask = AMD_EVENT_INTMASK;
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
        /* Not used */
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

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

    /* Set up BAR0 as the main MMIO region */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "ntb-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI-X support with 17 vectors (doorbell 0-14 + event) */
    s->has_msix = true;
    if (msix_init(pdev, AMD_MSIX_VECTOR_CNT,
                  &s->bar_regions[0], 0, 0x3000,
                  &s->bar_regions[0], 0, 0x3800,
                  0, errp)) {
        error_propagate(errp, NULL);
        return;
    }

    /* No DMA or heartbeat timer required */
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
    .name = "ntb_hw_amd_pci",
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
