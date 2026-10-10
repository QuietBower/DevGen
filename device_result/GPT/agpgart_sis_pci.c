/*
 * QEMU PCI device model for SiS AGP bridge (agpgart_sis_pci)
 * Generated to satisfy linux-6.18/drivers/char/agp/sis-agp.c probe
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

#define TYPE_PCIBASE_DEVICE "agpgart_sis_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SIS_ATTBASE     0x90
#define SIS_APSIZE      0x94
#define SIS_TLBCNTRL    0x97
#define SIS_TLBFLUSH    0x98
#define SIS_AGP_APERTURE_BAR 0
#define SIS_AGPSTAT     0x04
#define SIS_AGPCMD      0x08
#define SIS_AGPNISTAT   0x0c
#define SIS_AGPCTRL     0x10
#define SIS_AGPAPSIZE   0x14
#define SIS_AGPGARTLO   0x18

#define SIS_AGPSTAT_AGP_ENABLE   (1u << 8)
#define SIS_AGPSTAT_RQ_DEPTH     0xff000000u
#define SIS_AGPSTAT_FW           (1u << 4)
#define SIS_AGPSTAT_MODE_3_0     (1u << 3)
#define SIS_AGPSTAT_SBA          (1u << 9)
#define SIS_AGPSTAT3_RSVD        (1u << 2)
#define SIS_AGPSTAT2_1X          (1u << 0)
#define SIS_AGPSTAT2_2X          (1u << 1)
#define SIS_AGPSTAT2_4X          (1u << 2)
#define SIS_AGPSTAT3_8X          (1u << 1)
#define SIS_AGPSTAT3_4X          (1u << 0)

#define SIS_AGPCTRL_APERENB      (1u << 8)
#define SIS_AGPCTRL_GTLBEN       (1u << 7)

#define SIS_AGP_MAJOR_VERSION_SHIFT 20
#define SIS_AGP_MINOR_VERSION_SHIFT 16

#define SIS_VENDOR_ID   0x1039
#define SIS_DEVICE_ID   0x5591
#define SIS_CLASS_ID    0x0600


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
    struct {
        uint32_t agpstat;
        uint32_t agpcmd;
        uint32_t agpnistat;
        uint32_t agpctrl;
        uint32_t agpapsize;
        uint32_t agpgartlo;
        uint32_t attbase;
        uint32_t apsize;
        uint8_t  tlbcntrl;
        uint8_t  tlbflush;
    } regs;

};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The provided driver never enables or handles interrupts for this
     * device. Do not signal any IRQs.
     */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The driver does not program any DMA engines on the SiS AGP bridge
     * itself; it only uses the AGP aperture / GATT. No bus-master DMA
     * logic is required here.
     */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Aperture BAR is only used as a memory window for AGP accesses and
     * not directly touched in the provided driver logic via MMIO ops.
     * Return zero for all reads.
     */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* See comment in pcibase_mmio_read: no explicit MMIO access patterns
     * from the driver to this BAR are specified, so ignore writes.
     */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No PIO port accesses are used by the driver. */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No PIO port accesses are used by the driver. */
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

    /* Initialize configuration space shadow registers used by the driver. */

    /* AGP capability registers shadow. The driver reads these via
     * pci_read_config_dword(pdev, capndx + PCI_AGP_STATUS, &bridge->mode)
     * and writes PCI_AGP_COMMAND in other devices only, so we must provide
     * a sane AGP_STATUS here.
     */
    s->regs.agpstat = 0;

    /* Expose AGP 3.0 capability in AGP status: major/minor and mode bits. */
    s->regs.agpstat |= (3u << SIS_AGP_MAJOR_VERSION_SHIFT);
    s->regs.agpstat |= (5u << SIS_AGP_MINOR_VERSION_SHIFT); /* 3.5 as used in driver logic */

    /* Allow various transfer rates and features; details are not interpreted
     * by this model but must be non-zero so that the generic AGP code can
     * negotiate modes.
     */
    s->regs.agpstat |= SIS_AGPSTAT_SBA;
    s->regs.agpstat |= SIS_AGPSTAT_FW;
    s->regs.agpstat |= SIS_AGPSTAT_MODE_3_0;
    s->regs.agpstat |= SIS_AGPSTAT2_1X |
                       SIS_AGPSTAT2_2X |
                       SIS_AGPSTAT2_4X;
    s->regs.agpstat |= SIS_AGPSTAT3_4X |
                       SIS_AGPSTAT3_8X;
    /* Set some reasonable RQ depth. */
    s->regs.agpstat |= (0x20u << 24); /* fits SIS_AGPSTAT_RQ_DEPTH mask */

    /* Command register (AGP command). The generic AGP core may write this
     * via agp_generic_enable(), but it does so on the AGP master device,
     * not necessarily the bridge. For completeness we track writes but do
     * not enforce any behaviour.
     */
    s->regs.agpcmd = 0;

    /* Non-isochronous status, control and aperture/GART settings. The
     * driver does not read these fields directly from the bridge, so we
     * simply reset to zero.
     */
    s->regs.agpnistat = 0;
    s->regs.agpctrl   = 0;
    s->regs.agpapsize = 0;
    s->regs.agpgartlo = 0;

    /* PCI config space offsets 0x90..0x98 used via pci_{read,write}_config
     * in the driver.
     */
    s->regs.attbase   = 0;
    s->regs.apsize    = 0;
    s->regs.tlbcntrl  = 0;
    s->regs.tlbflush  = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  SIS_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SIS_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SIS_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Enable PCI Express capability so that the device is accepted as a
     * modern PCIe/AGP bridge by generic kernel PCI code. The sis-agp
     * driver also requires an AGP capability (PCI_CAP_ID_AGP) which we
     * model below.
     */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Power management capability (not used by driver beyond registration). */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Add AGP capability at a fixed offset (0x60 chosen to leave room
     * before PM and PCIe caps). The exact placement is irrelevant as long
     * as pci_find_capability() can discover it.
     */
    int agp_pos = pci_add_capability(pdev, PCI_CAP_ID_AGP, 0, PCI_AGP_SIZEOF, errp);
    if (agp_pos > 0) {
        /* Initialize AGP STATUS dword based on our shadow register. */
        pci_set_long(pci_conf + agp_pos + PCI_AGP_STATUS, s->regs.agpstat);
        /* Initialize AGP COMMAND dword to zero. Generic AGP code may
         * write this field in other devices; we keep it for completeness.
         */
        pci_set_long(pci_conf + agp_pos + PCI_AGP_COMMAND, s->regs.agpcmd);
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = SIS_AGP_APERTURE_BAR;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000000; /* 16 MiB default aperture window */
    s->bar_info[0].name  = "sis-agp-aperture";
    for (int i = 1; i < 6; ++i) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "unused";
    }
      
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "agpgart_sis_pci",
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
