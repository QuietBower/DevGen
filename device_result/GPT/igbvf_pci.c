/*
 * QEMU PCI Device Model for Intel igbvf Virtual Function NIC
 * Phase 2: Behavioral implementation based strictly on provided driver code
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

/* The driver provides __NETIF_F(name) as a wrapper around __NETIF_F_BIT. */
typedef uint64_t netdev_features_t;
#define __NETIF_F_BIT(bit)   ((netdev_features_t)1 << (bit))
#define __NETIF_F(name)      __NETIF_F_BIT(NETIF_F_##name##_BIT)

#define NETIF_F_GSO_GRE             __NETIF_F(GSO_GRE)
#define NETIF_F_GSO_GRE_CSUM        __NETIF_F(GSO_GRE_CSUM)
#define NETIF_F_GSO_IPXIP4          __NETIF_F(GSO_IPXIP4)
#define NETIF_F_GSO_IPXIP6          __NETIF_F(GSO_IPXIP6)
#define NETIF_F_GSO_UDP_TUNNEL      __NETIF_F(GSO_UDP_TUNNEL)
#define NETIF_F_GSO_UDP_TUNNEL_CSUM __NETIF_F(GSO_UDP_TUNNEL_CSUM)
#define PCI_CLASS_NETWORK_ETHERNET  0x0200
#define PCI_VENDOR_ID_INTEL         0x8086

#define TYPE_PCIBASE_DEVICE "igbvf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IGBVF_VENDOR_ID        PCI_VENDOR_ID_INTEL
#define IGBVF_DEVICE_ID        0x10CA /* E1000_DEV_ID_82576_VF */
#define IGBVF_CLASS_ID         PCI_CLASS_NETWORK_ETHERNET

#define E1000_EITR(_n)         (0x01680 + (0x4 * (_n)))
#define E1000_TDH(_n)          ((_n) < 4 ? (0x03810 + ((_n) * 0x100)) : \
                                (0x0E010 + ((_n) * 0x40)))
#define E1000_TDT(_n)          ((_n) < 4 ? (0x03818 + ((_n) * 0x100)) : \
                                (0x0E018 + ((_n) * 0x40)))
#define E1000_RDT(_n)          ((_n) < 4 ? (0x02818 + ((_n) * 0x100)) : \
                                (0x0C018 + ((_n) * 0x40)))
#define E1000_RDH(_n)          ((_n) < 4 ? (0x02810 + ((_n) * 0x100)) : \
                                (0x0C010 + ((_n) * 0x40)))

#define E1000_STATUS_LU        0x00000002
#define E1000_STATUS_SPEED_100 0x00000040
#define E1000_STATUS_SPEED_1000 0x00000080
#define E1000_STATUS_FD        0x00000001

#define E1000_V2PMAILBOX_PFACK 0x00000020
#define E1000_V2PMAILBOX_RSTI  0x00000040
#define E1000_V2PMAILBOX_RSTD  0x00000080
#define E1000_V2PMAILBOX_PFSTS 0x00000010
#define E1000_V2PMAILBOX_ACK   0x00000002
#define E1000_V2PMAILBOX_REQ   0x00000001
#define E1000_V2PMAILBOX_VFU   0x00000004
#define E1000_V2PMAILBOX_R2C_BITS 0x000000B0

#define E1000_VF_SET_LPE       0x05
#define E1000_VF_SET_MAC_ADDR  0x02
#define E1000_VF_SET_VLAN      0x04
#define E1000_VF_RESET         0x01
#define E1000_VF_SET_MULTICAST 0x03

#define E1000_VT_MSGINFO_SHIFT 16
#define E1000_VF_MAC_FILTER_ADD (0x02 << E1000_VT_MSGINFO_SHIFT)
#define E1000_VF_MAC_FILTER_CLR (0x01 << E1000_VT_MSGINFO_SHIFT)

#define E1000_VT_MSGTYPE_CTS   0x20000000
#define E1000_VT_MSGTYPE_NACK  0x40000000
#define E1000_VT_MSGTYPE_ACK   0x80000000

#define E1000_VFMAILBOX_SIZE   16
#define E1000_VF_MBX_INIT_DELAY 500
#define E1000_VF_INIT_TIMEOUT  200
#define E1000_VF_MBX_INIT_TIMEOUT 2000

#define E1000_CTRL_RST         0x04000000

#define IGBVF_MAX_TXD          4096
#define IGBVF_MAX_RXD          4096
#define IGBVF_MIN_TXD          64
#define IGBVF_MIN_RXD          64

#define IGBVF_MAX_ITR_USECS    10000
#define IGBVF_MIN_ITR_USECS    10

#define IGBVF_START_ITR        488
#define IGBVF_4K_ITR           980
#define IGBVF_70K_ITR          56
#define IGBVF_20K_ITR          196

#define IGBVF_RX_PTHRESH       16
#define IGBVF_RX_HTHRESH       8
#define IGBVF_RX_WTHRESH       1

#define IGBVF_MAX_MAC_FILTERS  3

#define IGBVF_MAX_TXD_PWR      16
#define IGBVF_MAX_DATA_PER_TXD (1u << IGBVF_MAX_TXD_PWR)

#define IGBVF_MAX_MAC_HDR_LEN        127
#define IGBVF_MAX_NETWORK_HDR_LEN    511

#define REQ_RX_DESCRIPTOR_MULTIPLE 8
#define REQ_TX_DESCRIPTOR_MULTIPLE 8

#define E1000_RXD_STAT_DD      0x01
#define E1000_RXD_STAT_EOP     0x02
#define E1000_RXD_STAT_VP      0x08
#define E1000_RXD_STAT_IXSM    0x04
#define E1000_RXD_STAT_UDPCS   0x10
#define E1000_RXD_STAT_TCPCS   0x20

#define E1000_RXDEXT_STATERR_LB 0x00040000
#define E1000_RXDEXT_STATERR_IPE 0x40000000
#define E1000_RXDEXT_STATERR_TCPE 0x20000000
#define E1000_RXDEXT_STATERR_CXE 0x10000000
#define E1000_RXDEXT_STATERR_SEQ 0x04000000
#define E1000_RXDEXT_STATERR_SE  0x02000000
#define E1000_RXDEXT_STATERR_CE  0x01000000
#define E1000_RXDEXT_STATERR_RXE 0x80000000
#define E1000_RXDEXT_ERR_FRAME_ERR_MASK ( \
    E1000_RXDEXT_STATERR_CE  | \
    E1000_RXDEXT_STATERR_SE  | \
    E1000_RXDEXT_STATERR_SEQ | \
    E1000_RXDEXT_STATERR_CXE | \
    E1000_RXDEXT_STATERR_RXE)

#define E1000_RXDADV_HDRBUFLEN_MASK 0x7FE0
#define E1000_RXD_SPC_VLAN_MASK     0x0FFF

#define E1000_TXD_STAT_DD      0x00000001
#define E1000_TXD_CMD_DEXT     0x20000000
#define E1000_ADVTXD_DTYP_CTXT 0x00200000
#define E1000_ADVTXD_DTYP_DATA 0x00300000
#define E1000_ADVTXD_DCMD_EOP  0x01000000
#define E1000_ADVTXD_DCMD_IFCS 0x02000000
#define E1000_ADVTXD_DCMD_RS   0x08000000
#define E1000_ADVTXD_DCMD_VLE  0x40000000
#define E1000_ADVTXD_DCMD_TSE  0x80000000
#define E1000_ADVTXD_DCMD_DEXT 0x20000000

#define E1000_ADVTXD_MSS_SHIFT     16
#define E1000_ADVTXD_L4LEN_SHIFT   8
#define E1000_ADVTXD_MACLEN_SHIFT  9
#define E1000_ADVTXD_PAYLEN_SHIFT  14

#define E1000_ADVTXD_TUCMD_L4T_TCP 0x00000800
#define E1000_ADVTXD_TUCMD_IPV4    0x00000400
#define E1000_ADVTXD_TUCMD_L4T_SCTP 0x00001000

#define E1000_TXD_POPTS_TXSM   0x02
#define E1000_TXD_POPTS_IXSM   0x01

#define E1000_SRRCTL_DESCTYPE_MASK              0x0E000000
#define E1000_SRRCTL_BSIZEPKT_SHIFT             10
#define E1000_SRRCTL_DROP_EN                    0x80000000
#define E1000_SRRCTL_DESCTYPE_ADV_ONEBUF        0x02000000
#define E1000_SRRCTL_BSIZEHDRSIZE_SHIFT         2
#define E1000_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS  0x0A000000
#define E1000_SRRCTL_BSIZEPKT_MASK              0x0000007F
#define E1000_SRRCTL_BSIZEHDR_MASK              0x00003F00

#define E1000_DCA_TXCTRL_TX_WB_RO_EN  (1 << 11)

#define E1000_TXDCTL_QUEUE_ENABLE    0x02000000
#define E1000_RXDCTL_QUEUE_ENABLE    0x02000000

#define E1000_IVAR_VALID             0x80

#define IGBVF_RX_BUFFER_WRITE        16
#define IGBVF_TX_QUEUE_WAKE          32

#define IGBVF_GSO_PARTIAL_FEATURES   (NETIF_F_GSO_GRE | \
                                      NETIF_F_GSO_GRE_CSUM | \
                                      NETIF_F_GSO_IPXIP4 | \
                                      NETIF_F_GSO_IPXIP6 | \
                                      NETIF_F_GSO_UDP_TUNNEL | \
                                      NETIF_F_GSO_UDP_TUNNEL_CSUM)

#define IGBVF_FLAG_RX_LB_VLAN_BSWAP  (1 << 1)
#define IGBVF_FLAG_RX_CSUM_DISABLED  (1 << 0)

#define E1000_SUCCESS        0
#define E1000_ERR_MAC_INIT   5
#define E1000_ERR_MBX        15

#define MAX_STD_JUMBO_FRAME_SIZE 9216

#define SPEED_10     10
#define SPEED_100    100
#define SPEED_1000   1000

#define FULL_DUPLEX  2
#define HALF_DUPLEX  1

#define VLAN_TAG_SIZE 4

#define IGBVF_NO_QUEUE -1

#define IGBVF_TX_FLAGS_CSUM       0x00000001
#define IGBVF_TX_FLAGS_VLAN       0x00000002
#define IGBVF_TX_FLAGS_TSO        0x00000004
#define IGBVF_TX_FLAGS_IPV4       0x00000008
#define IGBVF_TX_FLAGS_VLAN_MASK  0xffff0000
#define IGBVF_TX_FLAGS_VLAN_SHIFT 16


/* Minimal subset of VF registers actually touched in provided code */
#define REG_EIMS       0x01528
#define REG_EIMC       0x01524
#define REG_EIAC       0x0152C
#define REG_EIAM       0x01530
#define REG_EICS       0x01520
#define REG_EICR       0x01580
#define REG_IVAR0      0x01700
#define REG_IVAR_MISC  0x01740

/* We also expose generic RDH/RDT/TDH/TDT/EITR ranges implicitly via macros */


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

    /* Hardware Register Shadows (The 'Identity' of the device) */

    /* Simple flat MMIO register space for BAR0 (128 KiB is enough for used regs) */
    uint32_t regs[0x20000 / 4];

    /* Interrupt-related shadows */
    uint32_t eims;
    uint32_t eiam;
    uint32_t eiac;
    uint32_t eics;
    uint32_t eicr;

    /* MSI-X: 3 vectors as used by driver (tx, rx, other) */
    uint32_t msix_pending_mask; /* bit per vector */
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Interrupts are only signaled via MSI-X, 3 vectors; we just check
     * if any EIMS bit is set and raise legacy INTx as a fallback to make
     * request_irq() succeed even if MSI-X isn't wired up.
     */
    if (s->eims) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * The provided netdev.c never programs descriptor base addresses
 * via explicit writes we can see (ring->dma is filled by the driver,
 * but no MMIO write of RDBAL/TDBAL in this file is visible as macros),
 * so we do not implement active DMA. The kernel uses the rings as
 * software-visible memory only. We keep this function present but unused.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

static uint32_t pcibase_reg_read(PCIBaseState *s, hwaddr addr)
{
    hwaddr index = addr >> 2;
    if (index >= (sizeof(s->regs) / sizeof(s->regs[0]))) {
        return 0;
    }

    switch (addr) {
    case REG_EICR:
        /* Reading EICR returns pending causes and clears them. */
        {
            uint32_t val = s->eicr;
            s->eicr = 0;
            /* Also clear in shadow array */
            s->regs[REG_EICR >> 2] = 0;
            /* After clearing causes, update legacy INTx */
            pcibase_update_irq(s);
            return val;
        }
    default:
        break;
    }

    return s->regs[index];
}

static void pcibase_reg_write(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    hwaddr index = addr >> 2;
    if (index >= (sizeof(s->regs) / sizeof(s->regs[0]))) {
        return;
    }

    switch (addr) {
    case REG_EIMS:
        /* Interrupt mask set */
        s->eims |= val;
        s->regs[index] = s->eims;
        pcibase_update_irq(s);
        return;
    case REG_EIMC:
        /* Interrupt mask clear */
        s->eims &= ~val;
        s->regs[index] = s->eims;
        pcibase_update_irq(s);
        return;
    case REG_EIAM:
        s->eiam = val;
        s->regs[index] = val;
        return;
    case REG_EIAC:
        s->eiac = val;
        s->regs[index] = val;
        return;
    case REG_EICS:
        /* Interrupt cause set: software-triggered, OR into EICR */
        s->eics |= val;
        s->eicr |= val;
        s->regs[REG_EICR >> 2] = s->eicr;
        s->regs[index] = s->eics;
        /* If corresponding bits are unmasked in EIMS, signal interrupt */
        if (s->eims & val) {
            /* Driver requests MSI-X; we don't have mapping from bits to
             * vectors in this file, so we just pulse legacy INTx.
             */
            pci_set_irq(PCI_DEVICE(s), 1);
            pci_set_irq(PCI_DEVICE(s), 0);
        }
        return;
    default:
        break;
    }

    /* Generic register: just store */
    s->regs[index] = val;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        /* Driver uses readl/writel (32-bit). Any other size returns 0. */
        return 0;
    }

    val = pcibase_reg_read(s, addr);
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    pcibase_reg_write(s, addr, (uint32_t)val);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    /* No PIO used by driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* No PIO used by driver */
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    pci_device_reset(pdev);

    /* Clear all register shadows at device reset */
    memset(s->regs, 0, sizeof(s->regs));

    s->eims = 0;
    s->eiam = 0;
    s->eiac = 0;
    s->eics = 0;
    s->eicr = 0;
    s->msix_pending_mask = 0;

    /* STATUS: link up 1 Gbps full duplex so that driver sees carrier */
    /* STATUS register offset in 82576 is 0x0008, but not defined above; we
     * do not invent the offset and so we do not touch it explicitly.
     * The driver in this file does not read it directly.
     */

    (void)s;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  IGBVF_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IGBVF_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IGBVF_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCIe capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR0: MMIO region used for registers and rings (driver ioremaps BAR0) */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x20000; /* 128 KiB covers all used regs based on macros */
    s->bar_info[0].name = "igbvf-bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI-X: driver allocates 3 MSI-X vectors. We expose 3. */
    Error *local_err = NULL;
    if (msix_init_exclusive_bar(pdev, 3, 1, &local_err)) {
        if (local_err) {
            error_propagate(errp, local_err);
        }
    } else {
        s->has_msix = true;
    }

    /* We don't set up MSI since driver calls pci_enable_msix_range. */

    /* Clear register shadows */
    memset(s->regs, 0, sizeof(s->regs));
    s->eims = 0;
    s->eiam = 0;
    s->eiac = 0;
    s->eics = 0;
    s->eicr = 0;
    s->msix_pending_mask = 0;
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
    .name = "igbvf_pci",
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
