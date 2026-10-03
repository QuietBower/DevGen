/*
 * QEMU PCI device model for EHCI PCI host controller.
 * Based on Linux driver: /home/eely/linux-7.1/drivers/usb/host/ehci-pci.c
 * Generated functional implementation.
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

#define TYPE_PCIBASE_DEVICE "ehci_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/*======================================================================
 * EHCI Register Offset Definitions (from driver headers)
 *======================================================================*/

/* Capability Registers (offset from BAR base) */
#define EHCI_CAPLENGTH      0x00   /* HC Capability Register Length */
#define EHCI_HCSPARAMS      0x04   /* Structural Parameters */
#define EHCI_HCCPARAMS      0x08   /* Capability Parameters */
#define EHCI_PORTROUTE      0x0C   /* Companion Port Route Description */

/* Operational Registers (offset from operational base) */
#define EHCI_USBCMD         0x00   /* USB Command */
#define EHCI_USBSTS         0x04   /* USB Status */
#define EHCI_USBINTR        0x08   /* USB Interrupt Enable */
#define EHCI_FRINDEX        0x0C   /* Frame Index */
#define EHCI_CTRLDSSEGMENT  0x10   /* Control Data Structure Segment */
#define EHCI_PERIODICLISTBASE 0x14 /* Periodic Frame List Base Address */
#define EHCI_ASYNCLISTADDR  0x18   /* Current Asynchronous List Address */
#define EHCI_CONFIGFLAG     0x40   /* Configured Flag */
#define EHCI_PORTSC         0x44   /* Port Status/Control (first port) */

/* Default operational base (capability length) often 0x10 */
#define EHCI_OP_BASE        0x10

/*----------------------------------------------------------------------
 * PCI Identification - PLACEHOLDER: Actual IDs from first pci_device_id entry
 *----------------------------------------------------------------------*/
#define VENDOR_ID 0xffff
#define DEVICE_ID 0xffff

/*======================================================================
 * BAR Information
 *======================================================================*/
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

/*======================================================================
 * Device state structure
 *======================================================================*/
struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    
    /* Register state */
    uint32_t capbase;
    uint32_t hcs_params;
    uint32_t hcc_params;
    uint8_t  portroute[8];
    uint32_t usbcmd;
    uint32_t usbsts;
    uint32_t usbintr;
    uint32_t frindex;
    uint32_t ctrldssegment;
    uint32_t periodiclistbase;
    uint32_t asynclistaddr;
    uint32_t configflag;
    uint32_t port_status[15];
};

/*======================================================================
 * Interrupt update helper
 *======================================================================*/
static void ehci_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->usbsts & s->usbintr) {
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

/*======================================================================
 * Memory region operations
 *======================================================================*/
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint64_t val = 0;

    /* Capability registers (0x00 - 0x0F) */
    switch (addr) {
    case EHCI_CAPLENGTH:
        /* CAPLENGTH (0x10) + HCIVERSION (0x0100) */
        val = 0x01000010;
        break;
    case EHCI_HCSPARAMS:
        val = s->hcs_params;
        break;
    case EHCI_HCCPARAMS:
        val = s->hcc_params;
        break;
    case EHCI_PORTROUTE:
        val = 0; /* Low 32 bits of PORTROUTE, simplified */
        break;

    /* Operational registers (starting at EHCI_OP_BASE = 0x10) */
    case EHCI_OP_BASE + EHCI_USBCMD:
        val = s->usbcmd;
        break;
    case EHCI_OP_BASE + EHCI_USBSTS:
        val = s->usbsts;
        break;
    case EHCI_OP_BASE + EHCI_USBINTR:
        val = s->usbintr;
        break;
    case EHCI_OP_BASE + EHCI_FRINDEX:
        val = s->frindex;
        break;
    case EHCI_OP_BASE + EHCI_CTRLDSSEGMENT:
        val = s->ctrldssegment;
        break;
    case EHCI_OP_BASE + EHCI_PERIODICLISTBASE:
        val = s->periodiclistbase;
        break;
    case EHCI_OP_BASE + EHCI_ASYNCLISTADDR:
        val = s->asynclistaddr;
        break;
    case EHCI_OP_BASE + EHCI_CONFIGFLAG:
        val = s->configflag;
        break;
    default:
        if (addr >= EHCI_OP_BASE + EHCI_PORTSC &&
            addr < EHCI_OP_BASE + EHCI_PORTSC + 15 * 4) {
            int port = (addr - (EHCI_OP_BASE + EHCI_PORTSC)) / 4;
            val = s->port_status[port];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad read at 0x%" HWADDR_PRIx " size %u\n",
                          __func__, addr, size);
            val = 0;
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);

    switch (addr) {
    /* Operational registers only; capability registers are read-only */
    case EHCI_OP_BASE + EHCI_USBCMD:
        if (val & (1 << 1)) { /* HCRESET */
            /* Host controller reset: clear operational registers */
            s->usbcmd = 0;
            s->usbsts = 0;
            s->usbintr = 0;
            s->frindex = 0;
            s->ctrldssegment = 0;
            s->periodiclistbase = 0;
            s->asynclistaddr = 0;
            s->configflag = 0;
            for (int i = 0; i < 15; i++) {
                s->port_status[i] = 0x1000; /* Port power */
            }
            ehci_update_irq(s);
        } else {
            s->usbcmd = val & 0xFF; /* Keep only RUN, RESET, etc. */
        }
        break;
    case EHCI_OP_BASE + EHCI_USBSTS:
        /* Write-1-to-clear logic */
        s->usbsts &= ~val;
        ehci_update_irq(s);
        break;
    case EHCI_OP_BASE + EHCI_USBINTR:
        s->usbintr = val & 0x3F; /* Interrupt bits 0-5 */
        ehci_update_irq(s);
        break;
    case EHCI_OP_BASE + EHCI_FRINDEX:
        s->frindex = val & 0x3FFF; /* 14-bit frame index */
        break;
    case EHCI_OP_BASE + EHCI_CTRLDSSEGMENT:
        s->ctrldssegment = val;
        break;
    case EHCI_OP_BASE + EHCI_PERIODICLISTBASE:
        s->periodiclistbase = val;
        break;
    case EHCI_OP_BASE + EHCI_ASYNCLISTADDR:
        s->asynclistaddr = val;
        break;
    case EHCI_OP_BASE + EHCI_CONFIGFLAG:
        s->configflag = val & 1;
        break;
    default:
        if (addr >= EHCI_OP_BASE + EHCI_PORTSC &&
            addr < EHCI_OP_BASE + EHCI_PORTSC + 15 * 4) {
            int port = (addr - (EHCI_OP_BASE + EHCI_PORTSC)) / 4;
            s->port_status[port] = val; /* Full write, may implement W1C later */
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: bad write at 0x%" HWADDR_PRIx " size %u val 0x%" PRIx64 "\n",
                          __func__, addr, size, val);
        }
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

/* I/O ops (unused, kept for completeness) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/*======================================================================
 * Device lifecycle
 *======================================================================*/
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset operational registers to default state */
    s->usbcmd = 0;
    s->usbsts = 0;
    s->usbintr = 0;
    s->frindex = 0;
    s->ctrldssegment = 0;
    s->periodiclistbase = 0;
    s->asynclistaddr = 0;
    s->configflag = 0;
    for (int i = 0; i < 15; i++) {
        s->port_status[i] = 0x1000; /* Port powered */
    }
    ehci_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE || bi->size == 0) {
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
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x20);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c03);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCIe and PM capabilities (may be overridden if driver doesn't use) */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Capability register initial values */
    s->hcs_params = 0x01110001; /* 1 port, 1 companion, port power control */
    s->hcc_params = 0;          /* No extended capabilities */

    /* BAR Initialization: single MMIO BAR at index 0, size 256 bytes */
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x100,
        .name = "ehci-mmio"
    };
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ehci_pci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

/*======================================================================
 * Class initialization
 *======================================================================*/
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
