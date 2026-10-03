/*
 * QEMU model for Marvell OcteonTX2 RVU NIC PF
 * Generated from Linux driver otx2_pf.c
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
#include "qemu/typedefs.h"

#define TYPE_PCIBASE_DEVICE "rvu_nicpf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define VENDOR_ID 0x177D   /* PCI_VENDOR_ID_CAVIUM */
#define DEVICE_ID 0xA063   /* PCI_DEVID_OCTEONTX2_RVU_PF */
#define CLASS_ID  0x0200   /* PCI_CLASS_NETWORK_ETHERNET */

/* BIT_ULL helper */
#ifndef BIT_ULL
#define BIT_ULL(nr) (1ULL << (nr))
#endif

/* New definitions from supplementary source */
#define OTX2_ALIGN              128
#define VLAN_ETH_HLEN           18
#define VLAN_HLEN               4
#define OTX2_MBOX_RSP_SIG       0xbeef

/* GENMASK_ULL helper */
#ifndef GENMASK_ULL
#define GENMASK_ULL(h, l)       (((~0ULL) >> (63 - (h) + (l))) << (l))
#endif

/* Register Offsets and Constants extracted from Linux driver */
#define DRV_NAME    "rvu_nicpf"
#define DRV_STRING  "Marvell RVU NIC Physical Function Driver"
#define PCI_CFG_REG_BAR_NUM                     2
#define PCI_MBOX_BAR_NUM                        4

/* Block address shifts and masks */
#define RVU_FUNC_BLKADDR_SHIFT      20
#define RVU_FUNC_BLKADDR_MASK       0x1FULL

/* Block types */
#define BLKADDR_RVUM             0
#define BLKTYPE_NIX              1
#define BLKTYPE_NPA              2
#define BLKTYPE_CPT              3
#define BLKADDR_NPA              0
#define BLKADDR_CPT0             0

/* RVU_PF registers */
#define RVU_PF_BLOCK_ADDRX_DISC(a)          (0x200 | (a) << 3)
#define RVU_PF_INT                          (0xc20)
#define RVU_PF_INT_ENA_W1S                  (0xc30)
#define RVU_PF_INT_ENA_W1C                  (0xc38)
#define RVU_PF_PFAF_MBOX0                   (0xC00)
#define RVU_PF_VF_BAR4_ADDR                 (0x10)

/* NIX block registers */
#define NIX_LFBASE          (BLKTYPE_NIX << RVU_FUNC_BLKADDR_SHIFT)
#define NIX_LF_CQ_OP_INT        (NIX_LFBASE | 0xa00)
#define NIX_LF_SQ_OP_INT        (NIX_LFBASE | 0xa20)
#define NIX_LF_CINTX_INT(a)     (NIX_LFBASE | 0xD20 | (a) << 12)
#define NIX_LF_QINTX_ENA_W1S(a) (NIX_LFBASE | 0xC20 | (a) << 12)

/* NPA block registers */
#define NPA_LFBASE          (BLKTYPE_NPA << RVU_FUNC_BLKADDR_SHIFT)

/* Mailbox constants */
#define MBOX_UP_MSG              2
#define MBOX_DOWN_MSG            1
#define MBOX_SIZE                0x1000
#define MBOX_MSG_ALIGN           16
#define OTX2_MBOX_REQ_SIG        0xdead
#define OTX2_MBOX_VERSION        0x000a

/* Interrupt vector indices (from driver) */
#define RVU_PF_INT_VEC_AFPF_MBOX  0

/* Updated from supplementary source: MAX_RVU_BLKLF_CNT = 256 */
#define MAX_RVU_BLKLF_CNT 256

/* Mailbox header structure */
struct mbox_hdr {
    uint64_t msg_size;
    uint16_t num_msgs;
    uint16_t opt_msg;
    uint8_t  sig;
} __attribute__((packed));

typedef struct mbox_msghdr {
    uint16_t pcifunc;
    uint16_t id;
    uint16_t sig;
    uint16_t ver;
    uint16_t next_msgoff;
    int32_t  rc;
} __attribute__((packed)) MboxMsgHdr;

/* Response structures */
struct msix_offset_rsp {
    MboxMsgHdr hdr;
    uint16_t  npa_msixoff;
    uint16_t  nix_msixoff;
    uint16_t  sso;
    uint16_t  ssow;
    uint16_t  timlfs;
    uint16_t  cptlfs;
    uint16_t  sso_msixoff[MAX_RVU_BLKLF_CNT];
    uint16_t  ssow_msixoff[MAX_RVU_BLKLF_CNT];
    uint16_t  timlf_msixoff[MAX_RVU_BLKLF_CNT];
    uint16_t  cptlf_msixoff[MAX_RVU_BLKLF_CNT];
    uint16_t  cpt1_lfs;
    uint16_t  ree0_lfs;
    uint16_t  ree1_lfs;
    uint16_t  cpt1_lf_msixoff[MAX_RVU_BLKLF_CNT];
    uint16_t  ree0_lf_msixoff[MAX_RVU_BLKLF_CNT];
    uint16_t  ree1_lf_msixoff[MAX_RVU_BLKLF_CNT];
} __attribute__((packed));

struct npa_lf_alloc_rsp {
    MboxMsgHdr hdr;
    uint32_t stack_pg_ptrs;
    uint32_t stack_pg_bytes;
    uint16_t qints;
    uint8_t  cache_lines;
} __attribute__((packed));

struct nix_lf_alloc_rsp {
    MboxMsgHdr hdr;
    uint16_t sqb_size;
    uint16_t rx_chan_base;
    uint16_t tx_chan_base;
    uint8_t  rx_chan_cnt;
    uint8_t  tx_chan_cnt;
    uint8_t  lso_tsov4_idx;
    uint8_t  lso_tsov6_idx;
    uint8_t  mac_addr[6];  /* ETH_ALEN = 6 */
    uint8_t  lf_rx_stats;
    uint8_t  lf_tx_stats;
    uint16_t cints;
    uint16_t qints;
    uint8_t  cgx_links;
    uint8_t  lbk_links;
    uint8_t  sdp_links;
    uint8_t  tx_link;
} __attribute__((packed));

/* Message IDs */
#define MBOX_MSG_READY            1
#define MBOX_MSG_MSIX_OFFSET      2
#define MBOX_MSG_NPA_LF_ALLOC     3
#define MBOX_MSG_NIX_LF_ALLOC     4
#define MBOX_MSG_ATTACH_RESOURCES 5

#define PCI_REVISION_ID_96XX       0x00
#define PCI_SUBSYS_DEVID_96XX_RVU_PFVF      0xB200

/* State structure */
typedef struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar2;   /* CSR MMIO */
    MemoryRegion bar4;   /* Mailbox RAM */

    /* Mailbox state */
    uint8_t mailbox_buf[MBOX_SIZE]; /* unused, bar4 is RAM */
    QEMUTimer *af_response_timer;
    bool af_pending;
    uint16_t af_request_id;
    uint16_t af_request_pcifunc;
    uint32_t mbox_tx_offset;
    uint32_t mbox_rx_offset;

    /* Interrupt state */
    uint64_t pf_int;         /* RVU_PF_INT */
    uint64_t pf_int_ena;     /* enabled interrupts mask */
    bool msix_enabled;
    uint16_t msix_vectors;

    /* Block addresses */
    uint64_t nix_blkaddr;
    uint64_t npa_blkaddr;
    uint64_t cpt_blkaddr;

    /* Revision for RVU_PF_BLOCK_ADDRX_DISC */
    uint64_t rvum_revision;

    /* BAR4 base address to respond to RVU_PF_VF_BAR4_ADDR */
    uint64_t bar4_addr;
} PCIBaseState;

/* Forward declaration */
static void pcibase_update_irq(PCIBaseState *s);

/* Mailbox simulation: schedule response after a delay */
static void af_response_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    uint8_t *mbox_ptr = memory_region_get_ram_ptr(&s->bar4);
    uint8_t *rx_area = mbox_ptr + s->mbox_rx_offset;
    MboxMsgHdr rsp_hdr;

    /* Build common response header */
    memset(&rsp_hdr, 0, sizeof(rsp_hdr));
    rsp_hdr.sig = OTX2_MBOX_RSP_SIG;
    rsp_hdr.id = s->af_request_id;
    rsp_hdr.pcifunc = s->af_request_pcifunc;
    rsp_hdr.ver = OTX2_MBOX_VERSION;
    rsp_hdr.rc = 0;

    /* Fill message-specific data after the header */
    switch (s->af_request_id) {
    case MBOX_MSG_MSIX_OFFSET: {
        struct msix_offset_rsp *rsp = (struct msix_offset_rsp *)rx_area;
        memset(rsp, 0, sizeof(*rsp));
        rsp->hdr = rsp_hdr;
        /* Provide plausible msix offsets: NIX starting at 0x1000, NPA at 0x2000 */
        rsp->nix_msixoff = 0x1000;
        rsp->npa_msixoff = 0x2000;
        /* All other fields zero */
        break;
    }
    case MBOX_MSG_NPA_LF_ALLOC: {
        struct npa_lf_alloc_rsp *rsp = (struct npa_lf_alloc_rsp *)rx_area;
        memset(rsp, 0, sizeof(*rsp));
        rsp->hdr = rsp_hdr;
        rsp->stack_pg_ptrs = 64;
        rsp->stack_pg_bytes = 4096;
        rsp->qints = 1;
        rsp->cache_lines = 0;
        break;
    }
    case MBOX_MSG_NIX_LF_ALLOC: {
        struct nix_lf_alloc_rsp *rsp = (struct nix_lf_alloc_rsp *)rx_area;
        memset(rsp, 0, sizeof(*rsp));
        rsp->hdr = rsp_hdr;
        rsp->sqb_size = 128;
        rsp->rx_chan_base = 0;
        rsp->tx_chan_base = 0;
        rsp->rx_chan_cnt = 1;
        rsp->tx_chan_cnt = 1;
        rsp->lso_tsov4_idx = 0;
        rsp->lso_tsov6_idx = 0;
        memset(rsp->mac_addr, 0, 6);
        rsp->lf_rx_stats = 0;
        rsp->lf_tx_stats = 0;
        rsp->cints = 1;
        rsp->qints = 1;
        rsp->cgx_links = 0;
        rsp->lbk_links = 0;
        rsp->sdp_links = 0;
        rsp->tx_link = 0;
        break;
    }
    case MBOX_MSG_READY:
    case MBOX_MSG_ATTACH_RESOURCES:
    default:
        /* Just copy header, no additional data */
        memcpy(rx_area, &rsp_hdr, sizeof(rsp_hdr));
        break;
    }

    /* Set interrupt and raise */
    s->pf_int |= MBOX_UP_MSG;
    pcibase_update_irq(s);
}

/* Helper to handle mailbox trigger */
static void handle_mbox_trigger(PCIBaseState *s, uint64_t val)
{
    if (val & MBOX_DOWN_MSG) {
        /* AF received a message */
        uint8_t *mbox_ptr = memory_region_get_ram_ptr(&s->bar4);
        uint8_t *tx_area = mbox_ptr + s->mbox_tx_offset;
        struct mbox_hdr *mhdr = (struct mbox_hdr *)tx_area;
        MboxMsgHdr *msg;

        /* Sanity check: at least enough data for mbox_hdr and one message */
        if (mhdr->num_msgs < 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "rvu_nicpf: no messages in mailbox\n");
            return;
        }
        /* First message starts after the mbox_hdr */
        msg = (MboxMsgHdr *)(tx_area + sizeof(struct mbox_hdr));
        if (msg->sig != OTX2_MBOX_REQ_SIG) {
            qemu_log_mask(LOG_GUEST_ERROR, "rvu_nicpf: invalid msg sig 0x%x\n", msg->sig);
            return;
        }
        s->af_request_id = msg->id;
        s->af_request_pcifunc = msg->pcifunc;
        /* Schedule response after 1 ms (arbitrary delay) */
        timer_mod(s->af_response_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
    }
    if (val & MBOX_UP_MSG) {
        /* Driver acknowledges UP message, clear it */
        s->pf_int &= ~MBOX_UP_MSG;
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x100000) {
        /* CSR space */
        switch (addr) {
        case RVU_PF_BLOCK_ADDRX_DISC(0):
            val = s->rvum_revision;
            break;
        case RVU_PF_INT:
            val = s->pf_int;
            break;
        case RVU_PF_INT_ENA_W1S:
        case RVU_PF_INT_ENA_W1C:
            val = s->pf_int_ena;
            break;
        case RVU_PF_VF_BAR4_ADDR:
            val = s->bar4_addr;
            break;
        case RVU_PF_PFAF_MBOX0:
            val = s->pf_int & (MBOX_UP_MSG | MBOX_DOWN_MSG);
            break;
        default:
            break;
        }
    }
    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x100000) {
        switch (addr) {
        case RVU_PF_INT:
            /* Write 1 to clear */
            s->pf_int &= ~val;
            pcibase_update_irq(s);
            break;
        case RVU_PF_INT_ENA_W1S:
            s->pf_int_ena |= val;
            pcibase_update_irq(s);
            break;
        case RVU_PF_INT_ENA_W1C:
            s->pf_int_ena &= ~val;
            pcibase_update_irq(s);
            break;
        case RVU_PF_PFAF_MBOX0:
            handle_mbox_trigger(s, val);
            pcibase_update_irq(s);
            break;
        default:
            break;
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->msix_enabled && msix_enabled(pdev)) {
        if ((s->pf_int & s->pf_int_ena) != 0) {
            msix_notify(pdev, RVU_PF_INT_VEC_AFPF_MBOX);
        }
    } else {
        if (s->pf_int & s->pf_int_ena) {
            pci_set_irq(pdev, 1);
        } else {
            pci_set_irq(pdev, 0);
        }
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, PCI_REVISION_ID_96XX);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, PCI_SUBSYS_DEVID_96XX_RVU_PFVF);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x177D);
    pci_set_byte(pci_conf + PCI_INTERRUPT_PIN, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Initialize MSI-X: 128 vectors, table in BAR2 at offset 0, PBA in BAR2 at offset 0x800 */
    int msix_cap = pci_add_capability(pdev, PCI_CAP_ID_MSIX, 0, 12, errp);
    if (msix_cap < 0) {
        error_setg(errp, "Failed to add MSI-X capability");
        return;
    }
    if (msix_init(pdev, 128, &s->bar2, PCI_CFG_REG_BAR_NUM, 0, &s->bar2, PCI_CFG_REG_BAR_NUM, 0x800, msix_cap, errp)) {
        error_append_hint(errp, "MSI-X initialization failed\n");
        return;
    }
    s->msix_enabled = true;

    /* Initialize BAR2 (CSR) and BAR4 (Mailbox) */
    memory_region_init_io(&s->bar2, OBJECT(s), &pcibase_mmio_ops, s, "rvu_nicpf_csr", 0x100000);
    pci_register_bar(pdev, PCI_CFG_REG_BAR_NUM, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar2);

    memory_region_init_ram(&s->bar4, OBJECT(s), "rvu_nicpf_mbox", MBOX_SIZE, errp);
    pci_register_bar(pdev, PCI_MBOX_BAR_NUM, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar4);

    /* Set BAR4 address for RVU_PF_VF_BAR4_ADDR register */
    s->bar4_addr = pci_get_bar_addr(pdev, PCI_MBOX_BAR_NUM);

    /* Mailbox offsets: TX at 0, RX at half */
    s->mbox_tx_offset = 0;
    s->mbox_rx_offset = MBOX_SIZE / 2;

    /* Initialize revision */
    s->rvum_revision = 0x00001000;

    /* Create timer for AF response */
    s->af_response_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, af_response_timer_cb, s);

    /* Initialize block addresses */
    s->nix_blkaddr = BLKTYPE_NIX;
    s->npa_blkaddr = BLKTYPE_NPA;
    s->cpt_blkaddr = BLKTYPE_CPT;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(s->af_response_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar2, &s->bar2);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->pf_int = 0;
    s->pf_int_ena = 0;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "rvu_nicpf_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT64(pf_int, PCIBaseState),
        VMSTATE_UINT64(pf_int_ena, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;
    dc->vmsd = &vmstate_pcibase;
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
