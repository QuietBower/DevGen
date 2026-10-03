/*
 * QEMU PCI device model for ULI526x Ethernet controller
 * Based on Linux driver uli526x.c
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

#define TYPE_PCIBASE_DEVICE "uli526x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers */
#define PCI_VENDOR_ID_ULI 0x10B9
#define PCI_DEVICE_ID_ULI5261 0x5261
#define PCI_DEVICE_ID_ULI5263 0x5263

#define ULI526X_IO_SIZE 0x100

/* Register offsets (IO space) */
#define DCR0   0x00
#define DCR1   0x08
#define DCR2   0x10
#define DCR3   0x18
#define DCR4   0x20
#define DCR5   0x28
#define DCR6   0x30
#define DCR7   0x38
#define DCR8   0x40
#define DCR9   0x48
#define DCR10  0x50
#define DCR11  0x58
#define DCR12  0x60
#define DCR13  0x68
#define DCR14  0x70
#define DCR15  0x78

#define DCR_NUM 16

/* CR6 bits */
#define CR6_RXSC     0x00000002
#define CR6_PBF      0x00000008
#define CR6_PM       0x00000040
#define CR6_PAM      0x00000080
#define CR6_FDM      0x00000200
#define CR6_TXSC     0x00002000
#define CR6_STI      0x00100000
#define CR6_SFT      0x00200000
#define CR6_RXA      0x40000000
#define CR6_NO_PURGE 0x20000000

/* CR9 bits */
#define CR9_SROM_READ 0x4800
#define CR9_SRCS     0x1
#define CR9_SRCLK    0x2
#define CR9_CRDOUT   0x8

#define MDIO_DATA_BIT  0x20000
#define MDIO_CLK_BIT   0x40000
#define MDIO_READ_BIT  0x80000

#define SROM_DATA_1    0x4
#define SROM_DATA_0    0x0

#define PHY_ADDR_DEFAULT 1
#define PHY_ID1         0x001c
#define PHY_ID2         0xc910

#define CR0_DEFAULT  0
#define CR6_DEFAULT  0x22200000
#define CR7_DEFAULT  0x180c1
#define CR15_DEFAULT 0x06

#define ULI526X_RESET 0x1

#define PCI_CLASS_ULI526X 0x020000

/* Driver-defined constants */
#define ULI526X_TXTH_256 0x4000
#define FLT_SHIFT 0

typedef enum {
    SROM_IDLE = 0,
    SROM_COMMAND,
    SROM_ADDRESS,
    SROM_DATA
} SROMState;

typedef enum {
    MDIO_IDLE = 0,
    MDIO_PREAMBLE,
    MDIO_START,
    MDIO_OPCODE,
    MDIO_ADDRESS,
    MDIO_REGADDR,
    MDIO_TURNAROUND,
    MDIO_DATA,
    MDIO_DONE
} MDIOState;

struct PCIBaseState {
    PCIDevice pdev;

    MemoryRegion io_bar;

    /* Hardware Registers */
    uint32_t dcr[DCR_NUM];

    /* Status */
    uint32_t intr_status;
    uint32_t intr_mask;

    /* SROM */
    uint16_t srom[64];
    SROMState srom_state;
    uint8_t srom_bitcount;
    uint16_t srom_shift_reg;
    uint8_t srom_address;
    uint8_t srom_cmd;

    /* MDIO */
    MDIOState mdio_state;
    uint8_t mdio_bitcount;
    uint32_t mdio_shift_reg;
    uint8_t mdio_op;
    uint8_t mdio_phy_addr;
    uint8_t mdio_reg_addr;
    bool mdio_read;
    uint8_t mdio_ta_count;

    /* PHY registers */
    uint16_t phy_regs[32];

    /* TX */
    QEMUTimer tx_timer;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    uint32_t status = s->dcr[5] & s->dcr[7];
    if (status) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

static void srom_write(PCIBaseState *s, uint32_t val)
{
    uint8_t prev_srclk = (s->dcr[9] & CR9_SRCLK) ? 1 : 0;
    uint8_t new_srclk = (val & CR9_SRCLK) ? 1 : 0;
    bool rising = (!prev_srclk && new_srclk);
    bool srom_mode = (val & CR9_SROM_READ) != 0;

    if (!srom_mode) {
        s->srom_state = SROM_IDLE;
        return;
    }

    if (rising) {
        uint8_t data_bit = (val & SROM_DATA_1) ? 1 : 0;
        switch (s->srom_state) {
        case SROM_IDLE:
            s->srom_state = SROM_COMMAND;
            s->srom_bitcount = 0;
            s->srom_cmd = 0;
            break;
        case SROM_COMMAND:
            s->srom_cmd = (s->srom_cmd << 1) | data_bit;
            s->srom_bitcount++;
            if (s->srom_bitcount == 3) {
                if (s->srom_cmd == 0x6) { /* 110 */
                    s->srom_state = SROM_ADDRESS;
                    s->srom_bitcount = 0;
                    s->srom_address = 0;
                } else {
                    s->srom_state = SROM_IDLE;
                }
            }
            break;
        case SROM_ADDRESS:
            s->srom_address = (s->srom_address << 1) | data_bit;
            s->srom_bitcount++;
            if (s->srom_bitcount == 6) {
                s->srom_state = SROM_DATA;
                s->srom_bitcount = 0;
                s->srom_shift_reg = s->srom[s->srom_address];
            }
            break;
        case SROM_DATA:
            if (s->srom_bitcount < 16) {
                s->srom_shift_reg <<= 1;
                s->srom_bitcount++;
            }
            break;
        default:
            break;
        }
    } else {
        if (!(val & CR9_SRCS)) {
            s->srom_state = SROM_IDLE;
        }
    }
}

static void mdio_write(PCIBaseState *s, uint32_t val)
{
    uint8_t prev_clk = (s->dcr[9] & MDIO_CLK_BIT) ? 1 : 0;
    uint8_t new_clk = (val & MDIO_CLK_BIT) ? 1 : 0;
    bool rising = (!prev_clk && new_clk);

    if (!rising) {
        return;
    }

    uint8_t data_bit = (val & MDIO_DATA_BIT) ? 1 : 0;

    switch (s->mdio_state) {
    case MDIO_IDLE:
        s->mdio_bitcount = 0;
        s->mdio_state = MDIO_PREAMBLE;
        /* fall through */
    case MDIO_PREAMBLE:
        if (data_bit == 1) {
            s->mdio_bitcount++;
            if (s->mdio_bitcount >= 32) {
                s->mdio_state = MDIO_START;
                s->mdio_bitcount = 0;
                s->mdio_shift_reg = 0;
            }
        } else {
            s->mdio_state = MDIO_IDLE;
        }
        break;
    case MDIO_START:
        s->mdio_shift_reg = (s->mdio_shift_reg << 1) | data_bit;
        s->mdio_bitcount++;
        if (s->mdio_bitcount == 2) {
            if (s->mdio_shift_reg == 0x01) {
                s->mdio_state = MDIO_OPCODE;
                s->mdio_bitcount = 0;
                s->mdio_shift_reg = 0;
            } else {
                s->mdio_state = MDIO_IDLE;
            }
        }
        break;
    case MDIO_OPCODE:
        s->mdio_shift_reg = (s->mdio_shift_reg << 1) | data_bit;
        s->mdio_bitcount++;
        if (s->mdio_bitcount == 2) {
            if (s->mdio_shift_reg == 0x2) { /* 10 = read */
                s->mdio_read = true;
            } else if (s->mdio_shift_reg == 0x1) { /* 01 = write */
                s->mdio_read = false;
            } else {
                s->mdio_state = MDIO_IDLE;
                break;
            }
            s->mdio_state = MDIO_ADDRESS;
            s->mdio_bitcount = 0;
            s->mdio_shift_reg = 0;
        }
        break;
    case MDIO_ADDRESS:
        s->mdio_shift_reg = (s->mdio_shift_reg << 1) | data_bit;
        s->mdio_bitcount++;
        if (s->mdio_bitcount == 5) {
            s->mdio_phy_addr = s->mdio_shift_reg;
            s->mdio_state = MDIO_REGADDR;
            s->mdio_bitcount = 0;
            s->mdio_shift_reg = 0;
        }
        break;
    case MDIO_REGADDR:
        s->mdio_shift_reg = (s->mdio_shift_reg << 1) | data_bit;
        s->mdio_bitcount++;
        if (s->mdio_bitcount == 5) {
            s->mdio_reg_addr = s->mdio_shift_reg;
            s->mdio_state = MDIO_TURNAROUND;
            s->mdio_bitcount = 0;
            s->mdio_ta_count = 0;
        }
        break;
    case MDIO_TURNAROUND:
        if (s->mdio_read) {
            /* PHY drives the first TA bit as 0, second as 0, then data.
             * The driver releases MDIO (writes 1) and clocks.
             * We ignore the data bit written by driver and just count.
             */
            s->mdio_ta_count++;
            if (s->mdio_ta_count == 2) {
                s->mdio_state = MDIO_DATA;
                s->mdio_bitcount = 0;
                /* Load shift register with PHY data, MSB first */
                s->mdio_shift_reg = s->phy_regs[s->mdio_reg_addr];
            }
        } else {
            /* Write: host drives bits */
            s->mdio_shift_reg = (s->mdio_shift_reg << 1) | data_bit;
            s->mdio_bitcount++;
            if (s->mdio_bitcount == 2) {
                s->mdio_state = MDIO_DATA;
                s->mdio_bitcount = 0;
                s->mdio_shift_reg = 0;
            }
        }
        break;
    case MDIO_DATA:
        if (!s->mdio_read) {
            /* Write data */
            s->mdio_shift_reg = (s->mdio_shift_reg << 1) | data_bit;
            s->mdio_bitcount++;
            if (s->mdio_bitcount == 16) {
                s->phy_regs[s->mdio_reg_addr] = s->mdio_shift_reg;
                /* Handle PHY reset */
                if (s->mdio_reg_addr == 0 && (s->mdio_shift_reg & 0x8000)) {
                    s->phy_regs[0] &= ~0x8000;
                }
                s->mdio_state = MDIO_IDLE;
            }
        } else {
            /* Read data: shift out next bit on each rising edge */
            if (s->mdio_bitcount < 16) {
                s->mdio_shift_reg <<= 1;
                s->mdio_bitcount++;
            }
            if (s->mdio_bitcount == 16) {
                s->mdio_state = MDIO_IDLE;
            }
        }
        break;
    default:
        s->mdio_state = MDIO_IDLE;
        break;
    }
}

static uint64_t pcibase_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    /* All IO accesses are 32-bit */
    if (size != 4 || addr >= ULI526X_IO_SIZE) {
        return val;
    }

    int idx = addr >> 3; /* Each DCR occupies 8 bytes? Actually, offsets are 0x00, 0x08, etc. So idx = addr/8 */
    if (idx >= DCR_NUM) {
        return val;
    }

    val = s->dcr[idx];

    switch (idx) {
    case 5: /* DCR5: interrupt status, read-only */
        break;
    case 9: /* DCR9 with SROM/MDIO output bits */
        /* SROM data output */
        if (s->srom_state == SROM_DATA && (s->dcr[9] & CR9_SROM_READ)) {
            if (s->srom_shift_reg & 0x8000) {
                val |= CR9_CRDOUT;
            } else {
                val &= ~CR9_CRDOUT;
            }
        }
        /* MDIO read data output */
        if (s->mdio_state == MDIO_DATA && s->mdio_read) {
            if (s->mdio_shift_reg & 0x8000) {
                val |= MDIO_READ_BIT;
            } else {
                val &= ~MDIO_READ_BIT;
            }
        }
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 || addr >= ULI526X_IO_SIZE) {
        return;
    }

    int idx = addr >> 3;
    if (idx >= DCR_NUM) {
        return;
    }

    switch (idx) {
    case 0: /* DCR0 */
        if (val & ULI526X_RESET) {
            /* Soft reset */
            memset(s->dcr, 0, sizeof(s->dcr));
            s->dcr[0] = CR0_DEFAULT;
            s->dcr[6] = CR6_DEFAULT;
            s->dcr[7] = CR7_DEFAULT;
            s->dcr[15] = CR15_DEFAULT;
            s->srom_state = SROM_IDLE;
            s->mdio_state = MDIO_IDLE;
            pcibase_update_irq(s);
        } else {
            s->dcr[idx] = val;
        }
        break;
    case 1: /* DCR1 */
        s->dcr[idx] = val;
        if (val & 0x1) { /* TX start bit */
            /* Simulate TX completion after a short delay */
            timer_mod(&s->tx_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        }
        break;
    case 5: /* DCR5: interrupt status, write 1 to clear bits */
        s->dcr[5] &= ~(val & s->dcr[7]); /* Clear only if unmasked? Actually, clear regardless */
        pcibase_update_irq(s);
        break;
    case 7: /* DCR7: interrupt mask */
        s->dcr[7] = val;
        pcibase_update_irq(s);
        break;
    case 9: /* DCR9: SROM/MDIO bit-bang */
        srom_write(s, val);
        mdio_write(s, val);
        s->dcr[9] = val;
        break;
    default:
        s->dcr[idx] = val;
        break;
    }
}

static const MemoryRegionOps pcibase_io_ops = {
    .read = pcibase_io_read,
    .write = pcibase_io_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pcibase_tx_timer(void *opaque)
{
    PCIBaseState *s = opaque;

    /* Set TX completion interrupt status bit (DCR5 bit0) */
    s->dcr[5] |= 0x1;
    /* Also set TX available? */
    pcibase_update_irq(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Set PCI config */
    pci_config_set_interrupt_pin(pci_conf, 1);
    pdev->config[PCI_CLASS_PROG] = 0x00; /* Ethernet controller, subclass 0x00 */

    /* Setup IO BAR */
    memory_region_init_io(&s->io_bar, OBJECT(s), &pcibase_io_ops, s,
                          "uli526x-io", ULI526X_IO_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io_bar);

    /* Initialize SROM with dummy content */
    s->srom[0] = 0x1491; /* Check? */
    s->srom[1] = 0x00d0; /* MAC prefix */
    s->srom[2] = 0x1234;
    s->srom[3] = 0x5678; /* MAC */
    s->srom[4] = 0x9abc;
    /* ... set more if needed */

    /* Initialize PHY registers */
    s->phy_regs[0] = 0x1000; /* Basic mode control: auto-negotiation enable */
    s->phy_regs[1] = 0x782d; /* Status */
    s->phy_regs[2] = PHY_ID1;
    s->phy_regs[3] = PHY_ID2;

    /* Set default DCR values */
    s->dcr[0] = CR0_DEFAULT;
    s->dcr[6] = CR6_DEFAULT;
    s->dcr[7] = CR7_DEFAULT;
    s->dcr[15] = CR15_DEFAULT;

    /* TX timer */
    timer_init_ms(&s->tx_timer, QEMU_CLOCK_VIRTUAL, pcibase_tx_timer, s);
}

static void pcibase_exit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    timer_del(&s->tx_timer);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    memset(s->dcr, 0, sizeof(s->dcr));
    s->dcr[0] = CR0_DEFAULT;
    s->dcr[6] = CR6_DEFAULT;
    s->dcr[7] = CR7_DEFAULT;
    s->dcr[15] = CR15_DEFAULT;

    s->srom_state = SROM_IDLE;
    s->mdio_state = MDIO_IDLE;

    timer_del(&s->tx_timer);

    pcibase_update_irq(s);
}

static void pcibase_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pcibase_realize;
    k->exit = pcibase_exit;
    k->vendor_id = PCI_VENDOR_ID_ULI;
    k->device_id = PCI_DEVICE_ID_ULI5261;
    k->revision = 0x10;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->reset = pcibase_reset;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo pcibase_info = {
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}
type_init(pcibase_register_types)
