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

#define TYPE_PCIBASE_DEVICE "pch_udc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* Vendor and Device IDs */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x0939
#define CLASS_ID 0x0cfe

/* Register Offsets */
#define UDC_EPCTL_ADDR       0x00
#define UDC_EPSTS_ADDR       0x04
#define UDC_BUFIN_FRAMENUM_ADDR  0x08
#define UDC_BUFOUT_MAXPKT_ADDR   0x0C
#define UDC_SUBPTR_ADDR      0x10
#define UDC_DESPTR_ADDR      0x14
#define UDC_CONFIRM_ADDR     0x18
#define UDC_DEVCFG_ADDR      0x400
#define UDC_DEVCTL_ADDR      0x404
#define UDC_DEVSTS_ADDR      0x408
#define UDC_DEVIRQSTS_ADDR   0x40C
#define UDC_DEVIRQMSK_ADDR   0x410
#define UDC_EPIRQSTS_ADDR    0x414
#define UDC_EPIRQMSK_ADDR    0x418
#define UDC_DEVLPM_ADDR      0x41C
#define UDC_CSR_ADDR         0x500
#define UDC_CSR_BUSY_ADDR    0x4f0
#define UDC_SRST_ADDR        0x4fc

/* Endpoint Register Shift */
#define UDC_EP_REG_SHIFT     0x20

/* Number of Endpoints */
#define PCH_UDC_EP_NUM       32

/* Endpoint Control Bits */
#define UDC_EPCTL_RRDY       (1 << 9)
#define UDC_EPCTL_CNAK       (1 << 8)
#define UDC_EPCTL_SNAK       (1 << 7)
#define UDC_EPCTL_NAK        (1 << 6)
#define UDC_EPCTL_ET_MASK    0x00000030
#define UDC_EPCTL_ET_CONTROL 0
#define UDC_EPCTL_ET_ISO     1
#define UDC_EPCTL_ET_BULK    2
#define UDC_EPCTL_ET_INTERRUPT 3
#define UDC_EPCTL_P          (1 << 3)
#define UDC_EPCTL_F          (1 << 1)
#define UDC_EPCTL_S          (1 << 0)
#define UDC_EPSTS_XFERDONE   (1 << 27)
#define UDC_EPSTS_RSS        (1 << 26)
#define UDC_EPSTS_RCS        (1 << 25)
#define UDC_EPSTS_TXEMPTY    (1 << 24)
#define UDC_EPSTS_TDC        (1 << 10)
#define UDC_EPSTS_HE         (1 << 9)
#define UDC_EPSTS_MRXFIFO_EMP (1 << 8)
#define UDC_EPSTS_BNA        (1 << 7)
#define UDC_EPSTS_IN         (1 << 6)
#define UDC_EPSTS_OUT_MASK   0x00000030
#define UDC_EPSTS_OUT_DATA   1
#define UDC_EPSTS_OUT_SETUP  2
#define UDC_EPSTS_ALL_CLR_MASK 0x1F0006F0

/* Device Control Bits */
#define UDC_DEVCFG_CSR_PRG   (1 << 17)
#define UDC_DEVCFG_SP         (1 << 3)
#define UDC_DEVCFG_SPD_HS     0x0
#define UDC_DEVCFG_SPD_FS     0x1
#define UDC_DEVCFG_SPD_LS     0x2
#define UDC_DEVCTL_CSR_DONE   (1 << 13)
#define UDC_DEVCTL_SD         (1 << 10)
#define UDC_DEVCTL_MODE       (1 << 9)
#define UDC_DEVCTL_BREN       (1 << 8)
#define UDC_DEVCTL_THE        (1 << 7)
#define UDC_DEVCTL_DU         (1 << 4)
#define UDC_DEVCTL_TDE        (1 << 3)
#define UDC_DEVCTL_RDE        (1 << 2)
#define UDC_DEVCTL_RES        (1 << 0)

/* Device Status Bits */
#define UDC_DEVSTS_TS_MASK    0xfffc0000
#define UDC_DEVSTS_ENUM_SPEED_MASK 0x00006000
#define UDC_DEVSTS_ENUM_SPEED_FULL  1
#define UDC_DEVSTS_ENUM_SPEED_HIGH  0
#define UDC_DEVSTS_ALT_MASK   0x00000f00
#define UDC_DEVSTS_INTF_MASK  0x000000f0
#define UDC_DEVSTS_CFG_MASK   0x0000000f

/* Device Interrupt Bits */
#define UDC_DEVINT_ENUM       (1 << 6)
#define UDC_DEVINT_SOF        (1 << 5)
#define UDC_DEVINT_US         (1 << 4)
#define UDC_DEVINT_UR         (1 << 3)
#define UDC_DEVINT_ES         (1 << 2)
#define UDC_DEVINT_SI         (1 << 1)
#define UDC_DEVINT_SC         (1 << 0)
#define UDC_DEVINT_MSK        0x7f

/* Endpoint Interrupt Bits */
#define UDC_EPINT_IN_EP0      (1 << 0)
#define UDC_EPINT_OUT_EP0     (1 << 16)
#define UDC_EPINT_MSK_DISABLE_ALL 0xffffffff

/* CSR Bits */
#define UDC_CSR_BUSY          (1 << 0)
#define UDC_PSRST             (1 << 1)
#define UDC_SRST              (1 << 0)

/* DMA related */
#define PCH_UDC_BUFF_STS      0xC0000000
#define PCH_UDC_BS_HST_RDY    0x00000000
#define PCH_UDC_BS_DMA_BSY    0x40000000
#define PCH_UDC_BS_DMA_DONE   0x80000000
#define PCH_UDC_BS_HST_BSY    0xC0000000
#define PCH_UDC_RXTX_STS      0x30000000
#define PCH_UDC_RTS_SUCC      0x00000000
#define PCH_UDC_RTS_DESERR    0x10000000
#define PCH_UDC_RTS_BUFERR    0x30000000
#define PCH_UDC_DMA_LAST      0x08000000
#define PCH_UDC_RXTX_BYTES    0x0000ffff

/* DMA directions */
#define DMA_DIR_RX            1
#define DMA_DIR_TX            2

/* BAR definitions */
#define PCH_UDC_PCI_BAR_QUARK_X1000  0
#define PCH_UDC_PCI_BAR              1

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

struct usb_ctrlrequest {
    uint8_t bRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

/* Moved this definition before PCIBaseState to fix incomplete type error */
struct PchUdcEpRegs {
    uint32_t epctl;
    uint32_t epsts;
    uint32_t bufin_framenum;
    uint32_t bufout_maxpkt;
    uint32_t subptr;
    uint32_t desptr;
    uint32_t confirm;
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t irq_status;
    uint32_t irq_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t devcfg;
    uint32_t devctl;
    uint32_t devsts;
    uint32_t devirqsts;
    uint32_t devirqmsk;
    uint32_t epirqsts;
    uint32_t epirqmsk;
    uint32_t devlpm;
    uint32_t csr_busy;
    uint32_t srst;
    struct PchUdcEpRegs ep_regs[PCH_UDC_EP_NUM];
    uint32_t csr[PCH_UDC_EP_NUM];

    /* DMA Context */
    /* No separate DMA state; registers in ep_regs suffice */

    /* Operational status flags */
    /* No separate status; encoded in devsts/ep_regs */

    bool in_reset;

    uint8_t power_state;
};

struct pch_udc_data_dma_desc {
    uint32_t status;
    uint32_t reserved;
    uint32_t dataptr;
    uint32_t next;
};

struct pch_udc_stp_dma_desc {
    uint32_t status;
    uint32_t reserved;
    struct usb_ctrlrequest request;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    uint32_t dev_pending = s->devirqsts & ~s->devirqmsk;
    uint32_t ep_pending  = s->epirqsts  & ~s->epirqmsk;

    if (dev_pending || ep_pending) {
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

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* DMA not implemented in this emulation; no descriptor processing required for driver probe. */
}

/* Helper to obtain endpoint register pointer given address */
static struct PchUdcEpRegs *pcibase_ep_regs_by_addr(PCIBaseState *s, hwaddr addr)
{
    unsigned ep_num, reg_off, ep_index;

    if (addr < 0x200) {
        /* IN endpoint space */
        ep_num = addr >> 5;
        reg_off = addr & 0x1f;
        ep_index = ep_num * 2;          /* IN endpoints are even indices */
        if (ep_num >= 16 || ep_index >= PCH_UDC_EP_NUM)
            return NULL;
        return &s->ep_regs[ep_index];
    } else if (addr < 0x400) {
        /* OUT endpoint space */
        ep_num = (addr - 0x200) >> 5;
        reg_off = addr & 0x1f;
        ep_index = ep_num * 2 + 1;      /* OUT endpoints are odd indices */
        if (ep_num >= 16 || ep_index >= PCH_UDC_EP_NUM)
            return NULL;
        return &s->ep_regs[ep_index];
    }
    return NULL;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only 4-byte aligned accesses are expected per driver */
    if (size != 4 || (addr & 3)) {
        return ~0ULL;
    }

    if (addr < 0x400) {
        struct PchUdcEpRegs *ep = pcibase_ep_regs_by_addr(s, addr);
        if (!ep) {
            return ~0ULL;
        }
        switch (addr & 0x1f) {
        case 0x00: return ep->epctl;
        case 0x04: return ep->epsts;
        case 0x08: return ep->bufin_framenum;
        case 0x0C: return ep->bufout_maxpkt;
        case 0x10: return ep->subptr;
        case 0x14: return ep->desptr;
        case 0x18: return ep->confirm;
        default:   return ~0ULL;
        }
    }

    /* Global registers 0x400 .. */
    switch (addr) {
    case UDC_DEVCFG_ADDR:      return s->devcfg;
    case UDC_DEVCTL_ADDR:      return s->devctl;
    case UDC_DEVSTS_ADDR:      return s->devsts;
    case UDC_DEVIRQSTS_ADDR:   return s->devirqsts;
    case UDC_DEVIRQMSK_ADDR:   return s->devirqmsk;
    case UDC_EPIRQSTS_ADDR:    return s->epirqsts;
    case UDC_EPIRQMSK_ADDR:    return s->epirqmsk;
    case UDC_DEVLPM_ADDR:      return s->devlpm;
    case UDC_CSR_BUSY_ADDR:    return 0;      /* always idle */
    case UDC_SRST_ADDR:        return s->srst;
    }

    /* CSR registers per endpoint: 0x500 + ep*4 */
    if (addr >= UDC_CSR_ADDR && addr < UDC_CSR_ADDR + PCH_UDC_EP_NUM * 4) {
        unsigned ep = (addr - UDC_CSR_ADDR) / 4;
        return s->csr[ep];
    }

    return ~0ULL;
}

static void pcibase_device_reset(PCIBaseState *s)
{
    int i;
    for (i = 0; i < PCH_UDC_EP_NUM; i++) {
        memset(&s->ep_regs[i], 0, sizeof(s->ep_regs[i]));
        s->csr[i] = 0;
    }
    s->devcfg = 0;
    s->devctl = 0;
    s->devsts = 0;
    s->devirqsts = 0;
    s->devirqmsk = 0;
    s->epirqsts  = 0;
    s->epirqmsk  = 0;
    s->devlpm    = 0;
    s->srst      = 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Only 4-byte aligned accesses are expected */
    if (size != 4 || (addr & 3)) {
        return;
    }

    if (addr < 0x400) {
        struct PchUdcEpRegs *ep = pcibase_ep_regs_by_addr(s, addr);
        if (!ep) {
            return;
        }
        switch (addr & 0x1f) {
        case 0x00: ep->epctl = val; break;
        case 0x04:
            /* EPSTS is W1C: writing 1 clears bits */
            ep->epsts &= ~val;
            break;
        case 0x08: ep->bufin_framenum = val; break;
        case 0x0C: ep->bufout_maxpkt = val; break;
        case 0x10: ep->subptr = val;        break;
        case 0x14: ep->desptr = val;        break;
        case 0x18: ep->confirm = val;       break;
        default: break;
        }
        /* Endpoint writes do not directly affect global IRQs in this model */
        return;
    }

    /* Global registers */
    switch (addr) {
    case UDC_DEVCFG_ADDR:
        s->devcfg = val;
        break;
    case UDC_DEVCTL_ADDR:
        s->devctl = val;
        if (val & UDC_DEVCTL_RES) {
            pcibase_device_reset(s);
            s->devctl &= ~UDC_DEVCTL_RES; // clear the reset bit after reset
        }
        break;
    case UDC_DEVSTS_ADDR:
        s->devsts = val;  /* May be read-only in hw, but accept write for simplicity */
        break;
    case UDC_DEVIRQSTS_ADDR:
        /* W1C: clear bits that are set in val */
        s->devirqsts &= ~val;
        pcibase_update_irq(s);
        break;
    case UDC_DEVIRQMSK_ADDR:
        s->devirqmsk = val;
        pcibase_update_irq(s);
        break;
    case UDC_EPIRQSTS_ADDR:
        s->epirqsts &= ~val;
        pcibase_update_irq(s);
        break;
    case UDC_EPIRQMSK_ADDR:
        s->epirqmsk = val;
        pcibase_update_irq(s);
        break;
    case UDC_DEVLPM_ADDR:
        s->devlpm = val;
        break;
    case UDC_CSR_BUSY_ADDR:
        /* Ignored -- writes to busy register are no-ops */
        break;
    case UDC_SRST_ADDR:
        s->srst = val;
        if (val & UDC_SRST) {
            pcibase_device_reset(s);
        }
        break;
    default:
        if (addr >= UDC_CSR_ADDR && addr < UDC_CSR_ADDR + PCH_UDC_EP_NUM * 4) {
            unsigned ep = (addr - UDC_CSR_ADDR) / 4;
            s->csr[ep] = val;
        }
        break;
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
    pcibase_device_reset(s);
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

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "bar0";
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "bar1";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Enable MSI; driver calls pci_enable_msi() */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    /* Final state initialization before the device is 'live' */
    /* No special init required beyond zeroed registers */
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

    /* Free buffers, stop timers, etc. -- nothing to clean up */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pch_udc_pci",
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
