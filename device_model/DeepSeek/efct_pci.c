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

#define TYPE_PCIBASE_DEVICE "efct_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define EFCT_VENDOR_ID               0x10df
#define EFCT_DEVICE_ID               0xe307
#define EFCT_CLASS_ID                0x0c04   /* PCI Class Storage Fibre Channel, not from driver */

/* Register offsets from driver source */
#define SLI4_ASIC_ID_REG             0x009c   /* Config space, not MMIO */
#define SLI4_INTF_REG                0x0058   /* Config space, not MMIO */
#define SLI4_BMBX_REG                0x0160
#define SLI4_PORT_STATUS_REGOFF      0x0404
#define SLI4_PORT_CTRL_REG           0x0408
#define SLI4_PORT_ERROR1             0x040c
#define SLI4_PORT_ERROR2             0x0410
#define SLI4_PHYDEV_CTRL_REG         0x0414
#define SLI4_PHYDEV_CTRL_FRST        (1 << 1)
#define SLI4_RQ_DB_REG               0x0a0
#define SLI4_IF6_RQ_DB_REG           0x0080
#define SLI4_IF6_CQ_DB_REG           0x00c0
#define SLI4_EQCQ_DB_REG             0x0120
#define SLI4_REG_RPI_BUF_LEN         0x0070

/* Additional hardware definitions from driver */
#define SLI4_RCQE_RQ_EL_INDX         0xfff
#define SLI4_FC_COALESCE_RQ_SUCCESS  0x10
#define SLI4_CQE_CODE_OFFSET         14
#define SLI4_OCQE_XB                 0x10
#define SLI4_MASK_CCP                0xfe
#define SLI4_READSTATUS_CLEAR_COUNTERS 0x1
#define SLI4_CQE_BYTES               (4 * sizeof(u32))

/* Enums and types from driver that map hardware states */
enum efct_hw_state {
    EFCT_HW_STATE_UNINITIALIZED,
    EFCT_HW_STATE_QUEUES_ALLOCATED,
    EFCT_HW_STATE_ACTIVE,
    EFCT_HW_STATE_RESET_IN_PROGRESS,
    EFCT_HW_STATE_TEARDOWN_IN_PROGRESS,
};

enum sli4_qtype {
    SLI4_QTYPE_EQ,
    SLI4_QTYPE_CQ,
    SLI4_QTYPE_MQ,
    SLI4_QTYPE_WQ,
    SLI4_QTYPE_RQ,
    SLI4_QTYPE_MAX,
};

enum sli4_resource {
    SLI4_RSRC_VFI,
    SLI4_RSRC_VPI,
    SLI4_RSRC_RPI,
    SLI4_RSRC_XRI,
    SLI4_RSRC_FCFI,
    SLI4_RSRC_MAX,
};

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
        uint32_t asic_id;
        uint32_t intf;
        uint32_t bmbx_lo;
        uint32_t bmbx_hi;
        uint32_t port_status;
        uint32_t port_ctrl;
        uint32_t port_error1;
        uint32_t port_error2;
        uint32_t phydev_ctrl;
        uint32_t rpi_buf_len;
        uint32_t rq_db;
        uint32_t if6_rq_db;
        uint32_t if6_cq_db;
        uint32_t eqcq_db;
    } regs;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* No interrupt logic derived from provided driver source */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA logic derived from provided driver source */
}

/* Custom PCI config read: override registers accessed via pci_read_config_dword */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr == SLI4_INTF_REG && len == 4) {
        return s->regs.intf;
    } else if (addr == SLI4_ASIC_ID_REG && len == 4) {
        return s->regs.asic_id;
    } else {
        return pci_default_read_config(pdev, addr, len);
    }
}

/* Custom PCI config write: override registers accessed via pci_write_config_dword */
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr == SLI4_INTF_REG && len == 4) {
        s->regs.intf = val;
    } else if (addr == SLI4_ASIC_ID_REG && len == 4) {
        s->regs.asic_id = val;
    } else {
        pci_default_write_config(pdev, addr, val, len);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4 && size != 8) {
        return 0;
    }

    switch (addr) {
    case 0x0070: /* SLI4_REG_RPI_BUF_LEN */
        val = s->regs.rpi_buf_len;
        break;
    case 0x0160: /* SLI4_BMBX_REG + 0 */
        val = s->regs.bmbx_lo;
        break;
    case 0x0164: /* SLI4_BMBX_REG + 4 */
        val = s->regs.bmbx_hi;
        break;
    case 0x0404: /* SLI4_PORT_STATUS_REGOFF */
        val = s->regs.port_status;
        break;
    case 0x0408: /* SLI4_PORT_CTRL_REG */
        val = s->regs.port_ctrl;
        break;
    case 0x040c: /* SLI4_PORT_ERROR1 */
        val = s->regs.port_error1;
        break;
    case 0x0410: /* SLI4_PORT_ERROR2 */
        val = s->regs.port_error2;
        break;
    case 0x0414: /* SLI4_PHYDEV_CTRL_REG */
        val = s->regs.phydev_ctrl;
        break;
    case 0x00a0: /* SLI4_RQ_DB_REG (write-only) */
        val = 0;
        break;
    case 0x0080: /* SLI4_IF6_RQ_DB_REG (write-only) */
        val = 0;
        break;
    case 0x00c0: /* SLI4_IF6_CQ_DB_REG (write-only) */
        val = 0;
        break;
    case 0x0120: /* SLI4_EQCQ_DB_REG (write-only) */
        val = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "efct: unimplemented read at 0x%lx\n", addr);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 && size != 8) {
        return;
    }

    switch (addr) {
    case 0x0070: /* SLI4_REG_RPI_BUF_LEN */
        s->regs.rpi_buf_len = val;
        break;
    case 0x0160: /* BMBX lo */
        s->regs.bmbx_lo = val;
        break;
    case 0x0164: /* BMBX hi */
        s->regs.bmbx_hi = val;
        break;
    case 0x0404: /* PORT_STATUS, write-1-to-clear */
        s->regs.port_status &= ~(val & 0xffffffff);
        break;
    case 0x0408: /* PORT_CTRL */
        s->regs.port_ctrl = val;
        break;
    case 0x0414: /* PHYDEV_CTRL */
        s->regs.phydev_ctrl = val;
        if (val & SLI4_PHYDEV_CTRL_FRST) {
            /* Function reset not implemented, but harmless */
        }
        break;
    case 0x00a0: /* RQ_DB */
        s->regs.rq_db = val;
        break;
    case 0x0080: /* IF6_RQ_DB */
        s->regs.if6_rq_db = val;
        break;
    case 0x00c0: /* IF6_CQ_DB */
        s->regs.if6_cq_db = val;
        break;
    case 0x0120: /* EQCQ_DB */
        s->regs.eqcq_db = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "efct: unimplemented write at 0x%lx val=0x%lx\n", addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used by driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by driver */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
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

    /* Reset registers to power-on defaults */
    s->regs.bmbx_lo = 0;
    s->regs.bmbx_hi = 0;
    s->regs.port_status = 0;
    s->regs.port_ctrl = 0;
    s->regs.port_error1 = 0;
    s->regs.port_error2 = 0;
    s->regs.phydev_ctrl = 0;
    s->regs.rpi_buf_len = 0;
    s->regs.rq_db = 0;
    s->regs.if6_rq_db = 0;
    s->regs.if6_cq_db = 0;
    s->regs.eqcq_db = 0;

    /* Resetting config-space registers will be handled by pci_device_reset
     * and our custom config_write if needed. We set the initial values
     * in realize, but reset should restore them to defaults. Since the driver
     * likely expects the same values after reset, we re-apply the defaults.
     */
    /* s->regs.intf loaded from config defaults; keep */
    /* s->regs.asic_id loaded from config defaults; keep */
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
    int msix_cap;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  EFCT_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  EFCT_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, EFCT_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* MSI-X capability (required for msix_init) */
    msix_cap = pci_add_capability(pdev, PCI_CAP_ID_MSIX, 0, MSIX_CAP_LENGTH, errp);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "efct-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Override PCI config read/write to handle device-specific registers */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    /* Initialize device-specific config-space registers with valid defaults.
     * These values are derived from driver header constants that are currently
     * missing; they will be set correctly when the constants are provided.
     */
    /* TODO: Set s->regs.intf based on SLI4_INTF_VALID_VALUE, SLI4_INTF_REV_S4,
     * and a known family that matches an entry in sli4_asic_table.
     * For now, assign as 0 (will cause probe failure until fixed).
     */
    s->regs.intf = 0;  /* placeholder; will be replaced once defines are available */
    s->regs.asic_id = 0;  /* placeholder */

    /* MSI-X initialization */
    if (msix_init(pdev, 1, &s->bar_regions[0], 0, 0, &s->bar_regions[0], 0, 0x200, msix_cap, errp)) {
        error_setg(errp, "Failed to init MSI-X");
        return;
    }

    /* Initialize regs with power-on defaults */
    s->regs.bmbx_lo = 0;
    s->regs.bmbx_hi = 0;
    s->regs.port_status = 0;
    s->regs.port_ctrl = 0;
    s->regs.port_error1 = 0;
    s->regs.port_error2 = 0;
    s->regs.phydev_ctrl = 0;
    s->regs.rpi_buf_len = 0;
    s->regs.rq_db = 0;
    s->regs.if6_rq_db = 0;
    s->regs.if6_cq_db = 0;
    s->regs.eqcq_db = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* No additional cleanup required */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "efct_pci",
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
