/*
 * DM1105 QEMU PCI Device Model
 * Based on Linux driver dm1105.c (drivers/media/pci/dm1105/dm1105.c)
 * Implements the hardware interface for a DVB PCI card.
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
#include "hw/i2c/i2c.h"

#define TYPE_PCIBASE_DEVICE "dm1105_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_TRIGEM 0x109f
#define PCI_DEVICE_ID_DM1105  0x036f
#define PCI_CLASS_DM1105      0x048000
#define DM1105_BAR0_SIZE      256

/* Register offsets */
#define DM1105_TSCTR        0x00
#define DM1105_DTALENTH     0x04
#define DM1105_GPIOVAL      0x08
#define DM1105_GPIOCTR      0x0c
#define DM1105_PIDN         0x10
#define DM1105_CWSEL        0x14
#define DM1105_HOST_CTR     0x18
#define DM1105_HOST_AD      0x1c
#define DM1105_CR           0x30
#define DM1105_RST          0x34
#define DM1105_STADR        0x38
#define DM1105_RLEN         0x3c
#define DM1105_WRP          0x40
#define DM1105_INTCNT       0x44
#define DM1105_INTMAK       0x48
#define DM1105_INTSTS       0x4c
#define DM1105_ODD          0x50
#define DM1105_EVEN         0x58
#define DM1105_PID          0x60
#define DM1105_IRCTR        0x64
#define DM1105_IRMODE       0x68
#define DM1105_SYSTEMCODE   0x6c
#define DM1105_IRCODE       0x70
#define DM1105_ENCRYPT      0x74
#define DM1105_VER          0x7c
#define DM1105_I2CCTR       0x80
#define DM1105_I2CSTS       0x81
#define DM1105_I2CDAT       0x82
#define DM1105_I2C_RA       0x83

/* Interrupt masks and status bits */
#define INTMAK_TSIRQM       0x01
#define INTMAK_HIRQM        0x04
#define INTMAK_IRM          0x08
#define INTMAK_ALLMASK      (INTMAK_TSIRQM | INTMAK_HIRQM | INTMAK_IRM)
#define INTMAK_NONEMASK     0x00
#define INTSTS_TSIRQ        0x01
#define INTSTS_HIRQ         0x04
#define INTSTS_IR           0x08

/* DMA related constants */
#define DM1105_DMA_PACKETS       47
#define DM1105_DMA_PACKET_LENGTH (128*4)
#define DM1105_DMA_BYTES         (128 * 4 * DM1105_DMA_PACKETS)

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

/* I2C Slave type declaration */
#define TYPE_DM1105_I2C_SLAVE "dm1105-i2c-slave"
typedef struct DM1105I2CSlaveState DM1105I2CSlaveState;
OBJECT_DECLARE_SIMPLE_TYPE(DM1105I2CSlaveState, DM1105_I2C_SLAVE)

struct DM1105I2CSlaveState {
    I2CSlave parent_obj;
    uint8_t regs[256];
    uint8_t reg_addr;
    bool first_byte;
};

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint8_t intsts;
    uint8_t intmak;

    uint8_t io_buf[DM1105_BAR0_SIZE];

    /* DMA state */
    uint32_t stradr;
    uint32_t rlen;
    uint32_t wrp_offset;
    uint8_t  intcnt_val;
    bool cr;
    QEMUTimer dma_timer;

    /* I2C bus */
    I2CBus *i2c_bus;

    /* GPIO shadow */
    uint32_t gpioval;
    uint32_t gpioctr;
};

static void dm1105_irq_update(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int level = ((s->intsts & s->intmak) != 0);
    pci_set_irq(pdev, level);
}

static void dm1105_dma_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    int packets = s->intcnt_val ? s->intcnt_val : DM1105_DMA_PACKETS;
    int packet_size = s->io_buf[DM1105_DTALENTH];
    uint32_t rlen = s->rlen ? s->rlen : 5 * DM1105_DMA_BYTES;

    if (!s->cr || !s->stradr) {
        return;
    }

    for (int i = 0; i < packets; i++) {
        uint8_t packet[188];
        memset(packet, 0xFF, sizeof(packet));
        packet[0] = 0x47;
        dma_addr_t dest = s->stradr + s->wrp_offset;
        pci_dma_write(pdev, dest, packet, packet_size);
        s->wrp_offset += packet_size;
        if (s->wrp_offset >= rlen) {
            s->wrp_offset = 0;
        }
    }

    s->intsts |= INTSTS_TSIRQ;
    dm1105_irq_update(s);

    if (s->cr) {
        timer_mod(&s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
    }
}

static void dm1105_i2c_transfer(PCIBaseState *s, uint8_t cmd)
{
    I2CBus *bus = s->i2c_bus;
    uint8_t len = cmd & 0x3f;
    uint8_t addr_byte = s->io_buf[DM1105_I2CDAT];
    bool is_read = addr_byte & 1;
    uint8_t i2c_addr = addr_byte >> 1;
    int ret;

    if (!bus) {
        return;
    }

    if (is_read) {
        ret = i2c_start_transfer(bus, i2c_addr, false); /* false for read */
        if (ret < 0) {
            goto err;
        }
        for (int i = 0; i < len; i++) {
            s->io_buf[DM1105_I2CDAT + 1 + i] = i2c_recv(bus);
        }
        i2c_end_transfer(bus);
    } else {
        ret = i2c_start_transfer(bus, i2c_addr, true); /* true for write */
        if (ret < 0) {
            goto err;
        }
        for (int i = 0; i < len; i++) {
            ret = i2c_send(bus, s->io_buf[DM1105_I2CDAT + 1 + i]);
            if (ret < 0) {
                goto err;
            }
        }
        i2c_end_transfer(bus);
    }

    s->io_buf[DM1105_I2CSTS] = 0x40;
    return;

err:
    s->io_buf[DM1105_I2CSTS] = 0x80;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > DM1105_BAR0_SIZE) {
        return ~0ULL;
    }

    switch (size) {
    case 1:
        val = s->io_buf[addr];
        break;
    case 2:
        val = lduw_le_p(&s->io_buf[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->io_buf[addr]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "dm1105: unsupported pio read size %u\n", size);
        return ~0ULL;
    }

    /* Special handling for WRP (offset 0x40) */
    if (addr == DM1105_WRP && size == 4) {
        val = s->stradr + s->wrp_offset;
        stl_le_p(&s->io_buf[DM1105_WRP], val);
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > DM1105_BAR0_SIZE) {
        return;
    }

    /* Write shadow register */
    switch (size) {
    case 1:
        s->io_buf[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(&s->io_buf[addr], (uint16_t)val);
        break;
    case 4:
        stl_le_p(&s->io_buf[addr], (uint32_t)val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "dm1105: unsupported pio write size %u\n", size);
        return;
    }

    /* Side effects */
    switch (addr) {
    case DM1105_CR:
        s->cr = (val & 1);
        if (s->cr) {
            timer_mod(&s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
        } else {
            timer_del(&s->dma_timer);
        }
        break;
    case DM1105_RST:
        if (val & 1) {
            s->wrp_offset = 0;
        }
        break;
    case DM1105_STADR:
        if (size == 4) {
            s->stradr = (uint32_t)val;
        }
        break;
    case DM1105_RLEN:
        if (size == 4) {
            s->rlen = (uint32_t)val;
        }
        break;
    case DM1105_INTCNT:
        s->intcnt_val = (uint8_t)val;
        break;
    case DM1105_INTMAK:
        s->intmak = (uint8_t)val;
        dm1105_irq_update(s);
        break;
    case DM1105_INTSTS:
        /* Write 1 to clear */
        s->intsts &= ~((uint8_t)val);
        s->io_buf[DM1105_INTSTS] = s->intsts;
        dm1105_irq_update(s);
        break;
    case DM1105_I2CCTR:
        dm1105_i2c_transfer(s, (uint8_t)val);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    return ~0ULL;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
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

    memset(s->io_buf, 0, sizeof(s->io_buf));
    s->intsts = 0;
    s->intmak = 0;
    s->stradr = 0;
    s->rlen = 0;
    s->wrp_offset = 0;
    s->intcnt_val = 0;
    s->cr = false;
    timer_del(&s->dma_timer);
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

/* I2C Slave callbacks */
static int dm1105_i2c_slave_event(I2CSlave *i2c, enum i2c_event event)
{
    DM1105I2CSlaveState *s = DM1105_I2C_SLAVE(i2c);
    switch (event) {
    case I2C_START_SEND:
        s->first_byte = true;
        break;
    case I2C_START_RECV:
        s->first_byte = false;
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t dm1105_i2c_slave_recv(I2CSlave *i2c)
{
    DM1105I2CSlaveState *s = DM1105_I2C_SLAVE(i2c);
    return s->regs[s->reg_addr++];
}

static int dm1105_i2c_slave_send(I2CSlave *i2c, uint8_t data)
{
    DM1105I2CSlaveState *s = DM1105_I2C_SLAVE(i2c);
    if (s->first_byte) {
        s->reg_addr = data;
        s->first_byte = false;
    } else {
        s->regs[s->reg_addr++] = data;
    }
    return 0;
}

static void dm1105_i2c_slave_init(Object *obj)
{
    DM1105I2CSlaveState *s = DM1105_I2C_SLAVE(obj);
    memset(s->regs, 0xff, sizeof(s->regs));
    /* Emulate a STV0299 frontend to allow UNKNOWN board probe success */
    s->regs[0x00] = 0x0c;  /* STV0299 chip ID */
    s->reg_addr = 0;
    s->first_byte = true;
}

static void dm1105_i2c_slave_class_init(ObjectClass *klass, void *data)
{
    I2CSlaveClass *sc = I2C_SLAVE_CLASS(klass);
    sc->event = dm1105_i2c_slave_event;
    sc->recv = dm1105_i2c_slave_recv;
    sc->send = dm1105_i2c_slave_send;
}

static const TypeInfo dm1105_i2c_slave_info = {
    .name = TYPE_DM1105_I2C_SLAVE,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(DM1105I2CSlaveState),
    .instance_init = dm1105_i2c_slave_init,
    .class_init = dm1105_i2c_slave_class_init,
};

static void dm1105_i2c_slave_register_types(void)
{
    type_register_static(&dm1105_i2c_slave_info);
}
type_init(dm1105_i2c_slave_register_types);

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_TRIGEM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_DM1105);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DM1105);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 0: PIO */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = DM1105_BAR0_SIZE,
        .name = "dm1105-io",
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize DMA timer */
    timer_init_ms(&s->dma_timer, QEMU_CLOCK_VIRTUAL, dm1105_dma_timer, s);

    /* Create I2C bus and attach a slave that emulates a STV0299 frontend */
    s->i2c_bus = i2c_init_bus(DEVICE(pdev), "i2c");
    i2c_slave_create_simple(s->i2c_bus, TYPE_DM1105_I2C_SLAVE, 0x68);
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
    timer_del(&s->dma_timer);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "dm1105_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(intsts, PCIBaseState),
        VMSTATE_UINT8(intmak, PCIBaseState),
        VMSTATE_BUFFER(io_buf, PCIBaseState),
        VMSTATE_UINT32(stradr, PCIBaseState),
        VMSTATE_UINT32(rlen, PCIBaseState),
        VMSTATE_UINT32(wrp_offset, PCIBaseState),
        VMSTATE_UINT8(intcnt_val, PCIBaseState),
        VMSTATE_BOOL(cr, PCIBaseState),
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
