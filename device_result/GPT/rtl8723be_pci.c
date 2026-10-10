/*
 * QEMU PCI device model for rtl8723be_pci
 * Phase 2: Behavioral implementation based strictly on provided driver source.
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

/* Removed invalid include hw/misc/pci_ids.h: not present in QEMU 8.2.10 tree */

#define TYPE_PCIBASE_DEVICE "rtl8723be_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define RTL8723BE_VENDOR_ID   0x10ec
#define RTL8723BE_DEVICE_ID   0xB723
#define RTL8723BE_CLASS_ID    0x0280

/* Key register offsets used by the driver */
#define REG_SYS_ISO_CTRL      0x0000
#define REG_SYS_FUNC_EN       0x0002
#define REG_SYS_CLKR          0x0008
#define REG_HSISR             0x005c
#define REG_EFUSE_TEST        0x0034
#define REG_EFUSE_CTRL        0x0030
#define REG_HIMR              0x00B0
#define REG_HIMRE             0x00B8
#define REG_CAMCMD            0x0670
#define REG_CAMWRITE          0x0674
#define REG_CAMREAD           0x0678
#define REG_CAMDBG            0x067C
#define REG_SECCFG            0x0680

/* BAR index used by driver (rtl_hal_cfg.bar_id = 2) */
#define RTL8723BE_BAR_INDEX   2

/* Placeholder BAR size: actual size is determined by guest BAR probing */
#define RTL8723BE_BAR_SIZE    (64 * KiB)

/* Simple masks/placeholders for interrupt bits based on driver mask usage.
 * Exact bit definitions are not derived here; we just provide storage.
 */

/* Maximum MMIO space we care about; must cover highest used register (>= 0x680). */
#define RTL8723BE_MMIO_SIZE   RTL8723BE_BAR_SIZE

/* EFUSE shadow size: driver refers to HWSET_MAX_SIZE / REAL_CONTENT_LEN but
 * does access via REG_EFUSE_CTRL/TEST; here we only provide a minimal backing
 * buffer large enough for typical EFUSE content (512 bytes is common) without
 * relying on undocumented sizes.
 */
#define RTL8723BE_EFUSE_SHADOW_SIZE 512

/* CAM table emulation: provide a small shadow area for CAMREAD/WRITE/DBG/SECCFG. */
#define RTL8723BE_CAM_SHADOW_SIZE   256

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

    /* MMIO register shadow */
    uint8_t regs[RTL8723BE_MMIO_SIZE];

    /* Interrupt state shadow */
    uint32_t himr;   /* REG_HIMR */
    uint32_t himre;  /* REG_HIMRE */
    uint32_t hsisr;  /* REG_HSISR */

    /* EFUSE and CAM shadow areas */
    uint8_t efuse_shadow[RTL8723BE_EFUSE_SHADOW_SIZE];
    uint8_t cam_shadow[RTL8723BE_CAM_SHADOW_SIZE];
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The rtl8723be driver programs REG_HIMR/REG_HIMRE and reads REG_HSISR
     * via rtl8723be_interrupt_recognized(). Here we provide a minimal
     * model: raise legacy INTx whenever there is any pending interrupt bit
     * that is enabled in HIMR/HIMRE and reflected in HSISR.
     */

    uint32_t pending = s->hsisr & (s->himr | s->himre);

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * No explicit descriptor/DMA engine usage is visible in the provided
 * sw.c fragment, so we do not implement any DMA behavior here.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

/* Helpers for register access */
static inline uint8_t pcibase_reg_read8(PCIBaseState *s, hwaddr addr)
{
    if (addr < RTL8723BE_MMIO_SIZE) {
        return s->regs[addr];
    }
    return 0xff;
}

static inline void pcibase_reg_write8(PCIBaseState *s, hwaddr addr, uint8_t val)
{
    if (addr < RTL8723BE_MMIO_SIZE) {
        s->regs[addr] = val;
    }
}

static inline uint16_t pcibase_reg_read16(PCIBaseState *s, hwaddr addr)
{
    uint16_t v = 0xffff;
    if (addr + 1 < RTL8723BE_MMIO_SIZE) {
        v  = s->regs[addr];
        v |= ((uint16_t)s->regs[addr + 1]) << 8;
    }
    return v;
}

static inline void pcibase_reg_write16(PCIBaseState *s, hwaddr addr, uint16_t val)
{
    if (addr + 1 < RTL8723BE_MMIO_SIZE) {
        s->regs[addr]     = (uint8_t)(val & 0xff);
        s->regs[addr + 1] = (uint8_t)((val >> 8) & 0xff);
    }
}

static inline uint32_t pcibase_reg_read32(PCIBaseState *s, hwaddr addr)
{
    uint32_t v = 0xffffffffu;
    if (addr + 3 < RTL8723BE_MMIO_SIZE) {
        v  = (uint32_t)s->regs[addr];
        v |= (uint32_t)s->regs[addr + 1] << 8;
        v |= (uint32_t)s->regs[addr + 2] << 16;
        v |= (uint32_t)s->regs[addr + 3] << 24;
    }
    return v;
}

static inline void pcibase_reg_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr + 3 < RTL8723BE_MMIO_SIZE) {
        s->regs[addr]     = (uint8_t)(val & 0xff);
        s->regs[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        s->regs[addr + 2] = (uint8_t)((val >> 16) & 0xff);
        s->regs[addr + 3] = (uint8_t)((val >> 24) & 0xff);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /*
     * The rtl8723be driver uses 8/16/32-bit IO helpers (write8/16/32, read8/16/32).
     * We implement a simple little-endian register file backing.
     */

    uint64_t val = 0;

    switch (size) {
    case 1:
        val = pcibase_reg_read8(s, addr);
        break;
    case 2:
        val = pcibase_reg_read16(s, addr);
        break;
    case 4:
        /* Special handling for known 32-bit registers */
        if (addr == REG_HIMR) {
            val = s->himr;
        } else if (addr == REG_HIMRE) {
            val = s->himre;
        } else if (addr == REG_HSISR) {
            val = s->hsisr;
        } else if (addr >= REG_CAMREAD && addr < REG_CAMREAD + 4 &&
                   (addr + 3) < REG_CAMREAD + RTL8723BE_CAM_SHADOW_SIZE) {
            /* Map CAMREAD window to cam_shadow[0..] */
            hwaddr off = addr - REG_CAMREAD;
            uint32_t v = 0;
            if (off + 3 < RTL8723BE_CAM_SHADOW_SIZE) {
                v  = (uint32_t)s->cam_shadow[off];
                v |= (uint32_t)s->cam_shadow[off + 1] << 8;
                v |= (uint32_t)s->cam_shadow[off + 2] << 16;
                v |= (uint32_t)s->cam_shadow[off + 3] << 24;
            }
            val = v;
        } else {
            val = pcibase_reg_read32(s, addr);
        }
        break;
    default:
        /* The driver is not expected to use other widths. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 1:
        pcibase_reg_write8(s, addr, (uint8_t)val);
        break;
    case 2:
        pcibase_reg_write16(s, addr, (uint16_t)val);
        break;
    case 4:
        /* Handle important control/interrupt/EFUSE/CAM registers explicitly. */
        if (addr == REG_HIMR) {
            s->himr = (uint32_t)val;
            pcibase_reg_write32(s, addr, (uint32_t)val);
            pcibase_update_irq(s);
        } else if (addr == REG_HIMRE) {
            s->himre = (uint32_t)val;
            pcibase_reg_write32(s, addr, (uint32_t)val);
            pcibase_update_irq(s);
        } else if (addr == REG_HSISR) {
            /* HSISR is usually W1C for pending interrupts; we implement W1C. */
            uint32_t w1c = (uint32_t)val;
            s->hsisr &= ~w1c;
            pcibase_reg_write32(s, addr, s->hsisr);
            pcibase_update_irq(s);
        } else if (addr == REG_EFUSE_CTRL) {
            /* Store EFUSE control; we do not implement actual EFUSE timing. */
            pcibase_reg_write32(s, addr, (uint32_t)val);
        } else if (addr == REG_EFUSE_TEST) {
            pcibase_reg_write32(s, addr, (uint32_t)val);
        } else if (addr >= REG_CAMWRITE && addr < REG_CAMWRITE + 4 &&
                   (addr + 3) < REG_CAMWRITE + RTL8723BE_CAM_SHADOW_SIZE) {
            /* Map CAMWRITE window to cam_shadow[0..]. */
            hwaddr off = addr - REG_CAMWRITE;
            if (off + 3 < RTL8723BE_CAM_SHADOW_SIZE) {
                s->cam_shadow[off]     = (uint8_t)(val & 0xff);
                s->cam_shadow[off + 1] = (uint8_t)((val >> 8) & 0xff);
                s->cam_shadow[off + 2] = (uint8_t)((val >> 16) & 0xff);
                s->cam_shadow[off + 3] = (uint8_t)((val >> 24) & 0xff);
            }
            pcibase_reg_write32(s, addr, (uint32_t)val);
        } else if (addr == REG_CAMCMD || addr == REG_CAMDBG || addr == REG_SECCFG) {
            /* Store CAM control/debug/security config values. */
            pcibase_reg_write32(s, addr, (uint32_t)val);
        } else if (addr == REG_SYS_ISO_CTRL || addr == REG_SYS_FUNC_EN || addr == REG_SYS_CLKR) {
            /* Basic system control/clock registers: just shadow. */
            pcibase_reg_write32(s, addr, (uint32_t)val);
        } else {
            pcibase_reg_write32(s, addr, (uint32_t)val);
        }
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
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

    /* Clear MMIO register shadow and state on reset. */
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->efuse_shadow, 0xff, sizeof(s->efuse_shadow));
    memset(s->cam_shadow, 0, sizeof(s->cam_shadow));

    s->himr = 0;
    s->himre = 0;
    s->hsisr = 0;

    /* Basic default values for key control registers, if any. Here we just
     * keep them zeroed, which is safe because the driver will configure them.
     */

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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  RTL8723BE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RTL8723BE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RTL8723BE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: driver uses bar_id = 2 (MMIO space) */
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    s->bar_info[0].index = RTL8723BE_BAR_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = RTL8723BE_BAR_SIZE;
    s->bar_info[0].name  = "rtl8723be-mmio";

    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* MSI support: rtl8723be_mod_params.msi_support defaults to false.
     * We therefore keep only legacy INTx active here.
     */
    s->has_msi = false;
    s->has_msix = false;

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->efuse_shadow, 0xff, sizeof(s->efuse_shadow));
    memset(s->cam_shadow, 0, sizeof(s->cam_shadow));

    s->himr = 0;
    s->himre = 0;
    s->hsisr = 0;
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
    .name = "rtl8723be_pci",
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
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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
