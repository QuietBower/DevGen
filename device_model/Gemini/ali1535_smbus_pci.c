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


#define TYPE_PCIBASE_DEVICE "ali1535_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define MAX_TIMEOUT		500
#define SMBHSTSTS	0
#define SMBHSTTYP	1
#define SMBHSTPORT	2
#define SMBHSTCMD	7
#define SMBHSTADD	3
#define SMBHSTDAT0	4
#define SMBHSTDAT1	5
#define SMBBLKDAT	6
#define SMBCOM		0x004
#define SMBREV		0x008
#define SMBCFG		0x0D1
#define SMBBA		0x0E2
#define SMBHSTCFG	0x0F0
#define SMBCLK		0x0F2
#define ALI1535_SMB_IOSIZE	32
#define ALI1535_SMB_DEFAULTBASE	0x8040
#define ALI1535_LOCK		0x06
#define ALI1535_QUICK		0x00
#define ALI1535_BYTE		0x10
#define ALI1535_BYTE_DATA	0x20
#define ALI1535_WORD_DATA	0x30
#define ALI1535_BLOCK_DATA	0x40
#define ALI1535_I2C_READ	0x60
#define ALI1535_DEV10B_EN	0x80
#define ALI1535_T_OUT		0x08
#define ALI1535_A_HIGH_BIT9	0x08
#define ALI1535_KILL		0x04
#define ALI1535_A_HIGH_BIT8	0x04
#define ALI1535_D_HI_MASK	0x03
#define ALI1535_STS_IDLE	0x04
#define ALI1535_STS_BUSY	0x08
#define ALI1535_STS_DONE	0x10
#define ALI1535_STS_DEV		0x20
#define ALI1535_STS_BUSERR	0x40
#define ALI1535_STS_FAIL	0x80
#define ALI1535_STS_ERR		0xE0
#define ALI1535_BLOCK_CLR	0x04
#define ALI1535_RD_ADDR		0x01
#define ALI1535_SMBIO_EN	0x04

#define PCI_VENDOR_ID_AL		0x10b9
#define PCI_DEVICE_ID_AL_M7101		0x7101

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
    uint8_t smbhsttyp;
    uint8_t smbhstport;
    uint8_t smbhstcmd;
    uint8_t smbhstadd;
    uint8_t smbhstdat0;
    uint8_t smbhstdat1;
    uint8_t smbblkdat;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* MMIO not used by this driver */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* MMIO not used by this driver */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Masking with 0x0F handles both base offset 0x00 and 0x20 accesses */
    switch (addr & 0x0F) {
    case SMBHSTSTS:
        val = s->smbhststs;
        break;
    case SMBHSTTYP:
        val = s->smbhsttyp;
        break;
    case SMBHSTPORT:
        val = s->smbhstport;
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
        val = s->smbblkdat;
        break;
    case SMBHSTCMD:
        val = s->smbhstcmd;
        break;
    default:
        break;
    }
    
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr & 0x0F) {
    case SMBHSTSTS:
        /* Clear on write */
        s->smbhststs &= ~val;
        /* Ensure IDLE bit remains set in our synchronous model */
        s->smbhststs |= ALI1535_STS_IDLE;
        break;
    case SMBHSTTYP:
        s->smbhsttyp = val;
        break;
    case SMBHSTPORT:
        s->smbhstport = val;
        /* Writing to port starts transaction. Complete immediately. */
        s->smbhststs |= ALI1535_STS_DONE | ALI1535_STS_IDLE;
        s->smbhststs &= ~ALI1535_STS_BUSY;
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
        s->smbblkdat = val;
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

    s->smbhststs = ALI1535_STS_IDLE;
    s->smbhsttyp = 0;
    s->smbhstport = 0;
    s->smbhstadd = 0;
    s->smbhstdat0 = 0;
    s->smbhstdat1 = 0;
    s->smbblkdat = 0;
    s->smbhstcmd = 0;
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

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Set default values for PCI config registers expected by the driver */
    pci_set_word(pci_conf + SMBBA, 0x0020); /* ali1535_offset */
    pci_set_byte(pci_conf + SMBCFG, ALI1535_SMBIO_EN);
    pci_set_byte(pci_conf + SMBHSTCFG, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 64, "ali1535-pio"};
    
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
    .name = "ali1535_smbus_pci",
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
