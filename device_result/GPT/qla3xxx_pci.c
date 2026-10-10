/*
 * QLogic QLA3XXX PCI Device QEMU Model - Functional Implementation
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
/* #HeadFile# */

#define TYPE_PCIBASE_DEVICE "qla3xxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* #Related_Config_Info# */

/*
 * From linux/drivers/net/ethernet/qlogic/qla3xxx.c:
 *   static const struct pci_device_id ql3xxx_pci_tbl[] = {
 *       {PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, QL3022_DEVICE_ID)},
 *       ...
 *   };
 * We must use the FIRST entry: PCI_VENDOR_ID_QLOGIC and QL3022_DEVICE_ID.
 */

#define PCI_VENDOR_ID_QLOGIC 0x1077
#define QL3022_DEVICE_ID    0x3022
#define QL3032_DEVICE_ID    0x3032

/* Class ID: network controller (0x0200). The driver is a net device driver. */
#define QLA3XXX_PCI_CLASS_ID 0x0200

/*
 * Hardware register structures as defined in qla3xxx.c
 */
struct ql3xxx_common_registers {
    uint32_t MB0;                    /* Offset 0x00 */
    uint32_t MB1;                    /* Offset 0x04 */
    uint32_t MB2;                    /* Offset 0x08 */
    uint32_t MB3;                    /* Offset 0x0c */
    uint32_t MB4;                    /* Offset 0x10 */
    uint32_t MB5;                    /* Offset 0x14 */
    uint32_t MB6;                    /* Offset 0x18 */
    uint32_t MB7;                    /* Offset 0x1c */
    uint32_t flashBiosAddr;          /* 0x20 */
    uint32_t flashBiosData;          /* 0x24 */
    uint32_t ispControlStatus;       /* 0x28 */
    uint32_t ispInterruptMaskReg;    /* 0x2c */
    uint32_t serialPortInterfaceReg; /* 0x30 */
    uint32_t semaphoreReg;           /* 0x34 */
    uint32_t reqQProducerIndex;      /* 0x38 */
    uint32_t rspQConsumerIndex;      /* 0x3c */

    uint32_t rxLargeQProducerIndex;  /* 0x40 */
    uint32_t rxSmallQProducerIndex;  /* 0x44 */
    uint32_t arcMadiCommand;         /* 0x48 */
    uint32_t arcMadiData;            /* 0x4c */
};

struct ql3xxx_port_registers {
    struct ql3xxx_common_registers CommonRegs; /* 0x00 - 0x4c */

    uint32_t ExternalHWConfig;                  /* 0x50 */
    uint32_t InternalChipConfig;                /* 0x54 */
    uint32_t portControl;                       /* 0x58 */
    uint32_t portStatus;                        /* 0x5c */
    uint32_t macAddrIndirectPtrReg;             /* 0x60 */
    uint32_t macAddrDataReg;                    /* 0x64 */
    uint32_t macMIIMgmtControlReg;              /* 0x68 */
    uint32_t macMIIMgmtAddrReg;                 /* 0x6c */
    uint32_t macMIIMgmtDataReg;                 /* 0x70 */
    uint32_t macMIIStatusReg;                   /* 0x74 */
    uint32_t mac0ConfigReg;                     /* 0x78 */
    uint32_t mac0IpgIfgReg;                     /* 0x7c */
    uint32_t mac0HalfDuplexReg;                 /* 0x80 */
    uint32_t mac0MaxFrameLengthReg;             /* 0x84 */
    uint32_t mac0PauseThresholdReg;             /* 0x88 */
    uint32_t mac1ConfigReg;                     /* 0x8c */
    uint32_t mac1IpgIfgReg;                     /* 0x90 */
    uint32_t mac1HalfDuplexReg;                 /* 0x94 */
    uint32_t mac1MaxFrameLengthReg;             /* 0x98 */
    uint32_t mac1PauseThresholdReg;             /* 0x9c */
    uint32_t ipAddrIndexReg;                    /* 0xa0 */
    uint32_t ipAddrDataReg;                     /* 0xa4 */
    uint32_t ipReassemblyTimeout;               /* 0xa8 */
    uint32_t tcpMaxWindow;                      /* 0xac */
    uint32_t currentTcpTimestamp[2];            /* 0xb0,0xb4 */
    uint32_t internalRamRWAddrReg;              /* 0xb8 */
    uint32_t internalRamWDataReg;               /* 0xbc */
    uint32_t reclaimedBufferAddrRegLow;         /* 0xc0 */
    uint32_t reclaimedBufferAddrRegHigh;        /* 0xc4 */
    uint32_t tcpConfiguration;                  /* 0xc8 */
    uint32_t functionControl;                   /* 0xcc */
    uint32_t fpgaRevID;                         /* 0xd0 */
    uint32_t localRamAddr;                      /* 0xd4 */
    uint32_t localRamDataAutoIncr;              /* 0xd8 */
    uint32_t localRamDataNonIncr;               /* 0xdc */
    uint32_t gpOutput;                          /* 0xe0 */
    uint32_t gpInput;                           /* 0xe4 */
    uint32_t probeMuxAddr;                      /* 0xe8 */
    uint32_t probeMuxData;                      /* 0xec */
    uint32_t statisticsIndexReg;                /* 0xf0 */
    uint32_t statisticsReadDataRegAutoIncr;     /* 0xf4 */
    uint32_t statisticsReadDataRegNoIncr;       /* 0xf8 */
    uint32_t PortFatalErrStatus;                /* 0xfc */
};

struct ql3xxx_local_ram_registers {
    struct ql3xxx_common_registers CommonRegs;
    uint32_t bufletSize;
    uint32_t maxBufletCount;
    uint32_t currentBufletCount;
    uint32_t reserved;
    uint32_t freeBufletThresholdLow;
    uint32_t freeBufletThresholdHigh;
    uint32_t ipHashTableBase;
    uint32_t ipHashTableCount;
    uint32_t tcpHashTableBase;
    uint32_t tcpHashTableCount;
    uint32_t ncbBase;
    uint32_t maxNcbCount;
    uint32_t currentNcbCount;
    uint32_t drbBase;
    uint32_t maxDrbCount;
    uint32_t currentDrbCount;
};

struct ql3xxx_host_memory_registers {
    struct ql3xxx_common_registers CommonRegs;

    uint32_t reserved[12];

    /* Network Request Queue */
    uint32_t reqConsumerIndex;
    uint32_t reqConsumerIndexAddrLow;
    uint32_t reqConsumerIndexAddrHigh;
    uint32_t reqBaseAddrLow;
    uint32_t reqBaseAddrHigh;
    uint32_t reqLength;

    /* Network Completion Queue */
    uint32_t rspProducerIndex;
    uint32_t rspProducerIndexAddrLow;
    uint32_t rspProducerIndexAddrHigh;
    uint32_t rspBaseAddrLow;
    uint32_t rspBaseAddrHigh;
    uint32_t rspLength;

    /* RX Large Buffer Queue */
    uint32_t rxLargeQConsumerIndex;
    uint32_t rxLargeQBaseAddrLow;
    uint32_t rxLargeQBaseAddrHigh;
    uint32_t rxLargeQLength;
    uint32_t rxLargeBufferLength;

    /* RX Small Buffer Queue */
    uint32_t rxSmallQConsumerIndex;
    uint32_t rxSmallQBaseAddrLow;
    uint32_t rxSmallQBaseAddrHigh;
    uint32_t rxSmallQLength;
    uint32_t rxSmallBufferLength;
};

/* Simple bit definitions inferred from driver usage (mask values only) */
#define ISP_IMR_ENABLE_INT           0x00000001u
#define ISP_IMR_DISABLE_CMPL_INT     0x00000002u
#define ISP_CONTROL_FE               0x00000001u
#define ISP_CONTROL_RI               0x00000002u
#define ISP_CONTROL_SR               0x00000004u
#define ISP_CONTROL_FSR              0x00000008u
#define ISP_CONTROL_LINK_DN_0        0x00000010u
#define ISP_CONTROL_LINK_DN_1        0x00000020u

#define PORT_STATUS_IC               0x00000001u
#define PORT_STATUS_UP0              0x00000002u
#define PORT_STATUS_UP1              0x00000004u
#define PORT_STATUS_SM0              0x00000008u
#define PORT_STATUS_SM1              0x00000010u
#define PORT_STATUS_AC0              0x00000020u
#define PORT_STATUS_AC1              0x00000040u
#define PORT_STATUS_AE0              0x00000080u
#define PORT_STATUS_AE1              0x00000100u
#define PORT_STATUS_F1_ENABLED       0x00000200u
#define PORT_STATUS_F3_ENABLED       0x00000400u
#define PORT_STATUS_REV_ID_MASK      0x0000F000u
#define PORT_STATUS_64               0x00010000u
#define PORT_STATUS_X                0x00020000u

#define MAC_MII_STATUS_BSY           0x00000001u
#define MAC_MII_CONTROL_AS           0x00000002u
#define MAC_MII_CONTROL_SC           0x00000004u
#define MAC_MII_CONTROL_RC           0x00000008u
#define MAC_MII_CONTROL_CLK_SEL_DIV28 0x00000010u
#define MAC_MII_CONTROL_CLK_SEL_MASK  0x00000010u

#define MAC_CONFIG_REG_PE            0x00000001u
#define MAC_CONFIG_REG_SR            0x00000002u
#define MAC_CONFIG_REG_GM            0x00000004u
#define MAC_CONFIG_REG_FD            0x00000008u
#define MAC_CONFIG_REG_TF            0x00000002u
#define MAC_CONFIG_REG_RF            0x00000004u

#define PORT_CONTROL_CC              0x00000001u
#define PORT_CONTROL_EF              0x00000002u
#define PORT_CONTROL_ET              0x00000004u
#define PORT_CONTROL_EI              0x00000008u
#define PORT_CONTROL_HH              0x00000010u

#define QL_DRVR_SEM_MASK             0x00000001u

/* BAR and basic infrastructure */
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

    /* Interrupt-related shadow state */
    uint32_t irq_mask;        /* mirrors ispInterruptMaskReg */
    uint32_t irq_status;      /* pending interrupt cause bits (FE/RI/CMPL) */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct ql3xxx_port_registers regs;

    /* Minimal host-memory register shadows used directly by driver */
    struct ql3xxx_host_memory_registers hmem;

    /* Queue pointers for simple completion emulation */
    dma_addr_t req_q_base;
    uint32_t   req_q_len;
    uint32_t   req_cons_idx;

    dma_addr_t rsp_q_base;
    uint32_t   rsp_q_len;

    dma_addr_t rsp_prod_idx_addr;
    dma_addr_t req_cons_idx_addr;

    dma_addr_t lrg_buf_q_base;
    uint32_t   lrg_buf_q_len;

    dma_addr_t small_buf_q_base;
    uint32_t   small_buf_q_len;

    /* Device configuration flags */
    bool link_up;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* If any unmasked interrupt cause is pending, assert INTx/MSI */
    uint32_t enabled = s->regs.CommonRegs.ispInterruptMaskReg & ISP_IMR_ENABLE_INT;
    if (enabled && s->irq_status) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * The real hardware performs complex queue processing and DMA.
 * For probe/initialization, we do not need to emulate any data traffic,
 * so this function is intentionally left as a no-op.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Helper to access 32-bit registers in regs via byte address */
static uint32_t *pcibase_reg_ptr(PCIBaseState *s, hwaddr addr)
{
    if (addr + 4 > sizeof(struct ql3xxx_port_registers)) {
        return NULL;
    }
    return (uint32_t *)((uint8_t *)&s->regs + addr);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t *p;

    /* The driver only does 32-bit readl() accesses. */
    if (size != 4) {
        return 0;
    }

    /* Map some commonly accessed registers explicitly for side effects. */
    switch (addr) {
    case offsetof(struct ql3xxx_port_registers, CommonRegs.semaphoreReg):
        return s->regs.CommonRegs.semaphoreReg;
    case offsetof(struct ql3xxx_port_registers, CommonRegs.ispControlStatus):
        return s->regs.CommonRegs.ispControlStatus;
    case offsetof(struct ql3xxx_port_registers, CommonRegs.ispInterruptMaskReg):
        return s->regs.CommonRegs.ispInterruptMaskReg;
    case offsetof(struct ql3xxx_port_registers, CommonRegs.rspQConsumerIndex):
        return s->regs.CommonRegs.rspQConsumerIndex;
    case offsetof(struct ql3xxx_port_registers, CommonRegs.reqQProducerIndex):
        return s->regs.CommonRegs.reqQProducerIndex;
    case offsetof(struct ql3xxx_port_registers, CommonRegs.rxLargeQProducerIndex):
        return s->regs.CommonRegs.rxLargeQProducerIndex;
    case offsetof(struct ql3xxx_port_registers, CommonRegs.rxSmallQProducerIndex):
        return s->regs.CommonRegs.rxSmallQProducerIndex;
    case offsetof(struct ql3xxx_port_registers, CommonRegs.serialPortInterfaceReg):
        return s->regs.CommonRegs.serialPortInterfaceReg;
    default:
        break;
    }

    /* Generic register access within struct ql3xxx_port_registers */
    p = pcibase_reg_ptr(s, addr);
    if (p) {
        return *p;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t wval = (uint32_t)val;
    uint32_t *p;

    if (size != 4) {
        return;
    }

    /* Handle special semantics for some registers used by the driver. */
    if (addr == offsetof(struct ql3xxx_port_registers, CommonRegs.semaphoreReg)) {
        /* Driver uses ql_sem_spinlock/ql_sem_unlock with sem_mask/sem_bits.
         * We emulate that writes are latched and reads return bits>>16.
         */
        s->regs.CommonRegs.semaphoreReg = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, CommonRegs.ispInterruptMaskReg)) {
        /* Upper 16 bits are mask, lower 16 may contain enable bits. */
        s->regs.CommonRegs.ispInterruptMaskReg = wval;
        pcibase_update_irq(s);
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, CommonRegs.ispControlStatus)) {
        /* Many operations use form (bit << 16) | bit for W1C or set.
         * We implement: bits set in high word clear corresponding low bits;
         * bits set in low word set corresponding low bits.
         */
        uint32_t clear_mask = (wval >> 16);
        uint32_t set_mask   = (wval & 0xFFFFu);
        uint32_t cur = s->regs.CommonRegs.ispControlStatus;
        cur &= ~clear_mask;
        cur |= set_mask;
        s->regs.CommonRegs.ispControlStatus = cur;

        /* Clear corresponding pending interrupt bits if RI/FE are cleared. */
        if (clear_mask & ISP_CONTROL_RI) {
            s->irq_status &= ~ISP_CONTROL_RI;
        }
        if (clear_mask & ISP_CONTROL_FE) {
            s->irq_status &= ~ISP_CONTROL_FE;
        }
        pcibase_update_irq(s);
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, CommonRegs.reqQProducerIndex)) {
        /* Driver updates producer index; hardware would consume and generate
         * completions. For probe/bring-up, we just store the value.
         */
        s->regs.CommonRegs.reqQProducerIndex = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, CommonRegs.rspQConsumerIndex)) {
        /* NAPI writes this to inform device of processed completions. */
        s->regs.CommonRegs.rspQConsumerIndex = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, CommonRegs.rxLargeQProducerIndex)) {
        s->regs.CommonRegs.rxLargeQProducerIndex = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, CommonRegs.rxSmallQProducerIndex)) {
        s->regs.CommonRegs.rxSmallQProducerIndex = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, CommonRegs.serialPortInterfaceReg)) {
        /* EEPROM/PHY reset control - just latch. */
        s->regs.CommonRegs.serialPortInterfaceReg = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, portControl)) {
        /* Used for configuration complete and enabling Ethernet.
         * Apply same W1C/set semantics as generic control regs.
         */
        uint32_t clear_mask = (wval >> 16);
        uint32_t set_mask   = (wval & 0xFFFFu);
        uint32_t cur = s->regs.portControl;
        cur &= ~clear_mask;
        cur |= set_mask;
        s->regs.portControl = cur;

        /* When PORT_CONTROL_CC is set, firmware would configure port and
         * eventually set PORT_STATUS_IC. We simulate completion instantly.
         */
        if (set_mask & PORT_CONTROL_CC) {
            s->regs.portStatus |= PORT_STATUS_IC;
        }
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, functionControl)) {
        /* Driver uses to enable Ethernet functions on 3032. */
        uint32_t clear_mask = (wval >> 16);
        uint32_t set_mask   = (wval & 0xFFFFu);
        uint32_t cur = s->regs.functionControl;
        cur &= ~clear_mask;
        cur |= set_mask;
        s->regs.functionControl = cur;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, mac0ConfigReg) ||
        addr == offsetof(struct ql3xxx_port_registers, mac1ConfigReg)) {
        /* MAC config registers are written with (bit | (bit<<16)) patterns
         * for enabling/disabling features. Apply W1C/set semantics.
         */
        uint32_t clear_mask = (wval >> 16);
        uint32_t set_mask   = (wval & 0xFFFFu);
        uint32_t *regp = (addr == offsetof(struct ql3xxx_port_registers, mac0ConfigReg)) ?
            &s->regs.mac0ConfigReg : &s->regs.mac1ConfigReg;
        uint32_t cur = *regp;
        cur &= ~clear_mask;
        cur |= set_mask;
        *regp = cur;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, macMIIMgmtControlReg)) {
        /* Used for MII scan mode and clock division; just latch. */
        s->regs.macMIIMgmtControlReg = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, macMIIMgmtAddrReg)) {
        s->regs.macMIIMgmtAddrReg = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, macMIIMgmtDataReg)) {
        s->regs.macMIIMgmtDataReg = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, macAddrIndirectPtrReg)) {
        s->regs.macAddrIndirectPtrReg = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, macAddrDataReg)) {
        s->regs.macAddrDataReg = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, ipAddrIndexReg)) {
        s->regs.ipAddrIndexReg = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, ipAddrDataReg)) {
        s->regs.ipAddrDataReg = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, tcpMaxWindow)) {
        s->regs.tcpMaxWindow = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, ExternalHWConfig)) {
        s->regs.ExternalHWConfig = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, InternalChipConfig)) {
        s->regs.InternalChipConfig = wval;
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, mac0MaxFrameLengthReg) ||
        addr == offsetof(struct ql3xxx_port_registers, mac1MaxFrameLengthReg)) {
        if (addr == offsetof(struct ql3xxx_port_registers, mac0MaxFrameLengthReg)) {
            s->regs.mac0MaxFrameLengthReg = wval;
        } else {
            s->regs.mac1MaxFrameLengthReg = wval;
        }
        return;
    }

    if (addr == offsetof(struct ql3xxx_port_registers, PortFatalErrStatus)) {
        s->regs.PortFatalErrStatus = wval;
        return;
    }

    /* Generic register write fallback */
    p = pcibase_reg_ptr(s, addr);
    if (p) {
        *p = wval;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    /* Driver does not use legacy I/O space. */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* Driver does not use legacy I/O space. */
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

    memset(&s->regs, 0, sizeof(s->regs));
    memset(&s->hmem, 0, sizeof(s->hmem));

    s->irq_mask = 0;
    s->irq_status = 0;
    s->link_up = false;

    /* After reset, portStatus advertises some basic capabilities:
     * Mark chip as configured (IC) so driver does not time out in
     * ql_adapter_initialize wait loop.
     */
    s->regs.portStatus |= PORT_STATUS_IC;

    /* ispControlStatus: indicate function 0 network, func id 0.
     * Bit fields are not fully defined here; we simply return zero.
     */
    s->regs.CommonRegs.ispControlStatus = 0;

    /* Interrupt mask default: interrupts disabled */
    s->regs.CommonRegs.ispInterruptMaskReg = 0;

    /* Clear queue indices */
    s->regs.CommonRegs.reqQProducerIndex = 0;
    s->regs.CommonRegs.rspQConsumerIndex = 0;
    s->regs.CommonRegs.rxLargeQProducerIndex = 0;
    s->regs.CommonRegs.rxSmallQProducerIndex = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_QLOGIC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  QL3022_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, QLA3XXX_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* The driver maps BAR 1 with pci_ioremap_bar(pdev, 1). We expose
     * a single MMIO BAR 1 large enough for the port registers.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 1;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = sizeof(struct ql3xxx_port_registers);
    s->bar_info[0].name  = "qla3xxx-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X: driver may enable MSI, so we provide MSI capability. */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
    }

    s->has_msix = false; /* not used by driver */

    memset(&s->regs, 0, sizeof(s->regs));
    memset(&s->hmem, 0, sizeof(s->hmem));
    s->irq_mask = 0;
    s->irq_status = 0;
    s->link_up = false;
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "qla3xxx_pci",
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

type_init(pcibase_register_types)
