/*
 * QEMU model for fmh_gpib PCI device
 * Auto-generated based on Linux driver fmh_gpib.c
 * Phase 2: Implementation (skeleton with placeholders for missing definitions)
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

#define TYPE_PCIBASE_DEVICE "fmh_gpib_pci_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* These definitions are placeholders; actual values must come from driver headers */
#define BOGUS_PCI_VENDOR_ID_FLUKE 0xffff
#define BOGUS_PCI_DEVICE_ID_FLUKE_BLADERUNNER 0x0
#define gpib_control_status_pci_resource_index 0
#define gpib_fifo_pci_resource_index 1
#define fifo_reg_offset 4

/* FIFO register indices (placeholders) */
#define FIFO_CONTROL_STATUS_REG   0
#define FIFO_XFER_COUNTER_REG     1
#define FIFO_DATA_REG             0xa
#define FIFO_MAX_BURST_LENGTH_REG 3

/* FIFO bit masks (placeholders) */
#define RX_FIFO_HALF_FULL           2
#define TX_FIFO_HALF_EMPTY          2
#define RX_FIFO_EMPTY               0
#define TX_FIFO_EMPTY               1
#define RX_FIFO_HALF_FULL_INTERRUPT_ENABLE   0x0010
#define TX_FIFO_HALF_EMPTY_INTERRUPT_ENABLE  0x0020
#define RX_FIFO_HALF_FULL_INTERRUPT_IS_ENABLED 0x0040
#define TX_FIFO_HALF_EMPTY_INTERRUPT_IS_ENABLED 0x0080
#define TX_FIFO_DMA_REQUEST_ENABLE  0x0100
#define RX_FIFO_DMA_REQUEST_ENABLE  0x0200
#define TX_FIFO_CLEAR               0x0400
#define RX_FIFO_CLEAR               0x0800
#define FIFO_DATA_EOI_FLAG          0x1000
#define fifo_xfer_counter_mask      0xFFFF
#define fifo_data_mask              0xFFFF
#define fifo_max_burst_length_mask  0xFFFF

/* nec7210 register indices (placeholders) */
#define ISR0_IMR0_REG     0
#define ISR1              0x70
#define ISR2              2
#define EXT_STATUS_1_REG  3
#define BUS_STATUS_REG    4
#define IMR1              5
#define IMR2              6
#define SPMR              7
#define AUXMR             8
#define HR_DOIE           0x01
#define HR_DIIE           0x02
#define HR_DMAO           0x04
#define HR_DMAI           0x08
#define IFC_INTERRUPT_BIT 0x01
#define ATN_INTERRUPT_ENABLE_BIT 0x02
#define IFC_INTERRUPT_ENABLE_BIT 0x04
#define DATA_IN_STATUS_BIT  0x01
#define DATA_OUT_STATUS_BIT 0x02
#define COMMAND_OUT_STATUS_BIT 0x04
#define END_STATUS_BIT      0x08
#define RFD_HOLDOFF_STATUS_BIT 0x10
#define BSR_REN_BIT 0x01
#define BSR_IFC_BIT 0x02
#define BSR_SRQ_BIT 0x04
#define BSR_EOI_BIT 0x08
#define BSR_NRFD_BIT 0x10
#define BSR_NDAC_BIT 0x20
#define BSR_DAV_BIT 0x40
#define BSR_ATN_BIT 0x80
#define AUX_I_REG 0x01
#define LOCAL_PPOLL_MODE_BIT 0x01
#define AUX_LO_SPEED 0x02
#define AUX_HI_SPEED 0x04
#define AUX_RTL2 0x08
#define AUX_RTL 0x10
#define AUX_SEOI 0x20
#define AUX_FH 0x40
#define AUX_RFD_HOLDOFF_ASAP 0x80
#define AUX_REQT 0x100
#define AUX_REQF 0x200
#define VALID_ALL 0xFFFF
#define BUS_REN 0x0001
#define BUS_IFC 0x0002
#define BUS_SRQ 0x0004
#define BUS_EOI 0x0008
#define BUS_NRFD 0x0010
#define BUS_NDAC 0x0020
#define BUS_DAV 0x0040
#define BUS_ATN 0x0080

/* BAR enumerations */
enum {
    CS_BAR = 0,
    FIFO_BAR = 1,
    NUM_BARS = 2
};

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion cs_mr;    /* control/status registers */
    MemoryRegion fifo_mr;  /* FIFO registers */

    /* Internal register storage */
    uint8_t cs_regs[256];
    uint16_t fifo_regs[128];

    /* Interrupt state */
    bool irq_raised;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    /* TODO: implement based on interrupt status registers */
    /* For now, assume no interrupt pending */
    pci_set_irq(PCI_DEVICE(s), s->irq_raised ? 1 : 0);
}

static uint64_t pcibase_cs_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= sizeof(s->cs_regs)) return 0;
    return s->cs_regs[addr];
}

static void pcibase_cs_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < sizeof(s->cs_regs)) {
        s->cs_regs[addr] = val;
    }
}

static uint64_t pcibase_fifo_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* 16-bit aligned access expected */
    if ((addr & 1) == 0 && (addr/2) < ARRAY_SIZE(s->fifo_regs)) {
        return s->fifo_regs[addr/2];
    }
    return 0;
}

static void pcibase_fifo_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if ((addr & 1) == 0 && (addr/2) < ARRAY_SIZE(s->fifo_regs)) {
        s->fifo_regs[addr/2] = val;
    }
}

static const MemoryRegionOps pcibase_cs_ops = {
    .read = pcibase_cs_read,
    .write = pcibase_cs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static const MemoryRegionOps pcibase_fifo_ops = {
    .read = pcibase_fifo_read,
    .write = pcibase_fifo_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 2, .max_access_size = 2 },
    .impl  = { .min_access_size = 2, .max_access_size = 2 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->cs_regs, 0, sizeof(s->cs_regs));
    memset(s->fifo_regs, 0, sizeof(s->fifo_regs));
    s->irq_raised = false;
    pcibase_update_irq(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  BOGUS_PCI_VENDOR_ID_FLUKE);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  BOGUS_PCI_DEVICE_ID_FLUKE_BLADERUNNER);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xFF00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Control/Status BAR */
    memory_region_init_io(&s->cs_mr, OBJECT(s), &pcibase_cs_ops, s,
                          "gpib_control_status", 256);
    pci_register_bar(pdev, CS_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->cs_mr);

    /* FIFO BAR */
    memory_region_init_io(&s->fifo_mr, OBJECT(s), &pcibase_fifo_ops, s,
                          "dma_fifos", 256);
    pci_register_bar(pdev, FIFO_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->fifo_mr);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* nothing to do */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "fmh_gpib_pci_driver_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(cs_regs, PCIBaseState, 256),
        VMSTATE_UINT16_ARRAY(fifo_regs, PCIBaseState, 128),
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
