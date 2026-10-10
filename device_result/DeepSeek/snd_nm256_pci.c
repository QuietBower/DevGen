/*
 * QEMU PCI device model for NeoMagic NM256AV Audio
 * Based on Linux driver sound/pci/nm256/nm256.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bswap.h"
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
#include "hw/pci/pci_ids.h"

#define PCI_VENDOR_ID_NEOMAGIC 0x10c8

#define TYPE_PCIBASE_DEVICE "snd_nm256_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO 0x8005

#define NM_SIGNATURE 0x4e4d0000
#define NM_SIG_MASK 0xffff0000
#define NM_PORT2_SIZE 4096
#define NM_MIXER_OFFSET 0x600
#define NM_MAX_PLAYBACK_COEF_SIZE	0x5000
#define NM_MAX_RECORD_COEF_SIZE		0x1260
#define NM_INT_REG 0xa04
#define NM_PLAYBACK_INT 0x40
#define NM_RECORD_INT 0x100
#define NM_MISC_INT_1 0x4000
#define NM_MISC_INT_2 0x1
#define NM_MIXER_STATUS_OFFSET 0xa04
#define NM_MIXER_READY_MASK 0x0800
#define NM_MIXER_PRESENCE 0xa06
#define NM_PRESENCE_MASK 0x0050
#define NM_PRESENCE_VALUE 0x0040
#define NM2_PLAYBACK_INT 0x10000
#define NM2_RECORD_INT 0x80000
#define NM2_MISC_INT_1 0x8
#define NM2_MISC_INT_2 0x2
#define NM2_MIXER_STATUS_OFFSET 0xa06
#define NM2_MIXER_READY_MASK 0x0800
#define NM_PLAYBACK_REG_OFFSET 0x0
#define NM_RECORD_REG_OFFSET 0x200
#define NM_RATE_REG_OFFSET 2
#define NM_RATE_STEREO 1
#define NM_RATE_BITS_16 2
#define NM_RATE_MASK 0xf0
#define NM_PLAYBACK_ENABLE_REG (NM_PLAYBACK_REG_OFFSET + 0x1)
#define NM_PLAYBACK_ENABLE_FLAG 1
#define NM_PLAYBACK_ONESHOT 2
#define NM_PLAYBACK_FREERUN 4
#define NM_AUDIO_MUTE_REG (NM_PLAYBACK_REG_OFFSET + 0x18)
#define NM_AUDIO_MUTE_LEFT 0x8000
#define NM_AUDIO_MUTE_RIGHT 0x0080
#define NM_RECORD_ENABLE_REG (NM_RECORD_REG_OFFSET + 0)
#define NM_RECORD_ENABLE_FLAG 1
#define NM_RECORD_FREERUN 2
#define NM_COEFF_START_OFFSET	0x1c
#define NM_COEFF_END_OFFSET	0x20
#define NM_RBUFFER_START (NM_RECORD_REG_OFFSET + 0x4)
#define NM_RBUFFER_END   (NM_RECORD_REG_OFFSET + 0x10)
#define NM_RBUFFER_WMARK (NM_RECORD_REG_OFFSET + 0xc)
#define NM_RBUFFER_CURRP (NM_RECORD_REG_OFFSET + 0x8)
#define NM_PBUFFER_START (NM_PLAYBACK_REG_OFFSET + 0x4)
#define NM_PBUFFER_END   (NM_PLAYBACK_REG_OFFSET + 0x14)
#define NM_PBUFFER_WMARK (NM_PLAYBACK_REG_OFFSET + 0xc)
#define NM_PBUFFER_CURRP (NM_PLAYBACK_REG_OFFSET + 0x8)
#define NM_TOTAL_COEFF_COUNT 0x3158

#define NM_BAR0_SIZE (4 * MiB)

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

    /* DMA Context */
    struct {
        uint32_t start;
        uint32_t end;
        uint32_t curr;
        uint32_t wmark;
    } dma[2]; /* [0] playback, [1] capture */

    uint32_t status;
    uint8_t pm_state;

    /* BAR1 control register space */
    uint8_t bar1[NM_PORT2_SIZE];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Driver manages buffer pointers directly; no automated DMA transfer needed. */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= NM_PORT2_SIZE) {
        return ~0ULL;
    }

    switch (addr) {
    case NM_INT_REG:
        val = s->intr_status;
        break;
    default:
        switch (size) {
        case 1:
            val = s->bar1[addr];
            break;
        case 2:
            val = lduw_le_p(&s->bar1[addr]);
            break;
        case 4:
            val = ldl_le_p(&s->bar1[addr]);
            break;
        case 8:
            val = ldq_le_p(&s->bar1[addr]);
            break;
        default:
            val = 0;
            break;
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= NM_PORT2_SIZE) {
        return;
    }

    switch (addr) {
    case NM_INT_REG:
        /* Write-1-to-clear */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    default:
        switch (size) {
        case 1:
            s->bar1[addr] = val;
            break;
        case 2:
            stw_le_p(&s->bar1[addr], val);
            break;
        case 4:
            stl_le_p(&s->bar1[addr], val);
            break;
        case 8:
            stq_le_p(&s->bar1[addr], val);
            break;
        }
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset BAR1 registers to default values */
    memset(s->bar1, 0, sizeof(s->bar1));
    s->intr_status = 0;

    /* Set presence register value for AC97 detection */
    stw_le_p(&s->bar1[NM_MIXER_PRESENCE], NM_PRESENCE_VALUE);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_NEOMAGIC);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_NEOMAGIC_NM256AV_AUDIO);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Enable bus mastering for potential DMA */
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_MASTER);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = NM_BAR0_SIZE, .name = "nm-buffer" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = NM_PORT2_SIZE, .name = "nm-control" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Write signature into BAR0 RAM for probe detection */
    MemoryRegion *bar0 = &s->bar_regions[0];
    void *bar0_ptr = memory_region_get_ram_ptr(bar0);
    if (bar0_ptr) {
        /* buffer_end for NM256AV is 2560*1024 = 0x280000 */
        hwaddr signature_offset = 0x280000 - 0x400; /* 0x27C00 */
        if (signature_offset + 8 <= memory_region_size(bar0)) {
            stl_le_p(bar0_ptr + signature_offset, NM_SIGNATURE);
            stl_le_p(bar0_ptr + signature_offset + 4, 0x280000);
        }
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
    .name = "snd_nm256_pci",
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