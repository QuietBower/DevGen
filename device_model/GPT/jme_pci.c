/*
 * QEMU PCI device model for JMicron JME NIC (behavioral model)
 * Auto-generated from Linux driver information.
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
#include <stdint.h>

#define TYPE_PCIBASE_DEVICE "jme_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define JME_VENDOR_ID 0x197b
#define JME_DEVICE_ID 0x0250
#define JME_CLASS_ID  0x0200

#define JME_REG_LEN 0x500

#define RX_RING_NR 4
#define TX_RING_NR 8
#define RX_DESC_SIZE 16
#define TX_DESC_SIZE 16
#define RING_DESC_ALIGN 16

#define JME_EEPROM_MAGIC 0x250

#define JME_PHY_REG_NR 32
#define JME_SMB_LEN 256

/* DMA-related macros referenced */
#define RX_RING_ALLOC_SIZE(s) ((s * RX_DESC_SIZE) + RING_DESC_ALIGN)
#define TX_RING_ALLOC_SIZE(s) ((s * TX_DESC_SIZE) + RING_DESC_ALIGN)

/* Timeout / wait constants */
#define JME_WAIT_LINK_TIME 2000
#define JME_PHY_TIMEOUT 100
#define JME_EEPROM_RELOAD_TIMEOUT 2000
#define JME_SPDRSV_TIMEOUT 500
#define JME_SMB_BUSY_TIMEOUT 20
#define JME_TX_DISABLE_TIMEOUT 10
#define JME_RX_DISABLE_TIMEOUT 10

/* Misc constants from driver */
#define MAX_ETHERNET_JUMBO_PACKET_SIZE 9216
#define RX_PREPAD_SIZE 10
#define ETH_CRC_LEN 2
#define RX_VLANHDR_LEN 2
#define RX_BUF_DMA_ALIGN 8

#define PCI_DCSR_MRRS 0x59
#define PCI_DCSR_MRRS_MASK 0x70

#define PCI_DEVICE_ID_JMICRON_JMC250 0x0250
#define PCI_DEVICE_ID_JMICRON_JMC260 0x0260

#define DRV_NAME "jme"
#define DRV_VERSION "1.0.8"

#define __NETIF_MSG_BIT(bit) ((uint32_t)1 << (bit))
#define NETIF_MSG_PROBE     __NETIF_MSG_BIT(PROBE)
#define NETIF_MSG_LINK      __NETIF_MSG_BIT(LINK)
#define NETIF_MSG_RX_ERR    __NETIF_MSG_BIT(RX_ERR)
#define NETIF_MSG_TX_ERR    __NETIF_MSG_BIT(TX_ERR)
#define NETIF_MSG_HW        __NETIF_MSG_BIT(HW)

#define JME_DEF_MSG_ENABLE \
    (NETIF_MSG_PROBE | \
     NETIF_MSG_LINK | \
     NETIF_MSG_RX_ERR | \
     NETIF_MSG_TX_ERR | \
     NETIF_MSG_HW)

/* PHY specific constants from snippet */
#define JM_PHY_SPEC_ADDR_REG            0x1E
#define JM_PHY_SPEC_DATA_REG            0x1F
#define JM_PHY_SPEC_REG_READ            0x00004000
#define JM_PHY_SPEC_REG_WRITE           0x00008000
#define JM_PHY_EXT_COMM_2_REG           0x32
#define JM_PHY_EXT_COMM_1_REG           0x31
#define JM_PHY_EXT_COMM_0_REG           0x30
#define PHY_GAD_TEST_MODE_1             0x00002000
#define PHY_GAD_TEST_MODE_MSK           0x0000E000
#define JM_PHY_EXT_COMM_2_CALI_MODE_0   0x02
#define JM_PHY_EXT_COMM_2_CALI_ENABLE   0x01
#define JM_PHY_EXT_COMM_2_CALI_LATCH    0x10

/* PCI private config offsets from snippet */
#define PCI_PRIV_PE1        0xE4
#define PCI_PRIV_SHARE_NICCTRL 0xF5

/* Wake-on-LAN related constants */
#define WAKEUP_FRAME_MASK_DWNR 4
#define WAKEUP_FRAME_NR 8

/* PCC / dynamic interrupt coalescing constants */
#define PCC_TX_TO 1000
#define PCC_TX_CNT 8
#define PCC_P2_THRESHOLD 800
#define PCC_INTR_THRESHOLD 800
#define PCC_P3_THRESHOLD (2 * 1024 * 1024)
#define PCC_INTERVAL_US 100000

#define APMC_PHP_SHUTDOWN_DELAY (10 * 1000 * 1000)

/* Power management flag from snippet */
#define JME_FLAG_PHYEA_ENABLE 0x2

/* Status-related macros referenced */
#define BMSR_ANCOMP 0x0020

/* Interrupt-related registers (names from driver) */
#define JME_IENS      0x000  /* Interrupt enable set */
#define JME_IENC      0x004  /* Interrupt enable clear */
#define JME_IEVE      0x008  /* Interrupt events (status/W1C) */

/* Misc registers used via jread32/jwrite32 in the driver */
#define JME_SMI       0x020
#define JME_WFOI      0x024
#define JME_WFODP     0x028
#define JME_RXDBA_LO  0x02C
#define JME_RXDBA_HI  0x030
#define JME_RXQDC     0x034
#define JME_RXNDA     0x038
#define JME_TXDBA_LO  0x03C
#define JME_TXDBA_HI  0x040
#define JME_TXQDC     0x044
#define JME_TXNDA     0x048
#define JME_RXMCHT_LO 0x04C
#define JME_RXMCHT_HI 0x050
#define JME_GPREG0    0x054
#define JME_GPREG1    0x058
#define JME_RXUMA_LO  0x05C
#define JME_RXUMA_HI  0x060
#define JME_PCCRX0    0x064
#define JME_PCCTX     0x068
#define JME_TXCS      0x06C
#define JME_RXCS      0x070
#define JME_PHY_LINK  0x074
#define JME_TXMCS     0x078
#define JME_TXTRHD    0x07C
#define JME_TMCSR     0x080
#define JME_TIMER2    0x084
#define JME_APMC      0x088
#define JME_SMBCSR    0x08C
#define JME_SMBINTF   0x090
#define JME_GHC       0x094
#define JME_PMCS      0x098
#define JME_PHY_PWR   0x09C
#define JME_CHIPMODE  0x0A0

#define INTR_ENABLE   0x00000001u

/* Minimal stand-ins for kernel types used only in context here */
typedef uint64_t dma_addr_t;

typedef struct sk_buff {
    void *dummy;
} sk_buff;

struct jme_buffer_info {
    struct sk_buff *skb;
    dma_addr_t mapping;
    int len;
    int nr_desc;
    unsigned long start_xmit;
};

struct jme_ring {
    void *alloc;
    void *desc;
    dma_addr_t dmaalloc;
    dma_addr_t dma;
    struct jme_buffer_info *bufinf;
    int next_to_use;
    int next_to_clean;
    int nr_free;
};

struct rxdesc {
    union {
        uint8_t    all[16];
        uint32_t  dw[4];
        struct {
            uint16_t  rsv2;
            uint8_t    rsv1;
            uint8_t    flags;
            uint16_t  datalen;
            uint16_t  wbcpl;
            uint32_t  bufaddrh;
            uint32_t  bufaddrl;
        } desc1;
        struct {
            uint16_t  vlan;
            uint16_t  flags;
            uint16_t  framesize;
            uint8_t    errstat;
            uint8_t    desccnt;
            uint32_t  rsshash;
            uint8_t    hashfun;
            uint8_t    hashtype;
            uint16_t  resrv;
        } descwb;
    };
};

struct txdesc {
    union {
        uint8_t    all[16];
        uint32_t  dw[4];
        struct {
            uint16_t  vlan;
            uint8_t    rsv1;
            uint8_t    flags;
            uint16_t  datalen;
            uint16_t  mss;
            uint16_t  pktsize;
            uint16_t  rsv2;
            uint32_t  bufaddr;
        } desc1;
        struct {
            uint16_t  rsv1;
            uint8_t    rsv2;
            uint8_t    flags;
            uint16_t  datalen;
            uint16_t  rsv3;
            uint32_t  bufaddrh;
            uint32_t  bufaddrl;
        } desc2;
        struct {
            uint8_t    ehdrsz;
            uint8_t    rsv1;
            uint8_t    rsv2;
            uint8_t    flags;
            uint16_t  trycnt;
            uint16_t  segcnt;
            uint16_t  pktsz;
            uint16_t  rsv3;
            uint32_t  bufaddrl;
        } descwb;
    };
};

struct dynpcc_info {
    unsigned long last_bytes;
    unsigned long last_pkts;
    unsigned long intr_cnt;
    unsigned char cur;
    unsigned char attempt;
    unsigned char cnt;
};

#define DECLARE_NAPI_STRUCT struct { int dummy; } napi;
#define DECLARE_NET_DEVICE_STATS

struct pci_dev;
struct net_device;
struct mii_if_info;
struct tasklet_struct;
struct work_struct;

struct jme_adapter {
    struct pci_dev          *pdev;
    struct net_device       *dev;
    void                    *regs;
    struct mii_if_info      *mii_if;
    struct jme_ring         rxring[RX_RING_NR];
    struct jme_ring         txring[TX_RING_NR];
    unsigned long           phy_lock;
    unsigned long           macaddr_lock;
    unsigned long           rxmcs_lock;
    struct tasklet_struct   *rxempty_task;
    struct tasklet_struct   *rxclean_task;
    struct tasklet_struct   *txclean_task;
    struct work_struct      *linkch_task;
    struct tasklet_struct   *pcc_task;
    unsigned long           flags;
    uint32_t                reg_txcs;
    uint32_t                reg_txpfc;
    uint32_t                reg_rxcs;
    uint32_t                reg_rxmcs;
    uint32_t                reg_ghc;
    uint32_t                reg_pmcs;
    uint32_t                reg_gpreg1;
    uint32_t                phylink;
    uint32_t                tx_ring_size;
    uint32_t                tx_ring_mask;
    uint32_t                tx_wake_threshold;
    uint32_t                rx_ring_size;
    uint32_t                rx_ring_mask;
    uint8_t                 mrrs;
    unsigned int            fpgaver;
    uint8_t                 chiprev;
    uint8_t                 chip_main_rev;
    uint8_t                 chip_sub_rev;
    uint8_t                 pcirev;
    uint32_t                msg_enable;
    unsigned int            old_mtu;
    struct dynpcc_info      dpi;
    int                     intr_sem;
    int                     link_changing;
    int                     tx_cleaning;
    int                     rx_cleaning;
    int                     rx_empty;
    int                     (*jme_rx)(struct sk_buff *skb);
    DECLARE_NAPI_STRUCT
    DECLARE_NET_DEVICE_STATS
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
    uint32_t regs[JME_REG_LEN / sizeof(uint32_t)];

    unsigned int status_flags;
};


static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if ((s->intr_status & s->intr_mask) & INTR_ENABLE) {
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


static uint32_t jme_reg_index(hwaddr addr)
{
    return (uint32_t)(addr >> 2);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > JME_REG_LEN) {
        return 0;
    }

    /* Provide simple, consistent behaviour for jread32() semantics */
    switch (size) {
    case 1: {
        uint32_t idx = jme_reg_index(addr & ~3ULL);
        uint32_t shift = (addr & 3) * 8;
        uint32_t r = s->regs[idx];
        val = (r >> shift) & 0xffu;
        break;
    }
    case 2: {
        uint32_t idx = jme_reg_index(addr & ~3ULL);
        uint32_t shift = (addr & 2) * 8;
        uint32_t r = s->regs[idx];
        val = (r >> shift) & 0xffffu;
        break;
    }
    case 4: {
        uint32_t idx = jme_reg_index(addr);
        val = s->regs[idx];
        break;
    }
    case 8: {
        uint32_t idx = jme_reg_index(addr);
        uint64_t lo = s->regs[idx];
        uint64_t hi = 0;
        if (idx + 1 < (JME_REG_LEN / 4)) {
            hi = s->regs[idx + 1];
        }
        val = lo | (hi << 32);
        break;
    }
    default:
        return 0;
    }

    /* JME_IEVE is the interrupt status/event register, model it explicitly */
    if (addr == JME_IEVE && size == 4) {
        uint32_t st = s->intr_status;
        if (s->intr_mask & INTR_ENABLE) {
            st |= INTR_ENABLE;
        }
        val = st;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > JME_REG_LEN) {
        return;
    }

    /* Basic write behaviour backing jwrite32/jwrite32f helpers */
    switch (size) {
    case 1: {
        uint32_t idx = jme_reg_index(addr & ~3ULL);
        uint32_t shift = (addr & 3) * 8;
        uint32_t mask = 0xffu << shift;
        uint32_t old = s->regs[idx];
        uint32_t nv = (old & ~mask) | (((uint32_t)val & 0xffu) << shift);
        s->regs[idx] = nv;
        break;
    }
    case 2: {
        uint32_t idx = jme_reg_index(addr & ~3ULL);
        uint32_t shift = (addr & 2) * 8;
        uint32_t mask = 0xffffu << shift;
        uint32_t old = s->regs[idx];
        uint32_t nv = (old & ~mask) | (((uint32_t)val & 0xffffu) << shift);
        s->regs[idx] = nv;
        break;
    }
    case 4: {
        uint32_t idx = jme_reg_index(addr);
        s->regs[idx] = (uint32_t)val;
        break;
    }
    case 8: {
        uint32_t idx = jme_reg_index(addr);
        if (idx < (JME_REG_LEN / 4)) {
            s->regs[idx] = (uint32_t)val;
        }
        if (idx + 1 < (JME_REG_LEN / 4)) {
            s->regs[idx + 1] = (uint32_t)(val >> 32);
        }
        break;
    }
    default:
        return;
    }

    /* Interrupt mask and status handling follow driver semantics:
     *
     *  - JME_IENS: interrupt enable set (OR into mask)
     *  - JME_IENC: interrupt enable clear (AND with ~value)
     *  - JME_IEVE: interrupt event/status, write-one-to-clear
     */

    if (addr == JME_IENS && size == 4) {
        s->intr_mask |= (uint32_t)val;
        pcibase_update_irq(s);
        return;
    }

    if (addr == JME_IENC && size == 4) {
        s->intr_mask &= ~((uint32_t)val);
        pcibase_update_irq(s);
        return;
    }

    if (addr == JME_IEVE && size == 4) {
        uint32_t clr = (uint32_t)val;
        s->intr_status &= ~clr;
        pcibase_update_irq(s);
        return;
    }

    /* Other MMIO registers (e.g. JME_PCCRX0, JME_GPREG1, JME_GHC, JME_WFOI,
     * JME_WFODP, etc.) are modeled as simple read/write storage in regs[].
     * This is sufficient for jwrite32/jwrite32f/jread32 usage patterns
     * demonstrated in the driver snippets, where the driver only relies on
     * value preservation and read-after-write flush, which is naturally
     * provided by this MMIO implementation.
     */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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
    s->status_flags = 0;

    pcibase_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  JME_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  JME_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, JME_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = JME_REG_LEN;
    s->bar_info[0].name = "jme-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    s->has_msix = false;
    if (s->has_msi) {
        Error *local_err = NULL;
        if (msi_init(pdev, 0, 1, true, false, &local_err)) {
            error_propagate(errp, local_err);
            return;
        }
    }

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status_flags = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "jme_pci",
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
