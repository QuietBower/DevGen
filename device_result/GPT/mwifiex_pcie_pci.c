/*
 * QEMU PCI device model for Marvell mwifiex PCIe (minimal emulation
 * sufficient for Linux mwifiex_pcie driver probe and basic operation).
 *
 * This is derived from a generic PCI template and implements only the
 * register-level behavior that is explicitly visible in the mwifiex
 * Linux driver. No undocumented behavior is modeled.
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

#include <stdint.h>

#define TYPE_PCIBASE_DEVICE "mwifiex_pcie_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* IDs from driver (first pci_device_id entry) */
#define MWIFIEX_PCIE_VENDOR_ID_MARVELL   0x11ab
#define MWIFIEX_PCIE_DEVICE_ID_88W8766P  0x2b30
#define MWIFIEX_PCIE_CLASS_NETWORK       PCI_CLASS_NETWORK_OTHER

/* Register offsets used by driver (BAR2 MMIO space) */
#define MWIFIEX_PCIE_HOST_INT_MASK        0x0C34
#define MWIFIEX_PCIE_HOST_INT_STATUS      0x0C30
#define MWIFIEX_PCIE_HOST_INT_STATUS_MASK 0x0C3C
#define MWIFIEX_PCIE_CPU_INT_EVENT        0x0C18
#define MWIFIEX_PCIE_CPU_INT_STATUS       0x0C1C

#define MWIFIEX_PCIE_SCRATCH_0_REG        0x0C10
#define MWIFIEX_PCIE_SCRATCH_1_REG        0x0C14
#define MWIFIEX_PCIE_SCRATCH_2_REG        0x0C40
#define MWIFIEX_PCIE_SCRATCH_3_REG        0x0C44
#define MWIFIEX_PCIE_SCRATCH_4_REG        0x0CD0
#define MWIFIEX_PCIE_SCRATCH_5_REG        0x0CD4
#define MWIFIEX_PCIE_SCRATCH_6_REG        0x0CD8
#define MWIFIEX_PCIE_SCRATCH_7_REG        0x0CDC
#define MWIFIEX_PCIE_SCRATCH_8_REG        0x0CE0
#define MWIFIEX_PCIE_SCRATCH_9_REG        0x0CE4
#define MWIFIEX_PCIE_SCRATCH_10_REG       0x0CE8
#define MWIFIEX_PCIE_SCRATCH_11_REG       0x0CEC
#define MWIFIEX_PCIE_SCRATCH_12_REG       0x0CF0
#define MWIFIEX_PCIE_SCRATCH_13_REG       0x0CF4
#define MWIFIEX_PCIE_SCRATCH_14_REG       0x0CF8
#define MWIFIEX_PCIE_SCRATCH_15_REG       0x0CFC

#define MWIFIEX_PCIE_WR_DATA_PTR_Q0_Q1    0x0C05C
#define MWIFIEX_PCIE_RD_DATA_PTR_Q0_Q1    0x0C08C

/* Interrupt bits as used in driver (HOST side) */
#define MWIFIEX_PCIE_HOST_INTR_DNLD_DONE   (1u << 0)
#define MWIFIEX_PCIE_HOST_INTR_UPLD_RDY    (1u << 1)
#define MWIFIEX_PCIE_HOST_INTR_CMD_DONE    (1u << 2)
#define MWIFIEX_PCIE_HOST_INTR_EVENT_RDY   (1u << 3)

/* CPU interrupt/event bits written by driver */
#define MWIFIEX_PCIE_CPU_INTR_DNLD_RDY         (1u << 0)
#define MWIFIEX_PCIE_CPU_INTR_DOOR_BELL        (1u << 1)
#define MWIFIEX_PCIE_CPU_INTR_SLEEP_CFM_DONE   (1u << 2)
#define MWIFIEX_PCIE_CPU_INTR_EVENT_DONE       (1u << 5)

/* Firmware ready value used in scratch & fw_status */
#define MWIFIEX_FIRMWARE_READY_PCIE 0xF00DFACEu

/* Host interrupt mask the driver writes (value not strictly important
 * as long as non-zero bits enable raising of the corresponding events).
 * The driver uses a constant HOST_INTR_MASK; we mimic a reasonable mask.
 */
#define MWIFIEX_HOST_INTR_MASK_ALL ( \
    MWIFIEX_PCIE_HOST_INTR_DNLD_DONE | \
    MWIFIEX_PCIE_HOST_INTR_UPLD_RDY   | \
    MWIFIEX_PCIE_HOST_INTR_CMD_DONE   | \
    MWIFIEX_PCIE_HOST_INTR_EVENT_RDY)

/* BAR description */
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
    uint32_t intr_status;     /* current host interrupt status */
    uint32_t intr_mask;       /* host interrupt enable mask */

    /* Hardware Register Shadows */
    uint32_t scratch[16];     /* SCRATCH_0..15 */

    /* Extra register shadows used by driver */
    uint32_t host_int_status_mask_reg; /* PCIE_HOST_INT_STATUS_MASK */
    uint32_t cpu_int_event_reg;        /* last value written to CPU_INT_EVENT */
    uint32_t cpu_int_status_reg;       /* CPU_INT_STATUS mirror */

    uint32_t tx_wrptr_reg;    /* reg->tx_wrptr */
    uint32_t tx_rdptr_reg;    /* reg->tx_rdptr */
    uint32_t rx_wrptr_reg;    /* reg->rx_wrptr */
    uint32_t rx_rdptr_reg;    /* reg->rx_rdptr */
    uint32_t evt_wrptr_reg;   /* reg->evt_wrptr */
    uint32_t evt_rdptr_reg;   /* reg->evt_rdptr */

    /* Track whether firmware has been "downloaded" so that fw_status
     * reads behave as the driver expects. */
    bool fw_downloaded;
};

/* Helper to compute power-of-two BAR size */
/* Renamed from pow2ceil to avoid conflict with any existing symbol */
static hwaddr pcibase_pow2ceil(hwaddr size)
{
    if (!size) {
        return 0;
    }
    return 1ULL << (64 - clz64(size - 1));
}

/* IRQ update based on intr_status & intr_mask */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if ((s->intr_status & s->intr_mask) != 0) {
        /* Raise legacy INTx; driver may switch to MSI but still
         * expects an interrupt when host_int_status is non-zero.
         */
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* All registers are 32-bit accessed via ioread32/iowrite32 in driver */
    if (size != 4) {
        return 0xFFFFFFFFu;
    }

    switch (addr) {
    case MWIFIEX_PCIE_HOST_INT_STATUS:
        /* Driver reads interrupt cause; we return current status. */
        val = s->intr_status;
        break;
    case MWIFIEX_PCIE_HOST_INT_MASK:
        val = s->intr_mask;
        break;
    case MWIFIEX_PCIE_HOST_INT_STATUS_MASK:
        val = s->host_int_status_mask_reg;
        break;
    case MWIFIEX_PCIE_CPU_INT_STATUS:
        val = s->cpu_int_status_reg;
        break;

    /* Scratch registers used for command+data exchange and fw status */
    case MWIFIEX_PCIE_SCRATCH_0_REG:
        val = s->scratch[0];
        break;
    case MWIFIEX_PCIE_SCRATCH_1_REG:
        val = s->scratch[1];
        break;
    case MWIFIEX_PCIE_SCRATCH_2_REG:
        val = s->scratch[2];
        break;
    case MWIFIEX_PCIE_SCRATCH_3_REG:
        /* fw_status: advertise firmware ready once fw_downloaded is true */
        if (s->fw_downloaded) {
            val = MWIFIEX_FIRMWARE_READY_PCIE;
        } else {
            val = s->scratch[3];
        }
        break;
    case MWIFIEX_PCIE_SCRATCH_4_REG:
        val = s->scratch[4];
        break;
    case MWIFIEX_PCIE_SCRATCH_5_REG:
        val = s->scratch[5];
        break;
    case MWIFIEX_PCIE_SCRATCH_6_REG:
        val = s->scratch[6];
        break;
    case MWIFIEX_PCIE_SCRATCH_7_REG:
        val = s->scratch[7];
        break;
    case MWIFIEX_PCIE_SCRATCH_8_REG:
        val = s->scratch[8];
        break;
    case MWIFIEX_PCIE_SCRATCH_9_REG:
        val = s->scratch[9];
        break;
    case MWIFIEX_PCIE_SCRATCH_10_REG:
        val = s->scratch[10];
        break;
    case MWIFIEX_PCIE_SCRATCH_11_REG:
        val = s->scratch[11];
        break;
    case MWIFIEX_PCIE_SCRATCH_12_REG:
        val = s->scratch[12];
        break;
    case MWIFIEX_PCIE_SCRATCH_13_REG:
        val = s->scratch[13];
        break;
    case MWIFIEX_PCIE_SCRATCH_14_REG:
        val = s->scratch[14];
        break;
    case MWIFIEX_PCIE_SCRATCH_15_REG:
        val = s->scratch[15];
        break;

    /* Pointer/registers used for rings, per mwifiex_reg_xxx tables */
    case MWIFIEX_PCIE_WR_DATA_PTR_Q0_Q1:
        val = s->tx_wrptr_reg;
        break;
    case MWIFIEX_PCIE_RD_DATA_PTR_Q0_Q1:
        val = s->tx_rdptr_reg;
        break;

    default:
        /* For unimplemented registers, return all-ones to avoid errors. */
        val = 0xFFFFFFFFu;
        break;
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t data,
                               unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = (uint32_t)data;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case MWIFIEX_PCIE_HOST_INT_MASK:
        /* enable/disable bits that can raise host interrupt */
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;

    case MWIFIEX_PCIE_HOST_INT_STATUS:
        /* Driver writes ~pcie_ireg to clear bits (W1C style). */
        {
            uint32_t clear_mask = ~val;
            s->intr_status &= ~clear_mask;
            pcibase_update_irq(s);
        }
        break;

    case MWIFIEX_PCIE_HOST_INT_STATUS_MASK:
        s->host_int_status_mask_reg = val;
        break;

    case MWIFIEX_PCIE_CPU_INT_EVENT:
        /* Driver rings doorbell and other CPU events via this register. */
        s->cpu_int_event_reg = val;

        /* Reflect into CPU_INT_STATUS mirror; bits are cleared when the
         * driver polls CPU_INT_STATUS in the firmware download logic.
         */
        s->cpu_int_status_reg |= val;

        /* When doorbell is set during firmware download, firmware will
         * eventually assert HOST_INTR_CMD_DONE to signal completion.
         * When DNLD_RDY is written, we assert HOST_INTR_DNLD_DONE.
         * When EVENT_DONE/SLEEP_CFM_DONE are written, we assert
         * EVENT_RDY/CMD_DONE as needed. For simplicity we treat all
         * of them as "command done" or "download done" interrupts.
         */
        if (val & MWIFIEX_PCIE_CPU_INTR_DOOR_BELL) {
            s->intr_status |= MWIFIEX_PCIE_HOST_INTR_CMD_DONE;
        }
        if (val & MWIFIEX_PCIE_CPU_INTR_DNLD_RDY) {
            s->intr_status |= MWIFIEX_PCIE_HOST_INTR_DNLD_DONE;
        }
        if (val & MWIFIEX_PCIE_CPU_INTR_EVENT_DONE) {
            s->intr_status |= MWIFIEX_PCIE_HOST_INTR_EVENT_RDY;
        }
        if (val & MWIFIEX_PCIE_CPU_INTR_SLEEP_CFM_DONE) {
            s->intr_status |= MWIFIEX_PCIE_HOST_INTR_CMD_DONE;
        }

        pcibase_update_irq(s);
        break;

    case MWIFIEX_PCIE_CPU_INT_STATUS:
        /* Allow driver to clear CPU_INT_STATUS bits by writing a mask. */
        s->cpu_int_status_reg &= ~val;
        break;

    /* Scratch registers and fw_status / drv_rdy protocol */
    case MWIFIEX_PCIE_SCRATCH_0_REG:
        s->scratch[0] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_1_REG:
        s->scratch[1] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_2_REG:
        s->scratch[2] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_3_REG:
        /* fw_status: let driver write here; read path overrides with
         * FIRMWARE_READY_PCIE when fw_downloaded is true.
         */
        s->scratch[3] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_4_REG:
        s->scratch[4] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_5_REG:
        s->scratch[5] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_6_REG:
        s->scratch[6] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_7_REG:
        s->scratch[7] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_8_REG:
        s->scratch[8] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_9_REG:
        s->scratch[9] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_10_REG:
        s->scratch[10] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_11_REG:
        s->scratch[11] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_12_REG:
        /* drv_rdy: driver writes FIRMWARE_READY_PCIE here during
         * mwifiex_check_fw_status(). We simply store the value.
         */
        s->scratch[12] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_13_REG:
        /* Used for FLR and fw_dump_ctrl in some chips; keep value. */
        s->scratch[13] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_14_REG:
        s->scratch[14] = val;
        break;
    case MWIFIEX_PCIE_SCRATCH_15_REG:
        s->scratch[15] = val;
        break;

    /* Ring pointer registers for 8766/8897/8997 families */
    case MWIFIEX_PCIE_WR_DATA_PTR_Q0_Q1:
        s->tx_wrptr_reg = val;
        break;
    case MWIFIEX_PCIE_RD_DATA_PTR_Q0_Q1:
        s->tx_rdptr_reg = val;
        break;

    default:
        /* Ignore writes to unknown offsets */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Device does not expose PIO to the driver; return ~0. */
    (void)opaque;
    (void)addr;
    switch (size) {
    case 1:  return 0xFFu;       
    case 2:  return 0xFFFFu;     
    default: return 0xFFFFFFFFu; 
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)data;
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
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    /* Reset internal register state */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->host_int_status_mask_reg = 0;
    s->cpu_int_event_reg = 0;
    s->cpu_int_status_reg = 0;
    s->tx_wrptr_reg = 0;
    s->tx_rdptr_reg = 0;
    s->rx_wrptr_reg = 0;
    s->rx_rdptr_reg = 0;
    s->evt_wrptr_reg = 0;
    s->evt_rdptr_reg = 0;
    s->fw_downloaded = false;

    memset(s->scratch, 0, sizeof(s->scratch));

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                 BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pcibase_pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, aligned_size);
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

    /* PCI config space */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  MWIFIEX_PCIE_VENDOR_ID_MARVELL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  MWIFIEX_PCIE_DEVICE_ID_88W8766P);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, MWIFIEX_PCIE_CLASS_NETWORK);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Initialize simple PM capability */
    {
        int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0,
                                        PCI_PM_SIZEOF, errp);
        if (pm_pos > 0) {
            pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
        }
    }

    /* 1. 首先，安全起见，将所有 6 个 BAR 初始化为 NONE 状态 */
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    /* 2. 同时配置驱动需要的 BAR0 和 BAR2 */
    s->num_bars = 2;

    // 配置 BAR0
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 1 * MiB;
    s->bar_info[0].name  = "mwifiex-pcie-bar0";

    // 配置 BAR2
    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_MMIO;
    s->bar_info[2].size  = 1 * MiB;
    s->bar_info[2].name  = "mwifiex-pcie-bar2";

    /* 3. 依次注册这两个 BAR */
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
    pcibase_register_bar(pdev, s, &s->bar_info[2], errp);

    /* Initialize state */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->host_int_status_mask_reg = 0;
    s->cpu_int_event_reg = 0;
    s->cpu_int_status_reg = 0;
    s->tx_wrptr_reg = 0;
    s->tx_rdptr_reg = 0;
    s->rx_wrptr_reg = 0;
    s->rx_rdptr_reg = 0;
    s->evt_wrptr_reg = 0;
    s->evt_rdptr_reg = 0;
    s->fw_downloaded = true;  /* Pretend firmware is already active */

    memset(s->scratch, 0, sizeof(s->scratch));

    /* Enable MSI if desired (the real device can use MSI/MSI-X, but the
     * driver also works with legacy INTx; we keep MSI disabled by
     * default and rely on INTx).
     */
    s->has_msi  = false;
    s->has_msix = false;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No dynamic resources beyond standard PCI cleanup. */
    (void)pdev;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "mwifiex_pcie_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
        VMSTATE_UINT32_ARRAY(scratch, PCIBaseState, 16),
        VMSTATE_BOOL(fw_downloaded, PCIBaseState),
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
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_class_init,
        .interfaces    = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)

