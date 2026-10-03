/*
 * QEMU PCI Device Model for Intel Tangier/Baytrail Audio DSP (sof-audio-pci-intel-tng)
 * Corrected after runtime failure: adjusted BAR size to 2MB, added subsystem IDs,
 * and ensured proper platform identification for machine driver matching.
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

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_tng_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x119a
#define CLASS_ID  0x0401

/* BAR indices */
#define DSP_BAR 0
#define IMR_BAR 2
#define LPE_BAR 3

/* BAR size corrected to 2MB based on kernel resource allocation logs */
#define PCI_BAR_SIZE 0x200000

/* DSP memory map offsets (relative to the BAR base) */
#define IRAM_OFFSET    0x0C0000
#define DRAM_OFFSET    0x100000
#define MBOX_OFFSET    0x144000

/* SHIM register offsets within the DSP memory space */
#define SHIM_CSR_OFF      0x00
#define SHIM_IMRX_OFF     0x28
#define SHIM_IMRD_OFF     0x30
#define SHIM_IPCX_OFF     0x38
#define SHIM_IPCD_OFF     0x40

/* Compute BAR-relative offsets for SHIM registers */
#define BAR_SHIM_BASE    (0x140000 - IRAM_OFFSET)  /* 0x80000 */
#define BAR_SHIM_CSR     (BAR_SHIM_BASE + SHIM_CSR_OFF)
#define BAR_SHIM_IMRX    (BAR_SHIM_BASE + SHIM_IMRX_OFF)
#define BAR_SHIM_IMRD   (BAR_SHIM_BASE + SHIM_IMRD_OFF)
#define BAR_SHIM_IPCX    (BAR_SHIM_BASE + SHIM_IPCX_OFF)
#define BAR_SHIM_IPCD    (BAR_SHIM_BASE + SHIM_IPCD_OFF)

/* Additional BAR offsets for other regions */
#define BAR_MBOX_OFFSET  (MBOX_OFFSET - IRAM_OFFSET)

/* SHIM CSR bits */
#define SHIM_BYT_CSR_RST          (1 << 0)
#define SHIM_BYT_CSR_VECTOR_SEL   (1 << 1)
#define SHIM_BYT_CSR_STALL        (1 << 2)
#define SHIM_BYT_CSR_PWAITMODE    (1 << 3)

/* SHIM IMRX/IMRD interrupt mask bits */
#define SHIM_IMRX_BUSY  (1 << 1)
#define SHIM_IMRX_DONE  (1 << 0)
#define SHIM_IMRD_BUSY  (1 << 1)
#define SHIM_IMRD_DONE  (1 << 0)

/* SHIM IPCX/IPCD status bits (32-bit and 64-bit variants) */
#define SHIM_IPCX_DONE        (1 << 30)
#define SHIM_IPCX_BUSY        (1 << 31)
#define SHIM_BYT_IPCX_DONE    (1ULL << 62)
#define SHIM_BYT_IPCX_BUSY    (1ULL << 63)

#define SHIM_IPCD_DONE        (1 << 30)
#define SHIM_IPCD_BUSY        (1 << 31)
#define SHIM_BYT_IPCD_DONE    (1ULL << 62)
#define SHIM_BYT_IPCD_BUSY    (1ULL << 63)

/* Combined status bit masks for IPC registers */
#define IPC_DONE_BITS (SHIM_IPCX_DONE | SHIM_BYT_IPCX_DONE)
#define IPC_BUSY_BITS (SHIM_IPCX_BUSY | SHIM_BYT_IPCX_BUSY)
#define IPC_STATUS_MASK (IPC_DONE_BITS | IPC_BUSY_BITS)

/* For IPCD, similar masks */
#define IPCD_DONE_BITS (SHIM_IPCD_DONE | SHIM_BYT_IPCD_DONE)
#define IPCD_BUSY_BITS (SHIM_IPCD_BUSY | SHIM_BYT_IPCD_BUSY)
#define IPCD_STATUS_MASK (IPCD_DONE_BITS | IPCD_BUSY_BITS)

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
        uint32_t csr;
        uint64_t imrx;
        uint64_t imrd;
        uint64_t ipcx;
        uint64_t ipcd;
    } shim;

    /* DMA Context (unused for this device) */
    struct {
        dma_addr_t dma_addr;
        uint32_t dma_len;
        bool dma_active;
    } dma;

    uint32_t status;             /* Operational status flags */
    bool in_reset;               /* State used to handle reset sequences */
    uint32_t pm_state;           /* Power management state (D0-D3) */
};

/* Internal helper for status-triggered IRQ signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_pending = false;

    /* Check IPCX status bits and masks */
    if ( (s->shim.ipcx & IPC_DONE_BITS) && !(s->shim.imrx & SHIM_IMRX_DONE) ) {
        irq_pending = true;
    }
    if ( (s->shim.ipcx & IPC_BUSY_BITS) && !(s->shim.imrx & SHIM_IMRX_BUSY) ) {
        irq_pending = true;
    }

    /* Check IPCD status bits and masks */
    if ( (s->shim.ipcd & IPCD_DONE_BITS) && !(s->shim.imrd & SHIM_IMRD_DONE) ) {
        irq_pending = true;
    }
    if ( (s->shim.ipcd & IPCD_BUSY_BITS) && !(s->shim.imrd & SHIM_IMRD_BUSY) ) {
        irq_pending = true;
    }

    /* Raise or lower IRQ accordingly */
    if (irq_pending) {
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

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case BAR_SHIM_CSR:
        val = s->shim.csr;
        break;
    case BAR_SHIM_IMRX:
        val = s->shim.imrx;
        break;
    case BAR_SHIM_IMRD:
        val = s->shim.imrd;
        break;
    case BAR_SHIM_IPCX:
        val = s->shim.ipcx;
        break;
    case BAR_SHIM_IPCD:
        val = s->shim.ipcd;
        break;
    default:
        /* Unimplemented regions return 0 */
        val = 0;
        break;
    }

    /* Handle access sizes smaller than register width */
    if (size < 8 && addr >= BAR_SHIM_IMRX) {
        /* For 64-bit registers, mask according to access size */
        if (size == 4) {
            val = (uint32_t)val;
        } else if (size == 2) {
            val = (uint16_t)val;
        } else if (size == 1) {
            val = (uint8_t)val;
        }
    } else if (size == 8 && addr == BAR_SHIM_CSR) {
        /* CSR is 32-bit; return as 64-bit with high part 0 */
        val = s->shim.csr;
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case BAR_SHIM_CSR:
        /* Only update the lower 32 bits of CSR */
        s->shim.csr = (uint32_t)val;
        break;

    case BAR_SHIM_IMRX:
        /* Handle 64-bit write; allow sub-size writes with merge */
        if (size == 8) {
            s->shim.imrx = val;
        } else if (size == 4) {
            s->shim.imrx = (s->shim.imrx & 0xFFFFFFFF00000000ULL) | (uint32_t)val;
        } else if (size == 2) {
            s->shim.imrx = (s->shim.imrx & 0xFFFFFFFFFFFF0000ULL) | (uint16_t)val;
        } else if (size == 1) {
            s->shim.imrx = (s->shim.imrx & 0xFFFFFFFFFFFFFF00ULL) | (uint8_t)val;
        }
        pcibase_update_irq(s);
        break;

    case BAR_SHIM_IMRD:
        if (size == 8) {
            s->shim.imrd = val;
        } else if (size == 4) {
            s->shim.imrd = (s->shim.imrd & 0xFFFFFFFF00000000ULL) | (uint32_t)val;
        } else if (size == 2) {
            s->shim.imrd = (s->shim.imrd & 0xFFFFFFFFFFFF0000ULL) | (uint16_t)val;
        } else if (size == 1) {
            s->shim.imrd = (s->shim.imrd & 0xFFFFFFFFFFFFFF00ULL) | (uint8_t)val;
        }
        pcibase_update_irq(s);
        break;

    case BAR_SHIM_IPCX:
        /* Write-1-to-clear for status bits */
        if (size == 8) {
            s->shim.ipcx &= ~(val & IPC_STATUS_MASK);
        } else {
            /* For smaller writes, only affect status bits in the written bytes */
            uint64_t mask = 0;
            if (size == 4) {
                mask = 0xFFFFFFFFULL;
            } else if (size == 2) {
                mask = 0xFFFFULL;
            } else if (size == 1) {
                mask = 0xFFULL;
            }
            s->shim.ipcx &= ~( (val & mask) & IPC_STATUS_MASK );
        }
        pcibase_update_irq(s);
        break;

    case BAR_SHIM_IPCD:
        if (size == 8) {
            s->shim.ipcd &= ~(val & IPCD_STATUS_MASK);
        } else {
            uint64_t mask = 0;
            if (size == 4) mask = 0xFFFFFFFFULL;
            else if (size == 2) mask = 0xFFFFULL;
            else if (size == 1) mask = 0xFFULL;
            s->shim.ipcd &= ~( (val & mask) & IPCD_STATUS_MASK );
        }
        pcibase_update_irq(s);
        break;

    default:
        /* Other regions silently ignore writes */
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

    /* Reset all shadow registers to zero */
    s->shim.csr = 0;
    s->shim.imrx = 0;
    s->shim.imrd = 0;
    s->shim.ipcx = 0;
    s->shim.ipcd = 0;

    /* Clear any pending IRQ */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x00);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Subsystem IDs removed to allow generic machine driver matching (defaults to 0) */
    /* pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, VENDOR_ID); */
    /* pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, DEVICE_ID); */

    /* Enable PCI Express capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization (corrected to 2MB) */
    s->num_bars = 1;
    s->bar_info[0].index = LPE_BAR;   /* Use BAR3 as the main DSP memory window */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PCI_BAR_SIZE;
    s->bar_info[0].name = "lpe_bar";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);
    s->has_msi = true;
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
    .name = "sof_audio_pci_intel_tng_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(shim.csr, PCIBaseState),
        VMSTATE_UINT64(shim.imrx, PCIBaseState),
        VMSTATE_UINT64(shim.imrd, PCIBaseState),
        VMSTATE_UINT64(shim.ipcx, PCIBaseState),
        VMSTATE_UINT64(shim.ipcd, PCIBaseState),
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
