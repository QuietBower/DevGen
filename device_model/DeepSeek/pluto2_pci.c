/*
 * QEMU Pluto2 PCI device emulation for Pluto2 DVB-S card
 * Based on Linux driver pluto2.c analysis.
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

#define TYPE_PCIBASE_DEVICE "pluto2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_SCM       0x0432
#define PCI_DEVICE_ID_PLUTO2    0x0001
#define PLUTO2_CLASS_ID         0x000000  /* unknown, driver does not enforce class */

/* Register offsets */
#define REG_PIDn(n)      ((n) << 2)   /* 0x00 - 0x1c (if n=0..7) */
#define REG_PCAR          0x0020
#define REG_TSCR          0x0024
#define REG_MISC          0x0028
#define REG_MMAC          0x002c
#define REG_IMAC          0x0030
#define REG_LMAC          0x0034
#define REG_SPID          0x0038
#define REG_SLCS          0x003c

/* Bit definitions for PID registers */
#define PID0_NOFIL        (0x0001 << 16)
#define PIDn_ENP          (0x0001 << 15)
#define PID0_END          (0x0001 << 14)
#define PID0_AFIL         (0x0001 << 13)
#define PIDn_PID          (0x1fff <<  0)

/* TSCR bits */
#define TSCR_NBPACKETS    (0x00ff << 24)
#define TSCR_DEM          (0x0001 << 17)
#define TSCR_DE           (0x0001 << 16)
#define TSCR_RSTN         (0x0001 << 15)
#define TSCR_MSKO         (0x0001 << 14)
#define TSCR_MSKA         (0x0001 << 13)
#define TSCR_MSKL         (0x0001 << 12)
#define TSCR_OVR          (0x0001 << 11)
#define TSCR_AFUL         (0x0001 << 10)
#define TSCR_LOCK         (0x0001 <<  9)
#define TSCR_IACK         (0x0001 <<  8)
#define TSCR_ADEF         (0x007f <<  0)

/* MISC bits */
#define MISC_DVR          (0x0fff <<  4)
#define MISC_ALED         (0x0001 <<  3)
#define MISC_FRST         (0x0001 <<  2)
#define MISC_LED1         (0x0001 <<  1)
#define MISC_LED0         (0x0001 <<  0)

/* SPID bits */
#define SPID_SPIDR        (0x00ff <<  0)

/* SLCS bits */
#define SLCS_SCL          (0x0001 <<  7)
#define SLCS_SDA          (0x0001 <<  6)
#define SLCS_CSN          (0x0001 <<  2)
#define SLCS_OVR          (0x0001 <<  1)
#define SLCS_SWC          (0x0001 <<  0)

/* DMA constants */
#define TS_DMA_PACKETS    8
#define TS_DMA_BYTES      (188 * TS_DMA_PACKETS)

#define NHWFILTERS        8

struct pluto_regs {
    uint32_t pid[NHWFILTERS];  /* 0x00 - 0x1c */
    uint32_t pcar;             /* 0x20 */
    uint32_t tscr;             /* 0x24 */
    uint32_t misc;             /* 0x28 */
    uint32_t mmac;             /* 0x2c */
    uint32_t imac;             /* 0x30 */
    uint32_t lmac;             /* 0x34 */
    uint32_t spid;             /* 0x38 */
    uint32_t slcs;             /* 0x3c */
} QEMU_PACKED;

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

struct i2c_slave_state {
    bool scl;           /* last written SCL level */
    bool sda_wr;        /* last written SDA level (master drive) */
    bool sda_rd;        /* slave drive output (if not driven, line is high) */
    uint8_t shift_reg;
    int bit_count;
    bool addressed;
    bool read_mode;
    uint8_t reg_addr;
    bool reg_addr_set;
    int ack_hold;       /* not used in new state machine */
    bool ack_processed; /* not used */
    /* Transmit state for read responses */
    bool tx_active;
    uint8_t tx_data;
    int tx_bit;
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct pluto_regs regs;

    /* DMA Context */
    QEMUTimer dma_timer;
    uint8_t dummy_pkt_buffer[TS_DMA_BYTES]; /* pre-filled dummy TS packets */

    /* I2C bit-bang emulation */
    struct i2c_slave_state i2c;
};

static void pluto2_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t tscr = s->regs.tscr;
    bool irq = false;

    /* Check interrupt conditions per driver's ISR: DE, OVR, LOCK, AFUL with masks */
    if ((tscr & TSCR_DE)   && !(tscr & TSCR_DEM))  irq = true;
    if ((tscr & TSCR_OVR)  && !(tscr & TSCR_MSKO)) irq = true;
    if ((tscr & TSCR_LOCK) && !(tscr & TSCR_MSKL)) irq = true;
    if ((tscr & TSCR_AFUL) && !(tscr & TSCR_MSKA)) irq = true;

    pci_set_irq(pdev, irq ? 1 : 0);
}

static void pluto2_dma_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t tscr = s->regs.tscr;

    /* Only do DMA if TS is running (RSTN set) and no pending DE, and PCAR is set */
    if (!(tscr & TSCR_RSTN) || (tscr & TSCR_DE) || s->regs.pcar == 0) {
        return;
    }

    /* Write dummy TS packets to guest DMA buffer */
    pci_dma_write(pdev, s->regs.pcar, s->dummy_pkt_buffer, TS_DMA_BYTES);

    /* Update TSCR to indicate completion: set NBPACKETS and DE */
    s->regs.tscr = (s->regs.tscr & ~TSCR_NBPACKETS) | (TS_DMA_PACKETS << 24);
    s->regs.tscr |= TSCR_DE;

    pluto2_update_irq(s);
}

/* Improved I2C bit-bang state machine with proper ACK timing and read response */
static void pluto2_i2c_update(PCIBaseState *s, bool new_scl, bool new_sda_wr)
{
    struct i2c_slave_state *st = &s->i2c;
    bool old_scl = st->scl;
    bool old_sda_wr = st->sda_wr;

    /* START or repeated START (SDA 1->0 while SCL high) */
    if (new_scl == 1 && new_sda_wr == 0 && old_sda_wr == 1) {
        /* reset byte-level state, keep address info for repeated start */
        st->bit_count = 0;
        st->shift_reg = 0;
        st->read_mode = false;
        st->tx_active = false;
        st->tx_bit = 0;
        st->sda_rd = 1; /* release */
    }
    /* STOP (SDA 0->1 while SCL high) */
    else if (new_scl == 1 && new_sda_wr == 1 && old_sda_wr == 0) {
        /* full reset */
        st->bit_count = 0;
        st->shift_reg = 0;
        st->addressed = false;
        st->read_mode = false;
        st->reg_addr = 0;
        st->reg_addr_set = false;
        st->tx_active = false;
        st->tx_bit = 0;
        st->sda_rd = 1;
    }

    /* Rising edge of SCL */
    if (new_scl == 1 && old_scl == 0) {
        if (!st->tx_active) {
            /* Receive mode */
            if (st->bit_count < 8) {
                /* sample SDA */
                st->shift_reg = (st->shift_reg << 1) | (new_sda_wr ? 1 : 0);
                st->bit_count++;
                if (st->bit_count == 8) {
                    /* Byte received, decide ACK */
                    if (!st->addressed) {
                        /* Address byte */
                        if ((st->shift_reg & 0xFE) == (0x08 << 1)) {
                            st->addressed = true;
                            st->read_mode = st->shift_reg & 1;
                            st->sda_rd = 0; /* ACK */
                        } else {
                            st->sda_rd = 1; /* NAK */
                        }
                    } else {
                        /* Data byte */
                        st->sda_rd = 0; /* ACK always */
                    }
                }
            } else if (st->bit_count == 8) {
                /* ACK bit rising edge, do nothing (sda_rd already set) */
            }
        } else {
            /* Transmit mode: we have set sda_rd on falling edge, master samples now */
            /* Do nothing */
        }
    }
    /* Falling edge of SCL */
    if (new_scl == 0 && old_scl == 1) {
        if (!st->tx_active) {
            if (st->bit_count == 8) {
                /* End of byte transfer (after ACK bit) */
                /* Process received data */
                if (st->addressed && !st->read_mode) {
                    if (!st->reg_addr_set) {
                        st->reg_addr = st->shift_reg;
                        st->reg_addr_set = true;
                    }
                }
                /* If we are addressed and in read mode, start transmitting response */
                if (st->addressed && st->read_mode) {
                    /* Enter transmit mode */
                    if (st->reg_addr_set && st->reg_addr == 0x00) {
                        /* Register 0x00: chip ID (TDA10046) */
                        st->tx_data = 0x46;
                    } else {
                        st->tx_data = 0x00;
                    }
                    st->tx_active = true;
                    st->tx_bit = 0;
                    /* Set first bit (MSB) on this falling edge */
                    s->i2c.sda_rd = (st->tx_data >> 7) & 1;
                    st->tx_bit = 1;
                }
                /* Release SDA after ACK (if not entering transmit mode, line goes high) */
                if (!st->tx_active) {
                    st->sda_rd = 1;
                }
                /* Prepare for next byte */
                st->bit_count = 0;
                st->shift_reg = 0;
            }
        } else {
            /* Transmit mode: output next bit */
            if (st->tx_bit < 8) {
                /* Set SDA to next bit */
                uint8_t bit = (st->tx_data >> (7 - st->tx_bit)) & 1;
                s->i2c.sda_rd = bit;
                st->tx_bit++;
            } else {
                /* We've sent 8 bits, release SDA for master ACK */
                st->sda_rd = 1;
                st->tx_active = false;
                st->tx_bit = 0;
                /* After transmission, we go idle; master may continue or STOP */
            }
        }
    }
    st->scl = new_scl;
    st->sda_wr = new_sda_wr;
}

static uint64_t pluto2_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x40 || size != 4) {
        return 0;
    }

    /* Map addr directly to register array assuming sequential layout */
    uint32_t *regs = (uint32_t *)&s->regs;
    val = regs[addr >> 2];

    /* Handle special read for SLCS to return actual SDA state */
    if (addr == REG_SLCS) {
        /* Replace SDA bit with i2c_sda_rd */
        val = (val & ~SLCS_SDA) | (s->i2c.sda_rd ? SLCS_SDA : 0);
        /* SCL bit reflects last written value */
    }

    return val;
}

static void pluto2_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x40 || size != 4) {
        return;
    }

    uint32_t *regs = (uint32_t *)&s->regs;
    uint32_t reg_idx = addr >> 2;
    uint32_t old_val = regs[reg_idx];

    /* Handle special write effects for certain registers */
    switch (addr) {
    case REG_PCAR:
        /* Start DMA if conditions are met */
        regs[reg_idx] = val;
        if ((s->regs.tscr & TSCR_RSTN) && !(s->regs.tscr & TSCR_DE) && val != 0) {
            timer_mod(&s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        }
        break;
    case REG_TSCR: {
        /* Mask out read-only NBPACKETS field */
        val &= ~TSCR_NBPACKETS;
        /* Handle IACK to clear interrupt status */
        if (val & TSCR_IACK) {
            val &= ~(TSCR_DE | TSCR_OVR | TSCR_LOCK | TSCR_AFUL);
        }
        regs[reg_idx] = val;
        pluto2_update_irq(s);
        break;
    }
    case REG_SLCS: {
        /* Extract SCL and SDA bits from new value */
        bool new_scl = !!(val & SLCS_SCL);
        bool new_sda_wr = !!(val & SLCS_SDA);
        /* Preserve other bits (CSN, OVR, SWC) */
        uint32_t other_bits = val & (SLCS_CSN | SLCS_OVR | SLCS_SWC);
        bool other_bits_changed = (other_bits != (old_val & (SLCS_CSN | SLCS_OVR | SLCS_SWC)));
        regs[reg_idx] = val;  /* store full value */

        /* Update i2c state machine with new SCL and SDA */
        pluto2_i2c_update(s, new_scl, new_sda_wr);
        break;
    }
    default:
        regs[reg_idx] = val;
        break;
    }
}

static const MemoryRegionOps pluto2_mmio_ops = {
    .read = pluto2_mmio_read,
    .write = pluto2_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* BAR1: serial number memory region */
static const char serial_data[] = "SN:00001";
static const size_t serial_len = sizeof(serial_data) - 1; /* excluding null */

static uint64_t pluto2_serial_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    /* The driver reads from offset 0xe0 */
    for (int i = 0; i < size; i++) {
        uint8_t byte;
        if (addr + i >= 0xe0 && addr + i < 0xe0 + serial_len) {
            byte = serial_data[addr + i - 0xe0];
        } else {
            byte = 0xff;  /* end marker */
        }
        val |= (uint64_t)byte << (i * 8);
    }
    return val;
}

static void pluto2_serial_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore writes */
}

static const MemoryRegionOps pluto2_serial_ops = {
    .read = pluto2_serial_read,
    .write = pluto2_serial_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pluto2_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->regs, 0, sizeof(s->regs));
    /* Set initial values from driver expectations */
    s->regs.misc = 0x1000; /* board revision 1.0, DVR=0x1000 */
    s->regs.slcs = SLCS_SCL | SLCS_SDA; /* SCL and SDA high for I2C idle */
    /* MAC registers set to some default */
    s->regs.mmac = 0x0011;
    s->regs.imac = 0x2233;
    s->regs.lmac = 0x4455;

    /* Initialize I2C state */
    s->i2c.scl = 1;
    s->i2c.sda_wr = 1;
    s->i2c.sda_rd = 1;
    s->i2c.shift_reg = 0;
    s->i2c.bit_count = 0;
    s->i2c.addressed = false;
    s->i2c.read_mode = false;
    s->i2c.reg_addr = 0;
    s->i2c.reg_addr_set = false;
    s->i2c.ack_hold = 0;
    s->i2c.ack_processed = false;
    s->i2c.tx_active = false;
    s->i2c.tx_data = 0;
    s->i2c.tx_bit = 0;
}

static void pluto2_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 0) {
            memory_region_init_io(mr, OBJECT(s), &pluto2_mmio_ops, s, bi->name, aligned_size);
        } else if (bi->index == 1) {
            memory_region_init_io(mr, OBJECT(s), &pluto2_serial_ops, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &pluto2_mmio_ops, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pluto2_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x0432);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0001);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PLUTO2_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo) { .index = 0, .type = BAR_TYPE_MMIO, .size = 0x40, .name = "bar0" };
    s->bar_info[1] = (BARInfo) { .index = 1, .type = BAR_TYPE_MMIO, .size = 0x100, .name = "bar1" };
    for (int i = 0; i < s->num_bars; i++) {
        pluto2_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Prepare dummy TS packets (all zeros for simplicity, but first byte 0x47 per packet) */
    for (int pkt = 0; pkt < TS_DMA_PACKETS; pkt++) {
        s->dummy_pkt_buffer[pkt * 188] = 0x47;
    }

    /* Initialize DMA timer */
    timer_init_ms(&s->dma_timer, QEMU_CLOCK_VIRTUAL, pluto2_dma_timer, s);

    /* Final state initialization */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.slcs = SLCS_SCL | SLCS_SDA; /* idle high */
    s->i2c.scl = 1;
    s->i2c.sda_wr = 1;
    s->i2c.sda_rd = 1;
    s->i2c.ack_hold = 0;
    s->i2c.ack_processed = false;
    s->i2c.tx_active = false;
    s->i2c.tx_data = 0;
    s->i2c.tx_bit = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(&s->dma_timer);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pluto2_pci",
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
    dc->reset  = pluto2_reset;
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
