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


#define TYPE_PCIBASE_DEVICE "ixgbevf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IXGBE_DEV_ID_82599_VF 0x10ED
#define IXGBE_VF_MAX_RX_QUEUES 8
#define IXGBE_VF_MAX_TX_QUEUES 8

#define IXGBE_VFSTATUS        0x00008
#define IXGBE_VTIVAR_MISC     0x00140
#define IXGBE_VTIVAR(x)       (0x00120 + (4 * (x)))
#define IXGBE_VFTDH(x)        (0x02010 + (0x40 * (x)))
#define IXGBE_VFTDT(x)        (0x02018 + (0x40 * (x)))
#define IXGBE_VTEIMS          0x00108
#define IXGBE_VTEITR(x)       (0x00820 + (4 * (x)))
#define IXGBE_VTEIMC          0x0010C
#define IXGBE_VTEIAC          0x00110
#define IXGBE_VTEIAM          0x00114
#define IXGBE_VFTDWBAH(x)     (0x0203C + (0x40 * (x)))
#define IXGBE_VFTDBAH(x)      (0x02004 + (0x40 * (x)))
#define IXGBE_VFTXDCTL(x)     (0x02028 + (0x40 * (x)))
#define IXGBE_VFTDLEN(x)      (0x02008 + (0x40 * (x)))
#define IXGBE_VFDCA_TXCTRL(x) (0x0200c + (0x40 * (x)))
#define IXGBE_VFTDWBAL(x)     (0x02038 + (0x40 * (x)))
#define IXGBE_VFTDBAL(x)      (0x02000 + (0x40 * (x)))
#define IXGBE_VFSRRCTL(x)     (0x01014 + (0x40 * (x)))
#define IXGBE_VFPSRTYPE       0x00300
#define IXGBE_VFRXDCTL(x)     (0x01028 + (0x40 * (x)))
#define IXGBE_VFRSSRK(x)      (0x3100 + ((x) * 4))
#define IXGBE_VFRETA(x)       (0x3200 + ((x) * 4))
#define IXGBE_VFMRQC          0x3000
#define IXGBE_VFRDLEN(x)      (0x01008 + (0x40 * (x)))
#define IXGBE_VFRDH(x)        (0x01010 + (0x40 * (x)))
#define IXGBE_VFRDBAL(x)      (0x01000 + (0x40 * (x)))
#define IXGBE_VFDCA_RXCTRL(x) (0x0100C + (0x40 * (x)))
#define IXGBE_VFRDBAH(x)      (0x01004 + (0x40 * (x)))
#define IXGBE_VFRDT(x)        (0x01018 + (0x40 * (x)))
#define IXGBE_VFGPTC          0x0201C
#define IXGBE_VFGORC_LSB      0x01020
#define IXGBE_VFGOTC_LSB      0x02020
#define IXGBE_VFMPRC          0x01034
#define IXGBE_VFGOTC_MSB      0x02024
#define IXGBE_VFGORC_MSB      0x01024
#define IXGBE_VFGPRC          0x0101C
#define IXGBE_VTEICR          0x00100
#define IXGBE_VTEICS          0x00104
#define IXGBE_VFCTRL          0x00000
#define IXGBE_VFRXMEMWRAP     0x03190
#define IXGBE_VFFRTIMER       0x00048
#define IXGBE_VFLINKS         0x00010

#define IXGBE_TXD_STAT_DD     0x00000001
#define IXGBE_RXD_STAT_DD     0x01
#define IXGBE_RXD_STAT_EOP    0x02

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
    uint32_t vteicr;
    uint32_t vteics;
    uint32_t vteims;
    uint32_t vteimc;
    uint32_t vteiac;
    uint32_t vteiam;
    uint32_t vteitr[IXGBE_VF_MAX_RX_QUEUES];
    uint32_t vtivar[IXGBE_VF_MAX_RX_QUEUES];
    uint32_t vtivar_misc;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t vfctrl;
    uint32_t vfstatus;
    uint32_t vflinks;
    uint32_t vffrtimer;
    uint32_t vfrxmemwrap;
    uint32_t vfmrqc;
    uint32_t vfpsrtype;

    /* DMA Context */
    uint32_t vftdbal[IXGBE_VF_MAX_TX_QUEUES];
    uint32_t vftdbah[IXGBE_VF_MAX_TX_QUEUES];
    uint32_t vftdlen[IXGBE_VF_MAX_TX_QUEUES];
    uint32_t vftdh[IXGBE_VF_MAX_TX_QUEUES];
    uint32_t vftdt[IXGBE_VF_MAX_TX_QUEUES];
    uint32_t vftxdctl[IXGBE_VF_MAX_TX_QUEUES];
    uint32_t vftdwbal[IXGBE_VF_MAX_TX_QUEUES];
    uint32_t vftdwbah[IXGBE_VF_MAX_TX_QUEUES];
    uint32_t vfdca_txctrl[IXGBE_VF_MAX_TX_QUEUES];
    uint32_t vfrdbal[IXGBE_VF_MAX_RX_QUEUES];
    uint32_t vfrdbah[IXGBE_VF_MAX_RX_QUEUES];
    uint32_t vfrdlen[IXGBE_VF_MAX_RX_QUEUES];
    uint32_t vfrdh[IXGBE_VF_MAX_RX_QUEUES];
    uint32_t vfrdt[IXGBE_VF_MAX_RX_QUEUES];
    uint32_t vfrxdctl[IXGBE_VF_MAX_RX_QUEUES];
    uint32_t vfsrrctl[IXGBE_VF_MAX_RX_QUEUES];
    uint32_t vfdca_rxctrl[IXGBE_VF_MAX_RX_QUEUES];

    uint32_t vfgprc;
    uint32_t vfgptc;
    uint32_t vfgorc_lsb;
    uint32_t vfgorc_msb;
    uint32_t vfgotc_lsb;
    uint32_t vfgotc_msb;
    uint32_t vfmprc;
    
    
    uint32_t vfrssrk[10];
    uint32_t vfreta[64];
};

union ixgbe_adv_tx_desc {
    struct {
        uint64_t buffer_addr;
        uint32_t cmd_type_len;
        uint32_t olinfo_status;
    } read;
    struct {
        uint64_t rsvd;
        uint32_t nxtseq_seed;
        uint32_t status;
    } wb;
};

union ixgbe_adv_rx_desc {
    struct {
        uint64_t pkt_addr;
        uint64_t hdr_addr;
    } read;
    struct {
        struct {
            union {
                uint32_t data;
                struct {
                    uint16_t pkt_info;
                    uint16_t hdr_info;
                } hs_rss;
            } lo_dword;
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
};

struct ixgbe_adv_tx_context_desc {
    uint32_t vlan_macip_lens;
    uint32_t fceof_saidx;
    uint32_t type_tucmd_mlhl;
    uint32_t mss_l4len_idx;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->vteicr & s->vteims;

    if (pending) {
        if (msix_enabled(pdev)) {
            for (int i = 0; i < 32; i++) {
                if (pending & (1 << i)) {
                    msix_notify(pdev, i);
                }
            }
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (is_write) {
        for (int i = 0; i < IXGBE_VF_MAX_TX_QUEUES; i++) {
            if (s->vftdt[i] != s->vftdh[i]) {
                uint64_t base = ((uint64_t)s->vftdbah[i] << 32) | s->vftdbal[i];
                uint32_t head = s->vftdh[i];
                uint32_t tail = s->vftdt[i];
                uint32_t len = s->vftdlen[i];
                
                if (len == 0) continue;
                
                while (head != tail) {
                    union ixgbe_adv_tx_desc desc;
                    pci_dma_read(pdev, base + head * sizeof(desc), &desc, sizeof(desc));
                    
                    /* Simulate DMA completion */
                    desc.wb.status |= cpu_to_le32(IXGBE_TXD_STAT_DD);
                    pci_dma_write(pdev, base + head * sizeof(desc), &desc, sizeof(desc));
                    
                    head = (head + 1) % (len / sizeof(desc));
                }
                s->vftdh[i] = head;
                
                /* Trigger interrupt */
                s->vteicr |= s->vteims; 
                pcibase_update_irq(s);
            }
        }
    } else {
        for (int i = 0; i < IXGBE_VF_MAX_RX_QUEUES; i++) {
            if (s->vfrdt[i] != s->vfrdh[i]) {
                uint64_t base = ((uint64_t)s->vfrdbah[i] << 32) | s->vfrdbal[i];
                uint32_t head = s->vfrdh[i];
                uint32_t tail = s->vfrdt[i];
                uint32_t len = s->vfrdlen[i];
                
                if (len == 0) continue;
                
                while (head != tail) {
                    union ixgbe_adv_rx_desc desc;
                    pci_dma_read(pdev, base + head * sizeof(desc), &desc, sizeof(desc));
                    
                    /* Simulate DMA completion */
                    desc.wb.upper.status_error |= cpu_to_le32(IXGBE_RXD_STAT_DD | IXGBE_RXD_STAT_EOP);
                    desc.wb.upper.length = cpu_to_le16(64); /* Dummy length */
                    pci_dma_write(pdev, base + head * sizeof(desc), &desc, sizeof(desc));
                    
                    head = (head + 1) % (len / sizeof(desc));
                }
                s->vfrdh[i] = head;
                
                /* Trigger interrupt */
                s->vteicr |= s->vteims;
                pcibase_update_irq(s);
            }
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case IXGBE_VFSTATUS: val = s->vfstatus; break;
    case IXGBE_VTIVAR_MISC: val = s->vtivar_misc; break;
    case IXGBE_VTEIMS: val = s->vteims; break;
    case IXGBE_VTEIMC: val = s->vteimc; break;
    case IXGBE_VTEIAC: val = s->vteiac; break;
    case IXGBE_VTEIAM: val = s->vteiam; break;
    case IXGBE_VFPSRTYPE: val = s->vfpsrtype; break;
    case IXGBE_VFMRQC: val = s->vfmrqc; break;
    case IXGBE_VFGPTC: val = s->vfgptc; break;
    case IXGBE_VFGORC_LSB: val = s->vfgorc_lsb; break;
    case IXGBE_VFGOTC_LSB: val = s->vfgotc_lsb; break;
    case IXGBE_VFMPRC: val = s->vfmprc; break;
    case IXGBE_VFGOTC_MSB: val = s->vfgotc_msb; break;
    case IXGBE_VFGORC_MSB: val = s->vfgorc_msb; break;
    case IXGBE_VFGPRC: val = s->vfgprc; break;
    case IXGBE_VTEICR: 
        val = s->vteicr; 
        s->vteicr = 0; 
        pcibase_update_irq(s); 
        break;
    case IXGBE_VTEICS: val = s->vteics; break;
    case IXGBE_VFCTRL: val = s->vfctrl; break;
    case IXGBE_VFRXMEMWRAP: val = s->vfrxmemwrap; break;
    case IXGBE_VFFRTIMER: val = s->vffrtimer; break;
    case IXGBE_VFLINKS: val = s->vflinks; break;
    default:
        if (addr >= IXGBE_VTIVAR(0) && addr <= IXGBE_VTIVAR(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            val = s->vtivar[(addr - IXGBE_VTIVAR(0)) / 4];
        } else if (addr >= IXGBE_VFTDH(0) && addr <= IXGBE_VFTDH(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            val = s->vftdh[(addr - IXGBE_VFTDH(0)) / 0x40];
        } else if (addr >= IXGBE_VFTDT(0) && addr <= IXGBE_VFTDT(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            val = s->vftdt[(addr - IXGBE_VFTDT(0)) / 0x40];
        } else if (addr >= IXGBE_VTEITR(0) && addr <= IXGBE_VTEITR(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            val = s->vteitr[(addr - IXGBE_VTEITR(0)) / 4];
        } else if (addr >= IXGBE_VFTDWBAH(0) && addr <= IXGBE_VFTDWBAH(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            val = s->vftdwbah[(addr - IXGBE_VFTDWBAH(0)) / 0x40];
        } else if (addr >= IXGBE_VFTDBAH(0) && addr <= IXGBE_VFTDBAH(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            val = s->vftdbah[(addr - IXGBE_VFTDBAH(0)) / 0x40];
        } else if (addr >= IXGBE_VFTXDCTL(0) && addr <= IXGBE_VFTXDCTL(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            val = s->vftxdctl[(addr - IXGBE_VFTXDCTL(0)) / 0x40];
        } else if (addr >= IXGBE_VFTDLEN(0) && addr <= IXGBE_VFTDLEN(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            val = s->vftdlen[(addr - IXGBE_VFTDLEN(0)) / 0x40];
        } else if (addr >= IXGBE_VFDCA_TXCTRL(0) && addr <= IXGBE_VFDCA_TXCTRL(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            val = s->vfdca_txctrl[(addr - IXGBE_VFDCA_TXCTRL(0)) / 0x40];
        } else if (addr >= IXGBE_VFTDWBAL(0) && addr <= IXGBE_VFTDWBAL(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            val = s->vftdwbal[(addr - IXGBE_VFTDWBAL(0)) / 0x40];
        } else if (addr >= IXGBE_VFTDBAL(0) && addr <= IXGBE_VFTDBAL(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            val = s->vftdbal[(addr - IXGBE_VFTDBAL(0)) / 0x40];
        } else if (addr >= IXGBE_VFSRRCTL(0) && addr <= IXGBE_VFSRRCTL(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            val = s->vfsrrctl[(addr - IXGBE_VFSRRCTL(0)) / 0x40];
        } else if (addr >= IXGBE_VFRXDCTL(0) && addr <= IXGBE_VFRXDCTL(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            val = s->vfrxdctl[(addr - IXGBE_VFRXDCTL(0)) / 0x40];
        } else if (addr >= IXGBE_VFRSSRK(0) && addr <= IXGBE_VFRSSRK(9)) {
            val = s->vfrssrk[(addr - IXGBE_VFRSSRK(0)) / 4];
        } else if (addr >= IXGBE_VFRETA(0) && addr <= IXGBE_VFRETA(63)) {
            val = s->vfreta[(addr - IXGBE_VFRETA(0)) / 4];
        } else if (addr >= IXGBE_VFRDLEN(0) && addr <= IXGBE_VFRDLEN(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            val = s->vfrdlen[(addr - IXGBE_VFRDLEN(0)) / 0x40];
        } else if (addr >= IXGBE_VFRDH(0) && addr <= IXGBE_VFRDH(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            val = s->vfrdh[(addr - IXGBE_VFRDH(0)) / 0x40];
        } else if (addr >= IXGBE_VFRDBAL(0) && addr <= IXGBE_VFRDBAL(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            val = s->vfrdbal[(addr - IXGBE_VFRDBAL(0)) / 0x40];
        } else if (addr >= IXGBE_VFDCA_RXCTRL(0) && addr <= IXGBE_VFDCA_RXCTRL(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            val = s->vfdca_rxctrl[(addr - IXGBE_VFDCA_RXCTRL(0)) / 0x40];
        } else if (addr >= IXGBE_VFRDBAH(0) && addr <= IXGBE_VFRDBAH(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            val = s->vfrdbah[(addr - IXGBE_VFRDBAH(0)) / 0x40];
        } else if (addr >= IXGBE_VFRDT(0) && addr <= IXGBE_VFRDT(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            val = s->vfrdt[(addr - IXGBE_VFRDT(0)) / 0x40];
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case IXGBE_VFSTATUS: s->vfstatus = val; break;
    case IXGBE_VTIVAR_MISC: s->vtivar_misc = val; break;
    case IXGBE_VTEIMS: 
        s->vteims |= val; 
        pcibase_update_irq(s); 
        break;
    case IXGBE_VTEIMC: 
        s->vteims &= ~val; 
        pcibase_update_irq(s); 
        break;
    case IXGBE_VTEIAC: s->vteiac = val; break;
    case IXGBE_VTEIAM: s->vteiam = val; break;
    case IXGBE_VFPSRTYPE: s->vfpsrtype = val; break;
    case IXGBE_VFMRQC: s->vfmrqc = val; break;
    case IXGBE_VTEICR: 
        s->vteicr &= ~val; 
        pcibase_update_irq(s); 
        break;
    case IXGBE_VTEICS: 
        s->vteicr |= val; 
        pcibase_update_irq(s); 
        break;
    case IXGBE_VFCTRL: s->vfctrl = val; break;
    default:
        if (addr >= IXGBE_VTIVAR(0) && addr <= IXGBE_VTIVAR(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            s->vtivar[(addr - IXGBE_VTIVAR(0)) / 4] = val;
        } else if (addr >= IXGBE_VFTDH(0) && addr <= IXGBE_VFTDH(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            s->vftdh[(addr - IXGBE_VFTDH(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFTDT(0) && addr <= IXGBE_VFTDT(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            s->vftdt[(addr - IXGBE_VFTDT(0)) / 0x40] = val;
            pcibase_do_dma(s, true);
        } else if (addr >= IXGBE_VTEITR(0) && addr <= IXGBE_VTEITR(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            s->vteitr[(addr - IXGBE_VTEITR(0)) / 4] = val;
        } else if (addr >= IXGBE_VFTDWBAH(0) && addr <= IXGBE_VFTDWBAH(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            s->vftdwbah[(addr - IXGBE_VFTDWBAH(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFTDBAH(0) && addr <= IXGBE_VFTDBAH(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            s->vftdbah[(addr - IXGBE_VFTDBAH(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFTXDCTL(0) && addr <= IXGBE_VFTXDCTL(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            s->vftxdctl[(addr - IXGBE_VFTXDCTL(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFTDLEN(0) && addr <= IXGBE_VFTDLEN(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            s->vftdlen[(addr - IXGBE_VFTDLEN(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFDCA_TXCTRL(0) && addr <= IXGBE_VFDCA_TXCTRL(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            s->vfdca_txctrl[(addr - IXGBE_VFDCA_TXCTRL(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFTDWBAL(0) && addr <= IXGBE_VFTDWBAL(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            s->vftdwbal[(addr - IXGBE_VFTDWBAL(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFTDBAL(0) && addr <= IXGBE_VFTDBAL(IXGBE_VF_MAX_TX_QUEUES - 1)) {
            s->vftdbal[(addr - IXGBE_VFTDBAL(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFSRRCTL(0) && addr <= IXGBE_VFSRRCTL(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            s->vfsrrctl[(addr - IXGBE_VFSRRCTL(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFRXDCTL(0) && addr <= IXGBE_VFRXDCTL(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            s->vfrxdctl[(addr - IXGBE_VFRXDCTL(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFRSSRK(0) && addr <= IXGBE_VFRSSRK(9)) {
            s->vfrssrk[(addr - IXGBE_VFRSSRK(0)) / 4] = val;
        } else if (addr >= IXGBE_VFRETA(0) && addr <= IXGBE_VFRETA(63)) {
            s->vfreta[(addr - IXGBE_VFRETA(0)) / 4] = val;
        } else if (addr >= IXGBE_VFRDLEN(0) && addr <= IXGBE_VFRDLEN(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            s->vfrdlen[(addr - IXGBE_VFRDLEN(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFRDH(0) && addr <= IXGBE_VFRDH(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            s->vfrdh[(addr - IXGBE_VFRDH(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFRDBAL(0) && addr <= IXGBE_VFRDBAL(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            s->vfrdbal[(addr - IXGBE_VFRDBAL(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFDCA_RXCTRL(0) && addr <= IXGBE_VFDCA_RXCTRL(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            s->vfdca_rxctrl[(addr - IXGBE_VFDCA_RXCTRL(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFRDBAH(0) && addr <= IXGBE_VFRDBAH(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            s->vfrdbah[(addr - IXGBE_VFRDBAH(0)) / 0x40] = val;
        } else if (addr >= IXGBE_VFRDT(0) && addr <= IXGBE_VFRDT(IXGBE_VF_MAX_RX_QUEUES - 1)) {
            s->vfrdt[(addr - IXGBE_VFRDT(0)) / 0x40] = val;
            pcibase_do_dma(s, false);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    
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

    s->vfstatus = 0;
    s->vtivar_misc = 0;
    s->vteims = 0;
    s->vteimc = 0;
    s->vteiac = 0;
    s->vteiam = 0;
    s->vfpsrtype = 0;
    s->vfmrqc = 0;
    s->vteicr = 0;
    s->vteics = 0;
    s->vfctrl = 0;
    s->vfrxmemwrap = 0;
    s->vffrtimer = 0;
    s->vflinks = 0;
    
    for (int i = 0; i < IXGBE_VF_MAX_RX_QUEUES; i++) {
        s->vtivar[i] = 0;
        s->vteitr[i] = 0;
        s->vfsrrctl[i] = 0;
        s->vfrxdctl[i] = 0;
        s->vfrdlen[i] = 0;
        s->vfrdh[i] = 0;
        s->vfrdbal[i] = 0;
        s->vfdca_rxctrl[i] = 0;
        s->vfrdbah[i] = 0;
        s->vfrdt[i] = 0;
    }
    
    for (int i = 0; i < IXGBE_VF_MAX_TX_QUEUES; i++) {
        s->vftdh[i] = 0;
        s->vftdt[i] = 0;
        s->vftdwbah[i] = 0;
        s->vftdbah[i] = 0;
        s->vftxdctl[i] = 0;
        s->vftdlen[i] = 0;
        s->vfdca_txctrl[i] = 0;
        s->vftdwbal[i] = 0;
        s->vftdbal[i] = 0;
    }
    
    for (int i = 0; i < 10; i++) s->vfrssrk[i] = 0;
    for (int i = 0; i < 64; i++) s->vfreta[i] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IXGBE_DEV_ID_82599_VF );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "ixgbevf-bar0";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;
    
    if (msix_init(pdev, 32, &s->bar_regions[0], 0, 0x3800, &s->bar_regions[0], 0, 0x3A00, 0, errp)) {
        return;
    }
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ixgbevf_pci",
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
