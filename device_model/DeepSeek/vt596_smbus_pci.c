/*
 * QEMU emulation for VIA VT596 SMBus PCI device (VT82C596_3)
 *
 * This model provides a PIO-mapped SMBus controller at a fixed I/O base
 * address (0xC000) accessed via vendor-specific PCI config registers.
 * The driver (i2c-viapro) never claims the PCI device, and always returns
 * -ENODEV after registering the I2C adapter.
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

#define PCI_VENDOR_ID_VIA		0x1106
#define PCI_DEVICE_ID_VIA_82C596_3	0x3050

#define TYPE_PCIBASE_DEVICE "vt596_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Fixed I/O base address used by the driver */
#define BASE_ADDRESS 0xC000

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID               PCI_VENDOR_ID_VIA
#define DEVICE_ID               PCI_DEVICE_ID_VIA_82C596_3
#define CLASS_ID                PCI_CLASS_SERIAL_SMBUS

/* SMBus controller register offsets (relative to I/O base) */
#define SMBHSTSTS               0
#define SMBHSTCNT               2
#define SMBHSTCMD               3
#define SMBHSTADD               4
#define SMBHSTDAT0              5
#define SMBHSTDAT1              6
#define SMBBLKDAT               7

/* PCI configuration space offsets for SMB base address selection */
#define SMBBA1                  0x90
#define SMBBA2                  0x80
#define SMBHSTCFG               0xD2

/* Maximum block data size for I2C SMBus */
#define I2C_SMBUS_BLOCK_MAX     32

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
    struct {
        uint8_t sts;     /* SMBHSTSTS */
        uint8_t cnt;     /* SMBHSTCNT */
        uint8_t cmd;     /* SMBHSTCMD */
        uint8_t add;     /* SMBHSTADD */
        uint8_t dat0;    /* SMBHSTDAT0 */
        uint8_t dat1;    /* SMBHSTDAT1 */
        uint8_t blkdat;  /* SMBBLKDAT */
    } smbus_regs;

    /* Block data buffer and pointer for extended transfers */
    uint8_t block_data[I2C_SMBUS_BLOCK_MAX];
    int block_ptr;

    /* Shadow for SMBHSTCFG configuration byte */
    uint8_t smbhostcfg;
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* Custom PCI config space read/write to handle legacy SMBus base address registers */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;

    /* Intercept custom registers */
    switch (addr) {
    case SMBBA1: /* 0x90 - SMBus I/O base address (word) */
        /* Return fixed address with I/O indicator bit set */
        val = BASE_ADDRESS | 0x1;
        return val;
    case SMBBA2: /* 0x80 - fallback base address, not used */
        return 0;
    case SMBHSTCFG: /* 0xD2 - SMBus host configuration byte */
        return s->smbhostcfg;
    default:
        return pci_default_read_config(pdev, addr, len);
    }
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (addr) {
    case SMBBA1: /* 0x90 - base address write (force_addr path) */
        /* Ignore, we always use the fixed address */
        break;
    case SMBHSTCFG: /* 0xD2 - update host configuration */
        if (len == 1) {
            s->smbhostcfg = (uint8_t)val;
        }
        break;
    default:
        pci_default_write_config(pdev, addr, val, len);
        break;
    }
}

/* PIO Read/Write Handlers for SMBus I/O region */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    if (size != 1) {
        return val;
    }

    switch (addr) {
    case SMBHSTSTS: /* 0x00 - Status register */
        val = s->smbus_regs.sts;
        break;
    case SMBHSTCNT: /* 0x02 - Control register (reading resets block data pointer) */
        s->block_ptr = 0;
        val = s->smbus_regs.cnt;
        break;
    case SMBHSTCMD: /* 0x03 - Command register */
        val = s->smbus_regs.cmd;
        break;
    case SMBHSTADD: /* 0x04 - Address register */
        val = s->smbus_regs.add;
        break;
    case SMBHSTDAT0: /* 0x05 - Data byte 0 */
        val = s->smbus_regs.dat0;
        break;
    case SMBHSTDAT1: /* 0x06 - Data byte 1 */
        val = s->smbus_regs.dat1;
        break;
    case SMBBLKDAT: /* 0x07 - Block data (auto-increment pointer) */
        if (s->block_ptr < I2C_SMBUS_BLOCK_MAX) {
            val = s->block_data[s->block_ptr++];
        } else {
            val = 0xff;
        }
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }

    switch (addr) {
    case SMBHSTSTS: /* 0x00 - Status register (write-1-to-clear lower 5 bits) */
        s->smbus_regs.sts &= ~(val & 0x1F);
        break;
    case SMBHSTCNT: /* 0x02 - Control register */
        if (val & 0x40) { /* Transaction start */
            s->smbus_regs.sts &= ~0x01; /* Clear BUSY */
        }
        s->smbus_regs.cnt = (uint8_t)val;
        break;
    case SMBHSTCMD: /* 0x03 - Command register */
        s->smbus_regs.cmd = (uint8_t)val;
        break;
    case SMBHSTADD: /* 0x04 - Address register */
        s->smbus_regs.add = (uint8_t)val;
        break;
    case SMBHSTDAT0: /* 0x05 - Data byte 0 */
        s->smbus_regs.dat0 = (uint8_t)val;
        break;
    case SMBHSTDAT1: /* 0x06 - Data byte 1 */
        s->smbus_regs.dat1 = (uint8_t)val;
        break;
    case SMBBLKDAT: /* 0x07 - Block data (auto-increment pointer) */
        if (s->block_ptr < I2C_SMBUS_BLOCK_MAX) {
            s->block_data[s->block_ptr++] = (uint8_t)val;
        }
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

/* MMIO handlers are not used by the driver */
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

    memset(&s->smbus_regs, 0, sizeof(s->smbus_regs));
    memset(s->block_data, 0, sizeof(s->block_data));
    s->block_ptr = 0;
    s->smbhostcfg = 0x01; /* Enable bit set by default */
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Install custom config space handlers for legacy SMBus base address registers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* No BARs are exposed; the driver obtains the I/O base from SMBBA1 */
    s->num_bars = 0;

    /* Manually add the PIO region to the system I/O space at the fixed address */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_pio_ops, s,
                          "smbus-io", 8);
    memory_region_add_subregion(get_system_io(), BASE_ADDRESS, &s->bar_regions[0]);

    /* Initialize shadow registers */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    memory_region_del_subregion(get_system_io(), &s->bar_regions[0]);
    memory_region_unref(&s->bar_regions[0]);

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "vt596_smbus_pci",
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
