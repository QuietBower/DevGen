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

#define TYPE_PCIBASE_DEVICE "intel_ish_ipc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor/Device IDs (first entry in ish_pci_tbl) */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_ISH_CHV  0x22D8

/* BAR0 size – determined by highest register offset (0x368) rounded up to power of 2 */
#define ISH_BAR0_SIZE  0x400

/* Register offsets (all relative to IPC_REG_BASE = 0x0000) */
#define IPC_REG_BASE              0x0000
#define IPC_REG_PIMR_BXT          (IPC_REG_BASE + 0x08)
#define IPC_REG_ISH_HOST_FWSTS    (IPC_REG_BASE + 0x34)
#define IPC_REG_ISH_RMP2          (IPC_REG_BASE + 0x368)
#define IPC_REG_HOST_COMM         (IPC_REG_BASE + 0x38)
#define IPC_REG_ISH2HOST_DRBL     (IPC_REG_BASE + 0x54)
#define IPC_REG_ISH2HOST_MSG      (IPC_REG_BASE + 0x60)
#define IPC_REG_HOST2ISH_DRBL     (IPC_REG_BASE + 0x48)
#define IPC_REG_HOST2ISH_MSG      (IPC_REG_BASE + 0xE0)

/* Bit definitions for IPC registers */
#define IPC_ILUP_BIT              (1 << 0)
#define IPC_ILUP_OFFS             0
#define IPC_HOSTCOMM_READY_BIT    (1 << 7)
#define IPC_HOSTCOMM_READY_OFFS   7
#define IPC_PIMR_INT_EN_BIT_BXT   (1 << 0)
#define IPC_PIMR_INT_EN_OFFS_BXT  0
#define IPC_HOSTCOMM_INT_EN_BIT_CHV_AB  (1 << 31)
#define IPC_HOSTCOMM_INT_EN_OFFS_CHV_AB 31
#define IPC_HOST2ISH_BUSYCLEAR_MASK_BIT  (1 << 8)
#define IPC_HOST2ISH_BUSYCLEAR_MASK_OFFS_BXT 8
#define IPC_DRBL_BUSY_BIT         (1 << 31)
#define IPC_DRBL_BUSY_OFFS        31
#define IPC_HEADER_LENGTH_OFFSET  0
#define IPC_HEADER_LENGTH_MASK    0x03FF
#define IPC_HEADER_PROTOCOL_OFFSET 10
#define IPC_HEADER_PROTOCOL_MASK  0x0F
#define IPC_HEADER_MNG_CMD_OFFSET 16
#define IPC_HEADER_MNG_CMD_MASK   0x0F
#define IPC_FWSTS_DMA0            (1 << 16)
#define IPC_FWSTS_DMA1            (1 << 17)
#define IPC_FWSTS_DMA2            (1 << 18)
#define IPC_FWSTS_DMA3            (1 << 19)
#define IPC_ISH_IN_DMA            (IPC_FWSTS_DMA0 | IPC_FWSTS_DMA1 | IPC_FWSTS_DMA2 | IPC_FWSTS_DMA3)
#define IPC_RMP2_DMA_ENABLED      0x1

/* Header building helpers (driver macros) */
#define IPC_BUILD_HEADER(length, protocol, busy)        \
    (((busy) << IPC_DRBL_BUSY_OFFS) |                   \
     ((protocol) << IPC_HEADER_PROTOCOL_OFFSET) |       \
     ((length) << IPC_HEADER_LENGTH_OFFSET))
#define IPC_HEADER_GET_LENGTH(drbl_reg)                 \
    (((drbl_reg) >> IPC_HEADER_LENGTH_OFFSET) & IPC_HEADER_LENGTH_MASK)
#define IPC_HEADER_GET_PROTOCOL(drbl_reg)               \
    (((drbl_reg) >> IPC_HEADER_PROTOCOL_OFFSET) & IPC_HEADER_PROTOCOL_MASK)
#define IPC_HEADER_GET_MNG_CMD(drbl_reg)                \
    (((drbl_reg) >> IPC_HEADER_MNG_CMD_OFFSET) & IPC_HEADER_MNG_CMD_MASK)
#define IPC_BUILD_MNG_MSG(cmd, length)                  \
    (((1) << IPC_DRBL_BUSY_OFFS) |                      \
     ((IPC_PROTOCOL_MNG) << IPC_HEADER_PROTOCOL_OFFSET) | \
     ((cmd) << IPC_HEADER_MNG_CMD_OFFSET) |             \
     ((length) << IPC_HEADER_LENGTH_OFFSET))
#define IPC_IS_BUSY(drbl_reg)                           \
    (((drbl_reg) & IPC_DRBL_BUSY_BIT) == ((uint32_t)IPC_DRBL_BUSY_BIT))
#define IPC_PROTOCOL_ISHTP    1
#define IPC_PROTOCOL_MNG      3

/* Revision ID for Cherryview A0 (matching our device ID) */
#define REVISION_ID_CHT_A0    0x6

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

    /* Hardware Register Shadows – all MMIO space */
    uint8_t mmio[ISH_BAR0_SIZE];

    /* DMA structures not needed for probe */

    /* Power management state */
    uint8_t pm_state;  
    bool d0i3_active;

    /* IPC doorbell auto-response state */
    uint32_t ish2host_drbl;
    bool irq_pending;
};

/* Message structures used in IPC communication (for DMA buffers) */
struct time_sync_format {
    uint8_t ts1_source;
    uint8_t ts2_source;
    uint16_t reserved;
};

struct ipc_time_update_msg {
    uint64_t primary_host_time;
    struct time_sync_format sync_info;
    uint64_t secondary_host_time;
};

struct ipc_rst_payload_type {
    uint16_t reset_id;
    uint16_t reserved;
};

static void pcibase_write_mmio(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    if (addr + size > ISH_BAR0_SIZE) {
        return;
    }
    /* Simple little-endian write with size support */
    switch (size) {
    case 1:
        s->mmio[addr] = val & 0xFF;
        break;
    case 2:
        stw_le_p(s->mmio + addr, val);
        break;
    case 4:
        stl_le_p(s->mmio + addr, val);
        break;
    case 8:
        stq_le_p(s->mmio + addr, val);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_read_mmio(PCIBaseState *s, hwaddr addr, unsigned size)
{
    if (addr + size > ISH_BAR0_SIZE) {
        return 0;
    }
    switch (size) {
    case 1:
        return s->mmio[addr];
    case 2:
        return lduw_le_p(s->mmio + addr);
    case 4:
        return ldl_le_p(s->mmio + addr);
    case 8:
        return ldq_le_p(s->mmio + addr);
    default:
        return 0;
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Special handling for ISH2HOST doorbell when IRQ pending */
    if (addr == IPC_REG_ISH2HOST_DRBL && s->irq_pending) {
        return s->ish2host_drbl;
    }
    /* All known registers are 32-bit, but support 1/2/4/8 reads */
    return pcibase_read_mmio(s, addr, size);
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Protect read-only status registers */
    if (addr == IPC_REG_ISH_HOST_FWSTS) {
        /* FWSTS is a read-only status register; ignore writes */
        return;
    }

    /* Write to underlying storage first */
    pcibase_write_mmio(s, addr, val, size);

    /* Special handling for doorbell registers */
    if (size == 4) {
        if (addr == IPC_REG_HOST2ISH_DRBL) {
            /* Host writes command to doorbell; simulate ISH processing.
             * Clear the busy bit to indicate acceptance,
             * set a response on ISH2HOST doorbell with busy bit,
             * and raise interrupt.
             */
            uint32_t drbl_val = ldl_le_p(s->mmio + addr);
            drbl_val &= ~IPC_DRBL_BUSY_BIT;
            stl_le_p(s->mmio + addr, drbl_val);
            
            /* Build response: MNG message with cmd=0, length=0, busy set */
            s->ish2host_drbl = IPC_BUILD_MNG_MSG(0, 0);
            if (!s->irq_pending) {
                s->irq_pending = true;
                pci_set_irq(PCI_DEVICE(s), 1);
            }
        } else if (addr == IPC_REG_ISH2HOST_DRBL) {
            /* Host writes to clear busy bit or acknowledge */
            uint32_t drbl_val = ldl_le_p(s->mmio + addr);
            if (!(drbl_val & IPC_DRBL_BUSY_BIT)) {
                if (s->irq_pending) {
                    s->irq_pending = false;
                    pci_set_irq(PCI_DEVICE(s), 0);
                }
            }
        }
    }
}

/* PIO read handler (legacy support) – not used by this driver */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

/* PIO write handler (legacy support) – not used by this driver */
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

    memset(s->mmio, 0, sizeof(s->mmio));
    /* Set ILUP and HOSTCOMM_READY bits to indicate ISH is alive and ready,
     * also set DMA ready bits (FWSTS_DMA*). The driver polls for these.
     */
    stl_le_p(s->mmio + IPC_REG_ISH_HOST_FWSTS,
             IPC_ILUP_BIT | IPC_HOSTCOMM_READY_BIT | IPC_ISH_IN_DMA);
    /* Enable DMA in RMP2 */
    stl_le_p(s->mmio + IPC_REG_ISH_RMP2, IPC_RMP2_DMA_ENABLED);
    s->pm_state = 0;
    s->d0i3_active = false;
    
    /* Reset IPC doorbell state */
    s->ish2host_drbl = 0;
    s->irq_pending = false;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_ISH_CHV);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, REVISION_ID_CHT_A0);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = ISH_BAR0_SIZE,
        .name = "ish-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization */
    memset(s->mmio, 0, sizeof(s->mmio));
    stl_le_p(s->mmio + IPC_REG_ISH_HOST_FWSTS,
             IPC_ILUP_BIT | IPC_HOSTCOMM_READY_BIT | IPC_ISH_IN_DMA);
    stl_le_p(s->mmio + IPC_REG_ISH_RMP2, IPC_RMP2_DMA_ENABLED);
    s->pm_state = 0;  
    s->d0i3_active = false;

    /* IPC doorbell state */
    s->ish2host_drbl = 0;
    s->irq_pending = false;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

}

static const VMStateDescription vmstate_pcibase = {
    .name = "intel_ish_ipc_pci",
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
