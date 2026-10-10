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
#include "qemu/bitops.h"
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

#define TYPE_PCIBASE_DEVICE "fsl_enetc_vf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_FREESCALE
#define PCI_VENDOR_ID_FREESCALE 0x1957
#endif
#define ENETC_DEV_ID_VF 0xef00
#define ENETC_BAR_REGS 0

#define ENETC_VSIMSGSNDAR0 0x210
#define ENETC_VSIMSGSNDAR1 0x214
#define ENETC_VSIMSGSR 0x204
#define ENETC_SIRSSCAPR 0x1600
#define ENETC_SIRFSCAPR 0x1200
#define ENETC_SICAPR0 0x900
#define ENETC_SIPCAPR0 0x20
#define ENETC_SICBDRBAR1 0x814
#define ENETC_SICAR2 0x48
#define ENETC_SICBDRBAR0 0x810
#define ENETC_SICBDRPIR 0x818
#define ENETC_SICBDRCIR 0x81c
#define ENETC_SICBDRLENR 0x820
#define ENETC_SICBDRMR 0x800
#define ENETC_SIMR 0
#define ENETC_SICAR1 0x44
#define ENETC_SICAR0 0x40
#define ENETC_SIRBGCR 0x38
#define ENETC_TBMR 0
#define ENETC_SIPMAR0 0x80
#define ENETC_SIPMAR1 0x84
#define ENETC_TBIER 0xa0
#define ENETC_RBICR1 0xac
#define ENETC_RBIER 0xa0
#define ENETC_RBMR 0
#define ENETC_MMCSR 0x1f00
#define ENETC_TBLENR 0x20
#define ENETC_TBICR0 0xa8
#define ENETC_SITXIDR 0xa18
#define ENETC_TBBAR1 0x14
#define ENETC_TBCIR 0x1c
#define ENETC_TBBAR0 0x10
#define ENETC_TBPIR 0x18
#define ENETC_RBBAR0 0x10
#define ENETC_RBBAR1 0x14
#define ENETC_RBLENR 0x20
#define ENETC_RBBSR 0x8
#define ENETC_SIRXIDR 0xa28
#define ENETC_RBCIR 0xc
#define ENETC_RBICR0 0xa8
#define ENETC_RBPIR 0x18
#define ENETC_TBICR1 0xac
#define ENETC_SICTR0 0x18
#define ENETC_SICTR1 0x1c
#define ENETC_TBSR 0x4
#define ENETC_PFPMR 0x1900
#define ENETC_MMFCRXR 0x1f14
#define ENETC_MMFCTXR 0x1f18
#define ENETC_MMHCR 0x1f1c
#define ENETC_MMFAECR 0x1f08
#define ENETC_MMFAOCR 0x1f10
#define ENETC_MMFSECR 0x1f0c
#define ENETC_PM0_SINGLE_STEP 0x80c0
#define ENETC_PM0_CMD_CFG 0x8008
#define ENETC_SITFRM 0x328
#define ENETC_SITUCA 0x330
#define ENETC_SIRUCA 0x310
#define ENETC_SIROCT 0x300
#define ENETC_SITOCT 0x320
#define ENETC_SITMCA 0x338
#define ENETC_SIRMCA 0x318
#define ENETC_SIRFRM 0x308
#define ENETC_UFDMF 0x1680
#define ENETC_MFDMF 0x1684
#define ENETC_PBFDSIR 0x0810
#define ENETC_PUFDVFR 0x1780
#define ENETC_PBFDVFR 0x1788
#define ENETC_PMFDVFR 0x1784
#define ENETC_PFDMSAPR 0x0814
#define ENETC_PSR 0x0004
#define ENETC_PCAPR0 0x0900
#define ENETC_PRFSCAPR 0x1804
#define ENETC_PMR 0x0000
#define ENETC_PTXMBAR 0x0608
#define ENETC_PSIPMR 0x0018
#define ENETC_PCAPR1 0x0904
#define ENETC_PM0_MAXFRM 0x8014
#define ENETC_PM0_IF_MODE 0x8300
#define ENETC_SICAPR1 0x904
#define ENETC_SICBDRSR 0x804
#define ENETC_SIUEFDCR 0xe28
#define ENETC_RBSR 0x4

#define ENETC_REV_1_0		0x0100
#define ENETC_REV1		0x1
#define ENETC_MAX_NUM_TXQS	8
#define ENETC_CBDR_DEFAULT_SIZE	64
#define ENETC_F_TX_TSTAMP_MASK	0xff

#define ENETC_PORT_BASE		0x10000
#define ENETC_GLOBAL_BASE	0x20000
#define ENETC_SIPCAPR0_RFS	BIT(2)
#define ENETC_SIPCAPR0_RSS	BIT(8)
#define ENETC_SIPCAPR0_LSO	BIT(1)
#define ENETC_SIRFSCAPR_GET_NUM_RFS(val) ((val) & 0x7f)
#define ENETC_SIRSSCAPR_GET_NUM_RSS(val) (BIT((val) & 0xf) * 32)
#define ENETC_SICAR_RD_COHERENT	0x2b2b0000
#define ENETC_SICAR_WR_COHERENT	0x00006727
#define ENETC_SICAR_MSI	0x00300030
#define ENETC_SIMR_EN	BIT(31)
#define ENETC_SIMR_RSSE	BIT(0)
#define ENETC_RTBLENR_LEN(n)	((n) & ~0x7)
#define ENETC_BDR(t, i, r)	(0x8000 + (t) * 0x100 + ENETC_BDR_OFF(i) + (r))
#define ENETC_SIMSIRRV(n) (0xB80 + (n) * 0x4)
#define ENETC_SIMSITRV(n) (0xB00 + (n) * 0x4)
#define ENETC_BDR_INT_BASE_IDX	1
#define TX 1
#define RX 0
#define ENETC_MAX_RFS_SIZE 64
#define ENETC_SI_F_LSO	BIT(3)
#define ENETC_TX_RING_DEFAULT_SIZE	2048
#define ENETC_RX_RING_DEFAULT_SIZE	2048
#define ENETC_MAX_MTU		(ENETC_MAC_MAXFRM_SIZE - \
				(ETH_FCS_LEN + ETH_HLEN + VLAN_HLEN))
#define ENETC_SI_F_PPM	BIT(4)
#define ENETC_MAX_BDR_INT	6
#define ENETC_SI_ALIGN	32

#define ENETC_VSIMSGSR_MB	BIT(0)
#define ENETC_VSIMSGSR_MS	BIT(1)

#define ENETC4_SILSOSFMR0		0x1300
#define ENETC4_SILSOSFMR1		0x1304

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
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
    uint32_t regs[0x100000 / 4]; /* 1MB size to cover ENETC_GLOBAL_BASE */

    /* DMA Context */
    dma_addr_t tx_ring_base;
    dma_addr_t rx_ring_base;

    bool link_up;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        val = s->regs[addr / 4];
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->regs)) {
        s->regs[addr / 4] = val;
        
        if (addr == ENETC_VSIMSGSNDAR0) {
            /* Simulate immediate VSI mailbox command completion */
            /* Driver polls ENETC_VSIMSGSR for MB (Mailbox Busy) bit to clear */
            s->regs[ENETC_VSIMSGSR / 4] &= ~ENETC_VSIMSGSR_MB;
        } else if (addr == ENETC_SICBDRPIR) {
            /* Simulate immediate CBDR (Control BD Ring) completion */
            if (s->regs[ENETC_SICBDRMR / 4] & BIT(31)) {
                s->regs[ENETC_SICBDRCIR / 4] = val;
            }
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    
    /* Initialize Primary MAC Address (e.g., 52:54:00:12:34:56) */
    /* Driver reads SIPMAR0 (lower 4 bytes) and SIPMAR1 (upper 2 bytes) */
    s->regs[ENETC_SIPMAR0 / 4] = 0x12005452;
    s->regs[ENETC_SIPMAR1 / 4] = 0x00005634;

    /* Capabilities */
    /* 2 RX rings, 2 TX rings */
    s->regs[ENETC_SICAPR0 / 4] = (2 << 16) | 2;
    /* RFS, RSS, LSO supported */
    s->regs[ENETC_SIPCAPR0 / 4] = ENETC_SIPCAPR0_RFS | ENETC_SIPCAPR0_RSS | ENETC_SIPCAPR0_LSO;
    /* 64 RFS entries */
    s->regs[ENETC_SIRFSCAPR / 4] = 64;
    /* 1 RSS (32 entries) */
    s->regs[ENETC_SIRSSCAPR / 4] = 1;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_FREESCALE );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ENETC_DEV_ID_VF );
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
    s->bar_info[0].index = ENETC_BAR_REGS;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100000; /* 1MB size to cover ENETC_GLOBAL_BASE */
    s->bar_info[0].name = "enetc_vf_regs";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;
    msix_init(pdev, 16, &s->bar_regions[0], 0, 0x80000, &s->bar_regions[0], 0, 0x81000, 0, errp);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "fsl_enetc_vf_pci",
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
