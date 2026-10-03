/*
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

#define TYPE_PCIBASE_DEVICE "icom_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI IDs - actual values needed; see needed_sources */
#define PCI_VENDOR_ID_IBM 0x1014 /* placeholder */
#define PCI_DEVICE_ID_IBM_ICOM_DEV_ID_1 0x0031
#define PCI_CLASS_ID 0x070002 /* placeholder, likely serial */

/* BAR0 size unknown; see needed_sources */
#define ICOM_BAR0_SIZE 0x10000

/* ICOM IRAM region */
#define ICOM_IRAM_OFFSET 0x1000
#define ICOM_IRAM_SIZE   0x0C00

/* Newly provided interrupt and command constants */
#define INT_XMIT_COMPLETED           0x2000
#define INT_XMIT_DISABLED            0x0200
#define INT_RCV_COMPLETED            0x1000

#define START_XMIT                   0x80
#define START_DOWNLOAD               0x80

#define ICOM_HDW_ACTIVE 0x01

#define ICOM_CABLE_ID_VALID          0x01
#define ICOM_CABLE_ID_MASK           0xF0

/* Hardware register structures */
struct icom_regs {
    uint32_t control;
    uint32_t interrupt;
    uint32_t int_mask;
    uint32_t int_pri;
    uint32_t int_reg_b;
    uint32_t resvd01;
    uint32_t resvd02;
    uint32_t resvd03;
    uint32_t control_2;
    uint32_t interrupt_2;
    uint32_t int_mask_2;
    uint32_t int_pri_2;
    uint32_t int_reg_2b;
} QEMU_PACKED;

struct func_dram {
    uint32_t reserved[108];
    uint32_t RcvStatusAddr;
    uint8_t RcvStnAddr;
    uint8_t IdleState;
    uint8_t IdleMonitor;
    uint8_t FlagFillIdleTimer;
    uint32_t XmitStatusAddr;
    uint8_t StartXmitCmd;
    uint8_t HDLCConfigReg;
    uint8_t CauseCode;
    uint8_t xchar;
    uint32_t reserved3;
    uint8_t PrevCmdReg;
    uint8_t CmdReg;
    uint8_t async_config2;
    uint8_t async_config3;
    uint8_t dce_resvd[20];
    uint8_t dce_resvd21;
    uint8_t misc_flags;
    uint8_t call_length;
    uint8_t call_length2;
    uint32_t call_addr;
    uint16_t timer_value;
    uint8_t timer_command;
    uint8_t dce_command;
    uint8_t dce_cmd_status;
    uint8_t x21_r1_ioff;
    uint8_t x21_r0_ioff;
    uint8_t x21_ralt_ioff;
    uint8_t x21_r1_ion;
    uint8_t rsvd_ier;
    uint8_t ier;
    uint8_t isr;
    uint8_t osr;
    uint8_t reset;
    uint8_t disable;
    uint8_t sync;
    uint8_t error_stat;
    uint8_t cable_id;
    uint8_t cs_length;
    uint8_t mac_length;
    uint32_t cs_load_addr;
    uint32_t mac_load_addr;
} QEMU_PACKED;

struct statusArea {
    struct xmit_status_area {
        uint32_t leNext;
        uint32_t leNextASD;
        uint32_t leBuffer;
        uint16_t leLengthASD;
        uint16_t leOffsetASD;
        uint16_t leLength;
        uint16_t flags;
    } xmit[1];
    struct {
        uint32_t leNext;
        uint32_t leNextASD;
        uint32_t leBuffer;
        uint16_t WorkingLength;
        uint16_t reserv01;
        uint16_t leLength;
        uint16_t flags;
    } rcv[2];
} QEMU_PACKED;

#define SA_FLAGS_DONE           0x0080
#define SA_FLAGS_CONTINUED      0x8000
#define SA_FLAGS_IDLE           0x4000
#define SA_FLAGS_READY_TO_XMIT  0x0800
#define SA_FLAGS_STAT_MASK      0x007F
#define SA_FL_RCV_DONE           0x0010
#define SA_FLAGS_OVERRUN         0x0040
#define SA_FLAGS_PARITY_ERROR    0x0080
#define SA_FLAGS_FRAME_ERROR     0x0001
#define SA_FLAGS_FRAME_TRUNC     0x0002
#define SA_FLAGS_BREAK_DET       0x0004
#define SA_FLAGS_RCV_MASK        0xFFE6

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
    uint32_t intr2_status;
    uint32_t intr2_mask;

    struct icom_regs global_reg;
    struct func_dram dram[4];

    /* DMA context */
    /* operational status */
    /* reset state */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending;

    /* Compute pending interrupts from interrupt status and mask registers */
    pending = (s->global_reg.interrupt & ~s->global_reg.int_mask) |
              (s->global_reg.interrupt_2 & ~s->global_reg.int_mask_2);

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void icom_handle_xmit(PCIBaseState *s, int port)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct func_dram *dram = &s->dram[port];
    dma_addr_t xmit_addr, buf_addr;
    struct xmit_status_area desc;
    uint8_t data[4096];
    int len;

    /* Read XmitStatusAddr from DRAM */
    xmit_addr = dram->XmitStatusAddr;
    if (!xmit_addr) {
        return;
    }

    /* Read the descriptor */
    pci_dma_read(pdev, xmit_addr, &desc, sizeof(desc));

    if (le16_to_cpu(desc.flags) & SA_FLAGS_READY_TO_XMIT) {
        len = le16_to_cpu(desc.leLength);
        if (len > 4096) {
            len = 4096;
        }
        /* Read data from buffer */
        buf_addr = le32_to_cpu(desc.leBuffer);
        pci_dma_read(pdev, buf_addr, data, len);

        /* Set DONE flag */
        desc.flags = cpu_to_le16(SA_FLAGS_DONE);
        pci_dma_write(pdev, xmit_addr + offsetof(struct xmit_status_area, flags),
                      &desc.flags, sizeof(desc.flags));

        /* Set transmit complete interrupt bit */
        if (port < 2) {
            if (port == 0) {
                s->global_reg.interrupt |= INT_XMIT_COMPLETED;
            } else {
                s->global_reg.interrupt |= (INT_XMIT_COMPLETED << 16);
            }
        } else {
            if (port == 2) {
                s->global_reg.interrupt_2 |= INT_XMIT_COMPLETED;
            } else {
                s->global_reg.interrupt_2 |= (INT_XMIT_COMPLETED << 16);
            }
        }
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int port;
    uint32_t offset;

    /* Determine which region */
    if (addr < 0x8000) {
        /* Port DRAM or global V1 regs */
        port = addr >> 13; /* 0x2000 per port */
        if (port < 4 && (addr & 0x1FFF) < 0x1000) {
            /* DRAM region (first 4KB of port) */
            offset = addr & 0xFFF;
            if (offset + size > sizeof(struct func_dram)) {
                return ~0ULL;
            }
            switch (size) {
            case 1:
                val = *(uint8_t *)((uint8_t *)&s->dram[port] + offset);
                break;
            case 2:
                val = *(uint16_t *)((uint16_t *)((uint8_t *)&s->dram[port] + offset));
                break;
            case 4:
                val = *(uint32_t *)((uint32_t *)((uint8_t *)&s->dram[port] + offset));
                break;
            default:
                return ~0ULL;
            }
            return val;
        } else if (addr >= 0x4000 && addr < 0x4000 + sizeof(struct icom_regs)) {
            /* Global registers for V1 */
            offset = addr - 0x4000;
            goto global_reg_read;
        }
        return ~0ULL;
    } else if (addr >= 0x8000 && addr < 0x8000 + sizeof(struct icom_regs)) {
        /* Global registers for V2 */
        offset = addr - 0x8000;
global_reg_read:
        if (offset + size > sizeof(struct icom_regs)) {
            return ~0ULL;
        }
        switch (size) {
        case 4:
            val = *(uint32_t *)((uint8_t *)&s->global_reg + offset);
            break;
        default:
            return ~0ULL;
        }
        return val;
    }
    return ~0ULL;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port;
    uint32_t offset;

    /* Determine which region */
    if (addr < 0x8000) {
        /* Port DRAM or global V1 regs */
        port = addr >> 13; /* 0x2000 per port */
        if (port < 4 && (addr & 0x1FFF) < 0x1000) {
            /* DRAM region */
            offset = addr & 0xFFF;
            if (offset + size > sizeof(struct func_dram)) {
                return;
            }
            switch (size) {
            case 1:
                *(uint8_t *)((uint8_t *)&s->dram[port] + offset) = val;
                break;
            case 2:
                *(uint16_t *)((uint16_t *)((uint8_t *)&s->dram[port] + offset)) = val;
                break;
            case 4:
                *(uint32_t *)((uint32_t *)((uint8_t *)&s->dram[port] + offset)) = val;
                break;
            }
            /* Check for trigger registers */
            if (offset == offsetof(struct func_dram, StartXmitCmd) && val == START_XMIT) {
                icom_handle_xmit(s, port);
            } else if (offset == offsetof(struct func_dram, sync) && val == START_DOWNLOAD) {
                /* Simulate firmware download completion */
                s->dram[port].misc_flags |= ICOM_HDW_ACTIVE;
                s->dram[port].cable_id = (ICOM_CABLE_ID_VALID | (ICOM_CABLE_ID_MASK & (0 << 4)));
            }
        } else if (addr >= 0x4000 && addr < 0x4000 + sizeof(struct icom_regs)) {
            /* Global registers for V1 */
            offset = addr - 0x4000;
            goto global_reg_write;
        }
        return;
    } else if (addr >= 0x8000 && addr < 0x8000 + sizeof(struct icom_regs)) {
        /* Global registers for V2 */
        offset = addr - 0x8000;
global_reg_write:
        if (offset + size > sizeof(struct icom_regs)) {
            return;
        }
        switch (size) {
        case 4:
            /* Handle W1C for interrupt status registers */
            if (offset == offsetof(struct icom_regs, interrupt)) {
                s->global_reg.interrupt &= ~val;
            } else if (offset == offsetof(struct icom_regs, interrupt_2)) {
                s->global_reg.interrupt_2 &= ~val;
            } else {
                *(uint32_t *)((uint8_t *)&s->global_reg + offset) = val;
            }
            break;
        }
        /* After any global reg write, update IRQ */
        pcibase_update_irq(s);
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
    memset(&s->global_reg, 0, sizeof(s->global_reg));
    memset(s->dram, 0, sizeof(s->dram));
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1014);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0031);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0702);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = ICOM_BAR0_SIZE;
    s->bar_info[0].name = "icom-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not used by driver */

    /* DMA configuration will be implemented in Phase 2 */
    /* Timer configuration will be implemented in Phase 2 */
    /* Final state initialization will be implemented in Phase 2 */
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
    /* Additional uninit will be implemented in Phase 2 */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "icom_pci",
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
