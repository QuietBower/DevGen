/*
 * QEMU PCI device model for shpchp (Standard Hot Plug Controller)
 * Generated from Linux driver shpchp_core.c
 */

#include "qemu/osdep.h"
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
#include "hw/pci/pci_bus.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "shpchp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID   0x1057
#define DEVICE_ID   0x01
#define CLASS_ID    0x0604  /* PCI_CLASS_BRIDGE_PCI_NORMAL */
#define PCI_DEVICE_ID_AMD_POGO_7458 0x7458
#define SERRNONFATALENABLE_MASK     0x00200000
#define PERRFLOODENABLE_MASK        0x00100000
#define PERRNONFATALENABLE_MASK     0x00040000
#define PCIX_MISCII_OFFSET          0x48
#define PERRFATALENABLE_MASK        0x00080000
#define SERRFATALENABLE_MASK        0x00400000
#define RSE_MASK                    0x40000000
#define PCIX_MISC_BRIDGE_ERRORS_OFFSET 0x80
#define PERR_OBSERVED_MASK          0x00000001
#define PCIX_MEM_BASE_LIMIT_OFFSET  0x1C

/* Placeholder constants for SHPC registers - these MUST be obtained from
 * the actual driver headers (e.g., shpchp.h, pci_regs.h) for correct hardware emulation. */

#define NUM_SLOTS 4  /* Example value; should match driver's expectation */

/* SHPC register MMIO offsets (placeholders) */
#define SERR_INTR_ENABLE_OFFSET 0x20  /* TODO: replace with correct offset */
#define SLOT_CONFIG_OFFSET      0x0C  /* TODO: replace with correct offset */
#define SLOT_REG_OFFSET(i)      (0x10 + (i)*4)  /* TODO: correct macro */

/* SHPC bit masks from driver source */
#define GLOBAL_INTR_MASK	(1 << 0)
#define GLOBAL_SERR_MASK	(1 << 1)
#define COMMAND_INTR_MASK	(1 << 2)
#define ARBITER_SERR_MASK	(1 << 3)
#define SERR_INTR_RSVDZ_MASK	0xfffc0000
#define PRSNT_CHANGE_INTR_MASK	(1 << 24)
#define ISO_PFAULT_INTR_MASK	(1 << 25)
#define BUTTON_PRESS_INTR_MASK	(1 << 26)
#define MRL_CHANGE_INTR_MASK	(1 << 27)
#define CON_PFAULT_INTR_MASK	(1 << 28)
#define MRL_CHANGE_SERR_MASK	(1 << 29)
#define CON_PFAULT_SERR_MASK	(1 << 30)
#define SLOT_REG_RSVDZ_MASK	((1 << 15) | (7 << 21))
#define SLOT_STATE_MASK		(3 << 0)
#define SLOT_STATE_SHIFT	(0)
#define ATN_LED_STATE_MASK	(3 << 4)
#define ATN_LED_STATE_SHIFT	(4)
#define MRL_SENSOR		(1 << 8)
#define PRSNT_MASK		(3 << 10)
#define PRSNT_SHIFT		(10)
#define SET_ATTN_OFF		0x30
#define SET_ATTN_ON		0x10
#define SET_ATTN_BLINK		0x20
#define SLOT_NUM		0x0000001F
#define FIRST_DEV_NUM		0x00001F00
#define PSN(ctrl)		(((ctrl)->slot_cap & PCI_EXP_SLTCAP_PSN) >> 19)
#define UPDOWN			0x20000000

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Shadow config registers for W1C emulation */
    uint32_t pcix_misc2;
    uint32_t pcix_misc_bridge_errors;
    uint32_t pcix_mem_base_limit;

    /* SHPC state */
    MemoryRegion shpc_mmio;
    uint32_t shpc_cap_offset;
    uint32_t shpc_index;
    uint32_t shpc_regs[256];  /* indirect register file (index = MMIO offset / 4) */
};

/* Internal helper for status-triggered signaling - no interrupt logic required based on driver source */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Not used */
}

/* Custom config read handler to emulate W1C behavior and SHPC indirect access */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;

    /* Default read for all offsets except those we shadow */
    val = pci_default_read_config(pdev, addr, len);

    /* SHPC indirect access: DWORD SELECT (cap+4) and DWORD DATA (cap+8) */
    if (s->shpc_cap_offset) {
        if (addr >= s->shpc_cap_offset + 4 && addr < s->shpc_cap_offset + 8) {
            return s->shpc_index;
        }
        if (addr >= s->shpc_cap_offset + 8 && addr < s->shpc_cap_offset + 12) {
            if (s->shpc_index < 256) {
                return s->shpc_regs[s->shpc_index];
            }
            return 0;
        }
    }

    /* Retrieve our shadow copies when the driver reads the entire dword */
    if (addr == PCIX_MISCII_OFFSET && len == 4) {
        val = s->pcix_misc2;
    } else if (addr == PCIX_MISC_BRIDGE_ERRORS_OFFSET && len == 4) {
        val = s->pcix_misc_bridge_errors;
    } else if (addr == PCIX_MEM_BASE_LIMIT_OFFSET && len == 4) {
        val = s->pcix_mem_base_limit;
    }

    return val;
}

/* Custom config write handler to emulate W1C behavior and SHPC indirect access */
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* SHPC indirect access: DWORD SELECT (cap+4) and DWORD DATA (cap+8) */
    if (s->shpc_cap_offset) {
        if (addr >= s->shpc_cap_offset + 4 && addr < s->shpc_cap_offset + 8) {
            /* DWORD SELECT: store lower byte as index */
            s->shpc_index = val & 0xFF;
            return;
        }
        if (addr >= s->shpc_cap_offset + 8 && addr < s->shpc_cap_offset + 12) {
            /* DWORD DATA: write to selected register (dword access only) */
            if (len == 4 && s->shpc_index < 256) {
                s->shpc_regs[s->shpc_index] = val;
            }
            return;
        }
    }

    /* Handle W1C registers */
    if (addr == PCIX_MISC_BRIDGE_ERRORS_OFFSET && len == 4) {
        /* W1C: bits written with 1 are cleared */
        s->pcix_misc_bridge_errors &= ~(val & PERR_OBSERVED_MASK);
        /* Write-through other bits (not used by driver) */
        pci_default_write_config(pdev, addr, s->pcix_misc_bridge_errors, len);
        return;
    }
    if (addr == PCIX_MEM_BASE_LIMIT_OFFSET && len == 4) {
        /* W1C: RSE bit cleared on write-1 */
        s->pcix_mem_base_limit &= ~(val & RSE_MASK);
        pci_default_write_config(pdev, addr, s->pcix_mem_base_limit, len);
        return;
    }
    if (addr == PCIX_MISCII_OFFSET && len == 4) {
        /* Direct write, no W1C; update shadow */
        s->pcix_misc2 = val;
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    /* Default behavior for all other config writes */
    pci_default_write_config(pdev, addr, val, len);
}

/* SHPC MMIO read: maps offset directly to register file index = offset/4 */
static uint64_t shpc_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint32_t index = addr >> 2;
    if (index >= ARRAY_SIZE(s->shpc_regs)) {
        return 0;
    }
    uint32_t val = s->shpc_regs[index];
    /* Zero out reserved bits on reads to match hardware */
    if (addr == SERR_INTR_ENABLE_OFFSET) {
        val &= ~SERR_INTR_RSVDZ_MASK;
    } else if (addr >= SLOT_REG_OFFSET(0) && addr <= SLOT_REG_OFFSET(NUM_SLOTS - 1)) {
        val &= ~SLOT_REG_RSVDZ_MASK;
    }
    return val;
}

/* SHPC MMIO write: maps offset directly to register file index = offset/4 */
static void shpc_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint32_t index = addr >> 2;
    if (index >= ARRAY_SIZE(s->shpc_regs)) {
        return;
    }
    uint32_t masked_val = val;
    /* Apply reserved masks to emulate hardware writability */
    if (addr == SERR_INTR_ENABLE_OFFSET) {
        masked_val &= ~SERR_INTR_RSVDZ_MASK;
    } else if (addr >= SLOT_REG_OFFSET(0) && addr <= SLOT_REG_OFFSET(NUM_SLOTS - 1)) {
        masked_val &= ~SLOT_REG_RSVDZ_MASK;
    }
    s->shpc_regs[index] = masked_val;
}

static const MemoryRegionOps shpc_mmio_ops = {
    .read = shpc_mmio_read,
    .write = shpc_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize shadow config registers to plausible reset values */
    s->pcix_misc2 = 0x00000000;
    s->pcix_misc_bridge_errors = 0x00000000;
    s->pcix_mem_base_limit = 0x00000000;

    /* Reset SHPC state */
    memset(s->shpc_regs, 0, sizeof(s->shpc_regs));
    s->shpc_index = 0;
    /* Initialize slot configuration register: set num_slots field */
    s->shpc_regs[3] = (NUM_SLOTS & SLOT_NUM);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int cap_off;
    Error *msi_err = NULL;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Init MSI (driver may fall back to INTx if MSI fails) */
    if (msi_init(pdev, 0, 1, true, false, &msi_err) < 0) {
        /* MSI not available; clear error and continue */
        error_free(msi_err);
        msi_err = NULL;
    }

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Add SHPC capability required by driver */
    cap_off = pci_add_capability(pdev, PCI_CAP_ID_SHPC, 0, 0x0E, errp);
    if (cap_off > 0) {
        uint8_t *cap_data = pci_conf + cap_off;
        s->shpc_cap_offset = cap_off;
        /* Initialize SHPC capability fields */
        pci_set_word(cap_data + 2, 0x0000); /* Base Address = 0 (BAR0 start) */
        pci_set_word(cap_data + 4, 0x0000); /* DWORD SELECT = 0 */
        pci_set_long(cap_data + 8, 0x00000000); /* DWORD DATA = 0 */
        memset(cap_data + 12, 0, 2); /* Reserved */
    } else {
        error_setg(errp, "SHPC capability not added");
        return;
    }

    /* Initialize SHPC register file (reset will do this, but we set important values now) */
    memset(s->shpc_regs, 0, sizeof(s->shpc_regs));
    s->shpc_regs[3] = (NUM_SLOTS & SLOT_NUM); /* slot_config */

    /* Register BAR0 for SHPC MMIO region (size must be at least 0x24 + 0x4*NUM_SLOTS) */
    memory_region_init_io(&s->shpc_mmio, OBJECT(s), &shpc_mmio_ops, s,
                          "shpc-mmio", 0x1000); /* 4KB, enough for many slots */
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->shpc_mmio);

    /* Explicitly set config read/write handlers */
    PCIDeviceClass *k = PCI_DEVICE_GET_CLASS(pdev);
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "shpchp_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(pcix_misc2, PCIBaseState),
        VMSTATE_UINT32(pcix_misc_bridge_errors, PCIBaseState),
        VMSTATE_UINT32(pcix_mem_base_limit, PCIBaseState),
        VMSTATE_UINT32(shpc_index, PCIBaseState),
        VMSTATE_UINT32_ARRAY(shpc_regs, PCIBaseState, 256),
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
