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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

/* Additional include files retrieved from driver context */
/* No additional include files */

#define TYPE_PCIBASE_DEVICE "i2c_thunderx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM 0x177d
#define PCI_DEVICE_ID_THUNDER_TWSI 0xa012
#define SYS_FREQ_DEFAULT 800000000
#define OTX2_REF_FREQ_DEFAULT 100000000
#define TWSI_INT_ENA_W1C 0x1028
#define TWSI_INT_ENA_W1S 0x1030
#define TWSI_INT_CORE_INT BIT_ULL(2)
#define TWSI_INT_ST_INT BIT_ULL(0)
#define TWSI_INT_TS_INT BIT_ULL(1)
#define IS_LS_FREQ(twsi_freq) ((twsi_freq) <= 400000)
#define SW_TWSI_EOP_TWSI_RST (SW_TWSI_OP_EOP | 7ULL << SW_TWSI_EOP_SHIFT)
#define STAT_IDLE 0xF8
#define octeon_i2c_stat_read(i2c)                                         \
        octeon_i2c_reg_read(i2c, SW_TWSI_EOP_TWSI_STAT, NULL)
#define PCI_SUBSYS_MASK GENMASK(15, 12)
#define PCI_SUBSYS_DEVID_9XXX 0xB
#define OCTEON_REG_MODE(x) ((x)->roff.mode)
#define SW_TWSI_EOP_TWSI_CLKCTL (SW_TWSI_OP_EOP | 3ULL << SW_TWSI_EOP_SHIFT)
#define TWSX_MODE_HS_MASK (TWSX_MODE_REFCLK_SRC | TWSX_MODE_HS_MODE)
#define SW_TWSI_OP_TWSI_CLK (4ULL << SW_TWSI_OP_SHIFT)
#define OCTEON_REG_BLOCK_CTL(x) ((x)->roff.block_ctl)
#define octeon_i2c_ctl_write(i2c, val)                                   \
        octeon_i2c_reg_write(i2c, SW_TWSI_EOP_TWSI_CTL, val)
#define TWSI_CTL_ENAB 0x40
#define TWSI_CTL_CE 0x80
#define TWSI_CTL_IFLG 0x08
#define octeon_i2c_ctl_read(i2c)                                        \
        octeon_i2c_reg_read(i2c, SW_TWSI_EOP_TWSI_CTL, NULL)
#define TWSI_CTL_STP 0x10
#define TWSI_CTL_AAK 0x04
#define TWSI_CTL_STA 0x20
#define SW_TWSI_OP_EOP (6ULL << SW_TWSI_OP_SHIFT)
#define SW_TWSI_EOP_SHIFT 32
#define SW_TWSI_V BIT_ULL(63)
#define OCTEON_REG_SW_TWSI(x) ((x)->roff.sw_twsi)
#define SW_TWSI_EOP_TWSI_STAT (SW_TWSI_OP_EOP | 3ULL << SW_TWSI_EOP_SHIFT)
#define TWSX_MODE_REFCLK_SRC BIT(4)
#define TWSX_MODE_HS_MODE BIT(0)
#define SW_TWSI_OP_SHIFT 57
#define TWSI_INT_SDA BIT_ULL(10)
#define TWSI_INT_SCL_OVR BIT_ULL(9)
#define TWSI_INT_SDA_OVR BIT_ULL(8)
#define TWSI_INT_SCL BIT_ULL(11)
#define OCTEON_REG_BLOCK_FIFO(x) ((x)->roff.block_fifo)
#define SW_TWSI_ADDR_SHIFT 40
#define TWSX_BLOCK_STS_RESET_PTR BIT(0)
#define OCTEON_REG_BLOCK_STS(x) ((x)->roff.block_sts)
#define SW_TWSI_R BIT_ULL(56)
#define SW_TWSI_OP_7_IA (1ULL << SW_TWSI_OP_SHIFT)
#define SW_TWSI_SOVR BIT_ULL(55)
#define OCTEON_REG_SW_TWSI_EXT(x) ((x)->roff.sw_twsi_ext)
#define SW_TWSI_SIZE_SHIFT 52
#define octeon_i2c_data_write(i2c, val)                                  \
        octeon_i2c_reg_write(i2c, SW_TWSI_EOP_TWSI_DATA, val)
#define SW_TWSI_OP_7 (0ULL << SW_TWSI_OP_SHIFT)
#define octeon_i2c_data_read(i2c, error)                                \
        octeon_i2c_reg_read(i2c, SW_TWSI_EOP_TWSI_DATA, error)
#define STAT_REP_START 0x10
#define STAT_START 0x08
#define SW_TWSI_EOP_TWSI_CTL (SW_TWSI_OP_EOP | 2ULL << SW_TWSI_EOP_SHIFT)
#define OCTEON_REG_TWSI_INT(x) ((x)->roff.twsi_int)
#define TWSX_MODE_BLOCK_MODE BIT(2)
#define STAT_TXADDR_ACK 0x18
#define STAT_AD2W_ACK 0xD0
#define STAT_SLAVE_60 0x60
#define STAT_SLAVE_88 0x88
#define STAT_AD2W_NAK 0xD8
#define STAT_WDOG_TOUT 0xF0
#define STAT_TXDATA_NAK 0x30
#define BUS_MON_RST_MASK BIT(3)
#define STAT_SLAVE_NAK 0xC0
#define STAT_RXDATA_ACK 0x50
#define STAT_TXADDR_NAK 0x20
#define STAT_RXADDR_NAK 0x48
#define STAT_SLAVE_ACK 0xC8
#define STAT_GENDATA_NAK 0x98
#define STAT_SLAVE_70 0x70
#define STAT_TXDATA_ACK 0x28
#define STAT_RXADDR_ACK 0x40
#define STAT_LOST_ARB_38 0x38
#define STAT_RXDATA_NAK 0x58
#define STAT_LOST_ARB_78 0x78
#define STAT_SLAVE_A8 0xA8
#define STAT_SLAVE_LOST 0xB8
#define STAT_LOST_ARB_B0 0xB0
#define STAT_GENDATA_ACK 0x90
#define STAT_BUS_ERROR 0x00
#define STAT_LOST_ARB_68 0x68
#define STAT_SLAVE_80 0x80
#define STAT_SLAVE_A0 0xA0
#define SW_TWSI_EIA BIT_ULL(61)
#define SW_TWSI_IA_SHIFT 32
#define SW_TWSI_EOP_TWSI_DATA (SW_TWSI_OP_EOP | 1ULL << SW_TWSI_EOP_SHIFT)
#define I2C_OCTEON_EVENT_WAIT 80

#define BAR0_SIZE 0x10000  /* Plausible size for BAR0, containing MMIO registers */

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
    uint64_t intr_status;
    uint64_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint64_t sw_twsi_reg;
    uint64_t twsi_int;
    uint64_t twsi_int_enable;
    uint64_t sw_twsi_ext;
    uint64_t mode;
    uint64_t block_ctl;
    uint64_t block_sts;
    uint64_t block_fifo;

    /* Internal registers accessed via SW_TWSI mechanism */
    uint8_t tws_data;
    uint8_t tws_ctl;
    uint8_t tws_stat;

    /* MSI-X BAR region */
    MemoryRegion msix_bar;

    /* Operational status flags */
    /* No status flags defined */

    /* State used to handle reset sequences */
    /* No reset-specific state */

    /* Additional state from driver structs */
    struct octeon_i2c_reg_offset {
        unsigned int sw_twsi;
        unsigned int twsi_int;
        unsigned int sw_twsi_ext;
        unsigned int mode;
        unsigned int block_ctl;
        unsigned int block_sts;
        unsigned int block_fifo;
    } roff;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Interrupt logic: if any enabled interrupt bits are set, raise MSI-X */
    if ((s->twsi_int & s->twsi_int_enable) && msix_enabled(pdev)) {
        msix_notify(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x1000: /* SW_TWSI */
        val = s->sw_twsi_reg;
        break;
    case 0x1010: /* TWSI_INT */
        val = s->twsi_int;
        break;
    case 0x1018: /* SW_TWSI_EXT */
        val = s->sw_twsi_ext;
        break;
    case 0x1028: /* TWSI_INT_ENA_W1C (read returns enable mask) */
        val = s->twsi_int_enable;
        break;
    case 0x1030: /* TWSI_INT_ENA_W1S */
        val = s->twsi_int_enable;
        break;
    case 0x1038: /* MODE */
        val = s->mode;
        break;
    case 0x1048: /* BLOCK_CTL */
        val = s->block_ctl;
        break;
    case 0x1050: /* BLOCK_STS */
        val = s->block_sts;
        break;
    case 0x1058: /* BLOCK_FIFO */
        val = s->block_fifo;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void sw_twsi_process_command(PCIBaseState *s, uint64_t val)
{
    if (!(val & BIT_ULL(63))) {  /* V bit not set */
        return;
    }

    bool is_read = (val & BIT_ULL(56)) != 0;
    uint8_t op = (val >> 57) & 0x7;
    uint8_t eop = (val >> 32) & 0x7;
    uint8_t data = val & 0xFF;
    uint8_t *target = NULL;

    switch (op) {
    case 6: /* SW_TWSI_OP_EOP */
        switch (eop) {
        case 1: target = &s->tws_data; break;
        case 2: target = &s->tws_ctl; break;
        case 3: target = &s->tws_stat; break; /* also CLKCTL */
        default: break;
        }
        break;
    case 4: /* SW_TWSI_OP_TWSI_CLK - not used in provided driver */
        break;
    default:
        break;
    }

    if (target) {
        if (is_read) {
            data = *target;
        } else {
            *target = data;
        }
    }

    /* Update sw_twsi_reg: clear V bit, and set low byte appropriately */
    uint64_t new_val = val & ~(BIT_ULL(63) | BIT_ULL(56)); /* clear V and R */
    if (is_read) {
        new_val = (new_val & ~0xFFULL) | data;
    } else {
        /* keep the written data in low byte */
        new_val = (new_val & ~0xFFULL) | (val & 0xFF);
    }
    s->sw_twsi_reg = new_val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x1000: /* SW_TWSI write */
        s->sw_twsi_reg = val;
        sw_twsi_process_command(s, val);
        break;
    case 0x1010: /* TWSI_INT - write to clear */
        s->twsi_int &= ~val;
        pcibase_update_irq(s); /* check if IRQ should be lowered */
        break;
    case 0x1018:
        s->sw_twsi_ext = val;
        break;
    case 0x1028: /* TWSI_INT_ENA_W1C - clear enable bits */
        s->twsi_int_enable &= ~val;
        pcibase_update_irq(s);
        break;
    case 0x1030: /* TWSI_INT_ENA_W1S - set enable bits */
        s->twsi_int_enable |= val;
        pcibase_update_irq(s);
        break;
    case 0x1038:
        s->mode = val;
        break;
    case 0x1048:
        s->block_ctl = val;
        break;
    case 0x1050:
        s->block_sts = val;
        break;
    case 0x1058:
        s->block_fifo = val;
        break;
    default:
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

    /* Reset all register shadows to default values */
    s->sw_twsi_reg = 0;
    s->twsi_int = 0;
    s->twsi_int_enable = 0;
    s->sw_twsi_ext = 0;
    s->mode = 0;
    s->block_ctl = 0;
    s->block_sts = 0;
    s->block_fifo = 0;

    s->tws_data = 0;
    s->tws_ctl = 0;
    s->tws_stat = STAT_IDLE; /* 0xF8, indicates idle state */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x177d);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0xa012);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0xff);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 2; /* BAR0 for MMIO, BAR1 for MSI-X */
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "bar0"
    };
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* BAR1 for MSI-X: RAM type */
    memory_region_init_ram(&s->msix_bar, OBJECT(s), "msix-bar", 0x2000, errp);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_bar);

    /* MSI-X initialization */
    if (msix_init(pdev, 1, &s->msix_bar, 1, 0, &s->msix_bar, 1, 0x1000, 0, errp)) {
        return;
    }

    /* No DMA configuration */
    /* No timer configuration */
    /* Final state initialization: none */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->msix_bar, &s->msix_bar);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No uninit logic defined */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i2c_thunderx_pci",
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
