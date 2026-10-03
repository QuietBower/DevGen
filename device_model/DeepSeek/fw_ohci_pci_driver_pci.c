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
/* none */

#define TYPE_PCIBASE_DEVICE "fw_ohci_pci_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI IDs: first pci_device_id entry is class-based, so use PCI_ANY_ID for vendor/device */
#define PCI_VENDOR_ID_OHCI 0xFFFF   /* PCI_ANY_ID */
#define PCI_DEVICE_ID_OHCI 0xFFFF   /* PCI_ANY_ID */
#define PCI_CLASS_SERIAL_FIREWIRE_OHCI	0x0c0010

/* Driver register offsets and bit definitions */
#define OHCI1394_REGISTER_SIZE		0x800
#define OHCI1394_PCI_HCI_Control	0x40
#define SELF_ID_BUF_SIZE		0x800
#define OHCI_VERSION_1_1		0x010010

#define CONTEXT_ACTIVE	0x0400
#define CONTROL_SET(regs)	(regs)
#define CONTROL_CLEAR(regs)	((regs) + 4)
#define COMMAND_PTR(regs)	((regs) + 12)
#define CONTEXT_MATCH(regs)	((regs) + 16)

#define OHCI1394_IsoXmitContextBase(n)           (0x200 + 16 * (n))
#define OHCI1394_IsoRcvContextBase(n)         (0x400 + 32 * (n))
#define OHCI1394_IsoRcvContextControlClear(n) (0x404 + 32 * (n))

/* Descriptor control bits */
#define DESCRIPTOR_OUTPUT_MORE		0
#define DESCRIPTOR_OUTPUT_LAST		(1 << 12)
#define DESCRIPTOR_INPUT_MORE		(2 << 12)
#define DESCRIPTOR_INPUT_LAST		(3 << 12)
#define DESCRIPTOR_STATUS		(1 << 11)
#define DESCRIPTOR_KEY_IMMEDIATE	(2 << 8)
#define DESCRIPTOR_PING			(1 << 7)
#define DESCRIPTOR_YY			(1 << 6)
#define DESCRIPTOR_NO_IRQ		(0 << 4)
#define DESCRIPTOR_IRQ_ERROR		(1 << 4)
#define DESCRIPTOR_IRQ_ALWAYS		(3 << 4)
#define DESCRIPTOR_BRANCH_ALWAYS	(3 << 2)
#define DESCRIPTOR_WAIT			(3 << 0)
#define DESCRIPTOR_CMD			(0xf << 12)

/* Context states */
#define CONTEXT_RUN	0x8000
#define CONTEXT_WAKE	0x1000
#define CONTEXT_DEAD	0x0800

/* PHY control */
#define OHCI1394_PhyControl_Read(addr)	(((addr) << 8) | 0x00008000)
#define OHCI1394_PhyControl_ReadData(r)	(((r) & 0x00ff0000) >> 16)
#define OHCI1394_PhyControl_Write(addr, data)	(((addr) << 8) | (data) | 0x00004000)
#define PHY_INT_STATUS_BITS	0x3c
#define PHY_PAGE_SELECT		0xe0
#define PHY_ENABLE_MULTI	0x01
#define PHY_ENABLE_ACCEL	0x02
#define PHY_EXTENDED_REGISTERS	0xe0
#define PHY_CONTENDER		0x40
#define PHY_LINK_ACTIVE		0x80

/* CSR state bits */
#define CSR_STATE_BIT_ABDICATE	(1 << 10)
#define CSR_STATE_BIT_CMSTR	(1 << 8)

#define LOCAL_BUS 0xffc0

/* Isochronous context control bits */
#define IT_CONTEXT_CYCLE_MATCH_ENABLE	0x80000000
#define IR_CONTEXT_BUFFER_FILL		0x80000000
#define IR_CONTEXT_ISOCH_HEADER		0x40000000
#define IR_CONTEXT_CYCLE_MATCH_ENABLE	0x20000000
#define IR_CONTEXT_MULTI_CHANNEL_MODE	0x10000000
#define IR_CONTEXT_DUAL_BUFFER_MODE	0x08000000

/* AR buffer sizing */
#define AR_BUFFER_SIZE	(32*1024)
#define AR_BUFFERS_MIN	DIV_ROUND_UP(AR_BUFFER_SIZE, PAGE_SIZE)
#define AR_BUFFERS	MAX(2, AR_BUFFERS_MIN)
#define MAX_ASYNC_PAYLOAD	4096
#define MAX_AR_PACKET_SIZE	(16 + MAX_ASYNC_PAYLOAD + 4)
#define AR_WRAPAROUND_PAGES	DIV_ROUND_UP(MAX_AR_PACKET_SIZE, PAGE_SIZE)

/* Async header bits */
#define ASYNC_HEADER_Q3_DATA_LENGTH_MASK	0xffff0000
#define ASYNC_HEADER_QUADLET_COUNT		4
#define ASYNC_HEADER_Q3_DATA_LENGTH_SHIFT	16
#define ASYNC_HEADER_Q0_TCODE_SHIFT		4
#define ASYNC_HEADER_Q0_TCODE_MASK		0x000000f0
#define ASYNC_HEADER_Q1_RCODE_SHIFT		12
#define ASYNC_HEADER_Q1_RCODE_MASK		0x0000f000
#define ASYNC_HEADER_Q0_TLABEL_MASK		0x0000fc00
#define ASYNC_HEADER_Q0_TLABEL_SHIFT		10
#define ASYNC_HEADER_Q0_RETRY_MASK		0x00000300
#define ASYNC_HEADER_Q0_RETRY_SHIFT		8
#define ASYNC_HEADER_Q1_OFFSET_HIGH_MASK	0x0000ffff
#define ASYNC_HEADER_Q1_OFFSET_HIGH_SHIFT	0
#define ASYNC_HEADER_Q0_DESTINATION_SHIFT	16
#define ASYNC_HEADER_Q0_DESTINATION_MASK	0xffff0000

/* Isochronous header bits */
#define ISOC_HEADER_SY_MASK			0x0000000f
#define ISOC_HEADER_SY_SHIFT			0
#define ISOC_HEADER_DATA_LENGTH_SHIFT		16
#define ISOC_HEADER_DATA_LENGTH_MASK		0xffff0000
#define ISOC_HEADER_CHANNEL_MASK		0x00003f00
#define ISOC_HEADER_CHANNEL_SHIFT		8
#define ISOC_HEADER_TAG_MASK			0x0000c000
#define ISOC_HEADER_TAG_SHIFT			14

#define ASYNC_HEADER_Q3_EXTENDED_TCODE_MASK	0x0000ffff
#define ASYNC_HEADER_Q3_EXTENDED_TCODE_SHIFT	0

/* Self ID bits */
#define SELF_ID_PHY_ID_SHIFT			24
#define SELF_ID_PHY_ID_MASK			0x3f000000
#define OHCI1394_SELF_ID_RECEIVE_Q0_GENERATION_SHIFT	16
#define OHCI1394_SELF_ID_RECEIVE_Q0_GENERATION_MASK	0x00ff0000

#define ASYNC_HEADER_Q1_SOURCE_SHIFT		16
#define ASYNC_HEADER_Q1_SOURCE_MASK		0xffff0000

#define BROADCAST_CHANNEL_INITIAL	(1 << 31 | 31)

/* Other constants */
#define OHCI1394_MAX_AT_REQ_RETRIES	0xf
#define OHCI1394_MAX_AT_RESP_RETRIES	0x2
#define OHCI1394_MAX_PHYS_RESP_RETRIES	0x8
#define TCODE_LINK_INTERNAL		0xe
#define FW_MAX_PHYSICAL_RANGE		(1ULL << 32)

#define CSR_CONFIG_ROM			0x400
#define CSR_CONFIG_ROM_END		0x800
#define CONFIG_ROM_SIZE		(CSR_CONFIG_ROM_END - CSR_CONFIG_ROM)

/* Quirk flags for reference (not used directly in emulation) */
#define QUIRK_CYCLE_TIMER		0x1
#define QUIRK_RESET_PACKET		0x2
#define QUIRK_BE_HEADERS		0x4
#define QUIRK_NO_1394A			0x8
#define QUIRK_NO_MSI			0x10
#define QUIRK_TI_SLLZ059		0x20
#define QUIRK_IR_WAKE			0x40
#define QUIRK_REBOOT_BY_CYCLE_TIMER_READ	0x80000000

/* Additional register offsets (from OHCI spec) */
#define OHCI1394_Version                          0x000
#define OHCI1394_GUID_ROM                         0x004
#define OHCI1394_ATRetries                        0x008
#define OHCI1394_CSRData                          0x00C
#define OHCI1394_CSRCompareData                   0x010
#define OHCI1394_CSRControl                       0x014
#define OHCI1394_ConfigROMhdr                     0x018
#define OHCI1394_BusOptions                       0x01C
#define OHCI1394_GUIDHi                           0x020
#define OHCI1394_GUIDLo                           0x024
#define OHCI1394_ConfigROMmap                     0x028
#define OHCI1394_PostedWriteAddressLo             0x02C
#define OHCI1394_PostedWriteAddressHi             0x030
#define OHCI1394_VendorID                         0x034
#define OHCI1394_PhyUpperBound                    0x03c

#define OHCI1394_HCControlSet                     0x050
#define OHCI1394_HCControlClear                   0x054
#define OHCI1394_SelfIDBuffer                     0x058
#define OHCI1394_SelfIDCount                      0x05C
#define OHCI1394_LinkControlSet                   0x060
#define OHCI1394_LinkControlClear                 0x064
#define OHCI1394_NodeID                           0x068
#define OHCI1394_PhyControl                       0x06C
#define OHCI1394_IsochronousCycleTimer            0x070
#define OHCI1394_FairnessControl                  0x074

#define OHCI1394_IntEvent                         0x080
#define OHCI1394_IntMask                          0x084
#define OHCI1394_IntClear                         0x088
#define OHCI1394_IntSet                           0x08C
#define OHCI1394_IntEventSet                      0x090
#define OHCI1394_IntEventClear                    0x094
#define OHCI1394_IntMaskSet                       0x098
#define OHCI1394_IntMaskClear                     0x09C

#define OHCI1394_IsoXmitIntEvent                  0x0A0
#define OHCI1394_IsoXmitIntMask                   0x0A4
#define OHCI1394_IsoXmitIntClear                  0x0A8
#define OHCI1394_IsoXmitIntSet                    0x0AC
#define OHCI1394_IsoXmitIntEventSet               0x0B0
#define OHCI1394_IsoXmitIntEventClear             0x0B4
#define OHCI1394_IsoXmitIntMaskSet                0x0B8
#define OHCI1394_IsoXmitIntMaskClear              0x0BC

#define OHCI1394_IsoRecvIntEvent                  0x0C0
#define OHCI1394_IsoRecvIntMask                   0x0C4
#define OHCI1394_IsoRecvIntClear                  0x0C8
#define OHCI1394_IsoRecvIntSet                    0x0CC
#define OHCI1394_IsoRecvIntEventSet               0x0D0
#define OHCI1394_IsoRecvIntEventClear             0x0D4
#define OHCI1394_IsoRecvIntMaskSet                0x0D8
#define OHCI1394_IsoRecvIntMaskClear              0x0DC

#define OHCI1394_IRMultiChanMaskHiSet             0x0E0
#define OHCI1394_IRMultiChanMaskHiClear           0x0E4
#define OHCI1394_IRMultiChanMaskLoSet             0x0E8
#define OHCI1394_IRMultiChanMaskLoClear           0x0EC
#define OHCI1394_PhyReqFilterHiSet                0x0F0
#define OHCI1394_PhyReqFilterHiClear              0x0F4
#define OHCI1394_PhyReqFilterLoSet                0x0F8
#define OHCI1394_PhyReqFilterLoClear              0x0FC
#define OHCI1394_AsReqFilterHiSet                 0x100
#define OHCI1394_AsReqFilterLoSet                 0x104

#define OHCI1394_AsReqTrContextControlSet         0x180
#define OHCI1394_AsRspTrContextControlSet         0x190
#define OHCI1394_AsReqRcvContextControlSet        0x1A0
#define OHCI1394_AsRspRcvContextControlSet        0x1B0

#define OHCI1394_InitialChannelsAvailableHi       0x034
#define OHCI1394_InitialChannelsAvailableLo       0x038

/* HCControl bit definitions */
#define OHCI1394_HCControl_softReset           (1 << 0)
#define OHCI1394_HCControl_linkEnable          (1 << 1)
#define OHCI1394_HCControl_BIBimageValid       (1 << 2)
#define OHCI1394_HCControl_postedWriteEnable   (1 << 4)
#define OHCI1394_HCControl_LPS                 (1 << 7)
#define OHCI1394_HCControl_programPhyEnable    (1 << 8)
#define OHCI1394_HCControl_aPhyEnhanceEnable   (1 << 9)
#define OHCI1394_HCControl_noByteSwapData      (1 << 10)

/* LinkControl bit definitions */
#define OHCI1394_LinkControl_cycleMaster       (1 << 0)
#define OHCI1394_LinkControl_cycleTimerEnable  (1 << 1)
#define OHCI1394_LinkControl_rcvSelfID         (1 << 2)
#define OHCI1394_LinkControl_rcvPhyPkt         (1 << 3)

/* Interrupt event/mask bit definitions */
#define OHCI1394_busReset            (1 << 0)
#define OHCI1394_selfIDComplete      (1 << 1)
#define OHCI1394_postedWriteErr      (1 << 2)
#define OHCI1394_cycleInconsistent   (1 << 3)
#define OHCI1394_cycleTooLong        (1 << 4)
#define OHCI1394_reqTxComplete       (1 << 5)
#define OHCI1394_respTxComplete      (1 << 6)
#define OHCI1394_RQPkt               (1 << 7)
#define OHCI1394_RSPkt               (1 << 8)
#define OHCI1394_isochTx             (1 << 9)
#define OHCI1394_isochRx             (1 << 10)
#define OHCI1394_regAccessFail       (1 << 11)
#define OHCI1394_unrecoverableError  (1 << 12)
#define OHCI1394_cycle64Seconds      (1 << 13)
#define OHCI1394_masterIntEnable     (1 << 31)

/* NodeID bits */
#define OHCI1394_NodeID_busNumber    (0xff << 6)
#define OHCI1394_NodeID_nodeNumber   (0x3f)
#define OHCI1394_NodeID_idValid      (1 << 31)
#define OHCI1394_NodeID_root         (1 << 30)

/* SelfIDCount bits */
#define OHCI1394_SelfIDCount_selfIDGeneration_SHIFT  24
#define OHCI1394_SelfIDCount_selfIDGeneration_MASK   0xff000000
#define OHCI1394_SelfIDCount_selfIDSize_SHIFT        16
#define OHCI1394_SelfIDCount_selfIDSize_MASK         0x00ff0000
#define OHCI1394_SelfIDCount_selfIDError_SHIFT       15
#define OHCI1394_SelfIDCount_selfIDError_MASK        0x00008000

/* PHY Control bits */
#define OHCI1394_PhyControl_ReadDone     (1 << 31)
#define OHCI1394_PhyControl_WritePending (1 << 3)    /* assuming */

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

/* Hardware DMA descriptor format (from driver source) */
struct descriptor {
    __le16 req_count;
    __le16 control;
    __le32 data_address;
    __le32 branch_address;
    __le16 res_count;
    __le16 transfer_status;
};

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
    uint32_t regs[OHCI1394_REGISTER_SIZE / sizeof(uint32_t)];  /* 0x800 bytes reg space */

    /* DMA Context */
    dma_addr_t dma_ctx_base;  /* base of DMA context registers? Placeholder */
    int dma_running;

    /* Operational status flags */
    uint32_t status;

    /* State used to handle reset sequences */
    bool software_reset_pending;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* Additional info */
    /* Placeholder for future additions */
};

/* Static tables and definitions */
/* None beyond the structs above */

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t event = s->regs[OHCI1394_IntEvent >> 2];
    uint32_t mask  = s->regs[OHCI1394_IntMask >> 2];

    if (event & mask & OHCI1394_masterIntEnable) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Map address to regs index and handle Set/Clear pairs */
static void handle_set_clear_write(PCIBaseState *s, hwaddr addr, uint64_t val,
                                   hwaddr set_addr, hwaddr clear_addr, unsigned reg_idx)
{
    if (addr == set_addr) {
        s->regs[reg_idx] |= val;
        s->regs[clear_addr >> 2] = s->regs[reg_idx];
    } else if (addr == clear_addr) {
        s->regs[reg_idx] &= ~val;
        s->regs[clear_addr >> 2] = s->regs[reg_idx];
    }
    /* Update other side */
    s->regs[set_addr >> 2] = s->regs[reg_idx];
    s->regs[clear_addr >> 2] = s->regs[reg_idx];
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t offset = addr & 0x7ff;
    unsigned reg_idx = offset >> 2;

    if (size != 4) {
        return ~0ULL;
    }

    if (offset >= OHCI1394_REGISTER_SIZE) {
        return 0;
    }

    /* Handle register read */
    val = s->regs[reg_idx];

    /* Special registers */
    if (offset == OHCI1394_Version) {
        val = OHCI_VERSION_1_1; /* constant */
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr & 0x7ff;
    unsigned reg_idx = offset >> 2;

    if (size != 4) {
        return;
    }

    if (offset >= OHCI1394_REGISTER_SIZE) {
        return;
    }

    /* Handle Set/Clear registers */
    if (offset == OHCI1394_HCControlSet || offset == OHCI1394_HCControlClear) {
        /* Master HCControl is at HCControlSet reg_idx */
        unsigned hc_idx = OHCI1394_HCControlSet >> 2;
        if (offset == OHCI1394_HCControlSet) {
            s->regs[hc_idx] |= val;
            s->regs[OHCI1394_HCControlClear >> 2] = s->regs[hc_idx];
        } else { /* Clear */
            s->regs[hc_idx] &= ~val;
            s->regs[OHCI1394_HCControlSet >> 2] = s->regs[hc_idx];
        }
        s->regs[hc_idx] = s->regs[hc_idx];
        s->regs[OHCI1394_HCControlClear >> 2] = s->regs[hc_idx];
        /* Side effects */
        if (s->regs[hc_idx] & OHCI1394_HCControl_softReset) {
            /* Clear softReset immediately */
            s->regs[hc_idx] &= ~OHCI1394_HCControl_softReset;
            s->regs[OHCI1394_HCControlSet >> 2] = s->regs[hc_idx];
            s->regs[OHCI1394_HCControlClear >> 2] = s->regs[hc_idx];
        }
        if (s->regs[hc_idx] & OHCI1394_HCControl_LPS) {
            /* LPS set, no action */
        }
    } else if (offset == OHCI1394_LinkControlSet || offset == OHCI1394_LinkControlClear) {
        unsigned lc_idx = OHCI1394_LinkControlSet >> 2;
        if (offset == OHCI1394_LinkControlSet) {
            s->regs[lc_idx] |= val;
            s->regs[OHCI1394_LinkControlClear >> 2] = s->regs[lc_idx];
        } else {
            s->regs[lc_idx] &= ~val;
            s->regs[OHCI1394_LinkControlSet >> 2] = s->regs[lc_idx];
        }
    } else if (offset == OHCI1394_IntEventSet || offset == OHCI1394_IntEventClear) {
        unsigned int_idx = OHCI1394_IntEvent >> 2;
        if (offset == OHCI1394_IntEventSet) {
            s->regs[int_idx] |= val;
            s->regs[OHCI1394_IntEventClear >> 2] = s->regs[int_idx];
        } else {
            s->regs[int_idx] &= ~val;
            s->regs[OHCI1394_IntEventSet >> 2] = s->regs[int_idx];
        }
        s->regs[OHCI1394_IntEvent >> 2] = s->regs[int_idx];
        s->regs[OHCI1394_IntEventSet >> 2] = s->regs[int_idx];
        s->regs[OHCI1394_IntEventClear >> 2] = s->regs[int_idx];
        pcibase_update_irq(s);
    } else if (offset == OHCI1394_IntMaskSet || offset == OHCI1394_IntMaskClear) {
        unsigned mask_idx = OHCI1394_IntMask >> 2;
        if (offset == OHCI1394_IntMaskSet) {
            s->regs[mask_idx] |= val;
            s->regs[OHCI1394_IntMaskClear >> 2] = s->regs[mask_idx];
        } else {
            s->regs[mask_idx] &= ~val;
            s->regs[OHCI1394_IntMaskSet >> 2] = s->regs[mask_idx];
        }
        s->regs[OHCI1394_IntMask >> 2] = s->regs[mask_idx];
        s->regs[OHCI1394_IntMaskSet >> 2] = s->regs[mask_idx];
        s->regs[OHCI1394_IntMaskClear >> 2] = s->regs[mask_idx];
        pcibase_update_irq(s);
    } else if (offset == OHCI1394_PhyControl) {
        /* Emulate PHY register access */
        static uint32_t phy_regs[16];
        if (val & 0x8000) { /* Read command */
            int phy_addr = (val >> 8) & 0x7f;
            uint32_t data = 0;
            if (phy_addr < 16) {
                data = phy_regs[phy_addr];
            }
            /* Set ReadDone and data */
            s->regs[reg_idx] = (data << 16) | (1 << 31);
        } else if (val & 0x4000) { /* Write command */
            int phy_addr = (val >> 8) & 0x7f;
            uint32_t data = val & 0xff;
            if (phy_addr < 16) {
                phy_regs[phy_addr] = data;
            }
            s->regs[reg_idx] = 0; /* Clear WritePending */
        } else {
            s->regs[reg_idx] = val;
        }
    } else if (offset == OHCI1394_IsoXmitIntMaskSet || offset == OHCI1394_IsoXmitIntMaskClear) {
        unsigned mask_idx = OHCI1394_IsoXmitIntMask >> 2;
        if (offset == OHCI1394_IsoXmitIntMaskSet) {
            /* Only store supported contexts (e.g., 0xf) */
            s->regs[mask_idx] |= (val & 0xf);
            s->regs[OHCI1394_IsoXmitIntMaskClear >> 2] = s->regs[mask_idx];
        } else {
            s->regs[mask_idx] &= ~val;
            s->regs[OHCI1394_IsoXmitIntMaskSet >> 2] = s->regs[mask_idx];
        }
        s->regs[OHCI1394_IsoXmitIntMask >> 2] = s->regs[mask_idx];
        s->regs[OHCI1394_IsoXmitIntMaskSet >> 2] = s->regs[mask_idx];
        s->regs[OHCI1394_IsoXmitIntMaskClear >> 2] = s->regs[mask_idx];
    } else if (offset == OHCI1394_IsoRecvIntMaskSet || offset == OHCI1394_IsoRecvIntMaskClear) {
        unsigned mask_idx = OHCI1394_IsoRecvIntMask >> 2;
        if (offset == OHCI1394_IsoRecvIntMaskSet) {
            s->regs[mask_idx] |= (val & 0xf);
            s->regs[OHCI1394_IsoRecvIntMaskClear >> 2] = s->regs[mask_idx];
        } else {
            s->regs[mask_idx] &= ~val;
            s->regs[OHCI1394_IsoRecvIntMaskSet >> 2] = s->regs[mask_idx];
        }
        s->regs[OHCI1394_IsoRecvIntMask >> 2] = s->regs[mask_idx];
        s->regs[OHCI1394_IsoRecvIntMaskSet >> 2] = s->regs[mask_idx];
        s->regs[OHCI1394_IsoRecvIntMaskClear >> 2] = s->regs[mask_idx];
    } else {
        /* Default: store value */
        s->regs[reg_idx] = val;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* No PIO */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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
    s->status = 0;
    s->software_reset_pending = false;
    s->pm_state = 0; /* D0 */
    s->dma_running = 0;

    /* Set default register values for probe success */
    s->regs[OHCI1394_Version >> 2] = OHCI_VERSION_1_1;
    s->regs[OHCI1394_GUIDHi >> 2] = 0x00112233; /* example GUID */
    s->regs[OHCI1394_GUIDLo >> 2] = 0x44556677;
    /* Support 4 IR and 4 IT contexts */
    s->regs[OHCI1394_IsoRecvIntMask >> 2] = 0x0000000f;
    s->regs[OHCI1394_IsoXmitIntMask >> 2] = 0x0000000f;
    /* Initial channels available (used in enable) */
    s->regs[OHCI1394_InitialChannelsAvailableHi >> 2] = 0xfffffffe;
    /* SelfIDCount default */
    s->regs[OHCI1394_SelfIDCount >> 2] = 0x00000000; /* no error, size 0 */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_OHCI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_OHCI);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_FIREWIRE_OHCI);
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = OHCI1394_REGISTER_SIZE, .name = "ohci-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization */
    s->has_msi = true;
    s->has_msix = false;
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    /* DMA Configuration */
    /* No explicit DMA mask set */
    /* Timer Configuration */
    /* No hardware timers initialized yet */
    /* Field Init */
    pcibase_reset(DEVICE(s));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status = 0;
    s->software_reset_pending = false;
    s->pm_state = 0;
    s->dma_running = 0;
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
}

/* Minimal VMState */
static const VMStateDescription vmstate_pcibase = {
    .name = "fw_ohci_pci_driver_pci",
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
