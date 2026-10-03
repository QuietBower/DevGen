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

/* Additional include files retrieved from driver context */


#define TYPE_PCIBASE_DEVICE "PC300_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CYCLADES		0x120e
#define PCI_DEVICE_ID_PC300_RX_1	0x0301
#define PCI_DEVICE_ID_PC300_TE_1	0x0311
#define PCI_DEVICE_ID_PC300_TE_2	0x0310

typedef struct {
	uint32_t loc_addr_range[4];	/* 00-0Ch : Local Address Ranges */
	uint32_t loc_rom_range;	/* 10h : Local ROM Range */
	uint32_t loc_addr_base[4];	/* 14-20h : Local Address Base Addrs */
	uint32_t loc_rom_base;	/* 24h : Local ROM Base */
	uint32_t loc_bus_descr[4];	/* 28-34h : Local Bus Descriptors */
	uint32_t rom_bus_descr;	/* 38h : ROM Bus Descriptor */
	uint32_t cs_base[4];		/* 3C-48h : Chip Select Base Addrs */
	uint32_t intr_ctrl_stat;	/* 4Ch : Interrupt Control/Status */
	uint32_t init_ctrl;		/* 50h : EEPROM ctrl, Init Ctrl, etc */
} plx9050;

typedef struct {
	uint32_t loc_addr_range[4];	/* 00-0Ch : Local Address Ranges */
	uint32_t loc_rom_range;	/* 10h : Local ROM Range */
	uint32_t loc_addr_base[4];	/* 14-20h : Local Address Base Addrs */
	uint32_t loc_rom_base;	/* 24h : Local ROM Base */
	uint32_t loc_bus_descr[4];	/* 28-34h : Local Bus Descriptors */
	uint32_t rom_bus_descr;	/* 38h : ROM Bus Descriptor */
	uint32_t cs_base[4];		/* 3C-48h : Chip Select Base Addrs */
	uint32_t intr_ctrl_stat;	/* 4Ch : Interrupt Control/Status */
	uint32_t init_ctrl;		/* 50h : EEPROM ctrl, Init Ctrl, etc */
} plx9052;

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
    uint32_t intr_ctrl_stat;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t loc_addr_range[4];
    uint32_t loc_rom_range;
    uint32_t loc_addr_base[4];
    uint32_t loc_rom_base;
    uint32_t loc_bus_descr[4];
    uint32_t rom_bus_descr;
    uint32_t cs_base[4];
    uint32_t init_ctrl;

    /* DMA Context */
    uint32_t edal;
    uint32_t cdal;

    uint8_t st0, st1, st2, st3, st4;
    uint8_t cst0, cst1;
};

#define PC300_PLX_SIZE		0x80
#define PC300_SCA_SIZE		0x400
#define PC300_CLKSEL_MASK	 (0x00000004UL)
#define PC300_CHMEDIA_MASK(port) (0x00000020UL << ((port) * 3))
#define PC300_CTYPE_MASK	 (0x00000800UL)
#define EXS	0x13e
#define RXS	0x13c
#define CLK_LINE    	0x00
#define CLK_TX_RXCLK	0x60
#define MSCI1_OFFSET 0x80
#define TXS	0x13d
#define CLK_PIN_OUT	0x80
#define CLK_BRG     	0x40
#define MSCI0_OFFSET 0x00
#define EXS_TES1	0x20
#define CLK_BRG_MASK	0x0F
#define TMCT	0x144
#define TMCR	0x145
#define MD2	0x13a
#define MD2_LOOPBACK	0x03
#define MD2_NRZI	0x20
#define IE0_RXINTA	0x40
#define CMD_TX_ENABLE	0x02
#define MD0_CRC_16	0x05
#define MD0_CRC_ITU	0x07
#define CTL_URCT	0x80
#define MD0_CRC_16_0	0x04
#define MD0	0x138
#define TNR0	0x150
#define CMD_RX_ENABLE	0x12
#define MD0_HDLC        0x80
#define IE0	0x120
#define CTL	0x130
#define CMD_RESET	0x21
#define IE0_CDCD	0x00000400
#define TNR1	0x151
#define RNR	0x154
#define TFS	0x14b
#define MD0_CRC_NONE	0x00
#define TCR	0x152
#define MD2_FM_MARK	0xA0
#define IDL	0x142
#define MD2_MANCHESTER	0x80
#define MD1	0x139
#define CMD	0x128
#define MD2_FM_SPACE	0xC0
#define MD2_NRZ		0x00
#define CTL_IDLE	0x10
#define MD0_CRC_ITU32	0x06
#define CTL_URSKP	0x40
#define CST0	0x108
#define ISR1	0x70
#define ISR0	0x6c
#define ST2	0x11a
#define ST4	0x11c
#define DSR_DE		0x02
#define DSR_TX(chan)	(0x49 + 2*chan)
#define ST0	0x118
#define CST1	0x109
#define EDAL	0x88
#define FST	0x11d
#define ST1	0x119
#define CDAL	0x84
#define ST3	0x11b
#define DSR_RX(chan)	(0x48 + 2*chan)
#define ILAR	0x00
#define DMER	0x07
#define WCRH	0x26
#define PCR	0x40
#define WCRM	0x25
#define WCRL	0x24
#define DMER_DME        0x80
#define IER0	0x74
#define ST3_DCD		0x04
#define DMAC0RX_OFFSET 0x00
#define DMAC1RX_OFFSET 0x40
#define DMAC1TX_OFFSET 0x60
#define DMAC0TX_OFFSET 0x20
#define BTCR		0x08

#define ST_TX_EOM     0x80

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    bool level = (s->loc_addr_range[1] != 0); /* ISR0 non-zero */
    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case 0x4C:
            val = s->intr_ctrl_stat;
            break;
        case 0x50:
            val = s->init_ctrl;
            break;
        case 0x74: /* IER0 */
            val = s->loc_addr_range[0];
            break;
        case 0x6C: /* ISR0 */
            val = s->loc_addr_range[1];
            break;
        case 0x40: /* PCR */
            val = s->loc_addr_range[2];
            break;
        case MSCI0_OFFSET + MD2:
            val = s->loc_addr_base[0];
            break;
        case MSCI1_OFFSET + MD2:
            val = s->loc_addr_base[1];
            break;
        case MSCI0_OFFSET + ST3:
        case MSCI1_OFFSET + ST3:
            val = s->st3;
            break;
        case DMAC0RX_OFFSET + CDAL:
        case DMAC1RX_OFFSET + CDAL:
        case DMAC0TX_OFFSET + CDAL:
        case DMAC1TX_OFFSET + CDAL:
            val = s->cdal;
            break;
        case DMAC0RX_OFFSET + EDAL:
        case DMAC1RX_OFFSET + EDAL:
        case DMAC0TX_OFFSET + EDAL:
        case DMAC1TX_OFFSET + EDAL:
            val = s->edal;
            break;
        default:
            val = 0;
            break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
        case 0x4C:
            s->intr_ctrl_stat = val;
            break;
        case 0x50:
            s->init_ctrl = val;
            break;
        case 0x74: /* IER0 */
            s->loc_addr_range[0] = val;
            pcibase_update_irq(s);
            break;
        case 0x6C: /* ISR0 */
            s->loc_addr_range[1] = val;
            pcibase_update_irq(s);
            break;
        case 0x40: /* PCR */
            s->loc_addr_range[2] = val;
            break;
        case MSCI0_OFFSET + MD2:
            s->loc_addr_base[0] = val;
            break;
        case MSCI1_OFFSET + MD2:
            s->loc_addr_base[1] = val;
            break;
        case DMAC0RX_OFFSET + CDAL:
        case DMAC1RX_OFFSET + CDAL:
        case DMAC0TX_OFFSET + CDAL:
        case DMAC1TX_OFFSET + CDAL:
            s->cdal = val;
            break;
        case DMAC0RX_OFFSET + EDAL:
        case DMAC1RX_OFFSET + EDAL:
        case DMAC0TX_OFFSET + EDAL:
        case DMAC1TX_OFFSET + EDAL:
            s->edal = val;
            break;
        case DSR_TX(0):
        case DSR_TX(1):
            if (val & DSR_DE) {
                /* TX DMA Enable */
                /* Requires descriptor parsing logic, pending missing sources */
            }
            break;
        default:
            break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO not used by driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* PIO not used by driver */
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

    s->intr_ctrl_stat = 0;
    s->init_ctrl = 0;
    s->loc_addr_range[0] = 0;
    s->loc_addr_range[1] = 0;
    s->loc_addr_range[2] = 0;
    s->loc_addr_base[0] = 0;
    s->loc_addr_base[1] = 0;
    s->st3 = 0; /* Carrier ON */
    s->cdal = 0;
    s->edal = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CYCLADES );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PC300_RX_1 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, PC300_PLX_SIZE, "plx_regs"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_NONE, 0, NULL};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_MMIO, PC300_SCA_SIZE, "sca_regs"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_RAM, 0x40000, "ram"};
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
