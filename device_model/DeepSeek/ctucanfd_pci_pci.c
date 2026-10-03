/*
 * QEMU PCI device model for CTU CAN FD (ctucanfd_pci)
 * Based on driver: /home/eely/linux-7.1/drivers/net/can/ctucanfd/ctucanfd_pci.c
 * Phase 2: Incremental update with core register handling.
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

#define TYPE_PCIBASE_DEVICE "ctucanfd_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_TEDIA          0x1760
#define PCI_DEVICE_ID_TEDIA_CTUCAN_VER21 0xff00
#define CTUCAN_BAR0_CTUCAN_ID        0x0000
#define CTUCAN_BAR0_CRA_BASE         0x4000
#define CYCLONE_IV_CRA_A2P_IE        0x0050

/* CTU CAN FD Core Registers (from enum ctu_can_fd_can_registers) */
#define CTUCANFD_DEVICE_ID            0x0
#define CTUCANFD_VERSION              0x2
#define CTUCANFD_MODE                 0x4
#define CTUCANFD_SETTINGS             0x6
#define CTUCANFD_STATUS               0x8
#define CTUCANFD_COMMAND              0xc
#define CTUCANFD_INT_STAT            0x10
#define CTUCANFD_INT_ENA_SET         0x14
#define CTUCANFD_INT_ENA_CLR         0x18
#define CTUCANFD_INT_MASK_SET        0x1c
#define CTUCANFD_INT_MASK_CLR        0x20
#define CTUCANFD_BTR                 0x24
#define CTUCANFD_BTR_FD              0x28
#define CTUCANFD_EWL                 0x2c
#define CTUCANFD_ERP                 0x2d
#define CTUCANFD_FAULT_STATE         0x2e
#define CTUCANFD_REC                 0x30
#define CTUCANFD_TEC                 0x32
#define CTUCANFD_ERR_NORM            0x34
#define CTUCANFD_ERR_FD              0x36
#define CTUCANFD_CTR_PRES            0x38
#define CTUCANFD_FILTER_A_MASK       0x3c
#define CTUCANFD_FILTER_A_VAL        0x40
#define CTUCANFD_FILTER_B_MASK       0x44
#define CTUCANFD_FILTER_B_VAL        0x48
#define CTUCANFD_FILTER_C_MASK       0x4c
#define CTUCANFD_FILTER_C_VAL        0x50
#define CTUCANFD_FILTER_RAN_LOW      0x54
#define CTUCANFD_FILTER_RAN_HIGH     0x58
#define CTUCANFD_FILTER_CONTROL      0x5c
#define CTUCANFD_FILTER_STATUS       0x5e
#define CTUCANFD_RX_MEM_INFO         0x60
#define CTUCANFD_RX_POINTERS         0x64
#define CTUCANFD_RX_STATUS           0x68
#define CTUCANFD_RX_SETTINGS         0x6a
#define CTUCANFD_RX_DATA             0x6c
#define CTUCANFD_TX_STATUS           0x70
#define CTUCANFD_TX_COMMAND          0x74
#define CTUCANFD_TXTB_INFO           0x76
#define CTUCANFD_TX_PRIORITY         0x78
#define CTUCANFD_ERR_CAPT            0x7c
#define CTUCANFD_RETR_CTR            0x7d
#define CTUCANFD_ALC                 0x7e
#define CTUCANFD_TS_INFO             0x7f
#define CTUCANFD_TRV_DELAY           0x80
#define CTUCANFD_SSP_CFG             0x82
#define CTUCANFD_RX_FR_CTR           0x84
#define CTUCANFD_TX_FR_CTR           0x88
#define CTUCANFD_DEBUG_REGISTER      0x8c
#define CTUCANFD_YOLO_REG            0x90
#define CTUCANFD_TIMESTAMP_LOW       0x94
#define CTUCANFD_TIMESTAMP_HIGH      0x98
#define CTUCANFD_TXTB1_DATA_1       0x100
#define CTUCANFD_TXTB1_DATA_2       0x104
#define CTUCANFD_TXTB1_DATA_20      0x14c
#define CTUCANFD_TXTB2_DATA_1       0x200
#define CTUCANFD_TXTB2_DATA_2       0x204
#define CTUCANFD_TXTB2_DATA_20      0x24c
#define CTUCANFD_TXTB3_DATA_1       0x300
#define CTUCANFD_TXTB3_DATA_2       0x304
#define CTUCANFD_TXTB3_DATA_20      0x34c
#define CTUCANFD_TXTB4_DATA_1       0x400
#define CTUCANFD_TXTB4_DATA_2       0x404
#define CTUCANFD_TXTB4_DATA_20      0x44c

#define CTUCANFD_ID 0xCAFD

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Device-specific state inferred from driver usage */
    uint32_t ctucan_id;   /* BAR0 offset 0x0000 */
    uint32_t cra_a2p_ie;  /* BAR0 offset 0x4050 */
    
    /* Core registers (BAR1) */
    uint32_t device_id;   /* CTUCANFD_DEVICE_ID offset 0x0 */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* No interrupt generation logic needed for probe; driver enables A2P_IE but ISR not exercised during probe */
}

/* PIO handlers (unused but required by template) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return ~0ULL;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* MMIO Handlers for BAR0 (PCIe CRA space) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CTUCAN_BAR0_CTUCAN_ID:                 /* 0x0000 */
        val = s->ctucan_id;
        break;
    case CTUCAN_BAR0_CRA_BASE + CYCLONE_IV_CRA_A2P_IE: /* 0x4050 */
        val = s->cra_a2p_ie;
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
    case CTUCAN_BAR0_CRA_BASE + CYCLONE_IV_CRA_A2P_IE: /* 0x4050 */
        s->cra_a2p_ie = val;
        break;
    default:
        /* Ignore writes to all other MMIO offsets */
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* MMIO Handlers for BAR1 (CTU CAN FD Core) */
static uint64_t pcibase_bar1_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CTUCANFD_DEVICE_ID:    /* 0x0 */
        val = s->device_id;
        break;
    /* Additional core registers will be handled as needed */
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_bar1_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PCIBaseState *s = opaque; */
    /* Write placeholders: ignore writes for now during reset */
    switch (addr) {
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_bar1_mmio_ops = {
    .read = pcibase_bar1_mmio_read,
    .write = pcibase_bar1_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->ctucan_id = 1;        /* Report 1 CAN core */
    s->cra_a2p_ie = 0;       /* Interrupt enable off */
    s->device_id = CTUCANFD_ID;
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
        if (bi->index == 1) {
            /* Use separate ops for core registers */
            memory_region_init_io(mr, OBJECT(s), &pcibase_bar1_mmio_ops, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1760 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0xff00 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0c09 ); /* Serial bus controller, CAN? */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR Initialization - sizes inferred from register usage */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x8000, .name = "bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x8000, .name = "bar1" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI Init */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;
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
    /* No additional cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ctucanfd_pci_pci",
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
