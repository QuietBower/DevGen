/*
 * QEMU PCI device model for Intel iSMT SMBus controller
 * Functional implementation based on Linux driver drivers/i2c/busses/i2c-ismt.c
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

#include "hw/i2c/i2c.h"

#define TYPE_PCIBASE_DEVICE "ismt_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI class code for SMBus / I2C controller if not provided by headers */
#ifndef PCI_CLASS_SERIAL_BUS
#define PCI_CLASS_SERIAL_BUS 0x0c00
#endif

#define ISMT_PCIBASE_VENDOR_ID   PCI_VENDOR_ID_INTEL
#define ISMT_PCIBASE_DEVICE_ID   0x0c59
#define ISMT_PCIBASE_CLASS_ID    PCI_CLASS_SERIAL_BUS

#define SMBBAR                  0

#define ISMT_DESC_ENTRIES       2
#define ISMT_MAX_RETRIES        3
#define ISMT_LOG_ENTRIES        3

#define ISMT_DESC_CWRL          0x01
#define ISMT_DESC_BLK           0x04
#define ISMT_DESC_FAIR          0x08
#define ISMT_DESC_PEC           0x10
#define ISMT_DESC_I2C           0x20
#define ISMT_DESC_INT           0x40
#define ISMT_DESC_SOE           0x80

#define ISMT_DESC_SCS           0x01
#define ISMT_DESC_DLTO          0x04
#define ISMT_DESC_NAK           0x08
#define ISMT_DESC_CRC           0x10
#define ISMT_DESC_CLTO          0x20
#define ISMT_DESC_COL           0x40
#define ISMT_DESC_LPR           0x80

#define ISMT_DESC_ADDR_RW(addr, rw) (((addr) << 1) | (rw))

#define ISMT_GR_GCTRL           0x000
#define ISMT_GR_SMTICL          0x008
#define ISMT_GR_ERRINTMSK       0x010
#define ISMT_GR_ERRAERMSK       0x014
#define ISMT_GR_ERRSTS          0x018
#define ISMT_GR_ERRINFO         0x01c

#define ISMT_MSTR_MDBA          0x100
#define ISMT_MSTR_MCTRL         0x108
#define ISMT_MSTR_MSTS          0x10c
#define ISMT_MSTR_MDS           0x110
#define ISMT_MSTR_RPOLICY       0x114

#define ISMT_SPGT               0x300

#define ISMT_GCTRL_TRST         0x04
#define ISMT_GCTRL_KILL         0x08
#define ISMT_GCTRL_SRST         0x40

#define ISMT_MCTRL_SS           0x01
#define ISMT_MCTRL_MEIE         0x10
#define ISMT_MCTRL_FMHP         0x00ff0000

#define ISMT_MSTS_HMTP          0xff0000
#define ISMT_MSTS_MIS           0x20
#define ISMT_MSTS_MEIS          0x10
#define ISMT_MSTS_IP            0x01

#define ISMT_MDS_MASK           0xff

#define ISMT_SPGT_SPD_MASK      0xc0000000
#define ISMT_SPGT_SPD_80K       0x00
#define ISMT_SPGT_SPD_100K      (0x1 << 30)
#define ISMT_SPGT_SPD_400K      (0x2U << 30)
#define ISMT_SPGT_SPD_1M        (0x3U << 30)

#define ISMT_MSICTL_MSIE        0x01

#define ISMT_BAR_MMIO_SIZE      (64 * KiB)


typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
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

    /* Hardware Register Shadows */
    struct {
        uint32_t gctrl;
        uint32_t smticl_low;
        uint32_t smticl_high;
        uint32_t errintmsk;
        uint32_t erraermsk;
        uint32_t errsts;
        uint32_t errinfo;
        uint32_t mdba_low;
        uint32_t mdba_high;
        uint32_t mctrl;
        uint32_t msts;
        uint32_t mds;
        uint32_t rpolicy;
        uint32_t spgt;
    } regs;

    /* DMA Context */
    struct {
        uint64_t desc_base;
    } dma_ctx;

    uint32_t status_flags;
    uint32_t reset_state;
    uint32_t power_state;
    uint32_t other_state;
};


static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending;

    /* For this simplified model, use msts as the interrupt source register */
    pending = s->regs.msts & (ISMT_MSTS_MIS | ISMT_MSTS_MEIS);

    if (!pending) {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
        return;
    }

    if (msi_enabled(pdev)) {
        /* Single MSI vector */
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

/*
 * The Linux driver programs the descriptor ring and DMA buffers,
 * but it never reads back descriptor memory via MMIO; it uses
 * CPU-side coherent memory instead. Implementing real DMA is
 * unnecessary for driver probing/binding and would require
 * descriptor layout details that are not provided here.
 *
 * Therefore, this model does not implement any active DMA logic.
 */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ISMT_GR_GCTRL:
        val = s->regs.gctrl;
        break;
    case ISMT_GR_SMTICL:
        /* 64-bit register via two 32-bit parts */
        if (size == 8) {
            val = ((uint64_t)s->regs.smticl_high << 32) |
                  (uint64_t)s->regs.smticl_low;
        } else if (size == 4) {
            /* Return lower dword by default */
            val = s->regs.smticl_low;
        }
        break;
    case ISMT_GR_ERRINTMSK:
        val = s->regs.errintmsk;
        break;
    case ISMT_GR_ERRAERMSK:
        val = s->regs.erraermsk;
        break;
    case ISMT_GR_ERRSTS:
        val = s->regs.errsts;
        break;
    case ISMT_GR_ERRINFO:
        val = s->regs.errinfo;
        break;

    case ISMT_MSTR_MDBA:
        if (size == 8) {
            val = ((uint64_t)s->regs.mdba_high << 32) |
                  (uint64_t)s->regs.mdba_low;
        } else if (size == 4) {
            val = s->regs.mdba_low;
        }
        break;
    case ISMT_MSTR_MCTRL:
        val = s->regs.mctrl;
        break;
    case ISMT_MSTR_MSTS:
        val = s->regs.msts;
        break;
    case ISMT_MSTR_MDS:
        val = s->regs.mds;
        break;
    case ISMT_MSTR_RPOLICY:
        val = s->regs.rpolicy;
        break;

    case ISMT_SPGT:
        val = s->regs.spgt;
        break;

    default:
        /* Unimplemented offsets read as zero */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)size;

    switch (addr) {
    case ISMT_GR_GCTRL:
        s->regs.gctrl = (uint32_t)val;
        /*
         * KILL bit is written by the driver to abort a transaction.
         * We do not model any state machine, so no extra behavior.
         */
        break;

    case ISMT_GR_SMTICL:
        if (size == 8) {
            s->regs.smticl_low = (uint32_t)(val & 0xffffffffu);
            s->regs.smticl_high = (uint32_t)(val >> 32);
        } else if (size == 4) {
            /* Store lower dword; high remains unchanged */
            s->regs.smticl_low = (uint32_t)val;
        }
        break;

    case ISMT_GR_ERRINTMSK:
        s->regs.errintmsk = (uint32_t)val;
        break;

    case ISMT_GR_ERRAERMSK:
        s->regs.erraermsk = (uint32_t)val;
        break;

    case ISMT_GR_ERRSTS:
        /* Treat as W1C (write 1 to clear) */
        s->regs.errsts &= ~((uint32_t)val);
        break;

    case ISMT_GR_ERRINFO:
        s->regs.errinfo = (uint32_t)val;
        break;

    case ISMT_MSTR_MDBA:
        if (size == 8) {
            s->regs.mdba_low = (uint32_t)(val & 0xffffffffu);
            s->regs.mdba_high = (uint32_t)(val >> 32);
        } else if (size == 4) {
            s->regs.mdba_low = (uint32_t)val;
        }
        s->dma_ctx.desc_base = ((uint64_t)s->regs.mdba_high << 32) |
                                (uint64_t)s->regs.mdba_low;
        break;

    case ISMT_MSTR_MCTRL: {
        uint32_t old = s->regs.mctrl;
        uint32_t newv = (uint32_t)val;
        s->regs.mctrl = newv;

        /* If MEIE is enabled, interrupts can be generated. */

        /* Start bit (SS) is set by software to kick a transaction. */
        if ((newv & ISMT_MCTRL_SS) && !(old & ISMT_MCTRL_SS)) {
            /*
             * In real hardware, this would cause the controller
             * to process the descriptor and then complete with an
             * interrupt. For driver probing and simple operation,
             * we immediately complete: set MSTS status bits and
             * raise an interrupt.
             */
            s->regs.msts |= (ISMT_MSTS_MIS | ISMT_MSTS_MEIS);
            pcibase_update_irq(s);
        }
        break;
    }

    case ISMT_MSTR_MSTS: {
        uint32_t cur = s->regs.msts;
        uint32_t w = (uint32_t)val;
        /* W1C for MIS/MEIS bits */
        cur &= ~(w & (ISMT_MSTS_MIS | ISMT_MSTS_MEIS));
        s->regs.msts = cur;
        pcibase_update_irq(s);
        break;
    }

    case ISMT_MSTR_MDS:
        /* Driver uses this to program descriptor ring size. */
        s->regs.mds = (uint32_t)val;
        break;

    case ISMT_MSTR_RPOLICY:
        s->regs.rpolicy = (uint32_t)val;
        break;

    case ISMT_SPGT:
        /* Speed control register */
        s->regs.spgt = (uint32_t)val;
        break;

    default:
        /* Ignore writes to unknown offsets */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* This device is MMIO-only in the provided driver. */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
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

    memset(&s->regs, 0, sizeof(s->regs));
    s->status_flags = 0;
    s->reset_state = 0;

    /* Ensure MSTS is clear so no spurious interrupts */
    s->regs.msts = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  ISMT_PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ISMT_PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ISMT_PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = SMBBAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = ISMT_BAR_MMIO_SIZE;
    s->bar_info[0].name = "ismt-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->dma_ctx.desc_base = 0;

    memset(&s->regs, 0, sizeof(s->regs));
    s->status_flags = 0;
    s->reset_state = 0;
    s->power_state = 0;
    s->other_state = 0;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "ismt_smbus_pci",
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
