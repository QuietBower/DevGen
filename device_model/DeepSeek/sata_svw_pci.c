/*
 * QEMU model for ServerWorks K2 SATA controller (sata_svw).
 * Based on Linux driver sata_svw.c.
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

/* Missing driver-specific defines; request source */
#define K2_SATA_TF_CMD_OFFSET      0x100
#define K2_SATA_TF_DATA_OFFSET     0x800
#define K2_SATA_TF_ERROR_OFFSET    0x808
#define K2_SATA_TF_NSECT_OFFSET    0x810
#define K2_SATA_TF_LBAL_OFFSET     0x818
#define K2_SATA_TF_LBAM_OFFSET     0x820
#define K2_SATA_TF_LBAH_OFFSET     0x828
#define K2_SATA_TF_DEVICE_OFFSET   0x830
#define K2_SATA_TF_CMDSTAT_OFFSET  0x838
#define K2_SATA_TF_CTL_OFFSET      0x840
#define K2_SATA_DMA_CMD_OFFSET     0x200
#define K2_SATA_SCR_STATUS_OFFSET  0x1000
#define K2_SATA_PORT_OFFSET        0x2000
#define K2_SATA_SICR1_OFFSET       0x100
#define K2_SATA_SCR_ERROR_OFFSET   0x400
#define K2_SATA_SIM_OFFSET         0x404

#define TYPE_PCIBASE_DEVICE "sata_svw_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* BAR information and register offsets */
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

#define MAX_PORTS 4  /* default; may be overridden */

typedef struct {
    /* Taskfile shadow registers (16-bit, as accessed by driver) */
    uint16_t data;
    uint16_t error_feature;
    uint16_t nsect;
    uint16_t lbal;
    uint16_t lbam;
    uint16_t lbah;
    uint16_t device;
    uint16_t cmd_status;
    uint16_t ctl_altstatus;

    /* BMDMA registers */
    uint8_t  bmdma_cmd;      /* Command register (byte access) */
    uint8_t  bmdma_status;   /* Status register (byte access) */
    uint32_t bmdma_prd_addr; /* PRD table address (32-bit) */

    /* SCR registers */
    uint32_t sstatus;
    uint32_t serror;
    uint32_t scontrol;
} PortState;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Per-port state */
    PortState port[MAX_PORTS];
    uint8_t num_ports;

    /* Global registers */
    uint32_t sicr1;
    uint32_t sc_serror;
    uint32_t sim;
};

/* Update IRQ based on port interrupt status */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;
    bool pending = false;

    for (i = 0; i < s->num_ports; i++) {
        if (s->port[i].bmdma_status & 0x04) {  /* Interrupt bit (bit 2) */
            pending = true;
            break;
        }
    }

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int port;
    hwaddr port_offset;
    PortState *p;

    /* Handle global registers (offsets less than K2_SATA_PORT_OFFSET) */
    if (addr < K2_SATA_PORT_OFFSET) {
        switch (addr) {
        case K2_SATA_SICR1_OFFSET:
            val = s->sicr1;
            break;
        case K2_SATA_SCR_ERROR_OFFSET:
            val = s->sc_serror;
            break;
        case K2_SATA_SIM_OFFSET:
            val = s->sim;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "sata_svw: unknown global read at 0x%" HWADDR_PRIx "\n", addr);
            break;
        }
        return val;
    }

    port = addr / K2_SATA_PORT_OFFSET;
    if (port < 0 || port >= s->num_ports) {
        qemu_log_mask(LOG_GUEST_ERROR, "sata_svw: out-of-range port %d (addr 0x%" HWADDR_PRIx ")\n", port, addr);
        return 0;
    }
    port_offset = addr % K2_SATA_PORT_OFFSET;
    p = &s->port[port];

    /* Dispatch based on port-relative offset */
    if (port_offset >= K2_SATA_TF_CMD_OFFSET && port_offset < K2_SATA_TF_CMD_OFFSET + 0x100) {
        /* Taskfile region */
        switch (port_offset) {
        case K2_SATA_TF_CMD_OFFSET:
            val = p->cmd_status;
            break;
        case K2_SATA_TF_DATA_OFFSET:
            val = p->data;
            break;
        case K2_SATA_TF_ERROR_OFFSET:
            val = p->error_feature;
            break;
        case K2_SATA_TF_NSECT_OFFSET:
            val = p->nsect;
            break;
        case K2_SATA_TF_LBAL_OFFSET:
            val = p->lbal;
            break;
        case K2_SATA_TF_LBAM_OFFSET:
            val = p->lbam;
            break;
        case K2_SATA_TF_LBAH_OFFSET:
            val = p->lbah;
            break;
        case K2_SATA_TF_DEVICE_OFFSET:
            val = p->device;
            break;
        case K2_SATA_TF_CMDSTAT_OFFSET:
            val = p->cmd_status;
            break;
        case K2_SATA_TF_CTL_OFFSET:
            val = p->ctl_altstatus;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "sata_svw: unknown taskfile read at 0x%" HWADDR_PRIx "\n", addr);
            break;
        }
    } else if (port_offset >= K2_SATA_DMA_CMD_OFFSET && port_offset < K2_SATA_DMA_CMD_OFFSET + 8) {
        /* BMDMA region */
        hwaddr dma_off = port_offset - K2_SATA_DMA_CMD_OFFSET;
        switch (dma_off) {
        case 0:
            val = p->bmdma_cmd;
            break;
        case 2:
            val = p->bmdma_status;
            break;
        case 4:
            val = p->bmdma_prd_addr;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "sata_svw: unknown bmdma read at 0x%" HWADDR_PRIx "\n", addr);
            break;
        }
    } else if (port_offset >= K2_SATA_SCR_STATUS_OFFSET && port_offset < K2_SATA_SCR_STATUS_OFFSET + 12) {
        /* SCR region */
        hwaddr scr_off = port_offset - K2_SATA_SCR_STATUS_OFFSET;
        switch (scr_off) {
        case 0:
            val = p->sstatus;
            break;
        case 4:
            val = p->serror;
            break;
        case 8:
            val = p->scontrol;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "sata_svw: unknown scr read at 0x%" HWADDR_PRIx "\n", addr);
            break;
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "sata_svw: read from unimplemented offset 0x%" HWADDR_PRIx "\n", addr);
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port;
    hwaddr port_offset;
    PortState *p;

    /* Handle global registers */
    if (addr < K2_SATA_PORT_OFFSET) {
        switch (addr) {
        case K2_SATA_SICR1_OFFSET:
            s->sicr1 = val;
            break;
        case K2_SATA_SCR_ERROR_OFFSET:
            s->sc_serror = val;
            break;
        case K2_SATA_SIM_OFFSET:
            s->sim = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "sata_svw: unknown global write at 0x%" HWADDR_PRIx "\n", addr);
            break;
        }
        return;
    }

    port = addr / K2_SATA_PORT_OFFSET;
    if (port < 0 || port >= s->num_ports) {
        qemu_log_mask(LOG_GUEST_ERROR, "sata_svw: out-of-range port %d (addr 0x%" HWADDR_PRIx ")\n", port, addr);
        return;
    }
    port_offset = addr % K2_SATA_PORT_OFFSET;
    p = &s->port[port];

    /* Dispatch based on port-relative offset */
    if (port_offset >= K2_SATA_TF_CMD_OFFSET && port_offset < K2_SATA_TF_CMD_OFFSET + 0x100) {
        /* Taskfile region */
        switch (port_offset) {
        case K2_SATA_TF_CMD_OFFSET:
            p->cmd_status = val;
            break;
        case K2_SATA_TF_DATA_OFFSET:
            p->data = val;
            break;
        case K2_SATA_TF_ERROR_OFFSET:
            p->error_feature = val;
            break;
        case K2_SATA_TF_NSECT_OFFSET:
            p->nsect = val;
            break;
        case K2_SATA_TF_LBAL_OFFSET:
            p->lbal = val;
            break;
        case K2_SATA_TF_LBAM_OFFSET:
            p->lbam = val;
            break;
        case K2_SATA_TF_LBAH_OFFSET:
            p->lbah = val;
            break;
        case K2_SATA_TF_DEVICE_OFFSET:
            p->device = val;
            break;
        case K2_SATA_TF_CMDSTAT_OFFSET:
            p->cmd_status = val;
            break;
        case K2_SATA_TF_CTL_OFFSET:
            p->ctl_altstatus = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "sata_svw: unknown taskfile write at 0x%" HWADDR_PRIx "\n", addr);
            break;
        }
    } else if (port_offset >= K2_SATA_DMA_CMD_OFFSET && port_offset < K2_SATA_DMA_CMD_OFFSET + 8) {
        /* BMDMA region */
        hwaddr dma_off = port_offset - K2_SATA_DMA_CMD_OFFSET;
        switch (dma_off) {
        case 0:
            p->bmdma_cmd = val & 0xFF;
            break;
        case 2:
            /* Status register: write-1-to-clear */
            p->bmdma_status &= ~(val & 0xFF);
            pcibase_update_irq(s);
            break;
        case 4:
            p->bmdma_prd_addr = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "sata_svw: unknown bmdma write at 0x%" HWADDR_PRIx "\n", addr);
            break;
        }
    } else if (port_offset >= K2_SATA_SCR_STATUS_OFFSET && port_offset < K2_SATA_SCR_STATUS_OFFSET + 12) {
        /* SCR region */
        hwaddr scr_off = port_offset - K2_SATA_SCR_STATUS_OFFSET;
        switch (scr_off) {
        case 0:
            p->sstatus = val;
            break;
        case 4:
            p->serror = val;
            break;
        case 8:
            p->scontrol = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "sata_svw: unknown scr write at 0x%" HWADDR_PRIx "\n", addr);
            break;
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "sata_svw: write to unimplemented offset 0x%" HWADDR_PRIx " (val=0x%" PRIx64 ")\n", addr, val);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = NULL,
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    int i;
    for (i = 0; i < s->num_ports; i++) {
        PortState *p = &s->port[i];
        p->data = 0;
        p->error_feature = 0;
        p->nsect = 0;
        p->lbal = 0;
        p->lbam = 0;
        p->lbah = 0;
        p->device = 0;
        p->cmd_status = 0x50; /* DRDY | DSC */
        p->ctl_altstatus = 0;
        p->bmdma_cmd = 0;
        p->bmdma_status = 0;
        p->bmdma_prd_addr = 0;
        p->sstatus = 0x1; /* active, device present */
        p->serror = 0;
        p->scontrol = 0;
    }
    s->sicr1 = 0;
    s->sc_serror = 0;
    s->sim = 0;

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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1166);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0240);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0106);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR initialization: single MMIO BAR at index 5, size 0x10000 */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 5,
        .type = BAR_TYPE_MMIO,
        .size = 0x10000,
        .name = "sata_svw-mmio"
    };
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* Initialize port count (hardcoded to 4; may need override from k2_port_info) */
    s->num_ports = 4;
    pcibase_reset(DEVICE(s));
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
    .name = "sata_svw_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static Property pcibase_properties[] = {
    DEFINE_PROP_UINT8("num_ports", PCIBaseState, num_ports, 4),
    DEFINE_PROP_END_OF_LIST(),
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    dc->props_ = pcibase_properties;
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
