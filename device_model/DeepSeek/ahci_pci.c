/*
 * QEMU PCI AHCI Controller Device Model
 * Based on the Linux ahci.c driver analysis.
 * Implements minimal register set required for driver probe.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "ahci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs from driver */
#define PCI_VENDOR_ID_AHCI     0x8086
#define PCI_DEVICE_ID_AHCI     0x06d6

/* AHCI global register offsets */
#define HOST_CAP                0x00
#define HOST_CTL                0x04
#define HOST_IRQ_STAT           0x08
#define HOST_PORTS_IMPL         0x0C
#define HOST_VERSION            0x10
#define HOST_CCC_CTL            0x14
#define HOST_CCC_PORTS          0x18
#define HOST_EM_LOC             0x1C
#define HOST_EM_CTL             0x20
#define HOST_CAP2               0x24
#define HOST_BOHC               0x28

/* Global HOST_CTL bits */
#define HOST_RESET              (1 << 0)
#define HOST_IRQ_EN             (1 << 1)
#define HOST_MRSM               (1 << 2)

/* Per-port registers (offsets from port base) */
#define PORT_CLB                0x00
#define PORT_CLBU               0x04
#define PORT_FB                 0x08
#define PORT_FBU                0x0C
#define PORT_IS                 0x10
#define PORT_IE                 0x14
#define PORT_CMD                0x18
#define PORT_TFDATA             0x20
#define PORT_SIG                0x24
#define PORT_SSTS               0x28
#define PORT_SCTL               0x2C
#define PORT_SERR               0x30
#define PORT_SACT               0x34
#define PORT_CI                 0x38
#define PORT_SNTF               0x3C
#define PORT_FBS                0x40

/* Port CMD bits */
#define PORT_CMD_ST             (1 << 0)
#define PORT_CMD_SUD            (1 << 1)
#define PORT_CMD_POD            (1 << 2)
#define PORT_CMD_CLO            (1 << 3)
#define PORT_CMD_FRE            (1 << 4)
#define PORT_CMD_CCCMASK        (0x1f << 8)
#define PORT_CMD_ICC_MASK       (0xf << 28)

/* Port IS/IE bits */
#define PORT_IRQ_COLD_PRES      (1U << 31)
#define PORT_IRQ_TF_ERR         (1 << 30)
#define PORT_IRQ_HBUS_ERR       (1 << 29)
#define PORT_IRQ_HBUS_DATA_ERR  (1 << 28)
#define PORT_IRQ_IF_ERR         (1 << 27)
#define PORT_IRQ_IF_NONFATAL    (1 << 26)
#define PORT_IRQ_OVERFLOW       (1 << 24)
#define PORT_IRQ_BAD_PMP        (1 << 23)
#define PORT_IRQ_PHYRDY         (1 << 22)
#define PORT_IRQ_DMPS           (1 << 16)
#define PORT_IRQ_CONNECT        (1 << 7)
#define PORT_IRQ_D2H_REG_FIS    (1 << 5)
#define PORT_IRQ_PIOS_FIS       (1 << 4)
#define PORT_IRQ_DMA_SETUP_FIS  (1 << 3)
#define PORT_IRQ_SDB_FIS        (1 << 2)
#define PORT_IRQ_DS_FIS         (1 << 1)
#define PORT_IRQ_UNKNOWN_FIS    (1 << 0)

#define AHCI_PORT_BASE          0x100
#define AHCI_PORT_OFFSET        0x80
#define AHCI_MAX_PORTS          1  /* emulate single port */

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

typedef struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t irq_status;

    /* Global AHCI registers */
    uint32_t host_cap;
    uint32_t host_ctl;
    uint32_t host_irq_stat;
    uint32_t ports_impl;
    uint32_t host_version;
    uint32_t cap2;

    /* Port 0 registers */
    uint32_t p0_clb;
    uint32_t p0_clbu;
    uint32_t p0_fb;
    uint32_t p0_fbu;
    uint32_t p0_is;
    uint32_t p0_ie;
    uint32_t p0_cmd;
    uint32_t p0_tfdata;
    uint32_t p0_sig;
    uint32_t p0_ssts;
    uint32_t p0_sctl;
    uint32_t p0_serr;
    uint32_t p0_sact;
    uint32_t p0_ci;
} PCIBaseState;

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = false;

    /* Check each port for pending and enabled interrupts */
    if (s->ports_impl & (1 << 0)) {
        if (s->p0_is & s->p0_ie) {
            level = true;
        }
    }

    if (level && (s->host_ctl & HOST_IRQ_EN)) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return (uint64_t)-1;
    }

    if (addr < AHCI_PORT_BASE) {
        /* Global registers */
        switch (addr) {
        case HOST_CAP:
            val = s->host_cap;
            break;
        case HOST_CTL:
            val = s->host_ctl;
            break;
        case HOST_IRQ_STAT:
            val = s->host_irq_stat;
            break;
        case HOST_PORTS_IMPL:
            val = s->ports_impl;
            break;
        case HOST_VERSION:
            val = s->host_version;
            break;
        case HOST_CAP2:
            val = s->cap2;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "ahci: unknown global read at 0x%" HWADDR_PRIx "\n", addr);
            break;
        }
    } else if (addr >= AHCI_PORT_BASE && addr < AHCI_PORT_BASE + AHCI_PORT_OFFSET * AHCI_MAX_PORTS) {
        /* Per-port registers, only port 0 implemented */
        hwaddr port_addr = addr - AHCI_PORT_BASE;
        if (port_addr >= AHCI_PORT_OFFSET) {
            qemu_log_mask(LOG_UNIMP, "ahci: unimplemented port %d read\n", (int)(port_addr / AHCI_PORT_OFFSET));
            return 0;
        }
        switch (port_addr) {
        case PORT_CLB:
            val = s->p0_clb;
            break;
        case PORT_CLBU:
            val = s->p0_clbu;
            break;
        case PORT_FB:
            val = s->p0_fb;
            break;
        case PORT_FBU:
            val = s->p0_fbu;
            break;
        case PORT_IS:
            val = s->p0_is;
            break;
        case PORT_IE:
            val = s->p0_ie;
            break;
        case PORT_CMD:
            val = s->p0_cmd;
            break;
        case PORT_TFDATA:
            val = s->p0_tfdata;
            break;
        case PORT_SIG:
            val = s->p0_sig;
            break;
        case PORT_SSTS:
            val = s->p0_ssts;
            break;
        case PORT_SCTL:
            val = s->p0_sctl;
            break;
        case PORT_SERR:
            val = s->p0_serr;
            break;
        case PORT_SACT:
            val = s->p0_sact;
            break;
        case PORT_CI:
            val = s->p0_ci;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "ahci: unknown port read at 0x%" HWADDR_PRIx "\n", port_addr);
            break;
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "ahci: unknown read at 0x%" HWADDR_PRIx "\n", addr);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    if (addr < AHCI_PORT_BASE) {
        /* Global registers */
        switch (addr) {
        case HOST_CAP:
            /* RO */
            break;
        case HOST_CTL:
        {
            uint32_t new_val = val & 0xFFFFFFFF;
            bool start_reset = false;
            if ((new_val & HOST_RESET) && !(s->host_ctl & HOST_RESET)) {
                start_reset = true;
            }
            s->host_ctl = new_val;
            if (start_reset) {
                /* Perform reset immediately */
                /* Reset port 0 registers to default values */
                s->p0_clb = 0;
                s->p0_clbu = 0;
                s->p0_fb = 0;
                s->p0_fbu = 0;
                s->p0_is = 0;
                s->p0_ie = 0;
                s->p0_cmd = 0;
                s->p0_tfdata = 0;
                s->p0_sig = 0xFFFFFFFF;
                s->p0_ssts = 0;
                s->p0_sctl = 0;
                s->p0_serr = 0;
                s->p0_sact = 0;
                s->p0_ci = 0;

                /* Clear global interrupt status */
                s->host_irq_stat = 0;

                /* Reset complete, clear HR bit */
                s->host_ctl &= ~HOST_RESET;

                /* Update IRQ state in case any interrupt conditions cleared */
                pcibase_update_irq(s);
            }
            break;
        }
        case HOST_IRQ_STAT:
            /* W1C */
            s->host_irq_stat &= ~(val & 0xFFFFFFFF);
            break;
        case HOST_PORTS_IMPL:
            /* RO */
            break;
        case HOST_VERSION:
            /* RO */
            break;
        case HOST_CAP2:
            /* RO */
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "ahci: unknown global write at 0x%" HWADDR_PRIx "\n", addr);
            break;
        }
    } else if (addr >= AHCI_PORT_BASE && addr < AHCI_PORT_BASE + AHCI_PORT_OFFSET * AHCI_MAX_PORTS) {
        hwaddr port_addr = addr - AHCI_PORT_BASE;
        if (port_addr >= AHCI_PORT_OFFSET) {
            qemu_log_mask(LOG_UNIMP, "ahci: unimplemented port %d write\n", (int)(port_addr / AHCI_PORT_OFFSET));
            return;
        }
        switch (port_addr) {
        case PORT_CLB:
            s->p0_clb = val;
            break;
        case PORT_CLBU:
            s->p0_clbu = val;
            break;
        case PORT_FB:
            s->p0_fb = val;
            break;
        case PORT_FBU:
            s->p0_fbu = val;
            break;
        case PORT_IS:
            /* W1C */
            s->p0_is &= ~(val & 0xFFFFFFFF);
            break;
        case PORT_IE:
            s->p0_ie = val & 0xFFFFFFFF;
            break;
        case PORT_CMD:
            s->p0_cmd = val & 0xFFFFFFFF;
            break;
        case PORT_TFDATA:
            s->p0_tfdata = val;
            break;
        case PORT_SIG:
            s->p0_sig = val;
            break;
        case PORT_SSTS:
            s->p0_ssts = val;
            break;
        case PORT_SCTL:
            s->p0_sctl = val;
            break;
        case PORT_SERR:
            s->p0_serr = val;
            break;
        case PORT_SACT:
            s->p0_sact = val;
            break;
        case PORT_CI:
            s->p0_ci = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "ahci: unknown port write at 0x%" HWADDR_PRIx "\n", port_addr);
            break;
        }
    }
    pcibase_update_irq(s);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Global register defaults */
    s->host_cap = 0x00000000;    /* minimal, no NCQ, 64-bit, etc. */
    s->host_ctl = 0x00000000;
    s->host_irq_stat = 0x00000000;
    s->ports_impl = 0x00000001;  /* port 0 implemented */
    s->host_version = 0x00010200; /* version 1.2 */
    s->cap2 = 0x00000000;

    /* Port 0 default */
    s->p0_clb = 0x00000000;
    s->p0_clbu = 0x00000000;
    s->p0_fb = 0x00000000;
    s->p0_fbu = 0x00000000;
    s->p0_is = 0x00000000;
    s->p0_ie = 0x00000000;
    s->p0_cmd = 0x00000000;
    s->p0_tfdata = 0x00000000;
    s->p0_sig = 0xFFFFFFFF;      /* invalid signature */
    s->p0_ssts = 0x00000000;
    s->p0_sctl = 0x00000000;
    s->p0_serr = 0x00000000;
    s->p0_sact = 0x00000000;
    s->p0_ci = 0x00000000;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_AHCI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_AHCI);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0106);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x01);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Set up BAR5 (standard AHCI BAR) */
    s->num_bars = 1;
    s->bar_info[0].index = 5;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;  /* 4KB minimum for AHCI */
    s->bar_info[0].name = "ahci-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "ahci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(host_cap, PCIBaseState),
        VMSTATE_UINT32(host_ctl, PCIBaseState),
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
