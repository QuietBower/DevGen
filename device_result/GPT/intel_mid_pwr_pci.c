/*
 * QEMU PCI device model for intel_mid_pwr (linux-6.18/arch/x86/platform/intel-mid/pwr.c)
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

/* Removed driver-specific include that is not available in QEMU build environment */
/* #include "pci_ids.h" */

#define TYPE_PCIBASE_DEVICE "intel_mid_pwr_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Provide minimal PCI ID definitions to satisfy compilation, matching Linux IDs */
#ifndef PCI_VENDOR_ID_INTEL
#define PCI_VENDOR_ID_INTEL 0x8086
#endif

#ifndef PCI_CLASS_OTHERS
#define PCI_CLASS_OTHERS 0xff
#endif

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PM_STS                 0x00
#define PM_CMD                 0x04
#define PM_ICS                 0x08
#define PM_WKC(x)              (0x10 + (x) * 4)
#define PM_WKS(x)              (0x18 + (x) * 4)
#define PM_SSC(x)              (0x20 + (x) * 4)
#define PM_SSS(x)              (0x30 + (x) * 4)

#define PM_STS_BUSY            (1 << 8)

#define PM_CMD_CMD(x)          ((x) << 0)
#define PM_CMD_IOC             (1 << 8)
#define PM_CMD_CM_NOP          (0 << 9)
#define PM_CMD_CM_IMMEDIATE    (1 << 9)
#define PM_CMD_CM_DELAY        (2 << 9)
#define PM_CMD_CM_TRIGGER      (3 << 9)
#define PM_CMD_SYS_STATE_S5    (5 << 16)
#define PM_CMD_CFG_TRIGGER_NC  (3 << 19)
#define TRIGGER_NC_MSG_2       (2 << 22)
#define CMD_SET_CFG            0x01

#define PM_ICS_INT_STATUS(x)   ((x) & 0xff)
#define PM_ICS_IE              (1 << 8)
#define PM_ICS_IP              (1 << 9)
#define PM_ICS_SW_INT_STS      (1 << 10)

#define INT_INVALID            0
#define INT_CMD_COMPLETE       1
#define INT_CMD_ERR            2
#define INT_WAKE_EVENT         3
#define INT_LSS_POWER_ERR      4
#define INT_S0iX_MSG_ERR       5
#define INT_NO_C6              6
#define INT_TRIGGER_ERR        7
#define INT_INACTIVITY         8

#define LSS_MAX_SHARED_DEVS    4
#define LSS_MAX_DEVS           64
#define LSS_WS_BITS            1
#define LSS_PWS_BITS           2

#define PCI_DEVICE_ID_PENWELL  0x0828
#define PCI_DEVICE_ID_TANGIER  0x11a1

/* Use first pci_device_id entry: INTEL, PCI_DEVICE_ID_PENWELL */
#define PCIBASE_VENDOR_ID      PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID      PCI_DEVICE_ID_PENWELL
#define PCIBASE_CLASS_ID       PCI_CLASS_OTHERS

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
    uint32_t pm_sts;
    uint32_t pm_cmd;
    uint32_t pm_ics;
    uint32_t pm_wkc[2];
    uint32_t pm_wks[2];
    uint32_t pm_ssc[4];
    uint32_t pm_sss[4];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The driver never enables interrupts (it writes ~PM_ICS_IE at probe),
     * and the handler only checks that PM_ICS_IP is set and then clears it.
     * For probe/bind success we do not need to generate interrupts
     * autonomously. However, honor the IE bit if it ever gets set by
     * raising or lowering the legacy INTx line based on IP.
     */
    if (!(s->pm_ics & PM_ICS_IE)) {
        pci_set_irq(pdev, 0);
        return;
    }

    if (s->pm_ics & PM_ICS_IP) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The intel_mid_pwr driver does not perform device-initiated DMA.
     * No DMA behavior is implemented.
     */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver uses readl/writel, so 32-bit accesses are expected. */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case PM_STS:
        /* Busy bit is used by mid_pwr_wait(). We keep it clear so
         * mid_pwr_wait() returns immediately. */
        val = s->pm_sts;
        break;
    case PM_CMD:
        val = s->pm_cmd;
        break;
    case PM_ICS:
        val = s->pm_ics;
        break;
    case PM_WKC(0):
        val = s->pm_wkc[0];
        break;
    case PM_WKC(1):
        val = s->pm_wkc[1];
        break;
    case PM_WKS(0):
        val = s->pm_wks[0];
        break;
    case PM_WKS(1):
        val = s->pm_wks[1];
        break;
    case PM_SSC(0):
        val = s->pm_ssc[0];
        break;
    case PM_SSC(1):
        val = s->pm_ssc[1];
        break;
    case PM_SSC(2):
        val = s->pm_ssc[2];
        break;
    case PM_SSC(3):
        val = s->pm_ssc[3];
        break;
    case PM_SSS(0):
        val = s->pm_sss[0];
        break;
    case PM_SSS(1):
        val = s->pm_sss[1];
        break;
    case PM_SSS(2):
        val = s->pm_sss[2];
        break;
    case PM_SSS(3):
        val = s->pm_sss[3];
        break;
    default:
        /* Unused/undefined registers return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case PM_STS:
        /* Driver never writes PM_STS in provided code. Just store value. */
        s->pm_sts = (uint32_t)val;
        break;
    case PM_CMD:
        /* Store command value. For commands that the driver waits on,
         * we ensure PM_STS_BUSY remains clear so mid_pwr_wait() succeeds. */
        s->pm_cmd = (uint32_t)val;
        /* Clear BUSY bit to indicate command completion. */
        s->pm_sts &= ~PM_STS_BUSY;
        break;
    case PM_ICS: {
        uint32_t old = s->pm_ics;
        uint32_t newv = (uint32_t)val;

        /* Implement W1C for the IP bit as used by the IRQ handler:
         *   ics = readl(PM_ICS);
         *   writel(ics | PM_ICS_IP, PM_ICS);
         * The usual semantics are that writing 1 to IP clears it. */
        /* Preserve IE and all bits by default. */
        s->pm_ics = newv;

        if (newv & PM_ICS_IP) {
            /* Clear IP bit (write-1-to-clear behavior). */
            s->pm_ics = newv & ~PM_ICS_IP;
        }

        /* If IP changed, update interrupt line. */
        if ((old ^ s->pm_ics) & PM_ICS_IP) {
            pcibase_update_irq(s);
        }
        break;
    }
    case PM_WKC(0):
        s->pm_wkc[0] = (uint32_t)val;
        break;
    case PM_WKC(1):
        s->pm_wkc[1] = (uint32_t)val;
        break;
    case PM_WKS(0):
        s->pm_wks[0] = (uint32_t)val;
        break;
    case PM_WKS(1):
        s->pm_wks[1] = (uint32_t)val;
        break;
    case PM_SSC(0):
        s->pm_ssc[0] = (uint32_t)val;
        break;
    case PM_SSC(1):
        s->pm_ssc[1] = (uint32_t)val;
        break;
    case PM_SSC(2):
        s->pm_ssc[2] = (uint32_t)val;
        break;
    case PM_SSC(3):
        s->pm_ssc[3] = (uint32_t)val;
        break;
    case PM_SSS(0):
        s->pm_sss[0] = (uint32_t)val;
        break;
    case PM_SSS(1):
        s->pm_sss[1] = (uint32_t)val;
        break;
    case PM_SSS(2):
        s->pm_sss[2] = (uint32_t)val;
        break;
    case PM_SSS(3):
        s->pm_sss[3] = (uint32_t)val;
        break;
    default:
        /* Ignore writes to undefined offsets. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    /* Logic for Port I/O (Legacy support) - not used by driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* Logic for Port I/O (Legacy support) - not used by driver */
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

    /* Revert registers to power-on defaults */
    s->pm_sts = 0;
    s->pm_cmd = 0;
    /* Disable interrupts by default, matching driver probe sequence
     * which calls mid_pwr_interrupt_disable(). */
    s->pm_ics = 0;

    s->pm_wkc[0] = 0;
    s->pm_wkc[1] = 0;
    s->pm_wks[0] = 0;
    s->pm_wks[1] = 0;

    /* All devices start in PCI_D3hot according to mid_set_initial_state() */
    s->pm_ssc[0] = 0;
    s->pm_ssc[1] = 0;
    s->pm_ssc[2] = 0;
    s->pm_ssc[3] = 0;

    s->pm_sss[0] = 0;
    s->pm_sss[1] = 0;
    s->pm_sss[2] = 0;
    s->pm_sss[3] = 0;
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
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* The driver uses BAR 0 MMIO via pcim_iomap_region(pdev, 0, ...).
     * Provide a reasonably sized MMIO region that covers all defined regs. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* 4 KiB is sufficient for our offsets */
    s->bar_info[0].name = "intel_mid_pwr-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X usage is required by the driver; leave disabled. */

    /* Initialize internal state to reset defaults */
    pcibase_reset(DEVICE(pdev));
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

    /* No additional dynamic resources to free. */
    (void)s;
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
