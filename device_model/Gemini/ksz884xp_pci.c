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


#define TYPE_PCIBASE_DEVICE "ksz884xp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define KS_DMA_TX_CTRL			0x0000
#define KS_DMA_RX_CTRL			0x0004
#define KS_DMA_TX_START			0x0008
#define KS_DMA_RX_START			0x000C
#define KS_DMA_TX_ADDR			0x0010
#define KS_DMA_RX_ADDR			0x0014
#define KS884X_INTERRUPTS_ENABLE	0x0028
#define KS884X_INTERRUPTS_STATUS	0x002C
#define KS884X_INT_RX_STOPPED		0x02000000
#define KS884X_INT_TX_STOPPED		0x04000000
#define KS884X_INT_RX_OVERRUN		0x08000000
#define KS884X_INT_TX_EMPTY		0x10000000
#define KS884X_INT_RX			0x20000000
#define KS884X_INT_TX			0x40000000
#define KS884X_INT_PHY			0x80000000
#define KS884X_INT_RX_MASK		(KS884X_INT_RX | KS884X_INT_RX_OVERRUN)
#define KS884X_INT_TX_MASK		(KS884X_INT_TX | KS884X_INT_TX_EMPTY)
#define KS884X_INT_MASK	(KS884X_INT_RX | KS884X_INT_TX | KS884X_INT_PHY)

#define KS884X_BUS_CTRL_OFFSET		0x0210
#define KS884X_SIDER_P			0x0400
#define KS884X_CHIP_ID_OFFSET		KS884X_SIDER_P
#define KS884X_REVISION_MASK		0x000E
#define KS884X_REVISION_SHIFT		1
#define KS884X_CHIP_ID_MASK_41		0xFF10
#define REG_CHIP_ID_41			0x8810
#define REG_CHIP_ID_42			0x8800
#define KS8842_SGCR5_P			0x040A
#define KS8842_SWITCH_CTRL_5_OFFSET	KS8842_SGCR5_P
#define KS884X_GLOBAL_CTRL_OFFSET	0x0216
#define BUS_SPEED_125_MHZ		0x0000
#define GLOBAL_SOFTWARE_RESET		0x0001
#define DMA_TX_PAD_ENABLE		0x00000004
#define DMA_TX_CRC_ENABLE		0x00000002
#define DMA_BURST_DEFAULT		8
#define DMA_BURST_SHIFT			24
#define DMA_TX_ENABLE			0x00000001
#define DMA_RX_BROADCAST		0x00000040
#define DMA_RX_UNICAST			0x00000010
#define DMA_RX_ENABLE			0x00000001
#define KS884X_DMA_RX_MULTICAST		0x00000002
#define DMA_RX_CSUM_TCP			0x00020000
#define DMA_RX_CSUM_IP			0x00010000
#define DMA_RX_ALL_MULTICAST		0x00000020
#define DMA_RX_PROMISCUOUS		0x00000004
#define DMA_RX_CSUM_UDP			0x00040000

#define KS884X_IACR_P			0x04A0
#define KS884X_IADR4_P			0x04A8
#define KS884X_IADR2_P			0x04A4

#define TABLE_STATIC_MAC_V		0
#define TABLE_SELECT_S			2
#define TABLE_VLAN_V			1
#define TABLE_MIB_V			3
#define KS_DESC_BUF_SIZE		0x000007FF

#define KS_DESC_TX_INTERRUPT		0x80000000
#define KS_DESC_TX_FIRST		0x40000000
#define KS_DESC_TX_LAST			0x20000000
#define KS_DESC_TX_CSUM_GEN_IP		0x10000000
#define KS_DESC_TX_CSUM_GEN_TCP		0x08000000
#define KS_DESC_TX_CSUM_GEN_UDP		0x04000000

#define KS884X_IACR_OFFSET		KS884X_IACR_P
#define KS884X_ACC_DATA_0_OFFSET	KS884X_IADR4_P
#define KS884X_ACC_DATA_4_OFFSET	KS884X_IADR2_P
#define TABLE_SEL_SHIFT			2
#define TABLE_READ			0x10
#define TABLE_STATIC_MAC		(TABLE_STATIC_MAC_V << TABLE_SELECT_S)
#define TABLE_VLAN			(TABLE_VLAN_V << TABLE_SELECT_S)
#define TABLE_MIB			(TABLE_MIB_V << TABLE_SELECT_S)
#define KS_DESC_RX_MASK			(KS_DESC_BUF_SIZE)
#define KS_DESC_TX_MASK			\
	(KS_DESC_TX_INTERRUPT |		\
	KS_DESC_TX_FIRST |		\
	KS_DESC_TX_LAST |		\
	KS_DESC_TX_CSUM_GEN_IP |	\
	KS_DESC_TX_CSUM_GEN_TCP |	\
	KS_DESC_TX_CSUM_GEN_UDP |	\
	KS_DESC_BUF_SIZE)

struct ksz_desc_rx_stat {
#ifdef HOST_BIG_ENDIAN
	uint32_t hw_owned:1;
	uint32_t first_desc:1;
	uint32_t last_desc:1;
	uint32_t csum_err_ip:1;
	uint32_t csum_err_tcp:1;
	uint32_t csum_err_udp:1;
	uint32_t error:1;
	uint32_t multicast:1;
	uint32_t src_port:4;
	uint32_t err_phy:1;
	uint32_t err_too_long:1;
	uint32_t err_runt:1;
	uint32_t err_crc:1;
	uint32_t frame_type:1;
	uint32_t reserved1:4;
	uint32_t frame_len:11;
#else
	uint32_t frame_len:11;
	uint32_t reserved1:4;
	uint32_t frame_type:1;
	uint32_t err_crc:1;
	uint32_t err_runt:1;
	uint32_t err_too_long:1;
	uint32_t err_phy:1;
	uint32_t src_port:4;
	uint32_t multicast:1;
	uint32_t error:1;
	uint32_t csum_err_udp:1;
	uint32_t csum_err_tcp:1;
	uint32_t csum_err_ip:1;
	uint32_t last_desc:1;
	uint32_t first_desc:1;
	uint32_t hw_owned:1;
#endif
};

struct ksz_desc_tx_stat {
#ifdef HOST_BIG_ENDIAN
	uint32_t hw_owned:1;
	uint32_t reserved1:31;
#else
	uint32_t reserved1:31;
	uint32_t hw_owned:1;
#endif
};

struct ksz_desc_rx_buf {
#ifdef HOST_BIG_ENDIAN
	uint32_t reserved4:6;
	uint32_t end_of_ring:1;
	uint32_t reserved3:14;
	uint32_t buf_size:11;
#else
	uint32_t buf_size:11;
	uint32_t reserved3:14;
	uint32_t end_of_ring:1;
	uint32_t reserved4:6;
#endif
};

struct ksz_desc_tx_buf {
#ifdef HOST_BIG_ENDIAN
	uint32_t intr:1;
	uint32_t first_seg:1;
	uint32_t last_seg:1;
	uint32_t csum_gen_ip:1;
	uint32_t csum_gen_tcp:1;
	uint32_t csum_gen_udp:1;
	uint32_t end_of_ring:1;
	uint32_t reserved4:1;
	uint32_t dest_port:4;
	uint32_t reserved3:9;
	uint32_t buf_size:11;
#else
	uint32_t buf_size:11;
	uint32_t reserved3:9;
	uint32_t dest_port:4;
	uint32_t reserved4:1;
	uint32_t end_of_ring:1;
	uint32_t csum_gen_udp:1;
	uint32_t csum_gen_tcp:1;
	uint32_t csum_gen_ip:1;
	uint32_t last_seg:1;
	uint32_t first_seg:1;
	uint32_t intr:1;
#endif
};

union desc_stat {
	struct ksz_desc_rx_stat rx;
	struct ksz_desc_tx_stat tx;
	uint32_t data;
};

union desc_buf {
	struct ksz_desc_rx_buf rx;
	struct ksz_desc_tx_buf tx;
	uint32_t data;
};

struct ksz_hw_desc {
	union desc_stat ctrl;
	union desc_buf buf;
	uint32_t addr;
	uint32_t next;
};

struct ksz_sw_desc {
	union desc_stat ctrl;
	union desc_buf buf;
	uint32_t buf_size;
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
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x600 / 4];

    /* DMA Context */
    uint32_t dma_tx_addr;
    uint32_t dma_rx_addr;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intr_status & s->intr_mask) != 0;
    if (msix_enabled(pdev)) {
        if (level) msix_notify(pdev, 0);
    } else if (msi_enabled(pdev)) {
        if (level) msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, level);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    if (is_write) {
        s->intr_status |= KS884X_INT_TX;
    } else {
        s->intr_status |= KS884X_INT_RX;
    }
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t reg_idx = addr / 4;
    uint32_t shift = (addr % 4) * 8;

    if (reg_idx < ARRAY_SIZE(s->regs)) {
        val = (s->regs[reg_idx] >> shift) & ((1ULL << (size * 8)) - 1);
    }

    switch (addr) {
    case KS884X_CHIP_ID_OFFSET:
        val = 0x8810; /* REG_CHIP_ID_41 */
        break;
    case KS884X_INTERRUPTS_STATUS:
        val = s->intr_status & s->intr_mask;
        break;
    case KS884X_INTERRUPTS_ENABLE:
        val = s->intr_mask;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_idx = addr / 4;
    uint32_t shift = (addr % 4) * 8;
    uint32_t mask = ((1ULL << (size * 8)) - 1) << shift;

    if (reg_idx < ARRAY_SIZE(s->regs)) {
        s->regs[reg_idx] = (s->regs[reg_idx] & ~mask) | ((val << shift) & mask);
    }

    switch (addr) {
    case KS884X_INTERRUPTS_ENABLE:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case KS884X_INTERRUPTS_STATUS:
        s->intr_status &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case KS_DMA_TX_ADDR:
        s->dma_tx_addr = val;
        break;
    case KS_DMA_RX_ADDR:
        s->dma_rx_addr = val;
        break;
    case KS_DMA_TX_START:
        pcibase_do_dma(s, true);
        break;
    case KS_DMA_RX_START:
        pcibase_do_dma(s, false);
        break;
    case KS884X_GLOBAL_CTRL_OFFSET:
        if (val & GLOBAL_SOFTWARE_RESET) {
            s->intr_status = 0;
            s->intr_mask = 0;
            pcibase_update_irq(s);
        }
        break;
    }
}

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
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->dma_tx_addr = 0;
    s->dma_rx_addr = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x16c6 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8841 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200 );
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
    s->bar_info[0].size = 0x4000; /* Placeholder size */
    s->bar_info[0].name = "ksz884x-mmio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Set DMA masks or ring buffer limits */

    /* Final state initialization before the device is 'live' */
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
    .name = "ksz884xp_pci",
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
