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

#define TYPE_PCIBASE_DEVICE "exar_serial_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_ACCESSIO_COM_2S 0x1052
#define PCI_VENDOR_ID_ACCESSIO 0x494f
#define UART_EXAR_INT0 0x80
#define UART_EXAR_8XMODE 0x88
#define UART_EXAR_SLEEP 0x8b
#define UART_EXAR_DVID 0x8d
#define UART_EXAR_FCTR 0x08
#define UART_FCTR_EXAR_IRDA 0x10
#define UART_FCTR_EXAR_485 0x20
#define UART_FCTR_EXAR_TRGA 0x00
#define UART_FCTR_EXAR_TRGB 0x60
#define UART_FCTR_EXAR_TRGC 0x80
#define UART_FCTR_EXAR_TRGD 0xc0
#define UART_EXAR_TXTRG 0x0a
#define UART_EXAR_RXTRG 0x0b
#define UART_EXAR_MPIOINT_7_0 0x8f
#define UART_EXAR_MPIOLVL_7_0 0x90
#define UART_EXAR_MPIO3T_7_0 0x91
#define UART_EXAR_MPIOINV_7_0 0x92
#define UART_EXAR_MPIOSEL_7_0 0x93
#define UART_EXAR_MPIOOD_7_0 0x94
#define UART_EXAR_MPIOINT_15_8 0x95
#define UART_EXAR_MPIOLVL_15_8 0x96
#define UART_EXAR_MPIO3T_15_8 0x97
#define UART_EXAR_MPIOINV_15_8 0x98
#define UART_EXAR_MPIOSEL_15_8 0x99
#define UART_EXAR_MPIOOD_15_8 0x9a
#define UART_EXAR_RS485_DLY(x) ((x) << 4)
#define UART_EXAR_DLD 0x02
#define UART_EXAR_DLD_485_POLARITY 0x80
#define UART_EXAR_REGB 0x8e
#define UART_EXAR_REGB_EECK BIT(4)
#define UART_EXAR_REGB_EECS BIT(5)
#define UART_EXAR_REGB_EEDI BIT(6)
#define UART_EXAR_REGB_EEDO BIT(7)

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
    uint32_t intr_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t mmio_data[0x1000];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0 && addr < 0x1000) {
        if (size == 1) {
            val = s->mmio_data[addr];
        } else if (size == 2) {
            val = s->mmio_data[addr] | (s->mmio_data[addr+1] << 8);
        } else if (size == 4) {
            val = s->mmio_data[addr] | (s->mmio_data[addr+1] << 8) |
                  (s->mmio_data[addr+2] << 16) | (s->mmio_data[addr+3] << 24);
        } else if (size == 8) {
            val = (uint64_t)s->mmio_data[addr] | ((uint64_t)s->mmio_data[addr+1] << 8) |
                  ((uint64_t)s->mmio_data[addr+2] << 16) | ((uint64_t)s->mmio_data[addr+3] << 24) |
                  ((uint64_t)s->mmio_data[addr+4] << 32) | ((uint64_t)s->mmio_data[addr+5] << 40) |
                  ((uint64_t)s->mmio_data[addr+6] << 48) | ((uint64_t)s->mmio_data[addr+7] << 56);
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0 && addr < 0x1000) {
        if (size == 1) {
            s->mmio_data[addr] = (uint8_t)val;
        } else if (size == 2) {
            s->mmio_data[addr] = val & 0xff;
            s->mmio_data[addr+1] = (val >> 8) & 0xff;
        } else if (size == 4) {
            s->mmio_data[addr] = val & 0xff;
            s->mmio_data[addr+1] = (val >> 8) & 0xff;
            s->mmio_data[addr+2] = (val >> 16) & 0xff;
            s->mmio_data[addr+3] = (val >> 24) & 0xff;
        } else if (size == 8) {
            s->mmio_data[addr] = val & 0xff;
            s->mmio_data[addr+1] = (val >> 8) & 0xff;
            s->mmio_data[addr+2] = (val >> 16) & 0xff;
            s->mmio_data[addr+3] = (val >> 24) & 0xff;
            s->mmio_data[addr+4] = (val >> 32) & 0xff;
            s->mmio_data[addr+5] = (val >> 40) & 0xff;
            s->mmio_data[addr+6] = (val >> 48) & 0xff;
            s->mmio_data[addr+7] = (val >> 56) & 0xff;
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

    memset(s->mmio_data, 0, sizeof(s->mmio_data));
    /* Set DVID to 0x82 to identify as a 2-port XR17V352 */
    s->mmio_data[UART_EXAR_DVID] = 0x82;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ACCESSIO);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_ACCESSIO_COM_2S);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0700);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Enable MSI for interrupt allocation */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "exar-mmio";
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
    .name = "exar_serial_pci",
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
