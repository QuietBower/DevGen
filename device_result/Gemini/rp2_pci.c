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
#include "qemu/bswap.h"
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
#define RP2_FPGA_CTL0			0x110
#define RP2_FPGA_CTL1			0x11c
#define RP2_IRQ_MASK			0x1ec
#define RP2_IRQ_MASK_EN_m		BIT(0)
#define RP2_IRQ_STATUS			0x1f0
#define RP2_ASIC_SPACING		0x1000
#define RP2_ASIC_OFFSET(i)		((i) << ilog2(RP2_ASIC_SPACING))
#define RP2_PORT_BASE			0x000
#define RP2_PORT_SPACING		0x040
#define RP2_UCODE_BASE			0x400
#define RP2_UCODE_SPACING		0x80
#define RP2_CLK_PRESCALER		0xc00
#define RP2_CH_IRQ_STAT			0xc04
#define RP2_CH_IRQ_MASK			0xc08
#define RP2_ASIC_IRQ			0xd00
#define RP2_ASIC_IRQ_EN_m		BIT(20)
#define RP2_GLOBAL_CMD			0xd0c
#define RP2_ASIC_CFG			0xd04
#define RP2_DATA_DWORD			0x000
#define RP2_DATA_BYTE			0x008
#define RP2_DATA_BYTE_ERR_PARITY_m	BIT(8)
#define RP2_DATA_BYTE_ERR_OVERRUN_m	BIT(9)
#define RP2_DATA_BYTE_ERR_FRAMING_m	BIT(10)
#define RP2_DATA_BYTE_BREAK_m		BIT(11)
#define RP2_DUMMY_READ			BIT(16)
#define RP2_DATA_BYTE_EXCEPTION_MASK	(RP2_DATA_BYTE_ERR_PARITY_m | \
					 RP2_DATA_BYTE_ERR_OVERRUN_m | \
					 RP2_DATA_BYTE_ERR_FRAMING_m | \
					 RP2_DATA_BYTE_BREAK_m)
#define RP2_RX_FIFO_COUNT		0x00c
#define RP2_TX_FIFO_COUNT		0x00e
#define RP2_CHAN_STAT			0x010
#define RP2_CHAN_STAT_RXDATA_m		BIT(0)
#define RP2_CHAN_STAT_DCD_m		BIT(3)
#define RP2_CHAN_STAT_DSR_m		BIT(4)
#define RP2_CHAN_STAT_CTS_m		BIT(5)
#define RP2_CHAN_STAT_RI_m		BIT(6)
#define RP2_CHAN_STAT_OVERRUN_m		BIT(13)
#define RP2_CHAN_STAT_DSR_CHANGED_m	BIT(16)
#define RP2_CHAN_STAT_CTS_CHANGED_m	BIT(17)
#define RP2_CHAN_STAT_CD_CHANGED_m	BIT(18)
#define RP2_CHAN_STAT_RI_CHANGED_m	BIT(22)
#define RP2_CHAN_STAT_TXEMPTY_m		BIT(25)
#define RP2_CHAN_STAT_MS_CHANGED_MASK	(RP2_CHAN_STAT_DSR_CHANGED_m | \
					 RP2_CHAN_STAT_CTS_CHANGED_m | \
					 RP2_CHAN_STAT_CD_CHANGED_m | \
					 RP2_CHAN_STAT_RI_CHANGED_m)
#define RP2_TXRX_CTL			0x014
#define RP2_TXRX_CTL_MSRIRQ_m		BIT(0)
#define RP2_TXRX_CTL_RXIRQ_m		BIT(2)
#define RP2_TXRX_CTL_RX_TRIG_s		3
#define RP2_TXRX_CTL_RX_TRIG_m		(0x3 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_TRIG_1		(0x1 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_TRIG_256	(0x2 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_TRIG_448	(0x3 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_RX_EN_m		BIT(5)
#define RP2_TXRX_CTL_RTSFLOW_m		BIT(6)
#define RP2_TXRX_CTL_DTRFLOW_m		BIT(7)
#define RP2_TXRX_CTL_TX_TRIG_s		16
#define RP2_TXRX_CTL_TX_TRIG_m		(0x3 << RP2_TXRX_CTL_RX_TRIG_s)
#define RP2_TXRX_CTL_DSRFLOW_m		BIT(18)
#define RP2_TXRX_CTL_TXIRQ_m		BIT(19)
#define RP2_TXRX_CTL_CTSFLOW_m		BIT(23)
#define RP2_TXRX_CTL_TX_EN_m		BIT(24)
#define RP2_TXRX_CTL_RTS_m		BIT(25)
#define RP2_TXRX_CTL_DTR_m		BIT(26)
#define RP2_TXRX_CTL_LOOP_m		BIT(27)
#define RP2_TXRX_CTL_BREAK_m		BIT(28)
#define RP2_TXRX_CTL_CMSPAR_m		BIT(29)
#define RP2_TXRX_CTL_nPARODD_m		BIT(30)
#define RP2_TXRX_CTL_PARENB_m		BIT(31)
#define RP2_UART_CTL			0x018
#define RP2_UART_CTL_MODE_s		0
#define RP2_UART_CTL_MODE_m		(0x7 << RP2_UART_CTL_MODE_s)
#define RP2_UART_CTL_MODE_rs232		(0x1 << RP2_UART_CTL_MODE_s)
#define RP2_UART_CTL_FLUSH_RX_m		BIT(3)
#define RP2_UART_CTL_FLUSH_TX_m		BIT(4)
#define RP2_UART_CTL_RESET_CH_m		BIT(5)
#define RP2_UART_CTL_XMIT_EN_m		BIT(6)
#define RP2_UART_CTL_DATABITS_s		8
#define RP2_UART_CTL_DATABITS_m		(0x3 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_8		(0x3 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_7		(0x2 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_6		(0x1 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_DATABITS_5		(0x0 << RP2_UART_CTL_DATABITS_s)
#define RP2_UART_CTL_STOPBITS_m		BIT(10)
#define RP2_BAUD			0x01c
#define RP2_TX_SWFLOW			0x02
#define RP2_TX_SWFLOW_ena		0x81
#define RP2_TX_SWFLOW_dis		0x9d
#define RP2_RX_SWFLOW			0x0c
#define RP2_RX_SWFLOW_ena		0x81
#define RP2_RX_SWFLOW_dis		0x8d
#define RP2_RX_FIFO			0x37
#define RP2_RX_FIFO_ena			0x08
#define RP2_RX_FIFO_dis			0x81

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

    /* BAR0 state */
    uint32_t fpga_ctl0;
    uint32_t fpga_ctl1;
    uint32_t irq_mask;
    uint32_t irq_status;

    /* BAR1 state */
    uint8_t asic_data[0x10000];
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    bool irq = false;
    for (int i = 0; i < 2; i++) {
        uint32_t asic_offset = i * RP2_ASIC_SPACING;
        uint32_t ch_irq_stat = ldl_le_p(&s->asic_data[asic_offset + RP2_CH_IRQ_STAT]);
        uint32_t ch_irq_mask = ldl_le_p(&s->asic_data[asic_offset + RP2_CH_IRQ_MASK]);
        uint32_t asic_irq_en = ldl_le_p(&s->asic_data[asic_offset + RP2_ASIC_IRQ]);
        
        if ((ch_irq_stat & ~ch_irq_mask) && (asic_irq_en & RP2_ASIC_IRQ_EN_m)) {
            irq = true;
            break;
        }
    }
    
    if (irq && (s->irq_mask & RP2_IRQ_MASK_EN_m)) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

/* MMIO Handlers for BAR0 */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
        case RP2_FPGA_CTL0: return s->fpga_ctl0;
        case RP2_FPGA_CTL1: return s->fpga_ctl1;
        case RP2_IRQ_MASK: return s->irq_mask;
        case RP2_IRQ_STATUS: return s->irq_status;
    }
    return 0;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
        case RP2_FPGA_CTL0: s->fpga_ctl0 = val; break;
        case RP2_FPGA_CTL1: s->fpga_ctl1 = val; break;
        case RP2_IRQ_MASK: 
            s->irq_mask = val; 
            pcibase_update_irq(s);
            break;
    }
}

/* MMIO Handlers for BAR1 */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr < sizeof(s->asic_data)) {
        if (size == 1) return ldub_p(&s->asic_data[addr]);
        if (size == 2) return lduw_le_p(&s->asic_data[addr]);
        if (size == 4) return ldl_le_p(&s->asic_data[addr]);
    }
    return 0;
}

static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= sizeof(s->asic_data)) return;

    uint32_t asic_offset = addr & ~(RP2_ASIC_SPACING - 1);
    uint32_t local_addr = addr & (RP2_ASIC_SPACING - 1);

    if (local_addr == RP2_GLOBAL_CMD && size == 2) {
        if (val == 1) {
            memset(&s->asic_data[asic_offset], 0, RP2_ASIC_SPACING);
        }
        return;
    }

    if (local_addr < 0xc00 && (local_addr & 0x3f) == RP2_UART_CTL && size == 4) {
        stl_le_p(&s->asic_data[addr], val);
        if (val & RP2_UART_CTL_RESET_CH_m) {
            uint32_t port_base = asic_offset + (local_addr & ~0x3f);
            memset(&s->asic_data[port_base], 0, RP2_PORT_SPACING);
        }
        return;
    }

    if (local_addr < 0xc00 && (local_addr & 0x3f) == RP2_CHAN_STAT && size == 4) {
        uint32_t chan_stat = ldl_le_p(&s->asic_data[addr]);
        chan_stat &= ~val;
        stl_le_p(&s->asic_data[addr], chan_stat);
        
        uint32_t port_idx = (local_addr & ~0x3f) / RP2_PORT_SPACING;
        if ((chan_stat & (RP2_CHAN_STAT_RXDATA_m | RP2_CHAN_STAT_TXEMPTY_m | RP2_CHAN_STAT_MS_CHANGED_MASK)) == 0) {
            uint32_t ch_irq_stat = ldl_le_p(&s->asic_data[asic_offset + RP2_CH_IRQ_STAT]);
            ch_irq_stat &= ~(1 << port_idx);
            stl_le_p(&s->asic_data[asic_offset + RP2_CH_IRQ_STAT], ch_irq_stat);
        }
        pcibase_update_irq(s);
        return;
    }

    if (local_addr < 0xc00 && (local_addr & 0x3f) == RP2_TXRX_CTL && size == 4) {
        stl_le_p(&s->asic_data[addr], val);
        uint32_t port_idx = (local_addr & ~0x3f) / RP2_PORT_SPACING;
        uint32_t chan_stat_addr = asic_offset + (local_addr & ~0x3f) + RP2_CHAN_STAT;
        
        uint32_t chan_stat = ldl_le_p(&s->asic_data[chan_stat_addr]);
        uint32_t ch_irq_stat = ldl_le_p(&s->asic_data[asic_offset + RP2_CH_IRQ_STAT]);

        if (val & RP2_TXRX_CTL_TXIRQ_m) {
            chan_stat |= RP2_CHAN_STAT_TXEMPTY_m;
            ch_irq_stat |= (1 << port_idx);
            stl_le_p(&s->asic_data[chan_stat_addr], chan_stat);
            stl_le_p(&s->asic_data[asic_offset + RP2_CH_IRQ_STAT], ch_irq_stat);
        }
        pcibase_update_irq(s);
        return;
    }

    if (size == 1) stb_p(&s->asic_data[addr], val);
    else if (size == 2) stw_le_p(&s->asic_data[addr], val);
    else if (size == 4) stl_le_p(&s->asic_data[addr], val);
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->fpga_ctl0 = 0;
    s->fpga_ctl1 = 0;
    s->irq_mask = 0;
    s->irq_status = 0;
    memset(s->asic_data, 0, sizeof(s->asic_data));
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
        const MemoryRegionOps *ops = (bi->index == 0) ? &pcibase_bar0_ops : &pcibase_bar1_ops;
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x11fe );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0040 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0700 );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x10000, .name = "bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x10000, .name = "bar1" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "rp2_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(fpga_ctl0, PCIBaseState),
        VMSTATE_UINT32(fpga_ctl1, PCIBaseState),
        VMSTATE_UINT32(irq_mask, PCIBaseState),
        VMSTATE_UINT32(irq_status, PCIBaseState),
        VMSTATE_UINT8_ARRAY(asic_data, PCIBaseState, 0x10000),
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
