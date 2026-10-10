/*
 * QEMU PCI device model for agilent_82350b
 * Phase 2: Basic behavioral emulation sufficient for driver probe/attach.
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
/* #include "hw/gpib/tms9914.h" */

#define TYPE_PCIBASE_DEVICE "agilent_82350b_pci_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define AGILENT_82350B_PCI_VENDOR_ID   0x10b5 /* PCI_VENDOR_ID_PLX */
#define AGILENT_82350B_PCI_DEVICE_ID   0x9050 /* PCI_DEVICE_ID_PLX_9050 */
#define AGILENT_82350B_PCI_CLASS_ID    PCI_CLASS_OTHERS

/* BAR description helpers */
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

/* Minimal register map derived from driver usage */
/* BAR assignment (per driver pci_resource_start/len indices):
 * 82350A: PLX_MEM_REGION, GPIB_82350A_REGION, SRAM_82350A_REGION, BORG_82350A_REGION
 * 82350B/82351A: GPIB_REGION, SRAM_REGION, MISC_REGION
 * The driver uses readb/writeb on these; sizes come from pci_resource_len()
 * which we approximate with power-of-two windows big enough for used offsets.
 */

/* Offsets within GPIB/misc regions used by the driver */
#define REG_EVENT_STATUS          0x00  /* EVENT_STATUS_REG */
#define REG_EVENT_ENABLE          0x01  /* EVENT_ENABLE_REG */
#define REG_INTERRUPT_ENABLE      0x02  /* INTERRUPT_ENABLE_REG */
#define REG_CARD_MODE             0x03  /* CARD_MODE_REG */
#define REG_INTERNAL_CONFIG       0x04  /* INTERNAL_CONFIG_REG */
#define REG_SRAM_ACCESS_CONTROL   0x05  /* SRAM_ACCESS_CONTROL_REG */
#define REG_STREAM_STATUS         0x06  /* STREAM_STATUS_REG */
#define REG_CONFIG_DATA           0x07  /* CONFIG_DATA_REG (borg firmware) */
#define REG_T1_DELAY              0x08  /* T1_DELAY_REG */

#define REG_XFER_COUNT_LO         0x10  /* XFER_COUNT_LO_REG */
#define REG_XFER_COUNT_MID        0x11  /* XFER_COUNT_MID_REG */
#define REG_XFER_COUNT_HI         0x12  /* XFER_COUNT_HI_REG */

/* STREAM_STATUS_REG bits */
#define HALTED_STATUS_BIT         0x01
#define RESTART_STREAM_BIT        0x01

/* EVENT / IRQ bits used by driver */
#define IRQ_STATUS_BIT                    0x01
#define TMS9914_IRQ_STATUS_BIT            0x02
#define BUFFER_END_STATUS_BIT             0x04
#define TERM_COUNT_STATUS_BIT             0x08

#define ENABLE_BUFFER_END_EVENTS_BIT      0x04
#define ENABLE_TERM_COUNT_EVENTS_BIT      0x08

#define ENABLE_TERM_COUNT_INTERRUPT_BIT   0x08
#define ENABLE_BUFFER_END_INTERRUPT_BIT   0x04
#define ENABLE_TMS9914_INTERRUPTS_BIT     0x02

#define ENABLE_PCI_IRQ_BIT                0x01

/* Card mode / internal config bits */
#define CM_SYSTEM_CONTROLLER_BIT          0x10
#define IC_SYSTEM_CONTROLLER_BIT          0x01

/* SRAM access control bits (direction / enable) */
#define DIRECTION_GPIB_TO_HOST            0x01
#define ENABLE_TI_TO_SRAM                 0x02
#define ENABLE_TI_TO_SRAM_G2H             (ENABLE_TI_TO_SRAM | DIRECTION_GPIB_TO_HOST)

/* Model identifiers used in driver */
#define MODEL_82350A                      0
#define MODEL_82350B                      1
#define MODEL_82351A                      2

/* Simple FIFO parameters for emulation */
#define FIFO_SIZE                         4096

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Other additional device-specific state */
    /* struct tms9914_priv tms9914_priv; */
    struct pci_dev *linux_pci_dev_stub;

    /* MMIO-backed regions corresponding to driver ioremaps */
    /* For simplicity we map:
     * BAR0: PLX_MEM_REGION (82350A) or GPIB_REGION (B/B1A)
     * BAR1: GPIB_82350A_REGION or SRAM_REGION
     * BAR2: SRAM_82350A_REGION or MISC_REGION
     * BAR3: BORG_82350A_REGION (82350A only)
     */

    /* Shadow for gpib regs (EVENT, INTERRUPT, CARD_MODE, etc.) */
    uint8_t event_status;
    uint8_t event_enable;
    uint8_t interrupt_enable;
    uint8_t card_mode;
    uint8_t internal_config;
    uint8_t sram_access_ctrl;
    uint8_t stream_status;
    uint8_t config_data_reg;
    uint8_t t1_delay;

    uint8_t xfer_count_lo;
    uint8_t xfer_count_mid;
    uint8_t xfer_count_hi;

    /* Simple SRAM buffer shared for fifo operations */
    uint8_t sram[FIFO_SIZE];

    /* Event status aggregation for read_and_clear_event_status */
    uint16_t event_status_bits_shadow;

    /* Model selection (A/B/51A) */
    int model;

    /* BORG status for firmware loader path (82350A) */
    uint8_t borg_status;

    /* Simple flag to indicate whether firmware has been "loaded" */
    bool firmware_loaded;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Basic level interrupt behavior: if IRQ_STATUS_BIT is set in event_status
     * and interrupts are enabled at card_mode/interrupt_enable, assert IRQ.
     *
     * Driver sets ENABLE_PCI_IRQ_BIT in CARD_MODE_REG and enables specific
     * interrupt bits in INTERRUPT_ENABLE_REG.
     */
    bool want_irq = false;

    if (s->card_mode & ENABLE_PCI_IRQ_BIT) {
        uint8_t enabled_sources = 0;

        if (s->interrupt_enable & ENABLE_TMS9914_INTERRUPTS_BIT) {
            enabled_sources |= TMS9914_IRQ_STATUS_BIT;
        }
        if (s->interrupt_enable & ENABLE_BUFFER_END_INTERRUPT_BIT) {
            enabled_sources |= BUFFER_END_STATUS_BIT;
        }
        if (s->interrupt_enable & ENABLE_TERM_COUNT_INTERRUPT_BIT) {
            enabled_sources |= TERM_COUNT_STATUS_BIT;
        }

        if (s->event_status & enabled_sources) {
            s->event_status |= IRQ_STATUS_BIT;
        } else {
            s->event_status &= ~IRQ_STATUS_BIT;
        }

        if (s->event_status & IRQ_STATUS_BIT) {
            want_irq = true;
        }
    } else {
        s->event_status &= ~IRQ_STATUS_BIT;
    }

    pci_set_irq(pdev, want_irq ? 1 : 0);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* We don't distinguish which BAR here; QEMU passes offset within BAR. */
    uint8_t val8 = 0xff;

    switch (addr) {
    case REG_EVENT_STATUS:
        val8 = s->event_status;
        break;
    case REG_EVENT_ENABLE:
        val8 = s->event_enable;
        break;
    case REG_INTERRUPT_ENABLE:
        val8 = s->interrupt_enable;
        break;
    case REG_CARD_MODE:
        val8 = s->card_mode;
        break;
    case REG_INTERNAL_CONFIG:
        val8 = s->internal_config;
        break;
    case REG_SRAM_ACCESS_CONTROL:
        val8 = s->sram_access_ctrl;
        break;
    case REG_STREAM_STATUS:
        val8 = s->stream_status;
        break;
    case REG_CONFIG_DATA:
        val8 = s->config_data_reg;
        break;
    case REG_T1_DELAY:
        val8 = s->t1_delay;
        break;
    case REG_XFER_COUNT_LO:
        val8 = s->xfer_count_lo;
        break;
    case REG_XFER_COUNT_MID:
        val8 = s->xfer_count_mid;
        break;
    case REG_XFER_COUNT_HI:
        val8 = s->xfer_count_hi;
        break;
    default:
        /* For SRAM/BORG/MISC, treat as simple RAM or 0xff */
        if (addr >= 0x100 && addr < 0x100 + FIFO_SIZE) {
            val8 = s->sram[addr - 0x100];
        } else if (addr == 0x200) {
            /* BORG status readb(a_priv->borg_base) in firmware loader */
            val8 = s->borg_status;
        } else {
            val8 = 0xff;
        }
        break;
    }

    return val8;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v = (uint8_t)val;

    switch (addr) {
    case REG_EVENT_STATUS:
        /* Write-1-to-clear for BUFFER_END_STATUS_BIT and TERM_COUNT_STATUS_BIT */
        if (v & (BUFFER_END_STATUS_BIT | TERM_COUNT_STATUS_BIT)) {
            s->event_status &= ~(v & (BUFFER_END_STATUS_BIT | TERM_COUNT_STATUS_BIT));
        }
        break;
    case REG_EVENT_ENABLE:
        s->event_enable = v;
        break;
    case REG_INTERRUPT_ENABLE:
        s->interrupt_enable = v;
        break;
    case REG_CARD_MODE:
        s->card_mode = v;
        break;
    case REG_INTERNAL_CONFIG:
        s->internal_config = v;
        break;
    case REG_SRAM_ACCESS_CONTROL:
        s->sram_access_ctrl = v;
        break;
    case REG_STREAM_STATUS:
        /* RESTART_STREAM_BIT clears HALTED_STATUS_BIT */
        if (v & RESTART_STREAM_BIT) {
            s->stream_status &= ~HALTED_STATUS_BIT;
        }
        break;
    case REG_CONFIG_DATA:
        /* Firmware loader writes here; mark firmware as loaded near the end. */
        s->config_data_reg = v;
        /* We don't know firmware length; simply note that a write occurred. */
        s->firmware_loaded = true;
        /* When firmware is loaded, BORG_DONE_BIT in borg_status becomes set.
         * We model BORG_READY_BIT and BORG_DONE_BIT as 0x01 and 0x02.
         */
        s->borg_status |= 0x02; /* BORG_DONE_BIT */
        break;
    case REG_T1_DELAY:
        s->t1_delay = v;
        break;
    case REG_XFER_COUNT_LO:
        s->xfer_count_lo = v;
        break;
    case REG_XFER_COUNT_MID:
        s->xfer_count_mid = v;
        break;
    case REG_XFER_COUNT_HI:
        s->xfer_count_hi = v;
        break;
    default:
        /* SRAM or other regions */
        if (addr >= 0x100 && addr < 0x100 + FIFO_SIZE) {
            s->sram[addr - 0x100] = v;
        } else if (addr == 0x200) {
            /* BORG control writes not explicitly modeled */
            s->borg_status = v;
        }
        break;
    }

    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Device does not use IO-space in this driver; return 0xff. */
    return 0xff;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO behavior needed for this device. */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
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

    s->event_status = 0;
    s->event_enable = 0;
    s->interrupt_enable = 0;
    s->card_mode = 0;
    s->internal_config = 0;
    s->sram_access_ctrl = 0;
    s->stream_status = 0;
    s->config_data_reg = 0;
    s->t1_delay = 0;

    s->xfer_count_lo = 0;
    s->xfer_count_mid = 0;
    s->xfer_count_hi = 0;

    s->event_status_bits_shadow = 0;

    memset(s->sram, 0, sizeof(s->sram));

    s->model = MODEL_82350B; /* Default to 82350B/B1A path */

    /* BORG for 82350A: initially not done, not ready */
    s->borg_status = 0x00;
    s->firmware_loaded = false;

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  AGILENT_82350B_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  AGILENT_82350B_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x103c );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x10b0);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, AGILENT_82350B_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * We provide 3 MMIO BARs large enough for the registers the driver uses.
     */
    s->num_bars = 3;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* GPIB / PLX */
    s->bar_info[0].name = "agilent82350b-bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x2000; /* SRAM */
    s->bar_info[1].name = "agilent82350b-bar1";

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x1000; /* MISC/BORG */
    s->bar_info[2].name = "agilent82350b-bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* We do not expose MSI/MSI-X since the driver uses legacy INTx (request_irq with dev->irq). */

    /* Initialize internal state */
    pcibase_reset(DEVICE(pdev));
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "agilent_82350b_pci_driver_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        {.name = NULL}
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

