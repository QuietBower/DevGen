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

/* Additional include files retrieved from driver context */


#define TYPE_PCIBASE_DEVICE "pci1xxxx_serial_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_EFAR		0x1055
#define PCI_DEVICE_ID_EFAR_PCI11010		0xa012
#define UART_SYSTEM_ADDR_BASE			0x1000
#define UART_DEV_REV_REG			(UART_SYSTEM_ADDR_BASE + 0x00)
#define UART_SYSLOCK_REG			(UART_SYSTEM_ADDR_BASE + 0xA0)
#define UART_RX_BYTE_FIFO			0x00
#define UART_TX_BYTE_FIFO			0x00
#define UART_FIFO_CTL				0x02
#define UART_MODEM_CTL_REG			0x04
#define UART_LINE_STAT_REG			0x05
#define UART_ACTV_REG				0x11
#define UART_PCI_CTRL_REG			0x80
#define ADCL_CFG_REG				0x40
#define UART_WAKE_REG				0x8C
#define UART_WAKE_MASK_REG			0x90
#define UART_BAUD_CLK_DIVISOR_REG		0x54
#define FRAC_DIV_CFG_REG			0x58
#define UART_RESET_REG				0x94
#define UART_BURST_STATUS_REG			0x9C
#define UART_TX_BURST_FIFO			0xA0
#define UART_RX_BURST_FIFO			0xA4
#define MAX_PORTS				4

#define UART_PCI_CTRL_SET_MULTIPLE_MSI		(1 << 4)
#define UART_BLOCK_SET_ACTIVE			(1 << 0)
#define UART_WAKE_N_PIN				(1 << 2)
#define PORT_OFFSET				0x100

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t dev_rev;
    uint32_t syslock;
    uint8_t pci_ctrl;
    uint32_t wake_reg;
    uint32_t wake_mask;
    uint32_t reset_reg;
    uint32_t burst_status;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->burst_status != 0);

    if (msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= UART_SYSTEM_ADDR_BASE) {
        switch (addr) {
        case UART_DEV_REV_REG:
            val = s->dev_rev;
            break;
        case UART_SYSLOCK_REG:
            val = s->syslock;
            break;
        default:
            break;
        }
    } else {
        hwaddr reg = addr % PORT_OFFSET;
        switch (reg) {
        case UART_PCI_CTRL_REG:
            val = s->pci_ctrl;
            break;
        case UART_WAKE_REG:
            val = s->wake_reg;
            break;
        case UART_WAKE_MASK_REG:
            val = s->wake_mask;
            break;
        case UART_RESET_REG:
            val = s->reset_reg;
            break;
        case UART_BURST_STATUS_REG:
            val = s->burst_status;
            break;
        default:
            break;
        }
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= UART_SYSTEM_ADDR_BASE) {
        switch (addr) {
        case UART_SYSLOCK_REG:
            s->syslock = val;
            break;
        default:
            break;
        }
    } else {
        hwaddr reg = addr % PORT_OFFSET;
        switch (reg) {
        case UART_PCI_CTRL_REG:
            s->pci_ctrl = val;
            break;
        case UART_WAKE_REG:
            s->wake_reg = val;
            break;
        case UART_WAKE_MASK_REG:
            s->wake_mask = val;
            break;
        case UART_RESET_REG:
            s->reset_reg = val;
            break;
        case UART_BURST_STATUS_REG:
            s->burst_status = val;
            pcibase_update_irq(s);
            break;
        default:
            break;
        }
    }
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
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->dev_rev = 0xC0; /* Revision C0 supports burst mode */
    s->syslock = 0;
    s->pci_ctrl = 0;
    s->wake_reg = 0;
    s->wake_mask = 0;
    s->reset_reg = 0;
    s->burst_status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_EFAR );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_EFAR_PCI11010 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_COMMUNICATION_SERIAL );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    if (msi_init(pdev, 0, MAX_PORTS, true, false, errp)) {
        /* Non-fatal */
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000; /* 8KB covers 4 ports + system regs */
    s->bar_info[0].name = "pci1xxxx-bar0";

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
    .name = "pci1xxxx_serial_pci",
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
