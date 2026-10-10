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
#include "hw/pci/pci_device.h"

#define TYPE_PCIBASE_DEVICE "igbvf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define E1000_DEV_ID_82576_VF 0x10CA
#define E1000_DEV_ID_I350_VF 0x1520

#define E1000_CTRL     0x00000
#define E1000_STATUS   0x00008
#define E1000_V2PMAILBOX(_n)	(0x00C40 + (4 * (_n)))

#define E1000_EITR(_n) (0x01680 + (0x4 * (_n)))
#define E1000_TDT(_n)  ((_n) < 4 ? (0x03818 + ((_n) * 0x100)) : (0x0E018 + ((_n) * 0x40)))
#define E1000_TDH(_n)  ((_n) < 4 ? (0x03810 + ((_n) * 0x100)) : (0x0E010 + ((_n) * 0x40)))
#define E1000_RDT(_n)  ((_n) < 4 ? (0x02818 + ((_n) * 0x100)) : (0x0C018 + ((_n) * 0x40)))
#define E1000_RDH(_n)  ((_n) < 4 ? (0x02810 + ((_n) * 0x100)) : (0x0C010 + ((_n) * 0x40)))
#define E1000_CTRL_RST 0x04000000
#define E1000_VFMAILBOX_SIZE 16

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
    uint32_t eims_enable_mask;
    uint32_t eims_other;
    uint32_t eims_value;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t status;
    uint32_t ctrl;
    uint32_t v2p_mailbox[E1000_VFMAILBOX_SIZE];
    uint32_t eitr[4];
    uint32_t tdt[4];
    uint32_t tdh[4];
    uint32_t rdt[4];
    uint32_t rdh[4];
    uint32_t eimc;
    uint32_t eiac;

    /* DMA Context */
    dma_addr_t tx_ring_dma;
    dma_addr_t rx_ring_dma;

    uint32_t pba;
    bool reset_pending;
    
    uint8_t mac_addr[6];
};

union e1000_adv_rx_desc {
    struct {
        uint64_t pkt_addr; /* Packet buffer address */
        uint64_t hdr_addr; /* Header buffer address */
    } read;
    struct {
        struct {
            union {
                uint32_t data;
                struct {
                    uint16_t pkt_info; /* RSS/Packet type */
                    /* Split Header, hdr buffer length */
                    uint16_t hdr_info;
                } hs_rss;
            } lo_dword;
            union {
                uint32_t rss; /* RSS Hash */
                struct {
                    uint16_t ip_id; /* IP id */
                    uint16_t csum;  /* Packet Checksum */
                } csum_ip;
            } hi_dword;
        } lower;
        struct {
            uint32_t status_error; /* ext status/error */
            uint16_t length; /* Packet length */
            uint16_t vlan; /* VLAN tag */
        } upper;
    } wb;  /* writeback */
};

union e1000_adv_tx_desc {
    struct {
        uint64_t buffer_addr; /* Address of descriptor's data buf */
        uint32_t cmd_type_len;
        uint32_t olinfo_status;
    } read;
    struct {
        uint64_t rsvd; /* Reserved */
        uint32_t nxtseq_seed;
        uint32_t status;
    } wb;
};

struct e1000_adv_tx_context_desc {
    uint32_t vlan_macip_lens;
    uint32_t seqnum_seed;
    uint32_t type_tucmd_mlhl;
    uint32_t mss_l4len_idx;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->has_msix) {
        msix_notify(pdev, 0);
    } else if (s->has_msi) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA logic requires TDBAL/TDBAH and RDBAL/RDBAH which are currently missing */
}

static void pcibase_reset(DeviceState *dev);

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case E1000_CTRL:
        val = s->ctrl;
        break;
    case E1000_STATUS:
        val = s->status;
        break;
    default:
        if (addr >= E1000_V2PMAILBOX(0) && addr <= E1000_V2PMAILBOX(E1000_VFMAILBOX_SIZE - 1)) {
            int n = (addr - E1000_V2PMAILBOX(0)) / 4;
            val = s->v2p_mailbox[n];
        } else if (addr >= E1000_EITR(0) && addr <= E1000_EITR(3)) {
            int n = (addr - E1000_EITR(0)) / 4;
            val = s->eitr[n];
        } else {
            for (int i = 0; i < 4; i++) {
                if (addr == E1000_TDT(i)) { val = s->tdt[i]; break; }
                if (addr == E1000_TDH(i)) { val = s->tdh[i]; break; }
                if (addr == E1000_RDT(i)) { val = s->rdt[i]; break; }
                if (addr == E1000_RDH(i)) { val = s->rdh[i]; break; }
            }
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case E1000_CTRL:
        if (val & E1000_CTRL_RST) {
            pcibase_reset(DEVICE(s));
            val &= ~E1000_CTRL_RST;
        }
        s->ctrl = val;
        break;
    case E1000_STATUS:
        s->status = val;
        break;
    default:
        if (addr >= E1000_V2PMAILBOX(0) && addr <= E1000_V2PMAILBOX(E1000_VFMAILBOX_SIZE - 1)) {
            int n = (addr - E1000_V2PMAILBOX(0)) / 4;
            s->v2p_mailbox[n] = val;
        } else if (addr >= E1000_EITR(0) && addr <= E1000_EITR(3)) {
            int n = (addr - E1000_EITR(0)) / 4;
            s->eitr[n] = val;
        } else {
            for (int i = 0; i < 4; i++) {
                if (addr == E1000_TDT(i)) { 
                    s->tdt[i] = val; 
                    pcibase_do_dma(s, true);
                    break; 
                }
                if (addr == E1000_TDH(i)) { s->tdh[i] = val; break; }
                if (addr == E1000_RDT(i)) { 
                    s->rdt[i] = val; 
                    pcibase_do_dma(s, false);
                    break; 
                }
                if (addr == E1000_RDH(i)) { s->rdh[i] = val; break; }
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    return val;
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

    s->ctrl = 0;
    s->status = 0;
    memset(s->v2p_mailbox, 0, sizeof(s->v2p_mailbox));
    memset(s->eitr, 0, sizeof(s->eitr));
    memset(s->tdt, 0, sizeof(s->tdt));
    memset(s->tdh, 0, sizeof(s->tdh));
    memset(s->rdt, 0, sizeof(s->rdt));
    memset(s->rdh, 0, sizeof(s->rdh));
    s->eimc = 0;
    s->eiac = 0;
    s->eims_enable_mask = 0;
    s->eims_other = 0;
    s->eims_value = 0;
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  E1000_DEV_ID_82576_VF );
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
    s->bar_info[0].size = 0x4000; /* Placeholder size */
    s->bar_info[0].name = "igbvf-mmio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* msi_init or msix_init calls */
    if (msix_init_exclusive_bar(pdev, 3, 3, errp) == 0) {
        s->has_msix = true;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit_exclusive_bar(pdev);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
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
