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

#define TYPE_PCIBASE_DEVICE "tw68_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_TECHWELL
#define PCI_VENDOR_ID_TECHWELL 0x1797
#endif
#ifndef PCI_DEVICE_ID_TECHWELL_6800
#define PCI_DEVICE_ID_TECHWELL_6800 0x6800
#endif

#define TW68_DMAC		0x000
#define TW68_DMAP_SA		0x004
#define TW68_VBIC		0x010
#define TW68_INTSTAT		0x01C
#define TW68_INTMASK		0x020
#define TW68_GPIOC		0x024
#define TW68_GPOE		0x028
#define TW68_TESTREG		0x02C
#define TW68_CAP_CTL		0x040
#define TW68_GPDATA		0x100
#define TW68_STATUS1		0x204
#define TW68_INFORM		0x208
#define TW68_OPFORM		0x20C
#define TW68_HSYNC		0x210
#define TW68_ACNTL		0x218
#define TW68_CROP_HI		0x21C
#define TW68_VDELAY_LO		0x220
#define TW68_VACTIVE_LO		0x224
#define TW68_HDELAY_LO		0x228
#define TW68_HACTIVE_LO		0x22C
#define TW68_CNTRL1		0x230
#define TW68_VSCALE_LO		0x234
#define TW68_SCALE_HI		0x238
#define TW68_HSCALE_LO		0x23C
#define TW68_BRIGHT		0x240
#define TW68_CONTRAST		0x244
#define TW68_SHARPNESS		0x248
#define TW68_SAT_U		0x24C
#define TW68_SAT_V		0x250
#define TW68_HUE		0x254
#define TW68_SHARP2		0x258
#define TW68_VSHARP		0x25C
#define TW68_CORING		0x260
#define TW68_CNTRL2		0x268
#define TW68_SDT		0x270
#define TW68_SDTR		0x274
#define TW68_CLMPG		0x280
#define TW68_IAGC		0x284
#define TW68_AGCGAIN		0x288
#define TW68_PEAKWT		0x28C
#define TW68_CLMPL		0x290
#define TW68_SYNCT		0x294
#define TW68_MISSCNT		0x298
#define TW68_PCLAMP		0x29C
#define TW68_VCNTL1		0x2A0
#define TW68_VCNTL2		0x2A4
#define TW68_CKILL		0x2A8
#define TW68_COMB		0x2AC
#define TW68_LDLY		0x2B0
#define TW68_MISC1		0x2B4
#define TW68_LOOP		0x2B8
#define TW68_MISC2		0x2BC
#define TW68_MVSN		0x2C0
#define TW68_CLMD		0x2CC
#define TW68_IDCNTL		0x2D0
#define TW68_CLCNTL1		0x2D4

#define TW68_DMAP_EN		(1 << 0)
#define TW68_FIFO_EN		(1 << 1)
#define TW68_DMAPI		(1 << 1)
#define TW68_FFOF		(1 << 3)
#define TW68_FDMIS		(1 << 4)
#define TW68_DMAPERR		(1 << 5)
#define TW68_PABORT		(1 << 6)
#define TW68_FFERR		(1 << 15)
#define TW68_VLOCK		(1 << 19)
#define TW68_HLOCK		(1 << 22)

#define TW68_VID_INTS	(TW68_FFERR | TW68_PABORT | TW68_DMAPERR | \
			 TW68_FFOF   | TW68_DMAPI)
#define TW68_VID_INTSX	(TW68_FDMIS | TW68_HLOCK | TW68_VLOCK)

#define RISC_INT_BIT		0x08000000
#define RISC_JUMP		0xB0000000
#define RISC_INLINE		0xA0000000
#define RISC_SYNCO		0xC0000000
#define RISC_SYNCE		0xD0000000
#define RISC_LINESTART		0x90000000

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
    uint32_t intstat;
    uint32_t intmask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t dmac;
    uint32_t vbic;
    uint32_t gpioc;
    uint32_t gpoe;
    uint32_t testreg;
    uint32_t cap_ctl;
    uint32_t gpdata;
    uint32_t status1;
    uint32_t inform;
    uint32_t opform;
    uint32_t hsync;
    uint32_t acntl;
    uint32_t crop_hi;
    uint32_t vdelay_lo;
    uint32_t vactive_lo;
    uint32_t hdelay_lo;
    uint32_t hactive_lo;
    uint32_t cntrl1;
    uint32_t vscale_lo;
    uint32_t scale_hi;
    uint32_t hscale_lo;
    uint32_t bright;
    uint32_t contrast;
    uint32_t sharpness;
    uint32_t sat_u;
    uint32_t sat_v;
    uint32_t hue;
    uint32_t sharp2;
    uint32_t vsharp;
    uint32_t coring;
    uint32_t cntrl2;
    uint32_t sdt;
    uint32_t sdtr;
    uint32_t clmpg;
    uint32_t iagc;
    uint32_t agcgain;
    uint32_t peakwt;
    uint32_t clmpl;
    uint32_t synct;
    uint32_t misscnt;
    uint32_t pclamp;
    uint32_t vcntl1;
    uint32_t vcntl2;
    uint32_t ckill;
    uint32_t comb;
    uint32_t ldly;
    uint32_t misc1;
    uint32_t loop;
    uint32_t misc2;
    uint32_t mvsn;
    uint32_t clmd;
    uint32_t idcntl;
    uint32_t clcntl1;

    /* DMA Context */
    uint32_t dmap_sa;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = !!(s->intstat & s->intmask);
    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case TW68_DMAC: val = s->dmac; break;
    case TW68_DMAP_SA: val = s->dmap_sa; break;
    case TW68_VBIC: val = s->vbic; break;
    case TW68_INTSTAT: val = s->intstat; break;
    case TW68_INTMASK: val = s->intmask; break;
    case TW68_GPIOC: val = s->gpioc; break;
    case TW68_GPOE: val = s->gpoe; break;
    case TW68_TESTREG: val = s->testreg; break;
    case TW68_CAP_CTL: val = s->cap_ctl; break;
    case TW68_GPDATA: val = s->gpdata; break;
    case TW68_STATUS1: val = s->status1; break;
    case TW68_INFORM: val = s->inform; break;
    case TW68_OPFORM: val = s->opform; break;
    case TW68_HSYNC: val = s->hsync; break;
    case TW68_ACNTL: val = s->acntl; break;
    case TW68_CROP_HI: val = s->crop_hi; break;
    case TW68_VDELAY_LO: val = s->vdelay_lo; break;
    case TW68_VACTIVE_LO: val = s->vactive_lo; break;
    case TW68_HDELAY_LO: val = s->hdelay_lo; break;
    case TW68_HACTIVE_LO: val = s->hactive_lo; break;
    case TW68_CNTRL1: val = s->cntrl1; break;
    case TW68_VSCALE_LO: val = s->vscale_lo; break;
    case TW68_SCALE_HI: val = s->scale_hi; break;
    case TW68_HSCALE_LO: val = s->hscale_lo; break;
    case TW68_BRIGHT: val = s->bright; break;
    case TW68_CONTRAST: val = s->contrast; break;
    case TW68_SHARPNESS: val = s->sharpness; break;
    case TW68_SAT_U: val = s->sat_u; break;
    case TW68_SAT_V: val = s->sat_v; break;
    case TW68_HUE: val = s->hue; break;
    case TW68_SHARP2: val = s->sharp2; break;
    case TW68_VSHARP: val = s->vsharp; break;
    case TW68_CORING: val = s->coring; break;
    case TW68_CNTRL2: val = s->cntrl2; break;
    case TW68_SDT: val = s->sdt; break;
    case TW68_SDTR: val = s->sdtr; break;
    case TW68_CLMPG: val = s->clmpg; break;
    case TW68_IAGC: val = s->iagc; break;
    case TW68_AGCGAIN: val = s->agcgain; break;
    case TW68_PEAKWT: val = s->peakwt; break;
    case TW68_CLMPL: val = s->clmpl; break;
    case TW68_SYNCT: val = s->synct; break;
    case TW68_MISSCNT: val = s->misscnt; break;
    case TW68_PCLAMP: val = s->pclamp; break;
    case TW68_VCNTL1: val = s->vcntl1; break;
    case TW68_VCNTL2: val = s->vcntl2; break;
    case TW68_CKILL: val = s->ckill; break;
    case TW68_COMB: val = s->comb; break;
    case TW68_LDLY: val = s->ldly; break;
    case TW68_MISC1: val = s->misc1; break;
    case TW68_LOOP: val = s->loop; break;
    case TW68_MISC2: val = s->misc2; break;
    case TW68_MVSN: val = s->mvsn; break;
    case TW68_CLMD: val = s->clmd; break;
    case TW68_IDCNTL: val = s->idcntl; break;
    case TW68_CLCNTL1: val = s->clcntl1; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "tw68: read from undefined register 0x%" HWADDR_PRIx "\n", addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case TW68_DMAC: s->dmac = val; break;
    case TW68_DMAP_SA: s->dmap_sa = val; break;
    case TW68_VBIC: s->vbic = val; break;
    case TW68_INTSTAT: 
        s->intstat &= ~val; /* W1C */
        pcibase_update_irq(s); 
        break;
    case TW68_INTMASK: 
        s->intmask = val; 
        pcibase_update_irq(s); 
        break;
    case TW68_GPIOC: s->gpioc = val; break;
    case TW68_GPOE: s->gpoe = val; break;
    case TW68_TESTREG: s->testreg = val; break;
    case TW68_CAP_CTL: s->cap_ctl = val; break;
    case TW68_GPDATA: s->gpdata = val; break;
    case TW68_STATUS1: s->status1 = val; break;
    case TW68_INFORM: s->inform = val; break;
    case TW68_OPFORM: s->opform = val; break;
    case TW68_HSYNC: s->hsync = val; break;
    case TW68_ACNTL: s->acntl = val; break;
    case TW68_CROP_HI: s->crop_hi = val; break;
    case TW68_VDELAY_LO: s->vdelay_lo = val; break;
    case TW68_VACTIVE_LO: s->vactive_lo = val; break;
    case TW68_HDELAY_LO: s->hdelay_lo = val; break;
    case TW68_HACTIVE_LO: s->hactive_lo = val; break;
    case TW68_CNTRL1: s->cntrl1 = val; break;
    case TW68_VSCALE_LO: s->vscale_lo = val; break;
    case TW68_SCALE_HI: s->scale_hi = val; break;
    case TW68_HSCALE_LO: s->hscale_lo = val; break;
    case TW68_BRIGHT: s->bright = val; break;
    case TW68_CONTRAST: s->contrast = val; break;
    case TW68_SHARPNESS: s->sharpness = val; break;
    case TW68_SAT_U: s->sat_u = val; break;
    case TW68_SAT_V: s->sat_v = val; break;
    case TW68_HUE: s->hue = val; break;
    case TW68_SHARP2: s->sharp2 = val; break;
    case TW68_VSHARP: s->vsharp = val; break;
    case TW68_CORING: s->coring = val; break;
    case TW68_CNTRL2: s->cntrl2 = val; break;
    case TW68_SDT: s->sdt = val; break;
    case TW68_SDTR: s->sdtr = val; break;
    case TW68_CLMPG: s->clmpg = val; break;
    case TW68_IAGC: s->iagc = val; break;
    case TW68_AGCGAIN: s->agcgain = val; break;
    case TW68_PEAKWT: s->peakwt = val; break;
    case TW68_CLMPL: s->clmpl = val; break;
    case TW68_SYNCT: s->synct = val; break;
    case TW68_MISSCNT: s->misscnt = val; break;
    case TW68_PCLAMP: s->pclamp = val; break;
    case TW68_VCNTL1: s->vcntl1 = val; break;
    case TW68_VCNTL2: s->vcntl2 = val; break;
    case TW68_CKILL: s->ckill = val; break;
    case TW68_COMB: s->comb = val; break;
    case TW68_LDLY: s->ldly = val; break;
    case TW68_MISC1: s->misc1 = val; break;
    case TW68_LOOP: s->loop = val; break;
    case TW68_MISC2: s->misc2 = val; break;
    case TW68_MVSN: s->mvsn = val; break;
    case TW68_CLMD: s->clmd = val; break;
    case TW68_IDCNTL: s->idcntl = val; break;
    case TW68_CLCNTL1: s->clcntl1 = val; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "tw68: write to undefined register 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->intstat = 0;
    s->intmask = 0;
    s->dmac = 0;
    s->vbic = 0;
    s->gpioc = 0;
    s->gpoe = 0;
    s->testreg = 0;
    s->cap_ctl = 0;
    s->gpdata = 0;
    s->status1 = 0;
    s->inform = 0;
    s->opform = 0;
    s->hsync = 0;
    s->acntl = 0;
    s->crop_hi = 0;
    s->vdelay_lo = 0;
    s->vactive_lo = 0;
    s->hdelay_lo = 0;
    s->hactive_lo = 0;
    s->cntrl1 = 0;
    s->vscale_lo = 0;
    s->scale_hi = 0;
    s->hscale_lo = 0;
    s->bright = 0;
    s->contrast = 0;
    s->sharpness = 0;
    s->sat_u = 0;
    s->sat_v = 0;
    s->hue = 0;
    s->sharp2 = 0;
    s->vsharp = 0;
    s->coring = 0;
    s->cntrl2 = 0;
    s->sdt = 0;
    s->sdtr = 0;
    s->clmpg = 0;
    s->iagc = 0;
    s->agcgain = 0;
    s->peakwt = 0;
    s->clmpl = 0;
    s->synct = 0;
    s->misscnt = 0;
    s->pclamp = 0;
    s->vcntl1 = 0;
    s->vcntl2 = 0;
    s->ckill = 0;
    s->comb = 0;
    s->ldly = 0;
    s->misc1 = 0;
    s->loop = 0;
    s->misc2 = 0;
    s->mvsn = 0;
    s->clmd = 0;
    s->idcntl = 0;
    s->clcntl1 = 0;
    s->dmap_sa = 0;
    
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TECHWELL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_TECHWELL_6800 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0400 );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* Minimum valid size, exact size pending */
    s->bar_info[0].name = "tw68-mmio";

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
    .name = "tw68_pci",
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
