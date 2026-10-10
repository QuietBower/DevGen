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
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "intel_mid_pwr_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL  0x8086  /* Added for clarity */
#define PCI_DEVICE_ID_PENWELL  0x0828
#define PCI_DEVICE_ID_TANGIER  0x11a1

#define PM_STS          0x00
#define PM_CMD          0x04
#define PM_ICS          0x08
#define PM_WKC(x)       (0x10 + (x) * 4)
#define PM_WKS(x)       (0x18 + (x) * 4)
#define PM_SSC(x)       (0x20 + (x) * 4)
#define PM_SSS(x)       (0x30 + (x) * 4)

#define PM_STS_BUSY             (1 << 8)
#define PM_CMD_CMD(x)           ((x) << 0)
#define PM_CMD_IOC              (1 << 8)
#define PM_CMD_CM_NOP           (0 << 9)
#define PM_CMD_CM_IMMEDIATE     (1 << 9)
#define PM_CMD_CM_DELAY         (2 << 9)
#define PM_CMD_CM_TRIGGER       (3 << 9)
#define PM_CMD_SYS_STATE_S5     (5 << 16)
#define PM_CMD_CFG_TRIGGER_NC   (3 << 19)
#define TRIGGER_NC_MSG_2        (2 << 22)
#define CMD_SET_CFG             0x01

#define PM_ICS_INT_STATUS(x)    ((x) & 0xff)
#define PM_ICS_IE               (1 << 8)
#define PM_ICS_IP               (1 << 9)
#define PM_ICS_SW_INT_STS       (1 << 10)

#define INT_INVALID         0
#define INT_CMD_COMPLETE    1
#define INT_CMD_ERR         2
#define INT_WAKE_EVENT      3
#define INT_LSS_POWER_ERR   4
#define INT_S0iX_MSG_ERR    5
#define INT_NO_C6           6
#define INT_TRIGGER_ERR     7
#define INT_INACTIVITY      8

#define LSS_MAX_SHARED_DEVS 4
#define LSS_MAX_DEVS        64
#define LSS_WS_BITS         1
#define LSS_PWS_BITS        2

#define BAR0_SIZE 0x1000

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

    /* Hardware Register Shadows */
    uint32_t pm_sts;        /* PM_STS at 0x00 */
    uint32_t pm_ics;        /* PM_ICS at 0x08 */
    uint32_t pm_ssc[4];     /* PM_SSC at 0x20-0x2C (write) */
    uint32_t pm_sss[4];     /* PM_SSS at 0x30-0x3C (read) */

    /* Operational status flags (from template, not used) */
    bool available;
    uint32_t power_state;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* No interrupt generation needed for probe success; placeholder */
}

/* MMIO/PIO Handlers implemented during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Driver uses readl (4-byte reads); handle accordingly */
    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, 
                      "%s: invalid read size %u at addr 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return 0xffffffff;
    }

    switch (addr) {
    case PM_STS:
        val = s->pm_sts;
        break;
    case PM_ICS:
        val = s->pm_ics;
        break;
    case PM_SSS(0):
    case PM_SSS(1):
    case PM_SSS(2):
    case PM_SSS(3):
        val = s->pm_sss[(addr - PM_SSS(0)) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, 
                      "%s: unknown read at addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write size %u at addr 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return;
    }

    switch (addr) {
    case PM_CMD:
        /* Command register: driver writes command and then waits for BUSY=0.
         * Since we keep BUSY always clear, no action needed.
         */
        break;
    case PM_ICS:
        /* Handle interrupt control/status register.
         * - IE bit: writable (bit 8)
         * - IP bit: write-1-to-clear (bit 9) [the driver writes (ics | PM_ICS_IP) to clear]
         * - Other bits: might be read-only? We ignore writes to them.
         */
        if (val & PM_ICS_IP) {
            s->pm_ics &= ~PM_ICS_IP; /* clear IP if written with 1 */
        }
        /* Update IE bit */
        s->pm_ics = (s->pm_ics & ~PM_ICS_IE) | (val & PM_ICS_IE);
        /* Note: driver writes ~PM_ICS_IE to disable, which sets all bits except IE,
         * so IE becomes 0, and other bits become 1. Since we ignore other bits,
         * that's fine. */
        break;
    case PM_WKC(0):
    case PM_WKC(1):
        /* Wake configuration writes; no internal state needed */
        break;
    case PM_SSC(0):
    case PM_SSC(1):
    case PM_SSC(2):
    case PM_SSC(3):
        {
            int reg = (addr - PM_SSC(0)) / 4;
            s->pm_ssc[reg] = (uint32_t)val;
            /* Writing SSC updates the corresponding SSS register immediately,
             * which matches driver expectation (after command, read SSS sees new state).
             */
            s->pm_sss[reg] = (uint32_t)val;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unknown write at addr 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n",
                      __func__, addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* No PIO implemented according to driver */
    qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected PIO read\n", __func__);
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO implemented */
    qemu_log_mask(LOG_GUEST_ERROR, "%s: unexpected PIO write\n", __func__);
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

    /* Reset register shadows to initial power-on state */
    s->pm_sts = 0;              /* Not busy */
    s->pm_ics = 0;              /* IE=0, IP=0, int status=0 */
    memset(s->pm_ssc, 0, sizeof(s->pm_ssc));
    memset(s->pm_sss, 0, sizeof(s->pm_sss));
    /* optional: s->available = false; s->power_state = 0; */
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
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type  = BAR_TYPE_MMIO,
        .size  = BAR0_SIZE,    /* 4KB to cover all registers */
        .name  = "intel_mid_pwr"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No special cleanup needed */
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
