/*
 * QAT C62x VF minimal PCI device model for QEMU 8.2.x (fixed MSI and PF comms)
 *
 * This model only implements behavior that is directly visible
 * from the failure logs and minimal expectations of a SR-IOV VF:
 *  - Provide MSI capability so pci_enable_msi() succeeds
 *  - Expose a dummy SR-IOV capability in config-space so VF probing proceeds
 *  - Provide one MMIO BAR (BAR0) which is enabled so that
 *    VF<->PF mailbox accesses (not fully modeled) do not immediately fail.
 *
 * The BAR and capability layout is intentionally minimal and does not
 * implement any real crypto or mailbox protocol. It only aims to reach
 * "Kernel driver in use" by preventing the early -22 (MSI) and gross
 * config-space mismatches that cause the VF driver to abort probing.
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

#define TYPE_PCIBASE_DEVICE "c6xxvf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Constants visible in the provided driver snippet (kept for context) */
#define ADF_C62XIOV_ETR_MAX_BANKS 1
#define ADF_C62XIOV_MAX_ACCELENGINES 1
#define ADF_C62XIOV_MAX_ACCELERATORS 1
#define ADF_C62XIOV_RX_RINGS_OFFSET 8
#define ADF_C62XIOV_TX_RINGS_MASK 0xFF
#define ADF_C62XIOV_ACCELERATORS_MASK 0x1
#define ADF_C62XIOV_ACCELENGINES_MASK 0x1
#define ADF_C62XIOV_ETR_BAR 0
#define ADF_C62XIOV_PMISC_BAR 1
#define PCI_DEVICE_ID_INTEL_QAT_C62X_VF 0x37c9
#define ADF_C62XVF_DEVICE_NAME "c6xxvf"

/* Provide missing SR-IOV capability ID so compilation succeeds. */
#ifndef PCI_CAP_ID_SRIOV
#define PCI_CAP_ID_SRIOV 0x10
#endif

/* BAR metadata definition */
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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

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

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    MemoryRegion *mr;

    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        memcpy(&val, s->mmio_backing + addr, size);
        return val;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        memcpy(s->mmio_backing + addr, &val, size);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    /* Allow the guest to program BARs and MSI/MSI-X capabilities */
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int i;

    /* Vendor/device/class from driver table */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_QAT_C62X_VF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Minimal BAR layout: at least BAR0 MMIO for VF access */
    s->num_bars = 1;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000; /* minimal non-zero size */
    s->bar_info[0].name = "qat_c62xvf-bar0";
    s->bar_info[0].sparse = false;

    s->mmio_backing_size = s->bar_info[0].size;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    for (i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* Initialize MSI capability so that MSI enable in the driver succeeds. */
    if (msi_init(pdev, 0x50, 1, true, false, errp)) {
        /* On failure, abort realize so device is not partially visible */
        return;
    }
    s->has_msi = true;

    /* Expose a minimal SR-IOV capability stub so that VF probing code that
     * expects a VF device behind an SR-IOV PF does not bail out early.
     * The exact fields are not modeled; only the presence of the capability
     * structure is provided.
     */
    pci_set_word(pci_conf + PCI_STATUS,
                 pci_get_word(pci_conf + PCI_STATUS) | PCI_STATUS_CAP_LIST);
    /* Place a dummy SR-IOV capability after MSI. Offset 0x60 chosen to
     * avoid overlapping the MSI capability at 0x50. The exact location is
     * not critical for VF behavior as long as it is consistent.
     */
    uint8_t cap_ptr = 0x50; /* MSI cap start */
    uint8_t sriov_cap_offset = 0x60;

    /* Link MSI capability to SR-IOV capability via next pointer */
    pci_conf[cap_ptr + 1] = sriov_cap_offset; /* next capability pointer */

    /* Build a minimal SR-IOV capability header at sriov_cap_offset */
    pci_conf[sriov_cap_offset + 0] = PCI_CAP_ID_SRIOV; /* Capability ID */
    pci_conf[sriov_cap_offset + 1] = 0x00;             /* No further caps */

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
        s->mmio_backing_size = 0;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;
    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;

    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
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

