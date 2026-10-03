/*
 * QEMU SAA7164 PCI device model
 * Generated for driver: /home/eely/linux-7.1/drivers/media/pci/saa7164/saa7164-core.c
 * Emulates basic hardware interface required for driver probe and binding.
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

#define TYPE_PCIBASE_DEVICE "saa7164_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers from driver */
#define VENDOR_ID 0x1131
#define DEVICE_ID 0x7164
#define CLASS_ID 0x040000   /* PCI_CLASS_MULTIMEDIA_VIDEO */

/* Register offsets from driver */
#define SAA_DEVICE_VERSION          0x30
#define SAA_DEVICE_DOWNLOAD_OFFSET  0x1000
#define SAA_DEVICE_2ND_DOWNLOAD_OFFSET 0x200000
#define SAA_DEVICE_BUFFERBLOCKSIZE   0x1000
#define SAA_DEVICE_2ND_BUFFERBLOCKSIZE 0x100000
#define SAA_DOWNLOAD_FLAGS          0x34
#define SAA_DEVICE_DEADLOCK_DETECTED_OFFSET 0x6C
#define SAA_DEVICE_SYSINIT_STATUS   0x70
#define SAA_DEVICE_SYSINIT_MODE     0x74
#define SAA_DEVICE_SYSINIT_SPEC     0x78
#define SAA_DEVICE_SYSINIT_INST     0x7C
#define SAA_DEVICE_SYSINIT_CPULOAD  0x80
#define SAA_DEVICE_SYSINIT_REMAINHEAP 0x84
#define SAA_DEVICE_2ND_VERSION      0x50
#define SAA_DEVICE_2ND_DOWNLOADFLAG_OFFSET 0x54
#define SAA_BOOTLOADERERROR_FLAGS   0x44
#define SAA_SECONDSTAGEERROR_FLAGS  0x64
#define SAA_DATAREADY_FLAG_ACK      0x40
#define GET_DESCRIPTORS_CONTROL     0x01
#define GET_FW_STATUS_CONTROL       0x08
#define GET_FW_VERSION_CONTROL      0x09
#define GET_DEBUG_DATA_CONTROL      0x0C
#define SET_DEBUG_LEVEL_CONTROL     0x0B

/* BAR sizes */
#define BAR0_SIZE 0x200000  /* must cover up to int_ack at ~0x183f9c */
#define BAR1_SIZE 0x100000
#define BAR2_SIZE 0x10000
#define BAR3_SIZE 0x100000

#define MSI_VECTORS 1

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
    const MemoryRegionOps *ops;
} BARInfo;

/* Hardware descriptor structs inferred from driver dump functions */
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint16_t bcdSpecVersion;
    uint32_t dwClockFrequency;
    uint32_t dwClockUpdateRes;
    uint8_t  bCapabilities;
    uint32_t dwDeviceRegistersLocation;
    uint32_t dwHostMemoryRegion;
    uint32_t dwHostMemoryRegionSize;
    uint32_t dwHostHibernatMemRegion;
    uint32_t dwHostHibernatMemRegionSize;
} tmComResHWDescr;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bFlags;
    uint8_t  bInterfaceType;
    uint8_t  bInterfaceId;
    uint8_t  bBaseInterface;
    uint8_t  bInterruptId;
    uint8_t  bDebugInterruptId;
    uint32_t BARLocation;
} tmComResInterfaceDescr;

typedef struct __attribute__((packed)) {
    uint64_t CommandRing;
    uint64_t ResponseRing;
    uint32_t CommandWrite;
    uint32_t CommandRead;
    uint32_t ResponseWrite;
    uint32_t ResponseRead;
} tmComResBusDescr;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t int_status[4];  /* 4 u32 interrupt status registers */
    uint32_t int_ack[4];     /* 4 u32 interrupt ack registers */
    bool msi;

    /* Hardware Register Shadows */
    uint32_t fw_status_status;
    uint32_t fw_status_mode;
    uint32_t fw_status_spec;
    uint32_t fw_status_inst;
    uint32_t fw_status_cpuload;
    uint32_t fw_status_remainheap;
    uint32_t firmware_version;

    /* Descriptors stored as byte arrays */
    uint8_t hwdesc[sizeof(tmComResHWDescr)];
    uint8_t intfdesc[sizeof(tmComResInterfaceDescr)];

    /* State */
    uint32_t device_status;
    enum {
        FW_STATE_INIT = 0,
        FW_STATE_BOOTING,
        FW_STATE_LOADING,
        FW_STATE_RUNNING,
    } fw_state;
};

/* Forward declarations */
static uint64_t pcibase_mmio_bar0_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_mmio_bar2_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps pcibase_mmio_bar0_ops = {
    .read = pcibase_mmio_bar0_read,
    .write = pcibase_mmio_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_mmio_bar2_ops = {
    .read = pcibase_mmio_bar2_read,
    .write = pcibase_mmio_bar2_write,
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

static uint64_t pcibase_mmio_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;
    uint64_t val = 0;
    uint32_t offset = addr;

    /* Descriptor region at BAR0 start */
    if (offset < sizeof(tmComResHWDescr)) {
        return s->hwdesc[offset];
    }
    offset -= sizeof(tmComResHWDescr);
    if (offset < sizeof(tmComResInterfaceDescr)) {
        return s->intfdesc[offset];
    }
    offset += sizeof(tmComResHWDescr); /* restore absolute address */

    switch (offset) {
    case SAA_DEVICE_VERSION:
        val = s->firmware_version;
        break;
    case SAA_DEVICE_SYSINIT_STATUS:
        val = s->fw_status_status;
        break;
    case SAA_DEVICE_SYSINIT_MODE:
        val = s->fw_status_mode;
        break;
    case SAA_DEVICE_SYSINIT_SPEC:
        val = s->fw_status_spec;
        break;
    case SAA_DEVICE_SYSINIT_INST:
        val = s->fw_status_inst;
        break;
    case SAA_DEVICE_SYSINIT_CPULOAD:
        val = s->fw_status_cpuload;
        break;
    case SAA_DEVICE_SYSINIT_REMAINHEAP:
        val = s->fw_status_remainheap;
        break;
    default:
        /* INT_STATUS registers at 0x183f80 */
        if (offset >= 0x183f80 && offset < 0x183f90) {
            int reg_idx = (offset - 0x183f80) / 4;
            if (reg_idx < 4) {
                val = s->int_status[reg_idx];
            }
        }
        break;
    }
    return val;
}

static void pcibase_mmio_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;
    uint32_t offset = addr;

    /* INT_ACK registers at 0x183f90: write-1-to-clear */
    if (offset >= 0x183f90 && offset < 0x183fa0) {
        int reg_idx = (offset - 0x183f90) / 4;
        if (reg_idx < 4) {
            s->int_status[reg_idx] &= ~(val & 0xFFFFFFFF);
        }
        return;
    }

    /* Allow writes to some registers; ignore others */
    switch (offset) {
    /* Firmware status writes are usually read-only, but we accept */
    case SAA_DEVICE_SYSINIT_STATUS:
        s->fw_status_status = val;
        break;
    case SAA_DEVICE_SYSINIT_MODE:
        s->fw_status_mode = val;
        break;
    case SAA_DEVICE_SYSINIT_SPEC:
        s->fw_status_spec = val;
        break;
    case SAA_DEVICE_SYSINIT_INST:
        s->fw_status_inst = val;
        break;
    case SAA_DEVICE_SYSINIT_CPULOAD:
        s->fw_status_cpuload = val;
        break;
    case SAA_DEVICE_SYSINIT_REMAINHEAP:
        s->fw_status_remainheap = val;
        break;
    case SAA_DEVICE_VERSION:
        s->firmware_version = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    /* BAR2 is used for bus descriptor; currently it starts at offset 0 */
    /* We pre-populate bus descriptor in a local static array during realize */
    PCIBaseState *s = (PCIBaseState *)opaque;
    /* For now, we assume BAR2 is not backed by RAM, so we return busdesc data */
    /* We'll store busdesc in BAR2 memory region via MemoryRegionOps */
    /* Since we haven't allocated separate region, we'll just return 0 for now. */
    /* Proper implementation: use a RAM-backed BAR2 pre-filled with busdesc. */
    /* But for simplicity, we'll handle it in realize by using memory_region_init_ram
       and populating it; that requires a different realization. */
    /* Let's handle here: if we had RAM, we'd read from it. We'll assume BAR2 is RAM-backed. */
    /* We'll read from underlying RAM if we use it. */
    /* For this minimal read handler, we'll just return 0 as the driver only reads
       busdesc once at probe; we'll populate it via direct memory write in realize. */
    return 0;
}

static void pcibase_mmio_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not expected to be written */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->int_status, 0, sizeof(s->int_status));
    memset(s->int_ack, 0, sizeof(s->int_ack));
    s->fw_state = FW_STATE_RUNNING;  /* Emulate firmware already loaded */
    s->fw_status_status = 1;        /* Running */
    s->fw_status_mode = 0;
    s->fw_status_spec = 0;
    s->fw_status_inst = 0;
    s->fw_status_cpuload = 0;
    s->fw_status_remainheap = 0;
    s->firmware_version = 0x00010000; /* Version 1.0.0.0 */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), bi->ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Set up descriptor data */
    tmComResHWDescr hwdesc = {
        .bLength = sizeof(tmComResHWDescr),
        .bDescriptorType = 0,
        .bDescriptorSubtype = 0,
        .bcdSpecVersion = 0x0100,
        .dwClockFrequency = 100000000,
        .dwClockUpdateRes = 1,
        .bCapabilities = 0,
        .dwDeviceRegistersLocation = 0,
        .dwHostMemoryRegion = 0,
        .dwHostMemoryRegionSize = 0,
        .dwHostHibernatMemRegion = 0,
        .dwHostHibernatMemRegionSize = 0,
    };
    memcpy(s->hwdesc, &hwdesc, sizeof(hwdesc));

    tmComResInterfaceDescr intfdesc = {
        .bLength = sizeof(tmComResInterfaceDescr),
        .bDescriptorType = 0,
        .bDescriptorSubtype = 0,
        .bFlags = 0,
        .bInterfaceType = 0,
        .bInterfaceId = 0,
        .bBaseInterface = 0,
        .bInterruptId = 0,
        .bDebugInterruptId = 0,
        .BARLocation = 0,  /* Bus descriptor at BAR2 offset 0 */
    };
    memcpy(s->intfdesc, &intfdesc, sizeof(intfdesc));

    /* BAR configuration */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE,
                                .name = "bar0-lmmio", .ops = &pcibase_mmio_bar0_ops };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = BAR1_SIZE,
                                .name = "bar1-bmmio", .ops = &pcibase_mmio_bar0_ops }; /* unused, but keep BAR1 */
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM, .size = BAR2_SIZE,
                                .name = "bar2-lmmio2" }; /* RAM-backed for busdescriptor */
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_MMIO, .size = BAR3_SIZE,
                                .name = "bar3-bmmio2", .ops = &pcibase_mmio_bar2_ops };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Pre-populate BAR2 (RAM) with bus descriptor */
    if (!*errp) {
        MemoryRegion *bar2_mr = &s->bar_regions[2];
        if (bar2_mr->ram_block) {
            tmComResBusDescr busdesc = {
                .CommandRing = 0,
                .ResponseRing = 0,
                .CommandWrite = 0,
                .CommandRead = 0,
                .ResponseWrite = 0,
                .ResponseRead = 0,
            };
            /* Write to beginning of BAR2 RAM */
            void *hostmem = memory_region_get_ram_ptr(bar2_mr);
            memcpy(hostmem, &busdesc, sizeof(busdesc));
        }
    }

    if (msi_init(pdev, 0, MSI_VECTORS, true, false, errp)) {
        return;
    }
    s->msi = true;

    s->fw_state = FW_STATE_RUNNING;
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
