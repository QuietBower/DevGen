/*
 * QEMU QLogic ISP4010 iSCSI HBA emulation (PCI)
 * Based on Linux driver qla4xxx, ISP4010 variant.
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

#define TYPE_PCIBASE_DEVICE "qla4xxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID       0x1077  /* PCI_VENDOR_ID_QLOGIC */
#define DEVICE_ID       0x4010  /* PCI_DEVICE_ID_QLOGIC_ISP4010 */
#define CLASS_ID        PCI_CLASS_STORAGE_SCSI

/* ISP4010 Register Offsets */
typedef struct ISP4010Regs {
    uint32_t mailbox[8];        /* 0x00 - 0x1F */
    uint32_t flash_address;     /* 0x20 */
    uint32_t flash_data;        /* 0x24 */
    uint32_t ctrl_status;       /* 0x28 */
    uint32_t nvram;             /* 0x2C (isp4010 specific) */
    uint32_t reserved1[2];      /* 0x30, 0x34 */
    uint32_t req_q_in;          /* 0x38 */
    uint32_t rsp_q_out;         /* 0x3C */
    uint32_t reserved2[4];      /* 0x40 - 0x4F */
    uint32_t ext_hw_conf;       /* 0x50 */
    uint32_t flow_ctrl;         /* 0x54 */
    uint32_t port_ctrl;         /* 0x58 */
    uint32_t port_status;       /* 0x5C */
    uint32_t reserved3[8];      /* 0x60 - 0x7F */
    uint32_t req_q_out;         /* 0x80 */
    uint32_t reserved4[23];     /* 0x84 - 0xDF */
    uint32_t gp_out;            /* 0xE0 */
    uint32_t gp_in;             /* 0xE4 */
    uint32_t reserved5[5];      /* 0xE8 - 0xFB */
    uint32_t port_err_status;   /* 0xFC */
} ISP4010Regs;

/* Control/Status Register bit definitions (inferred from driver usage) */
#define CSR_SOFT_RESET          0x0001
#define CSR_FORCE_SOFT_RESET    0x0002
#define CSR_SCSI_RESET_INTR     0x0004
#define CSR_NET_RESET_INTR      0x0008
#define CSR_SCSI_PROCESSOR_INTR 0x0010

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
    ISP4010Regs regs;

    /* DMA Context */
    dma_addr_t request_dma;
    dma_addr_t response_dma;
    dma_addr_t shadow_regs_dma;
    uint32_t request_in;
    uint32_t request_out;
    uint32_t response_in;
    uint32_t response_out;

    uint32_t status;      /* Operational status flags */
    bool reset_active;    /* State used to handle reset sequences */

    QEMUTimer *reset_timer; /* Timer to simulate reset completion */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_mask;
    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Timer callback to clear soft reset bits */
static void pcibase_reset_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->regs.ctrl_status &= ~(CSR_SOFT_RESET | CSR_FORCE_SOFT_RESET);
    /* Optionally clear network reset interrupt if it was set */
    s->regs.ctrl_status &= ~CSR_NET_RESET_INTR;
    s->reset_active = false;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only allow 2 and 4 byte accesses for known registers */
    if (size != 2 && size != 4) {
        return val;
    }

    switch (addr) {
    case 0x00 ... 0x1F:
        val = s->regs.mailbox[(addr - 0x00) >> 2];
        break;
    case 0x20:
        val = s->regs.flash_address;
        break;
    case 0x24:
        val = s->regs.flash_data;
        break;
    case 0x28:
        val = s->regs.ctrl_status;
        break;
    case 0x2C:
        val = s->regs.nvram;
        break;
    case 0x30: case 0x34: /* reserved1 */
        val = 0;
        break;
    case 0x38:
        val = s->regs.req_q_in;
        break;
    case 0x3C:
        val = s->regs.rsp_q_out;
        break;
    case 0x40 ... 0x4F:
        val = 0;
        break;
    case 0x50:
        val = s->regs.ext_hw_conf;
        break;
    case 0x54:
        val = s->regs.flow_ctrl;
        break;
    case 0x58:
        val = s->regs.port_ctrl;
        break;
    case 0x5C:
        val = s->regs.port_status;
        break;
    case 0x60 ... 0x7F:
        val = 0;
        break;
    case 0x80:
        val = s->regs.req_q_out;
        break;
    case 0x84 ... 0xDF:
        val = 0;
        break;
    case 0xE0:
        val = s->regs.gp_out;
        break;
    case 0xE4:
        val = s->regs.gp_in;
        break;
    case 0xE8 ... 0xFB:
        val = 0;
        break;
    case 0xFC:
        val = s->regs.port_err_status;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from offset 0x%x\n", __func__, (int)addr);
    }

    /* Adjust value for size (driver may use readw) */
    if (size == 2) {
        val &= 0xFFFF;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 2 && size != 4) {
        return;
    }

    switch (addr) {
    case 0x00 ... 0x1F:
        s->regs.mailbox[(addr - 0x00) >> 2] = (uint32_t)val;
        break;
    case 0x20:
        s->regs.flash_address = (uint32_t)val;
        break;
    case 0x24:
        s->regs.flash_data = (uint32_t)val;
        break;
    case 0x28:
    {
        uint32_t old = s->regs.ctrl_status;
        /* Merge write based on size */
        if (size == 2) {
            s->regs.ctrl_status = (old & 0xFFFF0000) | ((uint32_t)val & 0xFFFF);
        } else {
            s->regs.ctrl_status = (uint32_t)val;
        }
        /* Handle reset bits */
        if (s->regs.ctrl_status & (CSR_SOFT_RESET | CSR_FORCE_SOFT_RESET)) {
            if (!s->reset_active) {
                s->reset_active = true;
                /* Start timer to clear reset bits after a delay */
                timer_mod(s->reset_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
            }
        }
        /* Interrupt enable/disable inferred: when CSR_SCSI_PROCESSOR_INTR is cleared, enable interrupts */
        s->intr_mask = (s->regs.ctrl_status & CSR_SCSI_PROCESSOR_INTR) ? 0 : 1;
        pcibase_update_irq(s);
        break;
    }
    case 0x2C:
        s->regs.nvram = (uint32_t)val;
        break;
    case 0x30: case 0x34:
        break;
    case 0x38:
        s->regs.req_q_in = (uint32_t)val;
        break;
    case 0x3C:
        s->regs.rsp_q_out = (uint32_t)val;
        break;
    case 0x40 ... 0x4F:
        break;
    case 0x50:
        s->regs.ext_hw_conf = (uint32_t)val;
        break;
    case 0x54:
        s->regs.flow_ctrl = (uint32_t)val;
        break;
    case 0x58:
        s->regs.port_ctrl = (uint32_t)val;
        break;
    case 0x5C:
        s->regs.port_status = (uint32_t)val;
        break;
    case 0x60 ... 0x7F:
        break;
    case 0x80:
        s->regs.req_q_out = (uint32_t)val;
        break;
    case 0x84 ... 0xDF:
        break;
    case 0xE0:
        s->regs.gp_out = (uint32_t)val;
        break;
    case 0xE4:
        s->regs.gp_in = (uint32_t)val;
        break;
    case 0xE8 ... 0xFB:
        break;
    case 0xFC:
        s->regs.port_err_status = (uint32_t)val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to offset 0x%x\n", __func__, (int)addr);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 2, .max_access_size = 4 },
    .impl  = { .min_access_size = 2, .max_access_size = 4 },
};

/* PIO handlers unused by driver, but region must exist */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    return;
}

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

    memset(&s->regs, 0, sizeof(s->regs));
    s->status = 0;
    s->reset_active = false;
    s->intr_status = 0;
    s->intr_mask = 1; /* enabled by default? The driver enables later */
    s->regs.ctrl_status = 0; /* Ensure reset bits are clear */
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

    /* Static PCI configuration */
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

    /* BAR0: PIO */
    MemoryRegion *mr0 = &s->bar_regions[0];
    memory_region_init_io(mr0, OBJECT(s), &pcibase_pio_ops, s, "qla4xxx-pio", 0x100);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, mr0);

    /* BAR1: MMIO for MSI-X, use RAM region so msix_init can map into it */
    MemoryRegion *mr1 = &s->bar_regions[1];
    memory_region_init_ram(mr1, OBJECT(s), "qla4xxx-msix", 0x1000, errp);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, mr1);

    /* MSI-X initialization */
    if (msix_init(pdev, 2, mr1, 1, 0, mr1, 1, 0x200, 0, errp)) {
        return;
    }

    /* Initialize soft reset timer */
    s->reset_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_reset_timer_cb, s);

    /* Set initial register values */
    s->regs.ctrl_status = 0;  /* No reset bits set */
    s->intr_mask = 1;         /* Interrupts enabled (bit cleared) */
    s->intr_status = 0;
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
    timer_del(s->reset_timer);
    timer_free(s->reset_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "qla4xxx_pci",
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
