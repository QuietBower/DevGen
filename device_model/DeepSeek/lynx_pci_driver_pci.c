/*
 * QEMU virtual device for TI PCILynx 1394 controller (nosy.c driver)
 * Based on Linux driver analysis, emulating register-level interface
 * for successful driver probing and binding.
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

#define TYPE_PCIBASE_DEVICE "lynx_pci_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_TI_PCILYNX         0x8000

#define BAR0_SIZE                        0x1000

/* Register offsets */
#define MISC_CONTROL                     0x40
#define PCI_INT_STATUS                   0x48
#define PCI_INT_ENABLE                   0x4c
#define DMA0_CURRENT_PCL                 0x104
#define DMA0_CHAN_CTRL                   0x110
#define LINK_PHY                         0xf0c
#define LINK_CONTROL                     0xf04
#define LINK_INT_STATUS                  0xf14
#define LINK_INT_ENABLE                  0xf18
#define FIFO_SIZES                       0xa00
#define DMA_GLOBAL_REGISTER              0x908
#define PCILYNX_MAX_REGISTER             0xfff

/* Bit definitions */
#define MISC_CONTROL_SWRESET             (1<<0)
#define PCI_INT_DMA0_HLT                 (1<<0)
#define PCI_INT_P1394_INT                (1<<16)
#define PCI_INT_INT_PEND                 (1<<31)
#define PCI_INT_DMA_ALL                  0x3ff
#define DMA_CHAN_CTRL_ENABLE             (1<<31)
#define DMA_CHAN_CTRL_LINK               (1<<29)
#define PCL_CMD_RCV                      (0x1<<24)
#define PCL_LAST_BUFF                    (1<<18)
#define PCL_BIGENDIAN                    (1<<16)
#define PCL_NEXT_INVALID                 (1<<0)
#define LINK_PHY_WRITE                   (1<<30)
#define LINK_PHY_WDATA(data)             ((data)<<16)
#define LINK_PHY_ADDR(addr)              ((addr)<<24)
#define LINK_CONTROL_SNOOP_ENABLE        (1<<6)
#define LINK_INT_GRF_OVER_FLOW           (1<<5)
#define LINK_INT_ATF_UNDER_FLOW          (1<<3)
#define LINK_INT_ITF_UNDER_FLOW          (1<<4)
#define LINK_INT_TC_ERR                  (1<<15)
#define LINK_INT_SNTRJ                   (1<<17)
#define LINK_INT_AT_STUCK                (1<<19)
#define LINK_INT_IT_STUCK                (1<<20)
#define LINK_INT_PHY_BUSRESET            (1<<28)
#define LINK_INT_PHY_REG_RCVD            (1<<29)
#define LINK_INT_PHY_TIME_OUT            (1<<30)

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
    uint32_t pci_int_status;
    uint32_t pci_int_enable;
    uint32_t link_int_status;
    uint32_t link_int_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x400];  /* 0x1000 bytes, 4-byte register accesses */

    /* DMA Context -- not emulated, driver probing does not depend on it */
    /* Operational status flags */
    uint32_t status;

    /* State used to handle reset sequences */
    uint32_t reset_state;
};

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint64_t val = 0;

    /* Driver accesses all registers with 32-bit readl/writel */
    if (addr + size <= 0x1000 && size == 4) {
        unsigned idx = addr >> 2;
        val = s->regs[idx];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        val = ~0ULL;
    }

    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);

    /* Same constraint: all register writes are 32-bit */
    if (addr + size <= 0x1000 && size == 4) {
        unsigned idx = addr >> 2;
        switch (addr) {
        case PCI_INT_STATUS:
            /* Write-1-to-clear status bits */
            s->regs[idx] &= ~val;
            s->pci_int_status = s->regs[idx];
            break;
        case LINK_INT_STATUS:
            /* Write-1-to-clear status bits */
            s->regs[idx] &= ~val;
            s->link_int_status = s->regs[idx];
            break;
        case PCI_INT_ENABLE:
            s->regs[idx] = val;
            s->pci_int_enable = val;
            break;
        case LINK_INT_ENABLE:
            s->regs[idx] = val;
            s->link_int_enable = val;
            break;
        default:
            s->regs[idx] = val;
            break;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write addr=0x%" HWADDR_PRIx " size=%u val=0x%" PRIx64 "\n",
                      __func__, addr, size, val);
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

    /* Clear all emulated registers to match power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->pci_int_status = 0;
    s->pci_int_enable = 0;
    s->link_int_status = 0;
    s->link_int_enable = 0;
    s->status = 0;
    s->reset_state = 0;
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
    }
    /* Device only registers MMIO BAR0; PIO and RAM handling omitted to avoid unused code */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_TI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_TI_PCILYNX);
    pci_config_set_class(pci_conf, 0x0c0010);
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
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "lynx-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA configuration is not required for driver probing; omitted */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No additional cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "lynx_pci_driver_pci",
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
