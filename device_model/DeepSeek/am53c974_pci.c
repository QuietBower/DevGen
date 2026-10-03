/* Integrated QEMU PCI device emulation for am53c974 SCSI controller */
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

/* Additional include files retrieved from driver context */
/* No extra includes needed */

#define TYPE_PCIBASE_DEVICE "am53c974_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMD         0x1022
#define PCI_DEVICE_ID_AMD_SCSI    0x2020
#define PCI_CLASS_STORAGE_SCSI    0x0100

/* ESP Core register offsets (logical register indices, multiplied by 4 for actual IO address) */
#define ESP_TCLOW    0x00
#define ESP_TCMED    0x01
#define ESP_TCHI     0x02   /* Transfer Counter High (if FENAB enabled) */
#define ESP_FIFO     0x02   /* FIFO data register (when FENAB=0) */
#define ESP_CMD      0x03
#define ESP_STAT     0x04   /* Status register */
#define ESP_INTRPT   0x05
#define ESP_STP      0x06
#define ESP_SOFF     0x07
#define ESP_CFG1     0x08
#define ESP_CFACT    0x09
#define ESP_CFG2     0x0b
#define ESP_CFG3     0x0c
#define ESP_CFG4     0x0d
#define ESP_UID      0x0e
#define ESP_FFLAGS   0x0f   /* FIFO flags */
#define ESP_TIMEO    0x05   /* same as INTRPT */

/* DMA register offsets (logical register indices) */
#define ESP_DMA_CMD    0x10
#define ESP_DMA_STC    0x11   /* Transfer count (32-bit) */
#define ESP_DMA_SPA    0x12   /* Start address (32-bit) */
#define ESP_DMA_WBC    0x13   /* Write-back count (32-bit) */
#define ESP_DMA_WAC    0x14   /* Write-back address count (32-bit) */
#define ESP_DMA_STATUS 0x15   /* DMA status byte */
#define ESP_DMA_SMDLA  0x16
#define ESP_DMA_WMAC   0x17

/* ESP register masks and bit definitions (extracted from driver) */
#define ESP_DMA_CMD_IDLE   0x00
#define ESP_DMA_CMD_BLAST  0x01
#define ESP_DMA_CMD_ABORT  0x02
#define ESP_DMA_CMD_START  0x03
#define ESP_DMA_CMD_MASK   0x03
#define ESP_DMA_CMD_DIAG   0x04
#define ESP_DMA_CMD_MDL    0x10
#define ESP_DMA_CMD_INTE_P 0x20
#define ESP_DMA_CMD_INTE_D 0x40
#define ESP_DMA_CMD_DIR    0x80

#define ESP_DMA_STAT_PWDN   0x01
#define ESP_DMA_STAT_ERROR  0x02
#define ESP_DMA_STAT_ABORT  0x04
#define ESP_DMA_STAT_DONE   0x08
#define ESP_DMA_STAT_SCSIINT 0x10
#define ESP_DMA_STAT_BCMPLT 0x20

#define ESP_CONFIG2_FENAB     0x40
#define ESP_CONFIG4_RAE       0x08
#define ESP_CONFIG4_RADE      0x04
#define ESP_CONFIG2_MAGIC     0xe0
#define ESP_CONFIG1_PENABLE   0x10
#define ESP_CONFIG1_SRRDISAB  0x40
#define ESP_CONFIG2_SCSI2ENAB 0x08
#define ESP_CONFIG2_REGPARITY 0x02
#define ESP_CONFIG2_HMEFENAB  0x10
#define ESP_CONFIG2_HME32     0x80
#define ESP_CONFIG3_OBPUSH    0x80
#define ESP_CONFIG3_IDBIT3    0x20
#define ESP_CONFIG3_FCLOCK    0x01
#define ESP_CONFIG3_FCLK      0x08
#define ESP_CONFIG4_GE1       0x80

/* EEPROM related defines */
#define DC390_EEPROM_LEN 0x40   /* 64 words */
#define DC390_EEPROM_READ 0x80

/* Internal flags for driver (used in device state) */
#define ESP_FLAG_DIFFERENTIAL  0x00000001
#define ESP_FLAG_RESETTING     0x00000002
#define ESP_FLAG_WIDE_CAPABLE  0x00000008
#define ESP_FLAG_QUICKIRQ_CHECK 0x00000010
#define ESP_FLAG_DISABLE_SYNC  0x00000020
#define ESP_FLAG_USE_FIFO      0x00000040
#define ESP_FLAG_NO_DMA_MAP    0x00000080

/* Missing defines updated from driver source */
#define ESP_FF_FBYTES  0x1f
#define ESP_CMD_DMA    0x80
#define ESP_STAT_PMASK 0x07
#define ESP_DOP        0
#define ESP_STAT_PIO   0x01
#define ESP_CMD_RESET  0x80

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
    #define AM53C974_REG_SIZE 0x100
    uint8_t regs[AM53C974_REG_SIZE];

    /* DMA Context */
    struct {
        dma_addr_t addr;
        uint32_t count;
        uint8_t cmd;
        uint8_t status;
        bool active;
        uint32_t residual;
        uint32_t stc;
        uint32_t spa;
        uint32_t wbc;
        uint32_t wac;
    } dma;

    uint32_t flags;     /* Operational status flags */
    bool is_resetting;  /* Reset state */
    uint16_t eeprom[64]; /* EEPROM contents */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t dma_status = s->dma.status;
    uint8_t intr = s->regs[ESP_INTRPT];
    bool irq_pending = false;

    /* Check DMA interrupt conditions */
    if (dma_status & (ESP_DMA_STAT_DONE | ESP_DMA_STAT_ERROR |
                      ESP_DMA_STAT_ABORT | ESP_DMA_STAT_SCSIINT)) {
        irq_pending = true;
    }

    /* Also check ESP interrupt (could be from INTRPT register) */
    if (intr & 0x80) {  /* generic INT pending */
        irq_pending = true;
    }

    /* In real hardware, SCSI interrupt is also routed through INTRPT */
    if (s->intr_status) {
        irq_pending = true;
    }

    pci_set_irq(pdev, irq_pending ? 1 : 0);
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_do_dma(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t cmd = s->dma.cmd;
    uint32_t count = s->dma.stc;
    dma_addr_t addr = s->dma.spa;
    bool is_read; /* from PCI perspective: read from memory or write to memory */

    if (!count || !(cmd & ESP_DMA_CMD_START))
        return;

    /* Direction logic: from driver, when write=1 (SCSI data out, writing
     * to device), they set DIR bit. The DMA engine transfers from system
     * memory to SCSI bus (pci_dma_read). When write=0 (SCSI data in),
     * DIR not set: transfer from SCSI bus to system memory (pci_dma_write).
     */
    is_read = (cmd & ESP_DMA_CMD_DIR) ? true : false;

    s->dma.active = true;
    s->dma.status = 0;

    if (is_read) {
        /* DMA read: host memory -> SCSI bus (device reads from host) */
        pci_dma_read(pdev, addr, s->regs + 0x100, count); /* temp buffer */
    } else {
        /* DMA write: SCSI bus -> host memory (device writes to host) */
        /* In real hardware, data would come from SCSI bus; we emulate zero data */
        uint8_t zero_buf[1024];
        if (count > sizeof(zero_buf)) count = sizeof(zero_buf);
        memset(zero_buf, 0, count);
        pci_dma_write(pdev, addr, zero_buf, count);
    }

    s->dma.status |= ESP_DMA_STAT_DONE;
    s->dma.cmd &= ~ESP_DMA_CMD_START; /* clear start bit */

    /* If DMA interrupt enable bits are set, signal interrupt */
    if (cmd & (ESP_DMA_CMD_INTE_P | ESP_DMA_CMD_INTE_D)) {
        s->intr_status |= 1; /* DMA interrupt */
    }
    pcibase_update_irq(s);
}

/* PIO Handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg = addr >> 2;
    uint64_t val = 0;

    if (size == 4) {
        switch (reg) {
        case ESP_DMA_STC:
            val = s->dma.stc;
            break;
        case ESP_DMA_SPA:
            val = s->dma.spa;
            break;
        case ESP_DMA_WBC:
            val = s->dma.wbc;
            break;
        case ESP_DMA_WAC:
            val = s->dma.wac;
            break;
        default:
            /* For other 32-bit reads, return 0 */
            val = 0;
            break;
        }
    } else if (size == 1) {
        switch (reg) {
        case ESP_TCLOW:
        case ESP_TCMED:
        case ESP_TCHI:
        case ESP_CMD:
        case ESP_INTRPT:
        case ESP_STP:
        case ESP_SOFF:
        case ESP_CFG1:
        case ESP_CFACT:
        case ESP_CFG2:
        case ESP_CFG3:
        case ESP_CFG4:
        case ESP_UID:
            val = s->regs[reg];
            break;
        case ESP_FFLAGS:
            /* FIFO flags: return number of bytes in FIFO (0 for now) */
            val = 0;
            break;
        case ESP_STAT:
            /* Status register: return current bus phase etc. */
            val = s->regs[reg];
            break;
        case ESP_DMA_CMD:
            val = s->dma.cmd;
            break;
        case ESP_DMA_STATUS:
            val = s->dma.status;
            break;
        default:
            val = s->regs[reg];
            break;
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg = addr >> 2;

    if (size == 4) {
        switch (reg) {
        case ESP_DMA_STC:
            s->dma.stc = val;
            break;
        case ESP_DMA_SPA:
            s->dma.spa = val;
            break;
        case ESP_DMA_WBC:
            s->dma.wbc = val;
            break;
        case ESP_DMA_WAC:
            s->dma.wac = val;
            break;
        }
    } else if (size == 1) {
        switch (reg) {
        case ESP_TCLOW:
        case ESP_TCMED:
        case ESP_TCHI:
        case ESP_STP:
        case ESP_SOFF:
        case ESP_CFG1:
        case ESP_CFACT:
        case ESP_CFG2:
        case ESP_CFG3:
        case ESP_CFG4:
        case ESP_UID:
            s->regs[reg] = val & 0xff;
            break;
        case ESP_CMD:
            s->regs[reg] = val & 0xff;
            if (val & ESP_CMD_RESET) {
                /* Perform chip reset */
                memset(s->regs, 0, sizeof(s->regs));
                s->dma.cmd = 0;
                s->dma.status = 0;
                s->intr_status = 0;
                s->is_resetting = false;
                s->regs[ESP_INTRPT] = 0x80; /* Reset completed interrupt */
                pcibase_update_irq(s);
            }
            break;
        case ESP_INTRPT:
            /* Writing to INTRPT clears interrupts */
            s->regs[reg] &= ~val;
            s->intr_status &= ~val;
            pcibase_update_irq(s);
            break;
        case ESP_DMA_CMD:
            s->dma.cmd = val & 0xff;
            if ((val & ESP_DMA_CMD_MASK) == ESP_DMA_CMD_START) {
                pcibase_do_dma(s);
            }
            break;
        case ESP_DMA_STATUS:
            /* Writing to DMA status may clear bits (W1C) */
            s->dma.status &= ~val;
            pcibase_update_irq(s);
            break;
        default:
            s->regs[reg] = val & 0xff;
            break;
        }
    }
}

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
    /* Reset all registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->dma.cmd = 0;
    s->dma.status = 0;
    s->dma.stc = 0;
    s->dma.spa = 0;
    s->dma.wbc = 0;
    s->dma.wac = 0;
    s->dma.active = false;
    s->dma.residual = 0;
    s->flags = 0;
    s->is_resetting = false;
    s->intr_status = 0;
    s->intr_mask = 0;
    /* Set chip ID: AMD 53C974 identifier */
    s->regs[ESP_UID] = 0x0b;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_AMD);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_AMD_SCSI);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SCSI);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    /* Driver uses only BAR0 (I/O ports) for both ESP and DMA registers */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x100, .name = "esp-io" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI or MSI-X used by driver */

    /* DMA configuration: already zero-initialized */

    /* No internal hardware timers used */

    /* Final state initialization */
    s->dma.active = false;
    s->dma.cmd = 0;
    s->dma.status = 0;
    s->dma.stc = 0;
    s->dma.spa = 0;
    s->dma.wbc = 0;
    s->dma.wac = 0;
    s->dma.residual = 0;
    s->flags = 0;
    s->is_resetting = false;
    s->regs[ESP_UID] = 0x0b;
    /* Initialize EEPROM with zeros */
    memset(s->eeprom, 0, sizeof(s->eeprom));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* Nothing else to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "am53c974_pci",
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