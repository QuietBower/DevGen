/*
 * This QEMU device model emulates the Promise SuperTrak EX SCSI/RAID controller
 * (vendor 0x105a, device 0x8350) based on the Linux stex.c driver.
 * It implements the MU (Message Unit) interface used by the st_shasta card type.
 * This allows the Linux driver to probe, initialize, and bind successfully.
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

#define TYPE_PCIBASE_DEVICE "stex_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor/Device IDs from driver's pci_device_id table */
#define PCI_VENDOR_ID_PROMISE 0x105a
#define STEX_DEVICE_ID_SHATA 0x8350
#define CLASS_ID PCI_CLASS_STORAGE_SCSI

/* BAR0 size (arbitrary, driver uses MMIO on BAR0) */
#define BAR0_SIZE 0x1000

/* Register offsets (derived from driver access patterns) */
enum {
    IMR0_OFFSET  = 0x00, /* Inbound Message Register 0 */
    IMR1_OFFSET  = 0x04, /* Inbound Message Register 1 */
    OMR0_OFFSET  = 0x08, /* Outbound Message Register 0 */
    OMR1_OFFSET  = 0x0C, /* Outbound Message Register 1 */
    IDBL_OFFSET  = 0x10, /* Inbound Doorbell */
    ODBL_OFFSET  = 0x14, /* Outbound Doorbell */
};

/* MU interface constants (reverse-engineered from driver behaviour) */
#define MU_HANDSHAKE_SIGNATURE          0x4d55414b   /* 'MUAK' */
#define MU_HANDSHAKE_SIGNATURE_HALF     0x4d550000   /* 'MU'   */
#define MU_INBOUND_DOORBELL_REQHEADCHANGED   (1 << 0)
#define MU_INBOUND_DOORBELL_HANDSHAKE        (1 << 1)
#define MU_INBOUND_DOORBELL_RESET            (1 << 2)
#define MU_OUTBOUND_DOORBELL_STATUSHEADCHANGED (1 << 0)
#define MU_OUTBOUND_DOORBELL_REQUEST_RESET     (1 << 1)

/* SCSI status codes */
#define SAM_STAT_GOOD      0x00
#define SRB_STATUS_SUCCESS 0x00

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

/* Hardware-defined structures from the driver */
struct ver_info {
    uint32_t major;
    uint32_t minor;
    uint32_t oem;
    uint32_t build;
    uint32_t reserved[2];
};

struct st_frame {
    uint32_t base[6];
    uint32_t rom_addr;
    struct ver_info drv_ver;
    struct ver_info bios_ver;
    uint32_t bus;
    uint32_t slot;
    uint32_t irq_level;
    uint32_t irq_vec;
    uint32_t id;
    uint32_t subid;
    uint32_t dimm_size;
    uint8_t dimm_type;
    uint8_t reserved[3];
    uint32_t channel;
    uint32_t reserved1;
};

struct handshake_frame {
    __le64 rb_phy;
    __le16 req_sz;
    __le16 req_cnt;
    __le16 status_sz;
    __le16 status_cnt;
    __le64 hosttime;
    uint8_t partner_type;
    uint8_t reserved0[7];
    __le32 partner_ver_major;
    __le32 partner_ver_minor;
    __le32 partner_ver_oem;
    __le32 partner_ver_build;
    __le32 extra_offset;
    __le32 extra_size;
    __le32 scratch_size;
    uint32_t reserved1;
};

/* Request message as defined in driver */
struct req_msg {
    __le16 tag;
    uint8_t lun;
    uint8_t target;
    uint8_t task_attr;
    uint8_t task_manage;
    uint8_t data_dir;
    uint8_t payload_sz;
    uint8_t cdb[16];
    uint32_t variable[];
};

/* Status message as defined in driver */
struct status_msg {
    __le16 tag;
    uint8_t lun;
    uint8_t target;
    uint8_t srb_status;
    uint8_t scsi_status;
    uint8_t reserved;
    uint8_t payload_sz;
    uint8_t variable[32];
};

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    struct st_frame frame;
    struct handshake_frame hs_frame;

    uint32_t mu_status;
    int out_req_cnt;
    bool reset_in_progress;
    uint8_t pm_state;
    unsigned int cardtype;
    __le32 *scratch;

    /* MMIO register shadows */
    uint32_t imr0;
    uint32_t imr1;
    uint32_t omr0;
    uint32_t omr1;
    uint32_t idbl;
    uint32_t odbl;

    /* Handshake state */
    bool handshake_done;
    dma_addr_t status_phys;
    dma_addr_t rb_phy;
    uint16_t req_sz;
    uint16_t req_cnt;
    uint16_t status_sz;
    uint16_t status_cnt;
    uint32_t sts_offset;
    uint32_t status_head;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->has_msi) {
        if (s->intr_status) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, s->intr_status ? 1 : 0);
    }
}

static void handle_handshake(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (!s->handshake_done) {
        if (s->imr0 != 0) {
            /* Phase 2: read handshake frame from DMA memory */
            struct handshake_frame frame;
            pci_dma_read(pdev, s->status_phys, &frame, sizeof(frame));
            s->rb_phy = le64_to_cpu(frame.rb_phy);
            s->req_sz = le16_to_cpu(frame.req_sz);
            s->req_cnt = le16_to_cpu(frame.req_cnt);
            s->status_sz = le16_to_cpu(frame.status_sz);
            s->status_cnt = le16_to_cpu(frame.status_cnt);
            s->sts_offset = s->req_cnt * s->req_sz;
            s->handshake_done = true;
        }
        /* Set OMR0 to handshake signature */
        s->omr0 = MU_HANDSHAKE_SIGNATURE;
        /* Provide a default queue depth via OMR1 */
        s->omr1 = MU_HANDSHAKE_SIGNATURE_HALF | 32;
    }
}

static void handle_request(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct req_msg req;
    struct status_msg sts;
    dma_addr_t req_addr, sts_addr;
    uint16_t tag;
    uint32_t head;

    /* Read request from DMA memory */
    req_addr = s->rb_phy + (s->imr0 % s->req_cnt) * s->req_sz;
    pci_dma_read(pdev, req_addr, &req, sizeof(req));
    tag = le16_to_cpu(req.tag);

    /* Build success status message */
    memset(&sts, 0, sizeof(sts));
    sts.tag = cpu_to_le16(tag);
    sts.srb_status = SRB_STATUS_SUCCESS;
    sts.scsi_status = SAM_STAT_GOOD;
    sts.payload_sz = 0;

    /* Write status to DMA memory */
    head = s->status_head;
    sts_addr = s->rb_phy + s->sts_offset + head * s->status_sz;
    pci_dma_write(pdev, sts_addr, &sts, sizeof(sts));

    /* Advance status head */
    head = (head + 1) % (s->status_cnt + 1);
    s->status_head = head;
    s->omr1 = head;

    /* Signal doorbell and raise interrupt */
    s->odbl = MU_OUTBOUND_DOORBELL_STATUSHEADCHANGED;
    s->intr_status = 1;
    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (size != 4) {
        return val;
    }

    switch (addr) {
    case IMR0_OFFSET:
        val = s->imr0;
        break;
    case IMR1_OFFSET:
        val = s->imr1;
        break;
    case OMR0_OFFSET:
        val = s->omr0;
        break;
    case OMR1_OFFSET:
        val = s->omr1;
        break;
    case IDBL_OFFSET:
        val = s->idbl;
        break;
    case ODBL_OFFSET:
        val = s->odbl;
        break;
    default:
        val = ~0ULL;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case IMR0_OFFSET:
        s->imr0 = val;
        break;
    case IMR1_OFFSET:
        s->imr1 = val;
        break;
    case OMR0_OFFSET:
        s->omr0 = val;
        break;
    case OMR1_OFFSET:
        s->omr1 = val;
        break;
    case IDBL_OFFSET:
        s->idbl = val;
        if (val & MU_INBOUND_DOORBELL_HANDSHAKE) {
            handle_handshake(s);
        }
        if (val & MU_INBOUND_DOORBELL_REQHEADCHANGED) {
            handle_request(s);
        }
        if (val & MU_INBOUND_DOORBELL_RESET) {
            /* Reset handling not needed for probe; could be added later */
        }
        break;
    case ODBL_OFFSET:
        /* Driver writes back ODBL value to clear bits */
        s->odbl &= ~val;
        if (s->odbl == 0) {
            s->intr_status = 0;
            pcibase_update_irq(s);
        }
        break;
    default:
        break;
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

    s->imr0 = s->imr1 = 0;
    s->omr0 = s->omr1 = 0;
    s->idbl = s->odbl = 0;
    s->status_phys = 0;
    s->handshake_done = false;
    s->rb_phy = 0;
    s->req_sz = s->req_cnt = 0;
    s->status_sz = s->status_cnt = 0;
    s->sts_offset = 0;
    s->status_head = 0;
    s->intr_status = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_PROMISE);
    pci_set_word(pci_conf + PCI_DEVICE_ID, STEX_DEVICE_ID_SHATA);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }
    s->has_msi = true;

    /* Initialize state */
    s->handshake_done = false;
    s->status_head = 0;
    s->intr_status = 0;
    s->status_phys = 0;
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
    .name = "stex_pci",
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
