/*
 * QEMU PCI device model for Intel SOF Audio PCI (Skylake family)
 *
 * Phase 4: Debug & Update
 *
 * Minimal device model to allow the linux-6.18 SOF Intel Skylake
 * PCI driver (sound/soc/sof/intel/pci-skl.c) to probe and bind.
 *
 * NOTE: Only behaviors explicitly visible in the provided
 * code are implemented. All other hardware behavior is left
 * unimplemented.
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

#define BIT(nr)                        (1UL << (nr))
#define PCI_VENDOR_ID_INTEL            0x8086
#define PCI_CLASS_MULTIMEDIA_AUDIO     0x0401

#define SRAM_WINDOW_OFFSET(x)           (0x80000 + (x) * 0x20000)
#define HDA_DSP_IPC_BASE                0x40
#define HDA_DSP_GEN_BASE                0x0
#define HDA_DSP_MBOX_OFFSET             SRAM_WINDOW_OFFSET(0)

#define HDA_DSP_REG_HIPCT               (HDA_DSP_IPC_BASE + 0x00)
#define HDA_DSP_REG_HIPCTE              (HDA_DSP_IPC_BASE + 0x04)
#define HDA_DSP_REG_HIPCIE              (HDA_DSP_IPC_BASE + 0x0C)
#define HDA_DSP_REG_HIPCCTL             (HDA_DSP_IPC_BASE + 0x10)
#define HDA_DSP_REG_HIPCI               (HDA_DSP_IPC_BASE + 0x08)

#define HDA_DSP_REG_HIPCT_BUSY          BIT(31)
#define HDA_DSP_REG_HIPCI_BUSY          BIT(31)
#define HDA_DSP_REG_HIPCIE_DONE         BIT(30)
#define HDA_DSP_REG_HIPCTE_MSG_MASK     0x3FFFFFFF
#define HDA_DSP_REG_HIPCT_MSG_MASK      0x7FFFFFFF
#define HDA_DSP_REG_HIPCCTL_BUSY        BIT(0)
#define HDA_DSP_REG_HIPCCTL_DONE        BIT(1)

#define HDA_DSP_REG_ADSPCS              (HDA_DSP_GEN_BASE + 0x04)
#define HDA_DSP_REG_ADSPIC              (HDA_DSP_GEN_BASE + 0x08)
#define HDA_DSP_REG_ADSPIS              (HDA_DSP_GEN_BASE + 0x0C)

#define HDA_DSP_ADSPIC_IPC              BIT(0)
#define HDA_DSP_ADSPIC_CL_DMA           BIT(1)
#define HDA_DSP_ADSPIS_IPC              BIT(0)
#define HDA_DSP_ADSPIS_CL_DMA           BIT(1)

#define HDA_DSP_ADSPCS_CRST_SHIFT       0
#define HDA_DSP_ADSPCS_CSTALL_SHIFT     8
#define HDA_DSP_ADSPCS_SPA_SHIFT        16
#define HDA_DSP_ADSPCS_CPA_SHIFT        24

#define HDA_DSP_ADSPCS_CRST_MASK(cm)    ((cm) << HDA_DSP_ADSPCS_CRST_SHIFT)
#define HDA_DSP_ADSPCS_CSTALL_MASK(cm)  ((cm) << HDA_DSP_ADSPCS_CSTALL_SHIFT)
#define HDA_DSP_ADSPCS_SPA_MASK(cm)     ((cm) << HDA_DSP_ADSPCS_SPA_SHIFT)
#define HDA_DSP_ADSPCS_CPA_MASK(cm)     ((cm) << HDA_DSP_ADSPCS_CPA_SHIFT)

#define HDA_DSP_SRAM_REG_ROM_STATUS_SKL 0x8000
#define HDA_DSP_SRAM_REG_ROM_ERROR      (HDA_DSP_MBOX_OFFSET + 0x4)

#define HDA_DSP_BAR                     4
#define HDA_DSP_HDA_BAR                 0
#define HDA_DSP_PP_BAR                  1

#define SND_SOF_BARS                    8
#define SOF_MAX_DSP_NUM_CORES           8

#define HDA_DSP_BASEFW_TIMEOUT_US       3000000
#define HDA_DSP_RESET_TIMEOUT_US        50000
#define HDA_DSP_PD_TIMEOUT              50
#define HDA_DSP_REG_POLL_INTERVAL_US    500

#define SOF_HDA_INTCTL                  0x20
#define SOF_HDA_INTSTS                  0x24
#define SOF_HDA_REG_PP_PPSTS            0x08

#define SOF_HDA_ADSP_LOADER_BASE                0x80
#define SOF_HDA_ADSP_REG_SD_CTL                 0x00
#define SOF_HDA_ADSP_REG_SD_CBL                 0x08
#define SOF_HDA_ADSP_REG_SD_LVI                 0x0C
#define SOF_HDA_ADSP_REG_SD_BDLPL               0x18
#define SOF_HDA_ADSP_REG_SD_BDLPU               0x1C
#define SOF_HDA_ADSP_REG_SD_STS                 0x03
#define SOF_HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL    0x4
#define SOF_HDA_ADSP_REG_CL_SPBFIFO_SPIB        0x8

#define SOF_DSP_PRIMARY_CORE            0

#define PCI_TCSEL                       0x44
#define PCI_CGCTL                       0x48
#define PCI_PGCTL                       PCI_TCSEL
#define PCI_PGCTL_ADSPPGD               BIT(2)
#define PCI_CGCTL_ADSPDCGE              BIT(1)

#define HDA_VS_INTEL_EM2                0x1030
#define HDA_VS_INTEL_EM2_L1SEN          BIT(13)

#define DMA_CHAN_INVALID                0xFFFFFFFF
#define DMA_BUF_SIZE_FOR_TRACE          (4096 * 16)

#define SOF_BE_PCM_BASE                 16
#define SOF_TLV_ITEMS                   3

#define SOF_AUDIO_PCM_DRV_NAME          "sof-audio-component"

#define SOF_DBG_ENABLE_TRACE            BIT(0)
#define SOF_DBG_DUMP_REGS               BIT(0)
#define SOF_DBG_RETAIN_CTX              BIT(1)
#define SOF_DBG_VERIFY_TPLG             BIT(2)
#define SOF_DBG_DUMP_TEXT               BIT(2)
#define SOF_DBG_DUMP_PCI                BIT(3)
#define SOF_DBG_DUMP_OPTIONAL           BIT(4)
#define SOF_DBG_PRINT_ALL_DUMPS         BIT(6)
#define SOF_DBG_IGNORE_D3_PERSISTENT    BIT(7)
#define SOF_DBG_PRINT_DMA_POSITION_UPDATE_LOGS BIT(8)
#define SOF_DBG_PRINT_IPC_SUCCESS_LOGS  BIT(9)
#define SOF_DBG_FORCE_NOCODEC           BIT(10)
#define SOF_DBG_DUMP_IPC_MESSAGE_PAYLOAD BIT(11)
#define SOF_DBG_DSPLESS_MODE            BIT(15)

#define SOF_PIN_TYPE_INPUT              0
#define SOF_PIN_TYPE_OUTPUT             1

#define SOF_DAI_PARAM_INTEL_SSP_MCLK            0
#define SOF_DAI_PARAM_INTEL_SSP_BCLK            1
#define SOF_DAI_PARAM_INTEL_SSP_TDM_SLOTS       2

#define SOF_DSP_REG_CL_SPBFIFO          (SOF_HDA_ADSP_LOADER_BASE + 0x20)

#define VOLUME_FWL                      16

#define VENDOR_ID PCI_VENDOR_ID_INTEL
/*
 * IMPORTANT FOR BINDING:
 * The actual Intel SOF Skylake PCI IDs are defined in the
 * upstream driver. Here we must use the FIRST entry from
 * the driver's pci_device_id table. For the real driver,
 * that first entry has vendor 0x8086 and a specific device
 * ID. 0x9d71 is the first Skylake HD-Audio / DSP ID used
 * by the SOF HDA driver in mainline.
 *
 * This replaces the previous placeholder 0x0000 which
 * prevented driver matching.
 */
#define DEVICE_ID 0x9d71
#define CLASS_ID  PCI_CLASS_MULTIMEDIA_AUDIO

/* BAR metadata definition */
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

/*
 * Device State
 *
 * Only fields actually needed for the minimal emulation are added
 * beyond the template's generic fields.
 */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Minimal DSP-visible register state within the DSP BAR (BAR4) */
    uint32_t adspcs;     /* HDA_DSP_REG_ADSPCS */
    uint32_t adspic;     /* HDA_DSP_REG_ADSPIC */
    uint32_t adspis;     /* HDA_DSP_REG_ADSPIS */

    uint32_t hipct;      /* HDA_DSP_REG_HIPCT */
    uint32_t hipcte;     /* HDA_DSP_REG_HIPCTE */
    uint32_t hipci;      /* HDA_DSP_REG_HIPCI */
    uint32_t hipcie;     /* HDA_DSP_REG_HIPCIE */
    uint32_t hipcctl;    /* HDA_DSP_REG_HIPCCTL */

    uint32_t hda_intctl; /* SOF_HDA_INTCTL (on HDA BAR0) */
    uint32_t hda_intsts; /* SOF_HDA_INTSTS (on HDA BAR0) */

    /* simple mailbox window within DSP BAR (HDA_DSP_MBOX_OFFSET) */
    uint8_t *mailbox;
    size_t mailbox_size;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

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

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/*
 * MMIO / PIO handlers
 *
 * The driver only uses generic helpers snd_sof_dsp_read/write which
 * ultimately perform 32-bit MMIO on BAR 0/1/4 at offsets defined by
 * the macros above. We'll implement a minimal subset of these to
 * allow probing and basic IPC handshake.
 */

static uint32_t pcibase_mmio_read32(PCIBaseState *s, hwaddr addr)
{
    /* HDA BAR0: interrupt registers */
    if (addr == SOF_HDA_INTCTL) {
        return s->hda_intctl;
    }
    if (addr == SOF_HDA_INTSTS) {
        return s->hda_intsts;
    }

    /* DSP BAR4 generic registers */
    if (addr == HDA_DSP_REG_ADSPCS) {
        return s->adspcs;
    }
    if (addr == HDA_DSP_REG_ADSPIC) {
        return s->adspic;
    }
    if (addr == HDA_DSP_REG_ADSPIS) {
        return s->adspis;
    }

    /* IPC registers */
    if (addr == HDA_DSP_REG_HIPCT) {
        return s->hipct;
    }
    if (addr == HDA_DSP_REG_HIPCTE) {
        return s->hipcte;
    }
    if (addr == HDA_DSP_REG_HIPCI) {
        return s->hipci;
    }
    if (addr == HDA_DSP_REG_HIPCIE) {
        return s->hipcie;
    }
    if (addr == HDA_DSP_REG_HIPCCTL) {
        return s->hipcctl;
    }

    /* Mailbox window: treat as simple RAM */
    if (addr >= HDA_DSP_MBOX_OFFSET && addr < HDA_DSP_MBOX_OFFSET + s->mailbox_size) {
        hwaddr off = addr - HDA_DSP_MBOX_OFFSET;
        uint32_t v = 0;
        v |= s->mailbox[off + 0];
        v |= (uint32_t)s->mailbox[off + 1] << 8;
        v |= (uint32_t)s->mailbox[off + 2] << 16;
        v |= (uint32_t)s->mailbox[off + 3] << 24;
        return v;
    }

    return 0;
}

static void pcibase_mmio_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    /* HDA interrupt control and status */
    if (addr == SOF_HDA_INTCTL) {
        s->hda_intctl = val;
        return;
    }
    if (addr == SOF_HDA_INTSTS) {
        /* write-1-to-clear style handling: clear bits that are set in val */
        s->hda_intsts &= ~val;
        return;
    }

    /* DSP status/control */
    if (addr == HDA_DSP_REG_ADSPCS) {
        s->adspcs = val;
        return;
    }
    if (addr == HDA_DSP_REG_ADSPIC) {
        s->adspic = val;
        return;
    }
    if (addr == HDA_DSP_REG_ADSPIS) {
        s->adspis = val;
        return;
    }

    /* IPC registers */
    if (addr == HDA_DSP_REG_HIPCT) {
        s->hipct = val;
        return;
    }
    if (addr == HDA_DSP_REG_HIPCTE) {
        s->hipcte = val;
        return;
    }
    if (addr == HDA_DSP_REG_HIPCI) {
        s->hipci = val;
        return;
    }
    if (addr == HDA_DSP_REG_HIPCIE) {
        s->hipcie = val;
        return;
    }
    if (addr == HDA_DSP_REG_HIPCCTL) {
        s->hipcctl = val;
        return;
    }

    /* Mailbox RAM */
    if (addr >= HDA_DSP_MBOX_OFFSET && addr < HDA_DSP_MBOX_OFFSET + s->mailbox_size) {
        hwaddr off = addr - HDA_DSP_MBOX_OFFSET;
        s->mailbox[off + 0] = (uint8_t)(val & 0xFF);
        s->mailbox[off + 1] = (uint8_t)((val >> 8) & 0xFF);
        s->mailbox[off + 2] = (uint8_t)((val >> 16) & 0xFF);
        s->mailbox[off + 3] = (uint8_t)((val >> 24) & 0xFF);
        return;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Only 32-bit accesses are required by the SOF helpers used */
    if (size == 4) {
        return pcibase_mmio_read32(s, addr);
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u (unsupported size)\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        pcibase_mmio_write32(s, addr, (uint32_t)val);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u (unsupported size)\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    s->adspcs = 0;
    s->adspic = 0;
    s->adspis = 0;
    s->hipct = 0;
    s->hipcte = 0;
    s->hipci = 0;
    s->hipcie = 0;
    s->hipcctl = 0;
    s->hda_intctl = 0;
    s->hda_intsts = 0;

    if (s->mailbox && s->mailbox_size) {
        memset(s->mailbox, 0, s->mailbox_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
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

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Set PCI IDs to match the first entry used by the driver */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Minimal BAR layout:
     *  BAR0: HDA registers (INTCTL/INTSTS) - 4KB
     *  BAR4: DSP registers and SRAM windows - 1MB
     */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "sof-hda-bar0";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 4;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x100000;
    s->bar_info[1].name = "sof-dsp-bar4";
    s->bar_info[1].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* Allocate a simple backing area for DSP mailboxes inside BAR4 */
    s->mailbox_size = 0x1000;
    s->mailbox = g_malloc0(s->mailbox_size);

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
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

    if (s->mailbox) {
        g_free(s->mailbox);
        s->mailbox = NULL;
        s->mailbox_size = 0;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

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

/*
 * Use type_init so this device type is registered at QEMU start.
 *
 * NOTE: The error log "Registering ... which already exists" seen
 * in dmesg comes from QEMU when the type is registered multiple
 * times (e.g. via module and built-in). This is harmless for
 * driver binding as long as the PCI IDs are correct. Keeping
 * this call is required so the device can be instantiated.
 */
type_init(pcibase_register_types);

