/*
 * QEMU PCI Device Model for PCI200SYN (PLX9050-based dual port sync HDLC)
 * Based on driver: drivers/net/wan/pci200syn.c
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
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "PCI200SYN_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define PCI_VENDOR_ID_PLX          0x10b5
#define PCI_DEVICE_ID_PLX_9050     0x9050

/* BAR Sizes */
#define PCI200SYN_PLX_SIZE  0x80
#define PCI200SYN_SCA_SIZE  0x400

/* PLX 9052 Register Offsets (subset of struct plx9052) */
#define PLX_INTR_CTRL_STAT  0x4C
#define PLX_INIT_CTRL       0x50

/* SCA Register Offsets (used by driver) */
#define IER0    0x74
#define EXS     0x13e
#define RXS     0x13c
#define TXS     0x13d
#define MSCI0_OFFSET 0x00
#define MSCI1_OFFSET 0x80
#define CLK_LINE        0x00
#define CLK_TX_RXCLK   0x60
#define CLK_BRG         0x40
#define CLK_BRG_MASK    0x0F
#define CLK_PIN_OUT 0x80
#define TMCT    0x144
#define TMCR    0x145
#define MD0  0x138
#define MD1  0x139
#define MD2 0x13a
#define IDL  0x142
#define CTL  0x130
#define CMD  0x128
#define CST0 0x108
#define CST1 0x109
#define TNR0 0x150
#define TNR1 0x151
#define RNR  0x154
#define TFS  0x14b
#define TCR  0x152
#define ISR0 0x6c
#define ISR1 0x70
#define ST0  0x118
#define ST1  0x119
#define ST2  0x11a
#define ST3  0x11b
#define ST4  0x11c
#define FST  0x11d
#define IE0  0x120
#define IE0_RXINTA  0x40
#define IE0_CDCD    0x00000400
#define EXS_TES1    0x20
#define MD0_HDLC        0x80
#define MD0_CRC_NONE    0x00
#define MD0_CRC_16  0x05
#define MD0_CRC_16_0    0x04
#define MD0_CRC_ITU 0x07
#define MD0_CRC_ITU32   0x06
#define MD2_NRZ     0x00
#define MD2_NRZI    0x20
#define MD2_MANCHESTER 0x80
#define MD2_FM_MARK 0xA0
#define MD2_FM_SPACE    0xC0
#define MD2_LOOPBACK    0x03
#define CTL_IDLE    0x10
#define CTL_URCT    0x80
#define CTL_URSKP   0x40
#define CMD_RESET   0x21
#define CMD_TX_ENABLE    0x02
#define CMD_RX_ENABLE    0x12
#define DSR_RX(chan)    (0x48 + 2*(chan))
#define DSR_TX(chan)    (0x49 + 2*(chan))
#define DSR_DE      0x02

/* DMA-related offsets */
#define DMAC0RX_OFFSET 0x00
#define DMAC0TX_OFFSET 0x20
#define DMAC1RX_OFFSET 0x40
#define DMAC1TX_OFFSET 0x60
#define ILAR 0x00
#define CDAL 0x84
#define EDAL 0x88
#define BFLL 0x90
#define DIR_RX(chan)    (0x4c + 2*(chan))
#define DIR_TX(chan)    (0x4d + 2*(chan))
#define DCR_RX(chan)    (0x58 + 2*(chan))
#define DCR_TX(chan)    (0x59 + 2*(chan))
#define DMR_RX(chan)    (0x54 + 2*(chan))
#define DMR_TX(chan)    (0x55 + 2*(chan))
#define DCR_CLEAR_EOF   0x02
#define DCR_ABORT   0x01
#define DIR_EOME    0x40
#define DMER        0x07
#define DMER_DME        0x80
#define ST_TX_EOM     0x80

/* Unknown SCA registers (infer from driver usage) */
#define WCRL        0x00
#define WCRM        0x01
#define WCRH        0x02
#define PCR         0x06

/* PLX 9052 register layout (for reference) */
typedef struct {
    uint32_t loc_addr_range[4];  /* 00-0Ch */
    uint32_t loc_rom_range;     /* 10h */
    uint32_t loc_addr_base[4];  /* 14-20h */
    uint32_t loc_rom_base;      /* 24h */
    uint32_t loc_bus_descr[4];  /* 28-34h */
    uint32_t rom_bus_descr;     /* 38h */
    uint32_t cs_base[4];        /* 3C-48h */
    uint32_t intr_ctrl_stat;    /* 4Ch */
    uint32_t init_ctrl;         /* 50h */
} plx9052;

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
    uint8_t plx_regs[0x80];
    uint8_t sca_regs[0x400];
};

static uint64_t plx_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->plx_regs)) {
        return 0;
    }

    switch (size) {
    case 1:
        val = s->plx_regs[addr];
        break;
    case 2:
        val = lduw_le_p(&s->plx_regs[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->plx_regs[addr]);
        break;
    default:
        break;
    }
    return val;
}

static void plx_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->plx_regs)) {
        return;
    }

    switch (size) {
    case 1:
        s->plx_regs[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(&s->plx_regs[addr], (uint16_t)val);
        break;
    case 4:
        stl_le_p(&s->plx_regs[addr], (uint32_t)val);
        break;
    default:
        break;
    }
}

static uint64_t sca_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->sca_regs)) {
        return 0;
    }

    switch (size) {
    case 1:
        val = s->sca_regs[addr];
        break;
    case 2:
        val = lduw_le_p(&s->sca_regs[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->sca_regs[addr]);
        break;
    default:
        break;
    }
    return val;
}

static void sca_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->sca_regs)) {
        return;
    }

    switch (size) {
    case 1:
        s->sca_regs[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(&s->sca_regs[addr], (uint16_t)val);
        break;
    case 4:
        stl_le_p(&s->sca_regs[addr], (uint32_t)val);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps plx_mmio_ops = {
    .read = plx_mmio_read,
    .write = plx_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps sca_mmio_ops = {
    .read = sca_mmio_read,
    .write = sca_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->plx_regs, 0, sizeof(s->plx_regs));
    memset(s->sca_regs, 0, sizeof(s->sca_regs));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 0) {
            memory_region_init_io(mr, OBJECT(s), &plx_mmio_ops, s, bi->name, aligned_size);
        } else if (bi->index == 2) {
            memory_region_init_io(mr, OBJECT(s), &sca_mmio_ops, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &plx_mmio_ops, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_PLX);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PLX_9050);
    pci_config_set_class(pdev, PCI_CLASS_NETWORK_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BARs: 0=PLX, 2=SCA, 3=RAM; BAR1 unused */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = PCI200SYN_PLX_SIZE, .name = "plx" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_NONE, .size = 0, .name = "bar1_unused" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = PCI200SYN_SCA_SIZE, .name = "sca" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_RAM, .size = 0x10000, .name = "ram" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSI-X to teardown */
}

static const VMStateDescription vmstate_pcibase = {
    .name = TYPE_PCIBASE_DEVICE,
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
