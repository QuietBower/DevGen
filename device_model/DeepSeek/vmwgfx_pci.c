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
/* #HeadFile# currently empty */

#define TYPE_PCIBASE_DEVICE "vmwgfx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x15AD
#define DEVICE_ID 0x0405
#define CLASS_ID PCI_CLASS_DISPLAY_VGA

/* I/O Ports used to access SVGA registers (MMIO offsets) */
#define SVGA_INDEX_PORT 0x0
#define SVGA_VALUE_PORT 0x1
#define SVGA_IRQSTATUS_PORT 0x8

/* SVGA capability bits (common for VMware SVGA) */
#define SVGA_CAP_RECT_COPY              0x00000002
#define SVGA_CAP_DX                     0x00200000
#define SVGA_CAP_CAP2_REGISTER          0x00000020
#define SVGA_CAP_PITCHLOCK              0x00000400

#define SVGA_CAP2_INTRA_SURFACE_COPY    0x00000001

/* BAR sizes: TODO obtain from hardware documentation */
#define SVGA_MMIO_SIZE (64 * KiB) /* Placeholder: size of MMIO register space */
#define VRAM_SIZE (256 * MiB) /* Placeholder: size of VRAM */

/* SVGA Register offsets (from vmware svga protocol) */
#define SVGA_REG_ID                      0x00
#define SVGA_REG_ENABLE                  0x01
#define SVGA_REG_CONFIG_DONE             0x0D
#define SVGA_REG_TRACES                  0x0F
#define SVGA_REG_CAPABILITIES            0x05
#define SVGA_REG_CAP2                    0x0C
#define SVGA_REG_VRAM_SIZE               0x02
#define SVGA_REG_MEM_SIZE                0x03
#define SVGA_REG_MAX_WIDTH               0x15
#define SVGA_REG_MAX_HEIGHT              0x16
#define SVGA_REG_WIDTH                   0x09
#define SVGA_REG_HEIGHT                  0x0A
#define SVGA_REG_GMR_MAX_IDS             0x2D
#define SVGA_REG_GMRS_MAX_PAGES          0x2E
#define SVGA_REG_MEMORY_SIZE             0x2F
#define SVGA_REG_GBOBJECT_MEM_SIZE_KB    0x33
#define SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB 0x34
#define SVGA_REG_MAX_PRIMARY_MEM         0x31
#define SVGA_REG_MOB_MAX_SIZE            0x30
#define SVGA_REG_SCREENTARGET_MAX_WIDTH  0x1A
#define SVGA_REG_SCREENTARGET_MAX_HEIGHT 0x1B
#define SVGA_REG_DEV_CAP                 0x0E
#define SVGA_REG_FENCE                   0x06
#define SVGA_REG_BUSY                    0x07
#define SVGA_REG_SYNC                    0x08
#define SVGA_REG_IRQ_STATUS              0x04
#define SVGA_REG_IRQ_MASK                0x0B
#define SVGA_REG_GUEST_DRIVER_ID         0x10
#define SVGA_REG_GUEST_DRIVER_VERSION1   0x11
#define SVGA_REG_GUEST_DRIVER_VERSION2   0x12
#define SVGA_REG_GUEST_DRIVER_VERSION3   0x13

/* New definitions from supplementary driver source */
#define SVGA_SYNC_GENERIC                1
#define SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH  19
#define SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT 20
#define SVGA3D_DEVCAP_DXCONTEXT          95
#define SVGA3D_DEVCAP_SM41               244
#define SVGA3D_DEVCAP_SM5                258
#define SVGA3D_DEVCAP_GL43               261

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
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
    /* FIFO register shadows will be added once svga_reg.h offsets are known */

    /* DMA Context - device does not have a dedicated DMA engine; DMA is managed by driver */

    /* Operational status flags from driver's vmw_private */
    uint32_t enable_state;
    uint32_t config_done_state;
    uint32_t traces_state;

    /* State used to handle reset sequences */
    /* #Probe_Reset_Stru# currently empty */

    /* Power management state not required; driver uses system PM notifier */

    /* Additional driver hardware mirror fields */
    uint32_t capabilities;
    uint32_t capabilities2;
    uint32_t max_gmr_ids;
    uint32_t max_gmr_pages;
    uint32_t max_mob_pages;
    uint32_t max_mob_size;
    uint32_t max_primary_mem;
    uint32_t memory_size;
    bool has_gmr;
    bool has_mob;
    bool assume_16bpp;
    uint32_t fb_max_width;
    uint32_t fb_max_height;
    uint32_t texture_max_width;
    uint32_t texture_max_height;
    uint32_t stdu_max_width;
    uint32_t stdu_max_height;
    uint32_t initial_width;
    uint32_t initial_height;

    /* Additional SVGA register shadows not covered by above */
    uint32_t svga_id;
    uint32_t svga_sync;
    uint32_t svga_busy;
    uint32_t svga_irq_status;
    uint32_t svga_irq_mask;
    uint32_t svga_fence;
    uint32_t svga_vram_size;
    uint32_t svga_fifo_mem_size;
    uint32_t svga_dev_cap_index;
    uint32_t svga_guest_driver_id;
    uint32_t svga_guest_driver_version1;
    uint32_t svga_guest_driver_version2;
    uint32_t svga_guest_driver_version3;

    /* Index register for SVGA IO/MMIO access */
    uint32_t svga_reg_index;
};

/* Other_Addition_Info_Defin currently empty */

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->svga_irq_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Update device state after writing SVGA_REG_ID to set protocol version */
static void pcibase_set_svga_version(PCIBaseState *s, uint32_t version)
{
    s->svga_id = version;
    if (version == 2 || version == 0x90000002) {
        /* SVGA_ID_2 */
        s->capabilities = SVGA_CAP_RECT_COPY | SVGA_CAP_CAP2_REGISTER |
                          SVGA_CAP_PITCHLOCK | SVGA_CAP_DX;
        s->capabilities2 = SVGA_CAP2_INTRA_SURFACE_COPY;
        s->svga_vram_size = VRAM_SIZE;
        s->svga_fifo_mem_size = 256 * MiB;
        s->max_gmr_ids = 1024;
        s->max_gmr_pages = 262144;
        s->max_mob_pages = 262144;
        s->max_mob_size = 256 * MiB;
        s->max_primary_mem = VRAM_SIZE;
        s->memory_size = 0;
        s->fb_max_width = 3840;
        s->fb_max_height = 2160;
        s->texture_max_width = 16384;
        s->texture_max_height = 16384;
        s->stdu_max_width = 3840;
        s->stdu_max_height = 2160;
        s->initial_width = 800;
        s->initial_height = 600;
    }
    /* unknown version, leave as-is */
}

/* Dispatch function for indexed register reads */
static uint32_t pcibase_reg_read(PCIBaseState *s, uint32_t index)
{
    uint32_t val = 0;

    switch (index) {
    case SVGA_REG_ID:
        val = s->svga_id;
        break;
    case SVGA_REG_ENABLE:
        val = s->enable_state;
        break;
    case SVGA_REG_CONFIG_DONE:
        val = s->config_done_state;
        break;
    case SVGA_REG_TRACES:
        val = s->traces_state;
        break;
    case SVGA_REG_CAPABILITIES:
        val = s->capabilities;
        break;
    case SVGA_REG_CAP2:
        val = s->capabilities2;
        break;
    case SVGA_REG_VRAM_SIZE:
        val = s->svga_vram_size;
        break;
    case SVGA_REG_MEM_SIZE:
        val = s->svga_fifo_mem_size;
        break;
    case SVGA_REG_MAX_WIDTH:
        val = s->fb_max_width;
        break;
    case SVGA_REG_MAX_HEIGHT:
        val = s->fb_max_height;
        break;
    case SVGA_REG_WIDTH:
        val = s->initial_width;
        break;
    case SVGA_REG_HEIGHT:
        val = s->initial_height;
        break;
    case SVGA_REG_GMR_MAX_IDS:
        val = s->max_gmr_ids;
        break;
    case SVGA_REG_GMRS_MAX_PAGES:
        val = s->max_gmr_pages;
        break;
    case SVGA_REG_MEMORY_SIZE:
        val = s->memory_size;
        break;
    case SVGA_REG_GBOBJECT_MEM_SIZE_KB:
        val = s->max_mob_pages * 4096 / 1024;
        break;
    case SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB:
        val = s->max_mob_pages * 4096 / 1024;
        break;
    case SVGA_REG_MAX_PRIMARY_MEM:
        val = s->max_primary_mem;
        break;
    case SVGA_REG_MOB_MAX_SIZE:
        val = s->max_mob_size;
        break;
    case SVGA_REG_SCREENTARGET_MAX_WIDTH:
        val = s->stdu_max_width;
        break;
    case SVGA_REG_SCREENTARGET_MAX_HEIGHT:
        val = s->stdu_max_height;
        break;
    case SVGA_REG_DEV_CAP:
        switch (s->svga_dev_cap_index) {
        case SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH:
            val = s->texture_max_width;
            break;
        case SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT:
            val = s->texture_max_height;
            break;
        case SVGA3D_DEVCAP_DXCONTEXT:
            val = 1;
            break;
        case SVGA3D_DEVCAP_SM41:
            val = 1;
            break;
        case SVGA3D_DEVCAP_SM5:
            val = 1;
            break;
        case SVGA3D_DEVCAP_GL43:
            val = 1;
            break;
        default:
            val = 0;
            break;
        }
        break;
    case SVGA_REG_FENCE:
        val = s->svga_fence;
        break;
    case SVGA_REG_BUSY:
        val = s->svga_busy;
        break;
    case SVGA_REG_SYNC:
        val = s->svga_sync;
        break;
    case SVGA_REG_IRQ_STATUS:
        val = s->svga_irq_status;
        break;
    case SVGA_REG_IRQ_MASK:
        val = s->svga_irq_mask;
        break;
    case SVGA_REG_GUEST_DRIVER_ID:
        val = s->svga_guest_driver_id;
        break;
    case SVGA_REG_GUEST_DRIVER_VERSION1:
        val = s->svga_guest_driver_version1;
        break;
    case SVGA_REG_GUEST_DRIVER_VERSION2:
        val = s->svga_guest_driver_version2;
        break;
    case SVGA_REG_GUEST_DRIVER_VERSION3:
        val = s->svga_guest_driver_version3;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

/* Dispatch function for indexed register writes */
static void pcibase_reg_write(PCIBaseState *s, uint32_t index, uint32_t val)
{
    switch (index) {
    case SVGA_REG_ID:
        pcibase_set_svga_version(s, val);
        break;
    case SVGA_REG_ENABLE:
        s->enable_state = val;
        break;
    case SVGA_REG_CONFIG_DONE:
        s->config_done_state = val;
        break;
    case SVGA_REG_TRACES:
        s->traces_state = val;
        break;
    case SVGA_REG_CAPABILITIES:
        /* read-only, ignore write */
        break;
    case SVGA_REG_CAP2:
        /* Writes to CAP2 are for sync only; device capabilities2 are fixed */
        break;
    case SVGA_REG_VRAM_SIZE:
        /* read-only, ignore write */
        break;
    case SVGA_REG_MEM_SIZE:
        s->svga_fifo_mem_size = val;
        break;
    case SVGA_REG_MAX_WIDTH:
        s->fb_max_width = val;
        break;
    case SVGA_REG_MAX_HEIGHT:
        s->fb_max_height = val;
        break;
    case SVGA_REG_WIDTH:
        s->initial_width = val;
        break;
    case SVGA_REG_HEIGHT:
        s->initial_height = val;
        break;
    case SVGA_REG_GMR_MAX_IDS:
        s->max_gmr_ids = val;
        break;
    case SVGA_REG_GMRS_MAX_PAGES:
        s->max_gmr_pages = val;
        break;
    case SVGA_REG_MEMORY_SIZE:
        s->memory_size = val;
        break;
    case SVGA_REG_GBOBJECT_MEM_SIZE_KB:
        s->max_mob_pages = (val * 1024) / 4096;
        break;
    case SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB:
        s->max_mob_pages = (val * 1024) / 4096;
        break;
    case SVGA_REG_MAX_PRIMARY_MEM:
        s->max_primary_mem = val;
        break;
    case SVGA_REG_MOB_MAX_SIZE:
        s->max_mob_size = val;
        break;
    case SVGA_REG_SCREENTARGET_MAX_WIDTH:
        s->stdu_max_width = val;
        break;
    case SVGA_REG_SCREENTARGET_MAX_HEIGHT:
        s->stdu_max_height = val;
        break;
    case SVGA_REG_DEV_CAP:
        s->svga_dev_cap_index = val;
        break;
    case SVGA_REG_FENCE:
        s->svga_fence = val;
        break;
    case SVGA_REG_BUSY:
        s->svga_busy = val;
        break;
    case SVGA_REG_SYNC:
        s->svga_sync = val;
        if (val == SVGA_SYNC_GENERIC) {
            s->svga_busy = 0;
            s->svga_fence++;
        }
        break;
    case SVGA_REG_IRQ_STATUS:
        s->svga_irq_status &= ~val;
        pcibase_update_irq(s);
        break;
    case SVGA_REG_IRQ_MASK:
        s->svga_irq_mask = val;
        pcibase_update_irq(s);
        break;
    case SVGA_REG_GUEST_DRIVER_ID:
        s->svga_guest_driver_id = val;
        break;
    case SVGA_REG_GUEST_DRIVER_VERSION1:
        s->svga_guest_driver_version1 = val;
        break;
    case SVGA_REG_GUEST_DRIVER_VERSION2:
        s->svga_guest_driver_version2 = val;
        break;
    case SVGA_REG_GUEST_DRIVER_VERSION3:
        s->svga_guest_driver_version3 = val;
        break;
    default:
        break;
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SVGA_INDEX_PORT:
        val = s->svga_reg_index;
        break;
    case SVGA_VALUE_PORT:
        val = pcibase_reg_read(s, s->svga_reg_index);
        break;
    case SVGA_IRQSTATUS_PORT:
        val = s->svga_irq_status;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SVGA_INDEX_PORT:
        s->svga_reg_index = (uint32_t)val;
        break;
    case SVGA_VALUE_PORT:
        pcibase_reg_write(s, s->svga_reg_index, (uint32_t)val);
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all shadow registers to power-on defaults */
    s->enable_state = 0;
    s->config_done_state = 0;
    s->traces_state = 0;
    s->capabilities = 0;
    s->capabilities2 = 0;
    s->max_gmr_ids = 0;
    s->max_gmr_pages = 0;
    s->max_mob_pages = 0;
    s->max_mob_size = 0;
    s->max_primary_mem = VRAM_SIZE;
    s->memory_size = 0;
    s->has_gmr = false;
    s->has_mob = false;
    s->assume_16bpp = false;
    s->fb_max_width = 0;
    s->fb_max_height = 0;
    s->texture_max_width = 0;
    s->texture_max_height = 0;
    s->stdu_max_width = 0;
    s->stdu_max_height = 0;
    s->initial_width = 800;
    s->initial_height = 600;
    s->svga_id = 0;
    s->svga_sync = 0;
    s->svga_busy = 0;
    s->svga_irq_status = 0;
    s->svga_irq_mask = 0;
    s->svga_fence = 0;
    s->svga_vram_size = VRAM_SIZE;
    s->svga_fifo_mem_size = 16 * KiB;
    s->svga_dev_cap_index = 0;
    s->svga_guest_driver_id = 0;
    s->svga_guest_driver_version1 = 0;
    s->svga_guest_driver_version2 = 0;
    s->svga_guest_driver_version3 = 0;
    s->svga_reg_index = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x15AD );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0405 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DISPLAY_VGA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR Initialization */
    /* SVGA3 uses BAR0 for MMIO and BAR2 for VRAM */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = SVGA_MMIO_SIZE, .name = "svga-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_NONE };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM,  .size = VRAM_SIZE, .name = "svga-vram" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X init: not used */
    /* #MSI_OR_MSIX_INIT# */

    /* DMA config removed */
    /* Timer config removed */

    /* Final state initialization */
    s->enable_state = 0;
    s->config_done_state = 0;
    s->traces_state = 0;
    s->capabilities = 0;
    s->capabilities2 = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->svga_reg_index = 0;
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

    /* #Uninit_Func# currently empty */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "vmwgfx_pci",
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
