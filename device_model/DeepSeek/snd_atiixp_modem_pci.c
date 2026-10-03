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

#define TYPE_PCIBASE_DEVICE "snd_atiixp_modem_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ATI           0x1002
#define PCI_DEVICE_ID_ATI_IXP_MODEM 0x434d
#define PCI_CLASS_MODEM             0x0703

/* ATI modem register offsets */
#define ATI_REG_ISR                0x00
#define ATI_REG_ISR_PHYS_INTR      (1U<<8)
#define ATI_REG_ISR_PHYS_MISMATCH  (1U<<9)
#define ATI_REG_ISR_CODEC0_NOT_READY (1U<<10)
#define ATI_REG_ISR_CODEC1_NOT_READY (1U<<11)
#define ATI_REG_ISR_CODEC2_NOT_READY (1U<<12)
#define ATI_REG_ISR_NEW_FRAME      (1U<<13)
#define ATI_REG_IER                0x04
#define ATI_REG_IER_PHYS_INTR_EN    (1U<<8)
#define ATI_REG_IER_PHYS_MISMATCH_EN (1U<<9)
#define ATI_REG_IER_CODEC0_INTR_EN  (1U<<10)
#define ATI_REG_IER_CODEC1_INTR_EN  (1U<<11)
#define ATI_REG_IER_CODEC2_INTR_EN  (1U<<12)
#define ATI_REG_IER_NEW_FRAME_EN    (1U<<13)
#define ATI_REG_CMD                0x08
#define ATI_REG_CMD_POWERDOWN       (1U<<0)
#define ATI_REG_CMD_AUDIO_PRESENT   (1U<<20)
#define ATI_REG_CMD_LOOPBACK_EN     (1U<<23)
#define ATI_REG_CMD_PACKED_DIS      (1U<<24)
#define ATI_REG_CMD_BURST_EN        (1U<<25)
#define ATI_REG_CMD_PANIC_EN        (1U<<26)
#define ATI_REG_CMD_MODEM_PRESENT   (1U<<27)
#define ATI_REG_CMD_ACLINK_ACTIVE   (1U<<28)
#define ATI_REG_CMD_AC_SOFT_RESET   (1U<<29)
#define ATI_REG_CMD_AC_SYNC         (1U<<30)
#define ATI_REG_CMD_AC_RESET        (1U<<31)
#define ATI_REG_PHYS_OUT_ADDR       0x0c
#define ATI_REG_PHYS_OUT_CODEC_MASK (3U<<0)
#define ATI_REG_PHYS_OUT_RW         (1U<<2)
#define ATI_REG_PHYS_OUT_ADDR_EN    (1U<<8)
#define ATI_REG_PHYS_OUT_ADDR_SHIFT 9
#define ATI_REG_PHYS_OUT_DATA_SHIFT 16
#define ATI_REG_PHYS_IN_ADDR        0x10
#define ATI_REG_PHYS_IN_READ_FLAG   (1U<<8)
#define ATI_REG_PHYS_IN_ADDR_SHIFT  9
#define ATI_REG_PHYS_IN_DATA_SHIFT  16
#define ATI_REG_SLOTREQ             0x14
#define ATI_REG_COUNTER             0x18
#define ATI_REG_COUNTER_SLOT        (3U<<0)
#define ATI_REG_COUNTER_BITCLOCK    (31U<<8)
#define ATI_REG_IN_FIFO_THRESHOLD   0x1c
#define ATI_REG_MODEM_MIRROR        0x7c
#define ATI_REG_AUDIO_MIRROR        0x80
#define ATI_REG_LINKPTR_EN          (1U<<0)
#define ATI_REG_ISR_MODEM_IN_XRUN       (1U<<0)
#define ATI_REG_ISR_MODEM_IN_STATUS     (1U<<1)
#define ATI_REG_ISR_MODEM_OUT1_XRUN     (1U<<2)
#define ATI_REG_ISR_MODEM_OUT1_STATUS   (1U<<3)
#define ATI_REG_ISR_MODEM_OUT2_XRUN     (1U<<4)
#define ATI_REG_ISR_MODEM_OUT2_STATUS   (1U<<5)
#define ATI_REG_ISR_MODEM_OUT3_XRUN     (1U<<6)
#define ATI_REG_ISR_MODEM_OUT3_STATUS   (1U<<7)
#define ATI_REG_ISR_MODEM_GPIO_DATA     (1U<<14)
#define ATI_REG_IER_MODEM_IN_XRUN_EN    (1U<<0)
#define ATI_REG_IER_MODEM_STATUS_EN     (1U<<1)
#define ATI_REG_IER_MODEM_OUT1_XRUN_EN  (1U<<2)
#define ATI_REG_IER_MODEM_OUT2_XRUN_EN  (1U<<4)
#define ATI_REG_IER_MODEM_OUT3_XRUN_EN  (1U<<6)
#define ATI_REG_IER_MODEM_GPIO_DATA_EN  (1U<<14)
#define ATI_REG_IER_MODEM_SET_BUS_BUSY  (1U<<15)
#define ATI_REG_CMD_MODEM_RECEIVE_EN    (1U<<1)
#define ATI_REG_CMD_MODEM_SEND1_EN      (1U<<2)
#define ATI_REG_CMD_MODEM_SEND2_EN      (1U<<3)
#define ATI_REG_CMD_MODEM_SEND3_EN      (1U<<4)
#define ATI_REG_CMD_MODEM_STATUS_MEM    (1U<<5)
#define ATI_REG_CMD_MODEM_IN_DMA_EN     (1U<<8)
#define ATI_REG_CMD_MODEM_OUT_DMA1_EN   (1U<<9)
#define ATI_REG_CMD_MODEM_OUT_DMA2_EN   (1U<<10)
#define ATI_REG_CMD_MODEM_OUT_DMA3_EN   (1U<<11)
#define ATI_REG_CMD_MODEM_GPIO_THRU_DMA (1U<<22)
#define ATI_REG_MODEM_IN_DMA_LINKPTR    0x20
#define ATI_REG_MODEM_IN_DMA_DT_START   0x24
#define ATI_REG_MODEM_IN_DMA_DT_NEXT    0x28
#define ATI_REG_MODEM_IN_DMA_DT_CUR     0x2c
#define ATI_REG_MODEM_IN_DMA_DT_SIZE    0x30
#define ATI_REG_MODEM_OUT_FIFO          0x34
#define ATI_REG_MODEM_OUT1_DMA_THRESHOLD_MASK  (0xf<<16)
#define ATI_REG_MODEM_OUT1_DMA_THRESHOLD_SHIFT 16
#define ATI_REG_MODEM_OUT_DMA1_LINKPTR  0x38
#define ATI_REG_MODEM_OUT_DMA2_LINKPTR  0x3c
#define ATI_REG_MODEM_OUT_DMA3_LINKPTR  0x40
#define ATI_REG_MODEM_OUT_DMA1_DT_START 0x44
#define ATI_REG_MODEM_OUT_DMA1_DT_NEXT  0x48
#define ATI_REG_MODEM_OUT_DMA1_DT_CUR   0x4c
#define ATI_REG_MODEM_OUT_DMA2_DT_START 0x50
#define ATI_REG_MODEM_OUT_DMA2_DT_NEXT  0x54
#define ATI_REG_MODEM_OUT_DMA2_DT_CUR   0x58
#define ATI_REG_MODEM_OUT_DMA3_DT_START 0x5c
#define ATI_REG_MODEM_OUT_DMA3_DT_NEXT  0x60
#define ATI_REG_MODEM_OUT_DMA3_DT_CUR   0x64
#define ATI_REG_MODEM_OUT_DMA12_DT_SIZE 0x68
#define ATI_REG_MODEM_OUT_DMA3_DT_SIZE  0x6c
#define ATI_REG_MODEM_OUT_FIFO_USED     0x70
#define ATI_REG_MODEM_OUT_GPIO          0x74
#define ATI_REG_MODEM_OUT_GPIO_EN       1
#define ATI_REG_MODEM_OUT_GPIO_DATA_SHIFT 5
#define ATI_REG_MODEM_IN_GPIO           0x78
#define ATI_REG_MODEM_FIFO_FLUSH        0x88
#define ATI_REG_MODEM_FIFO_OUT1_FLUSH   (1U<<0)
#define ATI_REG_MODEM_FIFO_OUT2_FLUSH   (1U<<1)
#define ATI_REG_MODEM_FIFO_OUT3_FLUSH   (1U<<2)
#define ATI_REG_MODEM_FIFO_IN_FLUSH     (1U<<3)

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
    uint32_t regs[0x100 / sizeof(uint32_t)];

    /* DMA Context */
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->regs[ATI_REG_ISR / 4] & s->regs[ATI_REG_IER / 4];
    if (pending) {
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
    if (size != 4) {
        return ~0ULL;
    }

    switch (addr) {
    case ATI_REG_PHYS_IN_ADDR: {
        uint32_t tmp = s->regs[addr / 4];
        /* clear read flag after read */
        s->regs[addr / 4] &= ~ATI_REG_PHYS_IN_READ_FLAG;
        val = tmp;
        break;
    }
    default:
        val = s->regs[addr / 4];
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x100) {
        return;
    }
    if (size != 4) {
        return;
    }

    switch (addr) {
    case ATI_REG_ISR: /* 0x00 */
        s->regs[addr / 4] &= ~val;  /* write-1-to-clear */
        pcibase_update_irq(s);
        break;
    case ATI_REG_IER: /* 0x04 */
        s->regs[addr / 4] = val;
        pcibase_update_irq(s);
        break;
    case ATI_REG_CMD: /* 0x08 */
        s->regs[addr / 4] = val;
        /* Ensure AC-link appears active and modem is present */
        s->regs[addr / 4] |= ATI_REG_CMD_ACLINK_ACTIVE | ATI_REG_CMD_MODEM_PRESENT;
        /* If AC_RESET is asserted, clear codec not ready bits to simulate reset completion */
        if (val & ATI_REG_CMD_AC_RESET) {
            s->regs[ATI_REG_ISR / 4] &= ~(ATI_REG_ISR_CODEC0_NOT_READY |
                                           ATI_REG_ISR_CODEC1_NOT_READY |
                                           ATI_REG_ISR_CODEC2_NOT_READY);
            pcibase_update_irq(s);
        }
        break;
    case ATI_REG_PHYS_OUT_ADDR: /* 0x0c */
    {
        if (val & ATI_REG_PHYS_OUT_RW) { /* read command */
            /* Determine AC97 register index */
            unsigned int reg = (val >> ATI_REG_PHYS_OUT_ADDR_SHIFT) & 0x7f;
            uint16_t data = 0;
            /* Provide dummy AC97 values; specifically, vendor ID to satisfy snd_ac97_mixer */
            if (reg == 0x00) data = 0x8000;  /* Reset register dummy value */
            else if (reg == 0x7c) data = 0x5349;  /* Si3036 vendor ID1 */
            else if (reg == 0x7e) data = 0x4c28; /* vendor ID2 */
            /* set PHYS_IN_ADDR with read flag and data */
            s->regs[ATI_REG_PHYS_IN_ADDR / 4] = ATI_REG_PHYS_IN_READ_FLAG |
                ((uint32_t)data << ATI_REG_PHYS_IN_DATA_SHIFT);
        }
        /* Signal physical I/O completion interrupt */
        s->regs[ATI_REG_ISR / 4] |= ATI_REG_ISR_PHYS_INTR;
        pcibase_update_irq(s);
        /* Clear ADDR_EN to indicate command completion */
        s->regs[addr / 4] = val & ~ATI_REG_PHYS_OUT_ADDR_EN;
        break;
    }
    case ATI_REG_PHYS_IN_ADDR: /* 0x10 */
        /* Driver does not write to this register, but store for completeness */
        s->regs[addr / 4] = val;
        break;
    default:
        s->regs[addr / 4] = val;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Logic for Port I/O (Legacy support) */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Logic for Port I/O (Legacy support) */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    memset(s->regs, 0, sizeof(s->regs));
    /* Initialize CMD with ACLINK_ACTIVE so that AC-link reset sequence completes quickly */
    s->regs[ATI_REG_CMD / 4] = ATI_REG_CMD_ACLINK_ACTIVE;
    /* Set codec not ready bits initially */
    s->regs[ATI_REG_ISR / 4] = ATI_REG_ISR_CODEC0_NOT_READY |
                                ATI_REG_ISR_CODEC1_NOT_READY |
                                ATI_REG_ISR_CODEC2_NOT_READY;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ATI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_ATI_IXP_MODEM);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MODEM);
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
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "atiixp_modem-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
    .name = "snd_atiixp_modem_pci",
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
