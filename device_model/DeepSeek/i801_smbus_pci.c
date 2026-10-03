/*
 * QEMU PCI device model for i801 SMBus controller
 * Based on Linux driver: i2c-i801.c
 * Phase 2: Functional implementation.
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

#define TYPE_PCIBASE_DEVICE "i801_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs and register definitions from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x2413
#define CLASS_ID 0x0c05

/* SMBus register offsets (relative to smba) */
#define SMBHSTSTS 0
#define SMBHSTCMD 3
#define SMBHSTADD 4
#define SMBHSTDAT0 5
#define SMBHSTDAT1 6
#define SMBBLKDAT 7
#define SMBHSTCFG 0x40
#define SMBHSTCNT 2
#define SMBPEC 8
#define SMBAUXSTS 12
#define SMBAUXCTL 13
#define SMBSLVSTS 16
#define SMBSLVCMD 17
#define SMBNTFDADD 20
#define SMBBAR_MMIO 0
#define SMBBAR 4
#define TCOBASE 0x50
#define TCOCTL 0x54
#define SBREG_SMBCTRL 0xc6000c
#define SBREG_SMBCTRL_DNV 0xcf000c

/* Control/status bits */
#define SMBHSTCFG_HST_EN       BIT(0)
#define SMBHSTCFG_SMB_SMI_EN   BIT(1)
#define SMBHSTCFG_I2C_EN       BIT(2)
#define SMBHSTCFG_SPD_WD       BIT(4)
#define TCOCTL_EN              BIT(8)
#define SMBAUXSTS_CRCE         BIT(0)
#define SMBAUXSTS_STCO         BIT(1)
#define SMBAUXCTL_CRC          BIT(0)
#define SMBAUXCTL_E32B         BIT(1)
#define SMBHSTCNT_INTREN       BIT(0)
#define SMBHSTCNT_KILL         BIT(1)
#define SMBHSTCNT_LAST_BYTE    BIT(5)
#define SMBHSTCNT_START        BIT(6)
#define SMBHSTCNT_PEC_EN       BIT(7)
#define SMBHSTSTS_BYTE_DONE    BIT(7)
#define SMBHSTSTS_INUSE_STS    BIT(6)
#define SMBHSTSTS_SMBALERT_STS BIT(5)
#define SMBHSTSTS_FAILED       BIT(4)
#define SMBHSTSTS_BUS_ERR      BIT(3)
#define SMBHSTSTS_DEV_ERR      BIT(2)
#define SMBHSTSTS_INTR         BIT(1)
#define SMBHSTSTS_HOST_BUSY    BIT(0)
#define SMBSLVSTS_HST_NTFY_STS BIT(0)
#define SMBSLVCMD_SMBALERT_DISABLE BIT(2)
#define SMBSLVCMD_HST_NTFY_INTREN  BIT(0)
#define STATUS_ERROR_FLAGS     (SMBHSTSTS_FAILED | SMBHSTSTS_BUS_ERR | SMBHSTSTS_DEV_ERR)
#define STATUS_FLAGS           (SMBHSTSTS_BYTE_DONE | SMBHSTSTS_INTR | STATUS_ERROR_FLAGS)

/* I2C commands */
#define I801_QUICK             0x00
#define I801_BYTE              0x04
#define I801_BYTE_DATA         0x08
#define I801_WORD_DATA         0x0C
#define I801_PROC_CALL         0x10
#define I801_BLOCK_DATA        0x14
#define I801_I2C_BLOCK_DATA    0x18
#define I801_BLOCK_PROC_CALL   0x1C

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

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Flat SMBus register space (size TBD) */
    uint8_t regs[256];

    /* Driver processes status in irq handler */
    uint8_t status;

    /* PCI config register SMBHSTCFG (0x40) */
    uint8_t hstcfg;
};

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (address == 0x40 && len == 1) {
        return s->hstcfg;
    }
    return pci_default_read_config(pdev, address, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (address == 0x40 && len == 1) {
        s->hstcfg = val & 0xff;
        return;
    }
    pci_default_write_config(pdev, address, val, len);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        switch (size) {
        case 1:
            val = s->regs[addr];
            break;
        case 2:
            val = *(uint16_t *)(s->regs + addr);
            break;
        case 4:
            val = *(uint32_t *)(s->regs + addr);
            break;
        case 8:
            val = *(uint64_t *)(s->regs + addr);
            break;
        default:
            val = 0;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->regs)) {
        switch (size) {
        case 1:
            s->regs[addr] = (uint8_t)val;
            break;
        case 2:
            *(uint16_t *)(s->regs + addr) = (uint16_t)val;
            break;
        case 4:
            *(uint32_t *)(s->regs + addr) = (uint32_t)val;
            break;
        case 8:
            *(uint64_t *)(s->regs + addr) = val;
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        switch (size) {
        case 1:
            val = s->regs[addr];
            break;
        case 2:
            val = *(uint16_t *)(s->regs + addr);
            break;
        case 4:
            val = *(uint32_t *)(s->regs + addr);
            break;
        default:
            val = 0;
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->regs)) {
        switch (size) {
        case 1:
            s->regs[addr] = (uint8_t)val;
            break;
        case 2:
            *(uint16_t *)(s->regs + addr) = (uint16_t)val;
            break;
        case 4:
            *(uint32_t *)(s->regs + addr) = (uint32_t)val;
            break;
        }
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
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->status = 0;
    s->hstcfg = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
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
    s->bar_info[0].size = 0x1000; /* placeholder: need real size */
    s->bar_info[0].name = "i801-mmio";
    s->bar_info[1].index = 4;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 0x100;  /* placeholder: need real size */
    s->bar_info[1].name = "i801-io";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X initialization (driver uses legacy IRQ) */
    /* No DMA configuration */
    /* No timer initialization */

    /* Final state initialization */
    memset(s->regs, 0, sizeof(s->regs));
    s->status = 0;
    s->hstcfg = 0;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "i801_smbus_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
