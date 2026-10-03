/*
 * QEMU PCI device model for R-Car Gen2 PCI host bridge
 * Based on Linux driver: pci-rcar-gen2.c
 * Phase 2: Functional Behavior Implementation
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
/* None needed */

#define TYPE_PCIBASE_DEVICE "pci_rcar_gen2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/*
 * Register Layout and Hardware Identifiers extracted from driver source
 * Original defines are relative to RCAR_AHBPCI_PCICOM_OFFSET (0x800).
 */
#define RCAR_AHBPCI_PCICOM_OFFSET     0x800
#define RCAR_PCIAHB_WIN1_CTR_REG      (RCAR_AHBPCI_PCICOM_OFFSET + 0x00)
#define RCAR_PCIAHB_WIN2_CTR_REG      (RCAR_AHBPCI_PCICOM_OFFSET + 0x04)
#define RCAR_PCIAHB_PREFETCH0          0x0
#define RCAR_PCIAHB_PREFETCH4          0x1
#define RCAR_PCIAHB_PREFETCH8          0x2
#define RCAR_PCIAHB_PREFETCH16         0x3
#define RCAR_AHBPCI_WIN1_CTR_REG      (RCAR_AHBPCI_PCICOM_OFFSET + 0x10)
#define RCAR_AHBPCI_WIN2_CTR_REG      (RCAR_AHBPCI_PCICOM_OFFSET + 0x14)
#define RCAR_AHBPCI_WIN_CTR_MEM       (3 << 1)
#define RCAR_AHBPCI_WIN_CTR_CFG       (5 << 1)
#define RCAR_AHBPCI_WIN1_HOST         (1 << 30)
#define RCAR_AHBPCI_WIN1_DEVICE       (1 << 31)
#define RCAR_PCI_INT_ENABLE_REG       (RCAR_AHBPCI_PCICOM_OFFSET + 0x20)
#define RCAR_PCI_INT_STATUS_REG       (RCAR_AHBPCI_PCICOM_OFFSET + 0x24)
#define RCAR_PCI_INT_SIGTABORT        (1 << 0)
#define RCAR_PCI_INT_SIGRETABORT      (1 << 1)
#define RCAR_PCI_INT_REMABORT         (1 << 2)
#define RCAR_PCI_INT_PERR             (1 << 3)
#define RCAR_PCI_INT_SIGSERR          (1 << 4)
#define RCAR_PCI_INT_RESERR           (1 << 5)
#define RCAR_PCI_INT_WIN1ERR          (1 << 12)
#define RCAR_PCI_INT_WIN2ERR          (1 << 13)
#define RCAR_PCI_INT_A                (1 << 16)
#define RCAR_PCI_INT_B                (1 << 17)
#define RCAR_PCI_INT_PME              (1 << 19)
#define RCAR_PCI_INT_ALLERRORS (RCAR_PCI_INT_SIGTABORT       | \
                               RCAR_PCI_INT_SIGRETABORT      | \
                               RCAR_PCI_INT_REMABORT         | \
                               RCAR_PCI_INT_PERR             | \
                               RCAR_PCI_INT_SIGSERR          | \
                               RCAR_PCI_INT_RESERR           | \
                               RCAR_PCI_INT_WIN1ERR          | \
                               RCAR_PCI_INT_WIN2ERR)
#define RCAR_AHB_BUS_CTR_REG          (RCAR_AHBPCI_PCICOM_OFFSET + 0x30)
#define RCAR_AHB_BUS_MMODE_HTRANS     (1 << 0)
#define RCAR_AHB_BUS_MMODE_BYTE_BURST (1 << 1)
#define RCAR_AHB_BUS_MMODE_WR_INCR    (1 << 2)
#define RCAR_AHB_BUS_MMODE_HBUS_REQ   (1 << 7)
#define RCAR_AHB_BUS_SMODE_READYCTR   (1 << 17)
#define RCAR_AHB_BUS_MODE             (RCAR_AHB_BUS_MMODE_HTRANS | \
                                       RCAR_AHB_BUS_MMODE_BYTE_BURST | \
                                       RCAR_AHB_BUS_MMODE_WR_INCR | \
                                       RCAR_AHB_BUS_MMODE_HBUS_REQ | \
                                       RCAR_AHB_BUS_SMODE_READYCTR)
#define RCAR_USBCTR_REG               (RCAR_AHBPCI_PCICOM_OFFSET + 0x34)
#define RCAR_USBCTR_USBH_RST          (1 << 0)
#define RCAR_USBCTR_PCICLK_MASK       (1 << 1)
#define RCAR_USBCTR_PLL_RST           (1 << 2)
#define RCAR_USBCTR_DIRPD             (1 << 8)
#define RCAR_USBCTR_PCIAHB_WIN2_EN    (1 << 9)
#define RCAR_USBCTR_PCIAHB_WIN1_256M  (0 << 10)
#define RCAR_USBCTR_PCIAHB_WIN1_512M  (1 << 10)
#define RCAR_USBCTR_PCIAHB_WIN1_1G    (2 << 10)
#define RCAR_USBCTR_PCIAHB_WIN1_2G    (3 << 10)
#define RCAR_USBCTR_PCIAHB_WIN1_MASK  (3 << 10)
#define RCAR_PCI_ARBITER_CTR_REG      (RCAR_AHBPCI_PCICOM_OFFSET + 0x40)
#define RCAR_PCI_ARBITER_PCIREQ0      (1 << 0)
#define RCAR_PCI_ARBITER_PCIREQ1      (1 << 1)
#define RCAR_PCI_ARBITER_PCIBP_MODE   (1 << 12)
#define RCAR_PCI_UNIT_REV_REG         (RCAR_AHBPCI_PCICOM_OFFSET + 0x48)

/* PCI Identification – values extracted from supplementary driver source */
#define VENDOR_ID 0x1057
#define DEVICE_ID 0x01
#define CLASS_ID  0x0600   /* PCI_CLASS_BRIDGE_HOST */
#define BAR0_SIZE 0x1000   /* covers 0x000-0x848 register range */

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t pcicahb_win1_ctr;
        uint32_t pcicahb_win2_ctr;
        uint32_t ahbpci_win1_ctr;
        uint32_t ahbpci_win2_ctr;
        uint32_t int_enable;
        uint32_t int_status;
        uint32_t ahb_bus_ctr;
        uint32_t usb_ctr;
        uint32_t arbiter_ctr;
        uint32_t unit_rev;
        uint32_t pci_command_status; /* shadow of PCI command/status (0x04) */
        uint32_t bar0_shadow;        /* shadow of BAR0 (0x10) */
        uint32_t bar1_shadow;        /* shadow of BAR1 (0x14) */
    } regs;
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->regs.int_status & s->regs.int_enable) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t val = 0;

    if (size != 4) {
        /* driver uses 32-bit accesses */
        return 0;
    }

    switch (addr) {
    case 0x00: /* Vendor/Device ID */
        val = pci_get_long(pdev->config + PCI_VENDOR_ID);
        break;
    case 0x04: /* Command/Status */
        val = s->regs.pci_command_status;
        break;
    case 0x08: /* Revision ID / Class Code */
        val = pci_get_long(pdev->config + PCI_REVISION_ID);
        break;
    case 0x10: /* BAR0 */
        val = s->regs.bar0_shadow;
        break;
    case 0x14: /* BAR1 */
        val = s->regs.bar1_shadow;
        break;
    case 0x800: /* RCAR_PCIAHB_WIN1_CTR_REG */
        val = s->regs.pcicahb_win1_ctr;
        break;
    case 0x804: /* RCAR_PCIAHB_WIN2_CTR_REG */
        val = s->regs.pcicahb_win2_ctr;
        break;
    case 0x810: /* RCAR_AHBPCI_WIN1_CTR_REG */
        val = s->regs.ahbpci_win1_ctr;
        break;
    case 0x814: /* RCAR_AHBPCI_WIN2_CTR_REG */
        val = s->regs.ahbpci_win2_ctr;
        break;
    case 0x820: /* RCAR_PCI_INT_ENABLE_REG */
        val = s->regs.int_enable;
        break;
    case 0x824: /* RCAR_PCI_INT_STATUS_REG */
        val = s->regs.int_status;
        break;
    case 0x830: /* RCAR_AHB_BUS_CTR_REG */
        val = s->regs.ahb_bus_ctr;
        break;
    case 0x834: /* RCAR_USBCTR_REG */
        val = s->regs.usb_ctr;
        break;
    case 0x840: /* RCAR_PCI_ARBITER_CTR_REG */
        val = s->regs.arbiter_ctr;
        break;
    case 0x848: /* RCAR_PCI_UNIT_REV_REG */
        val = s->regs.unit_rev;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    if (size != 4) {
        return;
    }

    switch (addr) {
    case 0x04: /* Command/Status */
        /* Update shadow and also write lower 16 bits (command) to pci config */
        s->regs.pci_command_status = val;
        pci_set_word(pdev->config + PCI_COMMAND, val & 0xFFFF);
        break;
    case 0x10: /* BAR0 */
        s->regs.bar0_shadow = val;
        break;
    case 0x14: /* BAR1 */
        s->regs.bar1_shadow = val;
        break;
    case 0x800: /* RCAR_PCIAHB_WIN1_CTR_REG */
        s->regs.pcicahb_win1_ctr = val;
        break;
    case 0x804: /* RCAR_PCIAHB_WIN2_CTR_REG */
        s->regs.pcicahb_win2_ctr = val;
        break;
    case 0x810: /* RCAR_AHBPCI_WIN1_CTR_REG */
        s->regs.ahbpci_win1_ctr = val;
        break;
    case 0x814: /* RCAR_AHBPCI_WIN2_CTR_REG */
        s->regs.ahbpci_win2_ctr = val;
        break;
    case 0x820: /* RCAR_PCI_INT_ENABLE_REG */
        s->regs.int_enable = val;
        pcibase_update_irq(s);
        break;
    case 0x824: /* RCAR_PCI_INT_STATUS_REG */
        /* Write 1 to clear bits */
        s->regs.int_status &= ~val;
        pcibase_update_irq(s);
        break;
    case 0x830: /* RCAR_AHB_BUS_CTR_REG */
        s->regs.ahb_bus_ctr = val;
        break;
    case 0x834: /* RCAR_USBCTR_REG */
        s->regs.usb_ctr = val;
        break;
    case 0x840: /* RCAR_PCI_ARBITER_CTR_REG */
        s->regs.arbiter_ctr = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize registers to reset state */
    s->regs.pcicahb_win1_ctr = 0;
    s->regs.pcicahb_win2_ctr = 0;
    s->regs.ahbpci_win1_ctr = 0;
    s->regs.ahbpci_win2_ctr = 0;
    s->regs.int_enable = 0;
    s->regs.int_status = 0;
    s->regs.ahb_bus_ctr = 0;
    s->regs.usb_ctr = 0;
    s->regs.arbiter_ctr = 0;
    s->regs.unit_rev = 1; /* arbitrary revision, driver just logs it */
    s->regs.pci_command_status = 0;
    s->regs.bar0_shadow = 0;
    s->regs.bar1_shadow = 0;

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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "rcar-regs" };
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not used */
    /* DMA not used */
    /* Timers not used */

    /* Final state initialization */
    pcibase_reset(DEVICE(s));
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

    /* Free buffers, stop timers, etc. None in this device. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pci_rcar_gen2_pci",
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

type_init(pcibase_register_types)
