/*
 * QEMU model for PIIX4 SMBus controller (i2c-piix4 driver)
 * Based on Intel 82371AB SMBus Host Controller
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "piix4_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets and PCI IDs */
#define SMBHSTSTS  0x00
#define SMBHSTCNT  0x02
#define SMBHSTCMD  0x03
#define SMBHSTADD  0x04
#define SMBHSTDAT0 0x05
#define SMBHSTDAT1 0x06
#define SMBBLKDAT  0x07
#define SMBSLVCNT  0x08
#define SMBIOSIZE  9

#define SMBBAR 4

/* PCI config register offsets (non-standard for PIIX4 SMBus) */
#define SMBBA       0x90
#define SMBHSTCFG   0xD2
#define SMBREV      0xD6

#define PIIX4_SMBUS_VENDOR_ID PCI_VENDOR_ID_INTEL
#define PIIX4_SMBUS_DEVICE_ID PCI_DEVICE_ID_INTEL_82371AB_3
#define PIIX4_SMBUS_CLASS PCI_CLASS_SERIAL_SMBUS

#define SMBUS_IOSIZE 0x10

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    uint8_t smb_regs[SMBUS_IOSIZE];  /* Full I/O space, first 9 used */
    uint8_t smbhstcfg;               /* SMBHSTCFG register value */
    uint8_t smbrev;                  /* SMBREV register value */
    uint8_t blkdat_index;            /* Index for SMBBLKDAT */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Not used: driver polls */
}

/* PIO read/write handlers for SMBus I/O registers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= SMBUS_IOSIZE) {
        return ~0ULL;
    }

    switch (addr) {
    case SMBHSTSTS:
        val = s->smb_regs[SMBHSTSTS];
        break;
    case SMBHSTCNT:
        /* Reading SMBHSTCNT resets the block data index */
        s->blkdat_index = 0;
        val = s->smb_regs[SMBHSTCNT];
        break;
    case SMBHSTCMD:
        val = s->smb_regs[SMBHSTCMD];
        break;
    case SMBHSTADD:
        val = s->smb_regs[SMBHSTADD];
        break;
    case SMBHSTDAT0:
        val = s->smb_regs[SMBHSTDAT0];
        break;
    case SMBHSTDAT1:
        val = s->smb_regs[SMBHSTDAT1];
        break;
    case SMBBLKDAT:
        /* Read at current index, then increment */
        val = s->smb_regs[SMBHSTDAT0]; /* In reality, block data is stored in a separate buffer,
                                          but for simplicity we just return DAT0 for the data, and
                                          the index is not used for data storage. Real hw has a 32-byte EEPROM? */
        /* We'll emulate a simple block read: index 0 initially, auto-increment after read */
        /* But since we don't have a real buffer, return 0 */
        val = 0; /* no real data */
        s->blkdat_index++;
        break;
    case SMBSLVCNT:
        val = s->smb_regs[SMBSLVCNT];
        break;
    default:
        val = s->smb_regs[addr];
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= SMBUS_IOSIZE) {
        return;
    }

    switch (addr) {
    case SMBHSTSTS:
        /* Write-1-to-clear */
        s->smb_regs[SMBHSTSTS] &= ~((uint8_t)val);
        break;
    case SMBHSTCNT:
        s->smb_regs[SMBHSTCNT] = (uint8_t)val;
        /* Start bit (bit 6) set: start transaction */
        if (val & 0x40) {
            /* In real hardware, this starts a transaction.
             * We emulate immediate completion. Set BUSY bit? Not needed
             * for probe, but to be safe, we could clear it immediately. */
            /* Nothing needed; the driver polls SMBHSTSTS and expects BUSY to clear */
        }
        break;
    case SMBHSTCMD:
        s->smb_regs[SMBHSTCMD] = (uint8_t)val;
        break;
    case SMBHSTADD:
        s->smb_regs[SMBHSTADD] = (uint8_t)val;
        break;
    case SMBHSTDAT0:
        s->smb_regs[SMBHSTDAT0] = (uint8_t)val;
        break;
    case SMBHSTDAT1:
        s->smb_regs[SMBHSTDAT1] = (uint8_t)val;
        break;
    case SMBBLKDAT:
        /* Write to block data at current index, then increment */
        s->smb_regs[addr] = (uint8_t)val;  /* This is a dummy; actual block data not stored */
        s->blkdat_index++;
        break;
    case SMBSLVCNT:
        s->smb_regs[SMBSLVCNT] = (uint8_t)val;
        break;
    default:
        s->smb_regs[addr] = (uint8_t)val;
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

/* Custom PCI config read/write to handle non-standard SMBus registers */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = 0;

    switch (address) {
    case SMBBA:
        /* SMBBA (0x90-0x93): redirect to BAR4 register (0x20-0x23) */
        address -= (SMBBA - 0x20);
        val = pci_default_read_config(pdev, address, len);
        break;
    case SMBHSTCFG:
        /* SMBHSTCFG byte (0xD2) */
        val = s->smbhstcfg;
        break;
    case SMBREV:
        /* SMBREV byte (0xD6) */
        val = s->smbrev;
        break;
    default:
        val = pci_default_read_config(pdev, address, len);
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (address) {
    case SMBBA:
        /* SMBBA write: redirect to BAR4 */
        address -= (SMBBA - 0x20);
        pci_default_write_config(pdev, address, val, len);
        break;
    case SMBHSTCFG:
        /* SMBHSTCFG byte (0xD2) */
        s->smbhstcfg = (uint8_t)val;
        break;
    default:
        pci_default_write_config(pdev, address, val, len);
        break;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->smb_regs, 0, sizeof(s->smb_regs));
    s->blkdat_index = 0;
    s->smbhstcfg = 0x00; /* SMI# mode, disabled */
    s->smbrev = 0x00;    /* Revision 0 */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PIIX4_SMBUS_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PIIX4_SMBUS_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PIIX4_SMBUS_CLASS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize BAR4 as I/O region for SMBus registers */
    MemoryRegion *mr = &s->bar_regions[SMBBAR];
    memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, "smbus-io", SMBUS_IOSIZE);
    pci_register_bar(pdev, SMBBAR, PCI_BASE_ADDRESS_SPACE_IO, mr);

    /* Pre-set SMBus base address to a common value (e.g., 0x1000).
     * In real hardware, the BIOS programs it. The driver will read SMBBA (0x90)
     * which is aliased to BAR4. */
    pci_set_long(pci_conf + 0x20, 0x1001);  /* I/O address 0x1000, enabled */

    s->smbhstcfg = 0x00; /* SMI# mode, not enabled initially */
    s->smbrev = 0x00;

    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No MSI/MSI-X cleanup needed */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "piix4_smbus_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(smb_regs, PCIBaseState, SMBUS_IOSIZE),
        VMSTATE_UINT8(smbhstcfg, PCIBaseState),
        VMSTATE_UINT8(smbrev, PCIBaseState),
        VMSTATE_UINT8(blkdat_index, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;
    dc->vmsd = &vmstate_pcibase;
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
