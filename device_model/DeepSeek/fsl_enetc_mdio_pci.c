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
/* No additional headers needed */

#define TYPE_PCIBASE_DEVICE "fsl_enetc_mdio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID       0x1957   /* PCI_VENDOR_ID_FREESCALE */
#define DEVICE_ID       0xee01   /* ENETC_MDIO_DEV_ID */
#define CLASS_ID        0xff00   /* Miscellaneous device */
#define ENETC_EMDIO_BASE 0x1c00

/* MDIO register offsets (relative to ENETC_EMDIO_BASE) */
#define ENETC_MDIO_CFG   0x0
#define ENETC_MDIO_CTL   0x4
#define ENETC_MDIO_DATA  0x8
#define ENETC_MDIO_ADDR  0xc

/* MDIO control register bitfields */
#define MDIO_CTL_PORT_ADDR(x)   (((x) & 0x1f) << 5)
#define MDIO_CTL_DEV_ADDR(x)    ((x) & 0x1f)
#define MDIO_CTL_READ           BIT(15)

/* MDIO configuration register bitfields */
#define MDIO_CFG_ENC45          BIT(6)
#define MDIO_CFG_RD_ER          BIT(1)
#define MDIO_CFG_BSY            BIT(0)  /* Tentative busy bit; actual bit may differ. Need enetc_mdio_is_busy() source */

/* ENETC EMDIO configuration value (needs MDIO_CFG_HOLD, MDIO_CFG_CLKDIV, MDIO_CFG_NEG) */
/* #define ENETC_EMDIO_CFG (MDIO_CFG_HOLD(2) | MDIO_CFG_CLKDIV(258) | MDIO_CFG_NEG) */

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

    /* MDIO register shadows */
    uint32_t mdio_cfg;
    uint32_t mdio_ctl;
    uint32_t mdio_data;
    uint32_t mdio_addr;

    /* Timer for simulating MDIO transaction completion delay */
    QEMUTimer mdio_timer;
};

/* Timer callback: complete MDIO read transaction after delay */
static void pcibase_mdio_complete(void *opaque)
{
    PCIBaseState *s = opaque;
    s->mdio_data = 0xFFFFFFFF;   /* Return all 1s to indicate no PHY present */
    s->mdio_cfg &= ~MDIO_CFG_BSY;   /* Clear busy bit */
    s->mdio_cfg &= ~MDIO_CFG_RD_ER; /* Clear read error */
    s->mdio_ctl &= ~MDIO_CTL_READ;  /* Clear read command flag */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Handle MDIO register region */
    if (addr >= ENETC_EMDIO_BASE && addr <= ENETC_EMDIO_BASE + 0xF) {
        hwaddr offset = addr - ENETC_EMDIO_BASE;
        switch (offset) {
        case ENETC_MDIO_CFG:
            val = s->mdio_cfg;
            break;
        case ENETC_MDIO_CTL:
            val = s->mdio_ctl;
            break;
        case ENETC_MDIO_DATA:
            val = s->mdio_data;
            break;
        case ENETC_MDIO_ADDR:
            val = s->mdio_addr;
            break;
        default:
            val = 0;
            break;
        }
        return val;
    }

    /* Default: return 0 for any other access */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= ENETC_EMDIO_BASE && addr <= ENETC_EMDIO_BASE + 0xF) {
        hwaddr offset = addr - ENETC_EMDIO_BASE;
        switch (offset) {
        case ENETC_MDIO_CFG:
            /* Preserve read-only busy bit; only write configurable fields */
            s->mdio_cfg = (s->mdio_cfg & MDIO_CFG_BSY) | (val & ~MDIO_CFG_BSY);
            break;
        case ENETC_MDIO_CTL:
            s->mdio_ctl = val;
            /* If read command and not already busy, initiate transaction */
            if ((val & MDIO_CTL_READ) && !(s->mdio_cfg & MDIO_CFG_BSY)) {
                s->mdio_cfg |= MDIO_CFG_BSY;   /* Set busy */
                /* Arm timer to complete after 1ms (virtual time) */
                timer_mod(&s->mdio_timer,
                          qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
            }
            break;
        case ENETC_MDIO_DATA:
            s->mdio_data = val;
            break;
        case ENETC_MDIO_ADDR:
            s->mdio_addr = val;
            break;
        default:
            break;
        }
    }
    /* Ignore writes to other regions */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No PIO used; placeholder deleted */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* No PIO used; placeholder deleted */
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

    /* Cancel any pending MDIO timer and clear busy bit */
    timer_del(&s->mdio_timer);
    s->mdio_cfg &= ~MDIO_CFG_BSY;

    /* Reset MDIO registers */
    s->mdio_cfg = 0;
    s->mdio_ctl = 0;
    s->mdio_data = 0;
    s->mdio_addr = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* BAR0: MMIO for ENETC register space, size 0x2000 (8KB) to cover MDIO at 0x1c00 */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "bar0" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MDIO timer */
    timer_init_ms(&s->mdio_timer, QEMU_CLOCK_VIRTUAL, pcibase_mdio_complete, s);

    /* MSI/MSI-X init placeholder deleted; not referenced in driver */
    /* DMA config placeholder deleted */
    /* Timer config placeholder deleted */
    /* Field init: zero MDIO shadow registers */
    s->mdio_cfg = 0;
    s->mdio_ctl = 0;
    s->mdio_data = 0;
    s->mdio_addr = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Cancel and delete MDIO timer */
    timer_del(&s->mdio_timer);

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* Placeholder: additional uninit logic will be added if needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "fsl_enetc_mdio_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(mdio_cfg, PCIBaseState),
        VMSTATE_UINT32(mdio_ctl, PCIBaseState),
        VMSTATE_UINT32(mdio_data, PCIBaseState),
        VMSTATE_UINT32(mdio_addr, PCIBaseState),
        VMSTATE_TIMER(mdio_timer, PCIBaseState),
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
