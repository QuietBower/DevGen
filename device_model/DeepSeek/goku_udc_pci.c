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
#define VENDOR_ID 0x102f
#define DEVICE_ID 0x0107
#define CLASS_ID  0x0c0310

/* Register offsets */
#define REG_INT_STATUS       0x000
#define REG_INT_ENABLE       0x004
#define REG_DMA_MASTER       0x008
#define REG_OUT_DMA_START    0x00c
#define REG_OUT_DMA_END      0x010
#define REG_OUT_DMA_CURRENT  0x014
#define REG_IN_DMA_START     0x018
#define REG_IN_DMA_END       0x01c
#define REG_IN_DMA_CURRENT   0x020
#define REG_POWER_DETECT     0x024
#define REG_EP_FIFO_BASE     0x200
#define REG_EP_FIFO(n)       (REG_EP_FIFO_BASE + (n)*4)
#define REG_EP_MODE_BASE     0x220
#define REG_EP_MODE(n)        (REG_EP_MODE_BASE + (n)*4)
#define REG_EP_STATUS_BASE   0x240
#define REG_EP_STATUS(n)      (REG_EP_STATUS_BASE + (n)*4)
#define REG_EPxSizeLA_BASE   0x260
#define REG_EPxSizeLA(n)      (REG_EPxSizeLA_BASE + (n)*4)
#define REG_EPxSizeLB_BASE   0x280
#define REG_EPxSizeLB(n)      (REG_EPxSizeLB_BASE + (n)*4)
#define REG_EPxSizeHA_BASE   0x2a0
#define REG_EPxSizeHA(n)      (REG_EPxSizeHA_BASE + (n)*4)
#define REG_EPxSizeHB_BASE   0x2c0
#define REG_EPxSizeHB(n)      (REG_EPxSizeHB_BASE + (n)*4)
#define REG_BREQUESTTYPE     0x300
#define REG_BREQUEST         0x304
#define REG_WVALUEL          0x308
#define REG_WVALUEH          0x30c
#define REG_WINDEXL          0x310
#define REG_WINDEXH          0x314
#define REG_WLENGTHL         0x318
#define REG_WLENGTHH         0x31c
#define REG_SETUPRECV        0x320
#define REG_CURRCONFIG       0x324
#define REG_STDREQUEST       0x328
#define REG_REQUEST          0x32c
#define REG_DATASET          0x330
#define REG_USBSTATE          0x338
#define REG_EOP               0x33c
#define REG_COMMAND           0x340
#define REG_EPXSINGLE         0x344
#define REG_EPXBCS            0x34c
#define REG_INTCONTROL        0x358
#define REG_REQMODE           0x360
#define REG_REQMODE2          0x364
#define REG_PORTSTATUS        0x380
#define REG_ADDRESS           0x38c
#define REG_BUFF_TEST         0x390
#define REG_USBREADY          0x398
#define REG_SETDESCSTALL      0x3a0
#define REG_DESCRIPTORS_BASE  0x800
#define REG_DESCRIPTORS(n)    (REG_DESCRIPTORS_BASE + (n)*4)

/* Interrupt bits */
#define INT_SUSPEND           0x00001
#define INT_USBRESET          0x00002
#define INT_ENDPOINT0         0x00004
#define INT_SETUP             0x00008
#define INT_STATUS            0x00010
#define INT_STATUSNAK         0x00020
#define INT_EP1DATASET        0x00040
#define INT_EP2DATASET        0x00080
#define INT_EP3DATASET        0x00100
#define INT_EP1NAK            0x00200
#define INT_EP2NAK            0x00400
#define INT_EP3NAK            0x00800
#define INT_SOF               0x01000
#define INT_ERR               0x02000
#define INT_MSTWRSET          0x04000
#define INT_MSTWREND          0x08000
#define INT_MSTWRTMOUT        0x10000
#define INT_MSTRDEND          0x20000
#define INT_SYSERROR          0x40000
#define INT_PWRDETECT         0x80000

#define INT_DEVWIDE \
        (INT_PWRDETECT | INT_SYSERROR | INT_USBRESET | INT_SUSPEND)
#define INT_EP0 \
        (INT_SETUP | INT_ENDPOINT0 | INT_STATUSNAK)

/* DMA master control bits */
#define MST_EOPB_DIS          0x0800
#define MST_EOPB_ENA          0x0400
#define MST_TIMEOUT_DIS       0x0200
#define MST_TIMEOUT_ENA       0x0100
#define MST_RD_EOPB           0x0080
#define MST_RD_RESET          0x0040
#define MST_WR_RESET          0x0020
#define MST_RD_ENA            0x0004
#define MST_WR_ENA            0x0002
#define MST_CONNECTION        0x0001

#define MST_R_BITS            (MST_EOPB_DIS | MST_EOPB_ENA | MST_RD_ENA | MST_RD_RESET)
#define MST_W_BITS            (MST_TIMEOUT_DIS | MST_TIMEOUT_ENA | MST_WR_ENA | MST_WR_RESET)
#define MST_RW_BITS           (MST_R_BITS | MST_W_BITS | MST_CONNECTION)

/* Power detect bits */
#define PW_DETECT  0x04
#define PW_RESETB  0x02
#define PW_PULLUP  0x01

/* Endpoint status bits */
#define EPxSTATUS_TOGGLE        0x40
#define EPxSTATUS_SUSPEND       0x20
#define EPxSTATUS_EP_MASK       (0x07 << 2)
#define EPxSTATUS_EP_READY      (0 << 2)
#define EPxSTATUS_EP_DATAIN     (1 << 2)
#define EPxSTATUS_EP_FULL       (2 << 2)
#define EPxSTATUS_EP_TX_ERR     (3 << 2)
#define EPxSTATUS_EP_RX_ERR     (4 << 2)
#define EPxSTATUS_EP_BUSY       (5 << 2)
#define EPxSTATUS_EP_STALL      (6 << 2)
#define EPxSTATUS_EP_INVALID    (7 << 2)
#define EPxSTATUS_FIFO_DISABLE  0x02
#define EPxSTATUS_STAGE_ERROR   0x01

/* Packet size bits */
#define PACKET_ACTIVE           (1 << 7)
#define DATASIZE                0x7f

/* DataSet bits */
#define DATASET_A(epnum)        (1 << (2 * (epnum)))
#define DATASET_B(epnum)        (2 << (2 * (epnum)))
#define DATASET_AB(epnum)       (3 << (2 * (epnum)))

/* Command values */
#define COMMAND_SETDATA0        2
#define COMMAND_RESET           3
#define COMMAND_STALL           4
#define COMMAND_INVALID         5
#define COMMAND_FIFO_DISABLE    7
#define COMMAND_FIFO_ENABLE     8
#define COMMAND_INIT_DESCRIPTOR 9
#define COMMAND_FIFO_CLEAR      10
#define COMMAND_STALL_CLEAR     11
#define COMMAND_EP(n)           ((n) << 4)

/* USB State bits */
#define USBSTATE_CONFIGURED     0x04
#define USBSTATE_ADDRESSED      0x02
#define USBSTATE_DEFAULT        0x01

/* IntControl bit */
#define ICONTROL_STATUSNAK      1

/* reqmode bits */
#define G_REQMODE_SET_INTF      (1 << 7)
#define G_REQMODE_GET_INTF      (1 << 6)
#define G_REQMODE_SET_CONF      (1 << 5)
#define G_REQMODE_GET_CONF      (1 << 4)
#define G_REQMODE_GET_DESC      (1 << 3)
#define G_REQMODE_SET_FEAT      (1 << 2)
#define G_REQMODE_CLEAR_FEAT    (1 << 1)
#define G_REQMODE_GET_STATUS    (1 << 0)

#define DESC_LEN                0x80

/* DMA endpoint identifiers provided by driver */
#define UDC_MSTRD_ENDPOINT        2
#define UDC_MSTWR_ENDPOINT        1

/* Register structure matching hardware layout */
struct goku_regs {
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
    uint8_t res0[0x1d8];
    uint32_t ep_fifo[4];
    uint8_t res1[0x10];
    uint32_t ep_mode[4];
    uint8_t res2[0x10];
    uint32_t ep_status[4];
    uint8_t res3[0x10];
    uint32_t EPxSizeLA[4];
    uint8_t res3a[0x10];
    uint32_t EPxSizeLB[4];
    uint8_t res3b[0x10];
    uint32_t EPxSizeHA[4];
    uint8_t res3c[0x10];
    uint32_t EPxSizeHB[4];
    uint8_t res4[0x30];
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
    uint8_t res5[4];
    uint32_t UsbState;
    uint32_t EOP;
    uint32_t Command;
    uint32_t EPxSingle;
    uint8_t res6[4];
    uint32_t EPxBCS;
    uint8_t res7[8];
    uint32_t IntControl;
    uint8_t res8[4];
    uint32_t reqmode;
    uint32_t ReqMode;
    uint8_t res9[0x18];
    uint32_t PortStatus;
    uint8_t res10[8];
    uint32_t address;
    uint32_t buff_test;
    uint8_t res11[4];
    uint32_t UsbReady;
    uint8_t res12[4];
    uint32_t SetDescStall;
    uint8_t res13[0x45c];
    uint32_t descriptors[DESC_LEN];
    uint8_t res14[0x600];
} QEMU_PACKED;

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
    struct goku_regs regs;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->regs.int_status & s->regs.int_enable;
    pci_set_irq(pdev, !!pending);
}

/* Device-initiated DMA logic based on driver access patterns */
/* DMA master transfer is not yet modeled; endpoint IDs are defined per driver. */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No-op: DMA transfers are not implemented because the driver code
     * does not detail the descriptor format or initiation sequence.
     * Endpoint numbers: UDC_MSTRD_ENDPOINT (2) for reads,
     *                  UDC_MSTWR_ENDPOINT (1) for writes.
     */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        return -1;
    }

    /* Map address to appropriate register */
    switch (addr) {
    case REG_INT_STATUS:
        val = s->regs.int_status;
        break;
    case REG_INT_ENABLE:
        val = s->regs.int_enable;
        break;
    case REG_DMA_MASTER:
        val = s->regs.dma_master;
        break;
    case REG_OUT_DMA_START:
        val = s->regs.out_dma_start;
        break;
    case REG_OUT_DMA_END:
        val = s->regs.out_dma_end;
        break;
    case REG_OUT_DMA_CURRENT:
        val = s->regs.out_dma_current;
        break;
    case REG_IN_DMA_START:
        val = s->regs.in_dma_start;
        break;
    case REG_IN_DMA_END:
        val = s->regs.in_dma_end;
        break;
    case REG_IN_DMA_CURRENT:
        val = s->regs.in_dma_current;
        break;
    case REG_POWER_DETECT:
        val = s->regs.power_detect;
        break;
    case REG_EP_FIFO(0):
    case REG_EP_FIFO(1):
    case REG_EP_FIFO(2):
    case REG_EP_FIFO(3):
        /* In real hardware, reading FIFO consumes a byte; we return 0 */
        val = 0;
        break;
    case REG_EP_MODE(0):
    case REG_EP_MODE(1):
    case REG_EP_MODE(2):
    case REG_EP_MODE(3):
        val = s->regs.ep_mode[(addr - REG_EP_MODE_BASE) / 4];
        break;
    case REG_EP_STATUS(0):
    case REG_EP_STATUS(1):
    case REG_EP_STATUS(2):
    case REG_EP_STATUS(3):
        val = s->regs.ep_status[(addr - REG_EP_STATUS_BASE) / 4];
        break;
    case REG_EPxSizeLA(0):
    case REG_EPxSizeLA(1):
    case REG_EPxSizeLA(2):
    case REG_EPxSizeLA(3):
        val = s->regs.EPxSizeLA[(addr - REG_EPxSizeLA_BASE) / 4];
        break;
    case REG_EPxSizeLB(0):
    case REG_EPxSizeLB(1):
    case REG_EPxSizeLB(2):
    case REG_EPxSizeLB(3):
        val = s->regs.EPxSizeLB[(addr - REG_EPxSizeLB_BASE) / 4];
        break;
    case REG_EPxSizeHA(0):
    case REG_EPxSizeHA(1):
    case REG_EPxSizeHA(2):
    case REG_EPxSizeHA(3):
        val = s->regs.EPxSizeHA[(addr - REG_EPxSizeHA_BASE) / 4];
        break;
    case REG_EPxSizeHB(0):
    case REG_EPxSizeHB(1):
    case REG_EPxSizeHB(2):
    case REG_EPxSizeHB(3):
        val = s->regs.EPxSizeHB[(addr - REG_EPxSizeHB_BASE) / 4];
        break;
    case REG_BREQUESTTYPE:
        val = s->regs.bRequestType;
        break;
    case REG_BREQUEST:
        val = s->regs.bRequest;
        break;
    case REG_WVALUEL:
        val = s->regs.wValueL;
        break;
    case REG_WVALUEH:
        val = s->regs.wValueH;
        break;
    case REG_WINDEXL:
        val = s->regs.wIndexL;
        break;
    case REG_WINDEXH:
        val = s->regs.wIndexH;
        break;
    case REG_WLENGTHL:
        val = s->regs.wLengthL;
        break;
    case REG_WLENGTHH:
        val = s->regs.wLengthH;
        break;
    case REG_SETUPRECV:
        val = s->regs.SetupRecv;
        break;
    case REG_CURRCONFIG:
        val = s->regs.CurrConfig;
        break;
    case REG_STDREQUEST:
        val = s->regs.StdRequest;
        break;
    case REG_REQUEST:
        val = s->regs.Request;
        break;
    case REG_DATASET:
        val = s->regs.DataSet;
        break;
    case REG_USBSTATE:
        val = s->regs.UsbState;
        break;
    case REG_EOP:
        val = s->regs.EOP;
        break;
    case REG_COMMAND:
        val = s->regs.Command;
        break;
    case REG_EPXSINGLE:
        val = s->regs.EPxSingle;
        break;
    case REG_EPXBCS:
        val = s->regs.EPxBCS;
        break;
    case REG_INTCONTROL:
        val = s->regs.IntControl;
        break;
    case REG_REQMODE:
        val = s->regs.reqmode;
        break;
    case REG_REQMODE2:
        val = s->regs.ReqMode;
        break;
    case REG_PORTSTATUS:
        val = s->regs.PortStatus;
        break;
    case REG_ADDRESS:
        val = s->regs.address;
        break;
    case REG_BUFF_TEST:
        val = s->regs.buff_test;
        break;
    case REG_USBREADY:
        val = s->regs.UsbReady;
        break;
    case REG_SETDESCSTALL:
        val = s->regs.SetDescStall;
        break;
    default:
        if (addr >= REG_DESCRIPTORS_BASE && addr < REG_DESCRIPTORS_BASE + DESC_LEN * 4) {
            unsigned idx = (addr - REG_DESCRIPTORS_BASE) / 4;
            val = s->regs.descriptors[idx];
        } else {
            val = 0;
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case REG_INT_STATUS:
        /* Write 0 to clear (new = old & val) */
        s->regs.int_status &= (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case REG_INT_ENABLE:
        s->regs.int_enable = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case REG_DMA_MASTER:
        s->regs.dma_master = (uint32_t)val;
        /* DMA engine start could be triggered here, but not implemented */
        break;
    case REG_OUT_DMA_START:
        s->regs.out_dma_start = (uint32_t)val;
        break;
    case REG_OUT_DMA_END:
        s->regs.out_dma_end = (uint32_t)val;
        break;
    case REG_OUT_DMA_CURRENT:
        s->regs.out_dma_current = (uint32_t)val;
        break;
    case REG_IN_DMA_START:
        s->regs.in_dma_start = (uint32_t)val;
        break;
    case REG_IN_DMA_END:
        s->regs.in_dma_end = (uint32_t)val;
        break;
    case REG_IN_DMA_CURRENT:
        s->regs.in_dma_current = (uint32_t)val;
        break;
    case REG_POWER_DETECT:
        s->regs.power_detect = (uint32_t)val;
        /* Could trigger INT_PWRDETECT if PW_DETECT changes */
        break;
    case REG_EP_FIFO(0):
    case REG_EP_FIFO(1):
    case REG_EP_FIFO(2):
    case REG_EP_FIFO(3):
        /* Write to FIFO: in real hardware, adds a byte to the buffer */
        /* We just ignore for now */
        break;
    case REG_EP_MODE(0):
    case REG_EP_MODE(1):
    case REG_EP_MODE(2):
    case REG_EP_MODE(3):
        s->regs.ep_mode[(addr - REG_EP_MODE_BASE) / 4] = (uint32_t)val;
        break;
    case REG_EP_STATUS(0):
    case REG_EP_STATUS(1):
    case REG_EP_STATUS(2):
    case REG_EP_STATUS(3):
        s->regs.ep_status[(addr - REG_EP_STATUS_BASE) / 4] = (uint32_t)val;
        break;
    case REG_EPxSizeLA(0):
    case REG_EPxSizeLA(1):
    case REG_EPxSizeLA(2):
    case REG_EPxSizeLA(3):
        s->regs.EPxSizeLA[(addr - REG_EPxSizeLA_BASE) / 4] = (uint32_t)val;
        break;
    case REG_EPxSizeLB(0):
    case REG_EPxSizeLB(1):
    case REG_EPxSizeLB(2):
    case REG_EPxSizeLB(3):
        s->regs.EPxSizeLB[(addr - REG_EPxSizeLB_BASE) / 4] = (uint32_t)val;
        break;
    case REG_EPxSizeHA(0):
    case REG_EPxSizeHA(1):
    case REG_EPxSizeHA(2):
    case REG_EPxSizeHA(3):
        s->regs.EPxSizeHA[(addr - REG_EPxSizeHA_BASE) / 4] = (uint32_t)val;
        break;
    case REG_EPxSizeHB(0):
    case REG_EPxSizeHB(1):
    case REG_EPxSizeHB(2):
    case REG_EPxSizeHB(3):
        s->regs.EPxSizeHB[(addr - REG_EPxSizeHB_BASE) / 4] = (uint32_t)val;
        break;
    case REG_BREQUESTTYPE:
        s->regs.bRequestType = (uint32_t)val;
        break;
    case REG_BREQUEST:
        s->regs.bRequest = (uint32_t)val;
        break;
    case REG_WVALUEL:
        s->regs.wValueL = (uint32_t)val;
        break;
    case REG_WVALUEH:
        s->regs.wValueH = (uint32_t)val;
        break;
    case REG_WINDEXL:
        s->regs.wIndexL = (uint32_t)val;
        break;
    case REG_WINDEXH:
        s->regs.wIndexH = (uint32_t)val;
        break;
    case REG_WLENGTHL:
        s->regs.wLengthL = (uint32_t)val;
        break;
    case REG_WLENGTHH:
        s->regs.wLengthH = (uint32_t)val;
        break;
    case REG_SETUPRECV:
        s->regs.SetupRecv = (uint32_t)val;
        break;
    case REG_CURRCONFIG:
        s->regs.CurrConfig = (uint32_t)val;
        break;
    case REG_STDREQUEST:
        s->regs.StdRequest = (uint32_t)val;
        break;
    case REG_REQUEST:
        s->regs.Request = (uint32_t)val;
        break;
    case REG_DATASET:
        s->regs.DataSet = (uint32_t)val;
        break;
    case REG_USBSTATE:
        s->regs.UsbState = (uint32_t)val;
        break;
    case REG_EOP:
        s->regs.EOP = (uint32_t)val;
        break;
    case REG_COMMAND:
        s->regs.Command = (uint32_t)val;
        /* Command handling could be added here */
        break;
    case REG_EPXSINGLE:
        s->regs.EPxSingle = (uint32_t)val;
        break;
    case REG_EPXBCS:
        s->regs.EPxBCS = (uint32_t)val;
        break;
    case REG_INTCONTROL:
        s->regs.IntControl = (uint32_t)val;
        break;
    case REG_REQMODE:
        s->regs.reqmode = (uint32_t)val;
        break;
    case REG_REQMODE2:
        s->regs.ReqMode = (uint32_t)val;
        break;
    case REG_PORTSTATUS:
        s->regs.PortStatus = (uint32_t)val;
        break;
    case REG_ADDRESS:
        s->regs.address = (uint32_t)val;
        break;
    case REG_BUFF_TEST:
        s->regs.buff_test = (uint32_t)val;
        break;
    case REG_USBREADY:
        s->regs.UsbReady = (uint32_t)val;
        break;
    case REG_SETDESCSTALL:
        s->regs.SetDescStall = (uint32_t)val;
        break;
    default:
        if (addr >= REG_DESCRIPTORS_BASE && addr < REG_DESCRIPTORS_BASE + DESC_LEN * 4) {
            unsigned idx = (addr - REG_DESCRIPTORS_BASE) / 4;
            s->regs.descriptors[idx] = (uint32_t)val;
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used */
    return ~0ull;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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
    /* After reset, power_detect should eventually show PW_RESETB, but driver sets it */
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
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c0310 );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "goku-mmio";
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
