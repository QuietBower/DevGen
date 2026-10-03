/*
 * QEMU PCI device model for Digi Neo/Classic serial boards (jsm driver)
 * Combined Phase 1 structure and Phase 2 behavior.
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
/* linux/pci_ids.h is not available in QEMU build environment; provide minimal local defines */

#define PCI_VENDOR_ID_DIGI 0x114f
#define PCI_DEVICE_ID_NEO_2DB9 0x00c8

#define TYPE_PCIBASE_DEVICE "jsm_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Related config info from jsm driver */
#define JSM_DRIVER_NAME "jsm"
#define JSM_MINOR_START 0
#define NR_PORTS 32
#define PCI_DEVICE_ID_CLASSIC_8_422 0x00D1
#define PCI_DEVICE_ID_CLASSIC_4_422 0x00D0
#define PCIE_DEVICE_ID_NEO_4RJ45 0x00F2
#define PCIE_DEVICE_ID_NEO_4 0x00F1
#define PCIE_DEVICE_ID_NEO_8 0x00F0
#define PCIE_DEVICE_ID_NEO_8RJ45 0x00F3
#define PCI_DEVICE_ID_NEO_4 0x00B0
#define PCI_DEVICE_ID_CLASSIC_4 0x0028
#define PCI_DEVICE_ID_CLASSIC_8 0x0029
#define PCI_DEVICE_ID_NEO_2_422_485 0x00CE
#define PCI_DEVICE_ID_NEO_1_422_485 0x00CD
#define PCI_DEVICE_ID_NEO_1_422 0x00CC
#define MAXPORTS 8
#define MAXLINES 256
#define CH_FIFO_ENABLED 0x0200
#define CH_TX_FIFO_LWM 0x0800
#define UART_EXAR654_ENHANCED_REGISTER_SET 0xBF
#define UART_EXAR654_EFR_ECB 0x10
#define CH_TX_FIFO_EMPTY 0x0400
#define CH_BREAK_SENDING 0x1000
#define UART_CLASSIC_POLL_ADDR_OFFSET 0x40
#define CH_BAUD0 0x08000
#define CH_STOP 0x0002
#define UART_17158_TX_FIFOSIZE 64
#define UART_17158_TXRDY 0x3
#define UART_17158_RXRDY_TIMEOUT 0x2
#define UART_17158_MSR 0x4
#define UART_17158_POLL_ADDR_OFFSET 0x80
#define UART_17158_RX_LINE_STATUS 0x1
#define UART_IIR_RDI_TIMEOUT 0x0C
#define UART_EXAR654_IER_CTSDSR 0x80
#define UART_16654_FCR_TXTRIGGER_16 0x10
#define UART_EXAR654_IER_XOFF 0x20
#define UART_EXAR654_EFR_IXON 0x2
#define UART_16654_FCR_RXTRIGGER_16 0x40
#define UART_EXAR654_EFR_CTSDSR 0x80
#define UART_EXAR654_EFR_IXOFF 0x8
#define UART_EXAR654_IER_RTSDTR 0x40
#define UART_16654_FCR_RXTRIGGER_56 0x80
#define UART_EXAR654_EFR_RTSDTR 0x40
#define UART_17158_FCTR_TRGD 0xC0
#define UART_17158_IER_CTSDSR 0x80
#define UART_17158_EFR_ECB 0x10
#define UART_17158_EFR_CTSDSR 0x80
#define UART_17158_FCTR_RTS_4DELAY 0x01
#define UART_17158_EFR_IXON 0x2
#define UART_17158_FCTR_RTS_8DELAY 0x03
#define UART_17158_EFR_RTSDTR 0x40
#define UART_17158_IER_XOFF 0x20
#define UART_17158_EFR_IXOFF 0x8
#define UART_17158_IER_RTSDTR 0x40
#define UART_17158_RX_FIFO_DATA_ERROR 0x80
#define UART_17158_TX_AND_FIFO_CLR 0x40
#define RQUEUESIZE (RQUEUEMASK + 1)
#define EQUEUEMASK 0x1FFF
#define RQUEUEMASK 0x1FFF
#define UART_17158_IIR_HWFLOW_STATE_CHANGE 0x20
#define UART_17158_IIR_FIFO_ENABLED 0xC0
#define UART_17158_XON_DETECT 0x2
#define UART_17158_XOFF_DETECT 0x1
#define UART_17158_IIR_RDI_TIMEOUT 0x0C
#define UART_17158_IIR_XONXOFF 0x10
#define CH_RECEIVER_OFF 0x0040
#define MAX_STOPS_SENT 5
#define CH_STOPI 0x0004
#define CH_OPENING 0x0080
#define EQUEUESIZE RQUEUESIZE
#define CH_FCAR 0x0010
#define CH_CD 0x0008

/* From jsm_pci_tbl: first entry only */
#define JSM_PCI_VENDOR_ID PCI_VENDOR_ID_DIGI
#define JSM_PCI_DEVICE_ID PCI_DEVICE_ID_NEO_2DB9

#define JSM_PCI_CLASS_ID PCI_CLASS_COMMUNICATION_SERIAL

/* Simple BAR layout assumptions driven by jsm_driver.c usage:
 * - Classic boards access an I/O BAR 1 (for PLX config/irq at offset 0x4c)
 * - Classic and Neo boards map a MMIO BAR (ioremap on BAR4 or BAR0),
 *   but the provided code does not touch any registers in that space.
 *
 * We only need BAR1 I/O region so that outb() to iobase+0x4c succeeds.
 */

#define JSM_BAR_IO_INDEX 1
#define JSM_BAR_IO_SIZE  0x100

/* We emulate only one PLX-like interrupt control register at offset 0x4c. */
#define JSM_PLX_INTCSR_OFFSET 0x4c

/* Internal BAR description */
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
    uint8_t io_regs[JSM_BAR_IO_SIZE]; /* shadow for I/O BAR1 */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The provided jsm_driver.c snippet does not read/modify any
     * status/enable bits or handle interrupts by registers; it only
     * requests an IRQ line from PCI core. Without more register
     * documentation, we cannot model real interrupt behavior, so we
     * leave this empty.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* jsm_driver.c snippet does not show any DMA engine programming
     * (no pci_dma_xxx, no bus mastering registers), so we do not
     * implement device-initiated DMA here.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No MMIO registers are accessed in provided jsm_driver.c snippet.
     * To stay within inference boundaries, treat reads as returning 0.
     */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO registers used; writes are ignored. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only BAR1 I/O is modeled, with a shadow array. We only know that
     * driver writes to offset 0x4c; it never reads back in the provided
     * code, but we still return our shadowed byte/word/dword.
     */
    if (addr >= JSM_BAR_IO_SIZE) {
        return 0;
    }

    switch (size) {
    case 1:
        val = s->io_regs[addr];
        break;
    case 2:
        val = s->io_regs[addr];
        if (addr + 1 < JSM_BAR_IO_SIZE) {
            val |= ((uint16_t)s->io_regs[addr + 1]) << 8;
        }
        break;
    case 4:
        val = s->io_regs[addr];
        if (addr + 1 < JSM_BAR_IO_SIZE) {
            val |= ((uint32_t)s->io_regs[addr + 1]) << 8;
        }
        if (addr + 2 < JSM_BAR_IO_SIZE) {
            val |= ((uint32_t)s->io_regs[addr + 2]) << 16;
        }
        if (addr + 3 < JSM_BAR_IO_SIZE) {
            val |= ((uint32_t)s->io_regs[addr + 3]) << 24;
        }
        break;
    default:
        /* unsupported access size: return 0 within valid struct */
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= JSM_BAR_IO_SIZE) {
        return;
    }

    switch (size) {
    case 1:
        s->io_regs[addr] = (uint8_t)val;
        break;
    case 2:
        s->io_regs[addr] = (uint8_t)(val & 0xff);
        if (addr + 1 < JSM_BAR_IO_SIZE) {
            s->io_regs[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        }
        break;
    case 4:
        s->io_regs[addr] = (uint8_t)(val & 0xff);
        if (addr + 1 < JSM_BAR_IO_SIZE) {
            s->io_regs[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        }
        if (addr + 2 < JSM_BAR_IO_SIZE) {
            s->io_regs[addr + 2] = (uint8_t)((val >> 16) & 0xff);
        }
        if (addr + 3 < JSM_BAR_IO_SIZE) {
            s->io_regs[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        }
        break;
    default:
        /* ignore unsupported sizes */
        break;
    }

    /* If we had exact semantics for PLX INTCSR at 0x4c, we would
     * update IRQ routing here. The snippet only sets it to 0x43 or 0,
     * and never reads; there is no ISR-visible status described, so
     * we cannot legally infer further behavior.
     */
    if (addr == JSM_PLX_INTCSR_OFFSET && size == 1) {
        /* value is stored in shadow already; no further action. */
    }
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

    /* Clear shadow I/O registers on reset. */
    for (i = 0; i < JSM_BAR_IO_SIZE; i++) {
        s->io_regs[i] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  JSM_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  JSM_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, JSM_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Initialize BAR descriptors */
    s->num_bars = 0;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    /* Configure BAR1 as I/O space to satisfy outb() in driver. */
    s->bar_info[s->num_bars].index = JSM_BAR_IO_INDEX;
    s->bar_info[s->num_bars].type  = BAR_TYPE_PIO;
    s->bar_info[s->num_bars].size  = JSM_BAR_IO_SIZE;
    s->bar_info[s->num_bars].name  = "jsm-io";
    s->num_bars++;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize shadow I/O registers */
    memset(s->io_regs, 0, sizeof(s->io_regs));
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
    .name = "jsm_pci",
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
