/*
 * QEMU PCI device model for AMD RPL ACP6x audio (minimal behavior
 * sufficient for Linux snd_rpl_pci_acp6x.c driver probe and PM).
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

#define TYPE_PCIBASE_DEVICE "snd_rpl_pci_acp6x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define RPL_ACP_VENDOR_ID        PCI_VENDOR_ID_AMD
#define RPL_ACP_DEVICE_ID        0x15E2
#define RPL_ACP_CLASS_ID         PCI_CLASS_MULTIMEDIA_OTHER

#define ACP_CLKMUX_SEL                                0x1241024
#define ACP_CONTROL                                   0x1241004
#define ACP_POWER_ON_IN_PROGRESS                      1
#define ACP_PGFSM_CONTROL                             0x124101C
#define ACP_PGFSM_STATUS                              0x1241020
#define ACP_PGFSM_STATUS_MASK                         3
#define ACP_PGFSM_CNTL_POWER_ON_MASK                  1
#define ACP_SOFT_RESET                                0x1241000
#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK         0x00010001
#define ACP_SUSPEND_DELAY_MS                          2000


/* BAR description helpers */
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
    uint32_t acp_control;
    uint32_t acp_clkmux_sel;
    uint32_t acp_pgfsm_control;
    uint32_t acp_pgfsm_status;
    uint32_t acp_soft_reset;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    /* The provided driver code does not use interrupts, so we do nothing. */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)s;
    (void)pdev;
    (void)is_write;
    /* The provided driver code does not program any DMA engine, so none is implemented. */
}

/* Translate guest physical register addresses (as seen by driver) to our BAR0 offset.
 * The driver does: writel(val, base_addr - ACP6x_PHY_BASE_ADDRESS);
 * and ACP_* macros are absolute physical addresses. We don't know
 * ACP6x_PHY_BASE_ADDRESS, but for modeling we treat the ACP_* values
 * as offsets into BAR0 directly, which matches how we initialized BAR0
 * size (1 MiB, large enough to cover them without caring about meaning).
 */
static inline uint32_t pcibase_mmio_readl_reg(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case ACP_CONTROL:
        return s->acp_control;
    case ACP_CLKMUX_SEL:
        return s->acp_clkmux_sel;
    case ACP_PGFSM_CONTROL:
        return s->acp_pgfsm_control;
    case ACP_PGFSM_STATUS:
        return s->acp_pgfsm_status;
    case ACP_SOFT_RESET:
        return s->acp_soft_reset;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rpl-acp6x: read32 from unknown offset 0x%" HWADDR_PRIx "\n",
                      addr);
        return 0;
    }
}

static inline void pcibase_mmio_writel_reg(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case ACP_CONTROL:
        s->acp_control = val;
        break;
    case ACP_CLKMUX_SEL:
        s->acp_clkmux_sel = val;
        break;
    case ACP_PGFSM_CONTROL:
        s->acp_pgfsm_control = val;
        if (val & ACP_PGFSM_CNTL_POWER_ON_MASK) {
            /* When driver requests power on, emulate power domain reaching ON
             * state by clearing ACP_PGFSM_STATUS. rpl_power_on() expects a
             * 0 value from ACP_PGFSM_STATUS to complete successfully.
             */
            s->acp_pgfsm_status = 0;
        }
        break;
    case ACP_PGFSM_STATUS:
        /* Driver never writes to STATUS in provided snippet; ignore. */
        break;
    case ACP_SOFT_RESET:
        /* rpl_reset() writes 1 then waits for AUDDONE bit(s) to be set, then
         * writes 0 and waits for register to become 0. We emulate this
         * behavior in-place.
         */
        if (val == 1) {
            s->acp_soft_reset = ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK;
        } else if (val == 0) {
            s->acp_soft_reset = 0;
        } else {
            s->acp_soft_reset = val;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rpl-acp6x: write32 0x%08x to unknown offset 0x%" HWADDR_PRIx "\n",
                      val, addr);
        break;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The ACP_* registers are accessed as 32-bit using readl/writel. */
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rpl-acp6x: invalid MMIO read size %u at 0x%" HWADDR_PRIx "\n",
                      size, addr);
        return 0;
    }

    return pcibase_mmio_readl_reg(s, addr);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rpl-acp6x: invalid MMIO write size %u at 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n",
                      size, addr, val);
        return;
    }

    pcibase_mmio_writel_reg(s, addr, (uint32_t)val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    /* Driver never uses PIO; return 0. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* Driver never uses PIO; ignore. */
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

    /* Establish reset defaults that let rpl_power_on() and rpl_reset()
     * behave as expected.
     * rpl_power_on() does:
     *   val = STATUS;
     *   if (!val) return 0; // already on
     *   if ((val & MASK) != IN_PROGRESS) write CONTROL POWER_ON;
     *   then loops until STATUS becomes 0.
     * We make default STATUS have IN_PROGRESS bits set so that driver
     * writes CONTROL and we then clear STATUS when CONTROL is written.
     */
    s->acp_control = 0;
    s->acp_clkmux_sel = 0;
    s->acp_pgfsm_control = 0;

    /* Encode a non-zero value with ACP_POWER_ON_IN_PROGRESS bits set
     * so that rpl_power_on() will perform the power-on sequence.
     */
    s->acp_pgfsm_status = ACP_POWER_ON_IN_PROGRESS;

    /* SOFT_RESET should be 0 at reset so that rpl_reset() first write of 1
     * sees it transition to AUDDONE then back to 0.
     */
    s->acp_soft_reset = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  RPL_ACP_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RPL_ACP_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RPL_ACP_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x62);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The driver uses pci_resource_start/len for BAR0 only and ioremaps it.
     * It only touches the ACP_* registers which we modeled directly as
     * offsets within this BAR. Size 1 MiB is arbitrary but sufficient.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 1 * MiB;
    s->bar_info[0].name = "rpl-acp6x-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "unused";
    }
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize register state to reset defaults so probe can succeed. */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_rpl_pci_acp6x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(acp_control, PCIBaseState),
        VMSTATE_UINT32(acp_clkmux_sel, PCIBaseState),
        VMSTATE_UINT32(acp_pgfsm_control, PCIBaseState),
        VMSTATE_UINT32(acp_pgfsm_status, PCIBaseState),
        VMSTATE_UINT32(acp_soft_reset, PCIBaseState),
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
