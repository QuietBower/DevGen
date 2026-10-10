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

#define TYPE_PCIBASE_DEVICE "e1000e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

#define E1000_VENDOR_ID    0x8086
#define E1000_DEVICE_ID    0x105E
#define E1000_CLASS_ID     0x020000

#define BAR0_SIZE 0x20000

/* Register offsets from driver */
#define E1000_CTRL     0x00000
#define E1000_STATUS   0x00008
#define E1000_CTRL_EXT 0x00018
#define E1000_ICR      0x000C0
#define E1000_RCTL     0x00100
#define E1000_TCTL     0x00400
#define E1000_TIPG     0x00410
#define E1000_FCAL     0x00028
#define E1000_FCAH     0x0002C
#define E1000_FCT      0x00030
#define E1000_FCTTV    0x00170
#define E1000_TXCW     0x00178
#define E1000_RA       0x05400
#define E1000_VET      0x00038
#define E1000_MTA      0x05200
#define E1000_VFTA     0x05600
#define E1000_TDFH     0x03410
#define E1000_TDFT     0x03418
#define E1000_TDFHS    0x03420
#define E1000_TDFTS    0x03428
#define E1000_TDFPC    0x03430
#define E1000_RDFH     0x02410
#define E1000_RDFT     0x02418
#define E1000_RDFHS    0x02420
#define E1000_RDFTS    0x02428
#define E1000_RDFPC    0x02430
#define E1000_TIDV     0x03820
#define E1000_TADV     0x0382C
#define E1000_RDTR     0x02820
#define E1000_ERT      0x02008
#define E1000_HOST_IF  0x08800
#define E1000_EERD     0x00014
#define E1000_MDIC     0x00020
#define E1000_PBA      0x01000
#define E1000_ICS      0x000C8
#define E1000_IMS      0x000D0
#define E1000_IMC      0x000D8
#define E1000_IAM      0x000E0
#define E1000_EIAC     0x0152C

/* Bit masks and constants (selected from driver) */
#define E1000_STATUS_LU           0x00000002
#define E1000_STATUS_TXOFF        0x00000010
#define E1000_STATUS_FD           0x00000001
#define E1000_STATUS_SPEED_100    0x00000040
#define E1000_STATUS_SPEED_1000   0x00000080
#define E1000_STATUS_GIO_MASTER_ENABLE 0x00080000
#define E1000_STATUS_PCIM_STATE   0x40000000

#define E1000_ICR_TXDW            0x00000001
#define E1000_ICR_LSC             0x00000004
#define E1000_ICR_RXSEQ           0x00000008
#define E1000_ICR_RXDMT0          0x00000010
#define E1000_ICR_RXO             0x00000040
#define E1000_ICR_RXT0            0x00000080
#define E1000_ICR_MDAC            0x00000200
#define E1000_ICR_SRPD            0x00010000
#define E1000_ICR_ACK             0x00020000
#define E1000_ICR_MNG             0x00040000
#define E1000_ICR_TXQ0            0x00400000
#define E1000_ICR_RXQ0            0x00100000
#define E1000_ICR_ECCER           0x00400000
#define E1000_ICR_INT_ASSERTED    0x80000000
#define E1000_ICR_OTHER           0x01000000

#define E1000_CTRL_FD             0x00000001
#define E1000_CTRL_GIO_MASTER_DISABLE 0x00000004
#define E1000_CTRL_SLU            0x00000040
#define E1000_CTRL_ILOS           0x00000080
#define E1000_CTRL_FRCSPD         0x00000800
#define E1000_CTRL_FRCDPX         0x00001000
#define E1000_CTRL_LANPHYPC_OVERRIDE 0x00010000
#define E1000_CTRL_LANPHYPC_VALUE    0x00020000
#define E1000_CTRL_RFCE           0x08000000
#define E1000_CTRL_TFCE           0x10000000
#define E1000_CTRL_VME            0x40000000
#define E1000_CTRL_PHY_RST        0x80000000
#define E1000_CTRL_SPD_SEL        0x00000300
#define E1000_CTRL_SPD_100        0x00000100
#define E1000_CTRL_SPD_1000       0x00000200
#define E1000_CTRL_EN_PHY_PWR_MGMT 0x00200000
#define E1000_CTRL_ADVD3WUC       0x00100000

#define E1000_CTRL_EXT_IAME            0x08000000
#define E1000_CTRL_EXT_SDP3_DATA       0x00000080
#define E1000_CTRL_EXT_DRV_LOAD        0x10000000
#define E1000_CTRL_EXT_EIAME           0x01000000
#define E1000_CTRL_EXT_PBA_CLR         0x80000000
#define E1000_CTRL_EXT_FORCE_SMBUS     0x00000800
#define E1000_CTRL_EXT_DMA_DYN_CLK_EN  0x00080000
#define E1000_CTRL_EXT_LPCD            0x00000004
#define E1000_CTRL_EXT_SPD_BYPS        0x00008000
#define E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES 0x00C00000

#define E1000_RCTL_EN             0x00000002
#define E1000_RCTL_SBP            0x00000004
#define E1000_RCTL_UPE            0x00000008
#define E1000_RCTL_MPE            0x00000010
#define E1000_RCTL_LPE            0x00000020
#define E1000_RCTL_LBM_NO         0x00000000
#define E1000_RCTL_LBM_MAC        0x00000040
#define E1000_RCTL_LBM_TCVR       0x000000C0
#define E1000_RCTL_DTYP_PS        0x00000400
#define E1000_RCTL_BAM            0x00008000
#define E1000_RCTL_VFE            0x00040000
#define E1000_RCTL_CFIEN          0x00080000
#define E1000_RCTL_DPF            0x00400000
#define E1000_RCTL_PMCF           0x00800000
#define E1000_RCTL_BSEX           0x02000000
#define E1000_RCTL_SECRC          0x04000000
#define E1000_RCTL_SZ_2048        0x00000000
#define E1000_RCTL_SZ_4096        0x00030000
#define E1000_RCTL_SZ_8192        0x00020000
#define E1000_RCTL_SZ_16384       0x00010000
#define E1000_RCTL_MO_SHIFT       12
#define E1000_RCTL_MO_3           0x00003000
#define E1000_RCTL_RDMTS_HALF     0x00000000
#define E1000_RCTL_RDMTS_HEX      0x00010000

#define E1000_TCTL_EN             0x00000002
#define E1000_TCTL_PSP            0x00000008
#define E1000_TCTL_CT             0x00000ff0
#define E1000_TCTL_RTLC           0x01000000
#define E1000_TCTL_MULR           0x10000000

#define E1000_TXDCTL_PTHRESH      0x0000003F
#define E1000_TXDCTL_HTHRESH      0x00003F00
#define E1000_TXDCTL_WTHRESH      0x003F0000
#define E1000_TXDCTL_GRAN         0x01000000
#define E1000_TXDCTL_COUNT_DESC   0x00400000
#define E1000_TXDCTL_DMA_BURST_ENABLE \
    (E1000_TXDCTL_GRAN | \
     E1000_TXDCTL_COUNT_DESC | \
     (1u << 16) | \
     (1u << 8) | \
     0x1f)

#define E1000_RXDCTL_DMA_BURST_ENABLE \
    (0x01000000 | \
     (4u << 16) | \
     (4u << 8) | \
     0x20)

#define E1000_TXD_STAT_DD         0x00000001
#define E1000_TXD_CMD_EOP         0x01000000
#define E1000_TXD_CMD_IFCS        0x02000000
#define E1000_TXD_CMD_RS          0x08000000
#define E1000_TXD_CMD_IDE         0x80000000
#define E1000_TXD_CMD_DEXT        0x20000000
#define E1000_TXD_CMD_VLE         0x40000000
#define E1000_TXD_CMD_IP          0x02000000
#define E1000_TXD_CMD_TCP         0x01000000
#define E1000_TXD_CMD_TSE         0x04000000

#define E1000_RXD_STAT_DD         0x01
#define E1000_RXD_STAT_EOP        0x02
#define E1000_RXD_STAT_IXSM       0x04
#define E1000_RXD_STAT_TCPCS      0x20
#define E1000_RXD_STAT_UDPCS      0x10
#define E1000_RXD_STAT_VP         0x08
#define E1000_RXD_ERR_IPE         0x40
#define E1000_RXD_ERR_TCPE        0x20

#define E1000_RXDEXT_STATERR_CE   0x01000000
#define E1000_RXDEXT_STATERR_SE   0x02000000
#define E1000_RXDEXT_STATERR_SEQ  0x04000000
#define E1000_RXDEXT_STATERR_CXE  0x10000000
#define E1000_RXDEXT_STATERR_RXE  0x80000000
#define E1000_RXDEXT_STATERR_TST  0x00000100

#define E1000_RAH_AV              0x80000000

#define E1000_MTA_MAX             128

#define MAX_PS_BUFFERS 4
#define PS_PAGE_BUFFERS  (MAX_PS_BUFFERS - 1)

/* Descriptor structures from driver */
typedef struct {
    uint64_t buffer_addr;
    union {
        uint32_t data;
        struct {
            uint16_t length;
            uint8_t cso;
            uint8_t cmd;
        } flags;
    } lower;
    union {
        uint32_t data;
        struct {
            uint8_t status;
            uint8_t css;
            uint16_t special;
        } fields;
    } upper;
} e1000_tx_desc;

typedef union {
    struct {
        uint64_t buffer_addr[MAX_PS_BUFFERS];
    } read;
    struct {
        struct {
            uint32_t mrq;
            union {
                uint32_t rss;
                struct {
                    uint16_t ip_id;
                    uint16_t csum;
                } csum_ip;
            } hi_dword;
        } lower;
        struct {
            uint32_t status_error;
            uint16_t length0;
            uint16_t vlan;
        } middle;
        struct {
            uint16_t header_status;
            uint16_t length[PS_PAGE_BUFFERS];
        } upper;
        uint64_t reserved;
    } wb;
} e1000_rx_desc_packet_split;

typedef union {
    struct {
        uint64_t buffer_addr;
        uint64_t reserved;
    } read;
    struct {
        struct {
            uint32_t mrq;
            union {
                uint32_t rss;
                struct {
                    uint16_t ip_id;
                    uint16_t csum;
                } csum_ip;
            } hi_dword;
        } lower;
        struct {
            uint32_t status_error;
            uint16_t length;
            uint16_t vlan;
        } upper;
    } wb;
} e1000_rx_desc_extended;

/* Enumerations from supplementary driver source */
enum e1000_mac_type {
    e1000_82571,
    e1000_82572,
    e1000_82573,
    e1000_82574,
    e1000_82583,
    e1000_80003es2lan,
    e1000_ich8lan,
    e1000_ich9lan,
    e1000_ich10lan,
    e1000_pchlan,
    e1000_pch2lan,
    e1000_pch_lpt,
    e1000_pch_spt,
    e1000_pch_cnp,
    e1000_pch_tgp,
    e1000_pch_adp,
    e1000_pch_mtp,
    e1000_pch_lnp,
    e1000_pch_ptp,
    e1000_pch_nvp,
};

enum e1000_media_type {
    e1000_media_type_unknown = 0,
    e1000_media_type_copper = 1,
    e1000_media_type_fiber = 2,
    e1000_media_type_internal_serdes = 3,
    e1000_num_media_types
};

enum e1000_nvm_type {
    e1000_nvm_unknown = 0,
    e1000_nvm_none,
    e1000_nvm_eeprom_spi,
    e1000_nvm_flash_hw,
    e1000_nvm_flash_sw
};

enum e1000_phy_type {
    e1000_phy_unknown = 0,
    e1000_phy_none,
    e1000_phy_m88,
    e1000_phy_igp,
    e1000_phy_igp_2,
    e1000_phy_gg82563,
    e1000_phy_igp_3,
    e1000_phy_ife,
    e1000_phy_bm,
    e1000_phy_82578,
    e1000_phy_82577,
    e1000_phy_82579,
    e1000_phy_i217,
};

enum e1000_bus_width {
    e1000_bus_width_unknown = 0,
    e1000_bus_width_pcie_x1,
    e1000_bus_width_pcie_x2,
    e1000_bus_width_pcie_x4 = 4,
    e1000_bus_width_pcie_x8 = 8,
    e1000_bus_width_32,
    e1000_bus_width_64,
    e1000_bus_width_reserved
};

enum e1000_fc_mode {
    e1000_fc_none = 0,
    e1000_fc_rx_pause,
    e1000_fc_tx_pause,
    e1000_fc_full,
    e1000_fc_default = 0xFF
};

enum e1000_ms_type {
    e1000_ms_hw_default = 0,
    e1000_ms_force_master,
    e1000_ms_force_slave,
    e1000_ms_auto
};

enum e1000_smart_speed {
    e1000_smart_speed_default = 0,
    e1000_smart_speed_on,
    e1000_smart_speed_off
};

enum e1000_ulp_state {
    e1000_ulp_state_unknown,
    e1000_ulp_state_off,
    e1000_ulp_state_on,
};

enum e1000_rev_polarity {
    e1000_rev_polarity_normal = 0,
    e1000_rev_polarity_reversed,
    e1000_rev_polarity_undefined = 0xFF
};

enum e1000_serdes_link_state {
    e1000_serdes_link_down = 0,
    e1000_serdes_link_autoneg_progress,
    e1000_serdes_link_autoneg_complete,
    e1000_serdes_link_forced_up
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
    uint8_t regs[BAR0_SIZE];

    /* Interrupt state */
    uint32_t icr_bits;
    uint32_t ims_bits;

    /* EEPROM emulation */
    uint16_t eeprom[64];

    /* PHY registers */
    uint16_t phy_regs[32];

    /* MAC address */
    uint8_t mac[6];
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->icr_bits & s->ims_bits;

    if (msix_enabled(pdev)) {
        if (pending & E1000_ICR_RXQ0) {
            msix_notify(pdev, 0);
        }
        if (pending & E1000_ICR_TXQ0) {
            msix_notify(pdev, 1);
        }
        if (pending & E1000_ICR_OTHER) {
            msix_notify(pdev, 2);
        }
    } else if (msi_enabled(pdev)) {
        if (pending) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, pending ? 1 : 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE) {
        return ~0ULL;
    }

    switch (addr) {
        case E1000_ICR:
            val = s->icr_bits;
            s->icr_bits = 0;
            pcibase_update_irq(s);
            break;
        case E1000_STATUS:
            val = E1000_STATUS_LU | E1000_STATUS_FD | E1000_STATUS_SPEED_1000 |
                  E1000_STATUS_GIO_MASTER_ENABLE | E1000_STATUS_PCIM_STATE;
            break;
        default:
            memcpy(&val, &s->regs[addr], sizeof(val));
            break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_SIZE) {
        return;
    }

    switch (addr) {
        case E1000_ICS:
            s->icr_bits |= (val & 0xFFFFFFFF);
            pcibase_update_irq(s);
            break;
        case E1000_IMS:
            s->ims_bits |= val;
            pcibase_update_irq(s);
            break;
        case E1000_IMC:
            s->ims_bits &= ~val;
            pcibase_update_irq(s);
            break;
        case E1000_EERD:
            if (val & 1) {  /* start */
                uint32_t addr_eep = (val >> 2) & 0xFFF;
                if (addr_eep < 64) {
                    uint16_t data = s->eeprom[addr_eep];
                    stl_le_p(&s->regs[addr], (data << 16) | (1 << 1));
                } else {
                    stl_le_p(&s->regs[addr], 1 << 1);
                }
            } else {
                stl_le_p(&s->regs[addr], val);
            }
            break;
        case E1000_MDIC:
            if (val & BIT(19)) {  /* start */
                uint32_t reg_addr = (val >> 26) & 0x1F;
                bool read_op = val & BIT(18);
                if (read_op) {
                    uint16_t data = s->phy_regs[reg_addr];
                    stl_le_p(&s->regs[addr], (data & 0xFFFF) | BIT(17) | BIT(18));
                } else {
                    uint16_t data = val & 0xFFFF;
                    s->phy_regs[reg_addr] = data;
                    stl_le_p(&s->regs[addr], BIT(17));
                }
            } else {
                stl_le_p(&s->regs[addr], val);
            }
            break;
        default:
            memcpy(&s->regs[addr], &val, sizeof(val));
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

    memset(s->regs, 0, BAR0_SIZE);
    s->icr_bits = 0;
    s->ims_bits = 0;

    /* Set MAC in RAR0 */
    uint8_t mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
    memcpy(s->mac, mac, 6);
    stl_le_p(&s->regs[E1000_RA], mac[0] | (mac[1] << 8) | (mac[2] << 16) | (mac[3] << 24));
    stl_le_p(&s->regs[E1000_RA + 4], mac[4] | (mac[5] << 8) | E1000_RAH_AV);

    /* Status register defaults */
    stl_le_p(&s->regs[E1000_STATUS],
             E1000_STATUS_LU | E1000_STATUS_FD | E1000_STATUS_SPEED_1000 |
             E1000_STATUS_GIO_MASTER_ENABLE | E1000_STATUS_PCIM_STATE);

    /* Initialize EEPROM with valid checksum */
    memset(s->eeprom, 0, sizeof(s->eeprom));
    s->eeprom[0x00] = 0x0000; /* example data */
    s->eeprom[0x01] = 0x0000;
    s->eeprom[0x02] = 0x0000;
    /* compute checksum for word 0x3F */
    uint16_t sum = 0;
    for (int i = 0; i < 63; i++) {
        sum += s->eeprom[i];
    }
    s->eeprom[63] = 0xBABA - sum;  /* make total = 0xBABA */

    /* PHY registers initialization */
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    /* PHY ID for i217 */
    s->phy_regs[2] = 0x02E0;
    s->phy_regs[3] = 0x0001;
    /* BMCR: auto-negotiation enabled, 1000Mbps, full duplex */
    s->phy_regs[0] = 0x0140;  /* typical value */
    /* BMSR: link up, AN complete, 10/100/1000 capable */
    s->phy_regs[1] = 0x002D;

    /* Clear interrupts */
    pci_set_irq(PCI_DEVICE(dev), 0);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x105E );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x020000 );
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
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "e1000e-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt setup: try MSI-X first, then MSI */
    if (msix_init(pdev, 3, &s->bar_regions[0], 0, 0x2000, &s->bar_regions[0], 0, 0x3000, 0x70, errp) < 0) {
        if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
            error_setg(errp, "Can't init MSI/MSI-X");
            return;
        }
    }

    /* Final state initialization */
    pcibase_reset(DEVICE(pdev));
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

static const VMStateDescription vmstate_pcibase = {
    .name = "e1000e_pci",
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
