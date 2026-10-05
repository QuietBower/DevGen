/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 1: fill register macros, BAR list, BAR sizes, structs, enums.
 * Phase 2: implement MMIO/PIO read/write, DMA init, IRQ init, reset, config hooks
 * Phase 3 (Repair Phase)：correct syntax, types, includes, missing symbols, remove dead code
 * Phase 4 (Debug & Update Phase)：Based on actual kernel logs from QEMU boot, repair the virtual hardware behavior
 *
 * Replace #PLACEHOLDER# blocks with device-specific code/data.
 * NOTE:
 * - Only essential fixes added (TYPE macro, PIO BAR handling, MMIO impl, DMA init, IRQ).
 * - No optional or advanced features added.
 * - Do not directly fill this file with the Linux kernel source code
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

/* ------------------------------------------------------------------ */
/* Additional include files (Stage1 optional)                          */
/* ------------------------------------------------------------------ */


#define TYPE_PCIBASE_DEVICE "intel_qep_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#ifndef GENMASK
#define GENMASK(h, l) \
    (((~0ULL) >> (63 - (h))) & ((~0ULL) << (l)))
#endif

#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif

#define INTEL_QEPCON			0x00
#define INTEL_QEPFLT			0x04
#define INTEL_QEPCOUNT			0x08
#define INTEL_QEPMAX			0x0c
#define INTEL_QEPWDT			0x10
#define INTEL_QEPCAPDIV			0x14
#define INTEL_QEPCNTR			0x18
#define INTEL_QEPCAPBUF			0x1c
#define INTEL_QEPINT_STAT		0x20
#define INTEL_QEPINT_MASK		0x24

#define INTEL_QEPCON_EN			BIT(0)
#define INTEL_QEPCON_FLT_EN		BIT(1)
#define INTEL_QEPCON_EDGE_A		BIT(2)
#define INTEL_QEPCON_EDGE_B		BIT(3)
#define INTEL_QEPCON_EDGE_INDX		BIT(4)
#define INTEL_QEPCON_SWPAB		BIT(5)
#define INTEL_QEPCON_OP_MODE		BIT(6)
#define INTEL_QEPCON_PH_ERR		BIT(7)
#define INTEL_QEPCON_COUNT_RST_MODE	BIT(8)
#define INTEL_QEPCON_INDX_GATING_MASK	GENMASK(10, 9)
#define INTEL_QEPCON_INDX_GATING(n)	(((n) & 3) << 9)
#define INTEL_QEPCON_INDX_PAL_PBL	INTEL_QEPCON_INDX_GATING(0)
#define INTEL_QEPCON_INDX_PAL_PBH	INTEL_QEPCON_INDX_GATING(1)
#define INTEL_QEPCON_INDX_PAH_PBL	INTEL_QEPCON_INDX_GATING(2)
#define INTEL_QEPCON_INDX_PAH_PBH	INTEL_QEPCON_INDX_GATING(3)
#define INTEL_QEPCON_CAP_MODE		BIT(11)
#define INTEL_QEPCON_FIFO_THRE_MASK	GENMASK(14, 12)
#define INTEL_QEPCON_FIFO_THRE(n)	((((n) - 1) & 7) << 12)
#define INTEL_QEPCON_FIFO_EMPTY		BIT(15)

#define INTEL_QEPFLT_MAX_COUNT(n)	((n) & 0x1fffff)

#define INTEL_QEPINT_FIFOCRIT		BIT(5)
#define INTEL_QEPINT_FIFOENTRY		BIT(4)
#define INTEL_QEPINT_QEPDIR		BIT(3)
#define INTEL_QEPINT_QEPRST_UP		BIT(2)
#define INTEL_QEPINT_QEPRST_DOWN	BIT(1)
#define INTEL_QEPINT_WDT		BIT(0)
#define INTEL_QEPINT_MASK_ALL		GENMASK(5, 0)

#define INTEL_QEP_CLK_PERIOD_NS		10

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* DMA info placeholder */
    

    /* Register shadows */
    bool enabled;
    uint32_t qepcon;
    uint32_t qepflt;
    uint32_t qepcount;
    uint32_t qepmax;
    uint32_t qepwdt;
    uint32_t qepcapdiv;
    uint32_t qepcntr;
    uint32_t qepcapbuf;
    uint32_t qepint_stat;
    uint32_t qepint_mask;

    /* Status fields */
    

    /* reset/probe state */
    

    /* power mgmt */
    

    /* other fields */
    
};



/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] Invalid read size %u at addr 0x%"HWADDR_PRIx"\n", TYPE_PCIBASE_DEVICE, size, addr);
        return 0;
    }

    switch (addr) {
    case INTEL_QEPCON:
        val = s->qepcon;
        break;
    case INTEL_QEPFLT:
        val = s->qepflt;
        break;
    case INTEL_QEPCOUNT:
        val = s->qepcount;
        break;
    case INTEL_QEPMAX:
        val = s->qepmax;
        break;
    case INTEL_QEPWDT:
        val = s->qepwdt;
        break;
    case INTEL_QEPCAPDIV:
        val = s->qepcapdiv;
        break;
    case INTEL_QEPCNTR:
        val = s->qepcntr;
        break;
    case INTEL_QEPCAPBUF:
        val = s->qepcapbuf;
        break;
    case INTEL_QEPINT_STAT:
        val = s->qepint_stat;
        break;
    case INTEL_QEPINT_MASK:
        val = s->qepint_mask;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] Unimplemented read addr=0x%"HWADDR_PRIx"\n", TYPE_PCIBASE_DEVICE, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] Invalid write size %u at addr 0x%"HWADDR_PRIx"\n", TYPE_PCIBASE_DEVICE, size, addr);
        return;
    }

    switch (addr) {
    case INTEL_QEPCON:
        s->qepcon = (uint32_t)val;
        s->enabled = (val & INTEL_QEPCON_EN) != 0;
        break;
    case INTEL_QEPFLT:
        s->qepflt = (uint32_t)val;
        break;
    case INTEL_QEPCOUNT:
        s->qepcount = (uint32_t)val;
        break;
    case INTEL_QEPMAX:
        s->qepmax = (uint32_t)val;
        break;
    case INTEL_QEPWDT:
        s->qepwdt = (uint32_t)val;
        break;
    case INTEL_QEPCAPDIV:
        s->qepcapdiv = (uint32_t)val;
        break;
    case INTEL_QEPCNTR:
        s->qepcntr = (uint32_t)val;
        break;
    case INTEL_QEPCAPBUF:
        s->qepcapbuf = (uint32_t)val;
        break;
    case INTEL_QEPINT_STAT:
        s->qepint_stat = (uint32_t)val;
        break;
    case INTEL_QEPINT_MASK:
        s->qepint_mask = (uint32_t)val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] Unimplemented write addr=0x%"HWADDR_PRIx" val=0x%"PRIx64"\n", TYPE_PCIBASE_DEVICE, addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used by this driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used by this driver */
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    s->enabled = false;
    s->qepcon = 0;
    s->qepflt = 0;
    s->qepcount = 0;
    s->qepmax = 0;
    s->qepwdt = 0;
    s->qepcapdiv = 0;
    s->qepcntr = 0;
    s->qepcapbuf = 0;
    s->qepint_stat = 0;
    s->qepint_mask = 0;

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    /* No DMA used by this driver */
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x4bc3 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "intel-qep-mmio" };

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config (placeholders) */
    

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    /* Power management / other init */
    
    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    /* free mmio_backing or other allocations */
    

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    /* optional */
    
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