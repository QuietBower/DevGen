/*
 * QEMU PCI device model for sja1000 plx_pci-based CAN cards
 * Auto-generated to match linux/drivers/net/can/sja1000/plx_pci.c
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

#include "hw/irq.h"

#define TYPE_PCIBASE_DEVICE "sja1000_plx_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define ADLINK_PCI_VENDOR_ID        0x144A
#define ADLINK_PCI_DEVICE_ID        0x7841

#define PLX_PCI_MAX_CHAN 2
#define PLX_PCI_CAN_CLOCK (16000000 / 2)
#define PLX_INTCSR   0x4c
#define PLX_CNTRL    0x50
#define PLX_LINT1_EN 0x1
#define PLX_LINT1_POL    (1 << 1)
#define PLX_LINT2_EN (1 << 3)
#define PLX_LINT2_POL    (1 << 4)
#define PLX_PCI_INT_EN   (1 << 6)
#define PLX_PCI_RESET    (1 << 30)
#define PLX9056_INTCSR   0x68
#define PLX9056_CNTRL    0x6c
#define PLX9056_LINTI    (1 << 11)
#define PLX9056_PCI_INT_EN   (1 << 8)
#define PLX9056_PCI_RCR  (1 << 29)
#define OCR_TX0_PUSHPULL  0x18
#define OCR_TX1_PUSHPULL  0xc0
#define CDR_CBP        0x40
#define CDR_CLKOUT_MASK 0x07
#define UL(x)          (_UL(x))
#define BIT(nr)            (UL(1) << (nr))
#define PLX_PCI_OCR  (OCR_TX0_PUSHPULL | OCR_TX1_PUSHPULL)
#define ASEM_PCI_OCR 0xfe
#define PLX_PCI_CDR         (CDR_CBP | CDR_CLKOUT_MASK)
#define REG_CR_BASICCAN_INITIAL     0x21
#define REG_CR_BASICCAN_INITIAL_MASK    0xa1
#define REG_SR_BASICCAN_INITIAL     0x0c
#define REG_IR_BASICCAN_INITIAL     0xe0
#define REG_MOD_PELICAN_INITIAL     0x01
#define REG_SR_PELICAN_INITIAL      0x3c
#define REG_IR_PELICAN_INITIAL      0x00
#define ESD_PCI_SUB_SYS_ID_PCI200   0x0004
#define ESD_PCI_SUB_SYS_ID_PCI266   0x0009
#define ESD_PCI_SUB_SYS_ID_PMC266   0x000e
#define ESD_PCI_SUB_SYS_ID_CPCI200  0x010b
#define ESD_PCI_SUB_SYS_ID_PCIE2000 0x0200
#define ESD_PCI_SUB_SYS_ID_PCI104200    0x0501
#define CAN200PCI_DEVICE_ID     0x9030
#define CAN200PCI_VENDOR_ID     0x10b5
#define CAN200PCI_SUB_DEVICE_ID     0x0301
#define CAN200PCI_SUB_VENDOR_ID     0xe1c5
#define IXXAT_PCI_VENDOR_ID     0x10b5
#define IXXAT_PCI_DEVICE_ID     0x9050
#define IXXAT_PCI_SUB_SYS_ID    0x2540
#define MARATHON_PCI_DEVICE_ID      0x2715
#define MARATHON_PCIE_DEVICE_ID     0x3432
#define TEWS_PCI_VENDOR_ID      0x1498
#define TEWS_PCI_DEVICE_ID_TMPC810  0x032A
#define CTI_PCI_DEVICE_ID_CRG001    0x0900
#define MOXA_PCI_VENDOR_ID      0x1393
#define MOXA_PCI_DEVICE_ID      0x0100
#define ASEM_RAW_CAN_VENDOR_ID      0x10b5
#define ASEM_RAW_CAN_DEVICE_ID      0x9030
#define ASEM_RAW_CAN_SUB_VENDOR_ID  0x3000
#define ASEM_RAW_CAN_SUB_DEVICE_ID  0x1001
#define ASEM_RAW_CAN_SUB_DEVICE_ID_BIS  0x1002
#define ASEM_RAW_CAN_RST_REGISTER   0x54
#define ASEM_RAW_CAN_RST_MASK_CAN1  0x20
#define ASEM_RAW_CAN_RST_MASK_CAN2  0x04
#define MARATHON_PCIE_RESET_OFFSET 32
#define SJA1000_CDR     0x1F
#define SJA1000_IR      0x03
#define SJA1000_MOD     0x00
#define SJA1000_SR      0x02
#define SJA1000_ECHO_SKB_MAX   1
#define MOD_RM      0x01
#define IRQ_OFF     0x00
#define SJA1000_IER     0x04
#define SJA1000_ACCM3   0x17
#define SJA1000_ACCM0   0x14
#define SJA1000_ACCM2   0x16
#define SJA1000_ACCC1   0x11
#define SJA1000_OCR     0x08
#define SJA1000_ACCC0   0x10
#define SJA1000_QUIRK_NO_CDR_REG BIT(1)
#define SJA1000_ACCC2   0x12
#define SJA1000_ACCM1   0x15
#define SJA1000_ACCC3   0x13
#define SJA1000_BTR1    0x07
#define SJA1000_BTR0    0x06
#define SJA1000_TXERR   0x0F
#define SJA1000_RXERR   0x0E
#define SJA1000_CUSTOM_IRQ_HANDLER BIT(0)
#define SJA1000_ID3     0x13
#define SJA1000_SFF_BUF 0x13
#define CMD_SRR     0x10
#define CMD_AT      0x02
#define SJA1000_FI      0x10
#define SJA1000_FI_RTR  0x40
#define SJA1000_FI_FF   0x80
#define SJA1000_ID1     0x11
#define SJA1000_EFF_BUF 0x15
#define SJA1000_ID4     0x14
#define CMD_TR      0x01
#define SJA1000_ID2     0x12
#define SJA1000_ECC     0x0C
#define SJA1000_CMR     0x01
#define MOD_STM     0x04
#define IRQ_BEI     0x80
#define MOD_LOM     0x02
#define IRQ_ALL     0xFF
#define REG_CR              0x00

#define PCIBASE_VENDOR_ID ADLINK_PCI_VENDOR_ID
#define PCIBASE_DEVICE_ID ADLINK_PCI_DEVICE_ID
#define PCIBASE_CLASS_ID  PCI_CLASS_NETWORK_OTHER

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

    /* Configuration space shadow (PLX bridge registers) */
    uint32_t plx_regs[0x1000 / 4];

    /* Two SJA1000 channels mapped into BAR0: offsets 0x80 and 0x100 */
    uint8_t sja_regs[PLX_PCI_MAX_CHAN][0x80];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intr_status & s->intr_mask) != 0;

    if (msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

static uint32_t pcibase_mmio_read32(PCIBaseState *s, hwaddr addr)
{
    if (addr + 4 <= 0x1000) {
        uint32_t v = s->plx_regs[addr >> 2];
        switch (addr) {
        case PLX_INTCSR:
            return v;
        case PLX_CNTRL:
            return v;
        case PLX9056_INTCSR:
            return v;
        case PLX9056_CNTRL:
            return v;
        default:
            return v;
        }
    }
    return 0;
}

static void pcibase_mmio_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr + 4 <= 0x1000) {
        switch (addr) {
        case PLX_INTCSR:
            s->plx_regs[addr >> 2] = val;
            s->intr_mask = val;
            pcibase_update_irq(s);
            break;
        case PLX_CNTRL:
            s->plx_regs[addr >> 2] = val;
            break;
        case PLX9056_INTCSR:
            s->plx_regs[addr >> 2] = val;
            s->intr_mask = val;
            pcibase_update_irq(s);
            break;
        case PLX9056_CNTRL:
            s->plx_regs[addr >> 2] = val;
            break;
        default:
            s->plx_regs[addr >> 2] = val;
            break;
        }
    }
}

static uint8_t pcibase_sja_read(PCIBaseState *s, int chan, hwaddr offset)
{
    if (chan < 0 || chan >= PLX_PCI_MAX_CHAN) {
        return 0xff;
    }
    if (offset >= 0x80) {
        return 0xff;
    }
    return s->sja_regs[chan][offset];
}

static void pcibase_sja_write(PCIBaseState *s, int chan, hwaddr offset, uint8_t val)
{
    if (chan < 0 || chan >= PLX_PCI_MAX_CHAN) {
        return;
    }
    if (offset >= 0x80) {
        return;
    }
    s->sja_regs[chan][offset] = val;

    if (offset == SJA1000_CDR) {
        if (val & 0x80) {
            s->sja_regs[chan][SJA1000_MOD] = REG_MOD_PELICAN_INITIAL;
            s->sja_regs[chan][SJA1000_SR] = REG_SR_PELICAN_INITIAL;
            s->sja_regs[chan][SJA1000_IR] = REG_IR_PELICAN_INITIAL;
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x1000) {
        if (size == 4) {
            val = pcibase_mmio_read32(s, addr & ~0x3ULL);
        } else if (size == 1) {
            uint32_t v32 = pcibase_mmio_read32(s, addr & ~0x3ULL);
            int shift = (addr & 3) * 8;
            val = (v32 >> shift) & 0xff;
        }
        return val;
    }

    if (addr >= 0x80 && addr < 0x80 + 0x80) {
        int chan = 0;
        hwaddr off = addr - 0x80;
        if (size == 1) {
            val = pcibase_sja_read(s, chan, off);
        }
        return val;
    }

    if (addr >= 0x100 && addr < 0x100 + 0x80) {
        int chan = 1;
        hwaddr off = addr - 0x100;
        if (size == 1) {
            val = pcibase_sja_read(s, chan, off);
        }
        return val;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x1000) {
        if (size == 4) {
            pcibase_mmio_write32(s, addr & ~0x3ULL, (uint32_t)val);
        } else if (size == 1) {
            uint32_t v32 = pcibase_mmio_read32(s, addr & ~0x3ULL);
            int shift = (addr & 3) * 8;
            v32 &= ~(0xffu << shift);
            v32 |= ((uint32_t)(val & 0xff) << shift);
            pcibase_mmio_write32(s, addr & ~0x3ULL, v32);
        }
        return;
    }

    if (addr >= 0x80 && addr < 0x80 + 0x80) {
        int chan = 0;
        hwaddr off = addr - 0x80;
        if (size == 1) {
            pcibase_sja_write(s, chan, off, (uint8_t)val);
        }
        return;
    }

    if (addr >= 0x100 && addr < 0x100 + 0x80) {
        int chan = 1;
        hwaddr off = addr - 0x100;
        if (size == 1) {
            pcibase_sja_write(s, chan, off, (uint8_t)val);
        }
        return;
    }
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->plx_regs, 0, sizeof(s->plx_regs));
    memset(s->sja_regs, 0, sizeof(s->sja_regs));

    s->intr_status = 0;
    s->intr_mask = 0;

    for (int ch = 0; ch < PLX_PCI_MAX_CHAN; ch++) {
        s->sja_regs[ch][REG_CR] = REG_CR_BASICCAN_INITIAL;
        s->sja_regs[ch][SJA1000_SR] = REG_SR_BASICCAN_INITIAL;
        s->sja_regs[ch][SJA1000_IR] = REG_IR_BASICCAN_INITIAL;
    }

    pcibase_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 0;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000;
    s->bar_info[0].name  = "plx_pci_bar0";
    s->num_bars = 1;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    pcibase_reset(DEVICE(pdev));
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

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "sja1000_plx_pci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
        VMSTATE_UINT32_ARRAY(plx_regs, PCIBaseState, 0x1000 / 4),
        VMSTATE_UINT8_2DARRAY(sja_regs, PCIBaseState, PLX_PCI_MAX_CHAN, 0x80),
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
