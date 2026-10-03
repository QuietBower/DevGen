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

#define TYPE_PCIBASE_DEVICE "jmb38x_ms_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI vendor and device IDs extracted from driver */
#define VENDOR_ID 0x197B
#define DEVICE_ID 0x2383
#define CLASS_ID 0x0000

/* PCI config register offsets (from driver) */
#define PCI_CTL_CLOCK_DLY_ADDR 0xb0
#define PCI_PMOS0_CONTROL 0xae
#define PCI_PMOS1_CONTROL 0xbd
#define PCI_CLOCK_CTL 0xb9

/* MMIO register offsets (from supplementary source) */
#define STATUS_OFFSET            0x14
#define DATA_OFFSET              0x100
#define CLOCK_CONTROL_OFFSET     0x30

/* Per-slot offset base: driver calculates addr[slot] = slot * 0x100 */
#define SLOT_OFFSET_MULT 0x100

/* Number of slots to emulate */
#define MAX_SLOTS 4

/* Register bit and field definitions */
#define HOST_CONTROL_RESET_REQ  0x00008000
#define HOST_CONTROL_RESET      0x00000100
#define HOST_CONTROL_CLOCK_EN   0x00000040
#define HOST_CONTROL_POWER_EN   0x00000080
#define HOST_CONTROL_LED        0x00000400
#define HOST_CONTROL_IF_SERIAL  0x0
#define HOST_CONTROL_IF_PAR4    0x1
#define HOST_CONTROL_IF_PAR8    0x3
#define HOST_CONTROL_TDELAY_EN  0x00040000
#define HOST_CONTROL_HW_OC_P    0x00010000
#define HOST_CONTROL_REI        0x00004000
#define HOST_CONTROL_REO        0x00000008
#define HOST_CONTROL_FAST_CLK   0x00000200
#define HOST_CONTROL_IF_SHIFT   4

#define INT_STATUS_CRC_ERR      0x00040000
#define INT_STATUS_TPC_ERR      0x00080000
#define INT_STATUS_EOTRAN       0x00000002
#define INT_STATUS_EOTPC        0x00000001
#define INT_STATUS_FIFO_RRDY    0x00000040
#define INT_STATUS_FIFO_WRDY    0x00000080
#define INT_STATUS_MEDIA_IN     0x00000008
#define INT_STATUS_MEDIA_OUT    0x00000010
#define INT_STATUS_ANY_ERR      0x00008000
#define INT_STATUS_ALL          0x000F801F  /* OR of known bits */

#define STATUS_FIFO_EMPTY       0x00000200
#define STATUS_FIFO_FULL        0x00000100
#define STATUS_HAS_MEDIA        0x00000400

#define BLOCK_COUNT_MASK        0xffff0000
#define BLOCK_SIZE_MASK         0x00000fff

#define DMA_CONTROL_ENABLE      0x00000001

#define PMOS0_ACTIVE_BITS (0x4a)
#define PMOS1_ACTIVE_BITS 0x4a

#define CLOCK_CONTROL_OFF       0x00000000
#define CLOCK_CONTROL_40MHZ     0x00000001
#define CLOCK_CONTROL_50MHZ     0x00000002
#define CLOCK_CONTROL_BY_MMIO   0x00000008

#define PAD_PU_PD_ON_MS_SOCK0   0x5f8f0000
#define PAD_PU_PD_ON_MS_SOCK1   0x0f0f0000
#define PAD_PU_PD_OFF           0x7FFF0000
#define PAD_OUTPUT_ENABLE_MS    0x0F3F

/* BAR0 size: must accommodate up to MAX_SLOTS * 0x100 + 0x100 for DATA of last slot.
 * For MAX_SLOTS=4, we need 4*0x100 + 0x100 = 0x500, rounded up to power of 2 => 0x800.
 * We use 0x800 to be safe. */
#define BAR0_SIZE 0x800

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

typedef struct {
    uint32_t host_control;
    uint32_t int_status;
    uint32_t int_signal_enable;
    uint32_t int_status_enable;
    uint32_t status_reg;
    uint32_t tpc;
    uint32_t tpc_p0, tpc_p1;
    uint32_t dma_address;
    uint32_t block;
    uint32_t dma_control;
    uint32_t clock_control;
    uint32_t pad_pu_pd;
    uint32_t pad_output_enable;

    /* FIFO for DATA register */
    uint32_t fifo[16];
    int fifo_head, fifo_tail, fifo_count;
} SlotState;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Interrupt State (legacy INTx) */
    uint32_t intr_enable;  /* Combined enable from INT_SIGNAL_ENABLE and INT_STATUS_ENABLE */

    /* Per-slot state */
    SlotState slots[MAX_SLOTS];

    /* PCI config shadow registers */
    uint32_t pci_clock_dly;
    uint8_t  pmos0_control;
    uint8_t  pmos1_control;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int level = 0;
    int i;

    for (i = 0; i < MAX_SLOTS; i++) {
        if (s->slots[i].int_status & s->intr_enable) {
            level = 1;
            break;
        }
    }
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_do_dma(PCIBaseState *s, int slot, bool is_write)
{
    /* Placeholder: DMA transfers will be implemented when DMA registers are
     * properly defined and the exact behavior is understood from the driver.
     * Currently, the driver sets DMA_ADDRESS, BLOCK, DMA_CONTROL and expects
     * hardware to perform bus-master DMA. We need register offsets and bit
     * definitions to complete this logic. */
    /* PCIDevice *pdev = PCI_DEVICE(s); */
    /* if (s->dma_control & DMA_CONTROL_ENABLE) { ... } */
}

/* MMIO Read handler with per-slot and sub-register support */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t reg_val;
    int slot, offset;

    /* Convert address to slot index and register offset within slot.
     * Data register is at offset 0x100, which is ((slot+1)<<8).
     * Special handling: if low 8 bits are zero, it may be DATA of previous slot. */
    if (addr == 0) {
        slot = 0;
        offset = 0;
    } else if ((addr & 0xFF) == 0) {
        /* DATA access for slot = (addr>>8) - 1 */
        slot = (addr >> 8) - 1;
        offset = 0x100;
    } else {
        slot = addr >> 8;
        offset = addr & 0xFF;
    }

    if (slot < 0 || slot >= MAX_SLOTS) {
        return 0;
    }
    SlotState *slot_state = &s->slots[slot];

    /* Read the full register value for the given offset */
    switch (offset) {
    case 0x00: /* HOST_CONTROL */
        reg_val = slot_state->host_control;
        break;
    case 0x04: /* INT_STATUS */
        reg_val = slot_state->int_status;
        break;
    case 0x08: /* INT_SIGNAL_ENABLE */
        reg_val = slot_state->int_signal_enable;
        break;
    case 0x0C: /* INT_STATUS_ENABLE */
        reg_val = slot_state->int_status_enable;
        break;
    case STATUS_OFFSET: /* 0x14 */
        reg_val = slot_state->status_reg;
        break;
    case 0x100: /* DATA_OFFSET */
        if (slot_state->fifo_count > 0) {
            reg_val = slot_state->fifo[slot_state->fifo_tail];
            slot_state->fifo_tail = (slot_state->fifo_tail + 1) & 15;
            slot_state->fifo_count--;
        } else {
            reg_val = 0;
        }
        /* Update STATUS_FIFO_EMPTY flag after read */
        if (slot_state->fifo_count == 0) {
            slot_state->status_reg |= STATUS_FIFO_EMPTY;
        } else {
            slot_state->status_reg &= ~STATUS_FIFO_EMPTY;
        }
        break;
    case 0x18: /* TPC */
        reg_val = slot_state->tpc;
        break;
    case 0x1C: /* TPC_P0 */
        reg_val = slot_state->tpc_p0;
        break;
    case 0x20: /* TPC_P1 */
        reg_val = slot_state->tpc_p1;
        break;
    case 0x24: /* DMA_ADDRESS (supposed offset) */
        reg_val = slot_state->dma_address;
        break;
    case 0x28: /* BLOCK (supposed offset) */
        reg_val = slot_state->block;
        break;
    case 0x2C: /* DMA_CONTROL (supposed offset) */
        reg_val = slot_state->dma_control;
        break;
    case CLOCK_CONTROL_OFFSET:
        reg_val = slot_state->clock_control;
        break;
    case 0x10: /* PAD_PU_PD (placeholder offset) */
        reg_val = slot_state->pad_pu_pd;
        break;
    case 0x34: /* PAD_OUTPUT_ENABLE (placeholder offset) */
        reg_val = slot_state->pad_output_enable;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unhandled read at slot %d offset 0x%x\n",
                      __func__, slot, offset);
        reg_val = 0;
        break;
    }

    /* Extract the requested part of the 32-bit register */
    switch (size) {
    case 1:
        val = (reg_val >> ((addr & 3) * 8)) & 0xFF;
        break;
    case 2:
        if (addr & 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: misaligned word read\n", __func__);
            val = 0;
        } else {
            val = (reg_val >> ((addr & 2) * 8)) & 0xFFFF;
        }
        break;
    case 4:
        val = reg_val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unhandled read size %u\n", __func__, size);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int slot, offset;
    uint32_t reg_val, mask, shift;

    /* Convert address to slot and offset (same as read) */
    if (addr == 0) {
        slot = 0;
        offset = 0;
    } else if ((addr & 0xFF) == 0) {
        slot = (addr >> 8) - 1;
        offset = 0x100;
    } else {
        slot = addr >> 8;
        offset = addr & 0xFF;
    }

    if (slot < 0 || slot >= MAX_SLOTS) {
        return;
    }
    SlotState *slot_state = &s->slots[slot];

    /* For sub-dword writes, read-modify-write the register */
    if (size != 4) {
        /* Read existing register value */
        switch (offset) {
        case 0x00: reg_val = slot_state->host_control; break;
        case 0x04: reg_val = slot_state->int_status; break;
        case 0x08: reg_val = slot_state->int_signal_enable; break;
        case 0x0C: reg_val = slot_state->int_status_enable; break;
        case STATUS_OFFSET: reg_val = slot_state->status_reg; break;
        case 0x100: reg_val = 0; break; /* data handled separately */
        case 0x18: reg_val = slot_state->tpc; break;
        case 0x1C: reg_val = slot_state->tpc_p0; break;
        case 0x20: reg_val = slot_state->tpc_p1; break;
        case 0x24: reg_val = slot_state->dma_address; break;
        case 0x28: reg_val = slot_state->block; break;
        case 0x2C: reg_val = slot_state->dma_control; break;
        case CLOCK_CONTROL_OFFSET: reg_val = slot_state->clock_control; break;
        case 0x10: reg_val = slot_state->pad_pu_pd; break;
        case 0x34: reg_val = slot_state->pad_output_enable; break;
        default: return;
        }

        /* Compute mask and shift the new value */
        if (size == 1) {
            shift = (addr & 3) * 8;
            mask = 0xFFU << shift;
            val = (val & 0xFF) << shift;
        } else if (size == 2) {
            if (addr & 1) {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: misaligned word write\n", __func__);
                return;
            }
            shift = (addr & 2) * 8;
            mask = 0xFFFFU << shift;
            val = (val & 0xFFFF) << shift;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unhandled write size %u\n", __func__, size);
            return;
        }
        val = (reg_val & ~mask) | val;
    } else {
        val = val; /* 32-bit write, val is passed as is */
    }

    /* Write the modified value back */
    switch (offset) {
    case 0x00: /* HOST_CONTROL */
        slot_state->host_control = val;
        /* Emulate self-clearing reset bits */
        if (val & HOST_CONTROL_RESET_REQ) {
            slot_state->host_control &= ~HOST_CONTROL_RESET_REQ;
        }
        if (val & HOST_CONTROL_RESET) {
            slot_state->host_control &= ~HOST_CONTROL_RESET;
        }
        break;
    case 0x04: /* INT_STATUS */
        /* Write-1-to-clear: bits set in val are cleared in intr_status. */
        slot_state->int_status &= ~val;
        pcibase_update_irq(s);
        break;
    case 0x08: /* INT_SIGNAL_ENABLE */
        slot_state->int_signal_enable = val;
        s->intr_enable = slot_state->int_signal_enable; /* treat them the same for now */
        pcibase_update_irq(s);
        break;
    case 0x0C: /* INT_STATUS_ENABLE */
        slot_state->int_status_enable = val;
        s->intr_enable = slot_state->int_status_enable;
        pcibase_update_irq(s);
        break;
    case STATUS_OFFSET: /* 0x14 */
        /* STATUS register is read-only? driver only reads it. We allow write for our use. */
        slot_state->status_reg = val;
        break;
    case 0x100: /* DATA_OFFSET */
        /* FIFO write */
        if (slot_state->fifo_count < 16) {
            slot_state->fifo[slot_state->fifo_head] = val;
            slot_state->fifo_head = (slot_state->fifo_head + 1) & 15;
            slot_state->fifo_count++;
            slot_state->status_reg &= ~STATUS_FIFO_EMPTY;
        }
        /* Update STATUS_FIFO_FULL flag */
        if (slot_state->fifo_count == 16) {
            slot_state->status_reg |= STATUS_FIFO_FULL;
        } else {
            slot_state->status_reg &= ~STATUS_FIFO_FULL;
        }
        break;
    case 0x18: /* TPC */
        slot_state->tpc = val;
        /* In real hardware, writing TPC triggers command execution, DMA, etc.
         * Placeholder: we do not simulate full operation yet. */
        break;
    case 0x1C: /* TPC_P0 */
        slot_state->tpc_p0 = val;
        break;
    case 0x20: /* TPC_P1 */
        slot_state->tpc_p1 = val;
        break;
    case 0x24: /* DMA_ADDRESS */
        slot_state->dma_address = val;
        break;
    case 0x28: /* BLOCK */
        slot_state->block = val;
        break;
    case 0x2C: /* DMA_CONTROL */
        slot_state->dma_control = val;
        if (val & DMA_CONTROL_ENABLE) {
            /* Start DMA transfer; placeholder */
            pcibase_do_dma(s, slot, false); /* need to know direction */
        }
        break;
    case CLOCK_CONTROL_OFFSET:
        slot_state->clock_control = val;
        break;
    case 0x10: /* PAD_PU_PD */
        slot_state->pad_pu_pd = val;
        break;
    case 0x34: /* PAD_OUTPUT_ENABLE */
        slot_state->pad_output_enable = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unhandled write at slot %d offset 0x%x val 0x%"PRIx64"\n",
                      __func__, slot, offset, val);
        break;
    }
}

/* PIO handlers: not used by this driver */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

/* PCI config space handlers to support vendor-specific registers */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;

    if (addr >= 0xae && addr + len <= 0xb0) { /* PMOS0_CONTROL area */
        val = s->pmos0_control;
    } else if (addr == 0xb0 && len == 4) { /* PCI_CTL_CLOCK_DLY_ADDR */
        val = s->pci_clock_dly;
    } else if (addr == 0xb9 && len == 1) { /* PCI_CLOCK_CTL */
        val = 0; /* dummy */
    } else if (addr == 0xbd && len == 1) { /* PCI_PMOS1_CONTROL */
        val = s->pmos1_control;
    } else {
        val = pci_default_read_config(pdev, addr, len);
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr >= 0xae && addr + len <= 0xb0) { /* PMOS0_CONTROL area */
        s->pmos0_control = val & 0xFF;
    } else if (addr == 0xb0 && len == 4) { /* PCI_CTL_CLOCK_DLY_ADDR */
        s->pci_clock_dly = val;
    } else if (addr == 0xb9 && len == 1) { /* PCI_CLOCK_CTL */
        /* store if needed, currently ignore */
    } else if (addr == 0xbd && len == 1) { /* PCI_PMOS1_CONTROL */
        s->pmos1_control = val & 0xFF;
    } else {
        pci_default_write_config(pdev, addr, val, len);
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
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset internal state for all slots */
    for (int i = 0; i < MAX_SLOTS; i++) {
        SlotState *slot = &s->slots[i];
        slot->host_control = 0;
        slot->int_status = 0;
        slot->int_signal_enable = 0;
        slot->int_status_enable = 0;
        slot->tpc = 0;
        slot->tpc_p0 = 0;
        slot->tpc_p1 = 0;
        slot->dma_address = 0;
        slot->block = 0;
        slot->dma_control = 0;
        slot->clock_control = 0;
        slot->pad_pu_pd = 0;
        slot->pad_output_enable = 0;
        slot->fifo_count = 0;
        slot->fifo_head = 0;
        slot->fifo_tail = 0;
        /* Slot 0 has media, others do not */
        slot->status_reg = STATUS_FIFO_EMPTY;
        if (i == 0) {
            slot->status_reg |= STATUS_HAS_MEDIA;
        }
    }
    s->intr_enable = 0;
    s->pci_clock_dly = 0;
    s->pmos0_control = 0;
    s->pmos1_control = 0;
    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* CRITICAL: PCI requires BAR sizes to be a power of 2. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1); /* INTA# */

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 1; /* Only one BAR0 for the memory stick host */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "jmb-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "jmb38x_ms_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
