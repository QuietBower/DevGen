/*
 * QEMU PCI device model for linux-6.18/drivers/gpu/drm/tiny/cirrus-qemu.c
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "cirrus_qemu_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID   PCI_VENDOR_ID_CIRRUS
#define PCIBASE_DEVICE_ID   0x00b8
#define PCIBASE_CLASS_ID    PCI_CLASS_DISPLAY_VGA

#define CIRRUS_VRAM_SIZE    (4 * 1024 * 1024)


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
    
    /* Hardware Register Shadows (The 'Identity' of the device) */
    
    /* We implement a very small subset of legacy VGA-style indexed
     * registers, just enough for the cirrus-qemu tiny DRM driver to
     * perform its MMIO register programming without errors.
     *
     * BAR1 is modeled as a VGA register window with the following
     * byte offsets (matching the driver macros used via SEQ_INDEX,
     * CRT_INDEX, GFX_INDEX, VGA_DAC_MASK, etc.). The driver uses
     * these as 8-bit accesses.
     */

    /* Sequencer (SEQ) */
    uint8_t seq_index;
    uint8_t seq_regs[0x100];

    /* CRT controller (CRTC) */
    uint8_t crt_index;
    uint8_t crt_regs[0x100];

    /* Graphics controller (GFX) */
    uint8_t gfx_index;
    uint8_t gfx_regs[0x100];

    /* DAC mask / header */
    uint8_t dac_mask;

    /* VRAM backing store for BAR0 */
    MemoryRegion vram_region;
    uint8_t *vram_ptr;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The tiny DRM driver does not use interrupts for this device.
     * Leave this empty.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The tiny DRM driver only performs CPU-side shadow blits into
     * the VRAM BAR; there is no device-initiated DMA described in
     * the driver. Keep this stub unused.
     */
    (void)s;
    (void)is_write;
}

/* BAR1 MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff; /* VGA-style default */

    /* The driver always uses 8-bit accesses (ioread8/iowrite8).
     * For safety, support 1-byte reads; other sizes return 0.
     */
    if (size != 1) {
        return 0;
    }

    switch (addr) {
    /* Sequencer indexed registers */
    case 0x3c4: /* SEQ_INDEX */
        val = s->seq_index;
        break;
    case 0x3c5: /* SEQ_DATA */
        val = s->seq_regs[s->seq_index];
        break;

    /* CRT controller indexed registers */
    case 0x3d4: /* CRT_INDEX */
        val = s->crt_index;
        break;
    case 0x3d5: /* CRT_DATA */
        val = s->crt_regs[s->crt_index];
        break;

    /* Graphics controller indexed registers */
    case 0x3ce: /* GFX_INDEX */
        val = s->gfx_index;
        break;
    case 0x3cf: /* GFX_DATA */
        val = s->gfx_regs[s->gfx_index];
        break;

    /* DAC mask / header register */
    case 0x3c6: /* VGA_DAC_MASK */
        val = s->dac_mask;
        break;

    default:
        /* Unimplemented registers return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }

    uint8_t byte_val = (uint8_t)val;

    switch (addr) {
    /* Sequencer indexed registers */
    case 0x3c4: /* SEQ_INDEX */
        s->seq_index = byte_val;
        break;
    case 0x3c5: /* SEQ_DATA */
        s->seq_regs[s->seq_index] = byte_val;
        break;

    /* CRT controller indexed registers */
    case 0x3d4: /* CRT_INDEX */
        s->crt_index = byte_val;
        break;
    case 0x3d5: /* CRT_DATA */
        s->crt_regs[s->crt_index] = byte_val;
        break;

    /* Graphics controller indexed registers */
    case 0x3ce: /* GFX_INDEX */
        s->gfx_index = byte_val;
        break;
    case 0x3cf: /* GFX_DATA */
        s->gfx_regs[s->gfx_index] = byte_val;
        break;

    /* DAC mask / header register */
    case 0x3c6: /* VGA_DAC_MASK */
        s->dac_mask = byte_val;
        break;

    default:
        /* Ignore writes to unimplemented registers */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The tiny DRM driver uses MMIO (devm_ioremap) for both VRAM and
     * register BARs; it does not perform port I/O. Provide a trivial
     * implementation that always returns 0.
     */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used by the driver; ignore. */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Clear indexed register state to simple defaults. */
    s->seq_index = 0;
    s->crt_index = 0;
    s->gfx_index = 0;
    s->dac_mask = 0x00;

    memset(s->seq_regs, 0, sizeof(s->seq_regs));
    memset(s->crt_regs, 0, sizeof(s->crt_regs));
    memset(s->gfx_regs, 0, sizeof(s->gfx_regs));
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
        /* For VRAM, back the BAR with RAM so the guest can memcpy into it. */
        Error *local_err = NULL;
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
        memory_region_set_flush_coalesced(mr);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);

        /* Keep an easy pointer to VRAM for potential future use. */
        if (bi->index == 0) {
            s->vram_region = *mr;
            s->vram_ptr = memory_region_get_ram_ptr(mr);
        }
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     *
     * The driver maps BAR0 as VRAM and BAR1 as MMIO registers.
     */
    s->num_bars = 2;

    /* BAR0: VRAM */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = CIRRUS_VRAM_SIZE;
    s->bar_info[0].name = "cirrus-qemu-vram";

    /* BAR1: MMIO register window; size is not specified in the driver,
     * but it only uses a few VGA-style bytes in the 0x3c0-0x3df range.
     * We use 64KB as in the skeleton, rounded to power-of-two.
     */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 64 * 1024; /* generic MMIO size; driver source does not specify */
    s->bar_info[1].name = "cirrus-qemu-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X initialization is needed: the tiny driver does not
     * request or use interrupts for this virtual device.
     */
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cirrus_qemu_pci",
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
