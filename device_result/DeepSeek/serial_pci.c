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

#define PCI_VENDOR_ID_ADVANTECH 0x13fe
#define PCI_DEVICE_ID_ADVANTECH_PCI1600 0x1600

#define FL_BASE_BARS        0x0008
#define FL_NOIRQ            0x0080

#define TYPE_PCIBASE_DEVICE "serial_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

    /* UART registers for up to 4 ports */
    struct {
        uint8_t rbr; /* receiver buffer (read) */
        uint8_t thr; /* transmitter holding (write) */
        uint8_t ier; /* interrupt enable */
        uint8_t iir; /* interrupt identification (read) */
        uint8_t fcr; /* FIFO control */
        uint8_t lcr; /* line control */
        uint8_t mcr; /* modem control */
        uint8_t lsr; /* line status */
        uint8_t msr; /* modem status */
        uint8_t scr; /* scratch */
        uint8_t dll; /* divisor latch LSB */
        uint8_t dlm; /* divisor latch MSB */
    } uart[4];
};

/* PIO Handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    int port = (addr / 8) % 4;
    int reg = addr & 7;
    uint64_t val = 0;

    switch (reg) {
    case 0: /* RBR (DLAB=0) or DLL (DLAB=1) */
        if (s->uart[port].lcr & 0x80) {
            val = s->uart[port].dll;
        } else {
            val = s->uart[port].rbr;
            /* Reading RBR clears DR and any pending RX interrupt */
            s->uart[port].lsr &= ~0x01; /* clear Data Ready */
            s->uart[port].iir = 1; /* no interrupt pending */
        }
        break;
    case 1: /* IER (DLAB=0) or DLM (DLAB=1) */
        if (s->uart[port].lcr & 0x80) {
            val = s->uart[port].dlm;
        } else {
            val = s->uart[port].ier;
        }
        break;
    case 2: /* IIR (read only) */
        val = s->uart[port].iir;
        break;
    case 3: /* LCR */
        val = s->uart[port].lcr;
        break;
    case 4: /* MCR */
        val = s->uart[port].mcr;
        break;
    case 5: /* LSR */
        val = s->uart[port].lsr;
        break;
    case 6: /* MSR */
        val = s->uart[port].msr;
        break;
    case 7: /* SCR */
        val = s->uart[port].scr;
        break;
    default:
        val = 0xff;
        break;
    }

    /* return value as zero-extended to the requested size */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port = (addr / 8) % 4;
    int reg = addr & 7;
    uint8_t byte = val & 0xff;

    switch (reg) {
    case 0: /* THR (DLAB=0) or DLL (DLAB=1) */
        if (s->uart[port].lcr & 0x80) {
            s->uart[port].dll = byte;
        } else {
            s->uart[port].thr = byte;
            /* In loopback mode, echo to receiver */
            if (s->uart[port].mcr & 0x10) { /* LOOP bit */
                s->uart[port].rbr = byte;
                s->uart[port].lsr |= 0x01; /* set Data Ready */
                /* Signal receive interrupt if enabled */
                if (s->uart[port].ier & 0x01) {
                    s->uart[port].iir = 0x04; /* Received Data Available */
                }
            }
            /* THRE and TEMT remain high; clear THRE briefly? For simplicity, keep high */
            /* In real hardware, THRE would go low and then high after byte transferred. */
            /* We keep THRE high always, which may cause no THRE interrupt. That's fine for probe. */
        }
        break;
    case 1: /* IER (DLAB=0) or DLM (DLAB=1) */
        if (s->uart[port].lcr & 0x80) {
            s->uart[port].dlm = byte;
        } else {
            s->uart[port].ier = byte & 0x0f; /* only lower 4 bits used */
        }
        break;
    case 2: /* FCR (write only) */
        s->uart[port].fcr = byte;
        /* Writing FCR enables FIFO if bit 0 is set */
        break;
    case 3: /* LCR */
        s->uart[port].lcr = byte;
        break;
    case 4: /* MCR */
        s->uart[port].mcr = byte & 0x1f; /* only lower 5 bits */
        break;
    case 5: /* LSR (some bits are read-only, writes ignored) */
        break;
    case 6: /* MSR (writes ignored) */
        break;
    case 7: /* SCR */
        s->uart[port].scr = byte;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* MMIO handlers not used for this device (PIO only) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    /* Initialize UART registers to default state */
    for (int i = 0; i < 4; i++) {
        s->uart[i].rbr = 0;
        s->uart[i].thr = 0;
        s->uart[i].ier = 0;
        s->uart[i].iir = 0x01; /* no interrupt pending */
        s->uart[i].fcr = 0;
        s->uart[i].lcr = 0;
        s->uart[i].mcr = 0;
        s->uart[i].lsr = 0x60; /* THRE and TEMT */
        s->uart[i].msr = 0xB0; /* CTS, DSR, DCD, RI high (typical) */
        s->uart[i].scr = 0;
        s->uart[i].dll = 0;
        s->uart[i].dlm = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ADVANTECH );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ADVANTECH_PCI1600 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0700 ); /* Communication: Serial controller */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR Initialization: PIO BAR0 with 4 UARTs (each 8 bytes) */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = 32,
        .name = "serial-io"
    };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI support */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        /* msi_init may fail; continue with legacy INTX */
        s->has_msi = false;
        s->has_msix = false;
    } else {
        s->has_msi = true;
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
    .name = "serial_pci",
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
