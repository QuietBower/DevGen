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

#define TYPE_PCIBASE_DEVICE "bnge_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* HWRM command codes from driver source */
#define HWRM_VER_GET        0x0UL
#define HWRM_FUNC_DRV_RGTR  0x1dUL

/* HWRM response structures */
struct hwrm_ver_get_output {
    uint16_t  error_code;
    uint16_t  req_type;
    uint16_t  seq_id;
    uint16_t  resp_len;
    uint8_t   hwrm_intf_maj_8b;
    uint8_t   hwrm_intf_min_8b;
    uint8_t   hwrm_intf_upd_8b;
    uint8_t   hwrm_intf_rsvd_8b;
    uint8_t   hwrm_fw_maj_8b;
    uint8_t   hwrm_fw_min_8b;
    uint8_t   hwrm_fw_bld_8b;
    uint8_t   hwrm_fw_rsvd_8b;
    uint8_t   mgmt_fw_maj_8b;
    uint8_t   mgmt_fw_min_8b;
    uint8_t   mgmt_fw_bld_8b;
    uint8_t   mgmt_fw_rsvd_8b;
    uint8_t   netctrl_fw_maj_8b;
    uint8_t   netctrl_fw_min_8b;
    uint8_t   netctrl_fw_bld_8b;
    uint8_t   netctrl_fw_rsvd_8b;
    uint32_t  dev_caps_cfg;
    uint8_t   roce_fw_maj_8b;
    uint8_t   roce_fw_min_8b;
    uint8_t   roce_fw_bld_8b;
    uint8_t   roce_fw_rsvd_8b;
    char      hwrm_fw_name[16];
    char      mgmt_fw_name[16];
    char      netctrl_fw_name[16];
    char      active_pkg_name[16];
    char      roce_fw_name[16];
    uint16_t  chip_num;
    uint8_t   chip_rev;
    uint8_t   chip_metal;
    uint8_t   chip_bond_id;
    uint8_t   chip_platform_type;
    uint16_t  max_req_win_len;
    uint16_t  max_resp_len;
    uint16_t  def_req_timeout;
    uint8_t   flags;
    uint8_t   unused_0[2];
    uint8_t   always_1;
    uint16_t  hwrm_intf_major;
    uint16_t  hwrm_intf_minor;
    uint16_t  hwrm_intf_build;
    uint16_t  hwrm_intf_patch;
    uint16_t  hwrm_fw_major;
    uint16_t  hwrm_fw_minor;
    uint16_t  hwrm_fw_build;
    uint16_t  hwrm_fw_patch;
    uint16_t  mgmt_fw_major;
    uint16_t  mgmt_fw_minor;
    uint16_t  mgmt_fw_build;
    uint16_t  mgmt_fw_patch;
    uint16_t  netctrl_fw_major;
    uint16_t  netctrl_fw_minor;
    uint16_t  netctrl_fw_build;
    uint16_t  netctrl_fw_patch;
    uint16_t  roce_fw_major;
    uint16_t  roce_fw_minor;
    uint16_t  roce_fw_build;
    uint16_t  roce_fw_patch;
    uint16_t  max_ext_req_len;
    uint16_t  max_req_timeout;
    uint8_t   unused_1[3];
    uint8_t   valid;
};

struct hwrm_func_drv_rgtr_output {
    uint16_t  error_code;
    uint16_t  req_type;
    uint16_t  seq_id;
    uint16_t  resp_len;
    uint32_t  flags;
    uint8_t   unused_0[3];
    uint8_t   valid;
};

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID  0x14e4
#define DEVICE_ID  0x1780
#define CLASS_ID   0x020000

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

    /* DMA Context */

    /* Operational status flags */

    /* State used to handle reset sequences */

    /* Power management state (D0-D3) */

    /* HWRM mailbox emulation to satisfy probe */
    uint8_t hwrm_mailbox[256];
    uint32_t hwrm_status; /* 0: idle, 1: response ready */
};

/* Helper to process a HWRM command from the mailbox */
static void hwrm_process_command(PCIBaseState *s)
{
    uint16_t req_type = lduw_le_p(&s->hwrm_mailbox[0x0]);
    uint16_t seq_id = lduw_le_p(&s->hwrm_mailbox[0x2]);

    /* Clear mailbox up to 0x100 and prepare response */
    memset(s->hwrm_mailbox, 0, 0x100);

    if (req_type == HWRM_VER_GET) {
        struct hwrm_ver_get_output *resp = (struct hwrm_ver_get_output *)s->hwrm_mailbox;
        resp->error_code = cpu_to_le16(0);
        resp->req_type = cpu_to_le16(HWRM_VER_GET);
        resp->seq_id = cpu_to_le16(seq_id);
        resp->resp_len = cpu_to_le16(sizeof(struct hwrm_ver_get_output));
        resp->hwrm_intf_maj_8b = 1;
        resp->hwrm_intf_min_8b = 1;
        resp->hwrm_intf_upd_8b = 1;
        resp->hwrm_intf_rsvd_8b = 0;
        resp->hwrm_fw_maj_8b = 1;
        resp->hwrm_fw_min_8b = 2;
        resp->hwrm_fw_bld_8b = 3;
        resp->hwrm_fw_rsvd_8b = 0;
        resp->mgmt_fw_maj_8b = 200;
        resp->mgmt_fw_min_8b = 100;
        resp->mgmt_fw_bld_8b = 50;
        resp->mgmt_fw_rsvd_8b = 0;
        resp->netctrl_fw_maj_8b = 100;
        resp->netctrl_fw_min_8b = 50;
        resp->netctrl_fw_bld_8b = 25;
        resp->netctrl_fw_rsvd_8b = 0;
        resp->dev_caps_cfg = cpu_to_le32(0);
        resp->roce_fw_maj_8b = 0;
        resp->roce_fw_min_8b = 0;
        resp->roce_fw_bld_8b = 0;
        resp->roce_fw_rsvd_8b = 0;
        memset(resp->hwrm_fw_name, 0, sizeof(resp->hwrm_fw_name));
        memset(resp->mgmt_fw_name, 0, sizeof(resp->mgmt_fw_name));
        memset(resp->netctrl_fw_name, 0, sizeof(resp->netctrl_fw_name));
        memset(resp->active_pkg_name, 0, sizeof(resp->active_pkg_name));
        memset(resp->roce_fw_name, 0, sizeof(resp->roce_fw_name));
        resp->chip_num = cpu_to_le16(0x5770);
        resp->chip_rev = 1;
        resp->chip_metal = 0;
        resp->chip_bond_id = 0;
        resp->chip_platform_type = 0; /* ASIC */
        resp->max_req_win_len = cpu_to_le16(0x10);
        resp->max_resp_len = cpu_to_le16(0x10);
        resp->def_req_timeout = cpu_to_le16(5000);
        resp->flags = 0;
        resp->unused_0[0] = 0; resp->unused_0[1] = 0;
        resp->always_1 = 1;
        resp->hwrm_intf_major = cpu_to_le16(1);
        resp->hwrm_intf_minor = cpu_to_le16(1);
        resp->hwrm_intf_build = cpu_to_le16(1);
        resp->hwrm_intf_patch = cpu_to_le16(1);
        resp->hwrm_fw_major = cpu_to_le16(1);
        resp->hwrm_fw_minor = cpu_to_le16(2);
        resp->hwrm_fw_build = cpu_to_le16(3);
        resp->hwrm_fw_patch = cpu_to_le16(4);
        resp->mgmt_fw_major = cpu_to_le16(200);
        resp->mgmt_fw_minor = cpu_to_le16(100);
        resp->mgmt_fw_build = cpu_to_le16(50);
        resp->mgmt_fw_patch = cpu_to_le16(25);
        resp->netctrl_fw_major = cpu_to_le16(100);
        resp->netctrl_fw_minor = cpu_to_le16(50);
        resp->netctrl_fw_build = cpu_to_le16(25);
        resp->netctrl_fw_patch = cpu_to_le16(12);
        resp->roce_fw_major = cpu_to_le16(0);
        resp->roce_fw_minor = cpu_to_le16(0);
        resp->roce_fw_build = cpu_to_le16(0);
        resp->roce_fw_patch = cpu_to_le16(0);
        resp->max_ext_req_len = cpu_to_le16(0x10);
        resp->max_req_timeout = cpu_to_le16(5000);
        resp->unused_1[0] = resp->unused_1[1] = resp->unused_1[2] = 0;
        resp->valid = 1;
    } else if (req_type == HWRM_FUNC_DRV_RGTR) {
        struct hwrm_func_drv_rgtr_output *resp = (struct hwrm_func_drv_rgtr_output *)s->hwrm_mailbox;
        resp->error_code = cpu_to_le16(0);
        resp->req_type = cpu_to_le16(HWRM_FUNC_DRV_RGTR);
        resp->seq_id = cpu_to_le16(seq_id);
        resp->resp_len = cpu_to_le16(sizeof(struct hwrm_func_drv_rgtr_output));
        resp->flags = cpu_to_le32(0);
        resp->unused_0[0] = resp->unused_0[1] = resp->unused_0[2] = 0;
        resp->valid = 1;
    } else {
        /* Unknown command: return error */
        stw_le_p(&s->hwrm_mailbox[0], 0xffff);
        stw_le_p(&s->hwrm_mailbox[2], req_type);
        stw_le_p(&s->hwrm_mailbox[4], seq_id);
        stw_le_p(&s->hwrm_mailbox[6], 8);
    }

    s->hwrm_status = 1;

    /* Raise MSI-X interrupt if configured */
    if (msix_enabled(PCI_DEVICE(s))) {
        msix_notify(PCI_DEVICE(s), 0);
    }
}

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (msix_enabled(pdev)) {
        msix_notify(pdev, 0);
    } else if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_bar0_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* HWRM mailbox emulation */
    if (addr < 0x100) {
        /* Mailbox: read the response bytes */
        if (size == 4 && (addr & 3) == 0 && addr + 4 <= 0x100) {
            val = ldl_le_p(&s->hwrm_mailbox[addr]);
        } else if (size == 2 && (addr & 1) == 0 && addr + 2 <= 0x100) {
            val = lduw_le_p(&s->hwrm_mailbox[addr]);
        } else if (size == 1) {
            val = s->hwrm_mailbox[addr];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned or illegal read, addr=0x%" HWADDR_PRIx ", size=%d\n", __func__, addr, size);
        }
        return val;
    }
    if (addr == 0x100 && size == 4) {
        /* Status register: reads return current status and clear */
        uint32_t sts = s->hwrm_status;
        s->hwrm_status = 0; /* Clear on read */
        return sts;
    }

    /* Default: return 0 */
    return val;
}

static void pcibase_bar0_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* HWRM mailbox emulation: write request bytes */
    if (addr < 0x100) {
        if (size == 4 && (addr & 3) == 0 && addr + 4 <= 0x100) {
            stl_le_p(&s->hwrm_mailbox[addr], (uint32_t)val);
        } else if (size == 2 && (addr & 1) == 0 && addr + 2 <= 0x100) {
            stw_le_p(&s->hwrm_mailbox[addr], (uint16_t)val);
        } else if (size == 1) {
            s->hwrm_mailbox[addr] = (uint8_t)val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned or illegal write, addr=0x%" HWADDR_PRIx ", size=%d\n", __func__, addr, size);
        }
        return;
    }
    /* Doorbell register write: trigger command processing */
    if (addr >= 0x100 && addr < 0x200) {
        hwrm_process_command(s);
        return;
    }

    /* Default: ignore */
}

static uint64_t pcibase_bar2_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    G_GNUC_UNUSED PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Doorbell regs: return 0 */
    return val;
}

static void pcibase_bar2_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Doorbell: trigger command processing */
    hwrm_process_command(s);
}

static const MemoryRegionOps pcibase_bar0_mmio_ops = {
    .read = pcibase_bar0_mmio_read,
    .write = pcibase_bar0_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_bar2_mmio_ops = {
    .read = pcibase_bar2_mmio_read,
    .write = pcibase_bar2_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* No persistent registers to reset */
    memset(s->hwrm_mailbox, 0, sizeof(s->hwrm_mailbox));
    s->hwrm_status = 0;
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
        const MemoryRegionOps *ops = (bi->index == 0) ? &pcibase_bar0_mmio_ops : &pcibase_bar2_mmio_ops;
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        /* Not used */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x14e4 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1780 );
    pci_config_set_class(pci_conf, 0x020000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;  /* BAR0 (main registers) and BAR2 (doorbell) */
    s->bar_info[0] = (BARInfo) { .index = 0, .type = BAR_TYPE_MMIO, .size = 0x10000, .name = "bnge-bar0" };
    s->bar_info[1] = (BARInfo) { .index = 2, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bnge-doorbell" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X: allocate exclusive BAR4 to avoid overriding the doorbell BAR */
    if (msix_init_exclusive_bar(pdev, 8, 4, errp)) {
        return;
    }

    /* No DMA config or timers needed */

    /* Final state initialization */
    memset(s->hwrm_mailbox, 0, sizeof(s->hwrm_mailbox));
    s->hwrm_status = 0;
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
    .name = "bnge_driver_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(hwrm_mailbox, PCIBaseState, 256),
        VMSTATE_UINT32(hwrm_status, PCIBaseState),
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
