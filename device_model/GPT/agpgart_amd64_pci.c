/*
 * QEMU PCI device model for agpgart_amd64_pci
 * Phase 2: Functional behavior for Linux amd64-agp driver.
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "agpgart_amd64_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define NVIDIA_X86_64_0_APBASE        0x10
#define NVIDIA_X86_64_1_APBASE1       0x50
#define NVIDIA_X86_64_1_APLIMIT1      0x54
#define NVIDIA_X86_64_1_APSIZE        0xa8
#define NVIDIA_X86_64_1_APBASE2       0xd8
#define NVIDIA_X86_64_1_APLIMIT2      0xdc
#define ULI_X86_64_BASE_ADDR          0x10
#define ULI_X86_64_HTT_FEA_REG        0x50
#define ULI_X86_64_ENU_SCR_REG        0x54
#define AGP_APERTURE_BAR              0
#define AGPCTRL                        0x10
#define AGPSTAT                        0x4
#define AGPCMD                         0x8
#define AGPNICMD                       0x20
#define AGPNISTAT                      0xc
#define AGPSTAT_MODE_3_0               (1<<3)
#define AGPSTAT_AGP_ENABLE             (1<<8)
#define AGPSTAT_RQ_DEPTH               (0xff000000)
#define AGPSTAT_FW                     (1<<4)
#define AGPSTAT_SBA                    (1<<9)
#define AGPSTAT3_RSVD                  (1<<2)
#define AGPSTAT2_1X                    (1<<0)
#define AGPSTAT2_2X                    (1<<1)
#define AGPSTAT2_4X                    (1<<2)
#define AGPSTAT3_4X                    (1)
#define AGPSTAT3_8X                    (1<<1)
#define AGPSTAT_CAL_MASK               (1<<12|1<<11|1<<10)
#define AGPSTAT_ARQSZ                  (1<<15|1<<14|1<<13)
#define AGP3_RESERVED_MASK             0x00ff00c4
#define AGP2_RESERVED_MASK             0x00fffcc8
#define AGP_ERRATA_FASTWRITES          (1<<0)
#define AGP_ERRATA_SBA                 (1<<1)
#define AGP_ERRATA_1X                  (1<<2)
#define AGPGART_VERSION_MAJOR          0
#define AGPGART_VERSION_MINOR          103
#define AGP_MAJOR_VERSION_SHIFT        (20)
#define AGP_MINOR_VERSION_SHIFT        (16)
#define MAXKEY                         (4096 * 32)

/* New AMD GART-specific config offsets from supplementary source */
#define AMD64_GARTAPERTURECTL  0x90
#define AMD64_GARTAPERTUREBASE 0x94
#define GARTEN                  (1<<0)

/* Use first pci_device_id entry from agp_amd64_pci_table */
#define PCIBASE_VENDOR_ID  PCI_VENDOR_ID_AMD
#define PCIBASE_DEVICE_ID  0x7454
#define PCIBASE_CLASS_ID   PCI_CLASS_BRIDGE_HOST

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

    /* Shadow AGP/bridge related config registers that driver may touch
     * via pci_read_config_dword()/pci_write_config_dword().
     * We mirror only offsets that appear in the driver.
     */
    uint32_t gart_aperturectl;   /* AMD64_GARTAPERTURECTL (0x90) */
    uint32_t gart_aperturebase;  /* AMD64_GARTAPERTUREBASE (0x94) */

    uint32_t nvidia_apbase0;     /* NVIDIA_X86_64_0_APBASE (0x10) */

    /* ULi bridge shadows */
    uint32_t uli_base_addr;      /* ULI_X86_64_BASE_ADDR (0x10) */
    uint32_t uli_htt_fea;        /* ULI_X86_64_HTT_FEA_REG (0x50) */
    uint32_t uli_enu_scr;        /* ULI_X86_64_ENU_SCR_REG (0x54) */

    /* NVIDIA secondary device shadowed registers. These are not on this
     * PCI function, so we do not expose them via config space; they are
     * kept for completeness, but not used by the driver on this device.
     */
    uint32_t nvidia1_apbase1;
    uint32_t nvidia1_aplimit1;
    uint32_t nvidia1_apsize;
    uint32_t nvidia1_apbase2;
    uint32_t nvidia1_aplimit2;
};

/* No interrupt logic is used by the driver for this bridge. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    (void)s;
}

/* The driver does not program DMA engines on this bridge directly. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/*
 * MMIO aperture BAR0: the driver never ioremaps or accesses BAR0 directly.
 * It only treats it as an address range for GART, so we can expose a
 * simple RAM region with no special semantics.
 */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    switch (size) {
    case 1:
    case 2:
    case 4:
    case 8:
        /* Return zero-filled aperture; contents are not inspected
         * by the driver, it only programs the GATT table and uses
         * the aperture as a translation window for other devices.
         */
        val = 0;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* Writes to the aperture are not observed by this virtual bridge. */
}

/*
 * PIO handlers: the AMD64 AGP driver never uses I/O port space for
 * this bridge, only PCI config and memory BARs, so we just return 0
 * and ignore writes.
 */
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;

    pci_device_reset(pdev);

    /* Re-establish fixed config values after reset. */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Zero internal shadows; the Linux driver will reconfigure. */
    s->gart_aperturectl = 0;
    s->gart_aperturebase = 0;
    s->nvidia_apbase0 = 0;
    s->uli_base_addr = 0;
    s->uli_htt_fea = 0;
    s->uli_enu_scr = 0;
    s->nvidia1_apbase1 = 0;
    s->nvidia1_aplimit1 = 0;
    s->nvidia1_apsize = 0;
    s->nvidia1_apbase2 = 0;
    s->nvidia1_aplimit2 = 0;

    /* Reflect shadowed registers into PCI config space after reset. */
    pci_set_long(pci_conf + AMD64_GARTAPERTURECTL, s->gart_aperturectl);
    pci_set_long(pci_conf + AMD64_GARTAPERTUREBASE, s->gart_aperturebase);
    pci_set_long(pci_conf + NVIDIA_X86_64_0_APBASE, s->nvidia_apbase0);
    pci_set_long(pci_conf + ULI_X86_64_BASE_ADDR, s->uli_base_addr);
    pci_set_long(pci_conf + ULI_X86_64_HTT_FEA_REG, s->uli_htt_fea);
    pci_set_long(pci_conf + ULI_X86_64_ENU_SCR_REG, s->uli_enu_scr);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

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

/*
 * Override default config read/write to keep internal shadows for
 * driver-visible config dwords that are interpreted by generic AGP
 * helper code (e.g. aperture base/size).
 */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    (void)pci_conf;

    switch (addr) {
    case AMD64_GARTAPERTURECTL:
        return s->gart_aperturectl;
    case AMD64_GARTAPERTUREBASE:
        return s->gart_aperturebase;
    case NVIDIA_X86_64_0_APBASE:
        return s->nvidia_apbase0;
    case ULI_X86_64_HTT_FEA_REG:
        return s->uli_htt_fea;
    case ULI_X86_64_ENU_SCR_REG:
        return s->uli_enu_scr;
    default:
        break;
    }

    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    switch (addr) {
    case AMD64_GARTAPERTURECTL:
        if (len == 4) {
            s->gart_aperturectl = val;
            pci_set_long(pci_conf + AMD64_GARTAPERTURECTL, s->gart_aperturectl);
        }
        return;
    case AMD64_GARTAPERTUREBASE:
        if (len == 4) {
            s->gart_aperturebase = val;
            pci_set_long(pci_conf + AMD64_GARTAPERTUREBASE, s->gart_aperturebase);
        }
        return;
    case NVIDIA_X86_64_0_APBASE:
        if (len == 4) {
            s->nvidia_apbase0 = val;
            pci_set_long(pci_conf + NVIDIA_X86_64_0_APBASE, s->nvidia_apbase0);
        }
        return;
    case ULI_X86_64_HTT_FEA_REG:
        if (len == 4) {
            s->uli_htt_fea = val;
            pci_set_long(pci_conf + ULI_X86_64_HTT_FEA_REG, s->uli_htt_fea);
        }
        return;
    case ULI_X86_64_ENU_SCR_REG:
        if (len == 4) {
            s->uli_enu_scr = val;
            pci_set_long(pci_conf + ULI_X86_64_ENU_SCR_REG, s->uli_enu_scr);
        }
        return;
    default:
        break;
    }

    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Advertise as PCIe endpoint, as done in skeleton. */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* === 新增：注入 AGP Capability (Capability ID: 0x02，大小一般给16字节) === */
    int agp_pos = pci_add_capability(pdev, PCI_CAP_ID_AGP, 0, 16, errp);
    if (agp_pos > 0) {
        /* * 驱动的 probe 函数后续有一句：
         * pci_read_config_dword(pdev, bridge->capndx+PCI_AGP_STATUS, &bridge->mode);
         * AGP Status 寄存器在 Capability 结构体内的偏移量是 0x4。
         * 我们需要伪造一个看起来合法的状态值，让驱动认为它支持 AGP 3.0 和 8X 模式等。
         */
        uint32_t fake_agp_status = AGPSTAT_AGP_ENABLE | AGPSTAT_MODE_3_0 | AGPSTAT3_8X;
        pci_set_long(pci_conf + agp_pos + 4, fake_agp_status);
    }   

    /* BAR Initialization: BAR0 as AGP aperture. */
    s->num_bars = 1;
    s->bar_info[0].index = AGP_APERTURE_BAR;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    /* Use 32 MiB aperture; driver will treat this as GART window. */
    s->bar_info[0].size  = 32 * MiB;
    s->bar_info[0].name  = "agp-aperture";

    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* The driver does not enable MSI/MSI-X explicitly; keep disabled. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize internal shadow registers to 0. */
    s->gart_aperturectl = 0;
    s->gart_aperturebase = 0;
    s->nvidia_apbase0 = 0;
    s->uli_base_addr = 0;
    s->uli_htt_fea = 0;
    s->uli_enu_scr = 0;
    s->nvidia1_apbase1 = 0;
    s->nvidia1_aplimit1 = 0;
    s->nvidia1_apsize = 0;
    s->nvidia1_apbase2 = 0;
    s->nvidia1_aplimit2 = 0;

    /*
     * Reflect AMD64 GART aperture control/base default values into
     * PCI config space so that early reads see consistent state.
     * The real hardware defaults are not obvious from the driver,
     * so we leave them as zero until the driver programs them via
     * pci_write_config_dword().
     */
    pci_set_long(pci_conf + AMD64_GARTAPERTURECTL, s->gart_aperturectl);
    pci_set_long(pci_conf + AMD64_GARTAPERTUREBASE, s->gart_aperturebase);
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
    .name = "agpgart_amd64_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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

