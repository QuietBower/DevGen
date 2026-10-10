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
#include "qemu/bitops.h"
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
#include "hw/pci/pci_regs.h"

#define TYPE_PCIBASE_DEVICE "ae4dma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1022
#define DEVICE_ID 0x149B
#define CLASS_ID 0x0B4000
#define AE4_DMA_VERSION 4
#define MAX_AE4_HW_QUEUES 16
#define CMD_Q_LEN 32
#define AE4_MAX_IDX_OFF 0x08
#define AE4_Q_BASE_H_OFF 0x1c
#define AE4_Q_SZ 0x20
#define AE4_Q_BASE_L_OFF 0x18
#define AE4_RD_IDX_OFF 0x0c
#define AE4_WR_IDX_OFF 0x10
#define SUPPORTED_INTERRUPTS (INT_COMPLETION | INT_ERROR)
#define INT_COMPLETION BIT(0)
#define INT_ERROR BIT(1)
#define AE4_TIME_OUT 5000
#define AE4_DESC_COMPLETED 0x03
#define Q_DESC_SIZE sizeof(struct ptdma_desc)
#define MAX_CMD_QLEN 100
#define CMD_DESC_DW0_VAL 0x500012
#define CMD_AE4_DESC_DW0_VAL 2
#define DWORD0_SOC BIT(0)
#define DWORD0_IOC BIT(1)
#define CMD_Q_RUN BIT(0)
#define PT_ENGINE_PASSTHRU 5
#define CMD_Q_ERROR(__qs) ((__qs) & 0x0000003f)
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

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t q_control;

    /* Command queue specific registers */
    uint32_t queue_max_idx;
    uint32_t queue_intr_enable;
    uint32_t queue_wr_idx;
    uint32_t queue_rd_idx;
    uint32_t queue_base_lo;
    uint32_t queue_base_hi;

    /* DMA Context */
    struct {
        dma_addr_t src;
        dma_addr_t dst;
        uint64_t count;
        uint32_t status;
    } dma;

    /* Interrupt state */
    uint32_t intr_enable;
};

/* Hardware descriptor structures */
struct dword3 {
    unsigned int  src_hi:16;
    unsigned int  src_mem:2;
    unsigned int  lsb_cxt_id:8;
    unsigned int  rsvd1:5;
    unsigned int  fixed:1;
};

struct dword5 {
    unsigned int  dst_hi:16;
    unsigned int  dst_mem:2;
    unsigned int  rsvd1:13;
    unsigned int  fixed:1;
};

struct ptdma_desc {
    uint32_t dw0;
    uint32_t length;
    uint32_t src_lo;
    struct dword3 dw3;
    uint32_t dst_lo;
    struct dword5 dw5;
    __le32 rsvd1;
    __le32 rsvd2;
};

union dwou {
    uint32_t dw0;
    struct dword0 {
        uint8_t  byte0;
        uint8_t  byte1;
        uint16_t timestamp;
    } dws;
};

struct dword1 {
    uint8_t  status;
    uint8_t  err_code;
    uint16_t desc_id;
};

struct ae4dma_desc {
    union dwou dwouv;
    struct dword1 dw1;
    uint32_t length;
    uint32_t rsvd;
    uint32_t src_hi;
    uint32_t src_lo;
    uint32_t dst_hi;
    uint32_t dst_lo;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->intr_status & s->queue_intr_enable;
    if (active) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x20) {
        return 0;
    }
    switch (addr) {
    case 0x00:
        val = AE4_DMA_VERSION;
        break;
    case 0x08:
        val = s->queue_max_idx;
        break;
    case 0x0c:
        val = s->queue_intr_enable;
        break;
    case 0x10:
        val = s->queue_wr_idx;
        break;
    case 0x14:
        val = s->queue_rd_idx;
        break;
    case 0x18:
        val = s->queue_base_lo;
        break;
    case 0x1c:
        val = s->queue_base_hi;
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
    if (addr >= 0x20) {
        return;
    }
    switch (addr) {
    case 0x08:
        s->queue_max_idx = val;
        break;
    case 0x0c:
        s->queue_intr_enable = val;
        pcibase_update_irq(s);
        break;
    case 0x10:
        s->queue_wr_idx = val;
        break;
    case 0x14:
        /* read index is hardware-updated; ignore driver writes */
        break;
    case 0x18:
        s->queue_base_lo = val;
        break;
    case 0x1c:
        s->queue_base_hi = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
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

    s->queue_max_idx = CMD_Q_LEN - 1;
    s->queue_intr_enable = 0;
    s->queue_wr_idx = 0;
    s->queue_rd_idx = 0;
    s->queue_base_lo = 0;
    s->queue_base_hi = 0;
    s->intr_status = 0;
    s->dma.src = 0;
    s->dma.dst = 0;
    s->dma.count = 0;
    s->dma.status = 0;
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
    /* Set class code: prog-if 0x00, subclass 0x40, base class 0x0b */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x4000);
    pci_set_byte(pci_conf + PCI_CLASS_DEVICE + 2, 0x0b);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "ae4dma-mmio"
    };
    s->bar_info[1] = (BARInfo){
        .index = 2,
        .type = BAR_TYPE_RAM,
        .size = 0x1000,
        .name = "ae4dma-msix"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Add MSI support */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    /* Add MSI-X support: table at offset 0, PBA at offset 0x800, cap position 0 */
    if (msix_init(pdev, MAX_AE4_HW_QUEUES,
                  &s->bar_regions[2], 2, 0,
                  &s->bar_regions[2], 2, 0x800,
                  0, errp)) {
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ae4dma_pci",
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