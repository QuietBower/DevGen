
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

#define TYPE_PCIBASE_DEVICE "mptspi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define MPI_MANUFACTPAGE_DEVID_53C1030 0x0030
#define VENDOR_ID PCI_VENDOR_ID_LSI_LOGIC
#define DEVICE_ID MPI_MANUFACTPAGE_DEVID_53C1030
#define CLASS_ID PCI_CLASS_STORAGE_SCSI

/* SYSIF_REGS definition from Linux driver */
typedef struct {
    uint32_t Doorbell;
    uint32_t WriteSequence;
    uint32_t Diagnostic;
    uint32_t TestBase;
    uint32_t DiagRwData;
    uint32_t DiagRwAddress;
    uint32_t Reserved1[6];
    uint32_t IntStatus;
    uint32_t IntMask;
    uint32_t Reserved2[2];
    uint32_t RequestFifo;
    uint32_t ReplyFifo;
    uint32_t RequestHiPriFifo;
    uint32_t Reserved3;
    uint32_t HostIndex;
    uint32_t Reserved4[15];
    uint32_t Fubar;
    uint32_t Reserved5[1050];
    uint32_t Reset_1078;
} SYSIF_REGS;

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

/* Message frame structures from the driver source */
typedef uint8_t U8;
typedef uint16_t U16;
typedef uint32_t U32;

typedef struct _MSG_REQUEST_HEADER {
    U8                      Reserved[2];      /* function specific */
    U8                      ChainOffset;
    U8                      Function;
    U8                      Reserved1[3];     /* function specific */
    U8                      MsgFlags;
    U32                     MsgContext;
} MSG_REQUEST_HEADER;

typedef struct _MSG_DEFAULT_REPLY {
    U8                      Reserved[2];      /* function specific */
    U8                      MsgLength;
    U8                      Function;
    U8                      Reserved1[3];     /* function specific */
    U8                      MsgFlags;
    U32                     MsgContext;
    U8                      Reserved2[2];     /* function specific */
    U16                     IOCStatus;
    U32                     IOCLogInfo;
} MSG_DEFAULT_REPLY;

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
    SYSIF_REGS regs;

    /* DMA Context */
    QEMUTimer request_timer;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->regs.IntStatus & s->regs.IntMask) {
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

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(SYSIF_REGS)) {
        uint32_t *regs = (uint32_t *)&s->regs;
        val = regs[addr >> 2];
    } else if (addr >= 0x1000 && addr < 0x2000) {
        /* IOC Facts region - read-only static data */
        qemu_log_mask(LOG_UNIMP, "mptspi: unimplemented IOC Facts read at offset 0x%lx\n", addr);
        /* #Placeholder# for IOC Facts table */
    } else if (addr >= 0x2000) {
        /* Port Facts region - read-only static data */
        qemu_log_mask(LOG_UNIMP, "mptspi: unimplemented Port Facts read at offset 0x%lx\n", addr);
        /* #Placeholder# for Port Facts table */
    } else {
        qemu_log_mask(LOG_UNIMP, "mptspi: unimplemented MMIO read at offset 0x%lx\n", addr);
    }

    return val;
}

static void pcibase_request_timer(void *opaque)
{
    qemu_log_mask(LOG_UNIMP, "mptspi: RequestFifo processing not implemented\n");
    /* #Placeholder# for RequestFifo DMA processing */
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(SYSIF_REGS)) {
        uint32_t *regs = (uint32_t *)&s->regs;
        uint32_t old_val = regs[addr >> 2];
        regs[addr >> 2] = val;
        
        /* Handle side-effects of specific registers */
        if (addr == offsetof(SYSIF_REGS, IntMask)) {
            pcibase_update_irq(s);
        } else if (addr == offsetof(SYSIF_REGS, IntStatus)) {
            /* write-1-to-clear behavior */
            regs[addr >> 2] = old_val & ~val;
            pcibase_update_irq(s);
        } else if (addr == offsetof(SYSIF_REGS, RequestFifo)) {
            /* Schedule processing of the request frame */
            timer_mod(&s->request_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        } else if (addr == offsetof(SYSIF_REGS, Doorbell)) {
            /* Doorbell write: update IOC state, log unimplemented */
            qemu_log_mask(LOG_UNIMP, "mptspi: Doorbell write not handled\n");
        } else {
            /* Other registers: no side effects */
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "mptspi: unimplemented MMIO write at offset 0x%lx\n", addr);
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
    s->intr_status = 0;
    s->intr_mask = 0;
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
        /* Not used for this device */
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

    /* Timer for request processing */
    timer_init_ms(&s->request_timer, QEMU_CLOCK_VIRTUAL, pcibase_request_timer, s);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000;
    s->bar_info[0].name = "mpt_regs";

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

    timer_del(&s->request_timer);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mptspi_pci",
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
