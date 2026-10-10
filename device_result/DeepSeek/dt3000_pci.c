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

#define TYPE_PCIBASE_DEVICE "dt3000_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* #define VENDOR_ID 0x1116 */
/* #define DEVICE_ID 0x0022 */
/* #define CLASS_ID  0x118000 */

/* Register offsets (byte offsets) */
#define DPR_DAC_BUFFER     (4 * 0x000)  /* 0x0000 */
#define DPR_ADC_BUFFER     (4 * 0x800)  /* 0x2000 */
#define DPR_COMMAND        (4 * 0xfd3)  /* 0x3F4C */
#define DPR_SUBSYS         (4 * 0xfd3)  /* 0x3F4C */
#define DPR_SUBSYS_AI      0
#define DPR_SUBSYS_AO      1
#define DPR_SUBSYS_DIN     2
#define DPR_SUBSYS_DOUT    3
#define DPR_SUBSYS_MEM     4
#define DPR_SUBSYS_CT      5
#define DPR_ENCODE          (4 * 0xfd4)  /* 0x3F50 */
#define DPR_PARAMS(x)       (4 * (0xfd5 + (x)))  /* 0x3F54 + 4*x */
#define DPR_TICK_REG_LO     (4 * 0xff5)  /* 0x3FD4 */
#define DPR_TICK_REG_HI     (4 * 0xff6)  /* 0x3FD8 */
#define DPR_DA_BUF_FRONT    (4 * 0xff7)  /* 0x3FDC */
#define DPR_DA_BUF_REAR     (4 * 0xff8)  /* 0x3FE0 */
#define DPR_AD_BUF_FRONT    (4 * 0xff9)  /* 0x3FE4 */
#define DPR_AD_BUF_REAR     (4 * 0xffa)  /* 0x3FE8 */
#define DPR_INT_MASK        (4 * 0xffb)  /* 0x3FEC */
#define DPR_INTR_FLAG       (4 * 0xffc)  /* 0x3FF0 */
#define DPR_RESPONSE_MBX    (4 * 0xffe)  /* 0x3FF8 */
#define DPR_CMD_MBX         (4 * 0xfff)  /* 0x3FFC */

/* Interrupt flag bits */
#define DPR_INTR_CMDONE     BIT(7)
#define DPR_INTR_CTDONE     BIT(6)
#define DPR_INTR_DAHWERR    BIT(5)
#define DPR_INTR_DASWERR    BIT(4)
#define DPR_INTR_DAEMPTY    BIT(3)
#define DPR_INTR_ADHWERR    BIT(2)
#define DPR_INTR_ADSWERR    BIT(1)
#define DPR_INTR_ADFULL     BIT(0)

/* Command mailbox definitions */
#define DPR_CMD_COMPLETION(x)    ((x) << 8)
#define DPR_CMD_NOTPROCESSED     DPR_CMD_COMPLETION(0x00)
#define DPR_CMD_NOERROR          DPR_CMD_COMPLETION(0x55)
#define DPR_CMD_ERROR            DPR_CMD_COMPLETION(0xaa)
#define DPR_CMD_NOTSUPPORTED     DPR_CMD_COMPLETION(0xff)
#define DPR_CMD_COMPLETION_MASK  DPR_CMD_COMPLETION(0xff)
#define DPR_CMD(x)               ((x) << 0)
#define DPR_CMD_GETBRDINFO       DPR_CMD(0)
#define DPR_CMD_CONFIG           DPR_CMD(1)
#define DPR_CMD_GETCONFIG        DPR_CMD(2)
#define DPR_CMD_START            DPR_CMD(3)
#define DPR_CMD_STOP             DPR_CMD(4)
#define DPR_CMD_READSINGLE       DPR_CMD(5)
#define DPR_CMD_WRITESINGLE      DPR_CMD(6)
#define DPR_CMD_CALCCLOCK        DPR_CMD(7)
#define DPR_CMD_READEVENTS       DPR_CMD(8)
#define DPR_CMD_WRITECTCTRL      DPR_CMD(16)
#define DPR_CMD_READCTCTRL       DPR_CMD(17)
#define DPR_CMD_WRITECT          DPR_CMD(18)
#define DPR_CMD_READCT           DPR_CMD(19)
#define DPR_CMD_WRITEDATA        DPR_CMD(32)
#define DPR_CMD_READDATA         DPR_CMD(33)
#define DPR_CMD_WRITEIO          DPR_CMD(34)
#define DPR_CMD_READIO           DPR_CMD(35)
#define DPR_CMD_WRITECODE        DPR_CMD(36)
#define DPR_CMD_READCODE         DPR_CMD(37)
#define DPR_CMD_EXECUTE          DPR_CMD(38)
#define DPR_CMD_HALT             DPR_CMD(48)
#define DPR_CMD_MASK             DPR_CMD(0xff)

/* Parameter 5 bits for analog trigger (unused in QEMU) */
#define DPR_PARAM5_AD_TRIG(x)          (((x) & 0x7) << 2)
#define DPR_PARAM5_AD_TRIG_INT         DPR_PARAM5_AD_TRIG(0)
#define DPR_PARAM5_AD_TRIG_EXT         DPR_PARAM5_AD_TRIG(1)
#define DPR_PARAM5_AD_TRIG_INT_RETRIG  DPR_PARAM5_AD_TRIG(2)
#define DPR_PARAM5_AD_TRIG_EXT_RETRIG  DPR_PARAM5_AD_TRIG(3)
#define DPR_PARAM5_AD_TRIG_INT_RETRIG2 DPR_PARAM5_AD_TRIG(4)

#define DPR_PARAM6_AD_DIFF             BIT(0)

#define DPR_AI_FIFO_DEPTH              2003
#define DPR_AO_FIFO_DEPTH              2048
#define DPR_EXTERNAL_CLOCK             1
#define DPR_RISING_EDGE                2
#define DPR_TMODE_MASK                 0x1c
#define DPR_CMD_TIMEOUT                100

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

    /* 16KB MMIO register space, 16-bit word access only */
    uint16_t regs16[0x2000];  /* 0x4000 bytes */

    /* Memory subdevice: 4KB of 8-bit RAM, accessed via READCODE/WRITECODE */
    uint8_t mem[0x1000];
};

/* Forward declaration */
static void process_command(PCIBaseState *s, uint16_t cmd);

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t flag = s->regs16[DPR_INTR_FLAG >> 1];
    uint16_t mask = s->regs16[DPR_INT_MASK >> 1];
    if (flag & mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 2) {
        /* Drivers only use 16-bit accesses */
        return 0;
    }

    /* Address within BAR0 (0x0000 - 0x3FFF) */
    if (addr >= 0x4000) {
        return 0;
    }

    /* 16-bit aligned access expected */
    val = s->regs16[addr >> 1];
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 2) {
        return;
    }

    if (addr >= 0x4000) {
        return;
    }

    /* Store value into register array */
    s->regs16[addr >> 1] = (uint16_t)val;

    /* Special handling for command mailbox */
    if (addr == DPR_CMD_MBX) {
        process_command(s, (uint16_t)val);
    }

    /* Update IRQ if interrupt mask or flag registers were modified */
    if (addr == DPR_INTR_FLAG || addr == DPR_INT_MASK) {
        pcibase_update_irq(s);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 2, .max_access_size = 2 },
    .impl  = { .min_access_size = 2, .max_access_size = 2 },
};

static void process_command(PCIBaseState *s, uint16_t cmd)
{
    uint16_t response = DPR_CMD_NOERROR;  /* Assume success */
    uint32_t subsys;

    /* Extract the command from low byte */
    uint8_t command = cmd & 0xff;

    /* The subsystem is selected via a previous write to DPR_SUBSYS */
    subsys = s->regs16[DPR_SUBSYS >> 1] & 0x7;

    switch (command) {
    case DPR_CMD(5): /* READSNGLE */
        if (subsys == DPR_SUBSYS_AI) {
            uint16_t chan = s->regs16[DPR_PARAMS(0) >> 1];
            /* Provide a dummy analog input value (e.g., channel * 100) */
            s->regs16[DPR_PARAMS(2) >> 1] = chan * 100;
        } else if (subsys == DPR_SUBSYS_DIN) {
            /* Return digital input state (all zeros for simplicity) */
            s->regs16[DPR_PARAMS(2) >> 1] = 0;
        } else {
            response = DPR_CMD_ERROR;
        }
        break;

    case DPR_CMD(6): /* WRITESINGLE */
        if (subsys == DPR_SUBSYS_AO || subsys == DPR_SUBSYS_DOUT) {
            /* Output channel and data already stored in PARAMS, nothing extra */
        } else {
            response = DPR_CMD_ERROR;
        }
        break;

    case DPR_CMD(1): /* CONFIG */
        /* Configuration commands are always successful for probe */
        break;

    case DPR_CMD(3): /* START */
        if (subsys == DPR_SUBSYS_AI) {
            /* Start conversion; in a full model would trigger timer, but
               for probing we simply acknowledge. */
        } else {
            response = DPR_CMD_ERROR;
        }
        break;

    case DPR_CMD(4): /* STOP */
        break;

    case DPR_CMD(37): /* READCODE */
        {
            uint16_t addr = s->regs16[DPR_PARAMS(0) >> 1];
            /* Count is in PARAMS(1), ignored; driver always reads 1 byte */
            uint8_t val = 0;
            if (addr < 0x1000) {
                val = s->mem[addr];
            }
            s->regs16[DPR_PARAMS(2) >> 1] = val;
        }
        break;

    default:
        response = DPR_CMD_NOTSUPPORTED;
        break;
    }

    /* Set the response in the command mailbox register */
    s->regs16[DPR_CMD_MBX >> 1] = response;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Zero all 16-bit registers */
    memset(s->regs16, 0, sizeof(s->regs16));

    /* Set command mailbox to "not processed" initial state */
    s->regs16[DPR_CMD_MBX >> 1] = DPR_CMD_NOTPROCESSED;

    /* Clear memory subdevice */
    memset(s->mem, 0, sizeof(s->mem));

    /* Lower IRQ */
    pci_set_irq(PCI_DEVICE(s), 0);
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1116);  /* VENDOR_ID */
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0022);  /* DEVICE_ID */
    pci_config_set_class(pci_conf, 0x118000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCI Express capability (for modern detection) */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Power management capability */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 0: 16KB MMIO register space */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x4000,
        .name = "bar0",
    };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "dt3000_pci",
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