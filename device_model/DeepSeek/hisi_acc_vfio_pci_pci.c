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

#define TYPE_PCIBASE_DEVICE "hisi_acc_vfio_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_HUAWEI 0x19e5
#define PCI_DEVICE_ID_HUAWEI_SEC_VF 0xa256
#define PCI_CLASS_DEVICE 0x0a

#define QM_REG_ADDR_OFFSET 0x0004
#define QM_REGS_MAX_LEN 7
#define QM_SQC_VFT_NUM_MASK_V2 GENMASK(9, 0)
#define QM_SQC_VFT_NUM_SHIFT_V2 45
#define QM_SQC_VFT_BASE_MASK_V2 GENMASK(15, 0)
#define QM_SQC_VFT_BASE_SHIFT_V2 28
#define QM_XQC_ADDR_OFFSET 32U
#define QM_AEQC_PF_DW0 0x1c20
#define QM_EQC_VF_DW0 0x8000
#define QM_EQC_PF_DW0 0x1c00
#define QM_AEQC_VF_DW0 0x8020
#define QM_IFC_INT_SOURCE_V 0x0020
#define QM_PAGE_SIZE 0x0034
#define QM_IFC_INT_SET_V 0x002c
#define QM_IFC_INT_MASK 0x0024
#define QM_VF_EQ_INT_MASK 0x000c
#define QM_VF_AEQ_INT_MASK 0x0004
#define QM_QUE_ISO_CFG_V 0x0030
#define QM_VFT_CFG_DATA_H 0x100068
#define QM_VFT_CFG 0x100060
#define QM_VFT_CFG_RDY 0x10006c
#define QM_VFT_CFG_OP_ENABLE 0x100054
#define QM_VFT_CFG_TYPE 0x10005c
#define QM_VFT_CFG_DATA_L 0x100064
#define QM_VFT_CFG_OP_WR 0x100058
#define QM_CACHE_WB_DONE 0x208
#define QM_CACHE_WB_START 0x204
#define QM_MB_CMD_PAUSE_QM 0xe
#define QM_MB_CMD_NOT_READY 0xffffffff
#define QM_MATCH_SIZE offsetofend(struct acc_vf_data, qm_rsv_state)
#define QM_XQC_ADDR_LOW 0x1
#define QM_XQC_ADDR_HIGH 0x2
#define ACC_DRV_MAJOR_VER 1
#define ACC_DRV_MINOR_VER 0
#define SEC_CORE_INT_STATUS 0x301008
#define HZIP_CORE_INT_STATUS 0x3010AC
#define QM_ABNORMAL_INT_STATUS 0x100008
#define HPRE_HAC_INT_STATUS 0x301800
#define QM_IFC_INT_STATUS 0x0028
#define QM_RESET_WAIT_TIMEOUT 60000
#define QM_MIG_REGION_SIZE 0x2000
#define QM_MIG_REGION_OFFSET 0x180000
#define MB_POLL_TIMEOUT_US 1000
#define MB_POLL_PERIOD_US 10
#define CHECK_DELAY_TIME 100
#define ERROR_CHECK_TIMEOUT 100

#define QM_VF_STATE 0x60
#define QM_MB_CMD_SEND_BASE 0x300
#define QM_DOORBELL_SQ_CQ_BASE_V2 0x1000
#define QM_DOORBELL_EQ_AEQ_BASE_V2 0x2000
#define QM_MB_CMD_DATA_ADDR_L 0x304
#define QM_MB_CMD_DATA_ADDR_H 0x308
#define QM_MB_CMD_SQC_VFT_V2 0x6
#define QM_MB_CMD_SQC_BT 0x4
#define QM_MB_CMD_CQC_BT 0x5
#define QM_MB_CMD_PAUSE_QM 0xe
#define QM_DB_CMD_SHIFT_V2 12
#define QM_DB_RAND_SHIFT_V2 16
#define QM_DB_INDEX_SHIFT_V2 32
#define QM_DB_PRIORITY_SHIFT_V2 48

#define QM_BAR2_SIZE 0x200000

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

    uint32_t regs[QM_BAR2_SIZE / sizeof(uint32_t)];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: unsupported read size %u at 0x%" HWADDR_PRIx "\n", size, addr);
        return ~0ULL;
    }

    uint32_t offset = addr / sizeof(uint32_t);
    if (offset >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: read out of bounds at 0x%" HWADDR_PRIx "\n", addr);
        return ~0ULL;
    }

    val = s->regs[offset];
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: unsupported write size %u at 0x%" HWADDR_PRIx "\n", size, addr);
        return;
    }

    uint32_t offset = addr / sizeof(uint32_t);
    if (offset >= ARRAY_SIZE(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: write out of bounds at 0x%" HWADDR_PRIx "\n", addr);
        return;
    }

    if (addr == QM_MB_CMD_SEND_BASE) {
        s->regs[offset] = (uint32_t)val;
        s->regs[offset] &= ~0xFFFF;
    } else {
        s->regs[offset] = (uint32_t)val;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));

    s->regs[QM_VF_STATE / 4] = 0x1;

    uint32_t mig_offset_word = QM_MIG_REGION_OFFSET / sizeof(uint32_t);
    s->regs[mig_offset_word] = 0xDECADEDE;
    s->regs[mig_offset_word + 1] = 0xAACCFEED;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_HUAWEI);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_HUAWEI_SEC_VF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DEVICE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Replace MSI with MSI-X to satisfy driver's MSI vector requirements */
    if (msix_init_exclusive_bar(pdev, 4, 4, errp) < 0) {
        return;
    }

    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = QM_BAR2_SIZE;
    s->bar_info[0].name = "bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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

static const VMStateDescription vmstate_pcibase = {
    .name = "hisi_acc_vfio_pci_pci",
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
