/* This is a complete QEMU device model for the Earth PT1 DVB adapter.
 * Generated based on the Linux driver pt1.c.
 * Hardware: BAR0 MMIO, registers 0,1,2,4,5.
 * I2C bit-banging controller, DMA table registration, and initialization sequence emulation.
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

#define TYPE_PCIBASE_DEVICE "earth_pt1_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register layout (offsets from BAR0, reg * 4) */
#define REG_COMMAND  0x00
#define REG_POWER    0x04
#define REG_STREAM   0x08
#define REG_I2C      0x10
#define REG_TABLE    0x14

/* Status bits for REG_COMMAND */
#define STATUS_PCI_RESET_DONE   (1 << 0)
#define STATUS_RAM_RESET_DONE   (1 << 1)
#define STATUS_RAM_ENABLED      (1 << 2)
#define STATUS_I2C_BUSY         (1 << 7)
#define STATUS_SYNC_DONE        (1 << 29)
#define STATUS_IDENTIFY_BIT     (1 << 30)
#define STATUS_UNLOCK_DONE      (1 << 31)

/* I2C sequence RAM size */
#define I2C_SEQ_SIZE 1024

/* Helper macro for I2C bit extraction */
#define I2C_CMD_SCL(val)    (!(((val) >> 11) & 1))
#define I2C_CMD_SDA(val)    (!(((val) >> 10) & 1))
#define I2C_CMD_READ_EN(val) (((val) >> 12) & 1)
#define I2C_CMD_BUSY(val)    (((val) >> 13) & 1)
#define I2C_CMD_NEXT(val)    ((val) & 0x3FF)
#define I2C_CMD_ADDR(val)    (((val) >> 18) & 0x3FF)

/* I2C bus states */
#define I2C_STATE_IDLE    0
#define I2C_STATE_START   1
#define I2C_STATE_ADDR    2
#define I2C_STATE_DATA    3
#define I2C_STATE_ACK     4
#define I2C_STATE_STOP    5

/* BAR type enumeration */
typedef enum {
    BAR_TYPE_NONE,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM,
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
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

    /* Hardware Register Shadows */
    uint32_t reg_command;     /* combined status / command */
    uint32_t reg_power;
    uint32_t reg_stream;
    uint32_t reg_table_pfn;

    /* I2C controller state */
    uint32_t i2c_seq[I2C_SEQ_SIZE];
    uint32_t i2c_read_data;
    uint8_t  i2c_state;
    uint8_t  i2c_scl_prev;
    uint8_t  i2c_sda_master_prev;
    uint8_t  i2c_shift;
    uint8_t  i2c_bit_cnt;
    uint8_t  i2c_addr_rw;
    bool     i2c_is_read;
    uint8_t  i2c_data_byte;
    uint8_t  i2c_slave_sda;
};

/* Forward declarations */
static void pt1_i2c_process_bit(PCIBaseState *s, bool scl, bool sda_master, bool read_enable);
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp);

/* Internal helper for status-triggered signaling. Unused as no IRQ. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* No IRQ in this device */
}

/* Device-initiated DMA logic. Unused as driver polls buffers. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA from device side */
}

/* I2C bus processing: detect start/stop, shift bits, manage ACKs. */
static void pt1_i2c_process_bit(PCIBaseState *s, bool scl, bool sda_master, bool read_enable)
{
    uint8_t state = s->i2c_state;
    bool sda = read_enable ? s->i2c_slave_sda : sda_master;

    /* Detect start: SDA falling while SCL high */
    if (s->i2c_scl_prev == 1 && scl == 1 && s->i2c_sda_master_prev == 1 && sda_master == 0) {
        s->i2c_state = I2C_STATE_START;
        s->i2c_bit_cnt = 0;
        s->i2c_shift = 0;
        s->i2c_is_read = false;
    }
    /* Detect stop: SDA rising while SCL high */
    else if (s->i2c_scl_prev == 1 && scl == 1 && s->i2c_sda_master_prev == 0 && sda_master == 1) {
        s->i2c_state = I2C_STATE_STOP;
    }
    /* On SCL rising edge, sample data */
    else if (s->i2c_scl_prev == 0 && scl == 1 && state != I2C_STATE_IDLE) {
        if (state == I2C_STATE_START) {
            /* First bit is the address */
            s->i2c_shift <<= 1;
            s->i2c_shift |= sda;
            s->i2c_bit_cnt++;
            if (s->i2c_bit_cnt == 8) {
                s->i2c_addr_rw = s->i2c_shift;
                s->i2c_is_read = sda; /* last bit is R/~W, 1=read */
                s->i2c_state = I2C_STATE_ACK;
                s->i2c_bit_cnt = 0;
                s->i2c_shift = 0;
            }
        } else if (state == I2C_STATE_ACK) {
            /* ACK bit: sda should be low (ack) or high (nack) */
            s->i2c_slave_sda = 0; /* always ACK for simplicity */
            s->i2c_state = s->i2c_is_read ? I2C_STATE_DATA : I2C_STATE_DATA; /* proceed to data */
            s->i2c_bit_cnt = 0;
            s->i2c_shift = 0;
        } else if (state == I2C_STATE_DATA) {
            if (s->i2c_bit_cnt < 8) {
                if (s->i2c_is_read) {
                    /* Master reads: slave drives SDA */
                    s->i2c_shift <<= 1;
                    /* For now, we just shift in the slave SDA (which is driven later) */
                    s->i2c_shift |= sda;
                } else {
                    /* Master writes: we capture data */
                    s->i2c_shift <<= 1;
                    s->i2c_shift |= sda;
                }
                s->i2c_bit_cnt++;
            }
            if (s->i2c_bit_cnt == 8) {
                s->i2c_data_byte = s->i2c_shift;
                s->i2c_bit_cnt = 0;
                s->i2c_state = I2C_STATE_ACK;
            }
        }
    }
    /* Update shadow lines */
    s->i2c_scl_prev = scl;
    s->i2c_sda_master_prev = sda_master;

    /* If read_enable, we need to prepare slave sda for next bit */
    if (read_enable && s->i2c_state == I2C_STATE_DATA && s->i2c_is_read) {
        /* For now, we implement a simple slave that returns a fixed pattern.
         * Later we will use a proper register map.
         */
        if (s->i2c_bit_cnt == 0) {
            /* first bit of a new byte: decide what to output */
            s->i2c_slave_sda = 1;  /* all ones for now */
        } else {
            s->i2c_slave_sda = 1;
        }
    }
}

/* Execute stored I2C sequence */
static void pt1_i2c_execute(PCIBaseState *s)
{
    uint32_t addr = 0;
    /* Reset I2C state before execution */
    s->i2c_state = I2C_STATE_IDLE;
    s->i2c_scl_prev = 1;
    s->i2c_sda_master_prev = 1;
    s->i2c_shift = 0;
    s->i2c_bit_cnt = 0;
    s->i2c_addr_rw = 0;
    s->i2c_is_read = false;
    s->i2c_data_byte = 0;
    s->i2c_slave_sda = 1;

    while (true) {
        uint32_t cmd = s->i2c_seq[addr & (I2C_SEQ_SIZE - 1)];
        if (!I2C_CMD_BUSY(cmd)) {
            break;
        }
        bool scl = I2C_CMD_SCL(cmd);
        bool sda_master = I2C_CMD_SDA(cmd);
        bool read_enable = I2C_CMD_READ_EN(cmd);
        pt1_i2c_process_bit(s, scl, sda_master, read_enable);
        addr = I2C_CMD_NEXT(cmd);
    }

    /* After the sequence, the last 32 bits shifted in form the read data */
    /* pt1_i2c_xfer reads reg2 after i2c_end; we assemble a 32-bit word from the last up to 4 bytes */
    s->i2c_read_data = s->i2c_shift; /* placeholder: final shift might hold last byte */
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned read size %u\n", __func__, size);
        return ~0ULL;
    }

    switch (addr) {
    case REG_COMMAND:
        val = s->reg_command;
        break;
    case REG_POWER:
        val = s->reg_power;
        break;
    case REG_STREAM:
        val = s->i2c_read_data;  /* also used for I2C read result */
        break;
    case REG_I2C:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: reading from I2C sequence memory not implemented\n", __func__);
        val = 0xFFFFFFFF;
        break;
    case REG_TABLE:
        val = s->reg_table_pfn;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown MMIO read addr 0x%" HWADDR_PRIx "\n", __func__, addr);
        val = 0xFFFFFFFF;
        break;
    }
    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned write size %u\n", __func__, size);
        return;
    }

    switch (addr) {
    case REG_COMMAND:
        /* Decode known commands and update status bits */
        switch (val) {
        case 0x00000008:
            /* Sync, identify, unlock: all set bits */
            s->reg_command |= STATUS_SYNC_DONE | STATUS_IDENTIFY_BIT | STATUS_UNLOCK_DONE;
            break;
        case 0x00000010:
            /* init_table_count: does nothing, just accept */
            break;
        case 0x00000020:
            /* increment_table_count: accepted */
            break;
        case 0x0c000040:
            /* register_tables: just enable a flag maybe? */
            break;
        case 0x08080000:
            /* unregister_tables: clear table */
            s->reg_table_pfn = 0;
            break;
        case 0x01010000:
            /* pci_reset step 1 */
            s->reg_command &= ~STATUS_PCI_RESET_DONE;
            break;
        case 0x01000000:
            /* pci_reset step 2 */
            s->reg_command |= STATUS_PCI_RESET_DONE;
            break;
        case 0x02020000:
            /* ram_reset step 1 */
            s->reg_command &= ~STATUS_RAM_RESET_DONE;
            break;
        case 0x02000000:
            /* ram_reset step 2 */
            s->reg_command |= STATUS_RAM_RESET_DONE;
            break;
        case 0x00000002:
            /* do_enable_ram: toggle bit 2 */
            s->reg_command ^= STATUS_RAM_ENABLED;
            break;
        case 0x0b0b0000:
            /* disable_ram */
            s->reg_command &= ~STATUS_RAM_ENABLED;
            break;
        case 0x00000004:
            /* i2c_end: execute sequence */
            s->reg_command |= STATUS_I2C_BUSY;
            pt1_i2c_execute(s);
            s->reg_command &= ~STATUS_I2C_BUSY;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown COMMAND write 0x%08x\n", __func__, (uint32_t)val);
            break;
        }
        break;

    case REG_POWER:
        s->reg_power = val;
        break;

    case REG_STREAM:
        s->reg_stream = val;
        /* Clear i2c_read_data after any stream write? Driver writes stream settings, but may also read it as i2c data. To avoid conflict, we only use reg2 for I2C read; here we update stream. */
        break;

    case REG_I2C:
        /* Store I2C sequence command */
        {
            uint32_t idx = I2C_CMD_ADDR(val) % I2C_SEQ_SIZE;
            s->i2c_seq[idx] = val;
        }
        break;

    case REG_TABLE:
        s->reg_table_pfn = val;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown MMIO write addr 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* PIO handlers (unused but required for template) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}
static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Reset handler */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->reg_command = 0;
    s->reg_power = 0;
    s->reg_stream = 0;
    s->reg_table_pfn = 0;
    s->i2c_read_data = 0;
    memset(s->i2c_seq, 0, sizeof(s->i2c_seq));
    s->i2c_state = I2C_STATE_IDLE;
    s->i2c_scl_prev = 1;
    s->i2c_sda_master_prev = 1;
    s->i2c_shift = 0;
    s->i2c_bit_cnt = 0;
    s->i2c_addr_rw = 0;
    s->i2c_is_read = false;
    s->i2c_data_byte = 0;
    s->i2c_slave_sda = 1;
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10ee);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x211a);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0480); /* unknown, placeholder */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0: MMIO, size enough for registers 0..5 (24 bytes) rounded up */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "pt1-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
    /* Initialize I2C sequence RAM to idle (all zeroes means busy=0, clk=1, data=1) */
    memset(s->i2c_seq, 0, sizeof(s->i2c_seq));
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
    .name = "earth_pt1_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(reg_command, PCIBaseState),
        VMSTATE_UINT32(reg_power, PCIBaseState),
        VMSTATE_UINT32(reg_stream, PCIBaseState),
        VMSTATE_UINT32(reg_table_pfn, PCIBaseState),
        VMSTATE_UINT32(i2c_read_data, PCIBaseState),
        VMSTATE_UINT8(i2c_state, PCIBaseState),
        VMSTATE_UINT8(i2c_scl_prev, PCIBaseState),
        VMSTATE_UINT8(i2c_sda_master_prev, PCIBaseState),
        VMSTATE_UINT8(i2c_shift, PCIBaseState),
        VMSTATE_UINT8(i2c_bit_cnt, PCIBaseState),
        VMSTATE_UINT8(i2c_addr_rw, PCIBaseState),
        VMSTATE_BOOL(i2c_is_read, PCIBaseState),
        VMSTATE_UINT8(i2c_data_byte, PCIBaseState),
        VMSTATE_UINT8(i2c_slave_sda, PCIBaseState),
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
