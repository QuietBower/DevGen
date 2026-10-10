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
#include "qemu/bitops.h"
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

#define TYPE_PCIBASE_DEVICE "cdns2_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CDNS		0x17cd
#define PCI_DEVICE_ID_CDNS_USB		0x0120
#define PCI_CLASS_SERIAL_USB_DEVICE	0x0c03fe
#define GENMASK(h, l)		MAKE_64BIT_MASK(l, (h) - (l) + 1)

#define PCI_DRIVER_NAME		"cdns-pci-usbhs"
#define PCI_BAR_DEV		0
#define PCI_DEV_FN_DEVICE	0
#define CDNS2_ENDPOINTS_NUM	31
#define MAX_ISO_SIZE		3076
#define USBCS_DISCON		BIT(6)
#define CDNS2_ADMA_REGS_OFFSET	0x400
#define CDNS2_EP_ZLP_BUF_SIZE	512
#define CPUCTRL_SW_RST		BIT(1)
#define CPUCTRL_UPCLK		BIT(0)
#define TR_SEG_SIZE		(TRB_SIZE * (TRBS_PER_SEGMENT + TRB_ISO_RESERVED))
#define CPUCTRL_WUEN		BIT(7)
#define EP_ENABLED		BIT(0)
#define EP_CLAIMED		BIT(4)
#define EP_STALL_PENDING	BIT(2)
#define EP_WEDGE		BIT(3)
#define EP_STALLED		BIT(1)
#define EP_DEFERRED_DRDY	BIT(6)
#define EP_RING_FULL		BIT(5)
#define LPMCLOCK_SLEEP_ENTRY	BIT(7)
#define USBCS_LPMNYET		BIT(2)
#define DMA_CONF_DMULT		BIT(9)
#define USB_IEN_INIT (USBIRQ_SUDAV | USBIRQ_SUSPEND | USBIRQ_URESET \
		      | USBIRQ_HSPEED | USBIRQ_LPM)
#define EXTIRQ_WAKEUP	BIT(7)
#define TRB_SIZE		(sizeof(struct cdns2_trb))
#define TRB_ISO_RESERVED	2
#define TRBS_PER_SEGMENT	600
#define USBIRQ_LPM	BIT(7)
#define USBIRQ_SUDAV	BIT(0)
#define USBIRQ_URESET	BIT(4)
#define USBIRQ_SUSPEND	BIT(3)
#define USBIRQ_HSPEED	BIT(5)
#define EP0_FIFO_AUTO	BIT(5)
#define DMA_EP_IEN_EP_IN0	BIT(16)
#define DMA_EP_IEN_EP_OUT0	BIT(0)
#define EP0_FIFO_IO_TX	BIT(4)
#define DMA_EP_CFG_ENABLE	BIT(0)
#define TRB_LINK		6
#define TRB_BUFFER(p)		((p) & GENMASK(31, 0))
#define TRB_TOGGLE		BIT(1)
#define TRB_CYCLE		BIT(0)
#define TRB_TYPE(p)		((p) << 10)
#define SPEEDCTRL_HSDISABLE	BIT(7)
#define ENDPRST_FIFORST	BIT(6)
#define ENDPRST_TOGRST	BIT(5)
#define ENDPRST_IO_TX	BIT(4)
#define DMA_EP_CMD_EPRST	BIT(0)
#define ISO_MAX_INTERVAL	8
#define DMA_EP_STS_CCS(p)	((p) & BIT(11))
#define DMA_EP_STS_EN_TRBERREN	BIT(7)
#define DMA_EP_CMD_DFLUSH	BIT(7)
#define TRB_CHAIN		BIT(4)
#define DMA_EP_STS_DBUSY	BIT(9)
#define USBCS_SIGRSUME		BIT(5)
#define CDNS2_STATUS_STAGE		0x2
#define EPX_CON_TYPE_BULK	0x8
#define EPX_CON_ISOD_SHIFT	0x4
#define EPX_CON_TYPE_ISOC	0x4
#define DMA_EP_STS_ISOERR	BIT(15)
#define EPX_CON_VAL		BIT(7)
#define FIFOCTRL_FIFOAUTO	BIT(5)
#define EPX_CON_TYPE_INT	0xC
#define FIFOCTRL_IO_TX		BIT(4)
#define DMA_EP_STS_TRBERR	BIT(7)
#define DMA_EP_CMD_DRDY		BIT(6)
#define EPX_CON_STALL		BIT(6)
#define EP0CS_HSNAK	BIT(1)
#define CDNS2_SETUP_STAGE		0x0
#define EP0CS_CHGSET	BIT(7)
#define TRB_NORMAL		1
#define TRB_LEN(p)		((p) & GENMASK(16, 0))
#define TRB_IOC			BIT(5)
#define EP0CS_DSTALL	BIT(4)
#define CDNS2_DATA_STAGE		0x1
#define TRB_MAX_ISO_BUFF_SHIFT	12
#define TRB_MAX_ISO_BUFF_SIZE	BIT(TRB_MAX_ISO_BUFF_SHIFT)
#define TRB_ISP			BIT(2)
#define TRB_BURST(p)		(((p) << 24) & GENMASK(31, 24))
#define TRB_BUFF_LEN_UP_TO_BOUNDARY(addr) (TRB_MAX_ISO_BUFF_SIZE - \
					((addr) & (TRB_MAX_ISO_BUFF_SIZE - 1)))
#define DMA_EP_STS_DESCMIS	BIT(4)
#define DMA_EP_ISTS_EP_OUT0	BIT(0)
#define DMA_EP_ISTS_EP_IN0	BIT(16)

struct cdns2_adma_regs {
    uint32_t conf;
    uint32_t sts;
    uint32_t reserved1[5];
    uint32_t ep_sel;
    uint32_t ep_traddr;
    uint32_t ep_cfg;
    uint32_t ep_cmd;
    uint32_t ep_sts;
    uint32_t reserved2;
    uint32_t ep_sts_en;
    uint32_t drbl;
    uint32_t ep_ien;
    uint32_t ep_ists;
    uint32_t axim_ctrl;
    uint32_t axim_id;
    uint32_t reserved3;
    uint32_t axim_cap;
    uint32_t reserved4;
    uint32_t axim_ctrl0;
    uint32_t axim_ctrl1;
};

struct cdns2_interrupt_regs {
    uint8_t reserved[396];
    uint8_t usbirq;
    uint8_t extirq;
    uint16_t rxpngirq;
    uint16_t reserved1[4];
    uint8_t usbien;
    uint8_t extien;
    uint16_t reserved2[3];
    uint8_t usbivect;
};

struct cdns2_usb_regs {
    uint8_t reserved[4];
    uint16_t lpmctrl;
    uint8_t lpmclock;
    uint8_t reserved2[411];
    uint8_t endprst;
    uint8_t usbcs;
    uint16_t frmnr;
    uint8_t fnaddr;
    uint8_t clkgate;
    uint8_t fifoctrl;
    uint8_t speedctrl;
    uint8_t sleep_clkgate;
    uint8_t reserved3[533];
    uint8_t cpuctrl;
};

struct cdns2_epx_base {
    uint16_t rxbc;
    uint8_t rxcon;
    uint8_t rxcs;
    uint16_t txbc;
    uint8_t txcon;
    uint8_t txcs;
};

struct cdns2_epx_regs {
    uint32_t reserved[2];
    struct cdns2_epx_base ep[15];
    uint8_t reserved2[290];
    uint8_t endprst;
    uint8_t reserved3[41];
    uint16_t isoautoarm;
    uint8_t reserved4[10];
    uint16_t isodctrl;
    uint16_t reserved5;
    uint16_t isoautodump;
    uint32_t reserved6;
    uint16_t rxmaxpack[15];
    uint32_t reserved7[65];
    uint32_t rxstaddr[15];
    uint8_t reserved8[4];
    uint32_t txstaddr[15];
    uint8_t reserved9[98];
    uint16_t txmaxpack[15];
};

struct cdns2_ep0_regs {
    uint8_t rxbc;
    uint8_t txbc;
    uint8_t cs;
    uint8_t reserved1[4];
    uint8_t fifo;
    uint32_t reserved2[94];
    uint8_t setupdat[8];
    uint8_t reserved4[88];
    uint8_t maxpack;
};

struct cdns2_trb {
    uint32_t buffer;
    uint32_t length;
    uint32_t control;
};

struct cdns2_ring {
    struct cdns2_trb *trbs;
    dma_addr_t dma;
    int free_trbs;
    uint8_t pcs;
    uint8_t ccs;
    int enqueue;
    int dequeue;
};

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
    int irq;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct cdns2_usb_regs usb_regs;
    struct cdns2_ep0_regs ep0_regs;
    struct cdns2_epx_regs epx_regs;
    struct cdns2_interrupt_regs interrupt_regs;
    struct cdns2_adma_regs adma_regs;

    /* DMA Context */
    struct cdns2_ring ring;

    uint8_t ep0_stage;
    uint8_t dev_address;
    uint32_t selected_ep;
    bool is_selfpowered;
    bool may_wakeup;
    bool in_lpm;
    uint32_t eps_supported;
    uint16_t onchip_tx_buf;
    uint16_t onchip_rx_buf;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= CDNS2_ADMA_REGS_OFFSET && addr < CDNS2_ADMA_REGS_OFFSET + sizeof(struct cdns2_adma_regs)) {
        uint32_t adma_offset = addr - CDNS2_ADMA_REGS_OFFSET;
        switch (adma_offset) {
        case offsetof(struct cdns2_adma_regs, ep_ien):
            val = s->adma_regs.ep_ien;
            break;
        case offsetof(struct cdns2_adma_regs, ep_ists):
            val = s->adma_regs.ep_ists;
            break;
        case offsetof(struct cdns2_adma_regs, ep_sts):
            val = s->adma_regs.ep_sts;
            break;
        case offsetof(struct cdns2_adma_regs, ep_cfg):
            val = s->adma_regs.ep_cfg;
            break;
        case offsetof(struct cdns2_adma_regs, ep_cmd):
            val = s->adma_regs.ep_cmd;
            break;
        case offsetof(struct cdns2_adma_regs, conf):
            val = s->adma_regs.conf;
            break;
        }
    } else {
        switch (addr) {
        case offsetof(struct cdns2_usb_regs, cpuctrl):
            val = s->usb_regs.cpuctrl;
            break;
        case offsetof(struct cdns2_usb_regs, lpmctrl):
            val = s->usb_regs.lpmctrl;
            break;
        case offsetof(struct cdns2_usb_regs, sleep_clkgate):
            val = s->usb_regs.sleep_clkgate;
            break;
        case offsetof(struct cdns2_interrupt_regs, usbien):
            val = s->interrupt_regs.usbien;
            break;
        case offsetof(struct cdns2_interrupt_regs, extien):
            val = s->interrupt_regs.extien;
            break;
        case offsetof(struct cdns2_interrupt_regs, usbirq):
            val = s->interrupt_regs.usbirq;
            break;
        case offsetof(struct cdns2_interrupt_regs, extirq):
            val = s->interrupt_regs.extirq;
            break;
        case offsetof(struct cdns2_usb_regs, usbcs):
            val = s->usb_regs.usbcs;
            break;
        case offsetof(struct cdns2_usb_regs, lpmclock):
            val = s->usb_regs.lpmclock;
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= CDNS2_ADMA_REGS_OFFSET && addr < CDNS2_ADMA_REGS_OFFSET + sizeof(struct cdns2_adma_regs)) {
        uint32_t adma_offset = addr - CDNS2_ADMA_REGS_OFFSET;
        switch (adma_offset) {
        case offsetof(struct cdns2_adma_regs, ep_ien):
            s->adma_regs.ep_ien = val;
            break;
        case offsetof(struct cdns2_adma_regs, ep_sts):
            s->adma_regs.ep_sts &= ~val; /* W1C */
            break;
        case offsetof(struct cdns2_adma_regs, ep_cfg):
            s->adma_regs.ep_cfg = val;
            break;
        case offsetof(struct cdns2_adma_regs, ep_cmd):
            s->adma_regs.ep_cmd = val;
            if (val & DMA_EP_CMD_DFLUSH) {
                s->adma_regs.ep_cmd &= ~DMA_EP_CMD_DFLUSH; /* Auto-clear */
            }
            break;
        case offsetof(struct cdns2_adma_regs, conf):
            s->adma_regs.conf = val;
            break;
        }
    } else {
        switch (addr) {
        case offsetof(struct cdns2_usb_regs, cpuctrl):
            s->usb_regs.cpuctrl = val;
            if (val & CPUCTRL_SW_RST) {
                /* Soft reset triggered by driver */
            }
            break;
        case offsetof(struct cdns2_usb_regs, lpmctrl):
            s->usb_regs.lpmctrl = val;
            break;
        case offsetof(struct cdns2_usb_regs, sleep_clkgate):
            s->usb_regs.sleep_clkgate = val;
            break;
        case offsetof(struct cdns2_interrupt_regs, usbien):
            s->interrupt_regs.usbien = val;
            break;
        case offsetof(struct cdns2_interrupt_regs, extien):
            s->interrupt_regs.extien = val;
            break;
        case offsetof(struct cdns2_interrupt_regs, usbirq):
            s->interrupt_regs.usbirq &= ~val; /* W1C */
            break;
        case offsetof(struct cdns2_interrupt_regs, extirq):
            s->interrupt_regs.extirq &= ~val; /* W1C */
            break;
        case offsetof(struct cdns2_usb_regs, usbcs):
            s->usb_regs.usbcs = val;
            break;
        case offsetof(struct cdns2_usb_regs, lpmclock):
            s->usb_regs.lpmclock = val;
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CDNS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_CDNS_USB );
    pci_config_set_class(pci_conf, PCI_CLASS_SERIAL_USB_DEVICE >> 8);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, PCI_CLASS_SERIAL_USB_DEVICE & 0xFF);
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
    s->bar_info[0].index = PCI_BAR_DEV;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "cdns2-bar0";  
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
    .name = "cdns2_pci_pci",
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
