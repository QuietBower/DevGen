#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_regs.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_CDNSP_PCI_HOSTDEV "cdnsp_pci_hostdev"
#define TYPE_CDNSP_PCI_OTG     "cdnsp_pci_otg"

typedef struct PCIBaseState PCIBaseState;
DECLARE_INSTANCE_CHECKER(PCIBaseState, CDNSP_PCI_HOSTDEV, TYPE_CDNSP_PCI_HOSTDEV);
DECLARE_INSTANCE_CHECKER(PCIBaseState, CDNSP_PCI_OTG, TYPE_CDNSP_PCI_OTG);

#define PCI_VENDOR_ID_CDNS          0x17cd
#define PCI_DEVICE_ID_CDNS_USBSSP   0x0200
#define PCI_DEVICE_ID_CDNS_USBSS    0x0201
#define PCI_CLASS_SERIAL_USB_DEVICE 0x0c03fe

#define PCI_BAR_HOST 0
#define PCI_BAR_DEV  2
#define PCI_BAR_OTG  0
#define CDNS_XHCI_RESOURCES_NUM 2

/* Register structures derived from Linux driver headers */
struct cdns3_usb_regs {
	uint32_t usb_conf;
	uint32_t usb_sts;
	uint32_t usb_cmd;
	uint32_t usb_itpn;
	uint32_t usb_lpm;
	uint32_t usb_ien;
	uint32_t usb_ists;
	uint32_t ep_sel;
	uint32_t ep_traddr;
	uint32_t ep_cfg;
	uint32_t ep_cmd;
	uint32_t ep_sts;
	uint32_t ep_sts_sid;
	uint32_t ep_sts_en;
	uint32_t drbl;
	uint32_t ep_ien;
	uint32_t ep_ists;
	uint32_t usb_pwr;
	uint32_t usb_conf2;
	uint32_t usb_cap1;
	uint32_t usb_cap2;
	uint32_t usb_cap3;
	uint32_t usb_cap4;
	uint32_t usb_cap5;
	uint32_t usb_cap6;
	uint32_t usb_cpkt1;
	uint32_t usb_cpkt2;
	uint32_t usb_cpkt3;
	uint32_t ep_dma_ext_addr;
	uint32_t buf_addr;
	uint32_t buf_data;
	uint32_t buf_ctrl;
	uint32_t dtrans;
	uint32_t tdl_from_trb;
	uint32_t tdl_beh;
	uint32_t ep_tdl;
	uint32_t tdl_beh2;
	uint32_t dma_adv_td;
	uint32_t reserved1[26];
	uint32_t cfg_reg1;
	uint32_t dbg_link1;
	uint32_t dbg_link2;
	uint32_t cfg_regs[74];
	uint32_t reserved2[51];
	uint32_t dma_axi_ctrl;
	uint32_t dma_axi_id;
	uint32_t dma_axi_cap;
	uint32_t dma_axi_ctrl0;
	uint32_t dma_axi_ctrl1;
};

struct cdns3_otg_legacy_regs {
	uint32_t cmd;
	uint32_t sts;
	uint32_t state;
	uint32_t refclk;
	uint32_t ien;
	uint32_t ivect;
	uint32_t reserved1[3];
	uint32_t tmr;
	uint32_t reserved2[2];
	uint32_t version;
	uint32_t capabilities;
	uint32_t reserved3[2];
	uint32_t simulate;
	uint32_t reserved4[5];
	uint32_t ctrl1;
};

struct cdns3_otg_regs {
	uint32_t did;
	uint32_t rid;
	uint32_t capabilities;
	uint32_t reserved1;
	uint32_t cmd;
	uint32_t sts;
	uint32_t state;
	uint32_t reserved2;
	uint32_t ien;
	uint32_t ivect;
	uint32_t refclk;
	uint32_t tmr;
	uint32_t reserved3[4];
	uint32_t simulate;
	uint32_t override;
	uint32_t susp_ctrl;
	uint32_t phyrst_cfg;
	uint32_t anasts;
	uint32_t adp_ramp_time;
	uint32_t ctrl1;
	uint32_t ctrl2;
};

struct cdnsp_otg_regs {
	uint32_t did;
	uint32_t rid;
	uint32_t cfgs1;
	uint32_t cfgs2;
	uint32_t cmd;
	uint32_t sts;
	uint32_t state;
	uint32_t ien;
	uint32_t ivect;
	uint32_t tmr;
	uint32_t simulate;
	uint32_t adpbc_sts;
	uint32_t adp_ramp_time;
	uint32_t adpbc_ctrl1;
	uint32_t adpbc_ctrl2;
	uint32_t override;
	uint32_t vbusvalid_dbnc_cfg;
	uint32_t sessvalid_dbnc_cfg;
	uint32_t susp_timing_ctrl;
};

struct cdns_otg_common_regs {
	uint32_t cmd;
	uint32_t sts;
	uint32_t state;
};

struct cdns_otg_irq_regs {
	uint32_t ien;
	uint32_t ivect;
};

#define CDNS3_CONTROLLER_V0  0
#define CDNS3_CONTROLLER_V1  1
#define CDNSP_CONTROLLER_V2  2

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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* No register shadows needed – using RAM backing */

    /* No DMA context */

    /* No operational status flags */

    /* No special reset state */

    /* Power management state (standard PCI PM) */
    /* No additional device-specific state */
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* IRQ update logic not needed */
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* DMA logic not needed */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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
    PCIBaseState *s = CDNSP_PCI_HOSTDEV(dev) ? CDNSP_PCI_HOSTDEV(dev) : CDNSP_PCI_OTG(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* No additional reset logic */
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
    PCIBaseState *s = CDNSP_PCI_HOSTDEV(pdev) ? CDNSP_PCI_HOSTDEV(pdev) : CDNSP_PCI_OTG(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CDNS);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_USB_DEVICE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    uint16_t dev_id = pci_get_word(pci_conf + PCI_DEVICE_ID);
    s->num_bars = 0;

    if (dev_id == PCI_DEVICE_ID_CDNS_USBSSP) {
        /* Function 0: Host (BAR0) + Device (BAR2) */
        s->bar_info[0].index = PCI_BAR_HOST; /* 0 */
        s->bar_info[0].type = BAR_TYPE_RAM;
        s->bar_info[0].size = 0x1000;
        s->bar_info[0].name = "cdnsp_pci_bar0_host";
        s->num_bars++;

        s->bar_info[1].index = PCI_BAR_DEV; /* 2 */
        s->bar_info[1].type = BAR_TYPE_RAM;
        s->bar_info[1].size = 0x1000;
        s->bar_info[1].name = "cdnsp_pci_bar2_dev";
        s->num_bars++;
    } else if (dev_id == PCI_DEVICE_ID_CDNS_USBSS) {
        /* Function 1: OTG (BAR0) */
        s->bar_info[0].index = PCI_BAR_OTG; /* 0 */
        s->bar_info[0].type = BAR_TYPE_RAM;
        s->bar_info[0].size = 0x1000;
        s->bar_info[0].name = "cdnsp_pci_bar0_otg";
        s->num_bars++;
    } else {
        error_setg(errp, "Unknown device ID 0x%04x", dev_id);
        return;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (*errp) {
            return;
        }
    }

    /* MSI/MSI-X not enabled by driver */
    /* DMA configuration not needed */
    /* No timers identified */
    /* No final initialization sequence */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = CDNSP_PCI_HOSTDEV(pdev) ? CDNSP_PCI_HOSTDEV(pdev) : CDNSP_PCI_OTG(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "cdnsp_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_hostdev_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    k->vendor_id = PCI_VENDOR_ID_CDNS;
    k->device_id = PCI_DEVICE_ID_CDNS_USBSSP;
    k->revision  = 0x01;
    k->class_id  = PCI_CLASS_SERIAL_USB_DEVICE;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_otg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    k->vendor_id = PCI_VENDOR_ID_CDNS;
    k->device_id = PCI_DEVICE_ID_CDNS_USBSS;
    k->revision  = 0x01;
    k->class_id  = PCI_CLASS_SERIAL_USB_DEVICE;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo cdnsp_pci_hostdev_info = {
        .name          = TYPE_CDNSP_PCI_HOSTDEV,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_hostdev_class_init,
        .interfaces    = interfaces,
    };

    static const TypeInfo cdnsp_pci_otg_info = {
        .name          = TYPE_CDNSP_PCI_OTG,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_otg_class_init,
        .interfaces    = interfaces,
    };

    type_register_static(&cdnsp_pci_hostdev_info);
    type_register_static(&cdnsp_pci_otg_info);
}
type_init(pcibase_register_types);