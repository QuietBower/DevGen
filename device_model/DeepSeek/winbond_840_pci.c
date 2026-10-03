/*
 * QEMU device model for Winbond w840-based PCI Ethernet Adapter
 * Based on driver: drivers/net/ethernet/dec/tulip/winbond-840.c
 * Registers and behavior extracted strictly from driver source.
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

#define TYPE_PCIBASE_DEVICE "winbond_840_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1050
#define DEVICE_ID 0x0840
#define CLASS_ID 0x0200

#define TULIP_BAR 1

enum w840_offsets {
	PCIBusCfg=0x00, TxStartDemand=0x04, RxStartDemand=0x08,
	RxRingPtr=0x0C, TxRingPtr=0x10,
	IntrStatus=0x14, NetworkConfig=0x18, IntrEnable=0x1C,
	RxMissed=0x20, EECtrl=0x24, MIICtrl=0x24, BootRom=0x28, GPTimer=0x2C,
	CurRxDescAddr=0x30, CurRxBufAddr=0x34,
	MulticastFilter0=0x38, MulticastFilter1=0x3C, StationAddr=0x40,
	CurTxDescAddr=0x4C, CurTxBufAddr=0x50,
};

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[22];  /* register file covering 0x00-0x54 */

    /* DMA Context */
    dma_addr_t tx_ring_dma;
    dma_addr_t rx_ring_dma;
    uint32_t tx_ring_size;
    uint32_t rx_ring_size;

    uint32_t status_flags;      /* Operational status flags */
    uint8_t pm_state;     /* Power management state (D0-D3) */
};

/* Descriptor structs as defined in driver */
typedef struct {
    int32_t status;
    int32_t length;
    uint32_t buffer1;
    uint32_t buffer2;
} W840RxDesc;

typedef struct {
    int32_t status;
    int32_t length;
    uint32_t buffer1, buffer2;
} W840TxDesc;

static void pcibase_reset(DeviceState *dev);

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    hwaddr aligned = addr & ~3;
    unsigned offset = addr & 3;
    uint32_t reg_val = 0;

    switch (aligned) {
    case 0x00: /* PCIBusCfg */
        reg_val = s->regs[0];
        break;
    case 0x04: /* TxStartDemand */
        /* write-only, return 0 */
        break;
    case 0x08: /* RxStartDemand */
        break;
    case 0x0c: /* RxRingPtr */
        reg_val = s->regs[3];
        break;
    case 0x10: /* TxRingPtr */
        reg_val = s->regs[4];
        break;
    case 0x14: /* IntrStatus */
        reg_val = s->intr_status;
        break;
    case 0x18: /* NetworkConfig */
        reg_val = s->regs[6];
        break;
    case 0x1c: /* IntrEnable */
        reg_val = s->intr_mask;
        break;
    case 0x20: /* RxMissed */
        /* read-only, always 0 for simplicity */
        reg_val = 0;
        break;
    case 0x24: /* EECtrl/MIICtrl */
        reg_val = s->regs[9];
        break;
    case 0x28: /* BootRom */
        reg_val = s->regs[10];
        break;
    case 0x2c: /* GPTimer */
        reg_val = s->regs[11];
        break;
    case 0x30: /* CurRxDescAddr */
        reg_val = s->regs[12];
        break;
    case 0x34: /* CurRxBufAddr */
        reg_val = s->regs[13];
        break;
    case 0x38: /* MulticastFilter0 */
        reg_val = s->regs[14];
        break;
    case 0x3c: /* MulticastFilter1 */
        reg_val = s->regs[15];
        break;
    case 0x40: /* StationAddr low word */
        reg_val = s->regs[16];
        break;
    case 0x44: /* StationAddr high word */
        reg_val = s->regs[17];
        break;
    case 0x4c: /* CurTxDescAddr */
        reg_val = s->regs[19];
        break;
    case 0x50: /* CurTxBufAddr */
        reg_val = s->regs[20];
        break;
    default:
        reg_val = 0;
        break;
    }

    /* Extract the requested bytes from reg_val */
    if (size == 1) {
        val = (reg_val >> (offset * 8)) & 0xff;
    } else if (size == 2) {
        val = (reg_val >> (offset * 8)) & 0xffff;
    } else if (size == 4) {
        val = reg_val;
    } else if (size == 8) {
        /* Unlikely, but return low 32 bits */
        val = reg_val;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr aligned = addr & ~3;
    unsigned offset = addr & 3;
    uint32_t mask = (size == 1) ? 0xff : (size == 2) ? 0xffff : 0xffffffff;
    uint32_t old_val, new_val;

    /* Helper macro to compute new value for partial writes */
#define UPDATE_REG(base, index) do { \
        old_val = (base)[(index)]; \
        new_val = (old_val & ~(mask << (offset * 8))) | ((val & mask) << (offset * 8)); \
        (base)[(index)] = new_val; \
    } while (0)

    switch (aligned) {
    case 0x00: /* PCIBusCfg */
        UPDATE_REG(s->regs, 0);
        if (new_val & 1) {
            /* Software reset */
            pcibase_reset(DEVICE(s));
        }
        break;
    case 0x04: /* TxStartDemand */
        /* Write to trigger Tx, ignored for now */
        break;
    case 0x08: /* RxStartDemand */
        break;
    case 0x0c: /* RxRingPtr */
        UPDATE_REG(s->regs, 3);
        break;
    case 0x10: /* TxRingPtr */
        UPDATE_REG(s->regs, 4);
        break;
    case 0x14: /* IntrStatus */
        /* W1C: clear bits written as 1 */
        old_val = s->intr_status;
        new_val = (old_val & ~(mask << (offset * 8))) | ((val & mask) << (offset * 8));
        s->intr_status &= ~(new_val & 0x1ffff);
        pcibase_update_irq(s);
        break;
    case 0x18: /* NetworkConfig */
        UPDATE_REG(s->regs, 6);
        break;
    case 0x1c: /* IntrEnable */
        old_val = s->intr_mask;
        new_val = (old_val & ~(mask << (offset * 8))) | ((val & mask) << (offset * 8));
        s->intr_mask = new_val;
        pcibase_update_irq(s);
        break;
    case 0x20: /* RxMissed: read-only, ignore writes */
        break;
    case 0x24: /* EECtrl/MIICtrl */
        UPDATE_REG(s->regs, 9);
        break;
    case 0x28: /* BootRom */
        UPDATE_REG(s->regs, 10);
        break;
    case 0x2c: /* GPTimer */
        UPDATE_REG(s->regs, 11);
        break;
    case 0x30: /* CurRxDescAddr */
        UPDATE_REG(s->regs, 12);
        break;
    case 0x34: /* CurRxBufAddr */
        UPDATE_REG(s->regs, 13);
        break;
    case 0x38: /* MulticastFilter0 */
        UPDATE_REG(s->regs, 14);
        break;
    case 0x3c: /* MulticastFilter1 */
        UPDATE_REG(s->regs, 15);
        break;
    case 0x40: /* StationAddr low */
        UPDATE_REG(s->regs, 16);
        break;
    case 0x44: /* StationAddr high */
        UPDATE_REG(s->regs, 17);
        break;
    case 0x4c: /* CurTxDescAddr */
        UPDATE_REG(s->regs, 19);
        break;
    case 0x50: /* CurTxBufAddr */
        UPDATE_REG(s->regs, 20);
        break;
    default:
        break;
    }
#undef UPDATE_REG
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used, MMIO only */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used */
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
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->tx_ring_dma = 0;
    s->rx_ring_dma = 0;
    s->tx_ring_size = 0;
    s->rx_ring_size = 0;
    s->status_flags = 0;
    s->pm_state = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = TULIP_BAR,
        .type = BAR_TYPE_MMIO,
        .size = 256,
        .name = "bar1"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
}

static const VMStateDescription vmstate_pcibase = {
    .name = "winbond_840_pci",
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
