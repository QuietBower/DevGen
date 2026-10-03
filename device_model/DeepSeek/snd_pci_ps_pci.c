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

#define TYPE_PCIBASE_DEVICE "snd_pci_ps_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMD 0x1022
#define ACP_DEVICE_ID 0x15E2
#define CLASS_ID 0x0480
#define ACP63_REG_START 0x1240000
#define ACP63_REG_END 0x125C000

/* Register offsets (relative to BAR0 base) */
#define ACP_EXTERNAL_INTR_STAT     0x1808
#define ACP_EXTERNAL_INTR_STAT1    0x1A10
#define ACP_SW0_WAKE_EN            0x1458
#define ACP_SW1_WAKE_EN            0x1460
#define ACP_SW0_I2S_ERROR_REASON   0x18B4
#define ACP_SW1_I2S_ERROR_REASON   0x1A50
#define ACP_ERROR_STATUS           0x18C4

/* Interrupt status bits */
#define ACP_SDW0_STAT              BIT(21)
#define ACP_SDW1_STAT              BIT(2)
#define ACP_ERROR_IRQ              BIT(29)
#define PDM_DMA_STAT               0x10

/* SoundWire DMA interrupt bits */
#define ACP63_SDW_DMA_IRQ_MASK         0x1F800000
#define ACP_AUDIO2_RX_THRESHOLD        0x17
#define ACP_AUDIO1_RX_THRESHOLD        0x19
#define ACP_AUDIO0_RX_THRESHOLD        0x1b
#define ACP_AUDIO2_TX_THRESHOLD        0x18
#define ACP_AUDIO1_TX_THRESHOLD        0x1a
#define ACP_AUDIO0_TX_THRESHOLD        0x1c
#define ACP63_P1_AUDIO1_RX_THRESHOLD   BIT(5)
#define ACP63_P1_AUDIO1_TX_THRESHOLD   BIT(6)
#define ACP70_P1_SDW_DMA_IRQ_MASK      0x1F8
#define ACP70_P1_AUDIO2_RX_THRESHOLD   0x3
#define ACP70_P1_AUDIO1_RX_THRESHOLD   0x5
#define ACP70_P1_AUDIO0_RX_THRESHOLD   0x7
#define ACP70_P1_AUDIO2_TX_THRESHOLD   0x4
#define ACP70_P1_AUDIO1_TX_THRESHOLD   0x6
#define ACP70_P1_AUDIO0_TX_THRESHOLD   0x8

/* ACP70 wake status bits */
#define ACP70_SDW0_HOST_WAKE_STAT      BIT(24)
#define ACP70_SDW1_HOST_WAKE_STAT      BIT(25)
#define ACP70_SDW0_PME_STAT            BIT(26)
#define ACP70_SDW1_PME_STAT            BIT(27)

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
    uint32_t intr_status;      /* Not used, for compatibility */
    uint32_t intr_mask;        /* Not used */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* All known MMIO registers shadowed */
    uint32_t ext_intr_stat;
    uint32_t ext_intr_stat1;
    uint32_t sw0_wake_en;
    uint32_t sw1_wake_en;
    uint32_t sw0_i2s_error_reason;
    uint32_t sw1_i2s_error_reason;
    uint32_t error_status;

    /* DMA Context - not used in this source, removed placeholder */
    /* Status and other placeholders removed */
};


/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int level = (s->ext_intr_stat | s->ext_intr_stat1) ? 1 : 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns – no DMA in this driver, removed placeholder */

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ACP_EXTERNAL_INTR_STAT:
        val = s->ext_intr_stat;
        break;
    case ACP_EXTERNAL_INTR_STAT1:
        val = s->ext_intr_stat1;
        break;
    case ACP_SW0_WAKE_EN:
        val = s->sw0_wake_en;
        break;
    case ACP_SW1_WAKE_EN:
        val = s->sw1_wake_en;
        break;
    case ACP_SW0_I2S_ERROR_REASON:
        val = s->sw0_i2s_error_reason;
        break;
    case ACP_SW1_I2S_ERROR_REASON:
        val = s->sw1_i2s_error_reason;
        break;
    case ACP_ERROR_STATUS:
        val = s->error_status;
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
    case ACP_EXTERNAL_INTR_STAT:
        /* W1C: writing 1 clears corresponding bits, writing 0 has no effect */
        s->ext_intr_stat &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case ACP_EXTERNAL_INTR_STAT1:
        s->ext_intr_stat1 &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case ACP_SW0_WAKE_EN:
        s->sw0_wake_en = (uint32_t)val;
        break;
    case ACP_SW1_WAKE_EN:
        s->sw1_wake_en = (uint32_t)val;
        break;
    case ACP_SW0_I2S_ERROR_REASON:
        s->sw0_i2s_error_reason = (uint32_t)val;
        break;
    case ACP_SW1_I2S_ERROR_REASON:
        s->sw1_i2s_error_reason = (uint32_t)val;
        break;
    case ACP_ERROR_STATUS:
        s->error_status = (uint32_t)val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    return;
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

    /* Power-on defaults: all registers zero */
    s->ext_intr_stat = 0;
    s->ext_intr_stat1 = 0;
    s->sw0_wake_en = 0;
    s->sw1_wake_en = 0;
    s->sw0_i2s_error_reason = 0;
    s->sw1_i2s_error_reason = 0;
    s->error_status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1022 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ACP_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0480 );
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: only BAR0 (MMIO) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = ACP63_REG_END - ACP63_REG_START; /* 0x1C000 */
    s->bar_info[0].name = "snd_pci_ps_pci_mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No DMA, timer, or additional init needed */
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
    .name = "snd_pci_ps_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(ext_intr_stat, PCIBaseState),
        VMSTATE_UINT32(ext_intr_stat1, PCIBaseState),
        VMSTATE_UINT32(sw0_wake_en, PCIBaseState),
        VMSTATE_UINT32(sw1_wake_en, PCIBaseState),
        VMSTATE_UINT32(sw0_i2s_error_reason, PCIBaseState),
        VMSTATE_UINT32(sw1_i2s_error_reason, PCIBaseState),
        VMSTATE_UINT32(error_status, PCIBaseState),
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

type_init(pcibase_register_types)
