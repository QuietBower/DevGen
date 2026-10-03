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

#define TYPE_PCIBASE_DEVICE "snd_atiixp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ATI_REG_ISR			0x00
#define ATI_REG_ISR_IN_XRUN		(1U<<0)
#define ATI_REG_ISR_IN_STATUS		(1U<<1)
#define ATI_REG_ISR_OUT_XRUN		(1U<<2)
#define ATI_REG_ISR_OUT_STATUS		(1U<<3)
#define ATI_REG_ISR_SPDF_XRUN		(1U<<4)
#define ATI_REG_ISR_SPDF_STATUS		(1U<<5)
#define ATI_REG_ISR_PHYS_INTR		(1U<<8)
#define ATI_REG_ISR_PHYS_MISMATCH	(1U<<9)
#define ATI_REG_ISR_CODEC0_NOT_READY	(1U<<10)
#define ATI_REG_ISR_CODEC1_NOT_READY	(1U<<11)
#define ATI_REG_ISR_CODEC2_NOT_READY	(1U<<12)
#define ATI_REG_ISR_NEW_FRAME		(1U<<13)
#define ATI_REG_IER			0x04
#define ATI_REG_IER_IN_XRUN_EN		(1U<<0)
#define ATI_REG_IER_IO_STATUS_EN	(1U<<1)
#define ATI_REG_IER_OUT_XRUN_EN		(1U<<2)
#define ATI_REG_IER_OUT_XRUN_COND	(1U<<3)
#define ATI_REG_IER_SPDF_XRUN_EN	(1U<<4)
#define ATI_REG_IER_SPDF_STATUS_EN	(1U<<5)
#define ATI_REG_IER_PHYS_INTR_EN	(1U<<8)
#define ATI_REG_IER_PHYS_MISMATCH_EN	(1U<<9)
#define ATI_REG_IER_CODEC0_INTR_EN	(1U<<10)
#define ATI_REG_IER_CODEC1_INTR_EN	(1U<<11)
#define ATI_REG_IER_CODEC2_INTR_EN	(1U<<12)
#define ATI_REG_IER_NEW_FRAME_EN	(1U<<13)
#define ATI_REG_IER_SET_BUS_BUSY	(1U<<14)
#define ATI_REG_CMD			0x08
#define ATI_REG_CMD_POWERDOWN		(1U<<0)
#define ATI_REG_CMD_RECEIVE_EN		(1U<<1)
#define ATI_REG_CMD_SEND_EN		(1U<<2)
#define ATI_REG_CMD_STATUS_MEM		(1U<<3)
#define ATI_REG_CMD_SPDF_OUT_EN		(1U<<4)
#define ATI_REG_CMD_SPDF_STATUS_MEM	(1U<<5)
#define ATI_REG_CMD_SPDF_THRESHOLD	(3U<<6)
#define ATI_REG_CMD_SPDF_THRESHOLD_SHIFT	6
#define ATI_REG_CMD_IN_DMA_EN		(1U<<8)
#define ATI_REG_CMD_OUT_DMA_EN		(1U<<9)
#define ATI_REG_CMD_SPDF_DMA_EN		(1U<<10)
#define ATI_REG_CMD_SPDF_OUT_STOPPED	(1U<<11)
#define ATI_REG_CMD_SPDF_CONFIG_MASK	(7U<<12)
#define ATI_REG_CMD_SPDF_CONFIG_34	(1U<<12)
#define ATI_REG_CMD_SPDF_CONFIG_78	(2U<<12)
#define ATI_REG_CMD_SPDF_CONFIG_69	(3U<<12)
#define ATI_REG_CMD_SPDF_CONFIG_01	(4U<<12)
#define ATI_REG_CMD_INTERLEAVE_SPDF	(1U<<16)
#define ATI_REG_CMD_AUDIO_PRESENT	(1U<<20)
#define ATI_REG_CMD_INTERLEAVE_IN	(1U<<21)
#define ATI_REG_CMD_INTERLEAVE_OUT	(1U<<22)
#define ATI_REG_CMD_LOOPBACK_EN		(1U<<23)
#define ATI_REG_CMD_PACKED_DIS		(1U<<24)
#define ATI_REG_CMD_BURST_EN		(1U<<25)
#define ATI_REG_CMD_PANIC_EN		(1U<<26)
#define ATI_REG_CMD_MODEM_PRESENT	(1U<<27)
#define ATI_REG_CMD_ACLINK_ACTIVE	(1U<<28)
#define ATI_REG_CMD_AC_SOFT_RESET	(1U<<29)
#define ATI_REG_CMD_AC_SYNC		(1U<<30)
#define ATI_REG_CMD_AC_RESET		(1U<<31)
#define ATI_REG_PHYS_OUT_ADDR		0x0c
#define ATI_REG_PHYS_OUT_CODEC_MASK	(3U<<0)
#define ATI_REG_PHYS_OUT_RW		(1U<<2)
#define ATI_REG_PHYS_OUT_ADDR_EN	(1U<<8)
#define ATI_REG_PHYS_OUT_ADDR_SHIFT	9
#define ATI_REG_PHYS_OUT_DATA_SHIFT	16
#define ATI_REG_PHYS_IN_ADDR		0x10
#define ATI_REG_PHYS_IN_READ_FLAG	(1U<<8)
#define ATI_REG_PHYS_IN_ADDR_SHIFT	9
#define ATI_REG_PHYS_IN_DATA_SHIFT	16
#define ATI_REG_SLOTREQ			0x14
#define ATI_REG_COUNTER			0x18
#define ATI_REG_COUNTER_SLOT		(3U<<0)
#define ATI_REG_COUNTER_BITCLOCK	(31U<<8)
#define ATI_REG_IN_FIFO_THRESHOLD	0x1c
#define ATI_REG_IN_DMA_LINKPTR		0x20
#define ATI_REG_IN_DMA_DT_START		0x24
#define ATI_REG_IN_DMA_DT_NEXT		0x28
#define ATI_REG_IN_DMA_DT_CUR		0x2c
#define ATI_REG_IN_DMA_DT_SIZE		0x30
#define ATI_REG_OUT_DMA_SLOT		0x34
#define ATI_REG_OUT_DMA_SLOT_BIT(x)	(1U << ((x) - 3))
#define ATI_REG_OUT_DMA_SLOT_MASK	0x1ff
#define ATI_REG_OUT_DMA_THRESHOLD_MASK	0xf800
#define ATI_REG_OUT_DMA_THRESHOLD_SHIFT	11
#define ATI_REG_OUT_DMA_LINKPTR		0x38
#define ATI_REG_OUT_DMA_DT_START	0x3c
#define ATI_REG_OUT_DMA_DT_NEXT		0x40
#define ATI_REG_OUT_DMA_DT_CUR		0x44
#define ATI_REG_OUT_DMA_DT_SIZE		0x48
#define ATI_REG_SPDF_CMD		0x4c
#define ATI_REG_SPDF_CMD_LFSR		(1U<<4)
#define ATI_REG_SPDF_CMD_SINGLE_CH	(1U<<5)
#define ATI_REG_SPDF_CMD_LFSR_ACC	(0xff<<8)
#define ATI_REG_SPDF_DMA_LINKPTR	0x50
#define ATI_REG_SPDF_DMA_DT_START	0x54
#define ATI_REG_SPDF_DMA_DT_NEXT	0x58
#define ATI_REG_SPDF_DMA_DT_CUR		0x5c
#define ATI_REG_SPDF_DMA_DT_SIZE	0x60
#define ATI_REG_MODEM_MIRROR		0x7c
#define ATI_REG_AUDIO_MIRROR		0x80
#define ATI_REG_6CH_REORDER		0x84
#define ATI_REG_6CH_REORDER_EN		(1U<<0)
#define ATI_REG_FIFO_FLUSH		0x88
#define ATI_REG_FIFO_OUT_FLUSH		(1U<<0)
#define ATI_REG_FIFO_IN_FLUSH		(1U<<1)
#define ATI_REG_LINKPTR_EN		(1U<<0)
#define ATI_REG_DMA_DT_SIZE		(0xffffU<<0)
#define ATI_REG_DMA_FIFO_USED		(0x1fU<<16)
#define ATI_REG_DMA_FIFO_FREE		(0x1fU<<21)
#define ATI_REG_DMA_STATE		(7U<<26)
#define ATI_MAX_DESCRIPTORS		256
#define NUM_ATI_CODECS			3

/* Writable bits for ATI_REG_CMD */
#define ATI_REG_CMD_WRITABLE_MASK \
    (ATI_REG_CMD_POWERDOWN | ATI_REG_CMD_RECEIVE_EN | ATI_REG_CMD_SEND_EN | \
    ATI_REG_CMD_STATUS_MEM | ATI_REG_CMD_SPDF_OUT_EN | ATI_REG_CMD_SPDF_STATUS_MEM | \
    ATI_REG_CMD_SPDF_THRESHOLD | ATI_REG_CMD_IN_DMA_EN | ATI_REG_CMD_OUT_DMA_EN | \
    ATI_REG_CMD_SPDF_DMA_EN | ATI_REG_CMD_SPDF_CONFIG_MASK | \
    ATI_REG_CMD_INTERLEAVE_SPDF | ATI_REG_CMD_INTERLEAVE_IN | ATI_REG_CMD_INTERLEAVE_OUT | \
    ATI_REG_CMD_LOOPBACK_EN | ATI_REG_CMD_PACKED_DIS | ATI_REG_CMD_BURST_EN | \
    ATI_REG_CMD_PANIC_EN | ATI_REG_CMD_AC_SOFT_RESET | ATI_REG_CMD_AC_SYNC | ATI_REG_CMD_AC_RESET)

struct atiixp_dma_desc {
	uint32_t addr;	/* DMA buffer address */
	uint16_t status;	/* status bits */
	uint16_t size;	/* size of the packet in dwords */
	uint32_t next;	/* address of the next packet descriptor */
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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x100/4];

    /* DMA Context */
    struct atiixp_dma_channel {
        uint32_t linkptr;
        uint32_t dt_start;
        uint32_t dt_next;
        uint32_t dt_cur;
        uint32_t dt_size;
    } dma_chan[3];

    uint32_t status;      /* Operational status flags */
    bool reset_active;    /* State used to handle reset sequences */
    uint8_t power_state;  /* Power management state (D0-D3) */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t enabled = s->regs[ATI_REG_ISR/4] & s->regs[ATI_REG_IER/4];
    if (enabled) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x100) {
        return ~0ULL;
    }

    /* Keep reads atomic for simplicity: read the whole dword and extract */
    uint32_t reg = s->regs[addr/4];
    switch (size) {
    case 1:
        val = (reg >> (8 * (addr & 3))) & 0xff;
        break;
    case 2:
        val = (reg >> (8 * (addr & 3))) & 0xffff;
        break;
    case 4:
        val = reg;
        break;
    case 8:
        val = (uint64_t)s->regs[addr/4] | ((uint64_t)s->regs[addr/4 + 1] << 32);
        break;
    default:
        val = 0;
    }

    /* Side-effect: reading PHYS_IN_ADDR clears the register */
    if ((addr & ~3) == ATI_REG_PHYS_IN_ADDR) {
        s->regs[ATI_REG_PHYS_IN_ADDR/4] = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x100) {
        return;
    }

    /* All registers are 32-bit wide, handle various write sizes */
    uint32_t *reg = &s->regs[addr/4];
    uint32_t old_val = *reg;
    uint32_t mask, new_val;

    switch (size) {
    case 1:
        mask = 0xff << (8 * (addr & 3));
        new_val = (old_val & ~mask) | ((val & 0xff) << (8 * (addr & 3)));
        break;
    case 2:
        mask = 0xffff << (8 * (addr & 3));
        new_val = (old_val & ~mask) | ((val & 0xffff) << (8 * (addr & 3)));
        break;
    case 4:
        new_val = val;
        break;
    case 8:
        /* Write lower 32 bits first, then upper */
        s->regs[addr/4] = val & 0xffffffff;
        if (addr/4 + 1 < 0x100/4) {
            s->regs[addr/4 + 1] = (val >> 32);
        }
        return;
    default:
        return;
    }

    /* Apply special register handling */
    switch (addr & ~3) {
    case ATI_REG_ISR:
        /* ISR is Write-1-to-Clear for all bits */
        new_val = old_val & ~new_val;
        break;
    case ATI_REG_CMD:
        {
            uint32_t writable_mask = ATI_REG_CMD_WRITABLE_MASK;
            uint32_t old_cmd = s->regs[ATI_REG_CMD/4];
            uint32_t new_cmd = (old_cmd & ~writable_mask) | (new_val & writable_mask);
            bool reset_rising = (new_cmd & ATI_REG_CMD_AC_RESET) && !(old_cmd & ATI_REG_CMD_AC_RESET);
            bool reset_falling = !(new_cmd & ATI_REG_CMD_AC_RESET) && (old_cmd & ATI_REG_CMD_AC_RESET);

            if (reset_rising) {
                new_cmd &= ~(ATI_REG_CMD_ACLINK_ACTIVE | ATI_REG_CMD_AUDIO_PRESENT);
                s->regs[ATI_REG_ISR/4] |= ATI_REG_ISR_CODEC0_NOT_READY |
                                           ATI_REG_ISR_CODEC1_NOT_READY |
                                           ATI_REG_ISR_CODEC2_NOT_READY;
            }
            if (reset_falling) {
                new_cmd |= ATI_REG_CMD_ACLINK_ACTIVE | ATI_REG_CMD_AUDIO_PRESENT;
                s->regs[ATI_REG_ISR/4] &= ~(ATI_REG_ISR_CODEC0_NOT_READY |
                                             ATI_REG_ISR_CODEC1_NOT_READY |
                                             ATI_REG_ISR_CODEC2_NOT_READY);
                pcibase_update_irq(s);
            }
            *reg = new_cmd;
        }
        return;
    case ATI_REG_PHYS_OUT_ADDR:
        if (new_val & ATI_REG_PHYS_OUT_ADDR_EN) {
            uint32_t codec = new_val & ATI_REG_PHYS_OUT_CODEC_MASK;
            uint32_t reg_addr = (new_val >> ATI_REG_PHYS_OUT_ADDR_SHIFT) & 0x7f;
            uint32_t rw = new_val & ATI_REG_PHYS_OUT_RW;

            if (rw) {
                uint16_t data = 0;
                switch (reg_addr) {
                    case 0x00: data = 0x0D50; break; /* reset register default */
                    case 0x02: data = 0x8000; break; /* master volume muted */
                    case 0x04: data = 0x8000; break;
                    case 0x06: data = 0x8000; break;
                    case 0x0A: data = 0x0000; break; /* PC Beep Volume */
                    case 0x0C: data = 0x8000; break; /* Phone Volume */
                    case 0x0E: data = 0x8000; break; /* Mic Volume */
                    case 0x10: data = 0x8000; break; /* Line In Volume */
                    case 0x12: data = 0x8000; break; /* CD Volume */
                    case 0x14: data = 0x8000; break; /* Video Volume */
                    case 0x16: data = 0x8000; break; /* Aux Volume */
                    case 0x18: data = 0x0000; break; /* PCM Out Volume */
                    case 0x1A: data = 0x0000; break; /* Record Select */
                    case 0x1C: data = 0x0000; break; /* Record Gain */
                    case 0x1E: data = 0x0000; break; /* Record Gain Mic */
                    case 0x20: data = 0x0000; break; /* General Purpose */
                    case 0x22: data = 0x0000; break; /* 3D Control */
                    case 0x24: data = 0x0000; break; /* Audio Interrupt & Paging */
                    case 0x26: data = 0x0080; break; /* Extended Audio ID */
                    case 0x28: data = 0x0000; break; /* Extended Audio Status & Control */
                    case 0x2A: data = 0x0000; break; /* PCM Front DAC Rate */
                    case 0x2C: data = 0xBB80; break; /* PCM Surround DAC Rate = 48000 */
                    case 0x2E: data = 0xBB80; break; /* PCM LFE DAC Rate */
                    case 0x30: data = 0xBB80; break; /* PCM L/R DAC Rate */
                    case 0x7C: data = 0x4144; break; /* Vendor ID1 */
                    case 0x7E: data = 0x5361; break; /* Vendor ID2 */
                    default:   data = 0x0000; break;
                }
                s->regs[ATI_REG_PHYS_IN_ADDR/4] = ATI_REG_PHYS_IN_READ_FLAG |
                                                   ((uint32_t)data << ATI_REG_PHYS_IN_DATA_SHIFT);
                s->regs[ATI_REG_ISR/4] |= ATI_REG_ISR_PHYS_INTR;
            } else {
                /* Write access: success, no data returned */
                s->regs[ATI_REG_PHYS_IN_ADDR/4] = 0; /* clear any previous read data */
                s->regs[ATI_REG_ISR/4] |= ATI_REG_ISR_PHYS_INTR;
            }
            new_val &= ~ATI_REG_PHYS_OUT_ADDR_EN;
            pcibase_update_irq(s);
        }
        break;
    case ATI_REG_IER:
        /* IER write may affect IRQ */
        *reg = new_val;
        pcibase_update_irq(s);
        return;
    }

    *reg = new_val;

    /* After ISR write, update IRQ */
    if ((addr & ~3) == ATI_REG_ISR) {
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

/* These are not used as the device only uses MMIO */
/*
static u64 pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    u64 val = 0;

    #PIO_Read_Func# 
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, u64 val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    #PIO_Write_Func# 
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};
*/

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Set power-on defaults: all registers zero except CMD_POWERDOWN */
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[ATI_REG_CMD/4] = ATI_REG_CMD_POWERDOWN;
    /* Clear IRQ */
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
        /* Not used: no PIO handler registered */
        error_setg(errp, "PIO BAR not implemented");
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1002 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x4341 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0401 );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA not used, no need to set up DMA mask */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No additional cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_atiixp_pci",
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
