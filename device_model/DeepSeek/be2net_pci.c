/* Emulex be2net virtual device model for QEMU 8.2.10 */
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

#define TYPE_PCIBASE_DEVICE "be2net_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define BE_VENDOR_ID     0x19a2
#define BE_DEVICE_ID1    0x211

/* Register offsets and masks */
#define PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET  0xfc
#define MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK  BIT(29)
#define SLIPORT_STATUS_OFFSET    0x404
#define SLIPORT_ERROR1_OFFSET    0x40C
#define SLIPORT_ERROR2_OFFSET    0x410
#define SLIPORT_CONTROL_OFFSET   0x408
#define SLIPORT_SOFTRESET_OFFSET 0x5c
#define SLIPORT_SOFTRESET_SR_MASK 0x00000080
#define SLIPORT_SEMAPHORE_OFFSET_BEx 0xac
#define SLIPORT_SEMAPHORE_OFFSET_SH 0x94
#define DB_RQ_OFFSET               0x100
#define DB_CQ_OFFSET               0x120
#define DB_EQ_OFFSET               DB_CQ_OFFSET
#define DB_TXULP1_OFFSET           0x60
#define DB_MCCQ_OFFSET             0x140
#define MPU_MAILBOX_DB_OFFSET      0x160
#define SLI_INTF_REG_OFFSET        0x58
#define PHYSDEV_CONTROL_OFFSET     0x414
#define PCICFG_UE_STATUS_LOW       0xA0
#define PCICFG_UE_STATUS_HIGH      0xA4
#define PCICFG_UE_STATUS_LOW_MASK  0xA8
#define PCICFG_UE_STATUS_HI_MASK   0xAC
#define POST_STAGE_RECOVERABLE_ERR 0xE000
#define POST_STAGE_ARMFW_UE        0xF000
#define POST_STAGE_ARMFW_RDY       0xC000
#define POST_STAGE_FAT_LOG_START   0x0D00
#define POST_STAGE_MASK            0x0000FFFF

/* Doorbell shift/masks */
#define DB_RQ_NUM_POSTED_SHIFT     24
#define DB_RQ_RING_ID_MASK         0x3FF
#define DB_TXULP_NUM_POSTED_SHIFT  16
#define DB_TXULP_RING_ID_MASK      0x7FF
#define DB_TXULP_NUM_POSTED_MASK   0x3FFF
#define DB_EQ_RING_ID_MASK         0x1FF
#define DB_EQ_NUM_POPPED_SHIFT     16
#define DB_EQ_R2I_DLY_SHIFT        30
#define DB_EQ_REARM_SHIFT          29
#define DB_EQ_EVNT_SHIFT           10
#define DB_EQ_RING_ID_EXT_MASK_SHIFT 2
#define DB_EQ_RING_ID_EXT_MASK     0x3e00
#define DB_EQ_CLR_SHIFT            9
#define DB_CQ_REARM_SHIFT          29
#define DB_CQ_RING_ID_EXT_MASK_SHIFT 1
#define DB_CQ_RING_ID_MASK         0x3FF
#define DB_CQ_RING_ID_EXT_MASK     0x7C00
#define DB_CQ_NUM_POPPED_SHIFT     16
#define DB_MCCQ_RING_ID_MASK       0x7FF
#define DB_MCCQ_NUM_POSTED_SHIFT   16
#define MPU_MAILBOX_DB_HI_MASK     0x2
#define MPU_MAILBOX_DB_RDY_MASK    0x1

#define SLI_INTF_FAMILY_MASK       0x00000F00
#define SLI_INTF_FAMILY_SHIFT      8
#define SLI_INTF_FT_MASK           0x00000001
#define PHYSDEV_CONTROL_INP_MASK   0x40000000
#define PHYSDEV_CONTROL_FW_RESET_MASK 0x00000002
#define PHYSDEV_CONTROL_DD_MASK    0x00000004
#define SLIPORT_STATUS_ERR_MASK    0x80000000
#define SLIPORT_STATUS_RN_MASK     0x01000000
#define SLIPORT_STATUS_RDY_MASK    0x00800000
#define SLIPORT_STATUS_DIP_MASK    0x02000000
#define SLI_PORT_CONTROL_IP_MASK   0x08000000
#define MSIX_TABLE_OFFSET          0x2000

/* Additional assumed definitions */
#define PCICFG_POST_STAGE_OFFSET    0x40   /* guessed, may need actual value */
#define PCICFG_MAILBOX_ADDR_OFFSET  0x20   /* guessed, may need actual value */
#define CQE_FLAGS_VALID_MASK        0x01   /* assumed */
#define CMD_SUBSYSTEM_COMMON       0x1
#define OPCODE_COMMON_FUNCTION_RESET 61

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar0;  /* pcicfg */
    MemoryRegion bar2;  /* csr */
    MemoryRegion bar4;  /* db + msix table */
    MemoryRegion msix_table;
    MemoryRegion msix_pba;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Shadow registers */
    uint32_t sli_intf;
    uint32_t physdev_control;
    uint32_t sliport_status;
    uint32_t sliport_control;
    uint32_t sliport_error1;
    uint32_t sliport_error2;
    uint32_t sliport_sofreset;
    uint32_t membar_ctrl_int_ctrl;
    uint32_t pcicfg_ue_status_low;
    uint32_t pcicfg_ue_status_high;
    uint32_t pcicfg_ue_status_low_mask;
    uint32_t pcicfg_ue_status_hi_mask;
    uint32_t post_stage;
    uint32_t mpu_mailbox_db;

    uint64_t mailbox_addr;  /* physical address of mailbox buffer */
    bool mailbox_addr_set;
    uint32_t dev_status;
    uint32_t reset_state;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status && msix_enabled(pdev)) {
        msix_notify(pdev, 0);
    } else if (s->intr_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_mailbox_command(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t mailbox_data[32];
    uint32_t compl;
    static const uint8_t fw_init_pattern[] = {
        0xFF, 0x12, 0x34, 0xFF, 0xFF, 0x56, 0x78, 0xFF
    };

    if (!s->mailbox_addr_set) {
        return;
    }

    /* Read first 32 bytes of mailbox */
    pci_dma_read(pdev, s->mailbox_addr, mailbox_data, sizeof(mailbox_data));

    /* Check for firmware init command (8-byte magic) */
    if (memcmp(mailbox_data, fw_init_pattern, sizeof(fw_init_pattern)) == 0) {
        /* Acknowledge successful init */
    } else {
        /* For any other command, respond with success.
         * Proper opcode parsing requires WRB structure and command defines,
         * which are currently missing.
         */
    }

    /* Write completion: status=0, flags=valid */
    compl = CQE_FLAGS_VALID_MASK;  /* status 0 */
    pci_dma_write(pdev, s->mailbox_addr, &compl, sizeof(compl));
    s->mpu_mailbox_db |= MPU_MAILBOX_DB_RDY_MASK;
    s->intr_status |= 0x1; /* generic interrupt */
    pcibase_update_irq(s);
}

static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case SLIPORT_SOFTRESET_OFFSET:
        val = s->sliport_sofreset;
        break;
    case PHYSDEV_CONTROL_OFFSET:
        val = s->physdev_control;
        break;
    case PCICFG_UE_STATUS_LOW:
        val = s->pcicfg_ue_status_low;
        break;
    case PCICFG_UE_STATUS_HIGH:
        val = s->pcicfg_ue_status_high;
        break;
    case PCICFG_UE_STATUS_LOW_MASK:
        val = s->pcicfg_ue_status_low_mask;
        break;
    case PCICFG_UE_STATUS_HI_MASK:
        val = s->pcicfg_ue_status_hi_mask;
        break;
    case PCICFG_POST_STAGE_OFFSET:
        val = s->post_stage;
        break;
    case PCICFG_MAILBOX_ADDR_OFFSET:
        val = s->mailbox_addr;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown read BAR0 offset 0x%"PRIx64"\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case SLIPORT_SOFTRESET_OFFSET:
        if (val & SLIPORT_SOFTRESET_SR_MASK) {
            s->sliport_sofreset |= SLIPORT_SOFTRESET_SR_MASK;
            s->post_stage = POST_STAGE_ARMFW_RDY;
            s->sliport_status = 0;
            s->sliport_error1 = 0;
            s->sliport_error2 = 0;
            s->pcicfg_ue_status_low = 0;
            s->pcicfg_ue_status_high = 0;
        }
        break;
    case PHYSDEV_CONTROL_OFFSET:
        s->physdev_control = val;
        break;
    case PCICFG_UE_STATUS_LOW:
        s->pcicfg_ue_status_low = val;
        break;
    case PCICFG_UE_STATUS_HIGH:
        s->pcicfg_ue_status_high = val;
        break;
    case PCICFG_UE_STATUS_LOW_MASK:
        s->pcicfg_ue_status_low_mask = val;
        break;
    case PCICFG_UE_STATUS_HI_MASK:
        s->pcicfg_ue_status_hi_mask = val;
        break;
    case PCICFG_POST_STAGE_OFFSET:
        s->post_stage = val;
        break;
    case PCICFG_MAILBOX_ADDR_OFFSET:
        s->mailbox_addr = val;
        s->mailbox_addr_set = true;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown write BAR0 offset 0x%"PRIx64" val 0x%"PRIx64"\n",
                      __func__, addr, val);
        break;
    }
}

static uint64_t pcibase_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case SLIPORT_SEMAPHORE_OFFSET_BEx:
        /* BEx POST stage read via CSR */
        val = s->post_stage;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown CSR read offset 0x%"PRIx64", returning 0\n", __func__, addr);
        break;
    }
    return val;
}

static void pcibase_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* dummy */
}

static uint64_t pcibase_bar4_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case SLIPORT_STATUS_OFFSET:
        val = s->sliport_status;
        break;
    case SLIPORT_ERROR1_OFFSET:
        val = s->sliport_error1;
        break;
    case SLIPORT_ERROR2_OFFSET:
        val = s->sliport_error2;
        break;
    case SLIPORT_CONTROL_OFFSET:
        val = s->sliport_control;
        break;
    case MPU_MAILBOX_DB_OFFSET:
        val = s->mpu_mailbox_db;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown read BAR4 offset 0x%"PRIx64"\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void pcibase_bar4_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case SLIPORT_STATUS_OFFSET:
        s->sliport_status = val;
        break;
    case SLIPORT_ERROR1_OFFSET:
        s->sliport_error1 = val;
        break;
    case SLIPORT_ERROR2_OFFSET:
        s->sliport_error2 = val;
        break;
    case SLIPORT_CONTROL_OFFSET:
        s->sliport_control = val;
        break;
    case MPU_MAILBOX_DB_OFFSET:
        s->mpu_mailbox_db = val;
        if (val & MPU_MAILBOX_DB_HI_MASK) {
            /* high priority command, process immediately */
            pcibase_mailbox_command(s);
        }
        break;
    case DB_RQ_OFFSET:
    case DB_CQ_OFFSET:
    case DB_TXULP1_OFFSET:
    case DB_MCCQ_OFFSET:
        /* doorbell writes; no action needed for minimal probe */
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown write BAR4 offset 0x%"PRIx64" val 0x%"PRIx64"\n",
                      __func__, addr, val);
        break;
    }
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_bar2_ops = {
    .read = pcibase_bar2_read,
    .write = pcibase_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_bar4_ops = {
    .read = pcibase_bar4_read,
    .write = pcibase_bar4_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->sli_intf = 0x00000100; /* BE3 PF */
    s->physdev_control = 0;
    s->sliport_status = 0;
    s->sliport_control = 0;
    s->sliport_error1 = 0;
    s->sliport_error2 = 0;
    s->sliport_sofreset = 0;
    s->membar_ctrl_int_ctrl = 0;
    s->pcicfg_ue_status_low = 0;
    s->pcicfg_ue_status_high = 0;
    s->pcicfg_ue_status_low_mask = 0xFFFFFFFF;
    s->pcicfg_ue_status_hi_mask = 0xFFFFFFFF;
    s->post_stage = POST_STAGE_ARMFW_RDY;
    s->mpu_mailbox_db = 0;
    s->mailbox_addr = 0;
    s->mailbox_addr_set = false;
    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int ret;

    pci_set_word(pci_conf + PCI_VENDOR_ID, BE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, BE_DEVICE_ID1);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pdev, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* SLI_INTF register at config offset 0x58 */
    pci_set_long(pci_conf + SLI_INTF_REG_OFFSET, s->sli_intf);

    /* MEMBAR_CTRL_INT_CTRL at config offset 0xfc */
    s->membar_ctrl_int_ctrl = 0;
    pci_set_long(pci_conf + PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET, s->membar_ctrl_int_ctrl);

    memory_region_init_io(&s->bar0, OBJECT(s), &pcibase_bar0_ops, s, "be2net-pcicfg", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    memory_region_init_io(&s->bar2, OBJECT(s), &pcibase_bar2_ops, s, "be2net-csr", 0x1000);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar2);

    memory_region_init_io(&s->bar4, OBJECT(s), &pcibase_bar4_ops, s, "be2net-db", 0x4000);
    /* MSI-X table and PBA are subregions of bar4 */
    memory_region_init(&s->msix_table, OBJECT(s), "be2net-msix-table", 0x1000);
    memory_region_init(&s->msix_pba, OBJECT(s), "be2net-msix-pba", 0x1000);
    memory_region_add_subregion(&s->bar4, MSIX_TABLE_OFFSET, &s->msix_table);
    memory_region_add_subregion(&s->bar4, MSIX_TABLE_OFFSET + 0x1000, &s->msix_pba);

    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar4);

    ret = msix_init(pdev, 32, &s->msix_table, 4, MSIX_TABLE_OFFSET,
                    &s->msix_pba, 4, MSIX_TABLE_OFFSET + 0x1000, 0, errp);
    if (ret < 0) {
        return;
    }
    s->has_msix = true;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->msix_table, &s->msix_pba);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static uint32_t pcibase_pci_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;

    if (addr == SLIPORT_SEMAPHORE_OFFSET_SH) {
        val = s->post_stage;
    } else if (addr == PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET) {
        val = s->membar_ctrl_int_ctrl;
    } else {
        val = pci_default_read_config(pdev, addr, len);
    }
    return val;
}

static void pcibase_pci_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    pci_default_write_config(pdev, addr, val, len);
    if (addr == PCICFG_MEMBAR_CTRL_INT_CTRL_OFFSET) {
        s->membar_ctrl_int_ctrl = val;
        /* Host interrupt enable bit update */
        if (val & MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK) {
            s->intr_mask &= ~MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
        } else {
            s->intr_mask |= MEMBAR_CTRL_INT_CTRL_HOSTINTR_MASK;
        }
        pcibase_update_irq(s);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "be2net_pci",
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
    k->config_read = pcibase_pci_config_read;
    k->config_write = pcibase_pci_config_write;
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
