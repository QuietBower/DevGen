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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "thunder_xcv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Vendor and Device IDs */
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define THUNDER_XCV_DEVICE_ID 0xA056
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Register Offsets */
#define XCV_RESET            0x00
#define XCV_DLL_CTL          0x10
#define XCV_COMP_CTL         0x20
#define XCV_CTL              0x30
#define XCV_INT              0x40
#define XCV_INT_W1S          0x48
#define XCV_INT_ENA_W1C      0x50
#define XCV_INT_ENA_W1S      0x58
#define XCV_INBND_STATUS     0x80
#define XCV_BATCH_CRD_RET    0x100

/* Bit definitions */
#define PORT_EN              BIT_ULL(63)
#define CLK_RESET            BIT_ULL(15)
#define DLL_RESET            BIT_ULL(11)
#define COMP_EN              BIT_ULL(7)
#define TX_PKT_RESET         BIT_ULL(3)
#define TX_DATA_RESET        BIT_ULL(2)
#define RX_PKT_RESET         BIT_ULL(1)
#define RX_DATA_RESET        BIT_ULL(0)
#define CLKRX_BYP            BIT_ULL(23)
#define CLKTX_BYP            BIT_ULL(15)
#define DRV_BYP              BIT_ULL(63)

/* BAR0 size (not explicitly defined in driver; set to 0x200 based on register map range) */
#define BAR0_SIZE 0x200

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
    uint64_t intr_status;
    uint64_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint64_t reset;
    uint64_t dll_ctl;
    uint64_t comp_ctl;
    uint64_t ctl;
    uint64_t int_reg;
    uint64_t int_w1s;
    uint64_t int_ena_w1c;
    uint64_t int_ena_w1s;
    uint64_t inbnd_status;
    uint64_t batch_crd_ret;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No interrupt logic required based on driver source. */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case XCV_RESET:
        val = s->reset;
        break;
    case XCV_DLL_CTL:
        val = s->dll_ctl;
        break;
    case XCV_COMP_CTL:
        val = s->comp_ctl;
        break;
    case XCV_CTL:
        val = s->ctl;
        break;
    case XCV_INT:
        val = s->int_reg;
        break;
    case XCV_INT_W1S:
        val = s->int_w1s;
        break;
    case XCV_INT_ENA_W1C:
        val = s->int_ena_w1c;
        break;
    case XCV_INT_ENA_W1S:
        val = s->int_ena_w1s;
        break;
    case XCV_INBND_STATUS:
        val = s->inbnd_status;
        break;
    case XCV_BATCH_CRD_RET:
        val = s->batch_crd_ret;
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
    case XCV_RESET:
        s->reset = val;
        break;
    case XCV_DLL_CTL:
        s->dll_ctl = val;
        break;
    case XCV_COMP_CTL:
        s->comp_ctl = val;
        break;
    case XCV_CTL:
        s->ctl = val;
        break;
    case XCV_INT:
        s->int_reg = val;
        break;
    case XCV_INT_W1S:
        s->int_w1s = val;
        break;
    case XCV_INT_ENA_W1C:
        s->int_ena_w1c = val;
        break;
    case XCV_INT_ENA_W1S:
        s->int_ena_w1s = val;
        break;
    case XCV_INBND_STATUS:
        s->inbnd_status = val;
        break;
    case XCV_BATCH_CRD_RET:
        s->batch_crd_ret = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO not used by driver; return 0 */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* PIO not used by driver; no-op */
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

    /* Power-on defaults: DLL and clock in reset, other registers zero. */
    s->reset = DLL_RESET | CLK_RESET;
    s->dll_ctl = 0;
    s->comp_ctl = 0;
    s->ctl = 0;
    s->int_reg = 0;
    s->int_w1s = 0;
    s->int_ena_w1c = 0;
    s->int_ena_w1s = 0;
    s->inbnd_status = 0;
    s->batch_crd_ret = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CAVIUM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  THUNDER_XCV_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "thunder_xcv-bar0"
    };
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
