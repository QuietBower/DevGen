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

#define TYPE_PCIBASE_DEVICE "megaraid_legacy_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define REG_INDOOR      0x20
#define REG_OUTDOOR     0x2C
#define MBOX_PORT0      0x04
#define MBOX_PORT1      0x05
#define MBOX_PORT2      0x06
#define MBOX_PORT3      0x07
#define INTR_PORT       0x0a
#define CMD_PORT        0x00
#define ACK_PORT        0x00

/* Supplementary constants from driver source */
#define ENABLE_MBOX_BYTE       0x00
#define ENABLE_MBOX_REGION     0x0B
#define FC_NEW_CONFIG          0xA1
#define NC_SUBOP_ENQUIRY3      0x0F
#define ENQ3_GET_SOLICITED_FULL 0x02
#define MEGA_MBOXCMD_ADPEXTINQ 0x04
#define NC_SUBOP_PRODUCT_INFO  0x0E

/* Mailbox offsets */
#define MBOX_OUT_CMD_OFFSET          0
#define MBOX_OUT_CMDID_OFFSET        1
#define MBOX_OUT_SUBOP_OFFSET        2
#define MBOX_OUT_XFERADDR_OFFSET     8
#define MBOX_IN_BUSY_OFFSET         15
#define MBOX_IN_NUMSTATUS_OFFSET    16
#define MBOX_IN_STATUS_OFFSET       17
#define MBOX_IN_COMPLETED_OFFSET    18
#define MBOX_IN_POLL_OFFSET         64
#define MBOX_IN_ACK_OFFSET          65
#define MBOX_SIZE                   66

/* Magic value for OUTDOOR signal */
#define OUTDOOR_MAGIC               0x10001234U

/* Additional defines from supplementary driver source */
#define MAX_NOTIFY_SIZE            0x80
#define CUR_NOTIFY_SIZE            sizeof(mraid_notify_t)
#define FC_MAX_PHYSICAL_DEVICES    256
#define MAX_LOGICAL_DRIVES_40LD    40

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
    uint8_t intr_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t indoor;
        uint32_t outdoor;
    } mmio_regs;

    /* DMA Context */
    /* DMA fields will be filled later */

    /* Operational status flags */
    uint32_t status;

    /* State used to handle reset sequences */
    int reset_in_progress;

    /* Additional hardware state: mailbox data, 64-bit addressing flag, etc. */
    struct {
        uint8_t raw_mbox[66];
        bool has_64bit_addr;
    } extra;

    /* Command processing state */
    dma_addr_t current_mbox_addr;
    bool cmd_pending;
};

/* Mailbox structures from driver source */
typedef struct {
    uint8_t busy;
    uint8_t numstatus;
    uint8_t status;
    uint8_t completed[46];
    uint8_t poll;
    uint8_t ack;
} mbox_in;

typedef struct {
    uint8_t cmd;
    uint8_t cmdid;
    uint16_t numsectors;
    uint32_t lba;
    uint32_t xferaddr;
    uint8_t logdrv;
    uint8_t numsgelements;
    uint8_t resvd;
} mbox_out;

typedef struct {
    mbox_out m_out;
    mbox_in  m_in;
} __attribute__((packed)) mbox_t;

typedef struct {
    uint32_t xfer_segment_lo;
    uint32_t xfer_segment_hi;
    mbox_t mbox;
} __attribute__((packed)) mbox64_t;

/* Structures from supplementary driver source */
struct notify {
    uint32_t global_counter;

    uint8_t param_counter;
    uint8_t param_id;
    uint16_t param_val;

    uint8_t write_config_counter;
    uint8_t write_config_rsvd[3];

    uint8_t ldrv_op_counter;
    uint8_t ldrv_opid;
    uint8_t ldrv_opcmd;
    uint8_t ldrv_opstatus;

    uint8_t ldrv_state_counter;
    uint8_t ldrv_state_id;
    uint8_t ldrv_state_new;
    uint8_t ldrv_state_old;

    uint8_t pdrv_state_counter;
    uint8_t pdrv_state_id;
    uint8_t pdrv_state_new;
    uint8_t pdrv_state_old;

    uint8_t pdrv_fmt_counter;
    uint8_t pdrv_fmt_id;
    uint8_t pdrv_fmt_val;
    uint8_t pdrv_fmt_rsvd;

    uint8_t targ_xfer_counter;
    uint8_t targ_xfer_id;
    uint8_t targ_xfer_val;
    uint8_t targ_xfer_rsvd;

    uint8_t fcloop_id_chg_counter;
    uint8_t fcloopid_pdrvid;
    uint8_t fcloop_id0;
    uint8_t fcloop_id1;

    uint8_t fcloop_state_counter;
    uint8_t fcloop_state0;
    uint8_t fcloop_state1;
    uint8_t fcloop_state_rsvd;
};

typedef struct notify mraid_notify_t;

typedef struct {
	uint32_t data_size; /* current size in bytes (not including resvd) */

	struct notify notify;

	uint8_t notify_rsvd[MAX_NOTIFY_SIZE - CUR_NOTIFY_SIZE];

	uint8_t rebuild_rate;		/* Rebuild rate (0% - 100%) */
	uint8_t cache_flush_interval;	/* In terms of Seconds */
	uint8_t sense_alert;
	uint8_t drive_insert_count;	/* drive insertion count */

	uint8_t battery_status;
	uint8_t num_ldrv;		/* No. of Log Drives configured */
	uint8_t recon_state[MAX_LOGICAL_DRIVES_40LD / 8];	/* State of
							   reconstruct */
	uint16_t ldrv_op_status[MAX_LOGICAL_DRIVES_40LD / 8]; /* logdrv
								 Status */

	uint32_t ldrv_size[MAX_LOGICAL_DRIVES_40LD];/* Size of each log drv */
	uint8_t ldrv_prop[MAX_LOGICAL_DRIVES_40LD];
	uint8_t ldrv_state[MAX_LOGICAL_DRIVES_40LD];/* State of log drives */
	uint8_t pdrv_state[FC_MAX_PHYSICAL_DEVICES];/* State of phys drvs. */
	uint16_t pdrv_format[FC_MAX_PHYSICAL_DEVICES / 16];

	uint8_t targ_xfer[80];	/* phys device transfer rate */
	uint8_t pad1k[263];	/* 761 + 263reserved = 1024 bytes total size */
} __attribute__ ((packed)) mega_inquiry3;

typedef struct {
	uint32_t data_size; /* current size in bytes (not including resvd) */

	uint32_t config_signature;
		/* Current value is 0x00282008
		 * 0x28=MAX_LOGICAL_DRIVES,
		 * 0x20=Number of stripes and
		 * 0x08=Number of spans */

	uint8_t fw_version[16];		/* printable ASCI string */
	uint8_t bios_version[16];	/* printable ASCI string */
	uint8_t product_name[80];	/* printable ASCI string */

	uint8_t max_commands;		/* Max. concurrent commands supported */
	uint8_t nchannels;		/* Number of SCSI Channels detected */
	uint8_t fc_loop_present;	/* Number of Fibre Loops detected */
	uint8_t mem_type;		/* EDO, FPM, SDRAM etc */

	uint32_t signature;
	uint16_t dram_size;		/* In terms of MB */
	uint16_t subsysid;

	uint16_t subsysvid;
	uint8_t notify_counters;
	uint8_t pad1k[889];		/* 135 + 889 resvd = 1024 total size */
} __attribute__ ((packed)) mega_product_info;

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->mmio_regs.outdoor == OUTDOOR_MAGIC) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns - not used, deleted */

static void pcibase_process_command(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    dma_addr_t mbox_addr = s->current_mbox_addr;
    uint8_t raw_mbox[MBOX_SIZE];
    uint8_t cmd, cmdid, subop;
    uint32_t xferaddr;
    int ret;

    /* Read mailbox from guest memory */
    ret = pci_dma_read(pdev, mbox_addr, raw_mbox, MBOX_SIZE);
    if (ret) {
        qemu_log("megaraid: DMA read failed for mailbox\n");
        return;
    }

    cmd = raw_mbox[MBOX_OUT_CMD_OFFSET];
    cmdid = raw_mbox[MBOX_OUT_CMDID_OFFSET];
    subop = raw_mbox[MBOX_OUT_SUBOP_OFFSET];
    memcpy(&xferaddr, raw_mbox + MBOX_OUT_XFERADDR_OFFSET, sizeof(xferaddr));
    xferaddr = le32_to_cpu(xferaddr);

    switch (cmd) {
    case FC_NEW_CONFIG:
        if (subop == NC_SUBOP_ENQUIRY3) {
            /* Handle ENQUIRY3 command */
            mega_inquiry3 inq;
            memset(&inq, 0, sizeof(inq));

            inq.data_size = cpu_to_le32(sizeof(inq));
            inq.num_ldrv = 1;
            inq.ldrv_size[0] = cpu_to_le32(0x10000);  /* example size */
            inq.ldrv_state[0] = 0x03;                  /* optimal */
            inq.ldrv_prop[0] = 0x01;
            inq.rebuild_rate = 0;
            inq.cache_flush_interval = 4;
            /* remaining fields left zero */

            pci_dma_write(pdev, xferaddr, &inq, sizeof(inq));

            /* Write response into mailbox */
            raw_mbox[MBOX_IN_BUSY_OFFSET] = 0;
            raw_mbox[MBOX_IN_NUMSTATUS_OFFSET] = 1;
            raw_mbox[MBOX_IN_STATUS_OFFSET] = 0;
            raw_mbox[MBOX_IN_COMPLETED_OFFSET] = cmdid;
            raw_mbox[MBOX_IN_POLL_OFFSET] = 0x77;
            raw_mbox[MBOX_IN_ACK_OFFSET] = 0;
            pci_dma_write(pdev, mbox_addr, raw_mbox, MBOX_SIZE);

            /* Signal completion */
            s->mmio_regs.outdoor = OUTDOOR_MAGIC;
            pcibase_update_irq(s);
        } else if (subop == NC_SUBOP_PRODUCT_INFO) {
            /* Handle product info command */
            mega_product_info prod;
            memset(&prod, 0, sizeof(prod));

            prod.data_size = cpu_to_le32(sizeof(prod));
            prod.config_signature = cpu_to_le32(0x00282008);
            memcpy(prod.fw_version, "1.00", 4);
            memcpy(prod.bios_version, "1.00", 4);
            memcpy(prod.product_name, "MegaRAID Legacy", 16);
            prod.max_commands = 128;
            prod.nchannels = 1;
            prod.fc_loop_present = 0;
            prod.mem_type = 1;
            prod.signature = cpu_to_le32(0x12345678);
            prod.dram_size = cpu_to_le16(64);
            prod.subsysid = 0;
            prod.subsysvid = 0;
            prod.notify_counters = 0;

            pci_dma_write(pdev, xferaddr, &prod, sizeof(prod));

            raw_mbox[MBOX_IN_BUSY_OFFSET] = 0;
            raw_mbox[MBOX_IN_NUMSTATUS_OFFSET] = 1;
            raw_mbox[MBOX_IN_STATUS_OFFSET] = 0;
            raw_mbox[MBOX_IN_COMPLETED_OFFSET] = cmdid;
            raw_mbox[MBOX_IN_POLL_OFFSET] = 0x77;
            raw_mbox[MBOX_IN_ACK_OFFSET] = 0;
            pci_dma_write(pdev, mbox_addr, raw_mbox, MBOX_SIZE);

            s->mmio_regs.outdoor = OUTDOOR_MAGIC;
            pcibase_update_irq(s);
        }
        break;
    case MEGA_MBOXCMD_ADPEXTINQ:
        /* Handle ADPEXTINQ command (alternative inquiry) */
        /* Similar to above; fill appropriate structure if needed */
        break;
    default:
        qemu_log("megaraid: unhandled command 0x%x\n", cmd);
        break;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_INDOOR:
        val = s->mmio_regs.indoor;
        break;
    case REG_OUTDOOR:
        val = s->mmio_regs.outdoor;
        break;
    default:
        qemu_log("megaraid: MMIO read from unknown addr 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_INDOOR:
        if (val & 0x1) {
            /* Command submission */
            s->current_mbox_addr = val & ~0x1ULL;
            s->cmd_pending = true;
            pcibase_process_command(s);
        } else if (val & 0x2) {
            /* Acknowledge: clear OUTDOOR and lower interrupt */
            s->mmio_regs.outdoor = 0;
            pcibase_update_irq(s);
            s->mmio_regs.indoor &= ~0x2;
        } else {
            /* Storing other bits if needed */
            s->mmio_regs.indoor = val;
        }
        break;
    case REG_OUTDOOR:
        /* Driver writes to OUTDOOR to clear it */
        s->mmio_regs.outdoor = 0;
        pcibase_update_irq(s);
        break;
    default:
        qemu_log("megaraid: MMIO write to unknown addr 0x%" HWADDR_PRIx " val 0x%" PRIx64 "\n", addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* PIO read logic: not used in memory-mapped mode, but kept for compatibility */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* PIO write logic: not used in memory-mapped mode */
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

    /* Reset logic */
    s->mmio_regs.indoor = 0;
    s->mmio_regs.outdoor = 0;
    s->current_mbox_addr = 0;
    s->cmd_pending = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x101E );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x9010 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0104 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization - BAR0 as 256-byte MMIO region */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X init skipped (legacy IRQ only) */
    /* DMA config skipped */
    /* Timer config skipped */
    /* Final state initialization */
    memset(&s->extra, 0, sizeof(s->extra));
    s->mmio_regs.indoor = 0;
    s->mmio_regs.outdoor = 0;
    s->intr_status = 0;
    s->status = 0;
    s->reset_in_progress = 0;
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

    /* Uninit logic: free resources */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "megaraid_legacy_pci",
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
