/*
 * QEMU PCI device model for Intel Skylake SOF Audio DSP
 *
 * Derived strictly from Linux driver: sound/soc/sof/intel/pci-skl.c
 *
 * This model implements only the hardware behavior explicitly required
 * by the driver for successful probe and basic operation.
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

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_skl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x9d70
#define CLASS_ID   PCI_CLASS_MULTIMEDIA_HDA

#define HDA_DSP_REG_HIPCIE		(HDA_DSP_IPC_BASE + 0x0C)
#define HDA_DSP_REG_HIPCIE_DONE	BIT(30)
#define HDA_DSP_SRAM_REG_ROM_STATUS_SKL	0x8000
#define HDA_DSP_REG_HIPCI_BUSY		BIT(31)
#define HDA_DSP_REG_HIPCI		(HDA_DSP_IPC_BASE + 0x08)
#define HDA_DSP_REG_HIPCCTL		(HDA_DSP_IPC_BASE + 0x10)
#define HDA_DSP_BAR			4
#define HDA_DSP_REG_HIPCT		(HDA_DSP_IPC_BASE + 0x00)
#define HDA_DSP_REG_HIPCTE		(HDA_DSP_IPC_BASE + 0x04)
#define SOF_MAX_DSP_NUM_CORES 8
#define SND_SOF_BARS	8
#define HDA_DSP_PP_BAR			1
#define HDA_DSP_HDA_BAR			0
#define SOF_DBG_IGNORE_D3_PERSISTENT		BIT(7)
#define HDA_DSP_REG_HIPCTE_MSG_MASK	0x3FFFFFFF
#define HDA_DSP_REG_HIPCT_MSG_MASK	0x7FFFFFFF
#define HDA_DSP_REG_HIPCCTL_BUSY	BIT(0)
#define HDA_DSP_REG_HIPCCTL_DONE	BIT(1)
#define HDA_DSP_REG_HIPCT_BUSY		BIT(31)
#define SOF_DBG_DUMP_PCI		BIT(3)
#define HDA_DSP_BASEFW_TIMEOUT_US       3000000
#define FSR_STATE_ROM_BASEFW_ENTERED		0xf
#define SOF_DBG_DUMP_MBOX		BIT(1)
#define HDA_DSP_REG_POLL_INTERVAL_US		500
#define HDA_DSP_SRAM_REG_ROM_ERROR		(HDA_DSP_MBOX_OFFSET + 0x4)
#define HDA_DSP_IPC_BASE		0x40
#define HDA_DSP_REG_ADSPIS		(HDA_DSP_GEN_BASE + 0x0C)
#define HDA_DSP_ADSPIS_IPC			BIT(0)
#define HDA_DSP_ADSPIS_CL_DMA		BIT(1)
#define SOF_HDA_INTCTL			0x20
#define SOF_HDA_REG_PP_PPSTS		0x08
#define SOF_HDA_INTSTS			0x24
#define PCI_CGCTL			0x48
#define PCI_PGCTL			PCI_TCSEL
#define PCI_PGCTL_ADSPPGD               BIT(2)
#define PCI_CGCTL_ADSPDCGE              BIT(1)
#define HDA_VS_INTEL_EM2		0x1030
#define HDA_VS_INTEL_EM2_L1SEN		BIT(13)
#define FSR_STATE_INIT_DONE			0x1
#define HDA_DSP_REG_ADSPIC		(HDA_DSP_GEN_BASE + 0x08)
#define HDA_DSP_ADSPIC_IPC			BIT(0)
#define FSR_STATE_MASK				GENMASK(23, 0)
#define SOF_HDA_ADSP_LOADER_BASE		0x80
#define SOF_HDA_ADSP_REG_SD_CTL			0x00
#define SOF_DBG_DUMP_OPTIONAL		BIT(4)
#define SOF_DBG_PRINT_ALL_DUMPS		BIT(6)
#define HDA_DSP_MBOX_OFFSET			SRAM_WINDOW_OFFSET(0)
#define SOF_HDA_D0I3_WORK_DELAY_MS	5000
#define HDA_DSP_GEN_BASE		0x0
#define HDA_DSP_PD_TIMEOUT		50
#define HDA_DSP_ADSPIC_CL_DMA		BIT(1)
#define SOF_HDA_ADSP_REG_SD_STS			0x03
#define SOF_DBG_DUMP_TEXT		BIT(2)
#define SOF_DBG_DUMP_REGS		BIT(0)
#define SOF_DBG_ENABLE_TRACE	BIT(0)
#define HDA_DSP_ADSPCS_CRST_SHIFT	0
#define HDA_DSP_ADSPCS_SPA_SHIFT	16
#define HDA_DSP_ADSPCS_CSTALL_SHIFT	8
#define HDA_DSP_ADSPCS_CPA_SHIFT	24
#define HDA_DSP_RESET_TIMEOUT_US	50000
#define SRAM_WINDOW_OFFSET(x)			(0x80000 + (x) * 0x20000)

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
    uint32_t hipcie;
    uint32_t hipci;
    uint32_t hipcctl;
    uint32_t hipct;
    uint32_t hipcte;
    uint32_t adspis;
    uint32_t adspic;
    uint32_t adspcs;
    
    /* Status fields */
    uint32_t rom_status;
    uint32_t rom_error;
    
    /* reset/probe state */
    bool first_boot;
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

    /* Handle IPC registers */
    switch (addr) {
    case HDA_DSP_REG_HIPCIE:
        return s->hipcie;
    case HDA_DSP_REG_HIPCI:
        return s->hipci;
    case HDA_DSP_REG_HIPCCTL:
        return s->hipcctl;
    case HDA_DSP_REG_HIPCT:
        return s->hipct;
    case HDA_DSP_REG_HIPCTE:
        return s->hipcte;
    case HDA_DSP_REG_ADSPIS:
        return s->adspis;
    case HDA_DSP_REG_ADSPIC:
        return s->adspic;
    case HDA_DSP_SRAM_REG_ROM_STATUS_SKL:
        return s->rom_status;
    case HDA_DSP_SRAM_REG_ROM_ERROR:
        return s->rom_error;
    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle IPC registers */
    switch (addr) {
    case HDA_DSP_REG_HIPCIE:
        s->hipcie = val;
        /* Clear DONE bit when written */
        if (val & HDA_DSP_REG_HIPCIE_DONE) {
            s->hipcie &= ~HDA_DSP_REG_HIPCIE_DONE;
        }
        break;
    case HDA_DSP_REG_HIPCI:
        s->hipci = val;
        /* Set BUSY bit when written */
        s->hipci |= HDA_DSP_REG_HIPCI_BUSY;
        /* Trigger interrupt */
        s->adspis |= HDA_DSP_ADSPIS_IPC;
        if (s->has_msi) {
            msi_notify(&s->parent_obj, 0);
        } else {
            pci_set_irq(&s->parent_obj, 1);
        }
        break;
    case HDA_DSP_REG_HIPCCTL:
        s->hipcctl = val;
        break;
    case HDA_DSP_REG_HIPCT:
        s->hipct = val;
        break;
    case HDA_DSP_REG_HIPCTE:
        s->hipcte = val;
        break;
    case HDA_DSP_REG_ADSPIC:
        /* Clear interrupt status when written */
        s->adspis &= ~val;
        if (!(s->adspis & HDA_DSP_ADSPIS_IPC) && !s->has_msi) {
            pci_set_irq(&s->parent_obj, 0);
        }
        break;
    default:
        break;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
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

    /* Reset register shadows */
    s->hipcie = 0;
    s->hipci = 0;
    s->hipcctl = 0;
    s->hipct = 0;
    s->hipcte = 0;
    s->adspis = 0;
    s->adspic = 0;
    s->adspcs = 0;
    s->rom_status = 0;
    s->rom_error = 0;

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    /* optional: override certain config offsets if needed */

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
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Initialize BAR info based on driver usage */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000; /* HDA base */
    s->bar_info[0].name = "hda-bar";
    
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000; /* PP base */
    s->bar_info[1].name = "pp-bar";
    
    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_MMIO;
    s->bar_info[4].size = 0x200000; /* DSP base */
    s->bar_info[4].name = "dsp-bar";
    
    s->num_bars = 5; /* indices 0, 1, 4 used */

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }

    /* Interrupt config - MSI is used by the driver */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    /* Initialize ROM status to indicate firmware is ready */
    s->rom_status = FSR_STATE_INIT_DONE | (FSR_STATE_ROM_BASEFW_ENTERED << 24);
    s->first_boot = true;

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
