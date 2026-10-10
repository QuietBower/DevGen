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

#ifndef PCI_VENDOR_ID_ADAPTEC2
#define PCI_VENDOR_ID_ADAPTEC2 0x9005
#endif

#define TYPE_PCIBASE_DEVICE "aic94xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define REG_BASE_ADDR                 0xB8000000
#define REG_BASE_ADDR_CSEQCIO         0xB8002000
#define REG_BASE_ADDR_EXSI            0xB8042800
#define OCM_BASE_ADDR                 0xA0000000
#define LSEQ0_HOST_REG_BASE_ADR       0xB8020000
#define CSEQ_HOST_REG_BASE_ADR        0xB8001000
#define CSEQ_RAM_REG_BASE_ADR         0xB8004000

#define CHIMINTEN       (REG_BASE_ADDR + 0x1C)
#define CHIMINT         (REG_BASE_ADDR + 0x18)
#define COMBIST         (REG_BASE_ADDR + 0x00)
#define COMSTATEN       (REG_BASE_ADDR + 0x08)
#define DCHSTATUS       (REG_BASE_ADDR + 0x81C)
#define SCBPRO          (REG_BASE_ADDR + 0x0C)
#define DEVCTXBASE      (REG_BASE_ADDR + 0x808)
#define CTXDOMAIN       (REG_BASE_ADDR + 0x810)
#define CMDCTXBASE      (REG_BASE_ADDR + 0x800)
#define OVLYDMACNT      (REG_BASE_ADDR + 0x24)
#define COMSTAT         (REG_BASE_ADDR + 0x04)
#define OVLYDMACTL      (REG_BASE_ADDR + 0x20)
#define OVLYDMAADR      (REG_BASE_ADDR + 0x28)
#define ATOMICSTATCTL   (REG_BASE_ADDR + 0x824)
#define AOLDDATA        (REG_BASE_ADDR + 0x834)
#define ANEWDATA        (REG_BASE_ADDR + 0x830)

#define DLAVAIL			0x00000001
#define COMINT			0x00000008
#define DEVINT			0x00000010
#define INITERR			0x00000020
#define HOSTERR			0x00000040

#define ALTCIOADR	(REG_BASE_ADDR + 0x828)
#define ADDBPTR		(REG_BASE_ADDR + 0x82E)
#define CTXACCESS	(REG_BASE_ADDR + 0x838)
#define ASCBPTR		(REG_BASE_ADDR + 0x82C)
#define MnDDB_SITE	0x180
#define MnSCB_SITE	0x100

#define AIC94XX_MBAR0_SIZE  0x100
#define AIC94XX_OCM_SIZE    0x20000

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
    uint32_t chimint;
    uint32_t chiminten;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t combist;
    uint32_t comstaten;
    uint32_t dchstatus;
    uint32_t scbpro;
    uint32_t devctxbase;
    uint32_t ctxdomain;
    uint32_t cmdctxbase;
    uint32_t ovlydmacnt;
    uint32_t comstat;
    uint32_t ovlydmactl;
    uint32_t ovlydmaadr;
    uint32_t atomicstatctl;
    uint32_t aolddata;
    uint32_t anewdata;
    uint32_t altcioadr;
    uint32_t addbptr;
    uint32_t ctxaccess;
    uint32_t ascbptr;

    /* DMA Context */
    dma_addr_t dma_src;
    dma_addr_t dma_dst;
    uint32_t dma_cnt;
    uint32_t dma_cmd;

    uint32_t status;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CHIMINT - REG_BASE_ADDR:
        val = s->chimint;
        break;
    case CHIMINTEN - REG_BASE_ADDR:
        val = s->chiminten;
        break;
    case COMSTATEN - REG_BASE_ADDR:
        val = s->comstaten;
        break;
    case DCHSTATUS - REG_BASE_ADDR:
        val = s->dchstatus;
        break;
    case ATOMICSTATCTL - REG_BASE_ADDR:
        val = s->atomicstatctl;
        break;
    case AOLDDATA - REG_BASE_ADDR:
        val = s->aolddata;
        break;
    case ANEWDATA - REG_BASE_ADDR:
        val = s->anewdata;
        break;
    case ALTCIOADR - REG_BASE_ADDR:
        val = s->altcioadr;
        break;
    case ADDBPTR - REG_BASE_ADDR:
        val = s->addbptr;
        break;
    case CTXACCESS - REG_BASE_ADDR:
        val = s->ctxaccess;
        break;
    case ASCBPTR - REG_BASE_ADDR:
        val = s->ascbptr;
        break;
    default:
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CHIMINT - REG_BASE_ADDR:
        s->chimint &= ~val; /* W1C */
        if (!s->chimint) {
            pci_set_irq(&s->parent_obj, 0);
        }
        break;
    case CHIMINTEN - REG_BASE_ADDR:
        s->chiminten = val;
        break;
    case COMSTATEN - REG_BASE_ADDR:
        s->comstaten = val;
        break;
    case DCHSTATUS - REG_BASE_ADDR:
        s->dchstatus = val;
        break;
    case ATOMICSTATCTL - REG_BASE_ADDR:
        s->atomicstatctl = val;
        break;
    case AOLDDATA - REG_BASE_ADDR:
        s->aolddata = val;
        break;
    case ANEWDATA - REG_BASE_ADDR:
        s->anewdata = val;
        break;
    case ALTCIOADR - REG_BASE_ADDR:
        s->altcioadr = val;
        break;
    case ADDBPTR - REG_BASE_ADDR:
        s->addbptr = val;
        break;
    case CTXACCESS - REG_BASE_ADDR:
        s->ctxaccess = val;
        break;
    case ASCBPTR - REG_BASE_ADDR:
        s->ascbptr = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    return val;
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
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->chimint = 0;
    s->chiminten = 0;
    s->comstaten = 0;
    s->dchstatus = 0;
    s->atomicstatctl = 0;
    s->aolddata = 0;
    s->anewdata = 0;
    s->altcioadr = 0;
    s->addbptr = 0;
    s->ctxaccess = 0;
    s->ascbptr = 0;
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
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ADAPTEC2 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0410 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0100 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x08);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type  = BAR_TYPE_MMIO,
        .size  = AIC94XX_MBAR0_SIZE,
        .name  = "aic94xx-mbar0",
    };

    s->bar_info[1] = (BARInfo) {
        .index = 2,
        .type  = BAR_TYPE_RAM,
        .size  = AIC94XX_OCM_SIZE,
        .name  = "aic94xx-mbar1-ocm",
    };

    s->num_bars = 2;  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
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
    .name = "aic94xx_pci",
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
