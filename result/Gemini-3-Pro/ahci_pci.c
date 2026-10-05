/*
 * QEMU AHCI PCI Device Model
 * Based on Linux driver: drivers/ata/ahci.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "qemu/log.h"
#include "qom/object.h"
#include "qapi/error.h"

#define TYPE_PCIBASE_DEVICE "ahci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* AHCI Register Offsets */
#define AHCI_PCI_BAR_STANDARD   5
#define AHCI_PCI_BAR_REG_SIZE   0x10000

/* Host Registers */
#define AHCI_HOST_CAP           0x00
#define AHCI_HOST_CTL           0x04
#define AHCI_HOST_IRQ_STAT      0x08
#define AHCI_HOST_PORTS_IMPL    0x0c
#define AHCI_HOST_VERSION       0x10
#define AHCI_HOST_EM_LOC        0x1c
#define AHCI_HOST_EM_CTL        0x20
#define AHCI_HOST_CAP2          0x24
#define AHCI_HOST_BOHC          0x28

/* Host Control Bits */
#define HOST_CTL_AHCI_EN        (1 << 31)
#define HOST_CTL_IE             (1 << 1)
#define HOST_CTL_RST            (1 << 0)
#define HOST_MRSM               (1 << 2)

/* Host Capability Bits */
#define HOST_CAP_64             (1 << 31)
#define HOST_CAP_NCQ            (1 << 30)
#define HOST_CAP_SNTF           (1 << 29)
#define HOST_CAP_MPS            (1 << 28)
#define HOST_CAP_SSS            (1 << 27)
#define HOST_CAP_ALPM           (1 << 26)
#define HOST_CAP_LED            (1 << 25)
#define HOST_CAP_CLO            (1 << 24)
#define HOST_CAP_ISS            (0xf << 20)
#define HOST_CAP_SAM            (1 << 18)
#define HOST_CAP_PMP            (1 << 17)
#define HOST_CAP_FBS            (1 << 16)
#define HOST_CAP_PMD            (1 << 15)
#define HOST_CAP_SSC            (1 << 14)
#define HOST_CAP_PSC            (1 << 13)
#define HOST_CAP_NCS            (0x1f << 8)
#define HOST_CAP_CCCS           (1 << 7)
#define HOST_CAP_EMS            (1 << 6)
#define HOST_CAP_SXS            (1 << 5)
#define HOST_CAP_NP             (0x1f << 0)

/* Port Registers (Offset 0x100 + port * 0x80) */
#define AHCI_PORT_CLB           0x00
#define AHCI_PORT_CLB_HI        0x04
#define AHCI_PORT_FB            0x08
#define AHCI_PORT_FB_HI         0x0c
#define AHCI_PORT_IRQ_STAT      0x10
#define AHCI_PORT_IRQ_MASK      0x14
#define AHCI_PORT_CMD           0x18
#define AHCI_PORT_TFDATA        0x20
#define AHCI_PORT_SIG           0x24
#define AHCI_PORT_SCR_STAT      0x28
#define AHCI_PORT_SCR_CTL       0x2c
#define AHCI_PORT_SCR_ERR       0x30
#define AHCI_PORT_SCR_ACT       0x34
#define AHCI_PORT_CMD_ISSUE     0x38
#define AHCI_PORT_RESERVED      0x3c

/* Port Command Bits */
#define PORT_CMD_ST             (1 << 0)
#define PORT_CMD_SUD            (1 << 1)
#define PORT_CMD_POD            (1 << 2)
#define PORT_CMD_CLO            (1 << 3)
#define PORT_CMD_FRE            (1 << 4)
#define PORT_CMD_FR             (1 << 14)
#define PORT_CMD_CR             (1 << 15)
#define PORT_CMD_HPCP           (1 << 18)
#define PORT_CMD_ESP            (1 << 21)
#define PORT_CMD_ICC_MASK       (0xf << 28)

/* Vendor Specific */
#define AHCI_VSCAP              0xa4
#define AHCI_REMAP_CAP          0xa8

typedef struct AHCIPortState {
    uint32_t clb;
    uint32_t clb_hi;
    uint32_t fb;
    uint32_t fb_hi;
    uint32_t irq_stat;
    uint32_t irq_mask;
    uint32_t cmd;
    uint32_t tfdata;
    uint32_t sig;
    uint32_t scr_stat;
    uint32_t scr_ctl;
    uint32_t scr_err;
    uint32_t scr_act;
    uint32_t cmd_issue;
} AHCIPortState;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion mmio;

    bool has_msi;
    bool has_msix;

    uint32_t cap;
    uint32_t ghc;
    uint32_t irq_status;
    uint32_t ports_impl;
    uint32_t version;
    uint32_t em_loc;
    uint32_t em_ctl;
    uint32_t cap2;
    uint32_t bohc;

    AHCIPortState ports[32];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    int i;
    uint32_t irq_stat = 0;

    /* Aggregate port interrupts */
    for (i = 0; i < 32; i++) {
        if ((s->ports_impl & (1 << i)) && s->ports[i].irq_stat) {
            irq_stat |= (1 << i);
        }
    }
    
    s->irq_status = irq_stat;

    if ((s->ghc & HOST_CTL_IE) && s->irq_status) {
        if (s->has_msix) {
            msix_notify(PCI_DEVICE(s), 0);
        } else if (s->has_msi) {
            msi_notify(PCI_DEVICE(s), 0);
        } else {
            pci_set_irq(PCI_DEVICE(s), 1);
        }
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

static void pcibase_port_reset(PCIBaseState *s, int i)
{
    AHCIPortState *pr = &s->ports[i];
    pr->clb = 0;
    pr->clb_hi = 0;
    pr->fb = 0;
    pr->fb_hi = 0;
    pr->irq_stat = 0;
    pr->irq_mask = 0;
    pr->cmd = 0;
    pr->tfdata = 0x7F; /* Busy */
    pr->sig = 0xFFFFFFFF;
    pr->scr_stat = 0;
    pr->scr_ctl = 0;
    pr->scr_err = 0;
    pr->scr_act = 0;
    pr->cmd_issue = 0;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    int i;

    s->ghc = 0;
    s->irq_status = 0;
    s->cap = HOST_CAP_64 | HOST_CAP_NCQ | HOST_CAP_SSS | HOST_CAP_SAM | (0x1F); /* 32 ports */
    s->ports_impl = 0xFFFFFFFF; /* All ports implemented */
    s->version = 0x01030000; /* AHCI 1.3 */
    s->cap2 = 0;
    s->bohc = 0;

    for (i = 0; i < 32; i++) {
        pcibase_port_reset(s, i);
    }
    
    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x100) {
        switch (addr) {
        case AHCI_HOST_CAP:
            val = s->cap;
            break;
        case AHCI_HOST_CTL:
            val = s->ghc;
            break;
        case AHCI_HOST_IRQ_STAT:
            val = s->irq_status;
            break;
        case AHCI_HOST_PORTS_IMPL:
            val = s->ports_impl;
            break;
        case AHCI_HOST_VERSION:
            val = s->version;
            break;
        case AHCI_HOST_EM_LOC:
            val = s->em_loc;
            break;
        case AHCI_HOST_EM_CTL:
            val = s->em_ctl;
            break;
        case AHCI_HOST_CAP2:
            val = s->cap2;
            break;
        case AHCI_HOST_BOHC:
            val = s->bohc;
            break;
        case AHCI_VSCAP:
            val = 0; /* No VSCAP */
            break;
        case AHCI_REMAP_CAP:
            val = 0; /* No Remap */
            break;
        }
    } else {
        int port = (addr - 0x100) / 0x80;
        int off = (addr - 0x100) % 0x80;
        
        if (port < 32) {
            AHCIPortState *pr = &s->ports[port];
            switch (off) {
            case AHCI_PORT_CLB: val = pr->clb; break;
            case AHCI_PORT_CLB_HI: val = pr->clb_hi; break;
            case AHCI_PORT_FB: val = pr->fb; break;
            case AHCI_PORT_FB_HI: val = pr->fb_hi; break;
            case AHCI_PORT_IRQ_STAT: val = pr->irq_stat; break;
            case AHCI_PORT_IRQ_MASK: val = pr->irq_mask; break;
            case AHCI_PORT_CMD: val = pr->cmd; break;
            case AHCI_PORT_TFDATA: val = pr->tfdata; break;
            case AHCI_PORT_SIG: val = pr->sig; break;
            case AHCI_PORT_SCR_STAT: val = pr->scr_stat; break;
            case AHCI_PORT_SCR_CTL: val = pr->scr_ctl; break;
            case AHCI_PORT_SCR_ERR: val = pr->scr_err; break;
            case AHCI_PORT_SCR_ACT: val = pr->scr_act; break;
            case AHCI_PORT_CMD_ISSUE: val = pr->cmd_issue; break;
            }
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x100) {
        switch (addr) {
        case AHCI_HOST_CTL:
            if (val & HOST_CTL_RST) {
                pcibase_reset(DEVICE(s));
            } else {
                s->ghc = val;
                pcibase_update_irq(s);
            }
            break;
        case AHCI_HOST_IRQ_STAT:
            s->irq_status &= ~val;
            pcibase_update_irq(s);
            break;
        case AHCI_HOST_PORTS_IMPL:
            /* Read-only usually, but some controllers allow write */
            break;
        }
    } else {
        int port = (addr - 0x100) / 0x80;
        int off = (addr - 0x100) % 0x80;

        if (port < 32) {
            AHCIPortState *pr = &s->ports[port];
            switch (off) {
            case AHCI_PORT_CLB: pr->clb = val; break;
            case AHCI_PORT_CLB_HI: pr->clb_hi = val; break;
            case AHCI_PORT_FB: pr->fb = val; break;
            case AHCI_PORT_FB_HI: pr->fb_hi = val; break;
            case AHCI_PORT_IRQ_STAT:
                pr->irq_stat &= ~val;
                pcibase_update_irq(s);
                break;
            case AHCI_PORT_IRQ_MASK:
                pr->irq_mask = val;
                pcibase_update_irq(s);
                break;
            case AHCI_PORT_CMD:
                pr->cmd = val;
                break;
            case AHCI_PORT_SCR_CTL:
                pr->scr_ctl = val;
                if ((val & 0xF) == 1) { /* Reset */
                    pr->scr_stat = 0;
                    pr->sig = 0xFFFFFFFF;
                    pr->tfdata = 0x80; /* Busy */
                } else if ((val & 0xF) == 0) { /* No Reset */
                    /* Simulate device detection */
                    pr->scr_stat = 0x123; /* Present, Gen 1, etc */
                    pr->sig = 0x0101EB14; /* SATA */
                    pr->tfdata = 0x50; /* Ready */
                }
                break;
            case AHCI_PORT_SCR_ERR: pr->scr_err = val; break;
            case AHCI_PORT_CMD_ISSUE:
                pr->cmd_issue = val;
                break;
            }
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x2652); /* ICH6 AHCI */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SATA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "ahci-mmio", AHCI_PCI_BAR_REG_SIZE);
    pci_register_bar(pdev, AHCI_PCI_BAR_STANDARD, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        /* Failed to init MSI, but not fatal */
    } else {
        s->has_msi = true;
    }

    /* Place MSI-X table at offset 0x2000 to avoid AHCI registers (0x0000-0x1100) */
    if (msix_init(pdev, 32, &s->mmio, AHCI_PCI_BAR_STANDARD, 0x2000, 
                  &s->mmio, AHCI_PCI_BAR_STANDARD, 0x3000, 0, errp)) {
        /* Failed to init MSI-X */
    } else {
        s->has_msix = true;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, &s->mmio, &s->mmio);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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

    static const TypeInfo ahci_pci_info = {
        .name = "ahci_pci",
        .parent = TYPE_PCIBASE_DEVICE,
    };

    type_register_static(&pcibase_info);
    type_register_static(&ahci_pci_info);
}

type_init(pcibase_register_types)