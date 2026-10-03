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

#define TYPE_PCIBASE_DEVICE "ali15x3_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AL 0x10b9
#define PCI_DEVICE_ID_AL_M7101 0x7101

#define MAX_TIMEOUT		200
#define SMBHSTSTS	0
#define SMBHSTCNT	1
#define SMBHSTSTART	2
#define SMBHSTADD	3
#define SMBHSTDAT0	4
#define SMBHSTDAT1	5
#define SMBBLKDAT	6
#define SMBHSTCMD	7

#define SMBCOM		0x004
#define SMBREV		0x008
#define SMBBA		0x014
#define SMBHSTCFG	0x0E0
#define SMBCLK		0x0E2
#define SMBATPC		0x05B
#define SMBSLVC		0x0E1

#define ALI15X3_SMB_IOSIZE	32
#define ALI15X3_SMB_DEFAULTBASE	0xE800

#define ALI15X3_LOCK		0x06
#define ALI15X3_ABORT		0x02
#define ALI15X3_T_OUT		0x04
#define ALI15X3_QUICK		0x00
#define ALI15X3_BYTE		0x10
#define ALI15X3_BYTE_DATA	0x20
#define ALI15X3_WORD_DATA	0x30
#define ALI15X3_BLOCK_DATA	0x40
#define ALI15X3_BLOCK_CLR	0x80

#define ALI15X3_STS_IDLE	0x04
#define ALI15X3_STS_BUSY	0x08
#define ALI15X3_STS_DONE	0x10
#define ALI15X3_STS_DEV		0x20
#define ALI15X3_STS_COLL	0x40
#define ALI15X3_STS_TERM	0x80
#define ALI15X3_STS_ERR		0xE0

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t smbhststs;
    uint8_t smbhstcnt;
    uint8_t smbhstcmd;
    uint8_t smbhstadd;
    uint8_t smbhstdat0;
    uint8_t smbhstdat1;
    uint8_t blk_data[32];
    uint8_t blk_idx;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SMBHSTSTS:
        /* Driver expects IDLE bit to be set when not busy */
        val = s->smbhststs | ALI15X3_STS_IDLE;
        break;
    case SMBHSTCNT:
        val = s->smbhstcnt;
        break;
    case SMBHSTCMD:
        val = s->smbhstcmd;
        break;
    case SMBHSTADD:
        val = s->smbhstadd;
        break;
    case SMBHSTDAT0:
        val = s->smbhstdat0;
        break;
    case SMBHSTDAT1:
        val = s->smbhstdat1;
        break;
    case SMBBLKDAT:
        val = s->blk_data[s->blk_idx % 32];
        s->blk_idx++;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SMBHSTSTS:
        s->smbhststs &= ~val; /* Clear on write */
        break;
    case SMBHSTCNT:
        s->smbhstcnt = val;
        if (val & ALI15X3_BLOCK_CLR) {
            s->blk_idx = 0;
        }
        if (val == ALI15X3_T_OUT) {
            s->smbhststs &= ~ALI15X3_STS_BUSY;
            s->smbhststs |= ALI15X3_STS_IDLE;
        }
        break;
    case SMBHSTSTART:
        /* Simulate immediate transaction completion */
        s->smbhststs &= ~ALI15X3_STS_BUSY;
        s->smbhststs |= ALI15X3_STS_DONE;
        break;
    case SMBHSTADD:
        s->smbhstadd = val;
        break;
    case SMBHSTDAT0:
        s->smbhstdat0 = val;
        break;
    case SMBHSTDAT1:
        s->smbhstdat1 = val;
        break;
    case SMBBLKDAT:
        s->blk_data[s->blk_idx % 32] = val;
        s->blk_idx++;
        break;
    case SMBHSTCMD:
        s->smbhstcmd = val;
        break;
    default:
        break;
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

    s->smbhststs = 0;
    s->smbhstcnt = 0;
    s->smbhstcmd = 0;
    s->smbhstadd = 0;
    s->smbhstdat0 = 0;
    s->smbhstdat1 = 0;
    s->blk_idx = 0;
    memset(s->blk_data, 0, sizeof(s->blk_data));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_AL_M7101 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_SMBUS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Driver expects ALI15X3_LOCK to be set initially so it can unlock it */
    pci_set_byte(pci_conf + SMBATPC, ALI15X3_LOCK);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = ALI15X3_SMB_IOSIZE;
    s->bar_info[0].name = "ali15x3-smbus-io";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ali15x3_smbus_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(smbhststs, PCIBaseState),
        VMSTATE_UINT8(smbhstcnt, PCIBaseState),
        VMSTATE_UINT8(smbhstcmd, PCIBaseState),
        VMSTATE_UINT8(smbhstadd, PCIBaseState),
        VMSTATE_UINT8(smbhstdat0, PCIBaseState),
        VMSTATE_UINT8(smbhstdat1, PCIBaseState),
        VMSTATE_UINT8_ARRAY(blk_data, PCIBaseState, 32),
        VMSTATE_UINT8(blk_idx, PCIBaseState),
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
