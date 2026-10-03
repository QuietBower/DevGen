/*
 * QEMU device model for Conexant CX23418 MPEG encoder
 * Generated from Linux driver /home/eely/linux-7.1/drivers/media/pci/cx18/cx18-driver.c
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

#define TYPE_PCIBASE_DEVICE "cx18_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* CX18 register and memory constants */
#define CX18_MEM_SIZE  0x04000000
#define SCB_OFFSET     0xDC0000
#define CX18_REG_OFFSET 0x02000000
#define CX18_MEM_OFFSET 0x00000000

/* Interrupt register sets (offsets from reg_mem base, i.e., BAR offset = CX18_REG_OFFSET + value) */
#define SW1_INT_SET      0xc73100
#define SW1_INT_STATUS   0xc73104
#define SW1_INT_ENABLE_PCI 0xc7311c
#define SW2_INT_STATUS   0xc73144
#define SW2_INT_ENABLE_PCI 0xc7315c
#define SW2_INT_ENABLE_CPU 0xc73158
#define HW2_INT_CLR_STATUS 0xc730c4

/* Additional register offsets from driver */
#define CX18_REG_CLOCK_SELECT 0xc71004
#define CX18_REG_CLOCK_ENABLE 0xc71024

/* Interrupt bit definitions (from the driver) */
#define IRQ_CPU_TO_EPU_ACK     0x00000008
#define IRQ_CPU_TO_EPU         0x00010000
#define IRQ_APU_TO_EPU         0x00020000
#define IRQ_APU_TO_EPU_ACK     0x00000080
#define IRQ_HPU_TO_APU         0x00000020
#define IRQ_CPU_TO_APU         0x00000010
#define IRQ_EPU_TO_CPU         0x00000008
#define IRQ_HPU_TO_PPU         0x00004000
#define IRQ_EPU_TO_PPU         0x00008000
#define IRQ_PPU_TO_HPU_ACK     0x00004000
#define IRQ_HPU_TO_EPU         0x00040000
#define IRQ_CPU_TO_PPU_ACK     0x00000004
#define IRQ_PPU_TO_HPU         0x00000400
#define IRQ_APU_TO_CPU_ACK     0x00000010
#define IRQ_CPU_TO_HPU_ACK     0x00000002
#define IRQ_APU_TO_HPU         0x00000200
#define IRQ_HPU_TO_APU_ACK     0x00000200
#define IRQ_CPU_TO_PPU         0x00001000
#define IRQ_EPU_TO_APU_ACK     0x00020000
#define IRQ_PPU_TO_APU         0x00000040
#define IRQ_APU_TO_CPU         0x00000001
#define IRQ_EPU_TO_CPU_ACK     0x00010000
#define IRQ_PPU_TO_EPU_ACK     0x00008000
#define IRQ_APU_TO_PPU_ACK     0x00000040
#define IRQ_EPU_TO_HPU_ACK     0x00040000
#define IRQ_EPU_TO_PPU_ACK     0x00080000
#define IRQ_PPU_TO_CPU_ACK     0x00001000
#define IRQ_HPU_TO_CPU_ACK     0x00000100
#define IRQ_HPU_TO_PPU_ACK     0x00000400
#define IRQ_PPU_TO_EPU         0x00080000
#define IRQ_CPU_TO_APU_ACK     0x00000001
#define IRQ_PPU_TO_CPU         0x00000004
#define IRQ_EPU_TO_HPU         0x00000800
#define IRQ_CPU_TO_HPU         0x00000100
#define IRQ_APU_TO_HPU_ACK     0x00000020
#define IRQ_HPU_TO_CPU         0x00000002
#define IRQ_HPU_TO_EPU_ACK     0x00000800
#define IRQ_EPU_TO_APU         0x00000080
#define IRQ_APU_TO_PPU         0x00002000
#define IRQ_PPU_TO_APU_ACK     0x00002000

/* Command masks for firmware commands (for completeness, not used by QEMU) */
#define APU_CMD_MASK     0x10000000
#define CPU_CMD_MASK     0x20000000
#define EPU_CMD_MASK     0x02000000
#define MGR_CMD_MASK     0x40000000
#define CPU_CMD_MASK_CAPTURE (CPU_CMD_MASK | 0x00020000)
#define CPU_CMD_MASK_DEBUG   (CPU_CMD_MASK | 0x00000000)
#define CPU_CMD_MASK_DE      (CPU_CMD_MASK | 0x00040000)
#define EPU_CMD_MASK_DEBUG   (EPU_CMD_MASK | 0x00000000)

/* Mailbox constants */
#define CX2341X_MBOX_MAX_DATA 16
#define MAX_MB_ARGUMENTS 6

/* Stream types */
#define CX18_ENC_STREAM_TYPE_MPG  0
#define CX18_ENC_STREAM_TYPE_TS   1
#define CX18_ENC_STREAM_TYPE_YUV  2
#define CX18_ENC_STREAM_TYPE_VBI  3
#define CX18_ENC_STREAM_TYPE_PCM  4
#define CX18_ENC_STREAM_TYPE_IDX  5
#define CX18_ENC_STREAM_TYPE_RAD  6
#define CX18_MAX_STREAMS  7
#define CX18_MAX_MDL_ACKS 2

/* Vendor and Device IDs */
#define PCI_VENDOR_ID_CX     0x14f1
#define PCI_DEVICE_ID_CX23418 0x5b7a

/* Hardware mailbox and MDL structures (from shared memory layout) */
struct cx18_mailbox {
    uint32_t request;
    uint32_t ack;
    uint32_t reserved[6];
    uint32_t cmd;
    uint32_t args[MAX_MB_ARGUMENTS];
    uint32_t error;
};

struct cx18_mdl_ent {
    uint32_t paddr;
    uint32_t length;
};

struct cx18_mdl_ack {
    uint32_t id;
    uint32_t data_used;
};

/* The System Control Block (SCB) structure – maintained for reference and potential future use */
struct cx18_scb {
    uint32_t ipc_offset;
    uint32_t reserved01[7];
    uint32_t cpu_code_offset;
    uint32_t reserved02[3];
    uint32_t apu_code_offset;
    uint32_t reserved03[3];
    uint32_t hpu_code_offset;
    uint32_t reserved04[3];
    uint32_t ppu_code_offset;
    uint32_t reserved05[3];

    /* CPU mailbox settings */
    uint32_t cpu_state;
    uint32_t reserved1[7];
    uint32_t apu2cpu_mb_offset;
    uint32_t apu2cpu_irq;
    uint32_t cpu2apu_irq_ack;
    uint32_t reserved2[13];

    uint32_t hpu2cpu_mb_offset;
    uint32_t hpu2cpu_irq;
    uint32_t cpu2hpu_irq_ack;
    uint32_t reserved3[13];

    uint32_t ppu2cpu_mb_offset;
    uint32_t ppu2cpu_irq;
    uint32_t cpu2ppu_irq_ack;
    uint32_t reserved4[13];

    uint32_t epu2cpu_mb_offset;
    uint32_t epu2cpu_irq;
    uint32_t cpu2epu_irq_ack;
    uint32_t reserved5[13];
    uint32_t reserved6[8];

    /* APU settings */
    uint32_t apu_state;
    uint32_t reserved11[7];
    uint32_t cpu2apu_mb_offset;
    uint32_t cpu2apu_irq;
    uint32_t apu2cpu_irq_ack;
    uint32_t reserved12[13];

    uint32_t hpu2apu_mb_offset;
    uint32_t hpu2apu_irq;
    uint32_t apu2hpu_irq_ack;
    uint32_t reserved13[13];

    uint32_t ppu2apu_mb_offset;
    uint32_t ppu2apu_irq;
    uint32_t apu2ppu_irq_ack;
    uint32_t reserved14[13];

    uint32_t epu2apu_mb_offset;
    uint32_t epu2apu_irq;
    uint32_t apu2epu_irq_ack;
    uint32_t reserved15[13];
    uint32_t reserved16[8];

    /* HPU settings */
    uint32_t hpu_state;
    uint32_t reserved21[7];
    uint32_t cpu2hpu_mb_offset;
    uint32_t cpu2hpu_irq;
    uint32_t hpu2cpu_irq_ack;
    uint32_t reserved22[13];

    uint32_t apu2hpu_mb_offset;
    uint32_t apu2hpu_irq;
    uint32_t hpu2apu_irq_ack;
    uint32_t reserved23[13];

    uint32_t ppu2hpu_mb_offset;
    uint32_t ppu2hpu_irq;
    uint32_t hpu2ppu_irq_ack;
    uint32_t reserved24[13];

    uint32_t epu2hpu_mb_offset;
    uint32_t epu2hpu_irq;
    uint32_t hpu2epu_irq_ack;
    uint32_t reserved25[13];
    uint32_t reserved26[8];

    /* PPU settings */
    uint32_t ppu_state;
    uint32_t reserved31[7];
    uint32_t cpu2ppu_mb_offset;
    uint32_t cpu2ppu_irq;
    uint32_t ppu2cpu_irq_ack;
    uint32_t reserved32[13];

    uint32_t apu2ppu_mb_offset;
    uint32_t apu2ppu_irq;
    uint32_t ppu2apu_irq_ack;
    uint32_t reserved33[13];

    uint32_t hpu2ppu_mb_offset;
    uint32_t hpu2ppu_irq;
    uint32_t ppu2hpu_irq_ack;
    uint32_t reserved34[13];

    uint32_t epu2ppu_mb_offset;
    uint32_t epu2ppu_irq;
    uint32_t ppu2epu_irq_ack;
    uint32_t reserved35[13];
    uint32_t reserved36[8];

    /* EPU settings */
    uint32_t epu_state;
    uint32_t reserved41[7];
    uint32_t cpu2epu_mb_offset;
    uint32_t cpu2epu_irq;
    uint32_t epu2cpu_irq_ack;
    uint32_t reserved42[13];

    uint32_t apu2epu_mb_offset;
    uint32_t apu2epu_irq;
    uint32_t epu2apu_irq_ack;
    uint32_t reserved43[13];

    uint32_t hpu2epu_mb_offset;
    uint32_t hpu2epu_irq;
    uint32_t epu2hpu_irq_ack;
    uint32_t reserved44[13];

    uint32_t ppu2epu_mb_offset;
    uint32_t ppu2epu_irq;
    uint32_t epu2ppu_irq_ack;
    uint32_t reserved45[13];
    uint32_t reserved46[8];

    uint32_t semaphores[8];
    uint32_t reserved50[32];

    struct cx18_mailbox  apu2cpu_mb;
    struct cx18_mailbox  hpu2cpu_mb;
    struct cx18_mailbox  ppu2cpu_mb;
    struct cx18_mailbox  epu2cpu_mb;

    struct cx18_mailbox  cpu2apu_mb;
    struct cx18_mailbox  hpu2apu_mb;
    struct cx18_mailbox  ppu2apu_mb;
    struct cx18_mailbox  epu2apu_mb;

    struct cx18_mailbox  cpu2hpu_mb;
    struct cx18_mailbox  apu2hpu_mb;
    struct cx18_mailbox  ppu2hpu_mb;
    struct cx18_mailbox  epu2hpu_mb;

    struct cx18_mailbox  cpu2ppu_mb;
    struct cx18_mailbox  apu2ppu_mb;
    struct cx18_mailbox  hpu2ppu_mb;
    struct cx18_mailbox  epu2ppu_mb;

    struct cx18_mailbox  cpu2epu_mb;
    struct cx18_mailbox  apu2epu_mb;
    struct cx18_mailbox  hpu2epu_mb;
    struct cx18_mailbox  ppu2epu_mb;

    struct cx18_mdl_ack  cpu_mdl_ack[CX18_MAX_STREAMS][CX18_MAX_MDL_ACKS];
    struct cx18_mdl_ent  cpu_mdl[];
};

/* BAR types */
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
        uint32_t sw1_int_set;
        uint32_t sw1_int_status;
        uint32_t sw1_int_enable;
        uint32_t sw2_int_status;
        uint32_t sw2_int_enable;
        uint32_t hw2_int_clr_status;
    } intr_regs;

    uint32_t status;

    /* Backing store for the 64 MB MMIO region */
    uint8_t *mmio_buf;
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active_irqs = 0;

    /* SW1 interrupts */
    if (s->intr_regs.sw1_int_enable & s->intr_regs.sw1_int_status)
        active_irqs = 1;

    /* SW2 interrupts */
    if (s->intr_regs.sw2_int_enable & s->intr_regs.sw2_int_status)
        active_irqs = 1;

    /* HW2 interrupt clear status – any unserviced? This is just a shadow, no enable */
    /* We currently ignore HW2_CLR_STATUS for IRQ signaling because no driver enable register is used;
     * the driver may clear bits there but the interrupt line is handled via SW1/SW2 */

    if (active_irqs) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic – not used in this minimal model */
/*
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}
*/

/* Convert a register offset (as seen in the driver, e.g., 0xC73100) into a BAR offset */
static inline hwaddr reg_to_bar(hwaddr reg_offset)
{
    return CX18_REG_OFFSET + reg_offset;
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Intercept special interrupt registers */
    if (addr == reg_to_bar(SW1_INT_SET)) {
        val = s->intr_regs.sw1_int_set;
    } else if (addr == reg_to_bar(SW1_INT_STATUS)) {
        val = s->intr_regs.sw1_int_status;
    } else if (addr == reg_to_bar(SW1_INT_ENABLE_PCI)) {
        val = s->intr_regs.sw1_int_enable;
    } else if (addr == reg_to_bar(SW2_INT_STATUS)) {
        val = s->intr_regs.sw2_int_status;
    } else if (addr == reg_to_bar(SW2_INT_ENABLE_PCI)) {
        val = s->intr_regs.sw2_int_enable;
    } else if (addr == reg_to_bar(HW2_INT_CLR_STATUS)) {
        val = s->intr_regs.hw2_int_clr_status;
    } else if (addr == reg_to_bar(CX18_REG_CLOCK_SELECT)) {
        /* After write, this register always reads back 0 (as expected by driver) */
        val = 0;
    } else {
        /* General MMIO access; use backing store */
        if (size == 1) {
            val = s->mmio_buf[addr];
        } else if (size == 2) {
            uint16_t tmp;
            memcpy(&tmp, s->mmio_buf + addr, sizeof(tmp));
            val = le16_to_cpu(tmp);
        } else if (size == 4) {
            uint32_t tmp;
            memcpy(&tmp, s->mmio_buf + addr, sizeof(tmp));
            val = le32_to_cpu(tmp);
        } else if (size == 8) {
            uint64_t tmp;
            memcpy(&tmp, s->mmio_buf + addr, sizeof(tmp));
            val = le64_to_cpu(tmp);
        } else {
            qemu_log_mask(LOG_UNIMP, "cx18: unhandled MMIO read size %u at 0x"HWADDR_FMT_plx"\n", size, addr);
        }
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Intercept special interrupt registers */
    if (addr == reg_to_bar(SW1_INT_SET)) {
        /* Writing 1 to a bit clears the corresponding status bit (W1C) */
        s->intr_regs.sw1_int_status &= ~val;
        s->intr_regs.sw1_int_set = val;
        pcibase_update_irq(s);
    } else if (addr == reg_to_bar(SW1_INT_STATUS)) {
        /* Status register may be read-only or W1C; assume W1C */
        s->intr_regs.sw1_int_status &= ~val;
        pcibase_update_irq(s);
    } else if (addr == reg_to_bar(SW1_INT_ENABLE_PCI)) {
        s->intr_regs.sw1_int_enable = val;
        pcibase_update_irq(s);
    } else if (addr == reg_to_bar(SW2_INT_STATUS)) {
        s->intr_regs.sw2_int_status &= ~val;
        pcibase_update_irq(s);
    } else if (addr == reg_to_bar(SW2_INT_ENABLE_PCI)) {
        s->intr_regs.sw2_int_enable = val;
        pcibase_update_irq(s);
    } else if (addr == reg_to_bar(HW2_INT_CLR_STATUS)) {
        /* HW2 clear – write-1-to-clear */
        s->intr_regs.hw2_int_clr_status &= ~val;
        /* No effect on IRQ directly; driver uses this to clear specific internal status */
    } else if (addr == reg_to_bar(CX18_REG_CLOCK_SELECT)) {
        /* Writes to clock select register are ignored; reads always return 0 */
        /* No side effects needed */
    } else {
        /* General MMIO write, store in backing buffer */
        if (size == 1) {
            s->mmio_buf[addr] = (uint8_t)val;
        } else if (size == 2) {
            uint16_t tmp = cpu_to_le16((uint16_t)val);
            memcpy(s->mmio_buf + addr, &tmp, sizeof(tmp));
        } else if (size == 4) {
            uint32_t tmp = cpu_to_le32((uint32_t)val);
            memcpy(s->mmio_buf + addr, &tmp, sizeof(tmp));
        } else if (size == 8) {
            uint64_t tmp = cpu_to_le64(val);
            memcpy(s->mmio_buf + addr, &tmp, sizeof(tmp));
        } else {
            qemu_log_mask(LOG_UNIMP, "cx18: unhandled MMIO write size %u at 0x"HWADDR_FMT_plx"\n", size, addr);
        }
    }
}

/* PIO handlers (no PIO BARs defined, but keep empty for completeness) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* No PIO BARs, so this shouldn't be called */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO BARs */
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

    /* Reset base PCI device state */
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear the entire MMIO buffer to zero */
    memset(s->mmio_buf, 0, CX18_MEM_SIZE);

    /* Reset interrupt shadow registers */
    memset(&s->intr_regs, 0, sizeof(s->intr_regs));

    /* Set the chip type register (offset 0xC72028 from reg_mem) to a valid value.
     * The driver expects either 0xff000000 (revision A) or 0x01000000 (revision B).
     * We use 0xff000000 to indicate the A revision. */
    hwaddr chip_type_offset = reg_to_bar(0xC72028);
    uint32_t chip_type_val = cpu_to_le32(0xff000000);
    memcpy(s->mmio_buf + chip_type_offset, &chip_type_val, sizeof(chip_type_val));

    /* Clear any pending IRQ */
    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        /* Our MMIO region uses custom I/O handlers */
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

    /* Allocate the 64 MB MMIO backing buffer */
    s->mmio_buf = g_malloc0(CX18_MEM_SIZE);

    /* Basic PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x14f1);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x5b7a);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0400);    /* Multimedia device */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCIe capability and power management (as in template) */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Define BAR0: 64 MB MMIO region */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = CX18_MEM_SIZE;
    s->bar_info[0].name = "cx18-bar0";

    /* Register all bars */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Free the MMIO backing buffer */
    g_free(s->mmio_buf);

    /* Clean up MSI/MSI-X if ever enabled */
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "cx18_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        /* The MMIO buffer is not migrated here; we rely on guest reinitialization.
         * The interrupt shadow registers could be added if needed. */
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
