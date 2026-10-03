#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/units.h"

#define TYPE_PCIBASE_DEVICE "myrs-pci"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* DAC960 GEM register offsets */
#define DAC960_GEM_IDB_READ_OFFSET        0x00
#define DAC960_GEM_IDB_CLEAR_OFFSET       0x04
#define DAC960_GEM_ODB_READ_OFFSET        0x08
#define DAC960_GEM_ODB_CLEAR_OFFSET       0x0C
#define DAC960_GEM_IRQMASK_READ_OFFSET    0x10
#define DAC960_GEM_IRQMASK_CLEAR_OFFSET   0x14
#define DAC960_GEM_ERRSTS_READ_OFFSET     0x18
#define DAC960_GEM_ERRSTS_CLEAR_OFFSET    0x1C
#define DAC960_GEM_CMDMBX_OFFSET          0x20
#define DAC960_GEM_CMDSTS_OFFSET          0x28

#define DAC960_GEM_MMIO_BAR               0
#define DAC960_GEM_MMIO_SIZE              0x100

enum myrs_cmd_opcode {
    MYRS_CMD_OP_MEMCOPY         = 0x01,
    MYRS_CMD_OP_SCSI_10_PASSTHRU = 0x02,
    MYRS_CMD_OP_SCSI_255_PASSTHRU = 0x03,
    MYRS_CMD_OP_SCSI_10         = 0x04,
    MYRS_CMD_OP_SCSI_256        = 0x05,
    MYRS_CMD_OP_IOCTL           = 0x20,
};

enum myrs_ioctl_opcode {
    MYRS_IOCTL_GET_CTLR_INFO    = 0x01,
    MYRS_IOCTL_GET_LDEV_INFO_VALID = 0x03,
    MYRS_IOCTL_GET_PDEV_INFO_VALID = 0x05,
    MYRS_IOCTL_GET_HEALTH_STATUS = 0x11,
    MYRS_IOCTL_GET_EVENT        = 0x15,
    MYRS_IOCTL_START_DISCOVERY  = 0x81,
    MYRS_IOCTL_SET_DEVICE_STATE = 0x82,
    MYRS_IOCTL_INIT_PDEV_START  = 0x84,
    MYRS_IOCTL_INIT_PDEV_STOP   = 0x85,
    MYRS_IOCTL_INIT_LDEV_START  = 0x86,
    MYRS_IOCTL_INIT_LDEV_STOP   = 0x87,
    MYRS_IOCTL_RBLD_DEVICE_START = 0x88,
    MYRS_IOCTL_RBLD_DEVICE_STOP  = 0x89,
    MYRS_IOCTL_MAKE_CONSISTENT_START = 0x8A,
    MYRS_IOCTL_MAKE_CONSISTENT_STOP  = 0x8B,
    MYRS_IOCTL_CC_START         = 0x8C,
    MYRS_IOCTL_CC_STOP          = 0x8D,
    MYRS_IOCTL_SET_MEM_MBOX     = 0x8E,
    MYRS_IOCTL_RESET_DEVICE     = 0x90,
    MYRS_IOCTL_FLUSH_DEVICE_DATA = 0x91,
    MYRS_IOCTL_PAUSE_DEVICE     = 0x92,
    MYRS_IOCTL_UNPAUS_EDEVICE   = 0x93,
    MYRS_IOCTL_LOCATE_DEVICE    = 0x94,
    MYRS_IOCTL_CREATE_CONFIGURATION = 0xC0,
    MYRS_IOCTL_DELETE_LDEV      = 0xC1,
    MYRS_IOCTL_REPLACE_INTERNALDEVICE = 0xC2,
    MYRS_IOCTL_RENAME_LDEV      = 0xC3,
    MYRS_IOCTL_ADD_CONFIGURATION = 0xC4,
    MYRS_IOCTL_XLATE_PDEV_TO_LDEV = 0xC5,
    MYRS_IOCTL_CLEAR_CONFIGURATION = 0xCA,
};

enum myrs_devstate {
    MYRS_DEVICE_UNCONFIGURED     = 0x00,
    MYRS_DEVICE_ONLINE           = 0x01,
    MYRS_DEVICE_REBUILD          = 0x03,
    MYRS_DEVICE_MISSING          = 0x04,
    MYRS_DEVICE_SUSPECTED_CRITICAL = 0x05,
    MYRS_DEVICE_OFFLINE          = 0x08,
    MYRS_DEVICE_CRITICAL         = 0x09,
    MYRS_DEVICE_SUSPECTED_DEAD   = 0x0C,
    MYRS_DEVICE_COMMANDED_OFFLINE = 0x10,
    MYRS_DEVICE_STANDBY          = 0x21,
    MYRS_DEVICE_INVALID_STATE    = 0xFF,
};

enum myrs_opdev {
    MYRS_PHYSICAL_DEVICE         = 0x00,
    MYRS_RAID_DEVICE             = 0x01,
    MYRS_PHYSICAL_CHANNEL        = 0x02,
    MYRS_RAID_CHANNEL            = 0x03,
    MYRS_PHYSICAL_CONTROLLER     = 0x04,
    MYRS_RAID_CONTROLLER         = 0x05,
    MYRS_CONFIGURATION_GROUP     = 0x10,
    MYRS_ENCLOSURE               = 0x11,
};

enum myrs_cpu_type {
    MYRS_CPUTYPE_i960CA          = 0x01,
    MYRS_CPUTYPE_i960RD          = 0x02,
    MYRS_CPUTYPE_i960RN          = 0x03,
    MYRS_CPUTYPE_i960RP          = 0x04,
    MYRS_CPUTYPE_NorthBay        = 0x05,
    MYRS_CPUTYPE_StrongArm       = 0x06,
    MYRS_CPUTYPE_i960RM          = 0x07,
};

struct PCIBaseState {
    PCIDevice pdev;
    MemoryRegion mmio;
};

static void pcibase_process_hw_mbox(PCIBaseState *s, uint32_t mbox_offset);
static void pcibase_process_mem_mbox(PCIBaseState *s, uint32_t mbox_offset);

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    if (addr < DAC960_GEM_IDB_CLEAR_OFFSET) {
        /* IDB read */
    } else if (addr >= DAC960_GEM_ODB_READ_OFFSET &&
               addr < DAC960_GEM_ODB_CLEAR_OFFSET) {
        /* ODB read */
    } else if (addr >= DAC960_GEM_IRQMASK_READ_OFFSET &&
               addr < DAC960_GEM_IRQMASK_CLEAR_OFFSET) {
        /* interrupt mask */
    } else if (addr >= DAC960_GEM_ERRSTS_READ_OFFSET &&
               addr < DAC960_GEM_ERRSTS_CLEAR_OFFSET) {
        /* error status */
    } else if (addr >= DAC960_GEM_CMDMBX_OFFSET &&
               addr < DAC960_GEM_CMDSTS_OFFSET) {
        /* command mailbox */
    } else if (addr >= DAC960_GEM_CMDSTS_OFFSET &&
               addr < DAC960_GEM_CMDSTS_OFFSET + 4) {
        /* command status */
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < DAC960_GEM_IDB_CLEAR_OFFSET) {
        /* IDB (inbound doorbell) write triggers processing */
        pcibase_process_hw_mbox(s, val);
    } else if (addr >= DAC960_GEM_IDB_CLEAR_OFFSET &&
               addr < DAC960_GEM_ODB_READ_OFFSET) {
        /* IDB clear */
    } else if (addr >= DAC960_GEM_ODB_READ_OFFSET &&
               addr < DAC960_GEM_ODB_CLEAR_OFFSET) {
        /* ODB write */
    } else if (addr >= DAC960_GEM_ODB_CLEAR_OFFSET &&
               addr < DAC960_GEM_IRQMASK_READ_OFFSET) {
        /* ODB clear */
    } else if (addr >= DAC960_GEM_IRQMASK_READ_OFFSET &&
               addr < DAC960_GEM_IRQMASK_CLEAR_OFFSET) {
        /* interrupt mask write */
    } else if (addr >= DAC960_GEM_IRQMASK_CLEAR_OFFSET &&
               addr < DAC960_GEM_ERRSTS_READ_OFFSET) {
        /* interrupt mask clear */
    } else if (addr >= DAC960_GEM_ERRSTS_READ_OFFSET &&
               addr < DAC960_GEM_ERRSTS_CLEAR_OFFSET) {
        /* error status write */
    } else if (addr >= DAC960_GEM_ERRSTS_CLEAR_OFFSET &&
               addr < DAC960_GEM_CMDMBX_OFFSET) {
        /* error status clear */
    } else if (addr >= DAC960_GEM_CMDMBX_OFFSET &&
               addr < DAC960_GEM_CMDSTS_OFFSET) {
        /* command mailbox write */
        pcibase_process_mem_mbox(s, addr - DAC960_GEM_CMDMBX_OFFSET);
    } else if (addr >= DAC960_GEM_CMDSTS_OFFSET &&
               addr < DAC960_GEM_CMDSTS_OFFSET + 4) {
        /* command status write */
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pcibase_process_hw_mbox(PCIBaseState *s, uint32_t mbox)
{
    /* Minimal processing to handle the command that sets memory mailbox */
    if (mbox && 0) { /* dummy condition, placeholder */
        /* if MYRS_CMD_OP_IOCTL && MYRS_IOCTL_SET_MEM_MBOX */
    }
}

static void pcibase_process_mem_mbox(PCIBaseState *s, uint32_t offset)
{
    /* Process commands from memory mailbox */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Initialize MMIO */
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "myrs-mmio", DAC960_GEM_MMIO_SIZE);
    pci_register_bar(pdev, DAC960_GEM_MMIO_BAR,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* PCI device uninit; no cleanup needed for our simple model */
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    k->vendor_id = 0x1069;
    k->device_id = 0x0020;
    k->class_id = PCI_CLASS_STORAGE_RAID;
    k->revision = 0;
}

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}
type_init(pcibase_register_types);
