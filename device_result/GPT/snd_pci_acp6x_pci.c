/*
 * QEMU PCI device model for AMD ACP6x audio controller (simplified)
 * Generated to satisfy Linux driver sound/soc/amd/yc/pci-acp6x.c
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

#define TYPE_PCIBASE_DEVICE "snd_pci_acp6x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ACP_POWER_ON_IN_PROGRESS              1
#define ACP_PGFSM_CNTL_POWER_ON_MASK          1
#define ACP_PGFSM_CONTROL                     0x1241024
#define ACP_PGFSM_STATUS                      0x1241028
#define ACP_PGFSM_STATUS_MASK                 3
#define ACP_SOFT_RESET                        0x1241000
#define ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK 0x00010001
#define ACP_EXTERNAL_INTR_ENB                 0x1241A00
#define ACP_EXT_INTR_STAT_CLEAR_MASK          0xFFFFFFFF
#define ACP_EXTERNAL_INTR_CNTL                0x1241A04
#define ACP_EXTERNAL_INTR_STAT                0x1241A0C
#define ACP_CLKMUX_SEL                        0x124102C
#define ACP_CONTROL                           0x1241004
#define PDM_DMA_STAT                          0x10
#define ACP_PIN_CONFIG                        0x1241440
#define ACP6x_PDM_MODE                        1
#define ACP_SUSPEND_DELAY_MS                  2000
#define ACP_DEVICE_ID                         0x15E2

#define ACP6x_PHY_BASE_ADDRESS                0x1240000
#define ACP6x_REG_START                       0x1240000
#define ACP6x_REG_END                         0x1250200

#define PCIBASE_VENDOR_ID PCI_VENDOR_ID_AMD
#define PCIBASE_DEVICE_ID ACP_DEVICE_ID
#define PCIBASE_CLASS_ID (PCI_CLASS_MULTIMEDIA_OTHER)

#define ACP6x_DEVS                            3

/* BAR configuration: driver uses only BAR 0 via pci_resource_start(pci, 0).
 * Size is not specified; we choose 1 MiB which safely covers used offsets
 * (up to ACP_PIN_CONFIG 0x1241440, but we cannot model holes beyond BAR
 * size. To stay within QEMU constraints, we expose 2 MiB window. The driver
 * only checks specific registers which we shadow explicitly.
 */
#define ACP_BAR0_SIZE                         (2 * MiB)

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
    uint32_t acp_pgfsm_cntl;
    uint32_t acp_pgfsm_status;
    uint32_t acp_soft_reset;
    uint32_t acp_external_intr_enb;
    uint32_t acp_external_intr_cntl;
    uint32_t acp_external_intr_stat;
    uint32_t acp_clkmux_sel;
    uint32_t acp_control;
    uint32_t acp_pin_config;

    /* Simple interrupt state */
    uint32_t irq_status;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The driver requests a legacy shared IRQ. Emulate via INTx. */
    if (s->irq_status && (s->acp_external_intr_enb & 0x1)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * The pci-acp6x.c driver does not program any DMA engine in this device
 * directly; DMA is handled by a separate platform device (acp_yc_pdm_dma).
 * Therefore, we do not implement any PCI-initiated DMA here.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Translate a MMIO offset to our shadow register pointer. Returns true if
 * handled, filling *reg with pointer to shadow. For some registers (status
 * only) we may not want direct writeback.
 */
static bool pcibase_get_reg_ptr(PCIBaseState *s, hwaddr addr, uint32_t **reg)
{
    switch (addr) {
    case ACP_PGFSM_CONTROL:
        *reg = &s->acp_pgfsm_cntl;
        return true;
    case ACP_PGFSM_STATUS:
        *reg = &s->acp_pgfsm_status;
        return true;
    case ACP_SOFT_RESET:
        *reg = &s->acp_soft_reset;
        return true;
    case ACP_EXTERNAL_INTR_ENB:
        *reg = &s->acp_external_intr_enb;
        return true;
    case ACP_EXTERNAL_INTR_CNTL:
        *reg = &s->acp_external_intr_cntl;
        return true;
    case ACP_EXTERNAL_INTR_STAT:
        *reg = &s->acp_external_intr_stat;
        return true;
    case ACP_CLKMUX_SEL:
        *reg = &s->acp_clkmux_sel;
        return true;
    case ACP_CONTROL:
        *reg = &s->acp_control;
        return true;
    case ACP_PIN_CONFIG:
        *reg = &s->acp_pin_config;
        return true;
    default:
        *reg = NULL;
        return false;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t *reg;
    uint32_t val32 = 0;

    /* Only 32-bit accesses are expected by driver (readl/writel). */
    if (size != 4) {
        return 0;
    }

    /* The driver maps the region starting at ACP6x_PHY_BASE_ADDRESS and then
     * uses ACP_* register offsets directly. Our BAR0 is exposed starting at 0,
     * so we translate by subtracting the physical base address.
     */
    if (addr >= ACP6x_REG_START && addr <= ACP6x_REG_END) {
        addr -= ACP6x_PHY_BASE_ADDRESS;
    }

    if (pcibase_get_reg_ptr(s, addr, &reg)) {
        val32 = *reg;
    } else {
        /* Unused/unknown register: read as 0 */
        val32 = 0;
    }

    return val32;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t *reg;
    uint32_t v = (uint32_t)val;

    if (size != 4) {
        return;
    }

    /* Translate from physical-style offsets used by the driver to our
     * BAR-relative offsets.
     */
    if (addr >= ACP6x_REG_START && addr <= ACP6x_REG_END) {
        addr -= ACP6x_PHY_BASE_ADDRESS;
    }

    if (!pcibase_get_reg_ptr(s, addr, &reg)) {
        /* Ignore writes to unknown offsets */
        return;
    }

    if (addr == ACP_PGFSM_CONTROL) {
        /* Driver writes ACP_PGFSM_CNTL_POWER_ON_MASK to request power on. */
        *reg = v;
        if (v & ACP_PGFSM_CNTL_POWER_ON_MASK) {
            /* Complete power-on sequence by driving status to 0 (success). */
            s->acp_pgfsm_status = 0;
        }
        return;
    }

    if (addr == ACP_SOFT_RESET) {
        /* Soft reset logic: when driver writes 1, we immediately set
         * SOFTRESET_AUDDONE bits; on write 0 we clear the register. */
        if (v == 1) {
            s->acp_soft_reset = ACP_SOFT_RESET_SOFTRESET_AUDDONE_MASK;
        } else if (v == 0) {
            s->acp_soft_reset = 0;
        } else {
            s->acp_soft_reset = v;
        }
        return;
    }

    if (addr == ACP_EXTERNAL_INTR_ENB) {
        s->acp_external_intr_enb = v;
        pcibase_update_irq(s);
        return;
    }

    if (addr == ACP_EXTERNAL_INTR_CNTL) {
        s->acp_external_intr_cntl = v;
        return;
    }

    if (addr == ACP_EXTERNAL_INTR_STAT) {
        /* The driver uses write-1-to-clear for BIT(PDM_DMA_STAT). */
        uint32_t mask = (uint32_t)v;
        s->acp_external_intr_stat &= ~mask;
        s->irq_status &= ~mask;
        pcibase_update_irq(s);
        return;
    }

    if (addr == ACP_CLKMUX_SEL) {
        s->acp_clkmux_sel = v;
        return;
    }

    if (addr == ACP_CONTROL) {
        s->acp_control = v;
        return;
    }

    if (addr == ACP_PIN_CONFIG) {
        /* The driver only reads this register to decide audio mode; it
         * never writes. Still, honor writes for completeness. */
        s->acp_pin_config = v;
        return;
    }

    /* Default handler for any other known register pointer */
    *reg = v;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    /* Initialize register shadows to power-on defaults that satisfy
     * the driver's expectations.
     */
    s->acp_pgfsm_cntl = 0;
    /* Non-zero status so that acp6x_power_on() will attempt power-on
     * and then we drive it to 0 when CNTL is written.
     */
    s->acp_pgfsm_status = ACP_POWER_ON_IN_PROGRESS;
    s->acp_soft_reset = 0;
    s->acp_external_intr_enb = 0;
    s->acp_external_intr_cntl = 0;
    s->acp_external_intr_stat = 0;
    s->acp_clkmux_sel = 0;
    s->acp_control = 0;

    /* Select a pin configuration that leads the driver into ACP6x_PDM_MODE.
     * The driver treats only specific constants (ACP_CONFIG_0,1,2,3,9,15)
     * specially, and default case enables PDM mode. We choose an arbitrary
     * value not equal to those (e.g., 0xFF) to enter that path.
     */
    s->acp_pin_config = 0xFF;

    s->irq_status = 0;

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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x60); /* revision expected by driver */
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize MSI/MSI-X flags (but driver uses legacy INTx). */
    s->has_msi = false;
    s->has_msix = false;

    /* BAR Initialization */
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = ACP_BAR0_SIZE;
    s->bar_info[0].name = "acp6x-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Ensure device starts in reset defaults */
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
    .name = "snd_pci_acp6x_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(acp_pgfsm_cntl, PCIBaseState),
        VMSTATE_UINT32(acp_pgfsm_status, PCIBaseState),
        VMSTATE_UINT32(acp_soft_reset, PCIBaseState),
        VMSTATE_UINT32(acp_external_intr_enb, PCIBaseState),
        VMSTATE_UINT32(acp_external_intr_cntl, PCIBaseState),
        VMSTATE_UINT32(acp_external_intr_stat, PCIBaseState),
        VMSTATE_UINT32(acp_clkmux_sel, PCIBaseState),
        VMSTATE_UINT32(acp_control, PCIBaseState),
        VMSTATE_UINT32(acp_pin_config, PCIBaseState),
        VMSTATE_UINT32(irq_status, PCIBaseState),
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
