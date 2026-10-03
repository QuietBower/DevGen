/*
 * QEMU Intel IPU7 PCI device model
 * Based on Linux driver: drivers/staging/media/ipu7/ipu7.c
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

#define TYPE_PCIBASE_DEVICE "intel_ipu7_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Extract from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_IPU7 0x645d
#define PCI_CLASS_MULTIMEDIA_VIDEO 0x048000

/* Buttress control registers (offsets within BAR0) */
#define BUTTRESS_REG_IRQ_STATUS       0x2000
#define BUTTRESS_REG_IRQ_ENABLE       0x2008
#define BUTTRESS_REG_IRQ_CLEAR        0x200c
#define BUTTRESS_REG_IRQ_MASK         0x2010
#define BUTTRESS_REG_PWR_STATUS       0x2114
#define BUTTRESS_REG_IS_WORKPOINT_REQ 0x2104
#define BUTTRESS_REG_PS_WORKPOINT_REQ 0x2100
#define BUTTRESS_REG_SECURITY_CTL     0x2318
#define BUTTRESS_REG_FW_SOURCE_BASE   0x233c
#define BUTTRESS_REG_FW_SOURCE_SIZE   0x2338
#define BUTTRESS_REG_IU2CSEDATA0      0x2510
#define BUTTRESS_REG_CSE2IUCSR        0x2508
#define BUTTRESS_REG_CSE2IUDB0        0x2500
#define BUTTRESS_REG_CAMERA_MASK      0x2310
#define BUTTRESS_REG_IU2CSECSR        0x2514
#define BUTTRESS_REG_CSE2IUDATA0      0x2504
#define BUTTRESS_REG_IU2CSEDB0        0x250c
#define BUTTRESS_REG_IPU_SKU          0x3030
#define BUTTRESS_REG_IDLE_WDT         0x218c
#define BUTTRESS_REG_FW_RESET_CTL     0x2334

/* PB (BAR4) register offsets - stubbed, need definitions for full emulation */
/* #define INTERRUPT_STATUS ... */
/* #define BTRS_LOCAL_INTERRUPT_MASK ... */
/* etc. */

/* IRQ bits */
#define BUTTRESS_IRQ_PS_IRQ             (1u << 31)
#define BUTTRESS_IRQ_CSE_CSR_SET        (1u << 2)
#define BUTTRESS_IRQ_IPC_EXEC_DONE_BY_CSE  (1u << 0)
#define BUTTRESS_IRQ_IPC_FROM_CSE_IS_WAITING  (1u << 1)
#define BUTTRESS_IRQ_PUNIT_2_IUNIT_IRQ  (1u << 3)
#define BUTTRESS_IRQ_IS_IRQ             (1u << 30)

#define BUTTRESS_IQS \
    (BUTTRESS_IRQ_IS_IRQ | BUTTRESS_IRQ_PS_IRQ | \
     BUTTRESS_IRQ_IPC_FROM_CSE_IS_WAITING | BUTTRESS_IRQ_CSE_CSR_SET | \
     BUTTRESS_IRQ_IPC_EXEC_DONE_BY_CSE | BUTTRESS_IRQ_PUNIT_2_IUNIT_IRQ)

/* Power management states */
#define IPU_BUTTRESS_PWR_STATE_UP_DONE  0x3
#define IPU_BUTTRESS_PWR_STATE_DN_DONE  0x0

/*----------------------------------------------------------------------------*/

typedef struct {
    uint32_t irq_enable;    /* 0x2008 */
    uint32_t irq_mask;      /* 0x2010 */
    uint32_t irq_status;    /* 0x2000 */
    uint32_t power_state;   /* 0x2114 */
    uint32_t cam_mask;      /* 0x2310 */
    /* The remaining registers fall through to bar0_mmio for storage,
     * but for ones needing special handling we use these fields.
     */
} ButtressState;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Memory regions */
    MemoryRegion bar0_mr;   /* Main IPU MMIO (includes buttress) */
    MemoryRegion bar4_mr;   /* PB IO region (stub) */

    /* 4 MB shadow for BAR0; specific offsets handled before array */
    uint8_t bar0_mmio[4 * MiB];

    /* Interrupt state */
    ButtressState buttress;

    /* MSI capability */
    bool has_msi;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    uint32_t pending = s->buttress.irq_status & s->buttress.irq_enable;
    if (pending) {
        if (msi_enabled(PCI_DEVICE(s))) {
            msi_notify(PCI_DEVICE(s), 0);
        } else {
            /* INTx not implemented; no fallback for now */
        }
    }
}

/*------------ BAR0 MMIO handlers ------------*/
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Handle known buttress registers specially */
    switch (addr) {
    case BUTTRESS_REG_IRQ_STATUS:
        val = s->buttress.irq_status;
        break;
    case BUTTRESS_REG_IRQ_ENABLE:
        val = s->buttress.irq_enable;
        break;
    case BUTTRESS_REG_IRQ_MASK:
        val = s->buttress.irq_mask;
        break;
    case BUTTRESS_REG_IRQ_CLEAR:
        /* Write-only: return 0 */
        val = 0;
        break;
    case BUTTRESS_REG_PWR_STATUS:
        val = s->buttress.power_state;
        break;
    case BUTTRESS_REG_CAMERA_MASK:
        val = s->buttress.cam_mask;
        break;
    case BUTTRESS_REG_SECURITY_CTL:
        /* Return non-secure mode (0) to skip IPC auth */
        val = 0;
        break;
    case BUTTRESS_REG_IPU_SKU:
        /* Report IPU generation 7, SKU 1 (0x17) */
        val = 0x17;
        break;
    default:
        /* Fallback: generic shadow access */
        if (addr < sizeof(s->bar0_mmio)) {
            switch (size) {
            case 1:
                val = s->bar0_mmio[addr];
                break;
            case 2:
                val = lduw_le_p(s->bar0_mmio + addr);
                break;
            case 4:
                val = ldl_le_p(s->bar0_mmio + addr);
                break;
            case 8:
                val = ldq_le_p(s->bar0_mmio + addr);
                break;
            default:
                val = 0;
                break;
            }
        }
        break;
    }
    return val;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case BUTTRESS_REG_IRQ_ENABLE:
        s->buttress.irq_enable = val;
        pcibase_update_irq(s);
        break;
    case BUTTRESS_REG_IRQ_MASK:
        s->buttress.irq_mask = val;
        break;
    case BUTTRESS_REG_IRQ_CLEAR:
        s->buttress.irq_status &= ~val;
        pcibase_update_irq(s);
        break;
    case BUTTRESS_REG_IRQ_STATUS:
        /* Writing to IRQ_STATUS sets the status (for emulation) */
        s->buttress.irq_status = val;
        pcibase_update_irq(s);
        break;
    case BUTTRESS_REG_PWR_STATUS:
        s->buttress.power_state = val;
        break;
    case BUTTRESS_REG_CAMERA_MASK:
        s->buttress.cam_mask = val;
        break;
    case BUTTRESS_REG_SECURITY_CTL:
        /* Ignore writes to keep secure mode disabled */
        break;
    case BUTTRESS_REG_IPU_SKU:
        /* Read-only, ignore write */
        break;
    default:
        /* Generic shadow storage */
        if (addr < sizeof(s->bar0_mmio)) {
            switch (size) {
            case 1:
                s->bar0_mmio[addr] = val;
                break;
            case 2:
                stw_le_p(s->bar0_mmio + addr, val);
                break;
            case 4:
                stl_le_p(s->bar0_mmio + addr, val);
                break;
            case 8:
                stq_le_p(s->bar0_mmio + addr, val);
                break;
            default:
                break;
            }
        }
        break;
    }
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/*------------ BAR4 (PB) MMIO stub ------------*/
static uint64_t pcibase_bar4_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Stub: return 0 for any PB register access */
    return 0;
}

static void pcibase_bar4_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Stub: ignore writes; driver may configure PB but we don't emulate it */
}

static const MemoryRegionOps pcibase_bar4_ops = {
    .read = pcibase_bar4_read,
    .write = pcibase_bar4_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->bar0_mmio, 0, sizeof(s->bar0_mmio));
    memset(&s->buttress, 0, sizeof(s->buttress));

    /* Set initial power state to UP_DONE to satisfy driver */
    s->buttress.power_state = IPU_BUTTRESS_PWR_STATE_UP_DONE;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_IPU7);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_VIDEO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 0); /* MSI */

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* MSI (1 vector) */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    /* BAR0: Main IPU MMIO, 4MB */
    memory_region_init_io(&s->bar0_mr, OBJECT(s), &pcibase_bar0_ops, s,
                          "bar0", 4 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0_mr);

    /* BAR4: PB IO, 4KB stub */
    memory_region_init_io(&s->bar4_mr, OBJECT(s), &pcibase_bar4_ops, s,
                          "bar4", 4 * KiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar4_mr);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "intel_ipu7_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(buttress.irq_enable, PCIBaseState),
        VMSTATE_UINT32(buttress.irq_mask, PCIBaseState),
        VMSTATE_UINT32(buttress.irq_status, PCIBaseState),
        VMSTATE_UINT32(buttress.power_state, PCIBaseState),
        VMSTATE_UINT32(buttress.cam_mask, PCIBaseState),
        VMSTATE_BUFFER_UNSAFE(bar0_mmio, PCIBaseState, 0, sizeof_field(PCIBaseState, bar0_mmio)),
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

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
