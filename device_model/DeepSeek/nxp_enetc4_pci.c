/*
 * QEMU device model for NXP ENETC4 PF
 * Based on Linux driver at drivers/net/ethernet/freescale/enetc/enetc4_pf.c
 * QEMU 8.2.10 compatible
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

#define TYPE_PCIBASE_DEVICE "nxp_enetc4_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define NXP_ENETC_VENDOR_ID 0x1131
#define NXP_ENETC_PF_DEV_ID  0xe101
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

/* BAR0 absolute offsets for port and global (as required by driver mapping) */
#define ENETC_PORT_BASE   0x10000
#define ENETC_GLOBAL_BASE 0x20000

/* Port register offsets (absolute BAR0 addresses) */
#define ENETC4_ECAPR1        (ENETC_PORT_BASE + 0x4)
#define ENETC4_ECAPR2        (ENETC_PORT_BASE + 0x8)
#define ENETC4_PMR           (ENETC_PORT_BASE + 0x10)
#define ENETC4_PSR           (ENETC_PORT_BASE + 0x4104)
#define ENETC4_POR           (ENETC_PORT_BASE + 0x4100)
#define ENETC4_PCAPR         (ENETC_PORT_BASE + 0x4000)
#define ENETC4_PMCAPR        (ENETC_PORT_BASE + 0x4004)
#define ENETC4_PCR           (ENETC_PORT_BASE + 0x4010)
#define ENETC4_PMAR0         (ENETC_PORT_BASE + 0x4020)
#define ENETC4_PMAR1         (ENETC_PORT_BASE + 0x4024)
#define ENETC4_PSIPMAR0(a)   ((a) * 0x80 + ENETC_PORT_BASE + 0x2000)
#define ENETC4_PSIPMAR1(a)   ((a) * 0x80 + ENETC_PORT_BASE + 0x2004)
#define ENETC4_PSIPMMR       (ENETC_PORT_BASE + 0x200)
#define ENETC4_PSICFGR0(a)   ((a) * 0x80 + ENETC_PORT_BASE + 0x2010)
#define ENETC4_PSICFGR2(a)   ((a) * 0x80 + ENETC_PORT_BASE + 0x2018)
#define ENETC4_PSIUMHFR0(a)  ((a) * 0x80 + ENETC_PORT_BASE + 0x2050)
#define ENETC4_PSIUMHFR1(a)  ((a) * 0x80 + ENETC_PORT_BASE + 0x2054)
#define ENETC4_PSIMMHFR0(a)  ((a) * 0x80 + ENETC_PORT_BASE + 0x2058)
#define ENETC4_PSIMMHFR1(a)  ((a) * 0x80 + ENETC_PORT_BASE + 0x205c)
#define ENETC4_PSIVHFR0(a)   ((a) * 0x80 + ENETC_PORT_BASE + 0x2060)
#define ENETC4_PSIVHFR1(a)   ((a) * 0x80 + ENETC_PORT_BASE + 0x2064)
#define ENETC4_PSIVLANFMR    (ENETC_PORT_BASE + 0x2c4)
#define ENETC4_PSIPVMR       (ENETC_PORT_BASE + 0x204)
#define ENETC4_PSIMAFCAPR    (ENETC_PORT_BASE + 0x280)
#define ENETC4_PBFDSIR       (ENETC_PORT_BASE + 0x208)
#define ENETC4_PFDMSAPR      (ENETC_PORT_BASE + 0x20c)
#define ENETC4_PICDRDCR(a)   ((a) * 0x10 + ENETC_PORT_BASE + 0x140)
#define ENETC4_PUFDVFR       (ENETC_PORT_BASE + 0x2d0)
#define ENETC4_PUFDMFR       (ENETC_PORT_BASE + 0x284)
#define ENETC4_PMFDVFR       (ENETC_PORT_BASE + 0x2d4)
#define ENETC4_PMFDMFR       (ENETC_PORT_BASE + 0x288)
#define ENETC4_PBFDVFR       (ENETC_PORT_BASE + 0x2d8)
#define ENETC4_PRXDCR        (ENETC_PORT_BASE + 0x41c0)
#define ENETC4_PRXDCRR0      (ENETC_PORT_BASE + 0x41c8)
#define ENETC4_PRXDCRR1      (ENETC_PORT_BASE + 0x41cc)
#define ENETC4_PRXDCRRR      (ENETC_PORT_BASE + 0x41c4)
#define ENETC4_PTCTMSDUR(a)  ((a) * 0x20 + ENETC_PORT_BASE + 0x4208)
#define ENETC4_PM_MAXFRM(mac) (0x5014 + (mac) * 0x400 + ENETC_PORT_BASE)
#define ENETC4_PM_CMD_CFG(mac) (0x5008 + (mac) * 0x400 + ENETC_PORT_BASE)
#define ENETC4_PM_IF_MODE(mac) (0x5300 + (mac) * 0x400 + ENETC_PORT_BASE)
#define ENETC4_PM_IEVENT(mac)  (0x5040 + (mac) * 0x400 + ENETC_PORT_BASE)
#define ENETC4_PM_SINGLE_STEP(mac) (0x50c0 + (mac) * 0x400 + ENETC_PORT_BASE)
#define ENETC4_PPAUOFFTR      (ENETC_PORT_BASE + 0x10c)
#define ENETC4_PPAUONTR       (ENETC_PORT_BASE + 0x108)
#define ENETC4_PM_PAUSE_QUANTA(mac) (0x5054 + (mac) * 0x400 + ENETC_PORT_BASE)
#define ENETC4_PM_PAUSE_THRESH(mac) (0x5064 + (mac) * 0x400 + ENETC_PORT_BASE)
#define ENETC4_EMDIO_BASE     (ENETC_PORT_BASE + 0x5c00)
#define ENETC4_PM_IMDIO_BASE  (ENETC_PORT_BASE + 0x5030)
#define ENETC4_SILSOSFMR0     (ENETC_PORT_BASE + 0x1300)
#define ENETC4_SILSOSFMR1     (ENETC_PORT_BASE + 0x1304)
#define ENETC4_PRSSKR(n)      ((n) * 0x4 + ENETC_PORT_BASE + 0x250)
#define ENETC4_PPMTUFCR       (ENETC_PORT_BASE + 0x50c8)
#define ENETC4_PPMRBFCR       (ENETC_PORT_BASE + 0x5098)
#define ENETC4_PPMTMFCR       (ENETC_PORT_BASE + 0x50d0)
#define ENETC4_PPMROCR        (ENETC_PORT_BASE + 0x5080)
#define ENETC4_PPMRUFCR       (ENETC_PORT_BASE + 0x5088)
#define ENETC4_PPMTBFCR       (ENETC_PORT_BASE + 0x50d8)
#define ENETC4_PPMRMFCR       (ENETC_PORT_BASE + 0x5090)
#define ENETC4_PPMTOCR        (ENETC_PORT_BASE + 0x50c0)

#define ENETC_G_EIPBRR0       (ENETC_GLOBAL_BASE + 0x0)

/* SI and BDR register offsets (relative to their base) */
#define ENETC_SIMR       0
#define ENETC_SICAR0     0x40
#define ENETC_SICAR1     0x44
#define ENETC_SICAPR0    0x900
#define ENETC_SIPCAPR0   0x20
#define ENETC_SIRFSCAPR  0x1200
#define ENETC_SIRSSCAPR  0x1600
#define ENETC_SICBDRPIR  0x818
#define ENETC_SICBDRCIR  0x81c
#define ENETC_SICBDRMR   0x800
#define ENETC_SICBDRBAR0 0x810
#define ENETC_SICBDRBAR1 0x814
#define ENETC_SICBDRLENR 0x820

/* Bitmask definitions */
#define ECAPR1_NUM_VSI        0xff000000
#define ECAPR1_NUM_MSIX       0x00fff000
#define ECAPR2_NUM_RX_BDR     0x00ff0000
#define ECAPR2_NUM_TX_BDR     0x000000ff
#define PMCAPR_HD             BIT(0)
#define PCAPR_LINK_TYPE       BIT(0)
#define PSIMAFCAPR_NUM_MAC_AFTE 0x000000ff
#define PMR_SI_EN(i)          BIT(i)
#define PSIPMMR_SI_MAC_UP(i)  BIT(i)
#define PSIPMMR_SI_MAC_MP(i)  BIT((i) + 16)
#define PSIVLANFMR_VS         BIT(0)
#define PSR_RX_BUSY           BIT(0)
#define PCR_PSPEED            0x00000007
#define PCR_PSPEED_VAL(s)     ((s) & PCR_PSPEED)
#define POR_TXDIS             BIT(0)
#define POR_RXDIS             BIT(1)
#define PM_CMD_CFG_LOOP_EN    BIT(0)
#define PM_CMD_CFG_LPBK_MODE  0x00000006
#define LPBCK_MODE_MAC_LEVEL  0x2
#define PM_CMD_CFG_TX_EN      BIT(2)
#define PM_CMD_CFG_RX_EN      BIT(3)
#define PM_CMD_CFG_PAUSE_IGN  BIT(4)
#define PM_CMD_CFG_HD_FCEN    BIT(5)
#define PM_IF_MODE_IFMODE     0x00000007
#define PM_IF_MODE_ENA        BIT(3)
#define PM_IF_MODE_M10        BIT(4)
#define PM_IF_MODE_REVMII     BIT(5)
#define PM_IF_MODE_SSP        0x00000700
#define PM_IF_MODE_HD         BIT(8)
#define IFMODE_RGMII          0x0
#define IFMODE_RMII           0x4
#define IFMODE_SGMII          0x5
#define IFMODE_XGMII          0x6
#define SSP_10M               0x0
#define SSP_100M              0x1
#define SSP_1G                0x2
#define PM_IEVENT_TX_EMPTY    BIT(16)
#define PM_IEVENT_RX_EMPTY    BIT(17)
#define PSICFGR2_NUM_MSIX     0x0000007f
#define ENETC_PSICFGR0_SET_TXBDR(n)  ((n) << 0)
#define ENETC_PSICFGR0_SET_RXBDR(n)  ((n) << 8)
#define ENETC_PSICFGR0_SIVC(v)       ((v) << 16)
#define ENETC_PSICFGR0_VTE           BIT(31)
#define ENETC_PSICFGR0_SIVIE         BIT(30)
#define ENETC_VLAN_TYPE_C            BIT(0)
#define ENETC_VLAN_TYPE_S            BIT(1)
#define ENETC_RBMR_VTE               BIT(16)
#define ENETC_TBMR_VIH               BIT(16)
#define ENETC_SICAR_RD_COHERENT      0x2b2b0000
#define ENETC_SICAR_WR_COHERENT      0x00006727
#define ENETC_SICAR_MSI              0x00300030
#define ENETC_SIMR_EN                BIT(31)
#define ENETC_SIRFSCAPR_GET_NUM_RFS(val) ((val) & 0x7f)
#define ENETC_SIRSSCAPR_GET_NUM_RSS(val) (BIT((val) & 0xf) * 32)
#define ENETC_RBMR_CM                BIT(4)

#define ENETC_BAR_REGS 0
#define BAR0_SIZE 0x40000   /* 256KB, must be > ENETC_GLOBAL_BASE (0x20000) */
#define BAR0_ARRAY_SIZE (BAR0_SIZE / 4)
#define MSIX_BAR_SIZE 0x4000ull

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint32_t regs[BAR0_ARRAY_SIZE];

    int dma_dummy;
    uint32_t status;
    uint32_t reset_ctl;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4 || (addr & 3)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad MMIO read addr=0x%lx size=%d\n", __func__, addr, size);
        return 0xffffffffull;
    }

    if (addr < BAR0_SIZE) {
        val = s->regs[addr >> 2];
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: MMIO read out of bounds addr=0x%lx\n", __func__, addr);
        val = 0xffffffffull;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 || (addr & 3)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad MMIO write addr=0x%lx size=%d val=0x%lx\n", __func__, addr, size, val);
        return;
    }

    if (addr < BAR0_SIZE) {
        s->regs[addr >> 2] = (uint32_t)val;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: MMIO write out of bounds addr=0x%lx val=0x%lx\n", __func__, addr, val);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: PIO read not implemented\n", __func__);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: PIO write not implemented\n", __func__);
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
    s->status = 0;
    s->reset_ctl = 0;

    /* Power-on defaults */
    s->regs[ENETC4_ECAPR1 >> 2] = (1 << 24) | (63 << 12);   /* NUM_VSI=1, 64 vectors */
    s->regs[ENETC4_ECAPR2 >> 2] = (8 << 16) | 8;            /* 8 RX BDRs, 8 TX BDRs */
    s->regs[ENETC4_PMCAPR >> 2] = 0;
    s->regs[ENETC4_PSIMAFCAPR >> 2] = 4;                    /* 4 MAC filter entries */
    s->regs[ENETC4_PCAPR >> 2] = 0;
    s->regs[ENETC_G_EIPBRR0 >> 2] = 0x0001;                 /* IP revision */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr size = bi->size;
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, NXP_ENETC_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, NXP_ENETC_PF_DEV_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = MSIX_BAR_SIZE;
    s->bar_info[1].name = "msix";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    Error *local_err = NULL;
    if (msix_init_exclusive_bar(pdev, 64, 1, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }
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
    .name = "nxp_enetc4_pci",
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
