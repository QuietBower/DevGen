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

#define TYPE_PCIBASE_DEVICE "mptctl_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related_Config_Info: PCI IDs and register offsets (guesses until base driver source available) */
#define VENDOR_ID 0x1000
#define DEVICE_ID 0x0030
#define CLASS_ID PCI_CLASS_STORAGE_SCSI

/* Register offsets derived from driver macros - x=0 assumed */
#define DOORBELL_OFFSET      0x00
#define REQUEST_FIFO_OFFSET  0x04  /* Unknown; need MPT_CHIP_REGS */
#define INTR_STATUS_OFFSET   0x4D0 /* INTR_STATUS_OFFSET(0) */
#define IOC_STATE_OFFSET     0x0C

/* IOC State Values from driver */
#define MPI_IOC_STATE_OPERATIONAL 0x20000000
#define MPI_IOC_STATE_READY       0x10000000
#define MPI_IOC_STATE_RESET       0x00000000
#define MPI_IOC_STATE_FAULT       0x40000000

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

/* Reg_Stru: Register shadows */
typedef struct MPTCtlRegs {
    uint32_t doorbell;       /* Doorbell/state register */
    uint32_t request_fifo;   /* Write to post a request frame address */
    uint32_t intr_status;    /* Interrupt status, W1C */
    uint32_t ioc_state;      /* IOC state: 0=operational, 1=ready, 2=fault, 3=reset */
} MPTCtlRegs;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows */
    MPTCtlRegs regs;
};

/* Other_Addition_Info_Defin: none yet */

static void pcibase_update_irq(PCIBaseState *s)
{
    if (s->regs.intr_status) {
        pci_set_irq(&s->parent_obj, 1);
    } else {
        pci_set_irq(&s->parent_obj, 0);
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DOORBELL_OFFSET:
        val = s->regs.ioc_state; /* Doorbell = IOC state bits */
        break;
    case REQUEST_FIFO_OFFSET:
        val = s->regs.request_fifo;
        break;
    case INTR_STATUS_OFFSET:
        val = s->regs.intr_status;
        break;
    case IOC_STATE_OFFSET:
        val = s->regs.ioc_state;
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
    case DOORBELL_OFFSET:
        s->regs.doorbell = val; /* Store raw but state overridden below */
        /* Any write transitions to OPERATIONAL */
        s->regs.ioc_state = MPI_IOC_STATE_OPERATIONAL;
        break;
    case REQUEST_FIFO_OFFSET:
        s->regs.request_fifo = val;
        s->regs.intr_status |= 1; /* Set generic interrupt */
        pcibase_update_irq(s);
        break;
    case INTR_STATUS_OFFSET:
        /* Write-1-to-clear */
        s->regs.intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case IOC_STATE_OFFSET:
        s->regs.ioc_state = val;
        break;
    }
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
        memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size); /* PIO not used */
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    
    s->regs.doorbell = 0;
    s->regs.request_fifo = 0;
    s->regs.intr_status = 0;
    s->regs.ioc_state = MPI_IOC_STATE_RESET;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* Increased to cover full register space up to 0x4D0+ */
    s->bar_info[0].name = "mptctl-mmio";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
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
    .name = "mptctl_driver_pci",
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
