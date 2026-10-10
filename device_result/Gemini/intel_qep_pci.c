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

#define TYPE_PCIBASE_DEVICE "intel_qep_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define INTEL_QEPCON            0x00
#define INTEL_QEPFLT            0x04
#define INTEL_QEPCOUNT          0x08
#define INTEL_QEPMAX            0x0c
#define INTEL_QEPWDT            0x10
#define INTEL_QEPCAPDIV         0x14
#define INTEL_QEPCNTR           0x18
#define INTEL_QEPCAPBUF         0x1c
#define INTEL_QEPINT_STAT       0x20
#define INTEL_QEPINT_MASK       0x24

#define INTEL_QEPCON_EN         (1 << 0)
#define INTEL_QEPCON_FLT_EN     (1 << 1)
#define INTEL_QEPCON_EDGE_A     (1 << 2)
#define INTEL_QEPCON_EDGE_B     (1 << 3)
#define INTEL_QEPCON_EDGE_INDX  (1 << 4)
#define INTEL_QEPCON_SWPAB      (1 << 5)
#define INTEL_QEPCON_OP_MODE    (1 << 6)
#define INTEL_QEPCON_PH_ERR     (1 << 7)
#define INTEL_QEPCON_COUNT_RST_MODE (1 << 8)
#define INTEL_QEPCON_INDX_GATING_MASK (3 << 9)
#define INTEL_QEPCON_CAP_MODE   (1 << 11)
#define INTEL_QEPCON_FIFO_THRE_MASK (7 << 12)
#define INTEL_QEPCON_FIFO_EMPTY (1 << 15)

#define INTEL_QEPINT_FIFOCRIT   (1 << 5)
#define INTEL_QEPINT_FIFOENTRY  (1 << 4)
#define INTEL_QEPINT_QEPDIR     (1 << 3)
#define INTEL_QEPINT_QEPRST_UP  (1 << 2)
#define INTEL_QEPINT_QEPRST_DOWN (1 << 1)
#define INTEL_QEPINT_WDT        (1 << 0)
#define INTEL_QEPINT_MASK_ALL   0x3F

#define INTEL_QEPFLT_MAX_COUNT(n)   ((n) & 0x1fffff)
#define INTEL_QEP_CLK_PERIOD_NS     10

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
    uint32_t qepint_stat;
    uint32_t qepint_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t qepcon;
    uint32_t qepflt;
    uint32_t qepcount;
    uint32_t qepmax;
    uint32_t qepwdt;
    uint32_t qepcapdiv;
    uint32_t qepcntr;
    uint32_t qepcapbuf;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* No ISR logic explicitly defined in the provided driver source */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case INTEL_QEPCON:
        val = s->qepcon;
        break;
    case INTEL_QEPFLT:
        val = s->qepflt;
        break;
    case INTEL_QEPCOUNT:
        val = s->qepcount;
        break;
    case INTEL_QEPMAX:
        val = s->qepmax;
        break;
    case INTEL_QEPWDT:
        val = s->qepwdt;
        break;
    case INTEL_QEPCAPDIV:
        val = s->qepcapdiv;
        break;
    case INTEL_QEPCNTR:
        val = s->qepcntr;
        break;
    case INTEL_QEPCAPBUF:
        val = s->qepcapbuf;
        break;
    case INTEL_QEPINT_STAT:
        val = s->qepint_stat;
        break;
    case INTEL_QEPINT_MASK:
        val = s->qepint_mask;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case INTEL_QEPCON:
        s->qepcon = val;
        break;
    case INTEL_QEPFLT:
        s->qepflt = val;
        break;
    case INTEL_QEPCOUNT:
        s->qepcount = val;
        break;
    case INTEL_QEPMAX:
        s->qepmax = val;
        break;
    case INTEL_QEPWDT:
        s->qepwdt = val;
        break;
    case INTEL_QEPCAPDIV:
        s->qepcapdiv = val;
        break;
    case INTEL_QEPCNTR:
        s->qepcntr = val;
        break;
    case INTEL_QEPCAPBUF:
        s->qepcapbuf = val;
        break;
    case INTEL_QEPINT_STAT:
        s->qepint_stat = val;
        break;
    case INTEL_QEPINT_MASK:
        s->qepint_mask = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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

    s->qepcon = 0;
    s->qepflt = 0;
    s->qepcount = 0;
    s->qepmax = 0;
    s->qepwdt = 0;
    s->qepcapdiv = 0;
    s->qepcntr = 0;
    s->qepcapbuf = 0;
    s->qepint_stat = 0;
    s->qepint_mask = 0;
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x4bc3 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "qep-mmio" };
      
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
    .name = "intel_qep_pci",
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
