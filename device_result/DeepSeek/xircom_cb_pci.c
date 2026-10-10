/*
 * QEMU Xircom CB Ethernet Virtual Device Model
 * Based on Linux driver xircom_cb.c
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

#define TYPE_PCIBASE_DEVICE "xircom_cb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* DMA Descriptor layout extracted from struct descriptor in xircom_cb.c */
typedef struct {
    uint16_t req_count;
    uint16_t control;
    uint32_t data_address;
    uint32_t branch_address;
    uint16_t res_count;
    uint16_t transfer_status;
} Desc;

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI Vendor and Device IDs (first entry from pci_device_id table) */
#define PCI_VENDOR_ID_XIRCOM 0x115d
#define PCI_DEVICE_ID_XIRCOM_CB 0x0003
/* Class ID: Network Controller, Ethernet */
#define PCI_CLASS_ID_ETHERNET 0x0200

/* Register offsets (CSR - Command/Status Registers) */
#define REG_CSR0   0x00
#define REG_CSR1   0x08
#define REG_CSR2   0x10
#define REG_CSR3   0x18
#define REG_CSR4   0x20
#define REG_CSR5   0x28
#define REG_CSR6   0x30
#define REG_CSR7   0x38
#define REG_CSR8   0x40
#define REG_CSR9   0x48
#define REG_CSR10  0x50
#define REG_CSR11  0x58
#define REG_CSR12  0x60
#define REG_CSR13  0x68
#define REG_CSR14  0x70
#define REG_CSR15  0x78
#define REG_CSR16  0x80
/* PCI Power Management register offset in config space */
#define PCI_POWERMGMT 0x40

/* Number of transmit/receive descriptors */
#define NUMDESCRIPTORS 4

/* Buffer offsets within DMA buffer (currently unused, keep for potential future use) */
/* static int bufferoffsets[NUMDESCRIPTORS] = {128, 2048, 4096, 6144}; */

/* BAR0 size (I/O ports) */
#define BAR0_SIZE 0x100

/* Boot ROM data containing MAC address tuple */
static const uint8_t xircom_bootrom[266] = {
    [0x00 ... 0xFF] = 0,
    /* At offset 0x100: driver starts scanning CIS chain */
    [0x100] = 0x22,         /* CISTPL_FUNCID */
    [0x101] = 0x00,         /* link: end of list */
    [0x102] = 0x04,         /* data_id: network */
    [0x103] = 0x06,         /* data_count: 6 bytes */
    [0x104] = 0x00,         /* MAC byte 0 */
    [0x105] = 0x11,         /* MAC byte 1 */
    [0x106] = 0x22,         /* MAC byte 2 */
    [0x107] = 0x33,         /* MAC byte 3 */
    [0x108] = 0x44,         /* MAC byte 4 */
    [0x109] = 0x55,         /* MAC byte 5 */
};

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
    struct {
        uint32_t CSR0;
        uint32_t CSR1;
        uint32_t CSR2;
        uint32_t CSR3;
        uint32_t CSR4;
        uint32_t CSR5;
        uint32_t CSR6;
        uint32_t CSR7;
        uint32_t CSR8;
        uint32_t CSR9;
        uint32_t CSR10;
        uint32_t CSR11;
        uint32_t CSR12;
        uint32_t CSR13;
        uint32_t CSR14;
        uint32_t CSR15;
        uint32_t CSR16;
    } regs;

    /* DMA Context */
    struct {
        dma_addr_t rx_dma;
        dma_addr_t tx_dma;
        int tx_used;
        int rx_used;
    } dma;

    uint32_t status_flags;  /* Operational status bits */

    /* Boot ROM address pointer for CSR10/CSR9 interface */
    uint32_t bootrom_addr;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t enabled = s->regs.CSR7;
    uint32_t status = s->regs.CSR5;
    bool irq_pending = (status & enabled) != 0;

    pci_set_irq(pdev, irq_pending ? 1 : 0);
}

/* Get pointer to 32-bit CSR register based on I/O address */
static uint32_t *get_csr_reg(PCIBaseState *s, hwaddr addr)
{
    switch (addr & 0xFF) {
        case REG_CSR0:  return &s->regs.CSR0;
        case REG_CSR1:  return &s->regs.CSR1;
        case REG_CSR2:  return &s->regs.CSR2;
        case REG_CSR3:  return &s->regs.CSR3;
        case REG_CSR4:  return &s->regs.CSR4;
        case REG_CSR5:  return &s->regs.CSR5;
        case REG_CSR6:  return &s->regs.CSR6;
        case REG_CSR7:  return &s->regs.CSR7;
        case REG_CSR8:  return &s->regs.CSR8;
        case REG_CSR9:  return &s->regs.CSR9;
        case REG_CSR10: return &s->regs.CSR10;
        case REG_CSR11: return &s->regs.CSR11;
        case REG_CSR12: return &s->regs.CSR12;
        case REG_CSR13: return &s->regs.CSR13;
        case REG_CSR14: return &s->regs.CSR14;
        case REG_CSR15: return &s->regs.CSR15;
        case REG_CSR16: return &s->regs.CSR16;
        default: return NULL;
    }
}

/* PIO Handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Special register CSR9: boot ROM data */
    if (addr == REG_CSR9) {
        uint8_t byte = xircom_bootrom[s->bootrom_addr & 0xFF];
        val = (uint32_t)byte;
    } else if (addr == REG_CSR10) {
        val = s->bootrom_addr;
    } else {
        uint32_t *reg = get_csr_reg(s, addr);
        if (reg) {
            if (size == 4) {
                val = *reg;
            } else if (size == 1) {
                val = *reg & 0xFF;
            } else if (size == 2) {
                val = *reg & 0xFFFF;
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "xircom: unhandled PIO read size %d at 0x%" HWADDR_PRIx "\n", size, addr);
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "xircom: PIO read from unknown register 0x%" HWADDR_PRIx "\n", addr);
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle special registers */
    if (addr == REG_CSR5) {
        /* Write-1-to-clear status register */
        s->regs.CSR5 &= ~val;
        pcibase_update_irq(s);
        return;
    } else if (addr == REG_CSR6) {
        s->regs.CSR6 = val;
        /* Update receiver/transmitter active status in CSR5 */
        if (val & 2) {
            /* Receiver enabled -> set bits 17-19 */
            s->regs.CSR5 |= (7 << 17);
        } else {
            s->regs.CSR5 &= ~(7 << 17);
        }
        if (val & (1 << 13)) {
            /* Transmitter enabled -> set bits 20-22 */
            s->regs.CSR5 |= (7 << 20);
        } else {
            s->regs.CSR5 &= ~(7 << 20);
        }
        pcibase_update_irq(s);
        return;
    } else if (addr == REG_CSR7) {
        s->regs.CSR7 = val;
        pcibase_update_irq(s);
        return;
    } else if (addr == REG_CSR9) {
        /* Driver writes (1<<12) to enable boot ROM access, ignore */
        s->regs.CSR9 = val;
        return;
    } else if (addr == REG_CSR10) {
        s->bootrom_addr = val;
        s->regs.CSR10 = val;
        return;
    }

    /* Generic register write */
    uint32_t *reg = get_csr_reg(s, addr);
    if (reg) {
        if (size == 4) {
            *reg = val;
        } else if (size == 1) {
            *reg = (*reg & 0xFFFFFF00) | (val & 0xFF);
        } else if (size == 2) {
            *reg = (*reg & 0xFFFF0000) | (val & 0xFFFF);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "xircom: unhandled PIO write size %d to 0x%" HWADDR_PRIx "\n", size, addr);
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "xircom: PIO write to unknown register 0x%" HWADDR_PRIx "\n", addr);
    }
}

/* MMIO handlers (unused, device uses PIO only) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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

    /* Reset all CSRs to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->bootrom_addr = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x115d );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0003 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    /* Place PM capability at offset 0x40 to match driver expectation */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0x40, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "xircom-pio";
    s->num_bars = 1;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not used by driver */
    s->has_msi = false;
    s->has_msix = false;

    /* DMA initialization */
    s->dma.rx_dma = 0;
    s->dma.tx_dma = 0;
    s->dma.tx_used = 0;
    s->dma.rx_used = 0;
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
    .name = "xircom_cb_pci",
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
