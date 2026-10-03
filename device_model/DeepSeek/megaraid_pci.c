/*
 * QEMU MegaRAID Mailbox PCI Device Model
 * Based on driver megaraid_mbox.c
 * Phase 2: Behavioral Implementation (Iteration 1)
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

typedef enum BARType {
    BAR_TYPE_NONE,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct BARInfo {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
} BARInfo;

#define TYPE_PCIBASE_DEVICE "megaraid_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs from driver */
#define PCI_VENDOR_ID_DELL 0x1028
#define PCI_DEVICE_ID_PERC4_DI_DISCOVERY 0x000E
#define PCI_SUBSYS_ID_PERC4_DI_DISCOVERY 0x0123
#define PCI_CLASS_STORAGE_RAID 0x0104

/* Register offsets from megaraid_mbox driver */
#define MBOX_WRINDOOR_OFFSET  0x20
#define MBOX_RDOUTDOOR_OFFSET 0x2C

/* BAR0 size: driver ioremaps 128 bytes */
#define BAR0_SIZE 128

/* Interrupt status magic value from driver */
#define MEGARAID_INTR_MAGIC 0x10001234

/* Mailbox-related constants from driver */
#define MBOX_MAX_FIRMWARE_STATUS  46
#define MBOX_MAX_SCSI_CMDS        128
#define MBOX_MAX_PHYSICAL_DRIVES  (MAX_MBOX_CHANNELS * MAX_MBOX_TARGET)
#define MBOX_MAX_SG_SIZE          32
#define MBOX_DEFAULT_SG_SIZE      26
#define MBOX_IBUF_SIZE            4096
#define MBOX_RESET_WAIT           180
#define MBOX_RESET_EXT_WAIT       120
#define MBOX_SYNC_WAIT_CNT        0xFFFF
#define MBOX_SYNC_DELAY_200       200
#define MBOX_TIMEOUT              30

/* Mailbox command opcodes */
#define FC_NEW_CONFIG         0xA1
#define NC_SUBOP_PRODUCT_INFO 0x0E
#define NC_SUBOP_ENQUIRY3     0x0F
#define FLUSH_ADAPTER         0x0A
#define FLUSH_SYSTEM          0xFE
#define MAIN_MISC_OPCODE      0xA4
#define SUPPORT_EXT_CDB       0x16
#define GET_TARGET_ID         0x7D
#define FC_DEL_LOGDRV         0xA4
#define OP_SUP_DEL_LOGDRV     0x2A
#define GET_MAX_SG_SUPPORT    0x01
#define CHNL_CLASS            0xA9
#define GET_CHNL_CLASS        0x00
#define CLUSTER_CMD           0x6E
#define RESERVATION_STATUS    0x04
#define RESERVE_LD            0x01
#define RELEASE_LD            0x02
#define RESET_RESERVATIONS    0x03
#define MBOXCMD_PASSTHRU      0x03
#define MBOXCMD_PASSTHRU64    0xC3
#define MBOXCMD_EXTPTHRU      0xE3
#define MBOXCMD_LREAD64       0xA7
#define MBOXCMD_LWRITE64      0xA8

/* PCI device IDs from supplementary source */
#define PCI_DEVICE_ID_VERDE                 0x0407
#define PCI_DEVICE_ID_DOBSON                0x0408
#define PCI_DEVICE_ID_LINDSAY               0x0409
#define PCI_DEVICE_ID_PERC4_DI_EVERGLADES   0x000F
#define PCI_DEVICE_ID_PERC4E_DI_KOBUK       0x0013
#define PCI_VENDOR_ID_AMI                   0x101E
#define PCI_DEVICE_ID_AMI_MEGARAID3         0x1960
#define PCI_SUBSYS_ID_CERC_ATA100_4CH       0x0511

/* Mailbox structures */
typedef struct {
    uint8_t     cmd;
    uint8_t     cmdid;
    uint16_t    numsectors;
    uint32_t    lba;
    uint32_t    xferaddr;
    uint8_t     logdrv;
    uint8_t     numsge;
    uint8_t     resvd;
    uint8_t     busy;
    uint8_t     numstatus;
    uint8_t     status;
    uint8_t     completed[MBOX_MAX_FIRMWARE_STATUS];
    uint8_t     poll;
    uint8_t     ack;
} __attribute__ ((packed)) mbox_t;

typedef struct {
    uint32_t    xferaddr_lo;
    uint32_t    xferaddr_hi;
    mbox_t      mbox32;
} __attribute__ ((packed)) mbox64_t;

typedef struct {
    uint32_t    data_size;
    uint32_t    config_signature;
    uint8_t     fw_version[16];
    uint8_t     bios_version[16];
    uint8_t     product_name[80];
    uint8_t     max_commands;
    uint8_t     nchannels;
    uint8_t     fc_loop_present;
    uint8_t     mem_type;
    uint32_t    signature;
    uint16_t    dram_size;
    uint16_t    subsysid;
    uint16_t    subsysvid;
    uint8_t     notify_counters;
    uint8_t     pad1k[889];
} __attribute__ ((packed)) mraid_pinfo_t;

typedef struct {
    uint32_t    data_size;
    uint32_t    reserved; /* placeholder for mraid_notify_t */
    uint8_t     notify_rsvd[0]; /* placeholder: depends on MAX_NOTIFY_SIZE and CUR_NOTIFY_SIZE */
    uint8_t     rebuild_rate;
    uint8_t     cache_flush_int;
    uint8_t     sense_alert;
    uint8_t     drive_insert_count;
    uint8_t     battery_status;
    uint8_t     num_ldrv;
    uint8_t     recon_state[5]; /* MAX_LOGICAL_DRIVES_40LD / 8 = 40/8 = 5 */
    uint16_t    ldrv_op_status[5];
    uint32_t    ldrv_size[40];     /* MAX_LOGICAL_DRIVES_40LD */
    uint8_t     ldrv_prop[40];
    uint8_t     ldrv_state[40];
    uint8_t     pdrv_state[240];   /* FC_MAX_PHYSICAL_DEVICES = MAX_MBOX_CHANNELS*MAX_MBOX_TARGET */
    uint16_t    pdrv_format[15];
    uint8_t     targ_xfer[80];
    uint8_t     pad1k[263];
} __attribute__ ((packed)) mraid_inquiry3_t;

typedef struct {
    uint8_t     timeout         :3;
    uint8_t     ars             :1;
    uint8_t     reserved        :3;
    uint8_t     islogical       :1;
    uint8_t     logdrv;
    uint8_t     channel;
    uint8_t     target;
    uint8_t     queuetag;
    uint8_t     queueaction;
    uint8_t     cdb[10];
    uint8_t     cdblen;
    uint8_t     reqsenselen;
    uint8_t     reqsensearea[0]; /* MAX_REQ_SENSE_LEN unknown, use flexible array */
    uint8_t     numsge;
    uint8_t     scsistatus;
    uint32_t    dataxferaddr;
    uint32_t    dataxferlen;
} __attribute__ ((packed)) mraid_passthru_t;

typedef struct {
    uint8_t     timeout         :3;
    uint8_t     ars             :1;
    uint8_t     rsvd1           :1;
    uint8_t     cd_rom          :1;
    uint8_t     rsvd2           :1;
    uint8_t     islogical       :1;
    uint8_t     logdrv;
    uint8_t     channel;
    uint8_t     target;
    uint8_t     queuetag;
    uint8_t     queueaction;
    uint8_t     cdblen;
    uint8_t     rsvd3;
    uint8_t     cdb[16];
    uint8_t     numsge;
    uint8_t     status;
    uint8_t     reqsenselen;
    uint8_t     reqsensearea[0];
    uint8_t     rsvd4;
    uint32_t    dataxferaddr;
    uint32_t    dataxferlen;
} __attribute__ ((packed)) mraid_epassthru_t;

typedef struct {
    uint64_t    address;
    uint32_t    length;
} __attribute__ ((packed)) mbox_sgl64;

typedef struct {
    uint32_t    address;
    uint32_t    length;
} __attribute__ ((packed)) mbox_sgl32;

/* Driver's internal structures (opaque, for reference) */
typedef struct scb_t scb_t;
typedef struct mbox_ccb_t mbox_ccb_t;
typedef struct uioc_t uioc_t;

typedef struct mraid_hba_info {
    uint16_t    pci_vendor_id;
    uint16_t    pci_device_id;
    uint16_t    subsys_vendor_id;
    uint16_t    subsys_device_id;
    uint64_t    baseport;
    uint8_t     pci_bus;
    uint8_t     pci_dev_fn;
    uint8_t     pci_slot;
    uint8_t     irq;
    uint32_t    unique_id;
    uint32_t    host_no;
    uint8_t     num_ldrv;
} __attribute__ ((aligned(256), packed)) mraid_hba_info_t;

/* QEMU device state */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Interrupt State */
    uint32_t intr_status;   /* Mirror of outbound doorbell */

    /* Hardware Register Shadows */
    uint32_t reg_in_doorbell;   /* Not directly used */
    uint32_t reg_out_doorbell;

    /* DMA Context */
    dma_addr_t mbox_dma_addr;   /* Address written to WRINDOOR */

    /* Operational status flags */
    uint32_t status;
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status == MEGARAID_INTR_MAGIC) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO read handler: only known register is RDOUTDOOR */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    switch (addr) {
    case MBOX_RDOUTDOOR_OFFSET:
        val = s->intr_status;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "megaraid: unimplemented MMIO read at 0x%"HWADDR_PRIx"\n", addr);
        break;
    }
    return val;
}

/* MMIO write handler: WRINDOOR triggers command processing, RDOUTDOOR clears interrupt */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    switch (addr) {
    case MBOX_WRINDOOR_OFFSET:
        /* Bit 0 indicates command pending, rest is DMA address of mailbox */
        if (val & 0x1) {
            s->mbox_dma_addr = val & ~0x1ULL;
            /* Read the mailbox from DMA */
            mbox_t mbox;
            pci_dma_read(pdev, s->mbox_dma_addr, &mbox, sizeof(mbox));
            qemu_log_mask(LOG_UNIMP, "megaraid: WRINDOOR cmd=0x%x\n", mbox.cmd);
            /* Set generic success status */
            mbox.status = 0;
            /* Write back the mailbox */
            pci_dma_write(pdev, s->mbox_dma_addr, &mbox, sizeof(mbox));
            /* Signal command completion via interrupt */
            s->intr_status = MEGARAID_INTR_MAGIC;
            pcibase_update_irq(s);
        }
        break;
    case MBOX_RDOUTDOOR_OFFSET:
        /* Writing the magic value clears the interrupt */
        s->intr_status = 0;
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "megaraid: unimplemented MMIO write at 0x%"HWADDR_PRIx" val=0x%"PRIx64"\n", addr, val);
        break;
    }
}

/* Memory region ops: 32-bit accesses only, as driver uses readl/writel */
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

    /* Reset internal state */
    s->intr_status = 0;
    s->mbox_dma_addr = 0;
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_DELL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_PERC4_DI_DISCOVERY);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_RAID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_DELL);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, PCI_SUBSYS_ID_PERC4_DI_DISCOVERY);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCIe capability (driver uses conventional PCI, but we enable for validation) */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: BAR0 for MMIO */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X initialized; driver uses legacy interrupts */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No special cleanup needed beyond default PCI */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "megaraid_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT64(mbox_dma_addr, PCIBaseState),
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
