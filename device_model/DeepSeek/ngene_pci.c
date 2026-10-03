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


#define TYPE_PCIBASE_DEVICE "ngene_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x18c3
#define DEVICE_ID 0x0720
#define CLASS_ID  0x048000 /* PCI_CLASS_MULTIMEDIA_OTHER */

/* Register offsets extracted from driver source */
#define MICNG_EE_START      0x0100
#define MICNG_EE_END        0x0FF0
#define MICNG_EETAG_END0    0x0000
#define MICNG_EETAG_END1    0xFFFF
#define MICNG_EETAG_DRXD1_OSCDEVIATION  0x1000
#define MICNG_EETAG_DRXD2_OSCDEVIATION  0x1001
#define MICNG_EETAG_MT2060_1_1STIF      0x1100
#define MICNG_EETAG_MT2060_2_1STIF      0x1101
#define MICNG_EETAG_OEM_FIRST  0xC000
#define MICNG_EETAG_OEM_LAST   0xFFEF
#define SHARED_BUFFER   0xC000
#define HOST_TO_NGENE    (SHARED_BUFFER+0x0000)
#define NGENE_TO_HOST    (SHARED_BUFFER+0x0100)
#define NGENE_COMMAND    (SHARED_BUFFER+0x0200)
#define NGENE_COMMAND_HI (SHARED_BUFFER+0x0204)
#define NGENE_STATUS     (SHARED_BUFFER+0x0208)
#define NGENE_STATUS_HI  (SHARED_BUFFER+0x020C)
#define NGENE_EVENT      (SHARED_BUFFER+0x0210)
#define NGENE_EVENT_HI   (SHARED_BUFFER+0x0214)
#define NGENE_INT_COUNTS       (SHARED_BUFFER+0x0260)
#define NGENE_INT_ENABLE       (SHARED_BUFFER+0x0264)
#define DEV_VER         0x9004
#define FORCE_INT       0xA088
#define TIMESTAMPS      0xA000
#define DATA_FIFO_AREA  (SHARED_BUFFER+0x1000)
#define PROGRAM_SRAM    0x1000

/* Buffer sizes and constants */
#define TS_FILLER  0x6f
#define UART_RBUF_LEN 4096
#define TSOUT_BUF_SIZE (512*188*8)
#define TSIN_BUF_SIZE (512*188*8)
#define EVENT_QUEUE_SIZE    16
#define OVERFLOW_BUFFER_SIZE    (8192)
#define MAX_TS_BUFFER_SIZE       (98304)
#define MAX_HDTV_BUFFER_SIZE   (2080768)
#define MAX_VIDEO_BUFFER_SIZE   (417792)
#define MAX_AUDIO_BUFFER_SIZE     (8192)
#define MAX_VBI_BUFFER_SIZE      (28672)
#define NUM_SCATTER_GATHER_ENTRIES  8
#define RING_SIZE_TS        8
#define RING_SIZE_VIDEO     4
#define RING_SIZE_AUDIO     8

/* Enums */
enum KSSTATE {
    KSSTATE_STOP,
    KSSTATE_ACQUIRE,
    KSSTATE_PAUSE,
    KSSTATE_RUN,
};

enum HWSTATE {
    HWSTATE_STOP,
    HWSTATE_STARTUP,
    HWSTATE_RUN,
    HWSTATE_PAUSE,
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

#define BAR0_SIZE 0x10000

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
    uint8_t mmio[BAR0_SIZE];  /* MMIO region covering all registers, size from BAR0_SIZE */

    /* DMA Context */
    /* Pointers and state for DMA transfers will be added during behavioral phase */

    /* Operational status flags */
    uint32_t card_type; /* To store card type from ngene_info */
};

/* Other definitions: DMA descriptor structures from driver */
struct BUFFER_STREAM_RESULTS {
    uint32_t Clock;           /* Stream time in 100ns units */
    uint16_t RemainingLines;  /* Remaining lines in this field.
                           0 for complete field */
    uint8_t  FieldCount;      /* Video field number */
    uint8_t  Flags;           /* Bit 7 = Done, Bit 6 = seen, Bit 5 = overflow,
                           Bit 0 = FieldID */
    uint16_t BlockCount;      /* Audio block count (unused) */
    uint8_t  Reserved[2];
    uint32_t DTOUpdate;
};

struct BUFFER_HEADER {
    uint64_t    Next;
    struct BUFFER_STREAM_RESULTS SR;

    uint32_t    Number_of_entries_1;
    uint32_t    Reserved5;
    uint64_t    Address_of_first_entry_1;

    uint32_t    Number_of_entries_2;
    uint32_t    Reserved7;
    uint64_t    Address_of_first_entry_2;
};

struct HW_SCATTER_GATHER_ELEMENT {
    uint64_t Address;
    uint32_t Length;
    uint32_t Reserved;
};

struct SBufferHeader {
    struct BUFFER_HEADER   ngeneBuffer; /* Physical descriptor */
    struct SBufferHeader  *Next;
    void                  *Buffer1;
    struct HW_SCATTER_GATHER_ELEMENT *scList1;
    void                  *Buffer2;
    struct HW_SCATTER_GATHER_ELEMENT *scList2;
};

struct SRingBufferDescriptor {
    struct SBufferHeader *Head; /* Points to first buffer in ring buffer
                                   structure*/
    uint64_t   PAHead;         /* Physical address of first buffer */
    uint32_t   MemSize;        /* Memory size of allocated ring buffers
                             (needed for freeing) */
    uint32_t   NumBuffers;     /* Number of buffers in the ring */
    uint32_t   Buffer1Length;  /* Allocated length of Buffer 1 */
    uint32_t   Buffer2Length;  /* Allocated length of Buffer 2 */
    void *SCListMem;      /* Memory to hold scatter gather lists for this
                             ring */
    uint64_t   PASCListMem;    /* Physical address  .. */
    uint32_t   SCListMemSize;  /* Size of this memory */
};

struct FW_HEADER {
    uint8_t Opcode;
    uint8_t Length;
};

struct FW_STREAM_CONTROL {
    struct FW_HEADER hdr;
    uint8_t     Stream;             /* Stream number (UVI1, UVI2, TVOUT) */
    uint8_t     Control;            /* Value written to UVI1_CTL */
    uint8_t     Mode;               /* Controls clock source */
    uint8_t     SetupDataLen;       /* Length of setup data, MSB=1 write
                                   backwards */
    uint16_t    CaptureBlockCount;  /* Blocks (a 256 Bytes) to capture per buffer
                                   for TS and Audio */
    uint64_t    Buffer_Address;     /* Address of first buffer header */
    uint16_t    BytesPerVideoLine;
    uint16_t    MaxLinesPerField;
    uint16_t    MinLinesPerField;
    uint16_t    Reserved_1;
    uint16_t    BytesPerVBILine;
    uint16_t    MaxVBILinesPerField;
    uint16_t    MinVBILinesPerField;
    uint16_t    SetupDataAddr;      /* ngene relative address of setup data */
    uint8_t     SetupData[32];      /* setup data */
};

struct EVENT_BUFFER {
    uint32_t    TimeStamp;
    uint8_t     GPIOStatus;
    uint8_t     UARTStatus;
    uint8_t     RXCharacter;
    uint8_t     EventStatus;
    uint32_t    Reserved[2];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size <= BAR0_SIZE) {
        switch (size) {
        case 1: val = s->mmio[addr]; break;
        case 2: val = lduw_le_p(&s->mmio[addr]); break;
        case 4: val = ldl_le_p(&s->mmio[addr]); break;
        case 8: val = ldq_le_p(&s->mmio[addr]); break;
        default: break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size <= BAR0_SIZE) {
        switch (size) {
        case 1: s->mmio[addr] = (uint8_t)val; break;
        case 2: stw_le_p(&s->mmio[addr], (uint16_t)val); break;
        case 4: stl_le_p(&s->mmio[addr], (uint32_t)val); break;
        case 8: stq_le_p(&s->mmio[addr], val); break;
        default: break;
        }
    }
}

/* Dummy PIO ops (no PIO BAR expected, but required for compilation) */
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
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to power-on defaults */
    memset(s->mmio, 0, sizeof(s->mmio));
    s->intr_status = 0;
    s->intr_mask = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_config_set_class(pci_conf, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "bar0" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X initialization */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    /* DMA configuration – not required, no driver-visible behavior */

    /* Final state initialization – set default register values via reset */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No extra resources to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ngene_pci",
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
