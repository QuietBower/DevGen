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
#define PEAK_PCI_VENDOR_ID      0x001C
#define PEAK_PCI_DEVICE_ID      0x0001
#define PEAK_PCIEC_DEVICE_ID	0x0002
#define PEAK_PCIEC34_DEVICE_ID	0x000A
#define PEAK_PCI_CFG_SIZE       0x1000
#define PEAK_PCI_CHAN_SIZE      0x0400
#define PEAK_PCI_CHAN_MAX       4

#define PEAK_PCI_CAN_CLOCK	(16000000 / 2)
#define PEAK_PCI_OCR		OCR_TX0_PUSHPULL
#define PEAK_PCI_CDR		(CDR_CBP | CDR_CLKOUT_MASK)
#define CDR_CLK_OFF	0x08

#define PEAK_VER_REG1           0x40
#define PEAK_VER_REG2           0x44
#define PITA_ICR                0x00
#define PITA_GPIOICR            0x18
#define PITA_MISC               0x1C
#define PITA_GPOUT              0x18
#define PITA_GPIN               0x19
#define PITA_GPOEN              0x1A

#define SJA1000_MOD             0x00
#define SJA1000_CMR             0x01
#define SJA1000_SR              0x02
#define SJA1000_IR              0x03
#define SJA1000_IER             0x04
#define SJA1000_BTR0            0x06
#define SJA1000_BTR1            0x07
#define SJA1000_OCR             0x08
#define SJA1000_ECC             0x0C
#define SJA1000_RXERR           0x0E
#define SJA1000_TXERR           0x0F

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
    uint16_t icr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t pita_icr;
    uint32_t pita_gpioicr;
    uint32_t pita_misc;
    uint8_t pita_gpout;
    uint8_t pita_gpin;
    uint8_t pita_gpoen;
    
    uint8_t sja_mod[PEAK_PCI_CHAN_MAX];
    uint8_t sja_cmr[PEAK_PCI_CHAN_MAX];
    uint8_t sja_sr[PEAK_PCI_CHAN_MAX];
    uint8_t sja_ir[PEAK_PCI_CHAN_MAX];
    uint8_t sja_ier[PEAK_PCI_CHAN_MAX];
    uint8_t sja_btr0[PEAK_PCI_CHAN_MAX];
    uint8_t sja_btr1[PEAK_PCI_CHAN_MAX];
    uint8_t sja_ocr[PEAK_PCI_CHAN_MAX];
    uint8_t sja_ecc[PEAK_PCI_CHAN_MAX];
    uint8_t sja_rxerr[PEAK_PCI_CHAN_MAX];
    uint8_t sja_txerr[PEAK_PCI_CHAN_MAX];

};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = (s->pita_icr & s->icr_mask) != 0;

    if (irq_active) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x400 || (addr < 0x40 && size == 1 && addr != 0x1F)) {
        int chan = addr / PEAK_PCI_CHAN_SIZE;
        int offset = addr % PEAK_PCI_CHAN_SIZE;
        if (chan >= PEAK_PCI_CHAN_MAX) return 0;
        
        switch (offset) {
            case 0x00: val = s->sja_mod[chan]; break;
            case 0x04: val = s->sja_cmr[chan]; break;
            case 0x08: val = s->sja_sr[chan]; break;
            case 0x0C: val = s->sja_ir[chan]; break;
            case 0x10: val = s->sja_ier[chan]; break;
            case 0x18: val = s->sja_btr0[chan]; break;
            case 0x1C: val = s->sja_btr1[chan]; break;
            case 0x20: val = s->sja_ocr[chan]; break;
            case 0x30: val = s->sja_ecc[chan]; break;
            case 0x38: val = s->sja_rxerr[chan]; break;
            case 0x3C: val = s->sja_txerr[chan]; break;
        }
        return val;
    }

    switch (addr) {
        case 0x00:
            if (size == 2) val = s->pita_icr;
            break;
        case 0x02:
            if (size == 2) val = s->icr_mask;
            break;
        case 0x18:
            if (size == 1) val = s->pita_gpout;
            break;
        case 0x19:
            if (size == 1) val = s->pita_gpin;
            break;
        case 0x1A:
            if (size == 1) val = s->pita_gpoen;
            break;
        case 0x40:
            if (size == 4) val = 0;
            break;
        case 0x44:
            if (size == 4) val = 0;
            break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x400 || (addr < 0x40 && size == 1 && addr != 0x1F)) {
        int chan = addr / PEAK_PCI_CHAN_SIZE;
        int offset = addr % PEAK_PCI_CHAN_SIZE;
        if (chan >= PEAK_PCI_CHAN_MAX) return;

        switch (offset) {
            case 0x00: s->sja_mod[chan] = val; break;
            case 0x04: s->sja_cmr[chan] = val; break;
            case 0x08: s->sja_sr[chan] = val; break;
            case 0x0C: s->sja_ir[chan] = val; break;
            case 0x10: s->sja_ier[chan] = val; break;
            case 0x18: s->sja_btr0[chan] = val; break;
            case 0x1C: s->sja_btr1[chan] = val; break;
            case 0x20: s->sja_ocr[chan] = val; break;
            case 0x30: s->sja_ecc[chan] = val; break;
            case 0x38: s->sja_rxerr[chan] = val; break;
            case 0x3C: s->sja_txerr[chan] = val; break;
        }
        return;
    }

    switch (addr) {
        case 0x00:
            if (size == 2) {
                s->pita_icr &= ~val;
                pcibase_update_irq(s);
            }
            break;
        case 0x02:
            if (size == 2) {
                s->icr_mask = val;
                pcibase_update_irq(s);
            }
            break;
        case 0x18:
            if (size == 1) s->pita_gpioicr = val;
            break;
        case 0x1A:
            if (size == 2) {
                /* PITA_GPIOICR + 2 */
            }
            break;
        case 0x1F:
            if (size == 1) {
                /* PITA_MISC + 3 */
            }
            break;
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

    s->pita_icr = 0;
    s->icr_mask = 0;
    s->pita_gpioicr = 0;
    s->pita_misc = 0;
    s->pita_gpout = 0;
    s->pita_gpin = 0;
    s->pita_gpoen = 0;

    for (int i = 0; i < PEAK_PCI_CHAN_MAX; i++) {
        s->sja_mod[i] = 0x01;
        s->sja_cmr[i] = 0;
        s->sja_sr[i] = 0x0C;
        s->sja_ir[i] = 0;
        s->sja_ier[i] = 0;
        s->sja_btr0[i] = 0;
        s->sja_btr1[i] = 0;
        s->sja_ocr[i] = 0;
        s->sja_ecc[i] = 0;
        s->sja_rxerr[i] = 0;
        s->sja_txerr[i] = 0;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x001C );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0001 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0C09 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = PEAK_PCI_CFG_SIZE, .name = "peak_pci_cfg" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = PEAK_PCI_CHAN_SIZE * PEAK_PCI_CHAN_MAX, .name = "peak_pci_reg" };  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pci_set_word(pci_conf + 0x2E, 12);
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
