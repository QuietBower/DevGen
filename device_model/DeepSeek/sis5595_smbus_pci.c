/*
 * QEMU PCI device model for SiS 5595 SMBus controller
 * Based on Linux driver i2c-sis5595.c
 * Phase 4: Runtime Refinement - Distinguished from SiS 630 to prevent region
 * conflict, ensuring sis5595_smbus driver can probe successfully.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "exec/ioport.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "sis5595_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor/Device IDs from the first pci_device_id entry */
#define VENDOR_ID 0x1039   /* Silicon Integrated Systems (SiS) */
#define DEVICE_ID 0x0008   /* SiS 5595 (SiS503) */
#define CLASS_ID  PCI_CLASS_SERIAL_SMBUS

/* PCI configuration register offsets (non-standard) */
#define SIS5595_ENABLE_REG 0x40
#define ACPI_BASE          0x90

/* SMBus indirect register offsets (accessed via INDEX/DATA ports) */
#define SMB_STS_LO        0x00
#define SMB_STS_HI        0x01
#define SMB_CTL_LO        0x02
#define SMB_CTL_HI        0x03
#define SMB_ADDR          0x04
#define SMB_CMD           0x05
#define SMB_PCNT          0x06
#define SMB_CNT           0x07
#define SMB_BYTE          0x08
#define SMB_DEV           0x10
#define SMB_DB0           0x11
#define SMB_DB1           0x12
#define SMB_HAA           0x13

/* Index and Data port offsets (relative to base) */
#define SMB_INDEX         0x38
#define SMB_DAT           0x39

/* SMBus command constants */
#define SIS5595_QUICK     0x00
#define SIS5595_BYTE      0x02
#define SIS5595_BYTE_DATA 0x04
#define SIS5595_WORD_DATA 0x06
#define SIS5595_PROC_CALL 0x08
#define SIS5595_BLOCK_DATA 0x0A

#define SIS5595_EXTENT    8   /* IO region size (for alignment) */
#define SIS5595_IOREGION_SIZE 0x40 /* Size of I/O region we provide */

struct PCIBaseState {
    PCIDevice parent_obj;

    /* I/O region for SMBus registers */
    MemoryRegion smbus_io;

    /* Base I/O port for SMBus */
    uint16_t pio_base;

    /* Internal SMBus indirect registers */
    uint8_t smb_sts_lo;
    uint8_t smb_sts_hi;
    uint8_t smb_ctl_lo;
    uint8_t smb_ctl_hi;
    uint8_t smb_addr;
    uint8_t smb_cmd;
    uint8_t smb_pcnt;
    uint8_t smb_cnt;
    uint8_t smb_byte;
    uint8_t smb_dev;
    uint8_t smb_db0;
    uint8_t smb_db1;
    uint8_t smb_haa;
    uint8_t smb_index;
};

/* PIO read/write handlers for SMBus register access */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == SMB_INDEX) {
        val = s->smb_index;
    } else if (addr == SMB_DAT) {
        switch (s->smb_index) {
        case SMB_STS_LO:
            val = s->smb_sts_lo;
            break;
        case SMB_STS_HI:
            val = s->smb_sts_hi;
            break;
        case SMB_CTL_LO:
            val = s->smb_ctl_lo;
            break;
        case SMB_CTL_HI:
            val = s->smb_ctl_hi;
            break;
        case SMB_ADDR:
            val = s->smb_addr;
            break;
        case SMB_CMD:
            val = s->smb_cmd;
            break;
        case SMB_PCNT:
            val = s->smb_pcnt;
            break;
        case SMB_CNT:
            val = s->smb_cnt;
            break;
        case SMB_BYTE:
            val = s->smb_byte;
            break;
        case 0x09:
            val = 0;
            break;
        case SMB_DEV:
            val = s->smb_dev;
            break;
        case SMB_DB0:
            val = s->smb_db0;
            break;
        case SMB_DB1:
            val = s->smb_db1;
            break;
        case SMB_HAA:
            val = s->smb_haa;
            break;
        default:
            val = 0;
            break;
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == SMB_INDEX) {
        s->smb_index = (uint8_t)val;
    } else if (addr == SMB_DAT) {
        uint8_t data = (uint8_t)val;
        switch (s->smb_index) {
        case SMB_STS_LO:
            s->smb_sts_lo &= ~data;
            break;
        case SMB_STS_HI:
            s->smb_sts_hi &= ~data;
            break;
        case SMB_CTL_LO:
            s->smb_ctl_lo = data;
            if (data & 0x10) {
                s->smb_sts_lo |= 0x40;
            }
            break;
        case SMB_CTL_HI:
            s->smb_ctl_hi = data;
            break;
        case SMB_ADDR:
            s->smb_addr = data;
            break;
        case SMB_CMD:
            s->smb_cmd = data;
            break;
        case SMB_PCNT:
            s->smb_pcnt = data;
            break;
        case SMB_CNT:
            s->smb_cnt = data;
            break;
        case SMB_BYTE:
            s->smb_byte = data;
            break;
        case 0x09:
            break;
        case SMB_DEV:
            s->smb_dev = data;
            break;
        case SMB_DB0:
            s->smb_db0 = data;
            break;
        case SMB_DB1:
            s->smb_db1 = data;
            break;
        case SMB_HAA:
            s->smb_haa = data;
            break;
        default:
            break;
        }
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Intercept writes to ACPI_BASE to update I/O mapping */
    if (addr == ACPI_BASE) {
        /* Store old base to update mapping */
        uint16_t old_base = s->pio_base;
        uint16_t new_base;

        /* Write to config space first */
        pci_default_write_config(pdev, addr, val, len);
        /* Update our cached base from config space */
        new_base = pci_get_word(pdev->config + ACPI_BASE);
        s->pio_base = new_base;

        /* Move the I/O region if base changed */
        if (old_base != new_base) {
            memory_region_del_subregion(get_system_io(), &s->smbus_io);
            memory_region_add_subregion(get_system_io(), new_base, &s->smbus_io);
        }
    } else {
        pci_default_write_config(pdev, addr, val, len);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->smb_sts_lo = 0;
    s->smb_sts_hi = 0;
    s->smb_ctl_lo = 0;
    s->smb_ctl_hi = 0;
    s->smb_addr = 0;
    s->smb_cmd = 0;
    s->smb_pcnt = 0;
    s->smb_cnt = 0;
    s->smb_byte = 0;
    s->smb_dev = 0x04;  /* SiS 5595 device ID */
    s->smb_db0 = 0;
    s->smb_db1 = 0;
    s->smb_haa = 0;
    s->smb_index = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x20);  /* Revision to distinguish from SiS 630 */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x1039);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x0008);

    /* Enable ACPI SMBus by setting bit 7 */
    pci_conf[SIS5595_ENABLE_REG] = 0x80;

    s->pio_base = 0x0000;
    pci_set_word(pci_conf + ACPI_BASE, s->pio_base);

    /* Create I/O region and map into system I/O space at the base address */
    memory_region_init_io(&s->smbus_io, OBJECT(s), &pcibase_pio_ops, s,
                          "sis5595-smbus", SIS5595_IOREGION_SIZE);
    memory_region_add_subregion(get_system_io(), s->pio_base, &s->smbus_io);

    /* Initialize device-specific register defaults */
    s->smb_dev = 0x04;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    memory_region_del_subregion(get_system_io(), &s->smbus_io);
    memory_region_unref(&s->smbus_io);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "sis5595_smbus_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16(pio_base, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->config_write = pcibase_config_write;
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
