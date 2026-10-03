/*
 * QEMU PCI device model for NI 65xx based on Linux driver
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

#define TYPE_PCIBASE_DEVICE "ni_65xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define NI_65XX_VENDOR_ID          0x1093
#define NI_65XX_DEVICE_ID          0x1710
#define NI_65XX_PCI_CLASS_ID       PCI_CLASS_OTHERS

#define MITE_IODWBSR               0xc0
#define WENAB                      (1U << 7)

#define NI_65XX_ID_REG             0x00
#define NI_65XX_CLR_REG            0x01
#define NI_65XX_CLR_WDOG_INT       (1U << 6)
#define NI_65XX_CLR_WDOG_PING      (1U << 5)
#define NI_65XX_CLR_WDOG_EXP       (1U << 4)
#define NI_65XX_CLR_EDGE_INT       (1U << 3)
#define NI_65XX_CLR_OVERFLOW_INT   (1U << 2)
#define NI_65XX_STATUS_REG         0x02
#define NI_65XX_STATUS_WDOG_INT        (1U << 5)
#define NI_65XX_STATUS_FALL_EDGE       (1U << 4)
#define NI_65XX_STATUS_RISE_EDGE       (1U << 3)
#define NI_65XX_STATUS_INT             (1U << 2)
#define NI_65XX_STATUS_OVERFLOW_INT    (1U << 1)
#define NI_65XX_STATUS_EDGE_INT        (1U << 0)
#define NI_65XX_CTRL_REG           0x03
#define NI_65XX_CTRL_WDOG_ENA          (1U << 5)
#define NI_65XX_CTRL_FALL_EDGE_ENA     (1U << 4)
#define NI_65XX_CTRL_RISE_EDGE_ENA     (1U << 3)
#define NI_65XX_CTRL_INT_ENA           (1U << 2)
#define NI_65XX_CTRL_OVERFLOW_ENA      (1U << 1)
#define NI_65XX_CTRL_EDGE_ENA          (1U << 0)
#define NI_65XX_REV_REG            0x04
#define NI_65XX_FILTER_REG         0x08
#define NI_65XX_RTSI_ROUTE_REG     0x0c
#define NI_65XX_RTSI_EDGE_REG      0x0e
#define NI_65XX_RTSI_WDOG_REG      0x10
#define NI_65XX_RTSI_TRIG_REG      0x12
#define NI_65XX_AUTO_CLK_SEL_REG   0x14
#define NI_65XX_AUTO_CLK_SEL_STATUS    (1U << 1)
#define NI_65XX_AUTO_CLK_SEL_DISABLE   (1U << 0)
#define NI_65XX_WDOG_CTRL_REG      0x15
#define NI_65XX_WDOG_CTRL_ENA      (1U << 0)
#define NI_65XX_RTSI_CFG_REG       0x16
#define NI_65XX_RTSI_CFG_RISE_SENSE    (1U << 2)
#define NI_65XX_RTSI_CFG_FALL_SENSE    (1U << 1)
#define NI_65XX_RTSI_CFG_SYNC_DETECT   (1U << 0)
#define NI_65XX_WDOG_STATUS_REG    0x17
#define NI_65XX_WDOG_STATUS_EXP    (1U << 0)
#define NI_65XX_WDOG_INTERVAL_REG  0x18
#define NI_65XX_PORT(x)            ((x) * 0x10)
#define NI_65XX_IO_DATA_REG(x)     (0x40 + NI_65XX_PORT(x))
#define NI_65XX_IO_SEL_REG(x)      (0x41 + NI_65XX_PORT(x))
#define NI_65XX_IO_SEL_OUTPUT      0
#define NI_65XX_IO_SEL_INPUT       (1U << 0)
#define NI_65XX_RISE_EDGE_ENA_REG(x)   (0x42 + NI_65XX_PORT(x))
#define NI_65XX_FALL_EDGE_ENA_REG(x)   (0x43 + NI_65XX_PORT(x))
#define NI_65XX_FILTER_ENA(x)      (0x44 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_HIZ_REG(x)    (0x46 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_ENA(x)        (0x47 + NI_65XX_PORT(x))
#define NI_65XX_WDOG_HI_LO_REG(x)  (0x48 + NI_65XX_PORT(x))
#define NI_65XX_RTSI_ENA(x)        (0x49 + NI_65XX_PORT(x))
#define NI_65XX_PORT_TO_CHAN(x)    ((x) * 8)
#define NI_65XX_CHAN_TO_PORT(x)    ((x) / 8)
#define NI_65XX_CHAN_TO_MASK(x)    (1U << ((x) % 8))


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
        uint8_t regs[0x200];
    } regs;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t status = s->regs.regs[NI_65XX_STATUS_REG];
    uint8_t ctrl = s->regs.regs[NI_65XX_CTRL_REG];

    if ((status & NI_65XX_STATUS_INT) && (ctrl & NI_65XX_CTRL_INT_ENA)) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only 8-bit accesses are used by the driver (readb / writeb).
     * A single 32-bit access is used for NI_65XX_FILTER_REG via writel,
     * but there is no 32-bit read path in the driver.
     */

    if (size == 1) {
        if (addr < sizeof(s->regs.regs)) {
            val = s->regs.regs[addr];
        } else {
            val = 0xff;
        }
    } else if (size == 4) {
        if (addr == NI_65XX_FILTER_REG) {
            /* Return the stored 32-bit filter interval. */
            uint32_t v = 0;
            if (addr + 4 <= sizeof(s->regs.regs)) {
                v |= ((uint32_t)s->regs.regs[addr + 0]) << 0;
                v |= ((uint32_t)s->regs.regs[addr + 1]) << 8;
                v |= ((uint32_t)s->regs.regs[addr + 2]) << 16;
                v |= ((uint32_t)s->regs.regs[addr + 3]) << 24;
            }
            val = v;
        } else {
            val = 0;
        }
    } else {
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        uint8_t v = (uint8_t)val;

        if (addr == NI_65XX_CLR_REG) {
            /* Clear bits in STATUS according to CLR register writes. */
            uint8_t status = s->regs.regs[NI_65XX_STATUS_REG];

            if (v & NI_65XX_CLR_EDGE_INT) {
                status &= ~(NI_65XX_STATUS_EDGE_INT | NI_65XX_STATUS_INT);
            }
            if (v & NI_65XX_CLR_OVERFLOW_INT) {
                status &= ~NI_65XX_STATUS_OVERFLOW_INT;
            }
            if (v & NI_65XX_CLR_WDOG_INT) {
                status &= ~NI_65XX_STATUS_WDOG_INT;
            }
            if (v & NI_65XX_CLR_WDOG_EXP) {
                status &= ~NI_65XX_WDOG_STATUS_EXP;
            }
            s->regs.regs[NI_65XX_STATUS_REG] = status;
            pcibase_update_irq(s);
            return;
        }

        if (addr == NI_65XX_CTRL_REG) {
            /* Store control register; IRQ logic handled by update helper. */
            s->regs.regs[NI_65XX_CTRL_REG] = v;
            pcibase_update_irq(s);
            return;
        }

        if (addr == NI_65XX_ID_REG) {
            /* ID register is read-only from driver's perspective.
             * Ignore writes.
             */
            return;
        }

        /* Generic 8-bit register write within backing array. */
        if (addr < sizeof(s->regs.regs)) {
            s->regs.regs[addr] = v;
        }

    } else if (size == 4) {
        /* Only NI_65XX_FILTER_REG is written as 32-bit value by the driver. */
        if (addr == NI_65XX_FILTER_REG) {
            uint32_t v = (uint32_t)val;
            if (addr + 4 <= sizeof(s->regs.regs)) {
                s->regs.regs[addr + 0] = (uint8_t)(v & 0xff);
                s->regs.regs[addr + 1] = (uint8_t)((v >> 8) & 0xff);
                s->regs.regs[addr + 2] = (uint8_t)((v >> 16) & 0xff);
                s->regs.regs[addr + 3] = (uint8_t)((v >> 24) & 0xff);
            }
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 1) {
        if (addr < sizeof(s->regs.regs)) {
            val = s->regs.regs[addr];
        } else {
            val = 0xff;
        }
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t addr2, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)addr2;

    if (size == 1) {
        if (addr < sizeof(s->regs.regs)) {
            s->regs.regs[addr] = (uint8_t)addr2;
        }
    }
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
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    /* Clear all register shadows. */
    memset(s->regs.regs, 0, sizeof(s->regs.regs));

    /* Initialize ID register to a non-zero value so that the driver
     * dev_info() prints something meaningful. The exact value is not
     * interpreted by the driver beyond logging.
     */
    s->regs.regs[NI_65XX_ID_REG] = 0x01;

    /* STATUS and CTRL default to 0 (already via memset). */

    /* Default I/O selection: set all ports as input (matching
     * ni_65xx_auto_attach configuration for DIO ports).
     * Without full board data we assume the maximum ports that fit
     * within the modeled register space.
     */
    for (i = 0; i < 16; i++) {
        hwaddr sel_off = NI_65XX_IO_SEL_REG(i);
        if (sel_off < sizeof(s->regs.regs)) {
            s->regs.regs[sel_off] = NI_65XX_IO_SEL_INPUT;
        }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  NI_65XX_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  NI_65XX_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, NI_65XX_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize BAR layout expected by the driver:
     * BAR0: MITE window (not used by QEMU model but must exist)
     * BAR1: main device MMIO used by dev->mmio accesses.
     */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x100; /* minimal, driver only pokes MITE_IODWBSR */
    s->bar_info[0].name  = "ni_65xx_mite";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 0x200; /* covers all defined registers */
    s->bar_info[1].name  = "ni_65xx_mmio";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize register file state. */
    memset(s->regs.regs, 0, sizeof(s->regs.regs));
    s->regs.regs[NI_65XX_ID_REG] = 0x01;
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
    .name = "ni_65xx_pci",
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

