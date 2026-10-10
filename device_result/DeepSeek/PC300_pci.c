/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

#define TYPE_PCIBASE_DEVICE "PC300_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CYCLADES       0x120e
#define PCI_DEVICE_ID_PC300_RX_1     0x0300
#define PCI_CLASS_ID                 0x0200

#define PC300_PLX_SIZE               0x80
#define PC300_SCA_SIZE               0x400
#define PC300_RAM_SIZE               0x10000

/* SCA Register Offsets */
#define EXS            0x13e
#define RXS            0x13c
#define TXS            0x13d
#define MSCI0_OFFSET   0x00
#define MSCI1_OFFSET   0x80
#define TMCT           0x144
#define TMCR           0x145
#define MD0            0x138
#define MD1            0x139
#define MD2            0x13a
#define CTL            0x130
#define CMD            0x128
#define IDL            0x142
#define TNR0           0x150
#define TNR1           0x151
#define RNR            0x154
#define TFS            0x14b
#define TCR            0x152
#define CST0           0x108
#define CST1           0x109
#define ISR0           0x6c
#define ISR1           0x70
#define ISR0_OFFSET    0x6c
#define ISR1_OFFSET    0x70
#define ST0            0x118
#define ST1            0x119
#define ST2            0x11a
#define ST3            0x11b
#define ST4            0x11c
#define FST            0x11d
#define IE0            0x120
#define IER0           0x74
#define DMER           0x07
#define ILAR           0x00
#define CDAL           0x84
#define EDAL           0x88
#define WCRL           0x24
#define WCRM           0x25
#define WCRH           0x26
#define PCR            0x40

/* DMAC Offsets */
#define DMAC0RX_OFFSET 0x00
#define DMAC1RX_OFFSET 0x40
#define DMAC0TX_OFFSET 0x20
#define DMAC1TX_OFFSET 0x60

/* DSR registers */
#define DSR_TX(chan)   (0x49 + 2*chan)
#define DSR_RX(chan)   (0x48 + 2*chan)

/* Bit definitions */
#define PC300_CLKSEL_MASK      (0x00000004UL)
#define PC300_CHMEDIA_MASK(port) (0x00000020UL << ((port) * 3))
#define PC300_CTYPE_MASK       (0x00000800UL)
#define CLK_LINE               0x00
#define CLK_TX_RXCLK           0x60
#define CLK_PIN_OUT            0x80
#define CLK_BRG                0x40
#define CLK_BRG_MASK           0x0F
#define EXS_TES1               0x20
#define MD2_LOOPBACK           0x03
#define MD2_NRZI               0x20
#define MD2_FM_MARK            0xA0
#define MD2_MANCHESTER         0x80
#define MD2_FM_SPACE           0xC0
#define MD2_NRZ                0x00
#define MD0_CRC_16             0x05
#define MD0_CRC_ITU            0x07
#define MD0_CRC_16_0           0x04
#define MD0_CRC_NONE           0x00
#define MD0_CRC_ITU32          0x06
#define MD0_HDLC               0x80
#define CTL_URCT               0x80
#define CTL_IDLE               0x10
#define CTL_URSKP              0x40
#define IE0_RXINTA             0x40
#define IE0_CDCD               0x00000400
#define CMD_TX_ENABLE          0x02
#define CMD_RX_ENABLE          0x12
#define CMD_RESET              0x21
#define DMER_DME               0x80
#define DSR_DE                 0x02
#define ST3_DCD                0x04

/* PLX9050 structure (offsets defined by struct layout) */
/* We only need to emulate the registers actually accessed: init_ctrl (32-bit) and intr_ctrl_stat (16-bit).
   According to PLX 9050 datasheet, these are at offsets 0x4C and 0x50 respectively. */
#define PLX_INIT_CTRL_OFFSET    0x4C
#define PLX_INTR_CTRL_STAT_OFFSET 0x50

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

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint32_t plx_init_ctrl;          /* init_ctrl at PLX offset 0x4C */
    uint16_t plx_intr_ctrl_stat;     /* intr_ctrl_stat at PLX offset 0x50 */
    uint8_t sca_mem[PC300_SCA_SIZE]; /* SCA register space */
    uint8_t *ram;
    hwaddr ram_size;

    uint32_t status;
};

static uint64_t pcibase_plx_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == PLX_INIT_CTRL_OFFSET && size == 4) {
        val = s->plx_init_ctrl;
    } else if (addr == PLX_INTR_CTRL_STAT_OFFSET && size == 2) {
        val = s->plx_intr_ctrl_stat;
    }
    /* Other PLX registers return 0 on read */
    return val;
}

static void pcibase_plx_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == PLX_INIT_CTRL_OFFSET && size == 4) {
        s->plx_init_ctrl = val;
    } else if (addr == PLX_INTR_CTRL_STAT_OFFSET && size == 2) {
        s->plx_intr_ctrl_stat = (uint16_t)val;
    }
    /* Other PLX writes ignored */
}

static const MemoryRegionOps pcibase_plx_ops = {
    .read = pcibase_plx_read,
    .write = pcibase_plx_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t pcibase_sca_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    unsigned i;

    if (addr + size > PC300_SCA_SIZE) {
        return 0;
    }

    for (i = 0; i < size; i++) {
        val |= (uint64_t)s->sca_mem[addr + i] << (i * 8);
    }

    return val;
}

static void pcibase_sca_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned i;

    if (addr + size > PC300_SCA_SIZE) {
        return;
    }

    for (i = 0; i < size; i++) {
        s->sca_mem[addr + i] = (val >> (i * 8)) & 0xFF;
    }
}

static const MemoryRegionOps pcibase_sca_ops = {
    .read = pcibase_sca_read,
    .write = pcibase_sca_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Reset registers to power-on defaults */
    memset(s->sca_mem, 0, PC300_SCA_SIZE);
    s->plx_init_ctrl = 0;
    s->plx_intr_ctrl_stat = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 0) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_plx_ops, s, bi->name, aligned_size);
        } else if (bi->index == 2) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_sca_ops, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CYCLADES);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_PC300_RX_1);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Capabilities: Do not add PCIe or PM as the driver does not use them */
    s->has_msi = false;
    s->has_msix = false;

    /* BAR Initialization */
    /* BAR0: PLX registers */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PC300_PLX_SIZE;
    s->bar_info[0].name = "plx";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* BAR2: SCA memory */
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = PC300_SCA_SIZE;
    s->bar_info[2].name = "sca";
    pcibase_register_bar(pdev, s, &s->bar_info[2], errp);

    /* BAR3: RAM for descriptors and buffers */
    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_RAM;
    s->bar_info[3].size = PC300_RAM_SIZE;
    s->bar_info[3].name = "ram";
    pcibase_register_bar(pdev, s, &s->bar_info[3], errp);
    s->ram = memory_region_get_ram_ptr(&s->bar_regions[3]);
    s->ram_size = PC300_RAM_SIZE;
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
    g_free(s->ram);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "PC300_pci",
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
