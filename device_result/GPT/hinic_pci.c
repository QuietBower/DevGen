/*
 * QEMU PCI device model for Huawei HiNIC (minimal probe-support emulation)
 *
 * This model is intentionally minimal and only implements behavior that is
 * explicitly visible from the provided hinic_main.c and the hwif reg
 * accessors. It focuses on:
 *  - PCI IDs and BAR layout
 *  - MMIO access to cfg_regs BAR for mailbox-related registers and basic
 *    attribute/control fields used for OUTBOUND/DB state
 *  - Basic reset and MSI-X capability exposure
 *
 * It does NOT attempt to emulate full datapath, DMA rings, or interrupt
 * coalescing logic, because those details are not present in the provided
 * source.
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

#define TYPE_PCIBASE_DEVICE "hinic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define HINIC_DEV_ID_QUAD_PORT_25GE         0x1822

/* Use the first PCI ID table entry: { PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_QUAD_PORT_25GE), 0 } */
#define HINIC_PCI_VENDOR_ID   0x19e5
#define HINIC_PCI_DEVICE_ID   HINIC_DEV_ID_QUAD_PORT_25GE

/* BAR indices used by the driver */
#define HINIC_PCI_CFG_REGS_BAR          0
#define HINIC_PCI_INTR_REGS_BAR         2
#define HINIC_PCI_DB_BAR                4

/* Mailbox register offsets used via hinic_hwif_{read,write}_reg() */
#define HINIC_FUNC_CSR_MAILBOX_DATA_OFF             0x0080
#define HINIC_FUNC_CSR_MAILBOX_CONTROL_OFF          0x0100
#define HINIC_FUNC_CSR_MAILBOX_INT_OFFSET_OFF       0x0104
#define HINIC_FUNC_CSR_MAILBOX_RESULT_H_OFF         0x0108
#define HINIC_FUNC_CSR_MAILBOX_RESULT_L_OFF         0x010C

/* Additional CSR offsets inferred from usage in driver code */
#define HINIC_CSR_FUNC_ATTR0_ADDR                   0x0000
#define HINIC_CSR_FUNC_ATTR1_ADDR                   0x0004
#define HINIC_CSR_FUNC_ATTR2_ADDR                   0x0008
#define HINIC_CSR_FUNC_ATTR4_ADDR                   0x0010
#define HINIC_CSR_FUNC_ATTR5_ADDR                   0x0014

/*
 * Bitfield helpers: real masks/shifts are in driver; here we just provide
 * identity helpers so that read-modify-write sequences behave consistently
 * at the register level. Any masking/shift is handled by driver macros.
 */
#define HINIC_FA4_CLEAR(val, member)                 (val)
#define HINIC_FA4_SET(v, member)                     (v)

/* Example values used by driver enums for states */
#define HINIC_OUTBOUND_ENABLE  0
#define HINIC_OUTBOUND_DISABLE 1
#define HINIC_DB_ENABLE        0
#define HINIC_DB_DISABLE       1

/*
 * The driver uses hinic_hwif_write_reg()/hinic_hwif_read_reg() helpers which
 * treat the cfg_regs BAR contents as big-endian 32-bit registers. We model
 * this behavior explicitly here.
 */

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
    /* Minimal mailbox-related register shadows used during init/probe */
    uint32_t mbox_data;
    uint32_t mbox_ctrl;
    uint32_t mbox_int_offset;
    uint32_t mbox_result_h;
    uint32_t mbox_result_l;

    /* FUNC_ATTR* shadows, for OUTBOUND/DB state and HWIF attributes */
    uint32_t func_attr0;
    uint32_t func_attr1;
    uint32_t func_attr2;
    uint32_t func_attr4;
    uint32_t func_attr5;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No static interrupt status/mask layout available in provided source. */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* DMA descriptor formats and registers are not defined in provided source. */
    (void)pdev;
    (void)is_write;
}

/*
 * MMIO/PIO Handlers
 *
 * The only explicit MMIO accessors in the provided driver snippet are
 * hinic_hwif_write_reg()/hinic_hwif_read_reg() that operate on the
 * cfg_regs BAR (BAR0). They use writel()/readl() with a big-endian view.
 *
 * We therefore implement a simple register bank at BAR0 where the
 * mailbox-related registers and FUNC_ATTR* have backing storage. All other
 * offsets are treated as returning 0 and ignoring writes, which is sufficient
 * for early probe logic that only relies on mailbox communication and basic
 * state bits in FUNC_ATTR4.
 */

static uint32_t pcibase_read_cfg_reg(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case HINIC_CSR_FUNC_ATTR0_ADDR:
        return s->func_attr0;
    case HINIC_CSR_FUNC_ATTR1_ADDR:
        return s->func_attr1;
    case HINIC_CSR_FUNC_ATTR2_ADDR:
        return s->func_attr2;
    case HINIC_CSR_FUNC_ATTR4_ADDR:
        return s->func_attr4;
    case HINIC_CSR_FUNC_ATTR5_ADDR:
        return s->func_attr5;
    case HINIC_FUNC_CSR_MAILBOX_DATA_OFF:
        return s->mbox_data;
    case HINIC_FUNC_CSR_MAILBOX_CONTROL_OFF:
        return s->mbox_ctrl;
    case HINIC_FUNC_CSR_MAILBOX_INT_OFFSET_OFF:
        return s->mbox_int_offset;
    case HINIC_FUNC_CSR_MAILBOX_RESULT_H_OFF:
        return s->mbox_result_h;
    case HINIC_FUNC_CSR_MAILBOX_RESULT_L_OFF:
        return s->mbox_result_l;
    default:
        /* For all undocumented registers, return 0 */
        return 0;
    }
}

static void pcibase_write_cfg_reg(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case HINIC_CSR_FUNC_ATTR0_ADDR:
        s->func_attr0 = val;
        break;
    case HINIC_CSR_FUNC_ATTR1_ADDR:
        s->func_attr1 = val;
        break;
    case HINIC_CSR_FUNC_ATTR2_ADDR:
        s->func_attr2 = val;
        break;
    case HINIC_CSR_FUNC_ATTR4_ADDR:
        /* Driver performs read-modify-write sequences on this register using
         * HINIC_FA4_CLEAR/SET macros. Store raw value so subsequent reads
         * observe what was written.
         */
        s->func_attr4 = val;
        break;
    case HINIC_CSR_FUNC_ATTR5_ADDR:
        s->func_attr5 = val;
        break;
    case HINIC_FUNC_CSR_MAILBOX_DATA_OFF:
        s->mbox_data = val;
        break;
    case HINIC_FUNC_CSR_MAILBOX_CONTROL_OFF:
        s->mbox_ctrl = val;
        break;
    case HINIC_FUNC_CSR_MAILBOX_INT_OFFSET_OFF:
        s->mbox_int_offset = val;
        break;
    case HINIC_FUNC_CSR_MAILBOX_RESULT_H_OFF:
        s->mbox_result_h = val;
        break;
    case HINIC_FUNC_CSR_MAILBOX_RESULT_L_OFF:
        s->mbox_result_l = val;
        break;
    default:
        /* Ignore writes to unknown cfg registers */
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver uses readl(), which is 32-bit. If size != 4, just
     * return 0 to avoid undefined semantics.
     */
    if (size != 4) {
        return 0;
    }

    /* cfg_regs is BAR0: we map mailbox offsets and FUNC_ATTR* explicitly. */
    val = pcibase_read_cfg_reg(s, addr);

    /* The driver expects big-endian register semantics at this level, but
     * hinic_hwif_read_reg() converts be32 -> cpu before returning to driver.
     * Our shadow values are stored in host-endian, and QEMU's MMIO interface
     * expects CPU-endian values as well, so no additional byte swapping is
     * required here.
     */

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    /* See comment in read: only documented mailbox offsets and FUNC_ATTR*
     * are modeled.
     */
    pcibase_write_cfg_reg(s, addr, (uint32_t)val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Driver does not use legacy PIO for hinic; keep stubbed. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Driver does not use legacy PIO for hinic; keep stubbed. */
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

    /* Reset mailbox-related register shadows to 0 */
    s->mbox_data = 0;
    s->mbox_ctrl = 0;
    s->mbox_int_offset = 0;
    s->mbox_result_h = 0;
    s->mbox_result_l = 0;

    /* Reset FUNC_ATTR* to a state where outbound/db are enabled so that
     * wait_for_outbound_state() and wait_for_db_state() can succeed. The
     * concrete bit encodings are provided by driver macros.
     */
    s->func_attr0 = 0;
    s->func_attr1 = 0;
    s->func_attr2 = 0;

    /* func_attr4: ensure OUTBOUND_STATE == ENABLE and DB_STATE == ENABLE.
     * Without knowledge of exact bitfields, we rely on driver-side macros
     * to interpret this value; setting to 0 corresponds to ENABLE states
     * because enums define ENABLE as 0.
     */
    s->func_attr4 = 0;

    /* func_attr5: PF_ACTION defaults to 0 (no action) after reset. */
    s->func_attr5 = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  HINIC_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  HINIC_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* The driver uses three MMIO BARs: cfg (0), interrupt regs (2), and doorbell (4).
     * Exact sizes are not specified in the provided source, so use minimal non-zero
     * placeholder sizes just for structural registration; behavior will be modeled later.
     */
    s->num_bars = 3;

    s->bar_info[0].index = HINIC_PCI_CFG_REGS_BAR;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000; /* minimal page for cfg regs */
    s->bar_info[0].name  = "hinic-cfg";

    s->bar_info[1].index = HINIC_PCI_INTR_REGS_BAR;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 0x1000; /* minimal page for intr regs */
    s->bar_info[1].name  = "hinic-intr";

    s->bar_info[2].index = HINIC_PCI_DB_BAR;
    s->bar_info[2].type  = BAR_TYPE_MMIO;
    s->bar_info[2].size  = 0x1000; /* minimal page for doorbells */
    s->bar_info[2].name  = "hinic-db";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt capabilities: driver uses MSI-X (msix_entry, msix_config, etc.).
     * Exact vector count is not defined here; initialize with a small default (e.g., 32).
     */
    s->has_msix = true;
    s->has_msi  = false;

    if (s->has_msix) {
        /* Allocate a reasonable upper bound for queues and event queues; adjust later if needed. */
        int nvec = 32;
        if (msix_init_exclusive_bar(pdev, nvec, 1, errp)) {
            /* If MSI-X init fails, just continue without MSI-X for now. */
            s->has_msix = false;
        }
    }

    /* Initialize mailbox register shadows */
    s->mbox_data = 0;
    s->mbox_ctrl = 0;
    s->mbox_int_offset = 0;
    s->mbox_result_h = 0;
    s->mbox_result_l = 0;

    /* Initialize FUNC_ATTR* so that outbound/DB states are effectively enabled
     * and HWIF appears ready. func_attr0/1/2 contents are opaque to QEMU.
     */
    s->func_attr0 = 0;
    s->func_attr1 = 0;
    s->func_attr2 = 0;
    s->func_attr4 = 0; /* OUTBOUND_ENABLE & DB_ENABLE */
    s->func_attr5 = 0; /* PF_ACTION = 0 */
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
    .name = "hinic_pci",
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
