/* Integrated QEMU PCI device model for ALi M5228 IDE controller (pata_ali) */

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

#define TYPE_PCIBASE_DEVICE "pata_ali_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ---------------------------------------------------------
 * PCI Configuration Register Overrides
 * The driver reads/writes chipset-specific registers 0x4A-0x5A
 * --------------------------------------------------------- */
#define ALI_CONFIG_REG_BASE    0x4A
#define ALI_CONFIG_REG_COUNT   0x11   /* bytes from 0x4A to 0x5A inclusive */

/* BMDMA register offsets */
#define BMDMA_REG_CMD       0
#define BMDMA_REG_STATUS    2

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    int num_bars;

    /* ALi configuration shadow registers */
    uint8_t config_bytes[ALI_CONFIG_REG_COUNT];

    /* BMDMA state per channel (0=primary, 1=secondary) */
    struct {
        uint8_t cmd;
        uint8_t status;
    } bmdma[2];
};

/* Forward declarations */
static uint32_t ali_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void ali_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static uint64_t primary_cmd_read(void *opaque, hwaddr addr, unsigned size);
static void primary_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t primary_ctl_read(void *opaque, hwaddr addr, unsigned size);
static void primary_ctl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t secondary_cmd_read(void *opaque, hwaddr addr, unsigned size);
static void secondary_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t secondary_ctl_read(void *opaque, hwaddr addr, unsigned size);
static void secondary_ctl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t bmdma_read(void *opaque, hwaddr addr, unsigned size);
static void bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps primary_cmd_ops = {
    .read = primary_cmd_read,
    .write = primary_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps primary_ctl_ops = {
    .read = primary_ctl_read,
    .write = primary_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static const MemoryRegionOps secondary_cmd_ops = {
    .read = secondary_cmd_read,
    .write = secondary_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps secondary_ctl_ops = {
    .read = secondary_ctl_read,
    .write = secondary_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static const MemoryRegionOps bmdma_ops = {
    .read = bmdma_read,
    .write = bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ---------------------------------------------------------
 * Status-driven IRQ update
 * --------------------------------------------------------- */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_asserted;

    irq_asserted = (s->bmdma[0].cmd & 1 && s->bmdma[0].status & 4) ||
                   (s->bmdma[1].cmd & 1 && s->bmdma[1].status & 4);

    pci_set_irq(pdev, irq_asserted ? 1 : 0);
}

/* ---------------------------------------------------------
 * PCI Configuration Space Intercepts
 * --------------------------------------------------------- */
static uint32_t ali_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);

    if (addr >= ALI_CONFIG_REG_BASE && addr < ALI_CONFIG_REG_BASE + ALI_CONFIG_REG_COUNT) {
        uint32_t off = addr - ALI_CONFIG_REG_BASE;
        val = 0;
        for (int i = 0; i < len; i++) {
            if (off + i < ALI_CONFIG_REG_COUNT) {
                val |= (uint32_t)s->config_bytes[off + i] << (8 * i);
            }
        }
    }
    return val;
}

static void ali_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr >= ALI_CONFIG_REG_BASE && addr < ALI_CONFIG_REG_BASE + ALI_CONFIG_REG_COUNT) {
        uint32_t off = addr - ALI_CONFIG_REG_BASE;
        for (int i = 0; i < len; i++) {
            if (off + i < ALI_CONFIG_REG_COUNT) {
                s->config_bytes[off + i] = (val >> (8 * i)) & 0xFF;
            }
        }
    }

    /* Always call default handler to update standard registers */
    pci_default_write_config(pdev, addr, val, len);
}

/* ---------------------------------------------------------
 * Primary Command Block (BAR0, I/O, 8 bytes)
 * --------------------------------------------------------- */
static uint64_t primary_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0: /* Data (16-bit) – read zero */
        break;
    case 1: /* Error */
        break;
    case 2: /* Sector Count */
        break;
    case 3: /* LBA Low */
        break;
    case 4: /* LBA Mid */
        break;
    case 5: /* LBA High */
        break;
    case 6: /* Device / Head */
        break;
    case 7: /* Status */
        val = 0x50;  /* DRDY=1, DSC=1, no BSY, no DRQ */
        break;
    default:
        break;
    }
    return val;
}

static void primary_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Only Status/Command writes are passed through, but we ignore all writes
     * for a minimal no-drive controller */
}

/* ---------------------------------------------------------
 * Primary Control Block (BAR1, I/O, 4 bytes)
 * --------------------------------------------------------- */
static uint64_t primary_ctl_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Alt Status */
    return 0x50;
}

static void primary_ctl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Device Control – ignored */
}

/* ---------------------------------------------------------
 * Secondary Command Block (BAR2, I/O, 8 bytes)
 * --------------------------------------------------------- */
static uint64_t secondary_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
        break;
    case 7:
        val = 0x50;
        break;
    }
    return val;
}

static void secondary_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

/* ---------------------------------------------------------
 * Secondary Control Block (BAR3, I/O, 4 bytes)
 * --------------------------------------------------------- */
static uint64_t secondary_ctl_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0x50;
}

static void secondary_ctl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

/* ---------------------------------------------------------
 * Bus Master DMA Base Address (BAR4, I/O, 16 bytes)
 * --------------------------------------------------------- */
static uint64_t bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    int channel = (addr >= 8) ? 1 : 0;
    uint32_t reg = addr & 7;
    uint64_t val = 0;

    switch (reg) {
    case BMDMA_REG_CMD:
        val = s->bmdma[channel].cmd;
        break;
    case BMDMA_REG_STATUS:
        val = s->bmdma[channel].status;
        break;
    default:
        break;
    }
    return val;
}

static void bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int channel = (addr >= 8) ? 1 : 0;
    uint32_t reg = addr & 7;

    switch (reg) {
    case BMDMA_REG_CMD:
        s->bmdma[channel].cmd = val & 0x01; /* only Start/Stop bit */
        break;
    case BMDMA_REG_STATUS:
        /* Bits 1 (Error) and 2 (Interrupt) are write-1-to-clear */
        s->bmdma[channel].status &= ~(val & 0x06);
        break;
    default:
        break;
    }

    pcibase_update_irq(s);
}

/* ---------------------------------------------------------
 * Generic (unused) MMIO / PIO handlers
 * --------------------------------------------------------- */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

/* ---------------------------------------------------------
 * Reset state: clear chipset registers and BMDMA
 * --------------------------------------------------------- */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->config_bytes, 0, sizeof(s->config_bytes));
    memset(s->bmdma, 0, sizeof(s->bmdma));
}

/* ---------------------------------------------------------
 * Realize: set up PCI config intercepts and register BARs
 * --------------------------------------------------------- */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x10b9);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x5228);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x8a);  /* Native PCI IDE, bus master capable */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0xc7);  /* ALi M5228 rev C7 */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Intercept config reads/writes for chipset registers */
    pdev->config_read  = ali_config_read;
    pdev->config_write = ali_config_write;

    /* No standard BAR array in this model */
    s->num_bars = 0;

    /* Primary Command Block (BAR0) */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &primary_cmd_ops, s,
                          "ide-primary-cmd", 16);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* Primary Control Block (BAR1) */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &primary_ctl_ops, s,
                          "ide-primary-ctl", 16);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    /* Secondary Command Block (BAR2) */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &secondary_cmd_ops, s,
                          "ide-secondary-cmd", 16);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);

    /* Secondary Control Block (BAR3) */
    memory_region_init_io(&s->bar_regions[3], OBJECT(s), &secondary_ctl_ops, s,
                          "ide-secondary-ctl", 16);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[3]);

    /* Bus Master DMA (BAR4) */
    memory_region_init_io(&s->bar_regions[4], OBJECT(s), &bmdma_ops, s,
                          "ide-bmdma", 16);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[4]);

    /* No DMA mask configuration, no additional state init needed */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSI-X, timers, or buffers to clean up */
}

/* Minimal VMState (migration of chipset regs is not critical) */
static const VMStateDescription vmstate_pcibase = {
    .name = "pata_ali_pci",
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
