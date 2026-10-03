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
#include "qemu/bswap.h"
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

#define TYPE_PCIBASE_DEVICE "ipr_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x106B
#define DEVICE_ID 0x0339
#define CLASS_ID  0x0104

/* PCI IDs */
#define PCI_VENDOR_ID_MYLEX 0x106B
#define PCI_DEVICE_ID_IBM_GEMSTONE 0x0339

/* Register Offsets for Gemstone chip (first entry in ipr_chip_cfg) */
#define IPR_SENSE_INT_OFFSET       0x00224
#define IPR_SENSE_INT_MASK_OFFSET  0x0022C
#define IPR_CLR_INT_OFFSET         0x00228
#define IPR_CLR_INT_MASK_OFFSET    0x00230
#define IPR_SET_INT_MASK_OFFSET    0x0022C
#define IPR_IOARRIN_OFFSET         0x00404
#define IPR_MAILBOX_OFFSET         0x0042C
#define IPR_SENSE_UPROC_INT_OFFSET 0x00214
#define IPR_SET_UPROC_INT_OFFSET   0x00214
#define IPR_CLR_UPROC_INT_OFFSET   0x00218

/* Interrupt Status Bit Definitions */
#define IPR_PCII_IOA_TRANS_TO_OPER   (0x80000000 >> 0)   /* 0x80000000 */
#define IPR_PCII_IOARCB_XFER_FAILED  (0x80000000 >> 3)   /* 0x10000000 */
#define IPR_PCII_IOA_UNIT_CHECKED    (0x80000000 >> 4)   /* 0x08000000 */
#define IPR_PCII_NO_HOST_RRQ         (0x80000000 >> 5)   /* 0x04000000 */
#define IPR_PCII_IOARRIN_LOST        (0x80000000 >> 27)  /* 0x00000010 */
#define IPR_PCII_MMIO_ERROR          (0x80000000 >> 28)  /* 0x00000008 */
#define IPR_PCII_HRRQ_UPDATED        (0x80000000 >> 30)  /* 0x00000002 */
#define IPR_PCII_MAILBOX_STABLE      (0x80000000 >> 4)   /* 0x08000000 */

/* Doorbell value for mailbox */
#define IPR_DOORBELL                  0x82800000

/* BAR size covers all known registers */
#define IPR_MMIO_SIZE                 0x1000

/* Command codes for IOA */
#define IPR_ID_HOST_RR_Q             0x81

/* IOARCB field offsets (assuming 32-bit layout) */
#define IOARCB_OFFSET_HOST_RESP_HANDLE 4
#define IOARCB_OFFSET_IOASA_ADDR       0x10
#define IOARCB_OFFSET_CMD_PKT_CDB      0x18

/* Internal register state structure */
typedef struct IPRRegs {
    uint32_t sense_interrupt;         /* 0x00224 */
    uint32_t interrupt_mask;          /* mask, accessible at 0x0022C */
    uint32_t clr_interrupt;           /* 0x00228 (write-only) */
    uint32_t clr_interrupt_mask;      /* 0x00230 (write-only) */
    uint32_t ioarrin;                /* 0x00404 */
    uint32_t mailbox;                /* 0x0042C */
    uint32_t sense_uproc;            /* 0x00214 */
    uint32_t clr_uproc;              /* 0x00218 (write-only) */
} IPRRegs;

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
    IPRRegs regs;

    /* DMA Context for host RRQ */
    dma_addr_t host_rrq_addr;
    uint32_t host_rrq_size;   /* number of entries */
    uint32_t host_rrq_index;
    uint32_t host_rrq_toggle;
    bool rrq_configured;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->regs.sense_interrupt & ~s->regs.interrupt_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Process a command submitted via IOARRIN register */
static void pcibase_process_ioarcb(PCIBaseState *s, dma_addr_t ioarcb_dma)
{
    uint8_t ioarcb[64];
    uint32_t resp_handle, ioasa_addr, cdb0, rrq_addr, rrq_size;
    PCIDevice *pdev = PCI_DEVICE(s);

    if (pci_dma_read(pdev, ioarcb_dma, ioarcb, sizeof(ioarcb)) != MEMTX_OK) {
        return;
    }

    resp_handle = ldl_be_p(ioarcb + IOARCB_OFFSET_HOST_RESP_HANDLE);
    ioasa_addr = ldl_be_p(ioarcb + IOARCB_OFFSET_IOASA_ADDR);
    cdb0 = ioarcb[IOARCB_OFFSET_CMD_PKT_CDB];

    /* Handle ID_HOST_RR_Q command: capture host RRQ address and size */
    if (cdb0 == IPR_ID_HOST_RR_Q) {
        rrq_addr = ldl_be_p(ioarcb + IOARCB_OFFSET_CMD_PKT_CDB + 2);
        rrq_size = lduw_be_p(ioarcb + IOARCB_OFFSET_CMD_PKT_CDB + 7); /* bytes 7-8 */
        s->host_rrq_addr = rrq_addr;
        s->host_rrq_size = rrq_size;
        s->host_rrq_index = 0;
        s->host_rrq_toggle = 1;  /* driver initializes toggle to 1 */
        s->rrq_configured = true;
    }

    /* Write IOASC = 0 (success) to the IOASA */
    if (ioasa_addr) {
        uint32_t ioasc_zero = 0;
        pci_dma_write(pdev, ioasa_addr, &ioasc_zero, sizeof(ioasc_zero));
    }

    /* Update host RRQ if configured */
    if (s->rrq_configured && s->host_rrq_size > 0) {
        uint32_t entry = resp_handle | (s->host_rrq_toggle ? 0x80000000 : 0);
        dma_addr_t entry_addr = s->host_rrq_addr + (s->host_rrq_index * sizeof(uint32_t));

        if (pci_dma_write(pdev, entry_addr, &entry, sizeof(entry)) == MEMTX_OK) {
            s->host_rrq_index++;
            if (s->host_rrq_index >= (s->host_rrq_size / sizeof(uint32_t))) {
                s->host_rrq_index = 0;
                s->host_rrq_toggle ^= 1;
            }
        }

        /* Raise HRRQ Updated interrupt */
        s->regs.sense_interrupt |= IPR_PCII_HRRQ_UPDATED;
        pcibase_update_irq(s);
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case IPR_SENSE_INT_OFFSET:
        val = s->regs.sense_interrupt;
        break;
    case IPR_SENSE_INT_MASK_OFFSET:
        val = s->regs.interrupt_mask;
        break;
    case IPR_SENSE_UPROC_INT_OFFSET:
        val = s->regs.sense_uproc;
        break;
    case IPR_MAILBOX_OFFSET:
        val = s->regs.mailbox;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case IPR_SET_UPROC_INT_OFFSET:
        s->regs.sense_uproc |= (uint32_t)val;
        break;
    case IPR_CLR_UPROC_INT_OFFSET:
        s->regs.sense_uproc &= ~(uint32_t)val;
        break;
    case IPR_CLR_INT_OFFSET:
        s->regs.sense_interrupt &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case IPR_SET_INT_MASK_OFFSET:
        s->regs.interrupt_mask |= (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case IPR_CLR_INT_MASK_OFFSET:
        s->regs.interrupt_mask &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case IPR_IOARRIN_OFFSET:
        pcibase_process_ioarcb(s, (dma_addr_t)val);
        break;
    case IPR_MAILBOX_OFFSET:
        s->regs.mailbox = (uint32_t)val;
        break;
    default:
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

    memset(&s->regs, 0, sizeof(s->regs));
    s->rrq_configured = false;
    s->host_rrq_addr = 0;
    s->host_rrq_size = 0;
    s->host_rrq_index = 0;
    s->host_rrq_toggle = 1;
    pcibase_update_irq(s);
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

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = IPR_MMIO_SIZE;
    s->bar_info[0].name = "ipr-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    memset(&s->regs, 0, sizeof(s->regs));
    s->rrq_configured = false;
    s->host_rrq_addr = 0;
    s->host_rrq_size = 0;
    s->host_rrq_index = 0;
    s->host_rrq_toggle = 1;
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
    .name = "ipr_pci",
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
