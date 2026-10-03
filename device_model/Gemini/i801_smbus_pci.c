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

#define TYPE_PCIBASE_DEVICE "i801_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_82801AA_3	0x2413

#define SMBHSTSTS	0
#define SMBHSTCNT	2
#define SMBHSTCMD	3
#define SMBHSTADD	4
#define SMBHSTDAT0	5
#define SMBHSTDAT1	6
#define SMBBLKDAT	7
#define SMBPEC		8
#define SMBAUXSTS	12
#define SMBAUXCTL	13
#define SMBSLVSTS	16
#define SMBSLVCMD	17
#define SMBNTFDADD	20
#define SMBBAR_MMIO	0
#define SMBBAR		4
#define TCOBASE		0x050
#define TCOCTL		0x054
#define SBREG_SMBCTRL		0xc6000c
#define SBREG_SMBCTRL_DNV	0xcf000c

#define SMBHSTCFG_HST_EN	BIT(0)
#define SMBHSTCFG_SMB_SMI_EN	BIT(1)
#define SMBHSTCFG_I2C_EN	BIT(2)
#define SMBHSTCFG_SPD_WD	BIT(4)
#define TCOCTL_EN		BIT(8)
#define SMBAUXSTS_CRCE		BIT(0)
#define SMBAUXSTS_STCO		BIT(1)
#define SMBAUXCTL_CRC		BIT(0)
#define SMBAUXCTL_E32B		BIT(1)

#define I801_QUICK		0x00
#define I801_BYTE		0x04
#define I801_BYTE_DATA		0x08
#define I801_WORD_DATA		0x0C
#define I801_PROC_CALL		0x10
#define I801_BLOCK_DATA		0x14
#define I801_I2C_BLOCK_DATA	0x18
#define I801_BLOCK_PROC_CALL	0x1C

#define SMBHSTCNT_INTREN	BIT(0)
#define SMBHSTCNT_KILL		BIT(1)
#define SMBHSTCNT_LAST_BYTE	BIT(5)
#define SMBHSTCNT_START		BIT(6)
#define SMBHSTCNT_PEC_EN	BIT(7)

#define SMBHSTSTS_BYTE_DONE	BIT(7)
#define SMBHSTSTS_INUSE_STS	BIT(6)
#define SMBHSTSTS_SMBALERT_STS	BIT(5)
#define SMBHSTSTS_FAILED	BIT(4)
#define SMBHSTSTS_BUS_ERR	BIT(3)
#define SMBHSTSTS_DEV_ERR	BIT(2)
#define SMBHSTSTS_INTR		BIT(1)
#define SMBHSTSTS_HOST_BUSY	BIT(0)

#define SMBSLVSTS_HST_NTFY_STS	BIT(0)
#define SMBSLVCMD_SMBALERT_DISABLE	BIT(2)
#define SMBSLVCMD_HST_NTFY_INTREN	BIT(0)

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
    uint8_t smbhststs;
    uint8_t smbhstcnt;
    uint8_t smbhstcmd;
    uint8_t smbhstadd;
    uint8_t smbhstdat0;
    uint8_t smbhstdat1;
    uint8_t smbblkdat;
    uint8_t smbpec;
    uint8_t smbauxsts;
    uint8_t smbauxctl;
    uint8_t smbslvsts;
    uint8_t smbslvcmd;
    uint8_t smbntfdadd;
    uint8_t smbhstcfg;
    uint8_t tcoctl;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = false;

    if ((s->smbhstcnt & SMBHSTCNT_INTREN) &&
        (s->smbhststs & (SMBHSTSTS_INTR | SMBHSTSTS_DEV_ERR | 
                         SMBHSTSTS_BUS_ERR | SMBHSTSTS_FAILED | 
                         SMBHSTSTS_SMBALERT_STS | SMBHSTSTS_BYTE_DONE))) {
        irq_active = true;
    }

    if ((s->smbslvcmd & SMBSLVCMD_HST_NTFY_INTREN) &&
        (s->smbslvsts & SMBSLVSTS_HST_NTFY_STS)) {
        irq_active = true;
    }

    pci_set_irq(pdev, irq_active ? 1 : 0);
}

static uint64_t pcibase_read_reg(PCIBaseState *s, hwaddr addr, unsigned size)
{
    switch (addr) {
    case SMBHSTSTS: return s->smbhststs;
    case SMBHSTCNT: return s->smbhstcnt;
    case SMBHSTCMD: return s->smbhstcmd;
    case SMBHSTADD: return s->smbhstadd;
    case SMBHSTDAT0: return s->smbhstdat0;
    case SMBHSTDAT1: return s->smbhstdat1;
    case SMBBLKDAT: return s->smbblkdat;
    case SMBPEC: return s->smbpec;
    case SMBAUXSTS: return s->smbauxsts;
    case SMBAUXCTL: return s->smbauxctl;
    case SMBSLVSTS: return s->smbslvsts;
    case SMBSLVCMD: return s->smbslvcmd;
    case SMBNTFDADD: return s->smbntfdadd;
    default: return 0;
    }
}

static void pcibase_write_reg(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    switch (addr) {
    case SMBHSTSTS:
        s->smbhststs &= ~(val & 0xFF); /* W1C */
        pcibase_update_irq(s);
        break;
    case SMBHSTCNT:
        s->smbhstcnt = val & 0xFF;
        if (val & SMBHSTCNT_START) {
            s->smbhststs &= ~SMBHSTSTS_HOST_BUSY;
            s->smbhststs |= SMBHSTSTS_INTR | SMBHSTSTS_BYTE_DONE;
            s->smbhstcnt &= ~SMBHSTCNT_START;
        }
        if (val & SMBHSTCNT_KILL) {
            s->smbhststs &= ~SMBHSTSTS_HOST_BUSY;
            s->smbhststs |= SMBHSTSTS_INTR | SMBHSTSTS_FAILED;
            s->smbhstcnt &= ~SMBHSTCNT_KILL;
        }
        pcibase_update_irq(s);
        break;
    case SMBHSTCMD:
        s->smbhstcmd = val & 0xFF;
        break;
    case SMBHSTADD:
        s->smbhstadd = val & 0xFF;
        break;
    case SMBHSTDAT0:
        s->smbhstdat0 = val & 0xFF;
        break;
    case SMBHSTDAT1:
        s->smbhstdat1 = val & 0xFF;
        break;
    case SMBBLKDAT:
        s->smbblkdat = val & 0xFF;
        break;
    case SMBPEC:
        s->smbpec = val & 0xFF;
        break;
    case SMBAUXSTS:
        s->smbauxsts &= ~(val & SMBAUXSTS_CRCE); /* W1C */
        break;
    case SMBAUXCTL:
        s->smbauxctl = val & 0xFF;
        break;
    case SMBSLVSTS:
        s->smbslvsts &= ~(val & SMBSLVSTS_HST_NTFY_STS); /* W1C */
        pcibase_update_irq(s);
        break;
    case SMBSLVCMD:
        s->smbslvcmd = val & 0xFF;
        pcibase_update_irq(s);
        break;
    case SMBNTFDADD:
        s->smbntfdadd = val & 0xFF;
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_read_reg((PCIBaseState *)opaque, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_write_reg((PCIBaseState *)opaque, addr, val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_read_reg((PCIBaseState *)opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_write_reg((PCIBaseState *)opaque, addr, val, size);
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

    s->smbhststs = 0x00;
    s->smbhstcnt = 0x00;
    s->smbhstcmd = 0x00;
    s->smbhstadd = 0x00;
    s->smbhstdat0 = 0x00;
    s->smbhstdat1 = 0x00;
    s->smbblkdat = 0x00;
    s->smbpec = 0x00;
    s->smbauxsts = 0x00;
    s->smbauxctl = 0x00;
    s->smbslvsts = 0x00;
    s->smbslvcmd = 0x00;
    s->smbntfdadd = 0x00;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_82801AA_3 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_SMBUS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize TCO registers to 0 to disable TCO watchdog in driver */
    pci_set_long(pci_conf + TCOBASE, 0);
    pci_set_long(pci_conf + TCOCTL, 0);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = SMBBAR_MMIO;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "i801-mmio";
    
    s->bar_info[1].index = SMBBAR;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 256;
    s->bar_info[1].name = "i801-pio";
  
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
    .name = "i801_smbus_pci",
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
