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

#define TYPE_PCIBASE_DEVICE "tlan_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_TLAN    0x0e11
#define PCI_DEVICE_ID_TLAN    0xb011
#define PCI_CLASS_ID_TLAN     0x0200  /* Network Controller Ethernet */

#define TLAN_IO_SIZE          256      /* IO BAR size (power of 2) */

/* Direct I/O Register Offsets */
#define TLAN_HOST_CMD         0x00
#define TLAN_CH_PARM          0x04
#define TLAN_HOST_INT         0x0A
#define TLAN_HASH_1           0x28
#define TLAN_HASH_2           0x2C
#define TLAN_LED_REG          0x44
#define TLAN_NET_CMD          0x00  /* same as HOST_CMD */
#define TLAN_NET_CONFIG       0x04  /* same as CH_PARM */
#define TLAN_NET_SIO          0x01
#define TLAN_NET_STS          0x02
#define TLAN_NET_MASK         0x03
#define TLAN_DIO_ADR          0x08
#define TLAN_DIO_DATA         0x0C
#define TLAN_AREG_0           0x10
#define TLAN_INT_DIS          0x48
#define TLAN_ACOMMIT          0x43
#define TLAN_MAX_RX           0x46

/* Host Command Bits */
#define TLAN_HC_GO            0x80000000
#define TLAN_HC_RT            0x00080000
#define TLAN_HC_LD_THR        0x00002000
#define TLAN_HC_LD_TMR        0x00004000
#define TLAN_HC_INT_OFF       0x00000800
#define TLAN_HC_INT_ON        0x00000400
#define TLAN_HC_REQ_INT       0x00001000
#define TLAN_HC_AD_RST        0x00008000
#define TLAN_HC_ACK           0x20000000

/* Host Interrupt Bits */
#define TLAN_HI_IT_MASK       0x001C
#define TLAN_HI_IV_MASK       0x1FE0

/* Net SIO Bits */
#define TLAN_NET_SIO_MCLK     0x04
#define TLAN_NET_SIO_MDATA    0x01
#define TLAN_NET_SIO_MTXEN    0x02
#define TLAN_NET_SIO_EDATA    0x10
#define TLAN_NET_SIO_ECLOK    0x40
#define TLAN_NET_SIO_ETXEN    0x20

/* EEPROM size from driver */
#define TLAN_EEPROM_SIZE 256

/* DMA list structures are not used directly in QEMU; removed to fix u32/u16 type errors. */

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
    uint16_t host_int;           /* TLAN_HOST_INT register */
    bool int_enabled;            /* interrupt enable flag */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t io_regs[TLAN_IO_SIZE];  /* direct I/O registers */
    uint8_t dio_mem[256];        /* internal DIO-accessible registers */
    uint16_t dio_addr;            /* current DIO address */
    uint32_t ch_parm;            /* TLAN_CH_PARM register */
    uint32_t host_cmd;           /* TLAN_HOST_CMD register */

    /* Operational status flags */
    uint8_t net_sts;             /* TLAN_NET_STS */

    /* State used to handle reset sequences */
    QEMUTimer reset_timer;
    int reset_state;

    /* Power management state (D0-D3) */
    uint32_t pm_state;

    /* Additional adapter-specific data */
    uint32_t adapter_flags;
    uint16_t addr_ofs;
    uint8_t eeprom[TLAN_EEPROM_SIZE];
    uint16_t phy_regs[2][32];
    int phy_num;

    /* EEPROM serial interface state machine */
    uint8_t ese_phase;        /* 0=idle, 1=recv_cmd, 2=recv_addr, 4=send_ack, 5=send_data */
    uint8_t ese_byte;         /* shift register for incoming/outgoing */
    uint8_t ese_bit_index;    /* bit count 0-7 */
    uint8_t ese_addr;         /* EEPROM address for read */
    uint8_t ese_ack_bit;      /* 0=ACK, 1=NACK */
    uint8_t ese_after_ack;    /* next phase after ack */
    uint8_t ese_data;         /* data byte to send during send_data */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->int_enabled && s->host_int != 0) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* EEPROM serial interface handler - called on writes to DIO register TLAN_NET_SIO */
static void tlan_eeprom_sio_write(PCIBaseState *s, uint8_t new_sio)
{
    uint8_t old_sio = s->dio_mem[TLAN_NET_SIO];
    s->dio_mem[TLAN_NET_SIO] = new_sio;
    uint8_t changed = new_sio ^ old_sio;

    /* Detect start condition: ETXEN high, ECLOK high, EDATA falling from 1 to 0 */
    if (s->ese_phase == 0) {
        if ((new_sio & (TLAN_NET_SIO_ECLOK | TLAN_NET_SIO_ETXEN)) == (TLAN_NET_SIO_ECLOK | TLAN_NET_SIO_ETXEN)) {
            if ((old_sio & TLAN_NET_SIO_EDATA) && !(new_sio & TLAN_NET_SIO_EDATA)) {
                /* Start condition detected */
                s->ese_phase = 1;   /* receiving command */
                s->ese_byte = 0;
                s->ese_bit_index = 0;
                return;
            }
        }
    }

    /* Process clock rising edge */
    if ((changed & TLAN_NET_SIO_ECLOK) && (new_sio & TLAN_NET_SIO_ECLOK)) {
        switch (s->ese_phase) {
        case 1: /* receiving command byte */
        case 2: /* receiving address byte */
            if (new_sio & TLAN_NET_SIO_ETXEN) {
                /* sample EDATA */
                s->ese_byte = (s->ese_byte << 1) | ((new_sio & TLAN_NET_SIO_EDATA) ? 1 : 0);
                s->ese_bit_index++;
                if (s->ese_bit_index == 8) {
                    if (s->ese_phase == 1) {
                        if (s->ese_byte == 0xa0) {
                            s->ese_phase = 4;        /* send ACK */
                            s->ese_ack_bit = 0;      /* ACK = 0 */
                            s->ese_after_ack = 2;    /* then receive address */
                        } else {
                            s->ese_phase = 0;        /* error */
                        }
                    } else { /* phase == 2 */
                        s->ese_addr = s->ese_byte;
                        s->ese_phase = 4;            /* send ACK */
                        s->ese_ack_bit = 0;
                        s->ese_after_ack = 0;        /* return to idle, wait for next start */
                    }
                    s->ese_bit_index = 0;
                }
            }
            break;
        case 4: /* sending ACK */
            if (!(new_sio & TLAN_NET_SIO_ETXEN)) {
                /* set EDATA according to ack bit */
                if (!s->ese_ack_bit)
                    s->dio_mem[TLAN_NET_SIO] &= ~TLAN_NET_SIO_EDATA;
                else
                    s->dio_mem[TLAN_NET_SIO] |= TLAN_NET_SIO_EDATA;

                if (s->ese_after_ack == 2) {
                    s->ese_phase = 2;                /* receive address byte */
                    s->ese_byte = 0;
                    s->ese_bit_index = 0;
                } else if (s->ese_after_ack == 5) {
                    s->ese_phase = 5;                /* send data */
                    s->ese_data = s->eeprom[s->ese_addr];
                    s->ese_bit_index = 0;
                } else {
                    s->ese_phase = 0;                /* idle */
                }
            }
            break;
        case 5: /* sending data */
            if (!(new_sio & TLAN_NET_SIO_ETXEN)) {
                /* output MSB first */
                if (s->ese_data & 0x80)
                    s->dio_mem[TLAN_NET_SIO] |= TLAN_NET_SIO_EDATA;
                else
                    s->dio_mem[TLAN_NET_SIO] &= ~TLAN_NET_SIO_EDATA;
                s->ese_data <<= 1;
                s->ese_bit_index++;
                if (s->ese_bit_index == 8) {
                    s->ese_phase = 0;                /* done */
                }
            }
            break;
        default:
            break;
        }
    }
}

/* PIO Handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint8_t offset = addr & 0xff;

    switch (offset) {
    case 0x00 ... 0x03: /* TLAN_HOST_CMD */
        if (size == 4)
            val = s->host_cmd;
        break;
    case 0x04 ... 0x07: /* TLAN_CH_PARM */
        if (size == 4)
            val = s->ch_parm;
        break;
    case 0x08 ... 0x09: /* TLAN_DIO_ADR (rarely read) */
        if (size == 2)
            val = s->dio_addr;
        break;
    case 0x0A ... 0x0B: /* TLAN_HOST_INT */
        if (size == 2)
            val = s->host_int;
        break;
    case 0x0C ... 0x0F: { /* TLAN_DIO_DATA window */
        uint8_t dio_off = offset - 0x0C;
        uint16_t dio_base = s->dio_addr & 0xFC;
        uint16_t internal_addr = dio_base + (dio_off & 0x3);

        switch (size) {
        case 1:
            val = s->dio_mem[internal_addr];
            break;
        case 2:
            val = lduw_le_p(&s->dio_mem[internal_addr & ~1]);
            break;
        case 4:
            val = ldl_le_p(&s->dio_mem[internal_addr & ~3]);
            break;
        default:
            break;
        }
        break;
    }
    default:
        val = ~0ULL;
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t offset = addr & 0xff;

    switch (offset) {
    case 0x00 ... 0x03: /* TLAN_HOST_CMD */
        if (size == 4) {
            s->host_cmd = (uint32_t)val;
            /* Check for reset command */
            if (val & TLAN_HC_AD_RST) {
                /* Perform adapter reset: clear critical state */
                s->host_int = 0;
                s->int_enabled = false;
                s->ch_parm = 0;
                s->dio_addr = 0;
                s->net_sts = 0;
                s->ese_phase = 0;
                s->ese_bit_index = 0;
                s->ese_byte = 0;
                s->ese_addr = 0;
                s->ese_data = 0;
                memset(s->io_regs, 0, sizeof(s->io_regs));
                memset(s->dio_mem, 0, sizeof(s->dio_mem));
                s->host_cmd &= ~TLAN_HC_AD_RST; /* clear reset bit after action */
            }
            /* Handle interrupt on/off */
            if (val & TLAN_HC_INT_ON) {
                s->int_enabled = true;
                pcibase_update_irq(s);
            }
            if (val & TLAN_HC_INT_OFF) {
                s->int_enabled = false;
                pcibase_update_irq(s);
            }
            /* If GO bit is set, we would start DMA here, but we omit DMA for now */
        }
        break;
    case 0x04 ... 0x07: /* TLAN_CH_PARM */
        if (size == 4)
            s->ch_parm = (uint32_t)val;
        break;
    case 0x08 ... 0x09: /* TLAN_DIO_ADR */
        if (size == 2)
            s->dio_addr = (uint16_t)val;
        break;
    case 0x0A ... 0x0B: /* TLAN_HOST_INT */
        if (size == 2) {
            /* Write-1-to-clear */
            s->host_int &= ~((uint16_t)val);
            pcibase_update_irq(s);
        }
        break;
    case 0x0C ... 0x0F: { /* TLAN_DIO_DATA window */
        uint8_t dio_off = offset - 0x0C;
        uint16_t dio_base = s->dio_addr & 0xFC;
        uint16_t internal_addr = dio_base + (dio_off & 0x3);

        switch (size) {
        case 1:
            s->dio_mem[internal_addr] = (uint8_t)val;
            if (internal_addr == TLAN_NET_SIO) {
                tlan_eeprom_sio_write(s, (uint8_t)val);
            }
            break;
        case 2:
            stw_le_p(&s->dio_mem[internal_addr & ~1], (uint16_t)val);
            break;
        case 4:
            stl_le_p(&s->dio_mem[internal_addr & ~3], (uint32_t)val);
            break;
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* MMIO handlers (unused) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    /* Reset registers to default state */
    s->host_cmd = 0;
    s->ch_parm = 0;
    s->host_int = 0;
    s->int_enabled = false;
    s->dio_addr = 0;
    s->net_sts = 0;
    s->ese_phase = 0;
    s->ese_bit_index = 0;
    s->ese_byte = 0;
    s->ese_addr = 0;
    s->ese_data = 0;
    memset(s->io_regs, 0, sizeof(s->io_regs));
    memset(s->dio_mem, 0, sizeof(s->dio_mem));
    /* Do not clear eeprom content; it is fixed */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TLAN );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_TLAN );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID_TLAN );
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
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = TLAN_IO_SIZE,
        .name = "tlan-io"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X initialization needed; driver uses legacy IRQ */

    /* Initialize EEPROM with a known MAC address (offset 0) */
    s->eeprom[0] = 0x02;
    s->eeprom[1] = 0x00;
    s->eeprom[2] = 0x00;
    s->eeprom[3] = 0x00;
    s->eeprom[4] = 0x00;
    s->eeprom[5] = 0x00;

    /* Final state initialization */
    memset(s->io_regs, 0, sizeof(s->io_regs));
    memset(s->dio_mem, 0, sizeof(s->dio_mem));
    s->dio_addr = 0;
    s->ch_parm = 0;
    s->host_int = 0;
    s->host_cmd = 0;
    s->net_sts = 0;
    s->pm_state = 0; /* D0 */
    s->int_enabled = false;
    s->ese_phase = 0;
    s->ese_bit_index = 0;
    s->ese_byte = 0;
    s->ese_addr = 0;
    s->ese_data = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* No timers to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "tlan_pci",
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
