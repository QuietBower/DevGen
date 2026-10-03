/*
 * QEMU ACard AHCI SATA Controller Emulation
 *
 * Derived from Linux kernel driver: drivers/ata/acard-ahci.c
 *
 * Copyright (c) 2025 QEMU contributors
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "acard_ahci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* AHCI register offsets */
#define HOST_CAP                0x00 /* host capabilities */
#define HOST_CTL                0x04 /* global host control */
#define HOST_IRQ_STAT           0x08 /* interrupt status */
#define HOST_PORTS_IMPL         0x0c /* bitmap of implemented ports */
#define HOST_VERSION            0x10 /* AHCI spec. version compliancy */
#define HOST_CAP2               0x24 /* extended capabilities */
#define HOST_EM_LOC             0x50 /* enclosure management location */
#define HOST_EM_CTL             0x54 /* enclosure management control */

#define PORT_CMD                0x14 /* port x cmd reg */
#define PORT_CMD_FBSCP          (1 << 26) /* FBS Capable Port */
#define PORT_CMD_CAP            (0xf << 28) /* command capability mask */
#define PORT_CMD_START          (1 << 0)  /* Start DMA engine */
#define PORT_CMD_LIST_ON        (1 << 1)  /* Command list on */
#define PORT_CMD_SPIN_UP        (1 << 10) /* Spin up device */
#define PORT_CMD_ICC_MASK       (0xf << 28) /* Interface communication control */
#define PORT_CMD_ICC_ACTIVE     (0x1 << 28) /* Active state */
#define PORT_CMD_PMP            (1 << 17) /* Port multiplier enable */

#define PORT_IRQ_MASK           0x10      /* port interrupt enable mask */

#define AHCI_PCI_BAR            5

/* AHCI capability bits */
#define HOST_CAP_SSS            (1 << 27) /* Staggered Spin-up */
#define HOST_CAP_NCQ            (1 << 30) /* Native Command Queueing */
#define HOST_CAP_64             (1 << 31) /* PCI DAC (64-bit DMA) */
#define HOST_CAP_PMP            (1 << 17) /* Port Multiplier support */
#define HOST_CAP_FBS            (1ULL << 32) /* FIS-based switching */
#define HOST_CAP_SNTF           (1 << 29) /* SNotification */
#define HOST_CAP_ALPM           (1 << 26) /* Aggressive Link PM */
#define HOST_CAP_SXS            (1 << 5)  /* External SATA */
#define HOST_CAP_EMS            (1 << 6)  /* Enclosure Management */
#define HOST_CAP_MPS            (1 << 23) /* Mechanical Presence Switch */

/* HOST_CAP2 bits */
#define HOST_CAP2_SDS           (1 << 3)  /* DEVSLP */
#define HOST_CAP2_SADM          (1 << 4)  /* SNotification Auto DIPM */
#define HOST_CAP2_DESO          (1 << 5)  /* Device Sleep Operation */
#define HOST_CAP2_APST          (1 << 6)  /* Automatic Partial to Slumber Transitions */
#define HOST_CAP2_NVMHCI        (1 << 7)  /* NVMHCI */
#define HOST_CAP2_BOH           (1 << 8)  /* BIOS/OS Handoff */

/* HOST_CTL bits */
#define HOST_IRQ_EN             (1 << 1) /* Global IRQ enable */
#define HOST_RESET              (1 << 0) /* Reset controller */
#define HOST_AHCI_EN            (1 << 31) /* AHCI Enable */

/* EM_CTL bits */
#define EM_CTRL_MSG_TYPE        (0xf << 16)
#define EM_CTL_ALHD             (1 << 9) /* Activity LED Heartbeat Disable */

/* Port interrupt bits */
#define PORT_IRQ_HBUS_ERR       (1 << 0)
#define PORT_IRQ_IF_ERR         (1 << 1)
#define PORT_IRQ_CONNECT        (1 << 2)
#define PORT_IRQ_PHYRDY         (1 << 3)
#define PORT_IRQ_UNK_FIS        (1 << 4)
#define PORT_IRQ_BAD_PMP        (1 << 5)
#define PORT_IRQ_TF_ERR         (1 << 6)
#define PORT_IRQ_HBUS_DATA_ERR  (1 << 7)
#define PORT_IRQ_IF_NONFATAL    (1 << 8)
#define PORT_IRQ_OVERFLOW       (1 << 9)
#define PORT_IRQ_PIOS_FIS       (1 << 20)
#define PORT_IRQ_D2H_REG_FIS    (1 << 21)

#define VENDOR_ID 0x1191
#define DEVICE_ID 0x000d
#define CLASS_ID PCI_CLASS_STORAGE_SATA

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* Register shadows */
    uint32_t host_cap;
    uint32_t host_cap2;
    uint32_t host_ctl;
    uint32_t host_ports_impl;
    uint32_t host_version;
    uint32_t port_cmd[32]; /* up to 32 ports */
    uint32_t host_irq_stat;
    uint32_t host_em_loc;
    uint32_t host_em_ctl;
    uint32_t port_irq_mask[32]; /* interrupt masks per port */

    /* Status fields */

    /* reset/probe state */

    /* power mgmt */

    /* other fields */
};


/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

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
        val = s->host_ports_impl;
        break;
    case HOST_VERSION:
        val = s->host_version;
        break;
    case HOST_CAP2:
        val = s->host_cap2;
        break;
    case HOST_EM_LOC:
        val = s->host_em_loc;
        break;
    case HOST_EM_CTL:
        val = s->host_em_ctl;
        break;
    default:
        /* Port-specific registers */
        if (addr >= 0x100 && addr < 0x100 + 32*0x80) {
            int port = (addr - 0x100) / 0x80;
            int offset = (addr - 0x100) % 0x80;
            if (offset == PORT_CMD) {
                val = s->port_cmd[port];
            } else if (offset == PORT_IRQ_MASK) {
                val = s->port_irq_mask[port];
            }
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HOST_CTL:
        if (val & HOST_RESET) {
            /* Perform reset */
            s->host_ctl |= HOST_RESET;
            /* Simulate reset completion */
            s->host_ctl &= ~HOST_RESET;
            /* Restore initial config after reset */
            s->host_cap = (1U << 27) | (1U << 30) | (1U << 31) | (1U << 17) | 0x1f;
            s->host_ports_impl = 0x1;
            s->port_cmd[0] = PORT_CMD_FBSCP;
        } else {
            s->host_ctl = val;
        }
        break;
    case HOST_IRQ_STAT:
        /* Clear interrupt status bits */
        s->host_irq_stat &= ~(val & 0xFFFFFFFF);
        break;
    default:
        /* Port-specific registers */
        if (addr >= 0x100 && addr < 0x100 + 32*0x80) {
            int port = (addr - 0x100) / 0x80;
            int offset = (addr - 0x100) % 0x80;
            if (offset == PORT_CMD) {
                s->port_cmd[port] = val;
                /* Handle AHCI_EN bit */
                if (val & HOST_AHCI_EN) {
                    s->host_ctl |= HOST_AHCI_EN;
                }
            } else if (offset == PORT_IRQ_MASK) {
                s->port_irq_mask[port] = val;
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    /* Reset AHCI registers to default values */
    s->host_cap = (1U << 27) | (1U << 30) | (1U << 31) | (1U << 17) | 0x1f; /* SSS, NCQ, 64-bit, PMP, FBS, 32 ports */
    s->host_cap2 = 0;
    s->host_ctl = 0;
    s->host_irq_stat = 0;
    s->host_ports_impl = 0x1; /* Only port 0 implemented */
    s->host_version = 0x10300; /* AHCI 1.3 */
    s->host_em_loc = 0;
    s->host_em_ctl = 0;
    memset(s->port_cmd, 0, sizeof(s->port_cmd));
    s->port_cmd[0] = PORT_CMD_FBSCP; /* Port 0 supports FBS */
    memset(s->port_irq_mask, 0, sizeof(s->port_irq_mask));

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    /* optional: override certain config offsets if needed */


    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    

    /* Set up BAR5 as MMIO region for AHCI */
    s->bar_info[0] = (BARInfo){ .index = 5, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "acard-ahci-mmio" };
    s->num_bars = 1;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;


    /* Power management / other init */
    /* Example:
    *   pdev->pm_cap = true;
    */
    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    /* free mmio_backing or other allocations */


    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);

    /* optional */
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

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
