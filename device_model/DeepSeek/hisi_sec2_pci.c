/* This template provides a robust skeleton for hardware emulation.
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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "hisi_sec2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI IDs */
#define PCI_VENDOR_ID_HUAWEI          0x19e5
#define PCI_DEVICE_ID_HUAWEI_SEC_PF   0xa255
/* BAR sizes */
#define SEC_BAR0_SIZE                 0x400000 /* Placeholder: actual size needed */
/* Register offsets */
#define SEC_CORE_INT_STATUS            0x301008
#define SEC_CTRL_CNT_CLR_CE            0x301120
#define SEC_CTRL_CNT_CLR_CE_BIT        BIT(0)
#define SEC_CORE_INT_SOURCE            0x301010
#define SEC_CORE_INT_MASK              0x301000
#define SEC_CORE_SRAM_ECC_ERR_INFO     0x301C14
#define SEC_ECC_NUM                    16
#define SEC_ECC_MASH                   0xFF
#define SEC_CORE_INT_DISABLE           0x0
#define SEC_RAS_CE_REG                 0x301050
#define SEC_RAS_FE_REG                 0x301054
#define SEC_RAS_NFE_REG                0x301058
#define SEC_RAS_FE_ENB_MSK             0x0
#define SEC_OOO_SHUTDOWN_SEL           0x301014
#define SEC_RAS_DISABLE                0x0
#define SEC_AXI_ERROR_MASK             (BIT(0) | BIT(1))
#define SEC_MEM_START_INIT_REG         0x301100
#define SEC_MEM_INIT_DONE_REG          0x301104
#define SEC_CONTROL_REG                0x301200
#define SEC_DYNAMIC_GATE_REG           0x30121c
#define SEC_CORE_AUTO_GATE             0x30212c
#define SEC_DYNAMIC_GATE_EN            0x7fff
#define SEC_CORE_AUTO_GATE_EN          GENMASK(3, 0)
#define SEC_CLK_GATE_ENABLE            BIT(3)
#define SEC_CLK_GATE_DISABLE           (~BIT(3))
#define SEC_TRNG_EN_SHIFT              8
#define SEC_AXI_SHUTDOWN_ENABLE        BIT(12)
#define SEC_AXI_SHUTDOWN_DISABLE        0xFFFFEFFF
#define SEC_INTERFACE_USER_CTRL0_REG   0x301220
#define SEC_INTERFACE_USER_CTRL1_REG   0x301224
#define SEC_SAA_EN_REG                 0x301270
#define SEC_BD_ERR_CHK_EN_REG0         0x301380
#define SEC_BD_ERR_CHK_EN_REG1         0x301384
#define SEC_BD_ERR_CHK_EN_REG3         0x30138c
#define SEC_USER0_SMMU_NORMAL          (BIT(23) | BIT(15))
#define SEC_USER1_SMMU_NORMAL          (BIT(31) | BIT(23) | BIT(15) | BIT(7))
#define SEC_USER1_ENABLE_CONTEXT_SSV   BIT(24)
#define SEC_USER1_ENABLE_DATA_SSV      BIT(16)
#define SEC_USER1_WB_CONTEXT_SSV       BIT(8)
#define SEC_USER1_WB_DATA_SSV          BIT(0)
#define SEC_USER1_SVA_SET              (SEC_USER1_ENABLE_CONTEXT_SSV | \
					SEC_USER1_ENABLE_DATA_SSV | \
					SEC_USER1_WB_CONTEXT_SSV | \
					SEC_USER1_WB_DATA_SSV)
#define SEC_USER1_SMMU_SVA              (SEC_USER1_SMMU_NORMAL | SEC_USER1_SVA_SET)
#define SEC_USER1_SMMU_MASK             (~SEC_USER1_SVA_SET)
#define SEC_INTERFACE_USER_CTRL0_REG_V3 0x302220
#define SEC_INTERFACE_USER_CTRL1_REG_V3 0x302224
#define SEC_USER1_SMMU_NORMAL_V3        (BIT(23) | BIT(17) | BIT(11) | BIT(5))
#define SEC_USER1_SMMU_MASK_V3          0xFF79E79E
#define SEC_CORE_INT_STATUS_M_ECC       BIT(2)
#define SEC_PREFETCH_CFG                0x301130
#define SEC_SVA_TRANS                   0x301EC4
#define SEC_PREFETCH_ENABLE             (~(BIT(0) | BIT(1) | BIT(11)))
#define SEC_PREFETCH_DISABLE            BIT(1)
#define SEC_SVA_DISABLE_READY           (BIT(7) | BIT(11))
#define SEC_SVA_PREFETCH_INFO           0x301ED4
#define SEC_SVA_STALL_NUM               GENMASK(23, 8)
#define SEC_SVA_PREFETCH_NUM            GENMASK(2, 0)
#define SEC_WAIT_SVA_READY              500000
#define SEC_READ_SVA_STATUS_TIMES       3
#define SEC_WAIT_US_MIN                 10
#define SEC_WAIT_US_MAX                 20
#define SEC_WAIT_QP_US_MIN              1000
#define SEC_WAIT_QP_US_MAX              2000
#define SEC_MAX_WAIT_TIMES              2000
#define SEC_DELAY_10_US                 10
#define SEC_POLL_TIMEOUT_US             1000
#define SEC_DBGFS_VAL_MAX_LEN           20
#define SEC_SINGLE_PORT_MAX_TRANS       0x2060
#define SEC_SQE_MASK_OFFSET            16
#define SEC_SQE_MASK_LEN               108
#define SEC_SHAPER_TYPE_RATE            400
#define SEC_DFX_BASE                    0x301000
#define SEC_DFX_CORE                    0x302100
#define SEC_DFX_COMMON1                 0x301600
#define SEC_DFX_COMMON2                 0x301C00
#define SEC_DFX_BASE_LEN                0x9D
#define SEC_DFX_CORE_LEN                0x32B
#define SEC_DFX_COMMON1_LEN             0x45
#define SEC_DFX_COMMON2_LEN             0xBA
#define SEC_ALG_BITMAP_SHIFT            32
#define SEC_CIPHER_BITMAP               (GENMASK_ULL(5, 0) | GENMASK_ULL(16, 12) | \
					GENMASK_ULL(24, 21))
#define SEC_DIGEST_BITMAP               (GENMASK_ULL(11, 8) | GENMASK_ULL(20, 19) | \
					GENMASK_ULL(42, 25))
#define SEC_AEAD_BITMAP                 (GENMASK_ULL(7, 6) | GENMASK_ULL(18, 17) | \
					GENMASK_ULL(45, 43))
#define SEC_MAX_CHANNEL_NUM             1
#define SEC_MIN_BLOCK_SZ                1
#define SEC_AIV_SIZE                    12
#define SEC_IV_SIZE                     24
#define SEC_PBUF_SZ                     512
#define SEC_COMM_SCENE                  0
#define SEC_MAX_KEY_SIZE                64
#define SEC_MAX_MAC_LEN                 64
#define SEC_SGE_NR_NUM                  4

/* Additional register offsets from QM/AXI configuration */
#define QM_ARUSER_M_CFG_1               0x100088
#define ARUSER_M_CFG_ENABLE             0xfffffffe
#define QM_AWUSER_M_CFG_1               0x100098
#define AWUSER_M_CFG_ENABLE             0xfffffffe
#define QM_WUSER_M_CFG_ENABLE           0x1000a8
#define WUSER_M_CFG_ENABLE              0xffffffff
#define QM_AXI_M_CFG                    0x1000ac
#define AXI_M_CFG                       0xffff
#define AXI_M_CFG_ENABLE                0xffffffff
#define QM_AXI_M_CFG_ENABLE             0x1000b0
#define QM_PEH_AXUSER_CFG               0x1000cc
#define PEH_AXUSER_CFG                  0x401001
#define PEH_AXUSER_CFG_ENABLE           0xffffffff
#define QM_PEH_AXUSER_CFG_ENABLE        0x1000d0
#define QM_CACHE_CTL                    0x100050
#define SQC_CACHE_ENABLE                BIT(0)
#define CQC_CACHE_ENABLE                BIT(1)
#define SQC_CACHE_WB_ENABLE             BIT(4)
#define CQC_CACHE_WB_ENABLE             BIT(11)
#define SQC_CACHE_WB_THRD               GENMASK(10, 5)
#define CQC_CACHE_WB_THRD               GENMASK(17, 12)
#define AM_CFG_SINGLE_PORT_MAX_TRANS    (0x5014)
#define SEC_SQE_SIZE                    128
#define SEC_PF_DEF_Q_BASE               0
#define SEC_PF_DEF_Q_NUM                256
#define SEC_QUEUE_NUM_V1                4096
#define SEC_CTX_Q_NUM_MAX               32
#define SEC_BD_ERR_CHK_EN0              0xEFFFFFFF
#define SEC_BD_ERR_CHK_EN1              0x7ffff7fd
#define SEC_BD_ERR_CHK_EN3              0xffffbfff
#define QM_SHAPER_ENABLE                BIT(30)
#define QM_ECC_MBIT                     BIT(2)

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
    uint32_t *regs;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t status = s->regs[SEC_CORE_INT_STATUS >> 2];
    uint32_t mask = s->regs[SEC_CORE_INT_MASK >> 2];
    bool level = !!(status & mask);

    if (msix_enabled(pdev)) {
        if (level) {
            msix_notify(pdev, 0);
        }
    } else if (msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= SEC_BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: addr=0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported read size %u to addr 0x%" HWADDR_PRIx "\n", __func__, size, addr);
        return 0;
    }

    uint32_t offset = addr >> 2;
    val = s->regs[offset];

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= SEC_BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: addr=0x%" HWADDR_PRIx "\n", __func__, addr);
        return;
    }

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write size %u to addr 0x%" HWADDR_PRIx "\n", __func__, size, addr);
        return;
    }

    uint32_t offset = addr >> 2;

    switch (addr) {
    case SEC_CORE_INT_SOURCE:
        /* Write-1-to-clear: clear bits in STATUS that are set in val */
        s->regs[SEC_CORE_INT_STATUS >> 2] &= ~val;
        pcibase_update_irq(s);
        break;
    case SEC_MEM_START_INIT_REG:
        s->regs[offset] = val;
        /* Trigger memory init done immediately */
        s->regs[SEC_MEM_INIT_DONE_REG >> 2] |= 1;
        break;
    case SEC_CORE_INT_MASK:
        s->regs[offset] = val;
        pcibase_update_irq(s);
        break;
    case SEC_RAS_CE_REG:
    case SEC_RAS_FE_REG:
    case SEC_RAS_NFE_REG:
        s->regs[offset] = val;
        pcibase_update_irq(s);
        break;
    default:
        s->regs[offset] = val;
        break;
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
    memset(s->regs, 0, SEC_BAR0_SIZE);
    pcibase_update_irq(s);
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
        /* Not used, but keep for completeness */
        memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_HUAWEI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_HUAWEI_SEC_PF );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x1080 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Allocate register file */
    s->regs = g_malloc0(SEC_BAR0_SIZE);
    if (!s->regs) {
        error_setg(errp, "Failed to allocate MMIO region");
        return;
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) { .index = 0, .type = BAR_TYPE_MMIO, .size = SEC_BAR0_SIZE, .name = "sec-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X capability allocation */
    int msix_cap_pos = pci_add_capability(pdev, PCI_CAP_ID_MSIX, 0, 12, errp);
    if (msix_cap_pos < 0) {
        error_setg(errp, "Failed to add MSI-X capability");
        g_free(s->regs);
        return;
    }

    /* MSI-X initialization: single vector, table at offset 0x0, PBA at offset 0x1000 */
    if (msix_init(pdev, 1, &s->bar_regions[0], 0, 0x00000000,
                  &s->bar_regions[0], 0, 0x00001000, msix_cap_pos, errp) < 0) {
        g_free(s->regs);
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    msix_uninit(pdev, NULL, NULL);
    g_free(s->regs);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "hisi_sec2_pci",
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
