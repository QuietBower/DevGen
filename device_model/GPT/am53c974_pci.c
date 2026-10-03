/*
 * QEMU PCI device model for AMD AM53C974 SCSI (am53c974.c Linux driver)
 *
 * Phase 2: minimal functional behavior sufficient for Linux driver probe
 *
 * NOTE: Implemented strictly from visible driver behavior; no undocumented
 * hardware features are modeled.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "am53c974_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* DMA / ESP-related register indices and constants mirrored from driver */
#define ESP_DMA_CMD 0x10
#define ESP_DMA_STC 0x11
#define ESP_DMA_SPA 0x12
#define ESP_DMA_WBC 0x13
#define ESP_DMA_WAC 0x14
#define ESP_DMA_STATUS 0x15
#define ESP_DMA_SMDLA 0x16
#define ESP_DMA_WMAC 0x17

#define ESP_DMA_CMD_IDLE  0x00
#define ESP_DMA_CMD_BLAST 0x01
#define ESP_DMA_CMD_ABORT 0x02
#define ESP_DMA_CMD_START 0x03
#define ESP_DMA_CMD_MASK  0x03
#define ESP_DMA_CMD_DIAG  0x04
#define ESP_DMA_CMD_MDL   0x10
#define ESP_DMA_CMD_INTE_P 0x20
#define ESP_DMA_CMD_INTE_D 0x40
#define ESP_DMA_CMD_DIR   0x80

#define ESP_DMA_STAT_PWDN   0x01
#define ESP_DMA_STAT_ERROR  0x02
#define ESP_DMA_STAT_ABORT  0x04
#define ESP_DMA_STAT_DONE   0x08
#define ESP_DMA_STAT_SCSIINT 0x10
#define ESP_DMA_STAT_BCMPLT 0x20

/* FIFO flags register index used by driver (ESP_FFLAGS) */
#define ESP_FFLAGS 0x07UL
#define ESP_FF_FBYTES 0x1f

/* ESP core registers indices for transfer count / UID used in driver */
#define ESP_TCLOW  0x00UL
#define ESP_TCMED  0x01UL
#define ESP_TCHI   0x0eUL
#define ESP_UID    ESP_TCHI

/* ESP status / interrupt related indices used by generic ESP core */
#define ESP_INTRPT 0x05UL
#define ESP_SSTEP  0x06UL
#define ESP_CFG1   0x08UL
#define ESP_CFACT  0x09UL
#define ESP_CFG2   0x0bUL
#define ESP_CFG3   0x0cUL
#define ESP_CFG4   0x0dUL

#define ESP_STAT_PMASK 0x07
#define ESP_STAT_PIO   0x01
#define ESP_DOP        0
#define ESP_DIP        ESP_STAT_PIO

#define ESP_CMD_DMA 0x80

#define ESP_CONFIG2_FENAB 0x40

#define ESP_CONFIG1_PENABLE  0x10
#define ESP_CONFIG1_SRRDISAB 0x40

#define ESP_CONFIG2_MAGIC    0xe0
#define ESP_CONFIG2_SCSI2ENAB 0x08
#define ESP_CONFIG2_REGPARITY 0x02
#define ESP_CONFIG2_HMEFENAB  0x10
#define ESP_CONFIG2_HME32     0x80

#define ESP_CONFIG3_FCLOCK 0x01
#define ESP_CONFIG3_FAST   0x02
#define ESP_CONFIG3_FSCSI  0x10
#define ESP_CONFIG3_IDBIT3 0x20
#define ESP_CONFIG3_EWIDE  0x40
#define ESP_CONFIG3_OBPUSH 0x80

#define ESP_CONFIG4_RADE 0x04
#define ESP_CONFIG4_RAE  0x08
#define ESP_CONFIG4_GE1  0x80

#define ESP_UID_FAM 0xf8
#define ESP_UID_F236 0x02
#define ESP_UID_HME  0x0a
#define ESP_UID_FSC  0x14

#define ESP_FAMILY(uid) (((uid) & ESP_UID_FAM) >> 3)

/* EEPROM constants used by driver */
#define DC390_EEPROM_READ 0x80
#define DC390_EEPROM_LEN  0x40
#define DC390_EE_MODE1 0x00
#define DC390_EE_SPEED 0x01
#define DC390_EE_ADAPT_SCSI_ID 0x40
#define DC390_EE_MODE2 0x41
#define DC390_EE_DELAY 0x42
#define DC390_EE_TAG_CMD_NUM 0x43

#define DC390_EE_MODE1_PARITY_CHK   0x01
#define DC390_EE_MODE1_SYNC_NEGO    0x02
#define DC390_EE_MODE1_EN_DISC      0x04
#define DC390_EE_MODE1_SEND_START   0x08
#define DC390_EE_MODE1_TCQ          0x10

#define DC390_EE_MODE2_MORE_2DRV    0x01
#define DC390_EE_MODE2_GREATER_1G   0x02
#define DC390_EE_MODE2_RST_SCSI_BUS 0x04
#define DC390_EE_MODE2_ACTIVE_NEGATION 0x08
#define DC390_EE_MODE2_NO_SEEK      0x10
#define DC390_EE_MODE2_LUN_CHECK    0x20

/* Some ESP flags from generic core used by pci driver */
#define ESP_FLAG_USE_FIFO       0x00000040
#define ESP_FLAG_DISABLE_SYNC   0x00000020
#define ESP_FLAG_QUICKIRQ_CHECK 0x00000010
#define ESP_FLAG_WIDE_CAPABLE   0x00000008
#define ESP_FLAG_RESETTING      0x00000002
#define ESP_FLAG_DIFFERENTIAL   0x00000001
#define ESP_FLAG_NO_DMA_MAP     0x00000080

/* Simple enum for revision (not used functionally here) */
enum esp_rev {
    ESP100,
    ESP100A,
    ESP236,
    FAS236,
    PCSCSI,
    FSC,
    FAS100A,
    FAST,
    FASHME,
};

/* BAR metadata definition */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* Device State */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Simple register shadows for DMA engine and FIFO */
    uint8_t esp_regs[0x40];    /* core ESP regs windowed by *4 */

    uint8_t dma_cmd;
    uint8_t dma_status;
    uint32_t dma_stc;      /* esp_count as written to DMA_STC */
    uint32_t dma_spa;      /* system memory addr for DMA */

    /* Interrupt line state */
    bool irq_level;
};

/* Forward declarations */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Helper to raise/lower legacy INTx */
static void pcibase_set_irq(PCIBaseState *s, bool level)
{
    if (s->irq_level == level) {
        return;
    }
    s->irq_level = level;
    pci_set_irq(PCI_DEVICE(s), level ? 1 : 0);
}

/* Simple DMA completion helper: mark DONE+SCSIINT and assert IRQ */
static void pcibase_dma_complete(PCIBaseState *s)
{
    s->dma_status |= ESP_DMA_STAT_DONE | ESP_DMA_STAT_SCSIINT | ESP_DMA_STAT_BCMPLT;
    pcibase_set_irq(s, true);
}

static uint8_t pcibase_read_esp_reg(PCIBaseState *s, unsigned long reg)
{
    if (reg < sizeof(s->esp_regs)) {
        return s->esp_regs[reg];
    }
    return 0;
}

static void pcibase_write_esp_reg(PCIBaseState *s, uint8_t val, unsigned long reg)
{
    if (reg < sizeof(s->esp_regs)) {
        s->esp_regs[reg] = val;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "[%s] mmio_read unexpected size=%u addr=0x%" PRIx64 "\n",
                      TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return 0xff;
    }

    /* Driver uses esp->regs + reg*4, so MMIO layout is 32-bit spaced */
    unsigned long reg = addr >> 2; /* map byte addr to logical reg index */

    switch (reg) {
    case ESP_DMA_STATUS:
        return s->dma_status;
    case ESP_DMA_CMD:
        return s->dma_cmd;
    case ESP_DMA_STC:
        /* low byte of dma_stc */
        return (uint8_t)(s->dma_stc & 0xff);
    case ESP_DMA_SPA:
        /* low byte of dma_spa */
        return (uint8_t)(s->dma_spa & 0xff);
    case ESP_FFLAGS: {
        /* Return FIFO byte count in low 5 bits; no actual FIFO modeled
         * Driver reads only & ESP_FF_FBYTES, so keep 0 meaning empty.
         */
        uint8_t v = s->esp_regs[ESP_FFLAGS] & ESP_FF_FBYTES;
        return v;
    }
    default:
        return pcibase_read_esp_reg(s, reg);
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "[%s] mmio_write unexpected size=%u addr=0x%" PRIx64
                      " val=0x%" PRIx64 "\n",
                      TYPE_PCIBASE_DEVICE, size, (uint64_t)addr, val);
        return;
    }

    uint8_t v = (uint8_t)val;
    unsigned long reg = addr >> 2; /* byte addr to logical reg index */

    switch (reg) {
    case ESP_DMA_CMD:
        s->dma_cmd = v;
        /* Transition handling for DMA state based on driver expectations */
        if ((v & ESP_DMA_CMD_MASK) == ESP_DMA_CMD_IDLE) {
            /* Clear DONE/SCSIINT on idle */
            s->dma_status &= ~(ESP_DMA_STAT_DONE | ESP_DMA_STAT_SCSIINT | ESP_DMA_STAT_BCMPLT);
            pcibase_set_irq(s, false);
        } else if ((v & ESP_DMA_CMD_MASK) == ESP_DMA_CMD_ABORT) {
            s->dma_status |= ESP_DMA_STAT_ABORT;
            /* In real hw, completion of abort would be signaled; we just set bit. */
        } else if ((v & ESP_DMA_CMD_MASK) == ESP_DMA_CMD_BLAST) {
            /* BLAST used in dma_drain(); driver waits while BCMPLT set */
            s->dma_status |= ESP_DMA_STAT_BCMPLT;
        } else if ((v & ESP_DMA_CMD_MASK) == ESP_DMA_CMD_START) {
            /* START after scsi_esp_cmd; we immediately complete for now */
            pcibase_dma_complete(s);
        }
        break;
    case ESP_DMA_STC:
        /* store full 32-bit esp_count in shadow; driver also writes to ESP_TC* */
        /* Only low byte visible through byte MMIO */
        s->dma_stc = (s->dma_stc & 0xffffff00U) | v;
        break;
    case ESP_DMA_SPA:
        /* store low byte of dma address (rest ignored here) */
        s->dma_spa = (s->dma_spa & 0xffffff00U) | v;
        break;
    case ESP_TCLOW: {
        uint32_t old = s->dma_stc;
        s->dma_stc = (old & 0xffffff00U) | v;
        pcibase_write_esp_reg(s, v, reg);
        break;
    }
    case ESP_TCMED: {
        uint32_t old = s->dma_stc;
        s->dma_stc = (old & 0xffff00ffU) | ((uint32_t)v << 8);
        pcibase_write_esp_reg(s, v, reg);
        break;
    }
    case ESP_TCHI: {
        uint32_t old = s->dma_stc;
        s->dma_stc = (old & 0xff00ffffU) | ((uint32_t)v << 16);
        pcibase_write_esp_reg(s, v, reg);
        break;
    }
    default:
        pcibase_write_esp_reg(s, v, reg);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;

    qemu_log_mask(LOG_UNIMP,
                  "[%s] pio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0xff;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;

    qemu_log_mask(LOG_UNIMP,
                  "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    memset(s->esp_regs, 0, sizeof(s->esp_regs));
    s->dma_cmd = 0;
    s->dma_status = 0;
    s->dma_stc = 0;
    s->dma_spa = 0;
    s->irq_level = false;

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    /* Driver uses config byte 0x80/0xc0 for EEPROM bit-banging and reads
     * config byte 0x00, but we don't emulate EEPROM here. We allow generic
     * config writes so those bytes latch in config space.
     */
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* IDs from am53c974 driver header (visible usage: PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_SCSI) */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_AMD);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_AMD_SCSI);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SCSI);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0 is used by driver via pci_iomap(pdev, 0, len). No explicit size,
     * but am53c974 is a small register set; use 256 bytes.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "am53c974-mmio";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;
    k->realize      = pcibase_realize;
    k->exit         = pcibase_uninit;
    dc->reset       = pcibase_reset;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_class_init,
        .interfaces    = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
