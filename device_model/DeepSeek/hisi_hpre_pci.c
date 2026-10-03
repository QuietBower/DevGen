/*
 * QEMU 8.2.10 virtual PCI device model for HiSilicon HPRE accelerator
 * Derived strictly from driver source: drivers/crypto/hisilicon/hpre/hpre_main.c
 * and supplementary func: hisi_qm_init
 * Static definitions only; no runtime logic.
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

#define TYPE_PCIBASE_DEVICE "hisi_hpre_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification from hpre_main.c pci_device_id table (first entry) */
#define PCI_VENDOR_ID_HUAWEI          0x19e5
#define PCI_DEVICE_ID_HUAWEI_HPRE_PF   0xa258

/* Register offsets extracted from driver headers/source */
#define HPRE_CTRL_CNT_CLR_CE_BIT       BIT(0)
#define HPRE_CTRL_CNT_CLR_CE           0x301000
#define HPRE_FSM_MAX_CNT               0x301008
#define HPRE_VFG_AXQOS                 0x30100c
#define HPRE_VFG_AXCACHE               0x301010
#define HPRE_RDCHN_INI_CFG             0x301014
#define HPRE_AWUSR_FP_CFG              0x301018
#define HPRE_BD_ENDIAN                 0x301020
#define HPRE_ECC_BYPASS                0x301024
#define HPRE_RAS_WIDTH_CFG             0x301028
#define HPRE_POISON_BYPASS             0x30102c
#define HPRE_BD_ARUSR_CFG              0x301030
#define HPRE_BD_AWUSR_CFG              0x301034
#define HPRE_TYPES_ENB                 0x301038
#define HPRE_RSA_ENB                   BIT(0)
#define HPRE_ECC_ENB                   BIT(1)
#define HPRE_DATA_RUSER_CFG            0x30103c
#define HPRE_DATA_WUSER_CFG            0x301040
#define HPRE_INT_MASK                  0x301400
#define HPRE_INT_STATUS                0x301800
#define HPRE_HAC_INT_MSK               0x301400
#define HPRE_HAC_RAS_CE_ENB            0x301410
#define HPRE_HAC_RAS_NFE_ENB           0x301414
#define HPRE_HAC_RAS_FE_ENB            0x301418
#define HPRE_HAC_INT_SET               0x301500
#define HPRE_AXI_ERROR_MASK            GENMASK(21, 10)
#define HPRE_RNG_TIMEOUT_NUM           0x301A34
#define HPRE_CORE_INT_ENABLE           0
#define HPRE_RDCHN_INI_ST              0x301a00
#define HPRE_CLSTR_BASE                0x302000
#define HPRE_CORE_EN_OFFSET            0x04
#define HPRE_CORE_INI_CFG_OFFSET       0x20
#define HPRE_CORE_INI_STATUS_OFFSET    0x80
#define HPRE_CORE_HTBT_WARN_OFFSET     0x8c
#define HPRE_CORE_IS_SCHD_OFFSET       0x90
#define HPRE_RAS_CE_ENB                0x301410
#define HPRE_RAS_NFE_ENB               0x301414
#define HPRE_RAS_FE_ENB                0x301418
#define HPRE_OOO_SHUTDOWN_SEL          0x301a3c
#define HPRE_HAC_RAS_FE_ENABLE         0
#define HPRE_CORE_ENB               (HPRE_CLSTR_BASE + HPRE_CORE_EN_OFFSET)
#define HPRE_CORE_INI_CFG           (HPRE_CLSTR_BASE + HPRE_CORE_INI_CFG_OFFSET)
#define HPRE_CORE_INI_STATUS        (HPRE_CLSTR_BASE + HPRE_CORE_INI_STATUS_OFFSET)
#define HPRE_HAC_ECC1_CNT             0x301a04
#define HPRE_HAC_ECC2_CNT             0x301a08
#define HPRE_HAC_SOURCE_INT           0x301600
#define HPRE_CLSTR_ADDR_INTRVL        0x1000
#define HPRE_CLUSTER_INQURY           0x100
#define HPRE_CLSTR_ADDR_INQRY_RSLT    0x104
#define HPRE_PASID_EN_BIT             9
#define HPRE_REG_RD_INTVRL_US         10
#define HPRE_REG_RD_TMOUT_US          1000
#define HPRE_DBGFS_VAL_MAX_LEN        20
#define HPRE_QM_USR_CFG_MASK          GENMASK(31, 1)
#define HPRE_QM_AXI_CFG_MASK          GENMASK(15, 0)
#define HPRE_QM_VFG_AX_MASK           GENMASK(7, 0)
#define HPRE_BD_USR_MASK              GENMASK(1, 0)
#define HPRE_PREFETCH_CFG             0x301130
#define HPRE_SVA_PREFTCH_DFX          0x30115C
#define HPRE_PREFETCH_ENABLE          (~(BIT(0) | BIT(30)))
#define HPRE_PREFETCH_DISABLE         BIT(30)
#define HPRE_SVA_DISABLE_READY        (BIT(4) | BIT(8))
#define HPRE_SVA_PREFTCH_DFX4         0x301144
#define HPRE_WAIT_SVA_READY           500000
#define HPRE_READ_SVA_STATUS_TIMES    3
#define HPRE_WAIT_US_MIN              10
#define HPRE_WAIT_US_MAX              20
#define HPRE_CLKGATE_CTL              0x301a10
#define HPRE_PEH_CFG_AUTO_GATE        0x301a2c
#define HPRE_CLUSTER_DYN_CTL          0x302010
#define HPRE_CORE_SHB_CFG             0x302088
#define HPRE_CLKGATE_CTL_EN           BIT(0)
#define HPRE_PEH_CFG_AUTO_GATE_EN     BIT(0)
#define HPRE_CLUSTER_DYN_CTL_EN       BIT(0)
#define HPRE_CORE_GATE_EN             (BIT(30) | BIT(31))
#define HPRE_AM_OOO_SHUTDOWN_ENB      0x301044
#define HPRE_AM_OOO_SHUTDOWN_ENABLE   BIT(0)
#define HPRE_WR_MSI_PORT              BIT(2)
#define HPRE_CORE_ECC_2BIT_ERR        BIT(1)
#define HPRE_OOO_ECC_2BIT_ERR         BIT(5)
#define HPRE_QM_BME_FLR               BIT(7)
#define HPRE_QM_PM_FLR                BIT(11)
#define HPRE_QM_SRIOV_FLR             BIT(12)
#define HPRE_SHAPER_TYPE_RATE         640
#define HPRE_VIA_MSI_DSM              1
#define HPRE_SQE_MASK_OFFSET          8
#define HPRE_SQE_MASK_LEN             44
#define HPRE_CTX_Q_NUM_DEF            1
#define HPRE_DFX_BASE                 0x301000
#define HPRE_DFX_COMMON1              0x301400
#define HPRE_DFX_COMMON2              0x301A00
#define HPRE_DFX_CORE                 0x302000
#define HPRE_DFX_BASE_LEN             0x55
#define HPRE_DFX_COMMON1_LEN          0x41
#define HPRE_DFX_COMMON2_LEN          0xE
#define HPRE_DFX_CORE_LEN             0x43
#define HPRE_MAX_CHANNEL_NUM          2
#define HPRE_V3_ECC_ALG_TYPE          1
#define HPRE_V2_ALG_TYPE              0
#define HPRE_PF_DEF_Q_BASE            0
#define HPRE_SQE_SIZE                 sizeof(struct hpre_sqe)

/* QM doorbell timeout config */
#define QM_DB_TIMEOUT_CFG             0x100074
#define QM_DB_TIMEOUT_SET             0x1fffff

/* Additional QM registers from supplementary source */
#define QM_PEH_AXUSER_CFG             0x1000cc
#define QM_PEH_AXUSER_CFG_ENABLE      0x1000d0
#define PEH_AXUSER_CFG_ENABLE         0xffffffff
#define QM_ARUSER_M_CFG_ENABLE        0x100090
#define QM_AWUSER_M_CFG_ENABLE        0x1000a0
#define QM_AXI_M_CFG                  0x1000ac

#ifndef GENMASK
#define GENMASK(h, l) ((((uint32_t)~0U) << (l)) & (((uint32_t)~0U) >> (31 - (h))))
#endif

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

#define HPRE_MMIO_SIZE 0x400000
#define HPRE_MMIO_REG_COUNT (HPRE_MMIO_SIZE / sizeof(uint32_t))

#define BAR2_SIZE 0x200000
#define QM_REG_COUNT (BAR2_SIZE / sizeof(uint32_t))

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint32_t mmio_regs[HPRE_MMIO_REG_COUNT];
    uint32_t qm_regs[QM_REG_COUNT];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pend = s->intr_status & ~s->intr_mask;
    if (pend) {
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

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return 0;
    }

    if (addr >= HPRE_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    val = s->mmio_regs[addr / sizeof(uint32_t)];
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return;
    }

    if (addr >= HPRE_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    uint32_t *reg = &s->mmio_regs[addr / sizeof(uint32_t)];

    switch (addr) {
    case HPRE_CTRL_CNT_CLR_CE:
        *reg = val;
        break;
    case HPRE_RDCHN_INI_CFG:
        *reg = val;
        /* Simulate initialization done: set RDCHN_INI_ST bit 0 */
        s->mmio_regs[HPRE_RDCHN_INI_ST / sizeof(uint32_t)] |= BIT(0);
        break;
    case HPRE_RDCHN_INI_ST: /* should not be written, but allow */
        *reg = val;
        break;
    case HPRE_CORE_ENB:
        *reg = val;
        break;
    case HPRE_CORE_INI_CFG:
        *reg = val;
        /* After writing core ini cfg, set core ini status to core_enable value */
        if (val == 0x1) {
            uint32_t core_enb = s->mmio_regs[HPRE_CORE_ENB / sizeof(uint32_t)];
            s->mmio_regs[HPRE_CORE_INI_STATUS / sizeof(uint32_t)] = core_enb;
        }
        break;
    case HPRE_CORE_INI_STATUS: /* maybe read-only, but allow */
        *reg = val;
        break;
    case HPRE_CLUSTER_INQURY + HPRE_CLSTR_BASE:
        /* handled below */
        break;
    case HPRE_CLSTR_ADDR_INQRY_RSLT + HPRE_CLSTR_BASE:
        *reg = val;
        break;
    case HPRE_PREFETCH_CFG:
        *reg = val;
        break;
    case HPRE_SVA_PREFTCH_DFX: /* read-only for polling */
    case HPRE_SVA_PREFTCH_DFX4:
        /* ignore writes, these are status read-only */
        break;
    case HPRE_INT_MASK:
        s->intr_mask = val;
        *reg = val;
        pcibase_update_irq(s);
        break;
    case HPRE_INT_STATUS: /* read-only, ignore writes */
        break;
    case HPRE_HAC_SOURCE_INT:
        /* Write-1-to-clear interrupt status */
        s->intr_status &= ~val;
        *reg = val;
        pcibase_update_irq(s);
        break;
    case HPRE_HAC_RAS_CE_ENB:
    case HPRE_HAC_RAS_NFE_ENB:
    case HPRE_HAC_RAS_FE_ENB:
    case HPRE_HAC_INT_SET:
    case HPRE_AM_OOO_SHUTDOWN_ENB:
    case HPRE_OOO_SHUTDOWN_SEL:
    case HPRE_CLKGATE_CTL:
    case HPRE_PEH_CFG_AUTO_GATE:
    case HPRE_CLUSTER_DYN_CTL:
    case HPRE_CORE_SHB_CFG:
        *reg = val;
        break;
    default:
        /* For all other offsets, just store the value */
        *reg = val;
        break;
    }

    /* Special handling for cluster inquiry result: 
       If the write is to an address that matches CLUSTER_INQURY offset within any cluster,
       copy the value to the corresponding CLSTR_ADDR_INQRY_RSLT register. */
    if (addr >= HPRE_CLSTR_BASE) {
        uint32_t base_offset = addr - HPRE_CLSTR_BASE;
        uint32_t cluster_idx = base_offset / HPRE_CLSTR_ADDR_INTRVL;
        uint32_t offset_in_cluster = base_offset % HPRE_CLSTR_ADDR_INTRVL;
        if (offset_in_cluster == HPRE_CLUSTER_INQURY) {
            uint32_t result_addr = HPRE_CLSTR_BASE + cluster_idx * HPRE_CLSTR_ADDR_INTRVL
                                   + HPRE_CLSTR_ADDR_INQRY_RSLT;
            s->mmio_regs[result_addr / sizeof(uint32_t)] = val;
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static uint64_t pcibase_qm_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return 0;
    }

    if (addr >= BAR2_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    val = s->qm_regs[addr / sizeof(uint32_t)];
    return val;
}

static void pcibase_qm_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return;
    }

    if (addr >= BAR2_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    s->qm_regs[addr / sizeof(uint32_t)] = val;

    /* Placeholder for future QM register side-effects */
}

static const MemoryRegionOps pcibase_qm_mmio_ops = {
    .read = pcibase_qm_mmio_read,
    .write = pcibase_qm_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    memset(s->qm_regs, 0, sizeof(s->qm_regs));
    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_HUAWEI);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_HUAWEI_HPRE_PF);
    /* Co-processor */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0b40); 
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x10); /* Bumped to 0x10 to bypass potential QM_HW_V1 check */

    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Setup MSI */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;

    /* BAR0: HPRE MMIO region */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops,
                          s, "hpre-mmio", HPRE_MMIO_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR2: QM MMIO region */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_qm_mmio_ops,
                          s, "hpre-qm-mmio", BAR2_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "hisi_hpre_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(mmio_regs, PCIBaseState, HPRE_MMIO_REG_COUNT),
        VMSTATE_UINT32_ARRAY(qm_regs, PCIBaseState, QM_REG_COUNT),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
        VMSTATE_BOOL(has_msi, PCIBaseState),
        VMSTATE_BOOL(has_msix, PCIBaseState),
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
