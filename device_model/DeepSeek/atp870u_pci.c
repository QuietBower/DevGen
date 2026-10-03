/* ATP870U SCSI Controller Emulation (QEMU 8.2.10 PCI device model).
 * Based on driver analysis: /home/eely/linux-7.1/drivers/scsi/atp870u.c
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "atp870u_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define VENDOR_ID 0x1191
#define DEVICE_ID 0x808A
#define CLASS_ID PCI_CLASS_STORAGE_SCSI

/* Register Offsets (IO Port BAR offset) extracted from driver init functions */
#define REG_1F  0x1F
#define REG_22  0x22
#define REG_28  0x28
#define REG_29  0x29
#define REG_30  0x30
#define REG_31  0x31
#define REG_32  0x32
#define REG_33  0x33
#define REG_34  0x34
#define REG_35  0x35
#define REG_38  0x38
#define REG_39  0x39
#define REG_3B  0x3B
#define REG_3C  0x3C
#define REG_3F  0x3F
#define REG_40  0x40
#define REG_50  0x50
#define REG_80  0x80
#define REG_C0  0xC0

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

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Device IO registers (flat 256 bytes) */
    uint8_t io_regs[256];

    /* Command/DMA state */
    QEMUTimer *cmd_timer;
    bool cmd_pending;
    uint32_t prd_addr;
    bool dma_direction; /* true = to device, false = from device */
};

/* Timer callback: completes a SCSI command */
static void atp870u_cmd_complete(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t opcode = s->io_regs[0x03];
    uint32_t xfer_len = s->io_regs[0x14] | (s->io_regs[0x13] << 8) | (s->io_regs[0x12] << 16);
    bool dma_needed = (s->prd_addr != 0) && (!s->dma_direction) && (xfer_len > 0);

    if (dma_needed && (opcode == 0x12 || opcode == 0x03)) {
        /* Prepare data buffer */
        uint8_t buf[96];
        memset(buf, 0, sizeof(buf));
        if (opcode == 0x12) {
            /* INQUIRY: peripheral qualifier 011b (not capable) */
            buf[0] = 0x7f;
        } else {
            /* REQUEST SENSE: return NO SENSE */
            buf[0] = 0x70; /* valid */
            buf[2] = 0;    /* sense key = NO SENSE */
            buf[7] = 14;   /* additional sense length */
        }

        /* Write data via PRD table */
        dma_addr_t prd_phys = le32_to_cpu(*(uint32_t *)&s->io_regs[0x24]);
        uint64_t total_copied = 0;
        while (total_copied < xfer_len) {
            uint8_t prd[8];
            pci_dma_read(pdev, prd_phys, prd, 8);
            uint32_t addr = le32_to_cpu(*(uint32_t *)&prd[0]);
            uint16_t len   = le16_to_cpu(*(uint16_t *)&prd[4]);
            uint16_t flags = le16_to_cpu(*(uint16_t *)&prd[6]);
            if (len > xfer_len - total_copied) {
                len = xfer_len - total_copied;
            }
            if (len > 0) {
                pci_dma_write(pdev, addr, buf + total_copied, len);
                total_copied += len;
            }
            if (flags & 0x8000) {
                break;
            }
            prd_phys += 8;
        }
        s->io_regs[0x17] = 0x16;  /* command complete */
        s->io_regs[0x0f] = 0x00;  /* SAM_STAT_GOOD */
    } else {
        /* Non-DMA command: complete with status that skips target */
        s->io_regs[0x17] = 0x00;
        s->io_regs[0x0f] = 0x02;  /* SAM_STAT_CHECK_CONDITION (fallback) */
    }

    s->io_regs[0x1f] |= 0x80;      /* set completion bit */
    pci_set_irq(pdev, 1);          /* raise IRQ */
    s->cmd_pending = false;
}

/* PIO read handler */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr >= 0x100) {
        return ~0ULL;
    }
    switch (size) {
    case 1:
        val = s->io_regs[addr];
        break;
    case 2:
        if (addr + 1 < 0x100) {
            val = s->io_regs[addr] | (s->io_regs[addr + 1] << 8);
        }
        break;
    case 4:
        if (addr + 3 < 0x100) {
            val = s->io_regs[addr] | (s->io_regs[addr + 1] << 8) |
                  (s->io_regs[addr + 2] << 16) | (s->io_regs[addr + 3] << 24);
        }
        break;
    default:
        val = 0;
    }
    return val;
}

/* PIO write handler */
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x100) {
        return;
    }

    switch (size) {
    case 1:
        s->io_regs[addr] = val;
        break;
    case 2:
        if (addr + 1 < 0x100) {
            s->io_regs[addr] = val & 0xff;
            s->io_regs[addr + 1] = (val >> 8) & 0xff;
        }
        break;
    case 4:
        if (addr + 3 < 0x100) {
            s->io_regs[addr] = val & 0xff;
            s->io_regs[addr + 1] = (val >> 8) & 0xff;
            s->io_regs[addr + 2] = (val >> 16) & 0xff;
            s->io_regs[addr + 3] = (val >> 24) & 0xff;
        }
        break;
    }

    /* Special command handling */
    if (addr == 0x18) {
        /* Command trigger: schedule completion */
        if (!s->cmd_pending) {
            s->cmd_pending = true;
            timer_mod(s->cmd_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000); /* 1 ms */
        }
    } else if (addr == 0x20) {  /* pciport+0 */
        if (val == 0) {
            /* Clear interrupt */
            s->io_regs[0x1f] &= ~0x80;
            pci_set_irq(PCI_DEVICE(s), 0);
            if (s->cmd_pending) {
                timer_del(s->cmd_timer);
                s->cmd_pending = false;
            }
        } else {
            /* Start DMA / command */
            s->dma_direction = (val & 0x09) ? false : true;  /* 0x09 = from device */
            if (!s->cmd_pending) {
                s->cmd_pending = true;
            }
            timer_mod(s->cmd_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
        }
    } else if (addr == 0x24 && size == 4) {
        s->prd_addr = val;  /* PRD address */
    }

    /* No action for other registers */
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

    memset(s->io_regs, 0, sizeof(s->io_regs));
    s->prd_addr = 0;
    s->dma_direction = false;
    s->cmd_pending = false;

    /* Initialize some registers to make driver init paths succeed */
    s->io_regs[0x2d] = 0x00;  /* global_map low */
    s->io_regs[0x2e] = 0x00;  /* ultra_map low */
    s->io_regs[0x2f] = 0x00;  /* ultra_map high */
    s->io_regs[0x22] = 0x00;  /* pciport+2 (scam_on control) */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
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
    pci_conf[0x49] = 0x07;  /* host ID */

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "io";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize command completion timer */
    s->cmd_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, atp870u_cmd_complete, s);

    /* Final state */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(s->cmd_timer);
    timer_free(s->cmd_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "atp870u_pci",
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
