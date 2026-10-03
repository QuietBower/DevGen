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

#define TYPE_PCIBASE_DEVICE "nForce2_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NVIDIA        0x10de
#define PCI_DEVICE_ID_NVIDIA_NFORCE2_SMBUS  0x0064
#define PCI_CLASS_ID_SMBUS          0x0c05
#define NFORCE_PCI_SMB1  0x50
#define NFORCE_PCI_SMB2  0x54
#define NVIDIA_SMB_PRTCL_OFFSET    0x00
#define NVIDIA_SMB_STS_OFFSET      0x01
#define NVIDIA_SMB_ADDR_OFFSET     0x02
#define NVIDIA_SMB_CMD_OFFSET      0x03
#define NVIDIA_SMB_DATA_OFFSET     0x04
#define NVIDIA_SMB_BCNT_OFFSET     0x24
#define NVIDIA_SMB_STATUS_ABRT_OFFSET  0x3c
#define NVIDIA_SMB_CTRL_OFFSET     0x3e

/* Bit masks */
#define NVIDIA_SMB_STATUS_ABRT_STS      0x01
#define NVIDIA_SMB_CTRL_ABORT           0x20
#define NVIDIA_SMB_STS_DONE             0x80
#define NVIDIA_SMB_STS_ALRM             0x40
#define NVIDIA_SMB_STS_RES              0x20
#define NVIDIA_SMB_STS_STATUS           0x1f

#define NVIDIA_SMB_PRTCL_WRITE          0x00
#define NVIDIA_SMB_PRTCL_READ           0x01
#define NVIDIA_SMB_PRTCL_QUICK          0x02
#define NVIDIA_SMB_PRTCL_BYTE           0x04
#define NVIDIA_SMB_PRTCL_BYTE_DATA      0x06
#define NVIDIA_SMB_PRTCL_WORD_DATA      0x08
#define NVIDIA_SMB_PRTCL_BLOCK_DATA     0x0a
#define NVIDIA_SMB_PRTCL_PEC            0x80

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
    struct {
        uint8_t prtcl;   /* 0x00 NVIDIA_SMB_PRTCL */
        uint8_t sts;     /* 0x01 NVIDIA_SMB_STS */
        uint8_t addr;    /* 0x02 NVIDIA_SMB_ADDR */
        uint8_t cmd;     /* 0x03 NVIDIA_SMB_CMD */
        uint8_t data;    /* 0x04 NVIDIA_SMB_DATA */
        uint8_t _pad1[0x1F]; /* 0x05..0x23 */
        uint8_t bcnt;    /* 0x24 NVIDIA_SMB_BCNT */
        uint8_t _pad2[0x17]; /* 0x25..0x3B */
        uint8_t status_abrt; /* 0x3C NVIDIA_SMB_STATUS_ABRT */
        uint8_t _pad3[0x01]; /* 0x3D */
        uint8_t ctrl;    /* 0x3E NVIDIA_SMB_CTRL */
    } regs;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO read: return byte from register shadow, padding with 0xFF if out of range */
    if (addr < 64) {
        val = ((uint8_t *)&s->regs)[addr];
    } else {
        val = 0xFF;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* PIO write: store byte to register shadow, ignore out-of-range writes */
    if (addr < 64) {
        ((uint8_t *)&s->regs)[addr] = (uint8_t)val;
    }
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
    /* Reset all register shadows to zero */
    memset(&s->regs, 0, sizeof(s->regs));
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

static void pcibase_instance_init(Object *obj)
{
    PCIBaseState *s = PCIBASE_DEVICE(obj);
    s->num_bars = 2;
    s->bar_info[0].index = 4;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 64;
    s->bar_info[0].name = "nforce2-smbus1";
    s->bar_info[1].index = 5;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 64;
    s->bar_info[1].name = "nforce2-smbus2";
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NVIDIA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NVIDIA_NFORCE2_SMBUS );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID_SMBUS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "nForce2_smbus_pci",
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
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
