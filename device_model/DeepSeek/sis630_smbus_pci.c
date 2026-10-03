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

#define TYPE_PCIBASE_DEVICE "sis630_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define TYPE_SIS630_HOST "sis630_host"
typedef struct SIS630HostState SIS630HostState;
OBJECT_DECLARE_SIMPLE_TYPE(SIS630HostState, SIS630_HOST)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Vendor/Device IDs */
#define PCI_VENDOR_ID_SI		0x1039
#define PCI_DEVICE_ID_SI_503		0x0008
#define PCI_DEVICE_ID_SI_630		0x0630

/* SMBus I/O region size (from driver) */
#define SIS630_SMB_IOREGION 20

/* SMBus register offsets (relative to I/O base) */
#define SMB_STS         0x00
#define SMB_CNT         0x02
#define SMBHOST_CNT     0x03
#define SMB_ADDR        0x04
#define SMB_CMD         0x05
#define SMB_COUNT       0x07
#define SMB_BYTE        0x08

/* Status/Control bits */
#define BYTE_DONE_STS   0x10
#define SMBCOL_STS      0x04
#define SMBERR_STS      0x02
#define MSTO_EN         0x40
#define SMBCLK_SEL      0x20
#define SMB_PROBE       0x02
#define SMB_HOSTBUSY    0x01
#define SMB_KILL        0x20
#define SMB_START       0x10

/* Additional PCI config registers from driver */
#define SIS630_BIOS_CTL_REG 0x40
#define SIS630_ACPI_BASE_REG 0x74

#define SIS630_SMBUS_CLASS_ID 0x0c05  /* PCI_CLASS_SERIAL_SMBUS */

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
    /* SMBus I/O registers (20 bytes) */
    struct {
        uint8_t sts;       /* SMB_STS at offset 0x00 */
        uint8_t reserved1; /* 0x01 */
        uint8_t cnt;       /* SMB_CNT at 0x02 */
        uint8_t hostcnt;   /* SMBHOST_CNT at 0x03 */
        uint8_t addr;      /* SMB_ADDR at 0x04 */
        uint8_t cmd;       /* SMB_CMD at 0x05 */
        uint8_t reserved2; /* 0x06 */
        uint8_t count;     /* SMB_COUNT at 0x07 */
        uint8_t byte;      /* SMB_BYTE at 0x08 */
        uint8_t reserved3[11]; /* 0x09-0x13, total size 20 */
    } smbus_regs;
};

struct SIS630HostState {
    PCIDevice parent_obj;
};

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    /* For SIS630_ACPI_BASE_REG, return a computed ACPI base derived from BAR0 */
    if (address == SIS630_ACPI_BASE_REG && len == 2) {
        uint16_t bar0 = pci_default_read_config(pdev, PCI_BASE_ADDRESS_0, 2);
        uint16_t bar0_base = bar0 & ~0x3;
        return (uint16_t)(bar0_base - 0x80);
    }
    return pci_default_read_config(pdev, address, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    pci_default_write_config(pdev, address, val, len);
}

/* PIO Handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SMB_STS:
        val = s->smbus_regs.sts;
        break;
    case SMB_CNT:
        val = s->smbus_regs.cnt;
        break;
    case SMBHOST_CNT:
        val = s->smbus_regs.hostcnt;
        break;
    case SMB_ADDR:
        val = s->smbus_regs.addr;
        break;
    case SMB_CMD:
        val = s->smbus_regs.cmd;
        break;
    case SMB_COUNT:
        val = s->smbus_regs.count;
        break;
    case SMB_BYTE:
        val = s->smbus_regs.byte;
        break;
    default:
        if (addr >= 0x09 && addr <= 0x13) {
            val = 0; /* reserved */
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "sis630: PIO read bad offset 0x%x\n", (int)addr);
        }
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SMB_STS:
        /* Direct write to allow probe detection; no W1C semantics for now */
        s->smbus_regs.sts = val;
        break;
    case SMB_CNT:
        /* Direct write to cnt register, but protect read-only bits */
        /* Allow writing MSTO_EN (0x40) and SMBCLK_SEL (0x20); preserve busy/probe bits */
        s->smbus_regs.cnt = (s->smbus_regs.cnt & (SMB_PROBE | SMB_HOSTBUSY)) | (val & (MSTO_EN | SMBCLK_SEL));
        break;
    case SMBHOST_CNT:
        /* Check for KILL or START */
        if (val & SMB_KILL) {
            s->smbus_regs.cnt &= ~(SMB_PROBE | SMB_HOSTBUSY);
        }
        if (val & SMB_START) {
            /* Simulate immediate completion */
            s->smbus_regs.sts |= 0x08 | BYTE_DONE_STS; /* set generic done and BYTE_DONE */
            /* For read block, ensure count is set to 0 if not written */
            if ((s->smbus_regs.addr & 1) && (s->smbus_regs.count == 0)) {
                /* likely read block with no count set */
                s->smbus_regs.count = 0; /* already 0 */
            }
        }
        /* Store the written value to hostcnt */
        s->smbus_regs.hostcnt = val;
        break;
    case SMB_ADDR:
        s->smbus_regs.addr = val;
        break;
    case SMB_CMD:
        s->smbus_regs.cmd = val;
        break;
    case SMB_COUNT:
        s->smbus_regs.count = val;
        break;
    case SMB_BYTE:
        s->smbus_regs.byte = val;
        break;
    default:
        if (addr >= 0x09 && addr <= 0x13) {
            /* reserved, ignore */
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "sis630: PIO write bad offset 0x%x\n", (int)addr);
        }
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset SMBus registers to power-on defaults */
    memset(&s->smbus_regs, 0, sizeof(s->smbus_regs));
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
        /* Not used for this device, but keep for completeness */
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
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
    PCIBus *bus = pci_get_bus(pdev);
    DeviceState *host_bridge;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_SI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_SI_503);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SIS630_SMBUS_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = SIS630_SMB_IOREGION;
    s->bar_info[0].name = "sis630-smbus";
    s->num_bars = 1;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Create and realize a host bridge device to satisfy the driver's chipset detection */
    host_bridge = qdev_new(TYPE_SIS630_HOST);
    if (!qdev_realize_and_unref(host_bridge, BUS(bus), errp)) {
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* No additional cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sis630_smbus_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void sis630_host_realize(PCIDevice *pdev, Error **errp)
{
    uint8_t *pci_conf = pdev->config;
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_SI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_SI_630);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0601); /* PCI_CLASS_BRIDGE_ISA */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);

    /* Enable PCI Express capability to allow placement on PCIe bus */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static InterfaceInfo sis630_host_interfaces[] = {
    { INTERFACE_PCIE_DEVICE },
    { INTERFACE_CONVENTIONAL_PCI_DEVICE },
    { },
};

static void sis630_host_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = sis630_host_realize;
    dc->desc = "SiS 630 Host Bridge";
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

    static const TypeInfo sis630_host_info = {
        .name = TYPE_SIS630_HOST,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(SIS630HostState),
        .class_init = sis630_host_class_init,
        .interfaces = sis630_host_interfaces,
    };

    type_register_static(&pcibase_info);
    type_register_static(&sis630_host_info);
}

type_init(pcibase_register_types);
