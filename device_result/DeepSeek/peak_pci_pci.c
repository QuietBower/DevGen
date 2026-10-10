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

#define TYPE_PCIBASE_DEVICE "peak_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PEAK_PCI_VENDOR_ID 0x001C
#define PEAK_PCI_DEVICE_ID 0x0001
#define PEAK_PCI_CLASS_ID   0x0C0900

#define PEAK_VER_REG1       0x40
#define PEAK_VER_REG2       0x44
#define PEAK_PCI_CAN_CLOCK  (16000000 / 2)
#define PEAK_PCI_CDR        (CDR_CBP | CDR_CLKOUT_MASK)
#define PEAK_PCI_OCR        OCR_TX0_PUSHPULL

#define PITA_ICR            0x00
#define PITA_GPIOICR        0x18
#define PITA_MISC           0x1C

#define PEAK_PCI_CFG_SIZE   0x1000
#define PEAK_PCI_CHAN_SIZE  0x0400
#define PEAK_PCI_CHAN_MAX   4

#define PITA_GPOUT          0x18
#define PITA_GPIN           0x19
#define PITA_GPOEN          0x1A

#define PITA_GPIN_SCL       0x01
#define PITA_GPIN_SDA       0x04

#define PCA9553_1_SLAVEADDR (0xC4 >> 1)
#define PCA9553_ON          PCA9553_LOW
#define PCA9553_OFF         PCA9553_HIGHZ
#define PCA9553_SLOW        PCA9553_PWM0
#define PCA9553_FAST        PCA9553_PWM1
#define PCA9553_LED(c)      (1 << (c))
#define PCA9553_LED_STATE(s, c) ((s) << ((c) << 1))
#define PCA9553_LED_ON(c)   PCA9553_LED_STATE(PCA9553_ON, c)
#define PCA9553_LED_OFF(c)  PCA9553_LED_STATE(PCA9553_OFF, c)
#define PCA9553_LED_SLOW(c) PCA9553_LED_STATE(PCA9553_SLOW, c)
#define PCA9553_LED_FAST(c) PCA9553_LED_STATE(PCA9553_FAST, c)
#define PCA9553_LED_MASK(c) PCA9553_LED_STATE(0x03, c)
#define PCA9553_LED_OFF_ALL (PCA9553_LED_OFF(0) | PCA9553_LED_OFF(1))
#define PCA9553_LS0_INIT    0x40

#define MOD_RM              0x01
#define SJA1000_MOD         0x00
#define SJA1000_ECHO_SKB_MAX 1
#define SJA1000_RXERR       0x0E
#define SJA1000_TXERR       0x0F
#define SJA1000_BTR0        0x06
#define SJA1000_BTR1        0x07
#define SJA1000_IER         0x04
#define IRQ_OFF             0x00
#define SJA1000_OCR         0x08
#define SJA1000_ACCC2       0x12
#define SJA1000_ACCM0       0x14
#define SJA1000_ACCC0       0x10
#define SJA1000_ACCC3       0x13
#define SJA1000_CDR         0x1F
#define SJA1000_ACCM1       0x15
#define SJA1000_ACCC1       0x11
#define SJA1000_ACCM2       0x16
#define SJA1000_ACCM3       0x17
#define SJA1000_QUIRK_NO_CDR_REG BIT(1)
#define SJA1000_ECC         0x0C
#define SJA1000_IR          0x03
#define SJA1000_CUSTOM_IRQ_HANDLER BIT(0)
#define SJA1000_FI_RTR      0x40
#define SJA1000_FI_FF       0x80
#define CMD_TR              0x01
#define SJA1000_ID4         0x14
#define SJA1000_ID3         0x13
#define SJA1000_EFF_BUF     0x15
#define CMD_AT              0x02
#define SJA1000_ID2         0x12
#define SJA1000_ID1         0x11
#define SJA1000_FI          0x10
#define SJA1000_SFF_BUF     0x13
#define CMD_SRR             0x10
#define MOD_LOM             0x02
#define IRQ_ALL             0xFF
#define IRQ_BEI             0x80
#define MOD_STM             0x04
#define SJA1000_CMR         0x01
#define SJA1000_SR          0x02

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t cfg_mem[PEAK_PCI_CFG_SIZE];
    uint8_t chan_mem[PEAK_PCI_CHAN_MAX * PEAK_PCI_CHAN_SIZE];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* Not used in this implementation */
}

/* MMIO Handlers: BAR0 (config) */
static uint64_t pcibase_mmio_cfg_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size <= PEAK_PCI_CFG_SIZE) {
        memcpy(&val, s->cfg_mem + addr, size);
    }
    return val;
}

static void pcibase_mmio_cfg_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size <= PEAK_PCI_CFG_SIZE) {
        memcpy(s->cfg_mem + addr, &val, size);
    }
}

/* MMIO Handlers: BAR1 (CAN channels) */
static uint64_t pcibase_mmio_chan_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size <= PEAK_PCI_CHAN_MAX * PEAK_PCI_CHAN_SIZE) {
        memcpy(&val, s->chan_mem + addr, size);
    }
    return val;
}

static void pcibase_mmio_chan_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size <= PEAK_PCI_CHAN_MAX * PEAK_PCI_CHAN_SIZE) {
        memcpy(s->chan_mem + addr, &val, size);
    }
}

static const MemoryRegionOps pcibase_mmio_ops_cfg = {
    .read = pcibase_mmio_cfg_read,
    .write = pcibase_mmio_cfg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_mmio_ops_chan = {
    .read = pcibase_mmio_chan_read,
    .write = pcibase_mmio_chan_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->cfg_mem, 0, sizeof(s->cfg_mem));
    memset(s->chan_mem, 0, sizeof(s->chan_mem));
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
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops_cfg, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops_chan, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* No PIO used */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PEAK_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PEAK_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PEAK_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = PEAK_PCI_CFG_SIZE, .name = "peak-pci-config" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = PEAK_PCI_CHAN_SIZE * PEAK_PCI_CHAN_MAX, .name = "peak-pci-channels" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt not explicitly exercised during probe; no IRQ init needed */
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
    .name = "peak_pci_pci",
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
