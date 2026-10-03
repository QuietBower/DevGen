/*
 * QEMU PCI device model for Intel MEI TXE (based on pci-txe.c driver)
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

#define TYPE_PCIBASE_DEVICE "mei_txe_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x0F18
#define CLASS_ID PCI_CLASS_COMMUNICATION_OTHER

/* Known register offsets from pci-txe.c */
#define SICR_HOST_ALIVENESS_REQ_REG 0x214C
#define HICR_HOST_ALIVENESS_RESP_REG 0x2044
#define HICR_HOST_ALIVENESS_RESP_ACK    BIT(0)
#define HHISR_REG                        0x2020
#define HISR_REG                         0x2060
#define HICR_SEC_IPC_READINESS_REG       0x2040

/* IPC base address within SEC BAR */
#define IPC_BASE_ADDR	0x80400

/* Hardware register structure (shadows) */
typedef struct MeiTxeRegisters {
    uint32_t aliveness_req;
    uint32_t aliveness_resp;
    uint32_t readiness;       /* HICR_SEC_IPC_READINESS_REG */
    uint32_t slots;
    uint32_t intr_cause;
    uint32_t hhisr;
    uint32_t hisr;
    uint32_t sec_ipc_int_status;
} MeiTxeRegisters;

/* DMA descriptor (from driver) */
struct mei_dma_dscr {
    void *vaddr;
    dma_addr_t daddr;
    size_t size;
};

/* DMA state placeholder - exact configuration not yet known */
#define DMA_DSCR_NUM 0 /* Placeholder: real value needed */
typedef struct MeiDMAState {
    struct mei_dma_dscr dr[DMA_DSCR_NUM];
    uint32_t hbuf_wr_idx;
    uint32_t hbuf_rd_idx;
    uint32_t dbuf_wr_idx;
    uint32_t dbuf_rd_idx;
} MeiDMAState;

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    MeiTxeRegisters regs;

    MeiDMAState dma;

    uint32_t status;

    uint8_t reset_seq; /* probe reset sequence state */
    uint8_t pm_state;  /* D0/D3 etc. */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    uint32_t pending = s->intr_status & s->intr_mask;
    if (pending) {
        if (msi_enabled(PCI_DEVICE(s))) {
            msi_notify(PCI_DEVICE(s), 0);
        } else {
            pci_set_irq(PCI_DEVICE(s), 1);
        }
    } else {
        if (!msi_enabled(PCI_DEVICE(s))) {
            pci_set_irq(PCI_DEVICE(s), 0);
        }
    }
}

static uint64_t pcibase_br_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case HICR_HOST_ALIVENESS_RESP_REG:
        val = s->regs.aliveness_resp;
        break;
    case HHISR_REG:
        val = s->regs.hhisr;
        break;
    case HISR_REG:
        val = s->regs.hisr;
        break;
    case HICR_SEC_IPC_READINESS_REG:
        val = s->regs.readiness;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_br_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SICR_HOST_ALIVENESS_REQ_REG:
        s->regs.aliveness_req = val;
        /* Acknowledge by setting the ACK bit in the response register */
        s->regs.aliveness_resp |= HICR_HOST_ALIVENESS_RESP_ACK;
        break;
    case HHISR_REG:
        s->regs.hhisr &= ~val;   /* W1C */
        break;
    case HISR_REG:
        s->regs.hisr &= ~val;    /* W1C */
        break;
    case HICR_SEC_IPC_READINESS_REG:
        s->regs.readiness = val;
        /* Future: may trigger a readiness state change */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_sec_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Placeholder: register offsets relative to IPC_BASE_ADDR not yet fully known */
    /* Example: if (addr == IPC_BASE_ADDR + SEC_IPC_HOST_INT_STATUS_REG) val = s->regs.sec_ipc_int_status; */
    return val;
}

static void pcibase_sec_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Placeholder: future register handling */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return ~0ULL;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used by driver */
}

static const MemoryRegionOps pcibase_br_mmio_ops = {
    .read = pcibase_br_mmio_read,
    .write = pcibase_br_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_sec_mmio_ops = {
    .read = pcibase_sec_mmio_read,
    .write = pcibase_sec_mmio_write,
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
    /* Reset registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->status = 0;
    s->pm_state = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, const MemoryRegionOps *ops, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Enable MSI (driver uses MSI) */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }

    /* BAR Initialization: SEC_BAR=0, BRIDGE_BAR=1 */
    s->num_bars = 2;

    /* BAR0: MMIO for SEC registers (IPC) */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "mei_txe_sec_bar0";

    /* BAR1: MMIO for bridge registers */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x4000;   /* increased to cover HHISR/HISR/ALIVENESS */
    s->bar_info[1].name = "mei_txe_br_bar1";

    pcibase_register_bar(pdev, s, &s->bar_info[0], &pcibase_sec_mmio_ops, errp);
    pcibase_register_bar(pdev, s, &s->bar_info[1], &pcibase_br_mmio_ops, errp);
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
    .name = "mei_txe_pci",
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
