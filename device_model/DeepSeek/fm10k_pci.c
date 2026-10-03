/*
 * QEMU emulation of Intel FM10K Ethernet Controller (PF)
 * Based on Linux driver fm10k_pci.c
 * QEMU 8.2.10 compatible
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

#define TYPE_PCIBASE_DEVICE "fm10k_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs from first entry of pci_device_id table */
#define VENDOR_ID 0x8086 /* Intel */
#define DEVICE_ID 0x15A4 /* FM10K_DEV_ID_PF */
#define CLASS_ID  0x0200 /* PCI_CLASS_NETWORK_ETHERNET */

/* Register offsets and hardware definitions from driver */
#define FM10K_ERR_MSG(type) case (type): error = #type; break
#define TX_DMA_DRAIN_RETRIES 25
#define FM10K_REMOVED(hw_addr) unlikely(!(hw_addr))
#define fm10k_write_flush(hw) fm10k_read_reg((hw), FM10K_CTRL)
#define FM10K_SWPRI_MAP(_n)	((_n) + 0x0050)
#define fm10k_write_reg(hw, reg, val) \
do { \
	u32 __iomem *hw_addr = READ_ONCE((hw)->hw_addr); \
	if (!FM10K_REMOVED(hw_addr)) \
		writel((val), &hw_addr[(reg)]); \
} while (0)
#define FM10K_SWPRI_MAX		16
#define set_check_for_tx_hang(ring) \
	set_bit(__FM10K_TX_DETECT_HANG, (ring)->state)
#define FM10K_ITR_PENDING2			0x10000000
#define FM10K_ITR_ENABLE	(FM10K_ITR_AUTOMASK | FM10K_ITR_MASK_CLEAR)
#define FM10K_VFMBX_MSG_MTU	((FM10K_VFMBMEM_LEN / 2) - 1)
#define FM10K_TXDCTL_MAX_TIME_SHIFT		16
#define FM10K_TDLEN(_n)		((0x40 * (_n)) + 0x8002)
#define FM10K_TXDCTL(_n)	((0x40 * (_n)) + 0x8006)
#define FM10K_TDH(_n)		((0x40 * (_n)) + 0x8004)
#define FM10K_TDBAH(_n)		((0x40 * (_n)) + 0x8001)
#define FM10K_TDT(_n)		((0x40 * (_n)) + 0x8005)
#define FM10K_PFVTCTL_FTAG_DESC_ENABLE		0x00000001
#define FM10K_INT_MAP_TIMER0			0x00000000
#define FM10K_TXINT(_n)		((0x40 * (_n)) + 0x8008)
#define FM10K_TDBAL(_n)		((0x40 * (_n)) + 0x8000)
#define FM10K_TXDCTL_ENABLE			0x00004000
#define FM10K_PFVTCTL(_n)	((0x40 * (_n)) + 0x800E)
#define FM10K_INT_MAP_DISABLE			0x00000300
#define FM10K_SRRCTL(_n)	((0x40 * (_n)) + 0x4009)
#define FM10K_RXQCTL(_n)	((0x40 * (_n)) + 0x4006)
#define FM10K_SRRCTL_BSIZEPKT_SHIFT		8
#define FM10K_RXDCTL(_n)	((0x40 * (_n)) + 0x4007)
#define FM10K_SRRCTL_BUFFER_CHAINING_EN		0x80000000
#define FM10K_SRRCTL_LOOPBACK_SUPPRESS		0x40000000
#define FM10K_RDH(_n)		((0x40 * (_n)) + 0x4004)
#define FM10K_RXDCTL_DROP_ON_EMPTY		0x00000200
#define FM10K_RXINT(_n)		((0x40 * (_n)) + 0x4008)
#define FM10K_RDT(_n)		((0x40 * (_n)) + 0x4005)
#define FM10K_RDLEN(_n)		((0x40 * (_n)) + 0x4002)
#define FM10K_INT_MAP_TIMER1			0x00000100
#define FM10K_RXQCTL_ENABLE			0x00000001
#define FM10K_RDBAH(_n)		((0x40 * (_n)) + 0x4001)
#define FM10K_RX_BUFSZ		FM10K_RXBUFFER_2048
#define FM10K_VLAN_CLEAR			BIT(15)
#define FM10K_RXDCTL_WRITE_BACK_MIN_DELAY	0x00000001
#define FM10K_RDBAL(_n)		((0x40 * (_n)) + 0x4000)
#define FM10K_MRQC_TCP_IPV4			0x00000001
#define FM10K_MRQC_UDP_IPV4			0x00000040
#define FM10K_RSSRK_SIZE	10
#define FM10K_MRQC_IPV4				0x00000002
#define FM10K_MRQC(_n)		((_n) + 0x2100)
#define FM10K_RETA_SIZE		32
#define FM10K_MRQC_IPV6				0x00000010
#define FM10K_RETA(_n, _m)	(((_n) * 0x20) + (_m) + 0x1000)
#define FM10K_RSSRK(_n, _m)	(((_n) * 0x10) + (_m) + 0x0800)
#define FM10K_MRQC_UDP_IPV6			0x00000080
#define FM10K_MRQC_TCP_IPV6			0x00000020
#define FM10K_MBX_INT_DELAY			20
#define FM10K_VFITR(_n)		((_n) + 0x00060)
#define FM10K_PCA_FAULT		0x0008
#define FM10K_THI_FAULT		0x0010
#define FM10K_FUM_FAULT		0x001C
#define FM10K_VF_FLAG_MULTI_CAPABLE	(u8)(BIT(FM10K_XCAST_MODE_MULTI))
#define FM10K_EICR_FAULT_MASK			0x0000003F
#define FM10K_FAULT_SIZE		0x4
#define FM10K_MAX_QUEUES_PF		128
#define FM10K_EICR_MAXHOLDTIME			0x00001000
#define FM10K_MAXHOLDQ(_n)	((_n) + 0x0020)
#define FM10K_EICR_SWITCHNOTREADY		0x00000100
#define FM10K_EICR		0x0006
#define FM10K_EICR_SWITCHREADY			0x00000080
#define FM10K_ERR_RESET_REQUESTED		-5
#define FM10K_DGLORTMAP_NONE			0x0000FFFF
#define FM10K_EICR_MAILBOX			0x00000040
#define FM10K_ITR(_n)		((_n) + 0x12400)
#define FM10K_ITR_MASK_SET			0x40000000
#define FM10K_EIMR		0x0007
#define FM10K_EIMR_DISABLE(NAME)		((FM10K_EIMR_ ## NAME) << 0)
#define FM10K_TLV_ID_MASK		((1u << FM10K_TLV_ID_SIZE) - 1)
#define FM10K_VFINT_MAP		0x00030
#define FM10K_MSG_ERR_PEP_NOT_SCHEDULED	280
#define fm10k_tlv_attr_get_u32(attr, ptr) \
		fm10k_tlv_attr_get_value(attr, ptr, sizeof(u32))
#define FM10K_VLAN_TABLE_VID_MAX		4096
#define FM10K_MSG_HDR_FIELD_GET(value, name) \
	((u16)((value) >> FM10K_MSG_##name##_SHIFT) & FM10K_MSG_HDR_MASK(name))
#define FM10K_ERR_PARAM				-2
#define FM10K_INT_MAP_IMMEDIATE			0x00000200
#define FM10K_EIMR_ENABLE(NAME)			((FM10K_EIMR_ ## NAME) << 1)
#define FM10K_INT_MAP(_n)	((_n) + 0x10080)
#define FM10K_ERR_REQUESTS_PENDING		-4
#define FM10K_ITR_ADAPTIVE	0x8000
#define FM10K_DEFAULT_RXD	 256
#define FM10K_RX_ITR_DEFAULT	FM10K_ITR_20K
#define FM10K_DEFAULT_TXD	 256
#define FM10K_MAX_RSS_INDICES	128
#define FM10K_TX_ITR_DEFAULT	FM10K_ITR_40K
#define FM10K_UC_ADDR_SIZE	(FM10K_UC_ADDR_END - FM10K_UC_ADDR_START)
#define FM10K_DEV_ID_PF			0x15A4
#define FM10K_DEV_ID_SDI_FM10420_DA2	0x15D5
#define FM10K_DEV_ID_SDI_FM10420_QDA2	0x15D0
#define FM10K_DEV_ID_VF			0x15A5
#define FM10K_VF_MSG_MAC_VLAN_HANDLER(func) \
	FM10K_MSG_HANDLER(FM10K_VF_MSG_ID_MAC_VLAN, \
			  fm10k_mac_vlan_msg_attr, func)
#define FM10K_TLV_MSG_ERROR_HANDLER(func) \
	FM10K_MSG_HANDLER(FM10K_TLV_ERROR, NULL, func)
#define FM10K_VF_MSG_LPORT_STATE_HANDLER(func) \
	FM10K_MSG_HANDLER(FM10K_VF_MSG_ID_LPORT_STATE, \
			  fm10k_lport_state_msg_attr, func)
#define FM10K_TLV_MSG_TEST_HANDLER(func) \
	FM10K_MSG_HANDLER(FM10K_TLV_MSG_ID_TEST, fm10k_tlv_msg_test_attr, func)
#define FM10K_PF_MSG_ERR_HANDLER(msg, func) \
	FM10K_MSG_HANDLER(FM10K_PF_MSG_ID_##msg, fm10k_err_msg_attr, func)
#define FM10K_PF_MSG_LPORT_MAP_HANDLER(func) \
	FM10K_MSG_HANDLER(FM10K_PF_MSG_ID_LPORT_MAP, \
			  fm10k_lport_map_msg_attr, func)
#define FM10K_PF_MSG_UPDATE_PVID_HANDLER(func) \
	FM10K_MSG_HANDLER(FM10K_PF_MSG_ID_UPDATE_PVID, \
			  fm10k_update_pvid_msg_attr, func)
#define MAX_QUEUES	FM10K_MAX_QUEUES_PF
#define MAX_Q_VECTORS 256
#define FM10K_CTRL		0x0000
#define FM10K_DGLORTMAP(_n)	((_n) + 0x0030)
#define FM10K_DGLORTMAP_MASK_SHIFT		16
#define FM10K_ITR_AUTOMASK			0x20000000
#define FM10K_ITR_MASK_CLEAR			0x80000000
#define FM10K_VFMBMEM_LEN	16
#define FM10K_RX_DESC(R, i)	\
	 (&(((union fm10k_rx_desc *)((R)->desc))[i]))
#define FM10K_RXBUFFER_2048	 2048
#define FM10K_MBX_BUFFER_SIZE \
	(FM10K_MBX_TX_BUFFER_SIZE + FM10K_MBX_RX_BUFFER_SIZE)
#define FM10K_MAX_QUEUES_POOL		16
#define FM10K_PFVFLRE(_n)	((0x1 * (_n)) + 0x18844)
#define FM10K_EICR_VFLR				0x00000800
#define FM10K_VLAN_OVERRIDE			FM10K_VLAN_CLEAR
#define FM10K_TLV_ID_SIZE		16
#define FM10K_MSG_HDR_MASK(name) \
	((0x1u << FM10K_MSG_##name##_SIZE) - 1)
#define FM10K_VLAN_ALL \
	((FM10K_VLAN_TABLE_VID_MAX - 1) << FM10K_VLAN_LENGTH_SHIFT)
#define FM10K_ITR_20K		50
#define FM10K_ITR_40K		25
#define FM10K_UC_ADDR_START	0x000000
#define FM10K_UC_ADDR_END	0x100000
#define FM10K_MAX_JUMBO_FRAME_SIZE	15342
#define FM10K_MSG_HANDLER(id, attr, func) { id, attr, func }
#define FM10K_TLV_ERROR (~0u)
#define FM10K_TLV_MSG_ID_TEST	0
#define FM10K_MAX_QUEUES		256
#define MIN_MSIX_COUNT(hw)	(MIN_Q_VECTORS + NON_Q_VECTORS)
#define FM10K_MBX_TX_BUFFER_SIZE	512
#define FM10K_MBX_RX_BUFFER_SIZE	128
#define FM10K_TLV_LEN_SHIFT		20
#define FM10K_TUNNEL_CFG_NVGRE_SHIFT		16
#define FM10K_TUNNEL_CFG	0x0040
#define FM10K_TUNNEL_CFG_GENEVE	0x0041
#define FM10K_VLAN_LENGTH_SHIFT			16
#define fm10k_for_each_ring(pos, head) \
	for (pos = &(head).ring[(head).count]; (--pos) >= (head).ring;)
#define FM10K_DEFAULT_TX_WORK	 256
#define MIN_Q_VECTORS	1
#define NON_Q_VECTORS	1
#define FM10K_MBX_ERR_NO_MBX		FM10K_MBX_ERR(0x01)
#define FM10K_VFMBX		0x00010
#define FM10K_MBX_MSG_MAX_SIZE \
	((FM10K_MBX_TX_BUFFER_SIZE - 1) & (FM10K_MBX_RX_BUFFER_SIZE - 1))
#define FM10K_VFMBMEM(_n)	((_n) + 0x00020)
#define FM10K_VFMBMEM_VF_XOR	(FM10K_VFMBMEM_LEN / 2)
#define FM10K_MBX_CRC_SEED		0xFFFF
#define FM10K_MBMEM_VF(_n, _m)	(((_n) * 0x10) + (_m) + 0x18000)
#define FM10K_MBX(_n)		((_n) + 0x18800)
#define FM10K_MBX_INIT_DELAY	500
#define FM10K_VF_TC_MIN		1
#define FM10K_VF_TC_MAX		100000
#define check_for_tx_hang(ring) \
	test_bit(__FM10K_TX_DETECT_HANG, (ring)->state)
#define FM10K_MAX_STATIONS	63
#define FM10K_MBX_ERR(_n) ((_n) - 512)
#define FM10K_MBX_ACK_INTERRUPT			0x00000010
#define FM10K_MBX_ERR_BUSY		FM10K_MBX_ERR(0x0C)
#define FM10K_MBX_INTERRUPT_ENABLE		0x00000020
#define FM10K_MBX_ERR_NO_SPACE		FM10K_MBX_ERR(0x03)
#define FM10K_MBX_INIT_TIMEOUT	2000
#define FM10K_MBX_REQ_INTERRUPT			0x00000008
#define FM10K_TLV_RESULTS_MAX		32
#define FM10K_MBX_ERR_TYPE		FM10K_MBX_ERR(0x09)
#define FM10K_MBX_POLL_DELAY			19
#define FM10K_MBX_INTERRUPT_DISABLE		0x00000040
#define FM10K_MBX_REQ				0x00000002
#define FM10K_MBX_DISCONNECT_TIMEOUT		500
#define FM10K_TUNNEL_HEADER_LENGTH	184
#define clear_check_for_tx_hang(ring) \
	clear_bit(__FM10K_TX_DETECT_HANG, (ring)->state)
#define TXD_USE_COUNT(S)	DIV_ROUND_UP((S), FM10K_MAX_DATA_PER_TXD)
#define FM10K_MIN_TXD		 128
#define FM10K_MIN_RXD		 128
#define FM10K_REQ_RX_DESCRIPTOR_MULTIPLE	8
#define FM10K_REQ_TX_DESCRIPTOR_MULTIPLE	8
#define FM10K_MAX_RXD		4096
#define FM10K_MAX_TXD		4096
#define FM10K_RSSRK_ENTRIES_PER_REG		4
#define FM10K_ITR_MAX		0x0FFF
#define FM10K_RETA_ENTRIES_PER_REG		4
#define FM10K_DMA_CTRL		0x20C3
#define FM10K_GCR_EXT		0x0005
#define FM10K_VFSYSTIME		0x00040
#define FM10K_VFCTRL		0x00000
#define FM10K_GCR		0x0003
#define FM10K_TPH_CTRL		0x20C7
#define FM10K_DMA_CTRL2		0x20C4
#define FM10K_CTRL_EXT		0x0001
#define FM10K_DGLORTDEC(_n)	((_n) + 0x0038)
#define ITR_IS_ADAPTIVE(itr) (!!(itr & FM10K_ITR_ADAPTIVE))
#define FM10K_MSG_HDR_FIELD_SET(value, name) \
	(((u32)(value) & FM10K_MSG_HDR_MASK(name)) << FM10K_MSG_##name##_SHIFT)
#define FM10K_MBX_ACK				0x00000004
#define FM10K_MBX_ERR_RSVD0		FM10K_MBX_ERR(0x0E)
#define FM10K_MBX_ERR_SIZE		FM10K_MBX_ERR(0x0B)
#define FM10K_MBX_ERR_TAIL		FM10K_MBX_ERR(0x05)
#define FM10K_MBX_ERR_HEAD		FM10K_MBX_ERR(0x06)
#define FM10K_MBX_ERR_CRC		FM10K_MBX_ERR(0x0F)
#define FM10K_TLV_DWORD_LEN(tlv) \
	((u16)((FM10K_TLV_LEN_ALIGN(tlv)) >> (FM10K_TLV_LEN_SHIFT + 2)) + 1)
#define FM10K_TX_DESC(R, i)	\
	(&(((struct fm10k_tx_desc *)((R)->desc))[i]))
#define FM10K_MAX_DATA_PER_TXD	(1u << FM10K_MAX_TXD_PWR)
#define FM10K_TXD_FLAG_LAST	0x40
#define DESC_NEEDED	(MAX_SKB_FRAGS + 4)
#define FM10K_NOT_IMPLEMENTED			0x7FFFFFFF
#define FM10K_QPTC(_n)		((0x40 * (_n)) + 0x8009)
#define FM10K_TPH_TXCTRL(_n)	((0x40 * (_n)) + 0x8003)
#define FM10K_TXQCTL(_n)	((0x40 * (_n)) + 0x8007)
#define FM10K_TQDLOC(_n)	((0x40 * (_n)) + 0x800C)
#define FM10K_QBRC_H(_n)	((0x40 * (_n)) + 0x400D)
#define FM10K_TPH_RXCTRL(_n)	((0x40 * (_n)) + 0x4003)
#define FM10K_QBRC_L(_n)	((0x40 * (_n)) + 0x400C)
#define FM10K_TX_SGLORT(_n)	((0x40 * (_n)) + 0x800D)
#define FM10K_QBTC_H(_n)	((0x40 * (_n)) + 0x800B)
#define FM10K_QPRDC(_n)		((0x40 * (_n)) + 0x400B)
#define FM10K_QBTC_L(_n)	((0x40 * (_n)) + 0x800A)
#define FM10K_QPRC(_n)		((0x40 * (_n)) + 0x400A)
#define FM10K_TLV_LEN_ALIGN(tlv) \
	(((tlv) + FM10K_TLV_LEN_ALIGN_MASK) & ~FM10K_TLV_LEN_ALIGN_MASK)
#define FM10K_MAX_TXD_PWR	14
#define FM10K_TXD_FLAG_RS	0x20
#define FM10K_TXD_FLAG_INT	0x01
#define FM10K_TXD_WB_FIFO_SIZE	4
#define FM10K_TXD_FLAG_CSUM	0x04
#define FM10K_TLV_FLAGS_SHIFT		16
#define FM10K_TLV_FLAGS_MSG		0x1
#define FM10K_TLV_LEN_ALIGN_MASK \
	((FM10K_TLV_HDR_LEN - 1) << FM10K_TLV_LEN_SHIFT)
#define FM10K_TLV_HDR_LEN		4ul
#define fm10k_tlv_attr_put_u64(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 8)
#define fm10k_tlv_attr_put_s8(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 1)
#define fm10k_tlv_attr_put_u32(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 4)
#define fm10k_tlv_attr_put_s64(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 8)
#define fm10k_tlv_attr_put_u8(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 1)
#define fm10k_tlv_attr_put_s16(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 2)
#define fm10k_tlv_attr_put_u16(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 2)
#define fm10k_tlv_attr_put_s32(msg, attr_id, val) \
		fm10k_tlv_attr_put_value(msg, attr_id, val, 4)

/* BAR0 size and MSI-X vectors */
#define FM10K_BAR0_SIZE 0x100000 /* 1 MB, from FM10K_UC_ADDR_SIZE */
#define MSIX_VECTORS 64

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

/* QEMU device state */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[2];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Register shadow: dynamic array for all MMIO registers */
    uint32_t *regs;
    uint32_t reg_count; /* number of 32-bit words in regs[] */

    /* Interrupt controller state */
    uint32_t eicr;       /* EICR register value */
    uint32_t eimr_mask;  /* Interrupt mask (1 = masked/disabled) */

    /* Additional status */
    uint32_t status;      /* Operational status flags */
    uint32_t reset_state; /* State used to handle reset sequences */
    uint8_t pm_state;     /* Power management state (D0-D3) */
};

#define FM10K_REG_COUNT (FM10K_BAR0_SIZE / sizeof(uint32_t))

/* Interrupt causes that map to the mailbox vector */
#define MBX_CAUSES_MASK (FM10K_EICR_FAULT_MASK | FM10K_EICR_MAILBOX | \
                         FM10K_EICR_SWITCHREADY | FM10K_EICR_SWITCHNOTREADY | \
                         FM10K_EICR_MAXHOLDTIME | FM10K_EICR_VFLR)

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->eicr & ~s->eimr_mask;

    if (pending & MBX_CAUSES_MASK) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        /* No relevant pending interrupts; deassert IRQ for legacy */
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t word_idx = addr >> 2;

    if (word_idx >= s->reg_count) {
        return 0;
    }

    switch (word_idx) {
    case FM10K_EICR:
        val = s->eicr;
        break;
    case FM10K_EIMR:
        val = s->eimr_mask;
        break;
    default:
        val = s->regs[word_idx];
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t word_idx = addr >> 2;
    uint32_t val32 = (uint32_t)val;

    if (word_idx >= s->reg_count) {
        return;
    }

    switch (word_idx) {
    case FM10K_EICR:
        /* Write-1-to-clear */
        s->eicr &= ~val32;
        break;
    case FM10K_EIMR:
        /* Enable/disable bits: val contains masks to enable or disable */
        /* Process each interrupt cause bit (0-31) */
        for (int i = 0; i < 32; i++) {
            uint32_t bit_mask = (1u << i);
            uint32_t enable_mask = (1u << (i + 1));
            if (val32 & bit_mask) {
                /* Disable (mask) interrupt */
                s->eimr_mask |= bit_mask;
            }
            if (val32 & enable_mask) {
                /* Enable (unmask) interrupt */
                s->eimr_mask &= ~bit_mask;
            }
        }
        break;
    default:
        s->regs[word_idx] = val32;
        break;
    }

    pcibase_update_irq(s);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear all registers */
    memset(s->regs, 0, s->reg_count * sizeof(uint32_t));
    s->eicr = 0;
    s->eimr_mask = 0;
    s->status = 0;
    s->reset_state = 0;
    s->pm_state = 0;
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
        /* Not used */
    } else if (bi->type == BAR_TYPE_RAM) {
        /* Not used */
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: MMIO with 1 MB */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = FM10K_BAR0_SIZE,
        .name = "fm10k-mmio"
    };

    /* Allocate register shadow array */
    s->reg_count = FM10K_BAR0_SIZE / sizeof(uint32_t);
    s->regs = g_malloc0(s->reg_count * sizeof(uint32_t));

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization using BAR1 */
    if (msix_init_exclusive_bar(pdev, MSIX_VECTORS, 1, errp)) {
        error_setg(errp, "MSI-X initialization failed");
        g_free(s->regs);
        return;
    }

    /* Set initial state */
    s->eicr = 0;
    s->eimr_mask = 0;
    s->status = 0;
    s->reset_state = 0;
    s->pm_state = 0;
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
    g_free(s->regs);
    s->regs = NULL;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "fm10k_pci",
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
