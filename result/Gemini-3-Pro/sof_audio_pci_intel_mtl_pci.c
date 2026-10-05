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

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_mtl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define PCI_DEVICE_ID_INTEL_HDA_MTL	0x7e28
#define MTL_SSP_COUNT		3
#define MTL_DSP_REG_HFIPCXIDA		0x73214
#define MTL_DSP_REG_HFIPCXIDR		0x73210
#define CNL_SSP_BASE_OFFSET	0x10000
#define MTL_DSP_REG_HFFLGPXQWY		0x163200
#define MTL_DSP_REG_HFIPCXCTL		0x73228
#define MTL_HDA_VS_D0I3C		0x1D4A
#define MTL_DSP_REG_HFIPCXIDR_BUSY	BIT(31)
#define MTL_DSP_REG_HFIPCXIDA_DONE	BIT(31)
#define HDA_DSP_BAR				4
#define MTL_DSP_REG_HFIPCXIDDY		0x73380
#define HDA_DSP_PP_BAR				1
#define HDA_DSP_HDA_BAR				0
#define MTL_HfPWRCTL_WPIOXPG(x)		BIT((x) + 8)
#define MTL_HFDSSCS_CPA_MASK		BIT(24)
#define MTL_HFPWRSTS			0x1D1C
#define MTL_HFPWRCTL_WPDSPHPXPG		BIT(0)
#define MTL_HFDSSCS			0x1000
#define MTL_HFPWRSTS_DSPHPXPGS_MASK	BIT(0)
#define PTL_HFPWRSTS2			0x1D24
#define MTL_HFDSSCS_SPA_MASK		BIT(16)
#define PTL_HFPWRCTL2			0x1D20
#define MTL_HFPWRCTL			0x1D18
#define SOF_HDA_SD_FIFOSIZE_FIFOS_MASK		GENMASK(15, 0)
#define SOF_HDA_ADSP_REG_SD_FIFOSIZE		0x10
#define HDA_CL_STREAM_FORMAT 0x40
#define MTL_DSP_REG_HFIPCXCTL_DONE	BIT(1)
#define MTL_DSP_REG_HFIPCXTDR_BUSY	BIT(31)
#define MTL_DSP_REG_HFIPCXTDR_MSG_MASK GENMASK(30, 0)
#define MTL_DSP_REG_HFIPCXTDDY		0x73300
#define MTL_DSP_REG_HFIPCXTDR		0x73200
#define MTL_DSP_REG_HFIPCXTDA		0x73204
#define MTL_DSP_IRQSTS			0x20
#define MTL_DSP_IRQSTS_IPC		BIT(0)
#define MTL_HFINTIPPTR_PTR_MASK		GENMASK(20, 0)
#define MTL_HFINTIPPTR			0x1108
#define MTL_DSP_REG_HfSNDWIE_IE_MASK	GENMASK(3, 0)
#define MTL_DSP_IRQSTS_SDW		BIT(6)
#define SOF_HDA_INTCTL			0x20
#define SOF_HDA_SD_CTL_DMA_START		0x02
#define SOF_HDA_ADSP_SD_ENTRY_SIZE		0x20
#define SOF_HDA_ADSP_LOADER_BASE		0x80
#define SOF_HDA_ADSP_REG_SD_BDLPU		0x1C
#define SOF_HDA_ADSP_REG_SD_BDLPL		0x18
#define MTL_DSP_REG_HFIPCXTDA_BUSY	BIT(31)
#define MTL_DSP2CXCTL_PRIMARY_CORE_CPA_MASK BIT(8)
#define MTL_DSP2CXCTL_PRIMARY_CORE	0x178D04
#define MTL_DSP2CXCTL_PRIMARY_CORE_SPA_MASK BIT(0)
#define MTL_DSP2CXCTL_PRIMARY_CORE_OSEL_SHIFT 24
#define MTL_DSP2CXCTL_PRIMARY_CORE_OSEL GENMASK(25, 24)
#define MTL_DSP_REG_HFIPCXCTL_BUSY	BIT(0)
#define MTL_DSP_REG_HfHIPCIE_IE_MASK	BIT(0)
#define MTL_IRQ_INTEN_L_HOST_IPC_MASK	BIT(0)
#define MTL_IRQ_INTEN_L_SOUNDWIRE_MASK	BIT(6)
#define SOF_HDA_ADSP_REG_SD_STS			0x03
#define HDA_VS_INTEL_EM2_L1SEN		BIT(13)
#define HDA_VS_INTEL_EM2		0x1030
#define SOF_HDA_ADSP_REG_SD_CBL			0x08
#define SOF_HDA_REG_PP_PPCTL		0x04
#define SOF_HDA_ADSP_REG_SD_LVI			0x0C
#define HDA_VS_INTEL_LTRP_GB_MASK	0x3F
#define HDA_VS_INTEL_LTRP		0x1048
#define SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL	0x4
#define HDA_DSP_SPIB_BAR			2
#define SOF_HDA_ADSP_REG_SD_FORMAT		0x12
#define SOF_HDA_ADSP_DPLBASE			0x70
#define SOF_HDA_ADSP_DPUBASE			0x74
#define SOF_HDA_ADSP_DPLBASE_ENABLE		0x01
#define SOF_HDA_VS_D0I3C_I3		BIT(2)

#ifndef PCI_VENDOR_ID_INTEL
#define PCI_VENDOR_ID_INTEL 0x8086
#endif

/* Helper struct for BAR dispatch */
typedef struct PCIBAR {
    struct PCIBaseState *s;
    int index;
} PCIBAR;

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
    
    /* Register shadows */
    uint32_t hfipcxida;
    uint32_t hfipcxidr;
    uint32_t hfflgpxqwy;
    uint32_t hfipcxctl;
    uint32_t hda_vs_d0i3c;
    uint32_t hfipcxiddy;
    uint32_t hfpwrsts;
    uint32_t hfdsscs;
    uint32_t ptl_hfpwrsts2;
    uint32_t ptl_hfpwrctl2;
    uint32_t hfpwrctl;
    uint32_t hfipcxtddy;
    uint32_t hfipcxtdr;
    uint32_t hfipcxtda;
    uint32_t dsp_irqsts;
    uint32_t hfintipptr;
    uint32_t hda_intctl;
    uint32_t dsp2cxctl_primary_core;
    uint32_t hda_vs_intel_em2;
    uint32_t hda_vs_intel_ltrp;
    uint32_t adsp_dplbase;
    uint32_t adsp_dpubase;

    /* BAR dispatch helpers */
    PCIBAR bars[6];
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
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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
    s->bars[bi->index].s = s;
    s->bars[bi->index].index = bi->index;

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, &s->bars[bi->index], bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, &s->bars[bi->index], bi->name, bi->size);
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
    PCIBAR *bar = opaque;
    PCIBaseState *s = bar->s;
    uint64_t val = 0;

    switch (bar->index) {
    case HDA_DSP_HDA_BAR: /* BAR 0 */
        switch (addr) {
        case SOF_HDA_INTCTL:
            val = s->hda_intctl;
            break;
        case SOF_HDA_ADSP_DPLBASE:
            val = s->adsp_dplbase;
            break;
        case SOF_HDA_ADSP_DPUBASE:
            val = s->adsp_dpubase;
            break;
        default:
            break;
        }
        break;

    case HDA_DSP_BAR: /* BAR 4 */
        switch (addr) {
        case MTL_HFDSSCS:
            val = s->hfdsscs;
            break;
        case MTL_HFPWRCTL:
            val = s->hfpwrctl;
            break;
        case MTL_HFPWRSTS:
            val = s->hfpwrsts;
            break;
        case MTL_DSP_REG_HFIPCXIDR:
            val = s->hfipcxidr;
            break;
        case MTL_DSP_REG_HFIPCXIDA:
            val = s->hfipcxida;
            break;
        case MTL_DSP_REG_HFIPCXCTL:
            val = s->hfipcxctl;
            break;
        case MTL_DSP_REG_HFIPCXTDR:
            val = s->hfipcxtdr;
            break;
        case MTL_DSP_REG_HFIPCXTDA:
            val = s->hfipcxtda;
            break;
        case MTL_DSP2CXCTL_PRIMARY_CORE:
            val = s->dsp2cxctl_primary_core;
            break;
        case MTL_DSP_REG_HFIPCXIDDY:
            val = s->hfipcxiddy;
            break;
        case MTL_DSP_REG_HFIPCXTDDY:
            val = s->hfipcxtddy;
            break;
        case MTL_DSP_REG_HFFLGPXQWY:
            val = s->hfflgpxqwy;
            break;
        case MTL_HDA_VS_D0I3C:
            val = s->hda_vs_d0i3c;
            break;
        case PTL_HFPWRSTS2:
            val = s->ptl_hfpwrsts2;
            break;
        case PTL_HFPWRCTL2:
            val = s->ptl_hfpwrctl2;
            break;
        case MTL_HFINTIPPTR:
            val = s->hfintipptr;
            break;
        case HDA_VS_INTEL_EM2:
            val = s->hda_vs_intel_em2;
            break;
        case HDA_VS_INTEL_LTRP:
            val = s->hda_vs_intel_ltrp;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBAR *bar = opaque;
    PCIBaseState *s = bar->s;

    switch (bar->index) {
    case HDA_DSP_HDA_BAR: /* BAR 0 */
        switch (addr) {
        case SOF_HDA_INTCTL:
            s->hda_intctl = val;
            break;
        case SOF_HDA_ADSP_DPLBASE:
            s->adsp_dplbase = val;
            break;
        case SOF_HDA_ADSP_DPUBASE:
            s->adsp_dpubase = val;
            break;
        default:
            break;
        }
        break;

    case HDA_DSP_BAR: /* BAR 4 */
        switch (addr) {
        case MTL_HFDSSCS:
            s->hfdsscs = val;
            /* Emulate power state transition: SPA -> CPA */
            if (val & MTL_HFDSSCS_SPA_MASK) {
                s->hfdsscs |= MTL_HFDSSCS_CPA_MASK;
            } else {
                s->hfdsscs &= ~MTL_HFDSSCS_CPA_MASK;
            }
            break;
        case MTL_HFPWRCTL:
            s->hfpwrctl = val;
            /* Emulate DSP power state transition */
            if (val & MTL_HFPWRCTL_WPDSPHPXPG) {
                s->hfpwrsts |= MTL_HFPWRSTS_DSPHPXPGS_MASK;
            } else {
                s->hfpwrsts &= ~MTL_HFPWRSTS_DSPHPXPGS_MASK;
            }
            break;
        case MTL_DSP_REG_HFIPCXIDR:
            s->hfipcxidr = val;
            break;
        case MTL_DSP_REG_HFIPCXIDA:
            s->hfipcxida = val;
            break;
        case MTL_DSP_REG_HFIPCXCTL:
            s->hfipcxctl = val;
            break;
        case MTL_DSP_REG_HFIPCXTDR:
            s->hfipcxtdr = val;
            break;
        case MTL_DSP_REG_HFIPCXTDA:
            s->hfipcxtda = val;
            break;
        case MTL_DSP2CXCTL_PRIMARY_CORE:
            s->dsp2cxctl_primary_core = val;
            break;
        case MTL_DSP_REG_HFIPCXIDDY:
            s->hfipcxiddy = val;
            break;
        case MTL_DSP_REG_HFIPCXTDDY:
            s->hfipcxtddy = val;
            break;
        case MTL_DSP_REG_HFFLGPXQWY:
            s->hfflgpxqwy = val;
            break;
        case MTL_HDA_VS_D0I3C:
            s->hda_vs_d0i3c = val;
            break;
        case PTL_HFPWRCTL2:
            s->ptl_hfpwrctl2 = val;
            break;
        case MTL_HFINTIPPTR:
            s->hfintipptr = val;
            break;
        case HDA_VS_INTEL_EM2:
            s->hda_vs_intel_em2 = val;
            break;
        case HDA_VS_INTEL_LTRP:
            s->hda_vs_intel_ltrp = val;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    s->hfdsscs = 0;
    s->hfpwrctl = 0;
    s->hfpwrsts = 0;
    s->hfipcxidr = 0;
    s->hfipcxida = 0;
    s->hfipcxctl = 0;
    s->hfipcxtdr = 0;
    s->hfipcxtda = 0;
    s->dsp2cxctl_primary_core = 0;
    s->hda_intctl = 0;
    s->adsp_dplbase = 0;
    s->adsp_dpubase = 0;

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_HDA_MTL );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0403); /* Multimedia HD Audio */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Setup BARs */
    s->num_bars = 5;
    
    /* BAR 0: HDA Controller (16KB) */
    s->bar_info[0].index = HDA_DSP_HDA_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "sof-mtl-hda";

    /* BAR 1: PP (4KB) */
    s->bar_info[1].index = HDA_DSP_PP_BAR;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "sof-mtl-pp";

    /* BAR 2: SPIB (4KB) */
    s->bar_info[2].index = HDA_DSP_SPIB_BAR;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x1000;
    s->bar_info[2].name = "sof-mtl-spib";

    /* BAR 4: DSP (2MB) */
    s->bar_info[4].index = HDA_DSP_BAR;
    s->bar_info[4].type = BAR_TYPE_MMIO;
    s->bar_info[4].size = 0x200000;
    s->bar_info[4].name = "sof-mtl-dsp";

    /* register BARs */
    for (int i = 0; i < 6; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

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