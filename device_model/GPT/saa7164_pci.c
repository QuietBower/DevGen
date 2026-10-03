/*
 * QEMU PCI device model for saa7164 (behavioral skeleton)
 * Generated to match Linux driver expectations for basic probing.
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
#include "hw/hw.h"

#define TYPE_PCIBASE_DEVICE "saa7164_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SAA7164_VENDOR_ID 0x1131
#define SAA7164_DEVICE_ID 0x7164
#define SAA7164_CLASS_ID  PCI_CLASS_MULTIMEDIA_OTHER

#define SAA_DEVICE_SYSINIT_STATUS      0x70
#define SAA_DEVICE_SYSINIT_MODE        0x74
#define SAA_DEVICE_SYSINIT_SPEC        0x78
#define SAA_DEVICE_SYSINIT_INST        0x7C
#define SAA_DEVICE_SYSINIT_CPULOAD     0x80
#define SAA_DEVICE_SYSINIT_REMAINHEAP  0x84
#define SAA_DEVICE_VERSION             0x30
#define SAA_DOWNLOAD_FLAGS             0x34
#define SAA_BOOTLOADERERROR_FLAGS      0x44
#define SAA_DEVICE_2ND_VERSION         0x50
#define SAA_DEVICE_2ND_DOWNLOADFLAG_OFFSET 0x54
#define SAA_DEVICE_DEADLOCK_DETECTED_OFFSET 0x6C

#define SAA_DEVICE_IMAGE_SEARCHING     0x01
#define SAA_DEVICE_IMAGE_LOADING       0x02
#define SAA_DEVICE_IMAGE_BOOTING       0x03
#define SAA_DEVICE_NO_IMAGE            0x10
#define SAA_DEVICE_IMAGE_CORRUPT       0x04
#define SAA_DEVICE_MEMORY_CORRUPT      0x08
#define SAA_DEVICE_DEADLOCK_DETECTED   0xDEADDEAD

#define SAA_DEVICE_BUFFERBLOCKSIZE     0x1000
#define SAA_DEVICE_2ND_BUFFERBLOCKSIZE 0x100000
#define SAA_DEVICE_DOWNLOAD_OFFSET     0x1000
#define SAA_DEVICE_2ND_DOWNLOAD_OFFSET 0x200000
#define SAA_DEVICE_TIMEOUT             5000

#define SAA_DATAREADY_FLAG_ACK         0x40

#define SAA_DEVICE_MAXREQUESTSIZE      256
#define SAA_CMD_MAX_MSG_UNITS          256

#define SAA7164_MAXBOARDS              8

#define SAA7164_TS_NUMBER_OF_LINES     312

#define SAA7164_PORT_TS1               0
#define SAA7164_PORT_TS2               (SAA7164_PORT_TS1 + 1)
#define SAA7164_PORT_ENC1              (SAA7164_PORT_TS2 + 1)
#define SAA7164_PORT_ENC2              (SAA7164_PORT_ENC1 + 1)
#define SAA7164_PORT_VBI1              (SAA7164_PORT_ENC2 + 1)
#define SAA7164_PORT_VBI2              (SAA7164_PORT_VBI1 + 1)
#define SAA7164_MAX_PORTS              (SAA7164_PORT_VBI2 + 1)

#define SAA7164_PT_ENTRIES             16

#define SAA_DMASTATE_STOP              0x00
#define SAA_DMASTATE_ACQUIRE           0x01
#define SAA_DMASTATE_PAUSE             0x02
#define SAA_DMASTATE_RUN               0x03

#define SAA_STATE_CONTROL              0x03

#define SAA_OK                         0
#define SAA_ERR_EMPTY                  0x22
#define SAA_ERR_NO_RESOURCES           0x0c
#define SAA_ERR_BAD_PARAMETER          0x09
#define SAA_ERR_INVALID_COMMAND        0x3e
#define SAA_ERR_BUSY                   0x15
#define SAA_ERR_TIMEOUT                0x1f
#define SAA_ERR_NOT_SUPPORTED          0x13
#define SAA_ERR_NULL_PACKET            0x59
#define SAA_ERR_OVERFLOW               0x20
#define SAA_ERR_ALREADY_STOPPED        0x26

#define PVC_RESPONSEFLAG_ERROR         0x01
#define PVC_ERRORCODE_UNKNOWN          0x00
#define PVC_ERRORCODE_INVALID_COMMAND  0x01
#define PVC_ERRORCODE_INVALID_CONTROL  0x02
#define PVC_ERRORCODE_INVALID_DATA     0x03
#define PVC_ERRORCODE_TIMEOUT          0x04
#define PVC_ERRORCODE_NAK              0x05

#define PVC_CMDFLAG_CONTINUE           0x10
#define SAA_CMDFLAG_CONTINUE           0x10

#define GET_DESCRIPTORS_CONTROL        0x01
#define GET_FW_STATUS_CONTROL          0x08
#define GET_FW_VERSION_CONTROL         0x09
#define SET_DEBUG_LEVEL_CONTROL        0x0B
#define GET_DEBUG_DATA_CONTROL         0x0C

#define EXU_REGISTER_ACCESS_CONTROL    0x00
#define EXU_GPIO_CONTROL               0x01

#define EU_PROFILE_CONTROL             0x00
#define EU_VIDEO_FORMAT_CONTROL        0x01
#define EU_VIDEO_BIT_RATE_CONTROL      0x02
#define EU_VIDEO_RESOLUTION_CONTROL    0x03
#define EU_VIDEO_GOP_STRUCTURE_CONTROL 0x04
#define EU_AUDIO_FORMAT_CONTROL        0x0C
#define EU_AUDIO_BIT_RATE_CONTROL      0x0D

#define PU_BRIGHTNESS_CONTROL          0x02
#define PU_CONTRAST_CONTROL            0x03
#define PU_HUE_CONTROL                 0x06
#define PU_SATURATION_CONTROL          0x07
#define PU_SHARPNESS_CONTROL           0x08

#define TU_STANDARD_AUTO_CONTROL       0x01

#define SU_INPUT_SELECT_CONTROL        0x01

#define VOLUME_CONTROL                 0x02
#define MUTE_CONTROL                   0x01

#define EU_VIDEO_FORMAT_MPEG_2         0x02

#define EU_VIDEO_BIT_RATE_MODE_CONSTANT       0
#define EU_VIDEO_BIT_RATE_MODE_VARIABLE_PEAK  2

#define EU_PROFILE_PS_DVD              0x06
#define EU_PROFILE_TS_HQ               0x09

#define VS_FORMAT_TYPE                 0x02
#define VS_FORMAT_UNCOMPRESSED         0x04
#define VS_FORMAT_MPEG2PS              0x09
#define VS_FORMAT_MPEG2TS              0x0A
#define VS_FORMAT_VBI                  0x0E
#define VS_FORMAT_RDS                  0x0F

#define VC_INPUT_TERMINAL              0x02
#define VC_OUTPUT_TERMINAL             0x03
#define VC_SELECTOR_UNIT               0x04
#define VC_PROCESSING_UNIT             0x05
#define FEATURE_UNIT                   0x06
#define EXTENSION_UNIT                 0x0B

#define VC_TUNER_PATH                  0xF0
#define TUNER_UNIT                     0x09
#define ENCODER_UNIT                   0x0A
#define PVC_INFRARED_UNIT              0xF3
#define DRM_UNIT                       0xF4
#define GENERAL_REQUEST                0xF5

#define SAA7164_BOARD_UNKNOWN          0
#define SAA7164_BOARD_HAUPPAUGE_HVR2250      3
#define SAA7164_BOARD_HAUPPAUGE_HVR2200     4
#define SAA7164_BOARD_HAUPPAUGE_HVR2200_2   5
#define SAA7164_BOARD_HAUPPAUGE_HVR2200_3   6
#define SAA7164_BOARD_HAUPPAUGE_HVR2250_2   7
#define SAA7164_BOARD_HAUPPAUGE_HVR2250_3   8
#define SAA7164_BOARD_HAUPPAUGE_HVR2200_4   9
#define SAA7164_BOARD_HAUPPAUGE_HVR2200_5   10
#define SAA7164_BOARD_HAUPPAUGE_HVR2255     12
#define SAA7164_BOARD_HAUPPAUGE_HVR2205     13

#define INT_SIZE                        16
#define FIXED_VIDEO_PID                 0xf1
#define FIXED_AUDIO_PID                 0xf2

#define SAA7164_TV_MIN_FREQ            (44U * 16U)

#define SAA_BUS_TIMEOUT                50

#define SAA7164_MAX_UNITS              8

#define DBGLVL_API   32
#define DBGLVL_I2C   16
#define DBGLVL_DVB    8
#define DBGLVL_FW     4
#define DBGLVL_CMD   64
#define DBGLVL_BUF  512
#define DBGLVL_BUS  128
#define DBGLVL_IRQ  256
#define DBGLVL_VBI 2048
#define DBGLVL_ENC 1024
#define DBGLVL_THR 4096
#define DBGLVL_CPU 8192

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
    uint32_t regs[0x1000 / 4]; /* simple MMIO register space shadow for BAR0 */

    /* Interrupt registers base (as seen by driver via dev->int_status/int_ack) */
    hwaddr int_status_base; /* 0x183000 + 0xf80 = 0x183f80 */
    hwaddr int_ack_base;    /* 0x183000 + 0xf90 = 0x183f90 */

    uint32_t int_status_words[INT_SIZE / 4]; /* 4 x u32 interrupt status */

    /* DMA Context */
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = false;
    int i;

    for (i = 0; i < INT_SIZE / 4; i++) {
        if (s->int_status_words[i]) {
            pending = true;
            break;
        }
    }

    if (!pending) {
        if (msi_enabled(pdev)) {
            /* Nothing to signal */
        } else {
            pci_set_irq(pdev, 0);
        }
        return;
    }

    if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
    /* The provided driver source does not expose explicit HW DMA descriptor
     * formats or MMIO-triggered DMA operations, so no DMA engine emulation
     * can be implemented here without speculation.
     */
}

/* Helper: convert MMIO address (in BAR0) into register index */
static inline bool pcibase_addr_in_bar0(hwaddr addr)
{
    /* BAR0 is modeled as 0x1000 bytes */
    return addr < 0x1000;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 1 && size != 2 && size != 4) {
        return 0;
    }

    /* Handle generic register shadow for BAR0 */
    if (pcibase_addr_in_bar0(addr)) {
        unsigned index = addr >> 2;
        uint32_t regv = s->regs[index];

        switch (size) {
        case 1:
            val = (regv >> ((addr & 3) * 8)) & 0xff;
            break;
        case 2:
            val = (regv >> ((addr & 2) * 8)) & 0xffff;
            break;
        case 4:
            val = regv;
            break;
        }

        /* Provide specific semantics for some firmware-related registers
         * used early in probe via saa7164_getfirmwarestatus() and
         * saa7164_getcurrentfirmwareversion().
         */
        if (addr == SAA_DEVICE_SYSINIT_STATUS) {
            /* Pretend firmware is running and OK. Use IMAGE_BOOTING. */
            val = SAA_DEVICE_IMAGE_BOOTING;
        } else if (addr == SAA_DEVICE_SYSINIT_MODE) {
            val = 0; /* arbitrary stable mode */
        } else if (addr == SAA_DEVICE_SYSINIT_SPEC) {
            val = 0;
        } else if (addr == SAA_DEVICE_SYSINIT_INST) {
            val = 0;
        } else if (addr == SAA_DEVICE_SYSINIT_CPULOAD) {
            val = 0; /* no load */
        } else if (addr == SAA_DEVICE_SYSINIT_REMAINHEAP) {
            val = 0x100000; /* some heap */
        } else if (addr == SAA_DEVICE_VERSION) {
            /* Return a non-zero, plausible version value. The driver only
             * uses bitfields to print the version; no functional dependency
             * is visible in the provided code.
             */
            val = 0x00010001; /* arbitrary stable version */
        }

        return val;
    }

    /* Interrupt status/ack region as used via dev->int_status/int_ack.
     * In saa7164_dev_setup():
     *   dev->int_status = 0x183000 + 0xf80;
     *   dev->int_ack    = 0x183000 + 0xf90;
     * These are used as absolute offsets from bmmio (BAR0 base).
     */
    if (addr >= s->int_status_base && addr < s->int_status_base + INT_SIZE) {
        unsigned word = (addr - s->int_status_base) >> 2;
        uint32_t regv = s->int_status_words[word];
        switch (size) {
        case 1:
            val = (regv >> ((addr & 3) * 8)) & 0xff;
            break;
        case 2:
            val = (regv >> ((addr & 2) * 8)) & 0xffff;
            break;
        case 4:
            val = regv;
            break;
        }
        return val;
    }

    if (addr >= s->int_ack_base && addr < s->int_ack_base + INT_SIZE) {
        /* Reads from ACK region are not used by the driver; return 0 */
        return 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1 && size != 2 && size != 4) {
        return;
    }

    /* Handle interrupt ACK write region first. In saa7164_irq():
     *   saa7164_writel(dev->int_ack + (i * 4), intstat[i]);
     * This implies write-1-to-clear on the corresponding int_status_words.
     */
    if (addr >= s->int_ack_base && addr < s->int_ack_base + INT_SIZE) {
        unsigned word = (addr - s->int_ack_base) >> 2;
        uint32_t mask;

        switch (size) {
        case 1:
            mask = (uint32_t)(val & 0xff) << ((addr & 3) * 8);
            break;
        case 2:
            mask = (uint32_t)(val & 0xffff) << ((addr & 2) * 8);
            break;
        case 4:
        default:
            mask = (uint32_t)val;
            break;
        }

        s->int_status_words[word] &= ~mask; /* W1C behavior */
        pcibase_update_irq(s);
        return;
    }

    /* Writes into generic BAR0 register space shadow. */
    if (pcibase_addr_in_bar0(addr)) {
        unsigned index = addr >> 2;
        uint32_t old = s->regs[index];
        uint32_t newv = old;

        switch (size) {
        case 1: {
            unsigned shift = (addr & 3) * 8;
            uint32_t mask = 0xffu << shift;
            newv = (old & ~mask) | (((uint32_t)val & 0xffu) << shift);
            break;
        }
        case 2: {
            unsigned shift = (addr & 2) * 8;
            uint32_t mask = 0xffffu << shift;
            newv = (old & ~mask) | (((uint32_t)val & 0xffffu) << shift);
            break;
        }
        case 4:
            newv = (uint32_t)val;
            break;
        }

        s->regs[index] = newv;
        return;
    }

    /* Writes to int_status region are not used by the driver (it only reads
     * from int_status and writes to int_ack), so they are ignored.
     */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    for (i = 0; i < (int)(sizeof(s->regs) / sizeof(s->regs[0])); i++) {
        s->regs[i] = 0;
    }

    for (i = 0; i < INT_SIZE / 4; i++) {
        s->int_status_words[i] = 0;
    }

    /* Initialize firmware-related registers to sane defaults used in probe */
    s->regs[SAA_DEVICE_SYSINIT_STATUS >> 2] = SAA_DEVICE_IMAGE_BOOTING;
    s->regs[SAA_DEVICE_SYSINIT_MODE   >> 2] = 0;
    s->regs[SAA_DEVICE_SYSINIT_SPEC   >> 2] = 0;
    s->regs[SAA_DEVICE_SYSINIT_INST   >> 2] = 0;
    s->regs[SAA_DEVICE_SYSINIT_CPULOAD >> 2] = 0;
    s->regs[SAA_DEVICE_SYSINIT_REMAINHEAP >> 2] = 0x100000;
    s->regs[SAA_DEVICE_VERSION >> 2] = 0x00010001;

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  SAA7164_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SAA7164_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SAA7164_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The driver explicitly uses BAR 0 and BAR 2 via pci_resource_start/len.
     */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000; /* minimal placeholder, actual usage determined by driver */
    s->bar_info[0].name  = "saa7164-mmio0";

    s->bar_info[1].index = 2; /* logical BAR slot used by driver is 2 */
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 0x1000; /* minimal placeholder */
    s->bar_info[1].name  = "saa7164-mmio2";

    for (int i = 0; i < 6; i++) {
        if (i >= s->num_bars) {
            s->bar_info[i].index = i;
            s->bar_info[i].type = BAR_TYPE_NONE;
            s->bar_info[i].size = 0;
            s->bar_info[i].name = "";
        }
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize interrupt register base offsets as seen from BAR0 */
    s->int_status_base = 0x183000 + 0xf80;
    s->int_ack_base    = 0x183000 + 0xf90;

    for (int i = 0; i < INT_SIZE / 4; i++) {
        s->int_status_words[i] = 0;
    }

    /* Initialize firmware status/version registers similar to reset path */
    s->regs[SAA_DEVICE_SYSINIT_STATUS >> 2] = SAA_DEVICE_IMAGE_BOOTING;
    s->regs[SAA_DEVICE_SYSINIT_MODE   >> 2] = 0;
    s->regs[SAA_DEVICE_SYSINIT_SPEC   >> 2] = 0;
    s->regs[SAA_DEVICE_SYSINIT_INST   >> 2] = 0;
    s->regs[SAA_DEVICE_SYSINIT_CPULOAD >> 2] = 0;
    s->regs[SAA_DEVICE_SYSINIT_REMAINHEAP >> 2] = 0x100000;
    s->regs[SAA_DEVICE_VERSION >> 2] = 0x00010001;

    /* No MSI/MSI-X initialization here; driver enables MSI via pci_enable_msi() */
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
    .name = "saa7164_pci",
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
