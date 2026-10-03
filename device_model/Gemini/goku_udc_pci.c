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

#define TYPE_PCIBASE_DEVICE "goku_udc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_TOSHIBA 0x102f
#define PCI_DEVICE_ID_GOKU    0x0107

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
    struct goku_udc_regs {
        uint32_t int_status;
        uint32_t int_enable;
        uint32_t dma_master;
        uint32_t out_dma_start;
        uint32_t out_dma_end;
        uint32_t out_dma_current;
        uint32_t in_dma_start;
        uint32_t in_dma_end;
        uint32_t in_dma_current;
        uint32_t power_detect;
        uint8_t  _reserved0[0x1d8];
        uint32_t ep_fifo[4];
        uint8_t  _reserved1[0x10];
        uint32_t ep_mode[4];
        uint8_t  _reserved2[0x10];
        uint32_t ep_status[4];
        uint8_t  _reserved3[0x10];
        uint32_t EPxSizeLA[4];
        uint8_t  _reserved3a[0x10];
        uint32_t EPxSizeLB[4];
        uint8_t  _reserved3b[0x10];
        uint32_t EPxSizeHA[4];
        uint8_t  _reserved3c[0x10];
        uint32_t EPxSizeHB[4];
        uint8_t  _reserved4[0x30];
        uint32_t bRequestType;
        uint32_t bRequest;
        uint32_t wValueL;
        uint32_t wValueH;
        uint32_t wIndexL;
        uint32_t wIndexH;
        uint32_t wLengthL;
        uint32_t wLengthH;
        uint32_t SetupRecv;
        uint32_t CurrConfig;
        uint32_t StdRequest;
        uint32_t Request;
        uint32_t DataSet;
        uint8_t  _reserved5[4];
        uint32_t UsbState;
        uint32_t EOP;
        uint32_t Command;
        uint32_t EPxSingle;
        uint8_t  _reserved6[4];
        uint32_t EPxBCS;
        uint8_t  _reserved7[8];
        uint32_t IntControl;
        uint8_t  _reserved8[4];
        uint32_t reqmode;
        uint32_t ReqMode;
        uint8_t  _reserved9[0x18];
        uint32_t PortStatus;
        uint8_t  _reserved10[8];
        uint32_t address;
        uint32_t buff_test;
        uint8_t  _reserved11[4];
        uint32_t UsbReady;
        uint8_t  _reserved12[4];
        uint32_t SetDescStall;
        uint8_t  _reserved13[0x45c];
        uint32_t descriptors[0x80];
        uint8_t  _reserved14[0x600];
    } regs;
};

#define INT_SUSPEND 0x00001
#define INT_USBRESET 0x00002
#define INT_ENDPOINT0 0x00004
#define INT_SETUP 0x00008
#define INT_STATUS 0x00010
#define INT_STATUSNAK 0x00020
#define INT_EPxDATASET(n) (0x00020 << (n))
#define INT_EP1DATASET 0x00040
#define INT_EP2DATASET 0x00080
#define INT_EP3DATASET 0x00100
#define INT_EPnNAK(n) (0x00100 << (n))
#define INT_EP1NAK 0x00200
#define INT_EP2NAK 0x00400
#define INT_EP3NAK 0x00800
#define INT_SOF 0x01000
#define INT_ERR 0x02000
#define INT_MSTWRSET 0x04000
#define INT_MSTWREND 0x08000
#define INT_MSTWRTMOUT 0x10000
#define INT_MSTRDEND 0x20000
#define INT_SYSERROR 0x40000
#define INT_PWRDETECT 0x80000
#define INT_DEVWIDE (INT_PWRDETECT|INT_SYSERROR|INT_USBRESET|INT_SUSPEND)
#define INT_EP0 (INT_SETUP|INT_ENDPOINT0|INT_STATUSNAK)
#define MST_EOPB_DIS 0x0800
#define MST_EOPB_ENA 0x0400
#define MST_TIMEOUT_DIS 0x0200
#define MST_TIMEOUT_ENA 0x0100
#define MST_RD_EOPB 0x0080
#define MST_RD_RESET 0x0040
#define MST_WR_RESET 0x0020
#define MST_RD_ENA 0x0004
#define MST_WR_ENA 0x0002
#define MST_CONNECTION 0x0001
#define MST_R_BITS (MST_EOPB_DIS|MST_EOPB_ENA|MST_RD_ENA|MST_RD_RESET)
#define MST_W_BITS (MST_TIMEOUT_DIS|MST_TIMEOUT_ENA|MST_WR_ENA|MST_WR_RESET)
#define MST_RW_BITS (MST_R_BITS|MST_W_BITS|MST_CONNECTION)
#define UDC_MSTWR_ENDPOINT 1
#define UDC_MSTRD_ENDPOINT 2
#define PW_DETECT 0x04
#define PW_RESETB 0x02
#define PW_PULLUP 0x01
#define EPxSTATUS_TOGGLE 0x40
#define EPxSTATUS_SUSPEND 0x20
#define EPxSTATUS_EP_MASK (0x07<<2)
#define EPxSTATUS_EP_READY (0<<2)
#define EPxSTATUS_EP_DATAIN (1<<2)
#define EPxSTATUS_EP_FULL (2<<2)
#define EPxSTATUS_EP_TX_ERR (3<<2)
#define EPxSTATUS_EP_RX_ERR (4<<2)
#define EPxSTATUS_EP_BUSY (5<<2)
#define EPxSTATUS_EP_STALL (6<<2)
#define EPxSTATUS_EP_INVALID (7<<2)
#define EPxSTATUS_FIFO_DISABLE 0x02
#define EPxSTATUS_STAGE_ERROR 0x01
#define PACKET_ACTIVE (1<<7)
#define DATASIZE 0x7f
#define DATASET_A(epnum) (1<<(2*(epnum)))
#define DATASET_B(epnum) (2<<(2*(epnum)))
#define DATASET_AB(epnum) (3<<(2*(epnum)))
#define USBSTATE_CONFIGURED 0x04
#define USBSTATE_ADDRESSED 0x02
#define USBSTATE_DEFAULT 0x01
#define COMMAND_SETDATA0 2
#define COMMAND_RESET 3
#define COMMAND_STALL 4
#define COMMAND_INVALID 5
#define COMMAND_FIFO_DISABLE 7
#define COMMAND_FIFO_ENABLE 8
#define COMMAND_INIT_DESCRIPTOR 9
#define COMMAND_FIFO_CLEAR 10
#define COMMAND_STALL_CLEAR 11
#define COMMAND_EP(n) ((n) << 4)
#define ICONTROL_STATUSNAK 1
#define G_REQMODE_SET_INTF (1<<7)
#define G_REQMODE_GET_INTF (1<<6)
#define G_REQMODE_SET_CONF (1<<5)
#define G_REQMODE_GET_CONF (1<<4)
#define G_REQMODE_GET_DESC (1<<3)
#define G_REQMODE_SET_FEAT (1<<2)
#define G_REQMODE_CLEAR_FEAT (1<<1)
#define G_REQMODE_GET_STATUS (1<<0)
#define DESC_LEN 0x80

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->regs.int_status & s->regs.int_enable) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (!is_write) {
        uint32_t start = s->regs.in_dma_start;
        uint32_t end = s->regs.in_dma_end;
        uint32_t len = (end >= start) ? (end - start + 1) : 0;
        if (len > 0 && len <= 4096) {
            uint8_t buf[4096];
            pci_dma_read(pdev, start, buf, len);
        }
        s->regs.in_dma_current = end;
        s->regs.dma_master &= ~MST_RD_ENA;
        s->regs.int_status |= INT_MSTRDEND;
        pcibase_update_irq(s);
    } else {
        uint32_t start = s->regs.out_dma_start;
        uint32_t end = s->regs.out_dma_end;
        uint32_t len = (end >= start) ? (end - start + 1) : 0;
        if (len > 0 && len <= 4096) {
            uint8_t buf[4096] = {0};
            pci_dma_write(pdev, start, buf, len);
        }
        s->regs.out_dma_current = end;
        s->regs.dma_master &= ~MST_WR_ENA;
        s->regs.int_status |= INT_MSTWREND;
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size <= sizeof(struct goku_udc_regs)) {
        memcpy(&val, (uint8_t *)&s->regs + addr, size);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size > sizeof(struct goku_udc_regs)) {
        return;
    }

    if (addr == offsetof(struct goku_udc_regs, int_status) && size == 4) {
        s->regs.int_status &= ~val;
        pcibase_update_irq(s);
        return;
    }
    if (addr == offsetof(struct goku_udc_regs, int_enable) && size == 4) {
        s->regs.int_enable = val;
        pcibase_update_irq(s);
        return;
    }
    if (addr == offsetof(struct goku_udc_regs, power_detect) && size == 4) {
        s->regs.power_detect = val;
        if (val & PW_RESETB) {
            s->regs.power_detect |= PW_DETECT;
            s->regs.int_status |= INT_PWRDETECT;
            pcibase_update_irq(s);
        }
        return;
    }
    if (addr == offsetof(struct goku_udc_regs, dma_master) && size == 4) {
        s->regs.dma_master = val;
        if (val & MST_RD_ENA) {
            pcibase_do_dma(s, false);
        }
        if (val & MST_WR_ENA) {
            pcibase_do_dma(s, true);
        }
        return;
    }

    memcpy((uint8_t *)&s->regs + addr, &val, size);
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
    memset(&s->regs, 0, sizeof(s->regs));
    for (int i = 0; i < 4; i++) {
        s->regs.ep_status[i] = EPxSTATUS_EP_READY;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x102f );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0107 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c03 );
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0xfe);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = sizeof(struct goku_udc_regs);
    s->bar_info[0].name = "goku-mmio";

    /* BAR Initialization */
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
    .name = "goku_udc_pci",
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
