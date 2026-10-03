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


#define TYPE_PCIBASE_DEVICE "intel_mid_pwr_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_PENWELL 0x0828
#define PCI_DEVICE_ID_TANGIER 0x11a1

#define PM_STS			0x00
#define PM_CMD			0x04
#define PM_ICS			0x08
#define PM_WKC(x)		(0x10 + (x) * 4)
#define PM_WKS(x)		(0x18 + (x) * 4)
#define PM_SSC(x)		(0x20 + (x) * 4)
#define PM_SSS(x)		(0x30 + (x) * 4)

#define PM_STS_BUSY		(1 << 8)
#define PM_CMD_CMD(x)		((x) << 0)
#define PM_CMD_IOC		(1 << 8)
#define PM_CMD_CM_NOP		(0 << 9)
#define PM_CMD_CM_IMMEDIATE	(1 << 9)
#define PM_CMD_CM_DELAY		(2 << 9)
#define PM_CMD_CM_TRIGGER	(3 << 9)
#define PM_CMD_SYS_STATE_S5	(5 << 16)
#define PM_CMD_CFG_TRIGGER_NC	(3 << 19)
#define TRIGGER_NC_MSG_2	(2 << 22)
#define CMD_SET_CFG		0x01

#define PM_ICS_INT_STATUS(x)	((x) & 0xff)
#define PM_ICS_IE		(1 << 8)
#define PM_ICS_IP		(1 << 9)
#define PM_ICS_SW_INT_STS	(1 << 10)

#define INT_INVALID		0
#define INT_CMD_COMPLETE	1
#define INT_CMD_ERR		2
#define INT_WAKE_EVENT		3
#define INT_LSS_POWER_ERR	4
#define INT_S0iX_MSG_ERR	5
#define INT_NO_C6		6
#define INT_TRIGGER_ERR		7
#define INT_INACTIVITY		8

#define LSS_MAX_SHARED_DEVS	4
#define LSS_MAX_DEVS		64
#define LSS_WS_BITS		1
#define LSS_PWS_BITS		2

#define INTEL_MID_PWR_LSS_OFFSET	4
#define INTEL_MID_PWR_LSS_TYPE		(1 << 7)

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
    uint32_t pm_ics;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t pm_sts;
    uint32_t pm_cmd;
    uint32_t pm_wkc[4];
    uint32_t pm_wks[4];
    uint32_t pm_ssc[4];
    uint32_t pm_sss[4];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->pm_ics & PM_ICS_IP) && (s->pm_ics & PM_ICS_IE);
    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PM_STS:
        val = s->pm_sts & ~PM_STS_BUSY;
        break;
    case PM_CMD:
        val = s->pm_cmd;
        break;
    case PM_ICS:
        val = s->pm_ics;
        break;
    case 0x10:
        val = s->pm_wkc[0];
        break;
    case 0x14:
        val = s->pm_wkc[1];
        break;
    case 0x20:
        val = s->pm_ssc[0];
        break;
    case 0x24:
        val = s->pm_ssc[1];
        break;
    case 0x28:
        val = s->pm_ssc[2];
        break;
    case 0x2C:
        val = s->pm_ssc[3];
        break;
    case 0x30:
        val = s->pm_sss[0];
        break;
    case 0x34:
        val = s->pm_sss[1];
        break;
    case 0x38:
        val = s->pm_sss[2];
        break;
    case 0x3C:
        val = s->pm_sss[3];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PM_STS:
        /* Read-only or W1C, driver doesn't write */
        break;
    case PM_CMD:
        s->pm_cmd = val;
        if ((val & 0xFF) == CMD_SET_CFG) {
            for (int i = 0; i < 4; i++) {
                s->pm_sss[i] = s->pm_ssc[i];
            }
        }
        break;
    case PM_ICS:
        if (val & PM_ICS_IP) {
            s->pm_ics &= ~PM_ICS_IP;
        }
        if (val & PM_ICS_IE) {
            s->pm_ics |= PM_ICS_IE;
        } else {
            s->pm_ics &= ~PM_ICS_IE;
        }
        pcibase_update_irq(s);
        break;
    case 0x10:
        s->pm_wkc[0] = val;
        break;
    case 0x14:
        s->pm_wkc[1] = val;
        break;
    case 0x20:
        s->pm_ssc[0] = val;
        break;
    case 0x24:
        s->pm_ssc[1] = val;
        break;
    case 0x28:
        s->pm_ssc[2] = val;
        break;
    case 0x2C:
        s->pm_ssc[3] = val;
        break;
    case 0x30:
        s->pm_sss[0] = val;
        break;
    case 0x34:
        s->pm_sss[1] = val;
        break;
    case 0x38:
        s->pm_sss[2] = val;
        break;
    case 0x3C:
        s->pm_sss[3] = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    s->pm_sts = 0;
    s->pm_cmd = 0;
    s->pm_ics = 0;
    for (int i = 0; i < 4; i++) {
        s->pm_wkc[i] = 0;
        s->pm_wks[i] = 0;
        s->pm_ssc[i] = 0;
        s->pm_sss[i] = 0;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PENWELL );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "intel_mid_pwr";
    
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
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "intel_mid_pwr_pci",
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
