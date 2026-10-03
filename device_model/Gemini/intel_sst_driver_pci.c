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

#define TYPE_PCIBASE_DEVICE "intel_sst_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_SST_MRFLD 0x082a

#define SST_CSR 0x00
#define SST_ISRX 0x18
#define SST_IMRX 0x28
#define SST_IPCX 0x38
#define SST_IPCD 0x40
#define SST_TIME_STAMP_MRFLD 0x800
#define SST_MAILBOX_SIZE 0x0400
#define SST_MAILBOX_SEND 0x0000

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
    uint64_t isrx;
    uint64_t imrx;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint64_t csr;
    uint64_t ipcx;
    uint64_t ipcd;

    int sst_state;
    
    uint8_t mailbox[SST_MAILBOX_SIZE];
};

union config_status_reg_mrfld {
    struct {
        uint64_t lpe_reset:1;
        uint64_t lpe_reset_vector:1;
        uint64_t runstall:1;
        uint64_t pwaitmode:1;
        uint64_t clk_sel:3;
        uint64_t rsvd2:1;
        uint64_t sst_clk:3;
        uint64_t xt_snoop:1;
        uint64_t rsvd3:4;
        uint64_t clk_sel1:6;
        uint64_t clk_enable:3;
        uint64_t rsvd4:6;
        uint64_t slim0baseclk:1;
        uint64_t rsvd:32;
    } part;
    uint64_t full;
};

union interrupt_reg_mrfld {
    struct {
        uint64_t done_interrupt:1;
        uint64_t busy_interrupt:1;
        uint64_t rsvd:62;
    } part;
    uint64_t full;
};

union sst_imr_reg_mrfld {
    struct {
        uint64_t done_interrupt:1;
        uint64_t busy_interrupt:1;
        uint64_t rsvd:62;
    } part;
    uint64_t full;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    
    switch (addr) {
    case SST_CSR:
        val = s->csr;
        break;
    case SST_ISRX:
        val = s->isrx;
        break;
    case SST_IMRX:
        val = s->imrx;
        break;
    case SST_IPCX:
        val = s->ipcx;
        break;
    case SST_IPCD:
        val = s->ipcd;
        break;
    default:
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
    case SST_CSR:
        s->csr = val;
        break;
    case SST_IMRX:
        s->imrx = val;
        break;
    case SST_IPCX:
        s->ipcx = val;
        break;
    case SST_IPCD:
        s->ipcd = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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
    
    s->csr = 0;
    s->isrx = 0;
    s->imrx = 0;
    s->ipcx = 0;
    s->ipcd = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_SST_MRFLD );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 5;  
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x400000, .name = "ddr" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "shim" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "mailbox" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_MMIO, .size = 0x20000, .name = "iram" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_MMIO, .size = 0x40000, .name = "dram" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "intel_sst_driver_pci",
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
