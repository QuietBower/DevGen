/*
 * QEMU RTL8139 PCI Ethernet Controller Emulation
 *
 * Copyright (c) 2025 QEMU Team
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "8139too_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define VENDOR_ID 0x10ec
#define DEVICE_ID 0x8139
#define CLASS_ID   PCI_CLASS_NETWORK_ETHERNET

#define EE_SHIFT_CLK	0x04
#define EE_CS			0x08
#define EE_DATA_WRITE	0x02
#define EE_DATA_READ	0x01
#define EE_ENB			(0x80 | EE_CS)
#define EE_WRITE_CMD	(5)
#define EE_READ_CMD		(6)
#define EE_ERASE_CMD	(7)
#define MDIO_WRITE0 (MDIO_DIR)
#define MDIO_WRITE1 (MDIO_DIR | MDIO_DATA_OUT)
#define RX_BUF_LEN	(8192 << RX_BUF_IDX)
#define MDIO_DATA_OUT	0x04
#define RX_FIFO_THRESH	7
#define RX_DMA_BURST	7
#define TX_DMA_BURST	6
#define EE_WRITE_0		0x00
#define EE_WRITE_1		0x02
#define NUM_TX_DESC	4
#define TX_BUF_SIZE	MAX_ETH_FRAME_SIZE
#define MAX_ETH_FRAME_SIZE	1792
#define TX_BUF_TOT_LEN	(TX_BUF_SIZE * NUM_TX_DESC)
#define RX_BUF_IDX	2
#define RX_BUF_PAD	16
#define RX_BUF_WRAP_PAD 2048
#define RX_BUF_TOT_LEN	(RX_BUF_LEN + RX_BUF_PAD + RX_BUF_WRAP_PAD)
#define MAX_ETH_DATA_SIZE (MAX_ETH_FRAME_SIZE - VLAN_ETH_HLEN - ETH_FCS_LEN)
#define TX_FIFO_THRESH 256
#define TX_RETRY	8
#define RTL_NUM_STATS 4
#define RTL_REGS_VER 1
#define RTL_MIN_IO_SIZE 0x80
#define RTL8139B_IO_SIZE 256

#define HW_REVID(b30, b29, b28, b27, b26, b23, b22) \
	(b30<<30 | b29<<29 | b28<<28 | b27<<27 | b26<<26 | b23<<23 | b22<<22)
#define HW_REVID_MASK	HW_REVID(1, 1, 1, 1, 1, 1, 1)

#define MDIO_DIR		0x80
#define MDIO_DATA_IN	0x02
#define MDIO_CLK		0x01

enum rx_mode_bits {
	AcceptErr	= 0x20,
	AcceptRunt	= 0x10,
	AcceptBroadcast	= 0x08,
	AcceptMulticast	= 0x04,
	AcceptMyPhys	= 0x02,
	AcceptAllPhys	= 0x01,
};

enum IntrStatusBits {
	PCIErr		= 0x8000,
	PCSTimeout	= 0x4000,
	RxFIFOOver	= 0x40,
	RxUnderrun	= 0x20,
	RxOverflow	= 0x10,
	TxErr		= 0x08,
	TxOK		= 0x04,
	RxErr		= 0x02,
	RxOK		= 0x01,

	RxAckBits	= RxFIFOOver | RxOverflow | RxOK,
};

enum TxStatusBits {
	TxHostOwns	= 0x2000,
	TxUnderrun	= 0x4000,
	TxStatOK	= 0x8000,
	TxOutOfWindow	= 0x20000000,
	TxAborted	= 0x40000000,
	TxCarrierLost	= 0x80000000,
};

enum RxStatusBits {
	RxMulticast	= 0x8000,
	RxPhysical	= 0x4000,
	RxBroadcast	= 0x2000,
	RxBadSymbol	= 0x0020,
	RxRunt		= 0x0010,
	RxTooLong	= 0x0008,
	RxCRCErr	= 0x0004,
	RxBadAlign	= 0x0002,
	RxStatusOK	= 0x0001,
};

enum RxConfigBits {
	/* rx fifo threshold */
	RxCfgFIFOShift	= 13,
	RxCfgFIFONone	= (7 << RxCfgFIFOShift),

	/* Max DMA burst */
	RxCfgDMAShift	= 8,
	RxCfgDMAUnlimited = (7 << RxCfgDMAShift),

	/* rx ring buffer length */
	RxCfgRcv8K	= 0,
	RxCfgRcv16K	= (1 << 11),
	RxCfgRcv32K	= (1 << 12),
	RxCfgRcv64K	= (1 << 11) | (1 << 12),

	/* Disable packet wrap at end of Rx buffer. (not possible with 64k) */
	RxNoWrap	= (1 << 7),
};

enum Config1Bits {
	Cfg1_PM_Enable	= 0x01,
	Cfg1_VPD_Enable	= 0x02,
	Cfg1_PIO	= 0x04,
	Cfg1_MMIO	= 0x08,
	LWAKE		= 0x10,		/* not on 8139, 8139A */
	Cfg1_Driver_Load = 0x20,
	Cfg1_LED0	= 0x40,
	Cfg1_LED1	= 0x80,
	SLEEP		= (1 << 1),	/* only on 8139, 8139A */
	PWRDN		= (1 << 0),	/* only on 8139, 8139A */
};

enum RTL8139_registers {
	MAC0		= 0,	 /* Ethernet hardware address. */
	MAR0		= 8,	 /* Multicast filter. */
	TxStatus0	= 0x10,	 /* Transmit status (Four 32bit registers). */
	TxAddr0		= 0x20,	 /* Tx descriptors (also four 32bit). */
	RxBuf		= 0x30,
	ChipCmd		= 0x37,
	RxBufPtr	= 0x38,
	RxBufAddr	= 0x3A,
	IntrMask	= 0x3C,
	IntrStatus	= 0x3E,
	TxConfig	= 0x40,
	RxConfig	= 0x44,
	Timer		= 0x48,	 /* A general-purpose counter. */
	RxMissed	= 0x4C,	 /* 24 bits valid, write clears. */
	Cfg9346		= 0x50,
	Config0		= 0x51,
	Config1		= 0x52,
	TimerInt	= 0x54,
	MediaStatus	= 0x58,
	Config3		= 0x59,
	Config4		= 0x5A,	 /* absent on RTL-8139A */
	HltClk		= 0x5B,
	MultiIntr	= 0x5C,
	TxSummary	= 0x60,
	BasicModeCtrl	= 0x62,
	BasicModeStatus	= 0x64,
	NWayAdvert	= 0x66,
	NWayLPAR	= 0x68,
	NWayExpansion	= 0x6A,
	/* Undocumented registers, but required for proper operation. */
	FIFOTMS		= 0x70,	 /* FIFO Control and test. */
	CSCR		= 0x74,	 /* Chip Status and Configuration Register. */
	PARA78		= 0x78,
	FlashReg	= 0xD4,	/* Communication with Flash ROM, four bytes. */
	PARA7c		= 0x7c,	 /* Magic transceiver parameter register. */
	Config5		= 0xD8,	 /* absent on RTL-8139A */
};

enum ClearBitMasks {
	MultiIntrClear	= 0xF000,
	ChipCmdClear	= 0xE2,
	Config1Clear	= (1<<7)|(1<<6)|(1<<3)|(1<<2)|(1<<1),
};

enum ChipCmdBits {
	CmdReset	= 0x10,
	CmdRxEnb	= 0x08,
	CmdTxEnb	= 0x04,
	RxBufEmpty	= 0x01,
};

enum tx_config_bits {
        /* Interframe Gap Time. Only TxIFG96 doesn't violate IEEE 802.3 */
        TxIFGShift	= 24,
        TxIFG84		= (0 << TxIFGShift), /* 8.4us / 840ns (10 / 100Mbps) */
        TxIFG88		= (1 << TxIFGShift), /* 8.8us / 880ns (10 / 100Mbps) */
        TxIFG92		= (2 << TxIFGShift), /* 9.2us / 920ns (10 / 100Mbps) */
        TxIFG96		= (3 << TxIFGShift), /* 9.6us / 960ns (10 / 100Mbps) */

	TxLoopBack	= (1 << 18) | (1 << 17), /* enable loopback test mode */
	TxCRC		= (1 << 16),	/* DISABLE Tx pkt CRC append */
	TxClearAbt	= (1 << 0),	/* Clear abort (WO) */
	TxDMAShift	= 8, /* DMA burst value (0-7) is shifted X many bits */
	TxRetryShift	= 4, /* TXRR value (0-15) is shifted X many bits */

	TxVersionMask	= 0x7C800000, /* mask out version bits 30-26, 23 */
};

enum Config3Bits {
	Cfg3_FBtBEn      	= (1 << 0), /* 1	= Fast Back to Back */
	Cfg3_FuncRegEn	= (1 << 1), /* 1	= enable CardBus Function registers */
	Cfg3_CLKRUN_En	= (1 << 2), /* 1	= enable CLKRUN */
	Cfg3_CardB_En    	= (1 << 3), /* 1	= enable CardBus registers */
	Cfg3_LinkUp      	= (1 << 4), /* 1	= wake up on link up */
	Cfg3_Magic       	= (1 << 5), /* 1	= wake up on Magic Packet (tm) */
	Cfg3_PARM_En     	= (1 << 6), /* 0	= software can set twister parameters */
	Cfg3_GNTSel      	= (1 << 7), /* 1	= delay 1 clock from PCI GNT signal */
};

enum Config4Bits {
	LWPTN	= (1 << 2),	/* not on 8139, 8139A */
};

enum Config5Bits {
	Cfg5_PME_STS      	= (1 << 0), /* 1	= PCI reset resets PME_Status */
	Cfg5_LANWake      	= (1 << 1), /* 1	= enable LANWake signal */
	Cfg5_LDPS         	= (1 << 2), /* 0	= save power when link is down */
	Cfg5_FIFOAddrPtr	= (1 << 3), /* Realtek internal SRAM testing */
	Cfg5_UWF        = (1 << 4), /* 1 = accept unicast wakeup frame */
	Cfg5_MWF        = (1 << 5), /* 1 = accept multicast wakeup frame */
	Cfg5_BWF        = (1 << 6), /* 1 = accept broadcast wakeup frame */
};

enum CSCRBits {
	CSCR_LinkOKBit		= 0x0400,
	CSCR_LinkChangeBit	= 0x0800,
	CSCR_LinkStatusBits	= 0x0f000,
	CSCR_LinkDownOffCmd	= 0x003c0,
	CSCR_LinkDownCmd	= 0x0f3c0,
};

enum Cfg9346Bits {
	Cfg9346_Lock	= 0x00,
	Cfg9346_Unlock	= 0xC0,
};

enum chip_flags {
	HasHltClk	= (1 << 0),
	HasLWake	= (1 << 1),
};

enum TwisterParamVals {
	PARA78_default	= 0x78fa8388,
	PARA7c_default	= 0xcb38de43,	/* param[0][3] */
	PARA7c_xxx	= 0xcb38de43,
};

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* DMA info placeholder */
    uint8_t *rx_ring;
    dma_addr_t rx_ring_dma;
    uint8_t *tx_bufs;
    dma_addr_t tx_bufs_dma;

    /* Register shadows */
    uint32_t mac_reg[8];
    uint32_t mar_reg[2];
    uint32_t tx_status[4];
    uint32_t tx_addr[4];
    uint32_t rx_buf;
    uint8_t chip_cmd;
    uint16_t rx_buf_ptr;
    uint16_t rx_buf_addr;
    uint16_t intr_mask;
    uint16_t intr_status;
    uint32_t tx_config;
    uint32_t rx_config;
    uint32_t timer;
    uint32_t rx_missed;
    uint8_t cfg9346;
    uint8_t config0;
    uint8_t config1;
    uint16_t timer_int;
    uint8_t media_status;
    uint8_t config3;
    uint8_t config4;
    uint8_t hlt_clk;
    uint16_t multi_intr;
    uint16_t tx_summary;
    uint16_t basic_mode_ctrl;
    uint16_t basic_mode_status;
    uint16_t nway_advert;
    uint16_t nway_lpar;
    uint16_t nway_expansion;
    uint32_t fifotms;
    uint16_t cscr;
    uint32_t para78;
    uint32_t flash_reg;
    uint32_t para7c;
    uint8_t config5;

    /* Status fields */
    uint32_t cur_rx;
    uint32_t cur_tx;
    uint32_t dirty_tx;
    uint32_t tx_flag;

    /* reset/probe state */
    bool is_resetting;

    /* power mgmt */
    
    /* other fields */
    
};


/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MAC0 ... MAC0 + 5:
        val = s->mac_reg[0] >> ((addr - MAC0) * 8);
        break;
    case MAC0 + 6 ... MAC0 + 7:
        val = s->mac_reg[1] >> ((addr - MAC0 - 6) * 8);
        break;
    case MAR0 ... MAR0 + 7:
        if (addr < MAR0 + 4) {
            val = s->mar_reg[0] >> ((addr - MAR0) * 8);
        } else {
            val = s->mar_reg[1] >> ((addr - MAR0 - 4) * 8);
        }
        break;
    case TxStatus0 ... TxStatus0 + 15:
        val = s->tx_status[(addr - TxStatus0) / 4];
        break;
    case TxAddr0 ... TxAddr0 + 15:
        val = s->tx_addr[(addr - TxAddr0) / 4];
        break;
    case RxBuf:
        val = s->rx_buf;
        break;
    case ChipCmd:
        val = s->chip_cmd;
        break;
    case RxBufPtr:
        val = s->rx_buf_ptr;
        break;
    case RxBufAddr:
        val = s->rx_buf_addr;
        break;
    case IntrMask:
        val = s->intr_mask;
        break;
    case IntrStatus:
        val = s->intr_status;
        break;
    case TxConfig:
        val = s->tx_config;
        break;
    case RxConfig:
        val = s->rx_config;
        break;
    case Timer:
        val = s->timer;
        break;
    case RxMissed:
        val = s->rx_missed;
        break;
    case Cfg9346:
        val = s->cfg9346;
        break;
    case Config0:
        val = s->config0;
        break;
    case Config1:
        val = s->config1;
        break;
    case TimerInt:
        val = s->timer_int;
        break;
    case MediaStatus:
        val = s->media_status;
        break;
    case Config3:
        val = s->config3;
        break;
    case Config4:
        val = s->config4;
        break;
    case HltClk:
        val = s->hlt_clk;
        break;
    case MultiIntr:
        val = s->multi_intr;
        break;
    case TxSummary:
        val = s->tx_summary;
        break;
    case BasicModeCtrl:
        val = s->basic_mode_ctrl;
        break;
    case BasicModeStatus:
        val = s->basic_mode_status;
        break;
    case NWayAdvert:
        val = s->nway_advert;
        break;
    case NWayLPAR:
        val = s->nway_lpar;
        break;
    case NWayExpansion:
        val = s->nway_expansion;
        break;
    case FIFOTMS:
        val = s->fifotms;
        break;
    case CSCR:
        val = s->cscr;
        break;
    case PARA78:
        val = s->para78;
        break;
    case FlashReg ... FlashReg + 3:
        val = (s->flash_reg >> ((addr - FlashReg) * 8)) & 0xff;
        break;
    case PARA7c:
        val = s->para7c;
        break;
    case Config5:
        val = s->config5;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_read unknown addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        break;
    }

    if (size == 1) {
        val &= 0xff;
    } else if (size == 2) {
        val &= 0xffff;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MAC0 ... MAC0 + 5:
        if (size == 4 && addr == MAC0) {
            s->mac_reg[0] = val;
        } else if (size == 2 && addr == MAC0 + 4) {
            s->mac_reg[1] = (s->mac_reg[1] & 0xffff0000) | (val & 0xffff);
        }
        break;
    case MAC0 + 6 ... MAC0 + 7:
        if (size == 2 && addr == MAC0 + 6) {
            s->mac_reg[1] = (val << 16) | (s->mac_reg[1] & 0xffff);
        }
        break;
    case MAR0 ... MAR0 + 7:
        if (size == 4) {
            if (addr == MAR0) {
                s->mar_reg[0] = val;
            } else if (addr == MAR0 + 4) {
                s->mar_reg[1] = val;
            }
        }
        break;
    case TxStatus0 ... TxStatus0 + 15:
        if (size == 4) {
            int idx = (addr - TxStatus0) / 4;
            s->tx_status[idx] = val;
            /* Trigger TX completion */
            s->intr_status |= TxOK;
            if (s->intr_mask & TxOK) {
                pci_set_irq(&s->parent_obj, 1);
            }
        }
        break;
    case TxAddr0 ... TxAddr0 + 15:
        if (size == 4) {
            s->tx_addr[(addr - TxAddr0) / 4] = val;
        }
        break;
    case RxBuf:
        if (size == 4) {
            s->rx_buf = val;
        }
        break;
    case ChipCmd:
        if (size == 1) {
            s->chip_cmd = val;
            if (val & CmdReset) {
                pcibase_reset(DEVICE(s));
            }
        }
        break;
    case RxBufPtr:
        if (size == 2) {
            s->rx_buf_ptr = val;
        }
        break;
    case RxBufAddr:
        if (size == 2) {
            s->rx_buf_addr = val;
        }
        break;
    case IntrMask:
        if (size == 2) {
            s->intr_mask = val;
        }
        break;
    case IntrStatus:
        if (size == 2) {
            s->intr_status &= ~val;
            if (!s->intr_status) {
                pci_set_irq(&s->parent_obj, 0);
            }
        }
        break;
    case TxConfig:
        if (size == 4) {
            s->tx_config = val;
        }
        break;
    case RxConfig:
        if (size == 4) {
            s->rx_config = val;
        }
        break;
    case Timer:
        if (size == 4) {
            s->timer = val;
        }
        break;
    case RxMissed:
        if (size == 4) {
            s->rx_missed = 0;
        }
        break;
    case Cfg9346:
        if (size == 1) {
            s->cfg9346 = val;
        }
        break;
    case Config0:
        if (size == 1) {
            s->config0 = val;
        }
        break;
    case Config1:
        if (size == 1) {
            s->config1 = val;
        }
        break;
    case TimerInt:
        if (size == 2) {
            s->timer_int = val;
        }
        break;
    case MediaStatus:
        if (size == 1) {
            s->media_status = val;
        }
        break;
    case Config3:
        if (size == 1) {
            s->config3 = val;
        }
        break;
    case Config4:
        if (size == 1) {
            s->config4 = val;
        }
        break;
    case HltClk:
        if (size == 1) {
            s->hlt_clk = val;
        }
        break;
    case MultiIntr:
        if (size == 2) {
            s->multi_intr = val;
        }
        break;
    case TxSummary:
        if (size == 2) {
            s->tx_summary = val;
        }
        break;
    case BasicModeCtrl:
        if (size == 2) {
            s->basic_mode_ctrl = val;
        }
        break;
    case BasicModeStatus:
        if (size == 2) {
            s->basic_mode_status = val;
        }
        break;
    case NWayAdvert:
        if (size == 2) {
            s->nway_advert = val;
        }
        break;
    case NWayLPAR:
        if (size == 2) {
            s->nway_lpar = val;
        }
        break;
    case NWayExpansion:
        if (size == 2) {
            s->nway_expansion = val;
        }
        break;
    case FIFOTMS:
        if (size == 4) {
            s->fifotms = val;
        }
        break;
    case CSCR:
        if (size == 2) {
            s->cscr = val;
        }
        break;
    case PARA78:
        if (size == 4) {
            s->para78 = val;
        }
        break;
    case FlashReg ... FlashReg + 3:
        if (size == 1) {
            s->flash_reg = (s->flash_reg & ~(0xff << ((addr - FlashReg) * 8))) |
                           ((val & 0xff) << ((addr - FlashReg) * 8));
        }
        break;
    case PARA7c:
        if (size == 4) {
            s->para7c = val;
        }
        break;
    case Config5:
        if (size == 1) {
            s->config5 = val;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_write unknown addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    memset(s->mac_reg, 0, sizeof(s->mac_reg));
    memset(s->mar_reg, 0, sizeof(s->mar_reg));
    memset(s->tx_status, 0, sizeof(s->tx_status));
    memset(s->tx_addr, 0, sizeof(s->tx_addr));
    s->rx_buf = 0;
    s->chip_cmd = 0;
    s->rx_buf_ptr = 0;
    s->rx_buf_addr = 0;
    s->intr_mask = 0;
    s->intr_status = 0;
    s->tx_config = 0;
    s->rx_config = 0;
    s->timer = 0;
    s->rx_missed = 0;
    s->cfg9346 = 0;
    s->config0 = 0;
    s->config1 = 0;
    s->timer_int = 0;
    s->media_status = 0;
    s->config3 = 0;
    s->config4 = 0;
    s->hlt_clk = 0;
    s->multi_intr = 0;
    s->tx_summary = 0;
    s->basic_mode_ctrl = 0;
    s->basic_mode_status = 0;
    s->nway_advert = 0;
    s->nway_lpar = 0;
    s->nway_expansion = 0;
    s->fifotms = 0;
    s->cscr = 0;
    s->para78 = 0;
    s->flash_reg = 0;
    s->para7c = 0;
    s->config5 = 0;

    s->cur_rx = 0;
    s->cur_tx = 0;
    s->dirty_tx = 0;
    s->tx_flag = 0;

    s->is_resetting = false;
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    s->rx_ring = g_malloc0(RX_BUF_TOT_LEN);
    s->rx_ring_dma = 0x10000000; /* fake DMA address */

    s->tx_bufs = g_malloc0(TX_BUF_TOT_LEN);
    s->tx_bufs_dma = 0x10001000; /* fake DMA address */
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x10);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set up BAR0 as PIO */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = RTL8139B_IO_SIZE;
    s->bar_info[0].name = "rtl8139-pio";
    s->num_bars = 1;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config */
    /* Legacy INTx only */

    /* DMA init */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    /* Initialize default register values */
    s->tx_config = HW_REVID(1, 1, 1, 1, 1, 1, 1); /* RTL8139C+ */
    s->basic_mode_status = 0x7809; /* Link up, 100Mbps, full duplex */
    s->media_status = 0x50; /* TX/RX enabled */

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->rx_ring) {
        g_free(s->rx_ring);
        s->rx_ring = NULL;
    }
    if (s->tx_bufs) {
        g_free(s->tx_bufs);
        s->tx_bufs = NULL;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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