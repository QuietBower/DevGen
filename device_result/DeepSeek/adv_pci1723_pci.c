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
/* No additional includes required */

#define TYPE_PCIBASE_DEVICE "adv_pci1723_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ADVANTECH 0x13fe
#define DEVICE_ID_PCI1723 0x1723
#define CLASS_ID_PCI1723 0x1180

#define PCI1723_AO_REG(x)		(0x00 + ((x) * 2))
#define PCI1723_BOARD_ID_REG		0x10
#define PCI1723_BOARD_ID_MASK		(0xf << 0)
#define PCI1723_SYNC_CTRL_REG		0x12
#define PCI1723_SYNC_CTRL(x)		(((x) & 0x1) << 0)
#define PCI1723_SYNC_CTRL_ASYNC		PCI1723_SYNC_CTRL(0)
#define PCI1723_SYNC_CTRL_SYNC		PCI1723_SYNC_CTRL(1)
#define PCI1723_CTRL_REG		0x14
#define PCI1723_CTRL_BUSY		BIT(15)
#define PCI1723_CTRL_INIT		BIT(14)
#define PCI1723_CTRL_SELF		BIT(8)
#define PCI1723_CTRL_IDX(x)		(((x) & 0x3) << 6)
#define PCI1723_CTRL_RANGE(x)		(((x) & 0x3) << 4)
#define PCI1723_CTRL_SEL(x)		(((x) & 0x1) << 3)
#define PCI1723_CTRL_GAIN		PCI1723_CTRL_SEL(0)
#define PCI1723_CTRL_OFFSET		PCI1723_CTRL_SEL(1)
#define PCI1723_CTRL_CHAN(x)		(((x) & 0x7) << 0)
#define PCI1723_CALIB_CTRL_REG		0x16
#define PCI1723_CALIB_CTRL_CS		BIT(2)
#define PCI1723_CALIB_CTRL_DAT		BIT(1)
#define PCI1723_CALIB_CTRL_CLK		BIT(0)
#define PCI1723_CALIB_STROBE_REG	0x18
#define PCI1723_DIO_CTRL_REG		0x1a
#define PCI1723_DIO_CTRL_HDIO		BIT(1)
#define PCI1723_DIO_CTRL_LDIO		BIT(0)
#define PCI1723_DIO_DATA_REG		0x1c
#define PCI1723_CALIB_DATA_REG		0x1e
#define PCI1723_SYNC_STROBE_REG		0x20
#define PCI1723_RESET_AO_STROBE_REG	0x22
#define PCI1723_RESET_CALIB_STROBE_REG	0x24
#define PCI1723_RANGE_STROBE_REG	0x26
#define PCI1723_VREF_REG		0x28
#define PCI1723_VREF(x)			(((x) & 0x3) << 0)
#define PCI1723_VREF_NEG10V		PCI1723_VREF(0)
#define PCI1723_VREF_0V			PCI1723_VREF(1)
#define PCI1723_VREF_POS10V		PCI1723_VREF(3)

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

/* Hardware Register Shadows */
typedef struct {
    uint16_t ao[8];              /* 0x00-0x0e */
    uint16_t board_id;           /* 0x10 */
    uint16_t sync_ctrl;          /* 0x12 */
    uint16_t ctrl;               /* 0x14 */
    uint16_t calib_ctrl;         /* 0x16 */
    uint16_t calib_strobe;       /* 0x18 */
    uint16_t dio_ctrl;           /* 0x1a */
    uint16_t dio_data;           /* 0x1c */
    uint16_t calib_data;         /* 0x1e */
    uint16_t sync_strobe;        /* 0x20 */
    uint16_t reset_ao_strobe;    /* 0x22 */
    uint16_t reset_calib_strobe; /* 0x24 */
    uint16_t range_strobe;       /* 0x26 */
    uint16_t vref;               /* 0x28 */
} PCI1723Regs;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    PCI1723Regs regs;

    /* DMA Context */
    /* No DMA */

    /* Operational status flags */
    /* No status struct */

    /* State used to handle reset sequences */
    /* No probe/reset struct */

    /* Power management state (D0-D3) */
    /* No power management struct */
};

/* No Other_Addition_Info_Defin */

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* No interrupt logic in driver */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA */
}

/* PIO read handler for the device */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 2) {
        return ~0ULL;
    }

    switch (addr) {
    case 0x00: /* AO_REG(0) */
    case 0x02: /* AO_REG(1) */
    case 0x04: /* AO_REG(2) */
    case 0x06: /* AO_REG(3) */
    case 0x08: /* AO_REG(4) */
    case 0x0a: /* AO_REG(5) */
    case 0x0c: /* AO_REG(6) */
    case 0x0e: /* AO_REG(7) */
        val = s->regs.ao[addr >> 1];
        break;
    case PCI1723_BOARD_ID_REG:
        val = s->regs.board_id;
        break;
    case PCI1723_SYNC_CTRL_REG:
        val = s->regs.sync_ctrl;
        break;
    case PCI1723_CTRL_REG:
        val = s->regs.ctrl;
        break;
    case PCI1723_CALIB_CTRL_REG:
        val = s->regs.calib_ctrl;
        break;
    case PCI1723_CALIB_STROBE_REG:
        val = s->regs.calib_strobe;
        break;
    case PCI1723_DIO_CTRL_REG:
        val = s->regs.dio_ctrl;
        break;
    case PCI1723_DIO_DATA_REG:
        val = s->regs.dio_data;
        break;
    case PCI1723_CALIB_DATA_REG:
        val = s->regs.calib_data;
        break;
    case PCI1723_SYNC_STROBE_REG:
        val = s->regs.sync_strobe;
        break;
    case PCI1723_RESET_AO_STROBE_REG:
        val = s->regs.reset_ao_strobe;
        break;
    case PCI1723_RESET_CALIB_STROBE_REG:
        val = s->regs.reset_calib_strobe;
        break;
    case PCI1723_RANGE_STROBE_REG:
        val = s->regs.range_strobe;
        break;
    case PCI1723_VREF_REG:
        val = s->regs.vref;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

/* PIO write handler for the device */
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 2) {
        return;
    }

    switch (addr) {
    case 0x00: /* AO_REG(0) */
    case 0x02: /* AO_REG(1) */
    case 0x04: /* AO_REG(2) */
    case 0x06: /* AO_REG(3) */
    case 0x08: /* AO_REG(4) */
    case 0x0a: /* AO_REG(5) */
    case 0x0c: /* AO_REG(6) */
    case 0x0e: /* AO_REG(7) */
        s->regs.ao[addr >> 1] = (uint16_t)val;
        break;
    case PCI1723_BOARD_ID_REG:
        s->regs.board_id = (uint16_t)val;
        break;
    case PCI1723_SYNC_CTRL_REG:
        s->regs.sync_ctrl = (uint16_t)val;
        break;
    case PCI1723_CTRL_REG:
        s->regs.ctrl = (uint16_t)val;
        break;
    case PCI1723_CALIB_CTRL_REG:
        s->regs.calib_ctrl = (uint16_t)val;
        break;
    case PCI1723_CALIB_STROBE_REG:
        s->regs.calib_strobe = (uint16_t)val;
        break;
    case PCI1723_DIO_CTRL_REG:
        s->regs.dio_ctrl = (uint16_t)val;
        break;
    case PCI1723_DIO_DATA_REG:
        s->regs.dio_data = (uint16_t)val;
        break;
    case PCI1723_CALIB_DATA_REG:
        s->regs.calib_data = (uint16_t)val;
        break;
    case PCI1723_SYNC_STROBE_REG:
        s->regs.sync_strobe = (uint16_t)val;
        break;
    case PCI1723_RESET_AO_STROBE_REG:
        s->regs.reset_ao_strobe = (uint16_t)val;
        break;
    case PCI1723_RESET_CALIB_STROBE_REG:
        s->regs.reset_calib_strobe = (uint16_t)val;
        break;
    case PCI1723_RANGE_STROBE_REG:
        s->regs.range_strobe = (uint16_t)val;
        break;
    case PCI1723_VREF_REG:
        s->regs.vref = (uint16_t)val;
        break;
    default:
        /* ignore writes to undefined offsets */
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 2, .max_access_size = 2 },
    .impl  = { .min_access_size = 2, .max_access_size = 2 },
};

/* MMIO Handlers (unused) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No MMIO used, return 0 */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO used */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ADVANTECH );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID_PCI1723 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID_PCI1723 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: BAR2 is the PIO region used by the driver */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 2,
        .type = BAR_TYPE_PIO,
        .size = 0x40,  /* covers registers 0x00-0x28 */
        .name = "bar2"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI or MSI-X */
    /* No DMA configuration */
    /* No timer configuration */
    /* Final state initialization before the device is 'live' */
    memset(&s->regs, 0, sizeof(s->regs));
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

    /* No additional uninit actions */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "adv_pci1723_pci",
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
