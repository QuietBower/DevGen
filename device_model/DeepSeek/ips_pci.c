/*
 * QEMU model of IBM ServeRAID IPS PCI SCSI controller (Copperhead)
 * Based on Linux driver drivers/scsi/ips.c
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

#define TYPE_PCIBASE_DEVICE "ips_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define PCI_VENDOR_ID_IBM              0x1014
#define PCI_DEVICE_ID_IPS_COPPERHEAD   0x002E
#define PCI_CLASS_STORAGE_RAID         0x0104

/* Register Offsets (extracted from driver source) */
#define IPS_REG_SCPR        0x05
#define IPS_REG_ISPR        0x06
#define IPS_REG_CBSP        0x07
#define IPS_REG_HISR        0x08
#define IPS_REG_CCSAR       0x10
#define IPS_REG_CCCR        0x14
#define IPS_REG_FLAP        0x18
#define IPS_REG_FLDP        0x1C
#define IPS_REG_I960_MSG0   0x18
#define IPS_REG_I960_MSG1   0x1C
#define IPS_REG_I960_IDR    0x20
#define IPS_REG_I960_OIMR   0x34
#define IPS_REG_SQHR        0x20
#define IPS_REG_SQTR        0x24
#define IPS_REG_SQER        0x28
#define IPS_REG_SQSR        0x2C
#define IPS_REG_I2O_HIR     0x30
#define IPS_REG_NDAE        0x38
#define IPS_REG_I2O_INMSGQ  0x40
#define IPS_REG_I2O_OUTMSGQ 0x44

/* Control/Status bits */
#define IPS_BIT_ILE         0x10
#define IPS_BIT_EI          0x80
#define IPS_BIT_EBM         0x02
#define IPS_BIT_GHI         0x04
#define IPS_BIT_OP          0x01
#define IPS_BIT_RST         0x80
#define IPS_BIT_SEM         0x08
#define IPS_BIT_START_CMD   0x101A
#define IPS_BIT_START_STOP  0x0002
#define IPS_BIT_SCE         0x01
#define IPS_BIT_SQO         0x02
#define IPS_BIT_I2O_OPQI    0x08
#define IPS_BIT_I960_MSG0I  0x01
#define IPS_BIT_I960_MSG1I  0x02

#define IPS_GOOD_POST_STATUS 0x80
#define IPS_OS_LINUX        0x07

/* Command opcodes (inferred from driver) */
#define IPS_CMD_ENQUIRY          0x05
#define IPS_CMD_READ_CONF        0x04
#define IPS_CMD_GET_SUBSYS       0x0A
#define IPS_CMD_RW_NVRAM_PAGE    0x0B
#define IPS_CMD_FFDC             0x0C
#define IPS_CMD_CONFIG_SYNC      0x0D
#define IPS_CMD_ERROR_TABLE      0x0E
#define IPS_CMD_FLUSH            0x12
#define IPS_CMD_GET_LD_INFO      0x11

/* Basic status masks */
#define IPS_GSC_STATUS_MASK      0x0F
#define IPS_CMD_SUCCESS          0x00
#define IPS_CMD_RECOVERED_ERROR  0x01
#define IPS_CMD_CMPLT_WERROR     0x0E
#define IPS_BASIC_STATUS_MASK    0xFF

/* NVRAM constants */
#define IPS_NVRAM_P5_SIG         0xFF55AA00  /* example signature */
#define IPS_MAX_POST_BYTES       2
#define IPS_MAX_CONFIG_BYTES     64

/* Dummy structures for command handling */
typedef struct {
    uint32_t cccr;
    uint32_t ccsar;
    uint8_t op_code;
    uint8_t reserved1[3];
    uint32_t command_id;
    uint32_t reserved2[3];
    uint32_t buffer_addr;
} __attribute__((packed)) IPSCommand;

typedef struct {
    uint32_t value;
} IPSStatus;

/* Simplified IPS_ENQ structure (fields accessed by driver) */
typedef struct {
    uint8_t ucMaxPhysicalDevices;
    uint8_t ucLogDriveCount;
    uint8_t padding1[6];
    uint32_t ulDriveSize[16];
    uint8_t CodeBlkVersion[8];
    uint8_t BootBlkVersion[8];
    uint8_t ucMiscFlag;
    uint8_t padding2[7];
    uint8_t ucConcurrentCmdCount;
    uint8_t padding3[255];
} __attribute__((packed)) IPS_ENQ;

/* Simplified IPS_CONF */
typedef struct {
    uint8_t ucLogDriveCount;
    uint8_t padding1[3];
    uint8_t logical_drive[16][4]; /* StripeSize in last byte? */
    uint8_t init_id[4];
    uint8_t padding2[256 - 4 - 64 - 4];
} __attribute__((packed)) IPS_CONF;

/* Simplified IPS_SUBSYS */
typedef struct {
    uint32_t param[8];
} IPS_SUBSYS;

/* Simplified IPS_NVRAM_P5 */
typedef struct {
    uint32_t signature;
    uint16_t adapter_type;
    uint8_t adapter_slot;
    uint8_t reserved1[1];
    char bios_high[4];
    char bios_low[4];
    uint8_t operating_system;
    uint8_t reserved2[3];
    char driver_high[4];
    char driver_low[4];
    uint8_t versioning;
    uint8_t adapter_order[1]; /* dummy */
} __attribute__((packed)) IPS_NVRAM_P5;

/* BAR types */
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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Hardware Register Shadows */
    uint8_t regs[256];

    /* State for POST and initialization */
    uint8_t post_bytes[IPS_MAX_POST_BYTES];
    int post_index;
    int post_done;
    int config_index;
    int config_done;

    /* Command processing */
    uint32_t ccsar;
    uint32_t cccr;
    bool cmd_in_progress;

    /* Status queue */
    uint32_t sq_tail;
    uint32_t sq_start;
    uint32_t sq_end;

    /* NVRAM */
    uint8_t nvram[256];

    /* Flash simulation */
    uint32_t flash_addr;

    /* Interrupt state */
    bool irq_raised;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool intx = (s->regs[IPS_REG_HISR] & IPS_BIT_SCE) && !s->cmd_in_progress;
    if (intx && !s->irq_raised) {
        pci_set_irq(pdev, 1);
        s->irq_raised = true;
    } else if (!intx && s->irq_raised) {
        pci_set_irq(pdev, 0);
        s->irq_raised = false;
    }
}

static void pcibase_post_status(PCIBaseState *s, uint8_t basic_status,
                                uint8_t ext_status, uint8_t cmd_id)
{
    uint32_t status = basic_status | (ext_status << 8) | (cmd_id << 16);
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->sq_tail) {
        pci_dma_write(pdev, s->sq_tail, &status, sizeof(status));
    }
    s->regs[IPS_REG_HISR] |= IPS_BIT_SCE;
    pcibase_update_irq(s);
}

static void pcibase_execute_command(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->ccsar == 0) {
        return;
    }

    IPSCommand cmd;
    pci_dma_read(pdev, s->ccsar, &cmd, sizeof(cmd));

    uint8_t op = cmd.op_code;
    uint8_t cmd_id = cmd.command_id & 0xFF;
    uint32_t buffer = cmd.buffer_addr;

    /* Default response: success */
    uint8_t basic_status = IPS_CMD_SUCCESS;
    uint8_t ext_status = 0;

    switch (op) {
    case IPS_CMD_ENQUIRY: {
        IPS_ENQ enq;
        memset(&enq, 0, sizeof(enq));
        enq.ucMaxPhysicalDevices = 2;
        enq.ucLogDriveCount = 0;
        enq.ucMiscFlag = 0;
        memcpy(enq.CodeBlkVersion, "1.00    ", 8);
        memcpy(enq.BootBlkVersion, "1.00    ", 8);
        enq.ucConcurrentCmdCount = 32;
        if (buffer) {
            pci_dma_write(pdev, buffer, &enq, sizeof(enq));
        }
        break;
    }
    case IPS_CMD_READ_CONF: {
        IPS_CONF conf;
        memset(&conf, 0, sizeof(conf));
        conf.ucLogDriveCount = 0;
        for (int i = 0; i < 4; i++) {
            conf.init_id[i] = 7;
        }
        if (buffer) {
            pci_dma_write(pdev, buffer, &conf, sizeof(conf));
        }
        break;
    }
    case IPS_CMD_GET_SUBSYS: {
        IPS_SUBSYS subsys;
        memset(&subsys, 0, sizeof(subsys));
        if (buffer) {
            pci_dma_write(pdev, buffer, &subsys, sizeof(subsys));
        }
        break;
    }
    case IPS_CMD_RW_NVRAM_PAGE: {
        /* Write case not fully emulated; just return success if read */
        if (buffer) {
            /* For simplicity, we always copy NVRAM page 5 (offset 0x80*5 = 0x280) */
            pci_dma_write(pdev, buffer, s->nvram + 0x280, sizeof(IPS_NVRAM_P5));
        }
        break;
    }
    case IPS_CMD_FFDC:
    case IPS_CMD_CONFIG_SYNC:
    case IPS_CMD_ERROR_TABLE:
    case IPS_CMD_FLUSH:
        /* No action needed */
        break;
    default:
        break;
    }

    pcibase_post_status(s, basic_status, ext_status, cmd_id);
}

static uint64_t pcibase_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t val = 0;

    /* Handle byte accesses */
    if (size == 1) {
        switch (addr) {
        case IPS_REG_HISR:
            val = s->regs[addr];
            break;
        case IPS_REG_ISPR:
            if (!s->post_done) {
                val = s->post_bytes[s->post_index];
                s->post_index++;
                if (s->post_index >= IPS_MAX_POST_BYTES) {
                    s->post_done = 1;
                }
            } else {
                val = 0; /* After post, reads return 0 */
            }
            break;
        case IPS_REG_FLDP:
            /* Simulate flash data read */
            if (s->flash_addr == 0) {
                val = 0x55;
            } else if (s->flash_addr == 1) {
                val = 0xAA;
            } else if (s->flash_addr == 0x1FF) {
                val = 0x01; /* major */
            } else if (s->flash_addr == 0x1FE) {
                val = 0x00; /* minor */
            } else if (s->flash_addr == 0x1FD) {
                val = 0x00; /* subminor */
            } else {
                val = 0xFF;
            }
            break;
        case IPS_REG_CBSP:
            /* OP bit is 0 when post and config done */
            if (s->post_done && s->config_done) {
                val = 0x00;
            } else {
                val = IPS_BIT_OP;
            }
            break;
        default:
            val = s->regs[addr];
            break;
        }
    } else if (size == 4) {
        switch (addr) {
        case IPS_REG_CCCR:
            val = s->cccr;
            break;
        case IPS_REG_FLAP:
            val = s->flash_addr;
            break;
        default:
            val = le32_to_cpu(*(uint32_t *)(s->regs + addr));
            break;
        }
    }

    return val;
}

static void pcibase_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    if (size == 1) {
        switch (addr) {
        case IPS_REG_HISR:
            /* Clear GHI if it was set, and advance to next post/config byte */
            if (val & IPS_BIT_GHI) {
                s->regs[addr] &= ~IPS_BIT_GHI;
                if (!s->post_done) {
                    /* Set GHI again for next post byte if available */
                    s->regs[addr] |= IPS_BIT_GHI;
                } else if (!s->config_done) {
                    /* In config phase, GHI is already managed */
                }
            }
            if (val & IPS_BIT_SQO) {
                s->regs[addr] &= ~IPS_BIT_SQO;
            }
            if (val & IPS_BIT_SCE) {
                s->regs[addr] &= ~IPS_BIT_SCE;
                pcibase_update_irq(s);
            }
            break;
        case IPS_REG_SCPR:
            s->regs[addr] = val;
            break;
        case IPS_REG_FLDP:
            /* Flash write (ignore) */
            break;
        default:
            s->regs[addr] = val;
            break;
        }
    } else if (size == 4) {
        switch (addr) {
        case IPS_REG_CCSAR:
            s->ccsar = val;
            break;
        case IPS_REG_CCCR:
            s->cccr = val;
            if (val == IPS_BIT_START_CMD || val == 0x1010) {
                /* Start command execution */
                pcibase_execute_command(s);
            }
            break;
        case IPS_REG_FLAP:
            s->flash_addr = val;
            break;
        case IPS_REG_SQSR:
            s->sq_start = val;
            s->sq_tail = val;
            *(uint32_t *)(s->regs + addr) = cpu_to_le32(val);
            break;
        case IPS_REG_SQER:
            s->sq_end = val;
            *(uint32_t *)(s->regs + addr) = cpu_to_le32(val);
            break;
        case IPS_REG_SQHR:
            *(uint32_t *)(s->regs + addr) = cpu_to_le32(val);
            break;
        case IPS_REG_SQTR:
            s->sq_tail = val;
            *(uint32_t *)(s->regs + addr) = cpu_to_le32(val);
            break;
        default:
            *(uint32_t *)(s->regs + addr) = cpu_to_le32(val);
            break;
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_io_read,
    .write = pcibase_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_io_read,
    .write = pcibase_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    s->post_index = 0;
    s->post_done = 0;
    s->config_index = 0;
    s->config_done = 0;
    s->ccsar = 0;
    s->cccr = 0;
    s->cmd_in_progress = false;
    s->sq_tail = 0;
    s->sq_start = 0;
    s->sq_end = 0;
    s->irq_raised = false;
    s->flash_addr = 0;

    /* Initialize POST bytes */
    s->post_bytes[0] = 0x80;
    s->post_bytes[1] = 0x00;

    /* Set GHI in HISR to signal first post byte available */
    s->regs[IPS_REG_HISR] = IPS_BIT_GHI;

    /* Initialize NVRAM page 5 with some valid data for driver */
    IPS_NVRAM_P5 *nv = (IPS_NVRAM_P5 *)(s->nvram + 0x280);
    memset(nv, 0, sizeof(*nv));
    nv->signature = cpu_to_le32(IPS_NVRAM_P5_SIG);
    nv->adapter_type = 0; /* will be filled by driver */
    nv->adapter_slot = 0;
    nv->operating_system = IPS_OS_LINUX;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,   0x1014);
    pci_set_word(pci_conf + PCI_DEVICE_ID,   0x002E);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0104);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR 0: Memory-mapped registers (size to cover all offsets) */
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 4096;
    s->bar_info[0].name  = "ips-mmio";

    /* BAR 1: I/O ports */
    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_PIO;
    s->bar_info[1].size  = 256;
    s->bar_info[1].name  = "ips-io";

    s->num_bars = 2;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize NVRAM with dummy but valid signature */
    memset(s->nvram, 0, sizeof(s->nvram));
    IPS_NVRAM_P5 *nv = (IPS_NVRAM_P5 *)(s->nvram + 0x280);
    nv->signature = cpu_to_le32(IPS_NVRAM_P5_SIG);
    nv->adapter_type = 0;
    nv->adapter_slot = 0;
    nv->operating_system = IPS_OS_LINUX;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No MSI/MSI-X cleanup needed */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "ips_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(regs, PCIBaseState, 256),
        VMSTATE_UINT8_ARRAY(post_bytes, PCIBaseState, IPS_MAX_POST_BYTES),
        VMSTATE_INT32(post_index, PCIBaseState),
        VMSTATE_INT32(post_done, PCIBaseState),
        VMSTATE_INT32(config_index, PCIBaseState),
        VMSTATE_INT32(config_done, PCIBaseState),
        VMSTATE_UINT32(ccsar, PCIBaseState),
        VMSTATE_UINT32(cccr, PCIBaseState),
        VMSTATE_BOOL(cmd_in_progress, PCIBaseState),
        VMSTATE_UINT32(sq_tail, PCIBaseState),
        VMSTATE_UINT32(sq_start, PCIBaseState),
        VMSTATE_UINT32(sq_end, PCIBaseState),
        VMSTATE_UINT8_ARRAY(nvram, PCIBaseState, 256),
        VMSTATE_UINT32(flash_addr, PCIBaseState),
        VMSTATE_BOOL(irq_raised, PCIBaseState),
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
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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
