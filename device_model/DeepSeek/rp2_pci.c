/*
 * Generated QEMU PCI device model for RocketPort 2 serial card.
 * Based on Linux driver rp2.c.
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

#define TYPE_PCIBASE_DEVICE "rp2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_RP 0x11fe
#define RP_DEVICE_ID 0x0040
#define PCI_CLASS_ID 0x0700

/* Register offsets and macros from rp2 driver */
#define RP2_FPGA_CTL0           0x110
#define RP2_FPGA_CTL1           0x11c
#define RP2_IRQ_MASK            0x1ec
#define RP2_IRQ_MASK_EN_m       BIT(0)
#define RP2_IRQ_STATUS          0x1f0
#define RP2_ASIC_SPACING        0x1000
#define RP2_ASIC_OFFSET(i)      ((i) << ilog2(RP2_ASIC_SPACING))
#define RP2_PORT_BASE           0x000
#define RP2_PORT_SPACING        0x040
#define RP2_UCODE_BASE          0x400
#define RP2_UCODE_SPACING       0x80
#define RP2_CLK_PRESCALER       0xc00
#define RP2_CH_IRQ_STAT         0xc04
#define RP2_CH_IRQ_MASK         0xc08
#define RP2_ASIC_IRQ            0xd00
#define RP2_ASIC_IRQ_EN_m       BIT(20)
#define RP2_GLOBAL_CMD          0xd0c
#define RP2_ASIC_CFG            0xd04
#define RP2_DATA_DWORD          0x000
#define RP2_DATA_BYTE           0x008
#define RP2_DATA_BYTE_ERR_PARITY_m    BIT(8)
#define RP2_DATA_BYTE_ERR_OVERRUN_m   BIT(9)
#define RP2_DATA_BYTE_ERR_FRAMING_m   BIT(10)
#define RP2_DATA_BYTE_BREAK_m         BIT(11)
#define RP2_DUMMY_READ          BIT(16)
#define RP2_DATA_BYTE_EXCEPTION_MASK   (RP2_DATA_BYTE_ERR_PARITY_m | \
                     RP2_DATA_BYTE_ERR_OVERRUN_m | \
                     RP2_DATA_BYTE_ERR_FRAMING_m | \
                     RP2_DATA_BYTE_BREAK_m)
#define RP2_RX_FIFO_COUNT       0x00c
#define RP2_TX_FIFO_COUNT       0x00e
#define RP2_CHAN_STAT           0x010
#define RP2_CHAN_STAT_RXDATA_m  BIT(0)
#define RP2_CHAN_STAT_DCD_m     BIT(3)
#define RP2_CHAN_STAT_DSR_m     BIT(4)
#define RP2_CHAN_STAT_CTS_m     BIT(5)
#define RP2_CHAN_STAT_RI_m      BIT(6)
#define RP2_CHAN_STAT_OVERRUN_m BIT(13)
#define RP2_CHAN_STAT_DSR_CHANGED_m BIT(16)
#define RP2_CHAN_STAT_CTS_CHANGED_m BIT(17)
#define RP2_CHAN_STAT_CD_CHANGED_m  BIT(18)
#define RP2_CHAN_STAT_RI_CHANGED_m  BIT(22)
#define RP2_CHAN_STAT_TXEMPTY_m     BIT(25)
#define RP2_CHAN_STAT_MS_CHANGED_MASK (RP2_CHAN_STAT_DSR_CHANGED_m | \
                     RP2_CHAN_STAT_CTS_CHANGED_m | \
                     RP2_CHAN_STAT_CD_CHANGED_m | \
                     RP2_CHAN_STAT_RI_CHANGED_m)
#define RP2_TXRX_CTL            0x014
#define RP2_TXRX_CTL_MSRIRQ_m   BIT(0)
#define RP2_TXRX_CTL_RXIRQ_m    BIT(2)
#define RP2_TXRX_CTL_RX_TRIG_s  3
#define RP2_TXRX_CTL_RX_TRIG_m  (0x3 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_TRIG_1  (0x1 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_TRIG_256 (0x2 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_TRIG_448 (0x3 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_EN_m    BIT(5)
#define RP2_TXRX_CTL_RTSFLOW_m  BIT(6)
#define RP2_TXRX_CTL_DTRFLOW_m  BIT(7)
#define RP2_TXRX_CTL_TX_TRIG_s  16
#define RP2_TXRX_CTL_TX_TRIG_m  (0x3 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_DSRFLOW_m  BIT(18)
#define RP2_TXRX_CTL_TXIRQ_m    BIT(19)
#define RP2_TXRX_CTL_CTSFLOW_m  BIT(23)
#define RP2_TXRX_CTL_TX_EN_m    BIT(24)
#define RP2_TXRX_CTL_RTS_m      BIT(25)
#define RP2_TXRX_CTL_DTR_m      BIT(26)
#define RP2_TXRX_CTL_LOOP_m     BIT(27)
#define RP2_TXRX_CTL_BREAK_m    BIT(28)
#define RP2_TXRX_CTL_CMSPAR_m   BIT(29)
#define RP2_TXRX_CTL_nPARODD_m  BIT(30)
#define RP2_TXRX_CTL_PARENB_m   BIT(31)
#define RP2_UART_CTL            0x018
#define RP2_UART_CTL_MODE_s     0
#define RP2_UART_CTL_MODE_m     (0x7 << RP2_UART_CTL_MODE_s)
#define RP2_UART_CTL_MODE_rs232 (0x1 << RP2_UART_CTL_MODE_s)
#define RP2_UART_CTL_FLUSH_RX_m BIT(3)
#define RP2_UART_CTL_FLUSH_TX_m BIT(4)
#define RP2_UART_CTL_RESET_CH_m BIT(5)
#define RP2_UART_CTL_XMIT_EN_m  BIT(6)
#define RP2_UART_CTL_DATABITS_s 8
#define RP2_UART_CTL_DATABITS_m (0x3 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_8 (0x3 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_7 (0x2 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_6 (0x1 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_5 (0x0 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_STOPBITS_m BIT(10)
#define RP2_BAUD                0x01c
#define RP2_TX_SWFLOW           0x02
#define RP2_TX_SWFLOW_ena       0x81
#define RP2_TX_SWFLOW_dis       0x9d
#define RP2_RX_SWFLOW           0x0c
#define RP2_RX_SWFLOW_ena       0x81
#define RP2_RX_SWFLOW_dis       0x8d
#define RP2_RX_FIFO             0x37
#define RP2_RX_FIFO_ena         0x08
#define RP2_RX_FIFO_dis         0x81
#define RP_ID(prod) PCI_VDEVICE(RP, (prod))
#define RP_CAP(ports, smpte) (((ports) << 8) | ((smpte) << 0))

/* Emulation constants */
#define N_PORTS      16
#define PORTS_PER_ASIC 16
#define ALL_PORTS_MASK ((1 << N_PORTS) - 1)

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

typedef struct {
    uint32_t txrx_ctl;
    uint32_t uart_ctl;
    uint16_t baud_div;
    uint32_t chan_stat;   /* dynamic status + latched change bits */
} RP2Channel;

typedef struct {
    uint32_t clk_prescaler;
    uint32_t ch_irq_mask;
    uint32_t asic_irq;      /* bit20 = enable */
    uint16_t asic_cfg;
    uint16_t global_cmd;
} RP2Asic;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t irq_status;
    uint32_t irq_mask;

    /* BAR0 registers */
    uint32_t fpga_ctl0;
    uint32_t fpga_ctl1;
    uint32_t bar0_irq_mask;  /* offset 0x1ec */
    uint32_t bar0_irq_status; /* offset 0x1f0 */

    /* BAR1 devices */
    RP2Asic asic;
    RP2Channel channels[N_PORTS];
};

/* Internal helper for status-triggered signaling */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t ch_irq_stat = 0;

    /* Compute raw channel interrupt requests */
    for (int i = 0; i < N_PORTS; i++) {
        RP2Channel *ch = &s->channels[i];
        uint32_t ctl = ch->txrx_ctl;
        uint32_t stat = ch->chan_stat;
        bool rx_irq = (stat & RP2_CHAN_STAT_RXDATA_m) && (ctl & RP2_TXRX_CTL_RXIRQ_m);
        bool tx_irq = (stat & RP2_CHAN_STAT_TXEMPTY_m) && (ctl & RP2_TXRX_CTL_TXIRQ_m);
        bool ms_irq = (stat & RP2_CHAN_STAT_MS_CHANGED_MASK) && (ctl & RP2_TXRX_CTL_MSRIRQ_m);
        if (rx_irq || tx_irq || ms_irq) {
            ch_irq_stat |= (1 << i);
        }
    }

    uint32_t masked = ch_irq_stat & ~s->asic.ch_irq_mask;
    bool asic_irq_enabled = s->asic.asic_irq & RP2_ASIC_IRQ_EN_m;
    bool card_irq_enabled = s->bar0_irq_mask & RP2_IRQ_MASK_EN_m;
    bool irq_raised = (masked != 0) && asic_irq_enabled && card_irq_enabled;

    pci_set_irq(pdev, irq_raised);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == 0x110 && size == 4) {
        val = s->fpga_ctl0;
    } else if (addr == 0x11c && size == 4) {
        val = s->fpga_ctl1;
    } else if (addr == 0x1ec && size == 4) {
        val = s->bar0_irq_mask;
    } else if (addr == 0x1f0 && size == 4) {
        val = s->bar0_irq_status;
    } else if (addr >= 0x000 && addr < (N_PORTS * 0x40)) {
        /* BAR1 channel registers */
        int ch = addr / 0x40;
        int offset = addr % 0x40;
        RP2Channel *ch_state = &s->channels[ch];

        if (offset == 0x000 && size == 4) {
            val = 0; /* DATA_DWORD */
        } else if (offset == 0x008 && size == 2) {
            val = 0; /* DATA_BYTE */
        } else if (offset == 0x00c && size == 2) {
            val = 0; /* RX_FIFO_COUNT, empty */
        } else if (offset == 0x00e && size == 2) {
            val = 0; /* TX_FIFO_COUNT, empty */
        } else if (offset == 0x010 && size == 4) {
            uint32_t stat = ch_state->chan_stat;
            /* Add live modem status bits (all high) and TXEMPTY (FIFO empty) */
            stat |= RP2_CHAN_STAT_DCD_m | RP2_CHAN_STAT_DSR_m |
                    RP2_CHAN_STAT_CTS_m | RP2_CHAN_STAT_RI_m;
            stat |= RP2_CHAN_STAT_TXEMPTY_m;
            val = stat;
        } else if (offset == 0x014 && size == 4) {
            val = ch_state->txrx_ctl;
        } else if (offset == 0x018 && size == 4) {
            val = ch_state->uart_ctl;
        } else if (offset == 0x01c && size == 2) {
            val = ch_state->baud_div;
        } else {
            qemu_log_mask(LOG_UNIMP, "rp2: unimplemented read addr 0x%"PRIx64" size %u\n", addr, size);
        }
    } else if (addr >= 0x400 && addr < 0x400 + N_PORTS * 0x80) {
        /* Ucode region, reads return 0 */
        val = 0;
    } else if (addr >= 0xc00 && addr < 0xd10) {
        /* ASIC registers */
        if (addr == 0xc00 && size == 4) {
            val = s->asic.clk_prescaler;
        } else if (addr == 0xc04 && size == 4) {
            uint32_t stat = 0;
            for (int i = 0; i < N_PORTS; i++) {
                RP2Channel *ch = &s->channels[i];
                uint32_t ctl = ch->txrx_ctl;
                uint32_t st = ch->chan_stat;
                bool rx_irq = (st & RP2_CHAN_STAT_RXDATA_m) && (ctl & RP2_TXRX_CTL_RXIRQ_m);
                bool tx_irq = (st & RP2_CHAN_STAT_TXEMPTY_m) && (ctl & RP2_TXRX_CTL_TXIRQ_m);
                bool ms_irq = (st & RP2_CHAN_STAT_MS_CHANGED_MASK) && (ctl & RP2_TXRX_CTL_MSRIRQ_m);
                if (rx_irq || tx_irq || ms_irq) {
                    stat |= (1 << i);
                }
            }
            val = stat;
        } else if (addr == 0xc08 && size == 4) {
            val = s->asic.ch_irq_mask;
        } else if (addr == 0xd00 && size == 4) {
            val = s->asic.asic_irq;
        } else if (addr == 0xd04 && size == 2) {
            val = s->asic.asic_cfg;
        } else if (addr == 0xd0c && size == 2) {
            val = s->asic.global_cmd;
        } else {
            qemu_log_mask(LOG_UNIMP, "rp2: unimplemented read addr 0x%"PRIx64" size %u\n", addr, size);
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "rp2: unimplemented read addr 0x%"PRIx64" size %u\n", addr, size);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == 0x110 && size == 4) {
        s->fpga_ctl0 = val;
    } else if (addr == 0x11c && size == 4) {
        s->fpga_ctl1 = val;
    } else if (addr == 0x1ec && size == 4) {
        s->bar0_irq_mask = val;
        pcibase_update_irq(s);
    } else if (addr == 0x1f0 && size == 4) {
        s->bar0_irq_status = val; /* Ignored, no effect */
    } else if (addr >= 0x000 && addr < (N_PORTS * 0x40)) {
        int ch = addr / 0x40;
        int offset = addr % 0x40;
        RP2Channel *ch_state = &s->channels[ch];

        if (offset == 0x000 && size == 4) {
            /* DATA_DWORD, ignore */
        } else if (offset == 0x008 && size == 2) {
            /* DATA_BYTE write to TX FIFO, no effect */
        } else if (offset == 0x010 && size == 4) {
            /* CHAN_STAT: clear W1C bits */
            ch_state->chan_stat &= ~val;
            pcibase_update_irq(s);
        } else if (offset == 0x014 && size == 4) {
            ch_state->txrx_ctl = val;
            pcibase_update_irq(s);
        } else if (offset == 0x018 && size == 4) {
            ch_state->uart_ctl = val;
        } else if (offset == 0x01c && size == 2) {
            ch_state->baud_div = val;
        } else {
            qemu_log_mask(LOG_UNIMP, "rp2: unimplemented write addr 0x%"PRIx64" size %u val 0x%"PRIx64"\n", addr, size, val);
        }
    } else if (addr >= 0x400 && addr < 0x400 + N_PORTS * 0x80) {
        /* Ucode writes, ignore */
    } else if (addr >= 0xc00 && addr < 0xd10) {
        if (addr == 0xc00 && size == 4) {
            s->asic.clk_prescaler = val;
        } else if (addr == 0xc04 && size == 4) {
            /* CH_IRQ_STAT read-only, ignore write */
        } else if (addr == 0xc08 && size == 4) {
            s->asic.ch_irq_mask = val;
            pcibase_update_irq(s);
        } else if (addr == 0xd00 && size == 4) {
            s->asic.asic_irq = val;
            pcibase_update_irq(s);
        } else if (addr == 0xd04 && size == 2) {
            s->asic.asic_cfg = val;
        } else if (addr == 0xd0c && size == 2) {
            s->asic.global_cmd = val;
        } else {
            qemu_log_mask(LOG_UNIMP, "rp2: unimplemented write addr 0x%"PRIx64" size %u val 0x%"PRIx64"\n", addr, size, val);
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "rp2: unimplemented write addr 0x%"PRIx64" size %u val 0x%"PRIx64"\n", addr, size, val);
    }
}

/* PIO handlers (not used) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    memset(&s->asic, 0, sizeof(s->asic));
    s->fpga_ctl0 = 0;
    s->fpga_ctl1 = 0;
    s->bar0_irq_mask = 0;
    s->bar0_irq_status = 0;

    for (int i = 0; i < N_PORTS; i++) {
        memset(&s->channels[i], 0, sizeof(s->channels[i]));
        /* Initial state: modem lines high, change bits set to trigger MS interrupt on enable */
        s->channels[i].chan_stat = RP2_CHAN_STAT_DCD_m | RP2_CHAN_STAT_DSR_m |
                                   RP2_CHAN_STAT_CTS_m | RP2_CHAN_STAT_RI_m |
                                   RP2_CHAN_STAT_MS_CHANGED_MASK;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_RP );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RP_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "rp2-bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "rp2-bar1" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization before the device is 'live' */
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
    /* Uninit logic to be implemented */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "rp2_pci",
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
