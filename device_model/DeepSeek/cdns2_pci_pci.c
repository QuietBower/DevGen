/*
 * QEMU PCI device model for Cadence CDNS2 USB controller
 * Based on Linux driver: cdns2-pci.c
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
/* #HeadFile# */

#define TYPE_PCIBASE_DEVICE "cdns2_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor/Device IDs taken from cdns2-pci.c */
#define VENDOR_ID 0x17cd
#define DEVICE_ID 0x0120

/* Hardware register bit definitions from cdns2-pci.c */
#define CPUCTRL_SW_RST          BIT(1)
#define CPUCTRL_UPCLK           BIT(0)
#define CPUCTRL_WUEN            BIT(7)
#define EP_ENABLED              BIT(0)
#define EP_CLAIMED              BIT(4)
#define EP_STALL_PENDING        BIT(2)
#define EP_WEDGE                BIT(3)
#define EP_STALLED              BIT(1)
#define EP_DEFERRED_DRDY        BIT(6)
#define EP_RING_FULL            BIT(5)
#define LPMCLOCK_SLEEP_ENTRY    BIT(7)
#define USBCS_LPMNYET           BIT(2)
#define DMA_CONF_DMULT          BIT(9)
#define USB_IEN_INIT (USBIRQ_SUDAV | USBIRQ_SUSPEND | USBIRQ_URESET \
                      | USBIRQ_HSPEED | USBIRQ_LPM)
#define EXTIRQ_WAKEUP           BIT(7)
#define TRB_SIZE                (sizeof(struct cdns2_trb))
#define TRB_ISO_RESERVED        2
#define TRBS_PER_SEGMENT        600
#define USBIRQ_LPM              BIT(7)
#define USBIRQ_SUDAV            BIT(0)
#define USBIRQ_URESET           BIT(4)
#define USBIRQ_SUSPEND          BIT(3)
#define USBIRQ_HSPEED           BIT(5)
#define EP0_FIFO_AUTO           BIT(5)
#define DMA_EP_IEN_EP_IN0       BIT(16)
#define DMA_EP_IEN_EP_OUT0      BIT(0)
#define EP0_FIFO_IO_TX          BIT(4)
#define DMA_EP_CFG_ENABLE       BIT(0)
#define TRB_LINK                6
#define TRB_BUFFER(p)           ((p) & GENMASK(31, 0))
#define TRB_TOGGLE              BIT(1)
#define TRB_CYCLE               BIT(0)
#define TRB_TYPE(p)             ((p) << 10)
#define SPEEDCTRL_HSDISABLE     BIT(7)
#define ENDPRST_FIFORST         BIT(6)
#define ENDPRST_TOGRST          BIT(5)
#define ENDPRST_IO_TX           BIT(4)
#define DMA_EP_CMD_EPRST        BIT(0)
#define ISO_MAX_INTERVAL        8
#define DMA_EP_STS_CCS(p)       ((p) & BIT(11))
#define DMA_EP_STS_EN_TRBERREN  BIT(7)
#define DMA_EP_CMD_DFLUSH       BIT(7)
#define TRB_CHAIN               BIT(4)
#define DMA_EP_STS_DBUSY        BIT(9)
#define USBCS_SIGRSUME          BIT(5)
#define CDNS2_STATUS_STAGE      0x2
#define EPX_CON_TYPE_BULK       0x8
#define EPX_CON_ISOD_SHIFT      0x4
#define EPX_CON_TYPE_ISOC       0x4
#define DMA_EP_STS_ISOERR       BIT(15)
#define EPX_CON_VAL             BIT(7)
#define FIFOCTRL_FIFOAUTO       BIT(5)
#define EPX_CON_TYPE_INT        0xC
#define FIFOCTRL_IO_TX          BIT(4)
#define DMA_EP_STS_TRBERR       BIT(7)
#define DMA_EP_CMD_DRDY         BIT(6)
#define EPX_CON_STALL           BIT(6)
#define EP0CS_HSNAK             BIT(1)
#define CDNS2_SETUP_STAGE       0x0
#define EP0CS_CHGSET            BIT(7)
#define TRB_NORMAL              1
#define TRB_LEN(p)              ((p) & GENMASK(16, 0))
#define TRB_IOC                 BIT(5)
#define EP0CS_DSTALL            BIT(4)
#define CDNS2_DATA_STAGE        0x1
#define TRB_MAX_ISO_BUFF_SIZE   BIT(TRB_MAX_ISO_BUFF_SHIFT)
#define TRB_ISP                 BIT(2)
#define TRB_BURST(p)            (((p) << 24) & GENMASK(31, 24))
#define TRB_BUFF_LEN_UP_TO_BOUNDARY(addr) (TRB_MAX_ISO_BUFF_SIZE - \
                                        ((addr) & (TRB_MAX_ISO_BUFF_SIZE - 1)))
#define DMA_EP_STS_DESCMIS      BIT(4)
#define TRB_MAX_ISO_BUFF_SHIFT  12

/* Newly supplied offset definition */
#define CDNS2_ADMA_REGS_OFFSET  0x400

/* Helper macros from driver context */
#define CDNS2_IF_EP_EXIST(pdev, ep_num, dir) \
                         ((pdev)->eps_supported & \
                         (BIT(ep_num) << ((dir) ? 0 : 16)))
#define gadget_to_cdns2_device(g) (container_of(g, struct cdns2_device, gadget))
#define ep_to_cdns2_ep(ep) (container_of(ep, struct cdns2_endpoint, endpoint))
#define to_cdns2_request(r) (container_of(r, struct cdns2_request, request))

/* Hardware register structures from driver */
typedef uint32_t __le32;
typedef uint16_t __le16;
typedef uint8_t __u8;
typedef uint16_t __u16;

struct cdns2_trb {
    uint32_t buffer;
    uint32_t length;
    uint32_t control;
};

struct cdns2_epx_base {
    uint16_t rxbc;
    uint8_t rxcon;
    uint8_t rxcs;
    uint16_t txbc;
    uint8_t txcon;
    uint8_t txcs;
};

/* Bar info constants */
#define CDNS2_BAR0_SIZE 0x10000

/* Additional structures */
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

    /* Flat MMIO register space */
    uint8_t regs[CDNS2_BAR0_SIZE];
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    uint8_t usbirq = s->regs[0x18C];
    uint8_t usbien = s->regs[0x198];
    uint8_t extirq = s->regs[0x18D];
    uint8_t extien = s->regs[0x199];
    uint32_t ep_ien = ldl_le_p(&s->regs[0x43C]);
    uint32_t ep_ists = ldl_le_p(&s->regs[0x440]);
    int level = ((usbien & usbirq) || (extien & extirq) || (ep_ien & ep_ists));
    pci_set_irq(PCI_DEVICE(s), level ? 1 : 0);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        val = ldn_he_p(&s->regs[addr], size);
    } else {
        qemu_log_mask(LOG_UNIMP, "CDNS2: unimplemented read at 0x%" HWADDR_PRIx " size %d\n", addr, size);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        qemu_log_mask(LOG_UNIMP, "CDNS2: unimplemented write at 0x%" HWADDR_PRIx " size %d value 0x%" PRIx64 "\n", addr, size, val);
        return;
    }

    /* Handle W1C status registers */
    if (addr == 0x18C || addr == 0x18D) {
        s->regs[addr] &= ~((uint8_t)val);
        goto update_irq;
    } else if (addr == 0x42C || addr == 0x440) {
        uint32_t old = ldl_le_p(&s->regs[addr]);
        stl_le_p(&s->regs[addr], old & ~((uint32_t)val));
        goto update_irq;
    }

    /* Generic write */
    stn_he_p(&s->regs[addr], size, val);

update_irq:
    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used */
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

    /* Reset all registers to zero */
    memset(s->regs, 0, sizeof(s->regs));

    /* Set default values for certain registers */
    s->regs[960] = CPUCTRL_UPCLK;  /* enable PHY clock */
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
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_config_set_vendor_id(pci_conf, VENDOR_ID);
    pci_config_set_device_id(pci_conf, DEVICE_ID);
    /* 
     * Set class code: base class = 0x0C (serial), sub-class = 0x03 (USB),
     * programming interface = 0xFE (USB device controller).
     * Use PCI_CLASS_SERIAL_USB (0x0c03) for base/sub, and set prog-if separately.
     */
    pci_config_set_class(pci_conf, PCI_CLASS_SERIAL_USB);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0xFE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: only BAR0 used */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = CDNS2_BAR0_SIZE;
    s->bar_info[0].name = "cdns2_mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize register space */
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[960] = CPUCTRL_UPCLK;
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
