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


#define TYPE_PCIBASE_DEVICE "ccp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1022
#define DEVICE_ID 0x1537
#define CLASS_ID  0x1080

#define MSIX_VECTORS 2
#define MAX_HW_QUEUES 5

#define CMD5_REQID_CONFIG_OFFSET 0x08
#define CMD5_AES_MASK_OFFSET 0x6010
#define LSB_PRIVATE_MASK_LO_OFFSET 0x20
#define LSB_PRIVATE_MASK_HI_OFFSET 0x24
#define CMD5_CMD_TIMEOUT_OFFSET 0x10
#define CMD5_CLK_GATE_CTL_OFFSET 0x603C
#define CMD5_TRNG_CTL_OFFSET 0x6008
#define CMD5_QUEUE_PRIO_OFFSET 0x04
#define TRNG_OUT_REG 0x00c
#define CMD5_CONFIG_0_OFFSET 0x6000
#define CMD5_QUEUE_MASK_OFFSET 0x00
#define CMD_Q_STATUS_BASE 0x210
#define CMD_Q_STATUS_INCR 0x20
#define CMD_Q_INT_STATUS_BASE 0x214
#define IRQ_STATUS_REG 0x200
#define Q_MASK_REG 0x000
#define CMD_Q_CACHE_BASE 0x228
#define CMD5_Q_HEAD_LO_BASE 0x0008
#define CMD5_Q_TAIL_LO_BASE 0x0004
#define CMD5_Q_INTERRUPT_STATUS_BASE 0x0010
#define LSB_PUBLIC_MASK_HI_OFFSET 0x1C
#define CMD5_Q_STATUS_INCR 0x1000
#define CMD5_Q_DMA_STATUS_BASE 0x0108
#define LSB_PUBLIC_MASK_LO_OFFSET 0x18
#define CMD5_Q_DMA_WRITE_STATUS_BASE 0x0110
#define CMD5_Q_STATUS_BASE 0x0100
#define CMD5_Q_INT_STATUS_BASE 0x0104
#define CMD5_Q_INT_ENABLE_BASE 0x000C
#define CMD5_Q_DMA_READ_STATUS_BASE 0x010C
#define IRQ_MASK_REG 0x040
#define DEL_CMD_Q_JOB 0x124
#define CMD_REQ0 0x180

struct dword0 {
    uint32_t soc:1;
    uint32_t ioc:1;
    uint32_t rsvd1:1;
    uint32_t init:1;
    uint32_t eom:1;
    uint32_t function:15;
    uint32_t engine:4;
    uint32_t prot:1;
    uint32_t rsvd2:7;
};

struct dword3 {
    uint32_t src_hi:16;
    uint32_t src_mem:2;
    uint32_t lsb_cxt_id:8;
    uint32_t rsvd1:5;
    uint32_t fixed:1;
};

union dword4 {
    uint32_t dst_lo;
    uint32_t sha_len_lo;
};

union dword5 {
    struct {
        uint32_t dst_hi:16;
        uint32_t dst_mem:2;
        uint32_t rsvd1:13;
        uint32_t fixed:1;
    } fields;
    uint32_t sha_len_hi;
};

struct dword7 {
    uint32_t key_hi:16;
    uint32_t key_mem:2;
    uint32_t rsvd1:14;
};

struct ccp5_desc {
    struct dword0 dw0;
    uint32_t length;
    uint32_t src_lo;
    struct dword3 dw3;
    union dword4 dw4;
    union dword5 dw5;
    uint32_t key_lo;
    struct dword7 dw7;
};

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
    uint32_t irq_status;
    uint32_t irq_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t trng_out;
    uint32_t q_mask;
    uint32_t cmd_req0;
    uint32_t del_cmd_q_job;

    /* DMA Context */
    dma_addr_t qbase_dma[MAX_HW_QUEUES];
    uint32_t qsize[MAX_HW_QUEUES];
    uint32_t q_head_lo[MAX_HW_QUEUES];
    uint32_t q_tail_lo[MAX_HW_QUEUES];

    uint32_t q_status[MAX_HW_QUEUES];
    uint32_t q_int_status[MAX_HW_QUEUES];
    uint32_t q_int_enable[MAX_HW_QUEUES];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x105ec: /* pspv1/pspv3 bootloader_info_reg */
    case 0x109ec: /* pspv2/pspv4/pspv5/pspv6/pspv7 bootloader_info_reg */
    case 0x109e8: /* teev1/teev2 info_reg */
        val = 0xffffffff; /* Default to all f's as per driver comment */
        break;
    default:
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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
    s->num_bars = 3;
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x1000; /* FIXME: Size not explicitly defined in provided source */
    s->bar_info[2].name = "ccp-bar2";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;
    if (msix_init(pdev, MSIX_VECTORS, &s->bar_regions[2], 2, 0, &s->bar_regions[2], 2, 0x800, 0, errp)) {
        s->has_msix = false;
    }
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
    .name = "ccp_pci",
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
