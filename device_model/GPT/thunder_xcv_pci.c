/*
 * QEMU PCI device model for Cavium Thunder XCV
 * Generated to satisfy Linux driver thunder_xcv.c expectations.
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "thunder_xcv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define XCV_DRV_NAME            "thunder_xcv"
#define XCV_DRV_VERSION         "1.0"

#define XCV_RESET               0x00
#define XCV_DLL_CTL             0x10
#define XCV_COMP_CTL            0x20
#define XCV_CTL                 0x30
#define XCV_INT                 0x40
#define XCV_INT_W1S             0x48
#define XCV_INT_ENA_W1C         0x50
#define XCV_INT_ENA_W1S         0x58
#define XCV_INBND_STATUS        0x80
#define XCV_BATCH_CRD_RET       0x100

#define XCV_PCI_CFG_REG_BAR_NUM 0

/* Define vendor ID directly to avoid dependency on undeclared PCI_VENDOR_ID_CAVIUM */
#define XCV_VENDOR_ID           0x177d
#define XCV_DEVICE_ID           0xA056
#define XCV_CLASS_ID            PCI_CLASS_NETWORK_ETHERNET

#define XCV_BAR0_SIZE           (4 * 1024)

#define XCV_PORT_EN             (1ULL << 63)
#define XCV_CLK_RESET           (1ULL << 15)
#define XCV_DLL_RESET           (1ULL << 11)
#define XCV_COMP_EN             (1ULL << 7)
#define XCV_TX_PKT_RESET        (1ULL << 3)
#define XCV_TX_DATA_RESET       (1ULL << 2)
#define XCV_RX_PKT_RESET        (1ULL << 1)
#define XCV_RX_DATA_RESET       (1ULL << 0)
#define XCV_CLKRX_BYP           (1ULL << 23)
#define XCV_CLKTX_BYP           (1ULL << 15)
#define XCV_DRV_BYP             (1ULL << 63)

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint64_t regs[0x200 / sizeof(uint64_t)];

};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /*
     * The provided thunder_xcv driver does not reference any interrupt
     * registers or ISRs. To avoid inventing hardware behavior, this
     * helper intentionally does nothing.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The thunder_xcv driver does not set up or use DMA. Avoid
     * implementing any DMA behavior.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only BAR0 is implemented; driver uses readq_relaxed (64-bit). */

    if (addr >= sizeof(s->regs) * sizeof(uint64_t)) {
        /* Out of implemented range: return 0 */
        return 0;
    }

    /* Align to 8-byte register index */
    addr &= ~0x7ULL;
    val = s->regs[addr / 8];

    /* For reads smaller than 8 bytes, return the lower bytes */
    switch (size) {
    case 1:
        val &= 0xFFULL;
        break;
    case 2:
        val &= 0xFFFFULL;
        break;
    case 4:
        val &= 0xFFFFFFFFULL;
        break;
    case 8:
        /* full 64-bit */
        break;
    default:
        /* Invalid size for this device; log and return 0 */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thunder_xcv: invalid MMIO read size %u at 0x%"HWADDR_PRIx"\n",
                      size, addr);
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs) * sizeof(uint64_t)) {
        /* Ignore writes beyond implemented register space */
        return;
    }

    /* Align to 8-byte register index */
    addr &= ~0x7ULL;

    /* Normalize value according to access size, mirroring read behavior */
    switch (size) {
    case 1:
        val &= 0xFFULL;
        break;
    case 2:
        val &= 0xFFFFULL;
        break;
    case 4:
        val &= 0xFFFFFFFFULL;
        break;
    case 8:
        /* full 64-bit */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "thunder_xcv: invalid MMIO write size %u at 0x%"HWADDR_PRIx"\n",
                      size, addr);
        return;
    }

    /*
     * Register-specific behaviors that are explicitly visible
     * in the driver are minimal: the driver performs read-modify-write
     * sequences, expects the values it wrote to persist, and does not
     * check for any specific hardware-driven bits or interrupts.
     * Implement simple shadow registers accordingly.
     */

    switch (addr) {
    case XCV_RESET:
        /* All control bits are simple R/W in the driver's usage. */
        s->regs[XCV_RESET / 8] = val;
        break;
    case XCV_DLL_CTL:
        s->regs[XCV_DLL_CTL / 8] = val;
        break;
    case XCV_COMP_CTL:
        /* Not directly used by provided driver snippet; keep as generic R/W. */
        s->regs[XCV_COMP_CTL / 8] = val;
        break;
    case XCV_CTL:
        s->regs[XCV_CTL / 8] = val;
        break;
    case XCV_INT:
    case XCV_INT_W1S:
    case XCV_INT_ENA_W1C:
    case XCV_INT_ENA_W1S:
    case XCV_INBND_STATUS:
        /*
         * Interrupt-related and status registers are not used by the
         * driver snippet. Implement as simple read/write shadows
         * without side effects, to avoid guessing semantics.
         */
        s->regs[addr / 8] = val;
        break;
    case XCV_BATCH_CRD_RET:
        /* Driver writes 0x1 to return credits; no reads. Just latch. */
        s->regs[XCV_BATCH_CRD_RET / 8] = val;
        break;
    default:
        /* Generic R/W for unmodeled registers within range */
        s->regs[addr / 8] = val;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Thunder XCV driver does not use PIO/IO ports. Return 0. */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Thunder XCV driver does not use PIO/IO ports. Ignore. */
    (void)opaque;
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
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    /* Clear all shadow registers to 0 on reset. */
    for (i = 0; i < (int)ARRAY_SIZE(s->regs); i++) {
        s->regs[i] = 0;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  XCV_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  XCV_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, XCV_CLASS_ID );
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
    s->bar_info[0].index = XCV_PCI_CFG_REG_BAR_NUM;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = XCV_BAR0_SIZE;
    s->bar_info[0].name = "thunder_xcv-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X or DMA/timers explicitly referenced in provided driver snippet */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "thunder_xcv_pci",
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
