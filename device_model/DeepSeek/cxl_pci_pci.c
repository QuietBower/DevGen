/*
 * QEMU CXL Type-3 Memory Device Emulation
 * Based on Linux driver cxl_pci.c
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

#define TYPE_PCIBASE_DEVICE "cxl_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs from driver's id_table (first entry) */
#define VENDOR_ID 0x1057
#define DEVICE_ID 0x01
#define PCI_CLASS_MEMORY_CXL 0x0502
#define CXL_MEMORY_PROGIF 0x10
#define CLASS_ID 0x050210

/* Register offsets extracted from driver */
#define CXLMDEV_STATUS_OFFSET 0x0
#define CXLDEV_MBOX_CAPS_OFFSET 0x00
#define CXLDEV_MBOX_CTRL_OFFSET 0x04
#define CXLDEV_MBOX_CMD_OFFSET 0x08
#define CXLDEV_MBOX_STATUS_OFFSET 0x10
#define CXLDEV_MBOX_BG_CMD_STATUS_OFFSET 0x18
#define CXLDEV_MBOX_PAYLOAD_OFFSET 0x20
#define CXLDEV_DEV_EVENT_STATUS_OFFSET 0x00

/* RAS offsets */
#define CXL_RAS_UNCORRECTABLE_STATUS_OFFSET 0x0
#define CXL_RAS_CORRECTABLE_STATUS_OFFSET 0xC
#define CXL_RAS_CAP_CONTROL_OFFSET 0x14
#define CXL_RAS_HEADER_LOG_OFFSET 0x18

/* Capability IDs */
#define CXLDEV_CAP_CAP_ID_DEVICE_STATUS 0x1
#define CXLDEV_CAP_CAP_ID_PRIMARY_MAILBOX 0x2
#define CXLDEV_CAP_CAP_ID_SECONDARY_MAILBOX 0x3
#define CM_CAP_HDR_CAP_ID 1
#define CXL_CM_CAP_CAP_ID_RAS 0x2
#define CXL_CM_CAP_CAP_ID_HDM 0x5
#define CXLDEV_CAP_CAP_ID_MEMDEV 0x4000

/* Masks and bits */
#define CXLDEV_MBOX_CTRL_DOORBELL BIT(0)
#define CXLDEV_MBOX_CTRL_BG_CMD_IRQ BIT(2)
#define CXLMDEV_MBOX_IF_READY BIT(4)
#define CXLDEV_MBOX_CAP_BG_CMD_IRQ BIT(6)
#define CXLMDEV_DEV_FATAL BIT(0)
#define CXLMDEV_FW_HALT BIT(1)
#define CXLDEV_EVENT_STATUS_INFO BIT(0)
#define CXLDEV_EVENT_STATUS_WARN BIT(1)
#define CXLDEV_EVENT_STATUS_FAIL BIT(2)
#define CXLDEV_EVENT_STATUS_FATAL BIT(3)
#define CXLDEV_EVENT_STATUS_ALL (CXLDEV_EVENT_STATUS_INFO | \
                                 CXLDEV_EVENT_STATUS_WARN | \
                                 CXLDEV_EVENT_STATUS_FAIL | \
                                 CXLDEV_EVENT_STATUS_FATAL)

/* Mailbox command opcodes */
enum cxl_opcode {
    CXL_MBOX_OP_INVALID               = 0x0000,
    CXL_MBOX_OP_RAW                   = CXL_MBOX_OP_INVALID,
    CXL_MBOX_OP_GET_EVENT_RECORD      = 0x0100,
    CXL_MBOX_OP_CLEAR_EVENT_RECORD    = 0x0101,
    CXL_MBOX_OP_GET_EVT_INT_POLICY    = 0x0102,
    CXL_MBOX_OP_SET_EVT_INT_POLICY    = 0x0103,
    CXL_MBOX_OP_GET_FW_INFO           = 0x0200,
    CXL_MBOX_OP_TRANSFER_FW           = 0x0201,
    CXL_MBOX_OP_ACTIVATE_FW           = 0x0202,
    CXL_MBOX_OP_GET_TIMESTAMP         = 0x0300,
    CXL_MBOX_OP_SET_TIMESTAMP         = 0x0301,
    CXL_MBOX_OP_GET_SUPPORTED_LOGS    = 0x0400,
    CXL_MBOX_OP_GET_LOG               = 0x0401,
    CXL_MBOX_OP_GET_LOG_CAPS          = 0x0402,
    CXL_MBOX_OP_CLEAR_LOG             = 0x0403,
    CXL_MBOX_OP_GET_SUP_LOG_SUBLIST   = 0x0405,
    CXL_MBOX_OP_GET_SUPPORTED_FEATURES = 0x0500,
    CXL_MBOX_OP_GET_FEATURE           = 0x0501,
    CXL_MBOX_OP_SET_FEATURE           = 0x0502,
    CXL_MBOX_OP_DO_MAINTENANCE        = 0x0600,
    CXL_MBOX_OP_IDENTIFY              = 0x4000,
    CXL_MBOX_OP_GET_PARTITION_INFO    = 0x4100,
    CXL_MBOX_OP_SET_PARTITION_INFO    = 0x4101,
    CXL_MBOX_OP_GET_LSA               = 0x4102,
    CXL_MBOX_OP_SET_LSA               = 0x4103,
    CXL_MBOX_OP_GET_HEALTH_INFO       = 0x4200,
    CXL_MBOX_OP_GET_ALERT_CONFIG      = 0x4201,
    CXL_MBOX_OP_SET_ALERT_CONFIG      = 0x4202,
    CXL_MBOX_OP_GET_SHUTDOWN_STATE    = 0x4203,
    CXL_MBOX_OP_SET_SHUTDOWN_STATE    = 0x4204,
    CXL_MBOX_OP_GET_POISON            = 0x4300,
    CXL_MBOX_OP_INJECT_POISON         = 0x4301,
    CXL_MBOX_OP_CLEAR_POISON          = 0x4302,
    CXL_MBOX_OP_GET_SCAN_MEDIA_CAPS   = 0x4303,
    CXL_MBOX_OP_SCAN_MEDIA            = 0x4304,
    CXL_MBOX_OP_GET_SCAN_MEDIA        = 0x4305,
    CXL_MBOX_OP_SANITIZE              = 0x4400,
    CXL_MBOX_OP_SECURE_ERASE          = 0x4401,
    CXL_MBOX_OP_GET_SECURITY_STATE    = 0x4500,
    CXL_MBOX_OP_SET_PASSPHRASE        = 0x4501,
    CXL_MBOX_OP_DISABLE_PASSPHRASE    = 0x4502,
    CXL_MBOX_OP_UNLOCK                = 0x4503,
    CXL_MBOX_OP_FREEZE_SECURITY       = 0x4504,
    CXL_MBOX_OP_PASSPHRASE_SECURE_ERASE = 0x4505,
    CXL_MBOX_OP_MAX                   = 0x10000
};

/* CXL register block identifiers (from spec) */
#define CXL_REGLOC_RBI_COMPONENT 1
#define CXL_REGLOC_RBI_MEMDEV    3

/* Mailbox return codes */
#define CXL_MBOX_CMD_RC_SUCCESS      0x0000
#define CXL_MBOX_CMD_RC_BACKGROUND   0x0200

/* Field helper macros */
#define FIELD_PREP(_mask, _val) (((_mask) << (ffsll(_mask) - 1)) & (_mask))
#define FIELD_GET(_mask, _reg)  (((_reg) & (_mask)) >> (ffsll(_mask) - 1))

/* Mbox CMD register masks */
#define CXLDEV_MBOX_CMD_COMMAND_OPCODE_MASK    0xffff000000000000ULL
#define CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK    0x000fffff00000000ULL

/* Mbox Status register mask */
#define CXLDEV_MBOX_STATUS_RET_CODE_MASK       0x0000ffff00000000ULL

/* BG CMD Status register masks */
#define CXLDEV_MBOX_BG_CMD_COMMAND_OPCODE_MASK 0xffff000000000000ULL
#define CXLDEV_MBOX_BG_CMD_COMMAND_PCT_MASK    0x0000ffff00000000ULL
#define CXLDEV_MBOX_BG_CMD_COMMAND_RC_MASK     0x000000000000ffffULL

/* Mbox Capabilities masks */
#define CXLDEV_MBOX_CAP_PAYLOAD_SIZE_MASK      0x000000000000001fULL
#define CXLDEV_MBOX_CAP_IRQ_MSGNUM_MASK        0x0000000000000700ULL

/* Event interrupt policy masks */
#define CXLDEV_EVENT_INT_MODE_MASK  0x3
#define CXLDEV_EVENT_INT_MSGNUM_MASK 0x000000f0
#define CXL_INT_FW                 0
#define CXL_INT_MSI_MSIX           1

/* BAR layout */
#define MEMDEV_BASE  0x0000
#define MBOX_BASE    0x1000
#define CM_BASE      0x2000
#define BAR0_SIZE    0x10000

#define MSIX_BAR_NR  1
#define MSIX_BAR_SIZE 0x10000

/* Identify payload size (CXL 2.0 Type 3) */
#define IDENTIFY_PAYLOAD_SIZE 0x42

#define PCI_CAP_ID_DSN 0x03

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    MemoryRegion msix_region;

    bool has_msi;
    bool has_msix;
    uint32_t irq_status;

    /* Hardware register shadows */
    uint8_t mmio[BAR0_SIZE];

    /* Mailbox state */
    bool mbox_doorbell;
};

static void process_mailbox_command(PCIBaseState *s)
{
    uint64_t cmd_reg = ldq_le_p(&s->mmio[MBOX_BASE + CXLDEV_MBOX_CMD_OFFSET]);
    uint16_t opcode = FIELD_GET(CXLDEV_MBOX_CMD_COMMAND_OPCODE_MASK, cmd_reg);
    uint32_t payload_len = FIELD_GET(CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK, cmd_reg);
    uint8_t *payload = &s->mmio[MBOX_BASE + CXLDEV_MBOX_PAYLOAD_OFFSET];
    uint64_t status_reg = 0;
    uint32_t out_len = 0;
    bool bg = false;

    switch (opcode) {
    case CXL_MBOX_OP_GET_EVT_INT_POLICY:
        /* Return firmware-controlled for all event types */
        stl_le_p(payload, 0); /* info_settings = CXL_INT_FW */
        out_len = 4;
        break;
    case CXL_MBOX_OP_SET_EVT_INT_POLICY:
        out_len = 0;
        break;
    case CXL_MBOX_OP_IDENTIFY:
        /* Provide a minimal valid identify response */
        memset(payload, 0, IDENTIFY_PAYLOAD_SIZE);
        stw_le_p(payload + 0x00, 0x0200); /* FW Revision */
        stq_le_p(payload + 0x08, 128 * MiB); /* Total capacity */
        stq_le_p(payload + 0x10, 128 * MiB); /* Volatile capacity */
        stq_le_p(payload + 0x18, 0); /* Persistent capacity */
        stl_le_p(payload + 0x20, 1); /* LSA Size */
        stb_p(payload + 0x2E, 2); /* Supported memory types */
        out_len = IDENTIFY_PAYLOAD_SIZE;
        break;
    case CXL_MBOX_OP_GET_PARTITION_INFO:
        memset(payload, 0, 8);
        stq_le_p(payload, 128 * MiB); /* Active volatile */
        out_len = 8;
        break;
    case CXL_MBOX_OP_SET_TIMESTAMP:
        out_len = 0;
        break;
    case CXL_MBOX_OP_GET_HEALTH_INFO:
        memset(payload, 0, 32);
        out_len = 32;
        break;
    case CXL_MBOX_OP_GET_POISON:
        /* return empty poison list */
        memset(payload, 0, payload_len);
        out_len = 0;
        break;
    default:
        /* unsupported command, return error */
        status_reg = FIELD_PREP(CXLDEV_MBOX_STATUS_RET_CODE_MASK, 0x0003); /* Invalid opcode */
        goto done;
    }

    /* Success */
    status_reg = FIELD_PREP(CXLDEV_MBOX_STATUS_RET_CODE_MASK, CXL_MBOX_CMD_RC_SUCCESS);
    if (bg) {
        status_reg = FIELD_PREP(CXLDEV_MBOX_STATUS_RET_CODE_MASK, CXL_MBOX_CMD_RC_BACKGROUND);
        /* Simulate immediate background completion */
        stq_le_p(&s->mmio[MBOX_BASE + CXLDEV_MBOX_BG_CMD_STATUS_OFFSET],
                 FIELD_PREP(CXLDEV_MBOX_BG_CMD_COMMAND_PCT_MASK, 100));
    }
    /* Write output length back to CMD register */
    cmd_reg &= ~CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK;
    cmd_reg |= FIELD_PREP(CXLDEV_MBOX_CMD_PAYLOAD_LENGTH_MASK, out_len);
    stq_le_p(&s->mmio[MBOX_BASE + CXLDEV_MBOX_CMD_OFFSET], cmd_reg);

done:
    stq_le_p(&s->mmio[MBOX_BASE + CXLDEV_MBOX_STATUS_OFFSET], status_reg);
    s->mbox_doorbell = false;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint8_t *ptr;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }

    ptr = &s->mmio[addr];
    switch (size) {
    case 1: return ldub_p(ptr);
    case 2: return lduw_le_p(ptr);
    case 4: return ldl_le_p(ptr);
    case 8: return ldq_le_p(ptr);
    default: return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    uint8_t *ptr;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: 0x%" HWADDR_PRIx "\n", __func__, addr);
        return;
    }

    ptr = &s->mmio[addr];
    switch (size) {
    case 1: stb_p(ptr, val); break;
    case 2: stw_le_p(ptr, val); break;
    case 4: stl_le_p(ptr, val); break;
    case 8: stq_le_p(ptr, val); break;
    default: break;
    }

    /* Handle mailbox doorbell */
    if (addr >= MBOX_BASE && addr < MBOX_BASE + 0x1000) {
        hwaddr mbox_addr = addr - MBOX_BASE;
        if (mbox_addr == CXLDEV_MBOX_CTRL_OFFSET) {
            uint32_t ctrl = ldl_le_p(&s->mmio[addr]);
            if (ctrl & CXLDEV_MBOX_CTRL_DOORBELL) {
                if (!s->mbox_doorbell) {
                    s->mbox_doorbell = true;
                    process_mailbox_command(s);
                }
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
    memset(s->mmio, 0, sizeof(s->mmio));

    /* Set device status to ready */
    stq_le_p(&s->mmio[MEMDEV_BASE + CXLMDEV_STATUS_OFFSET], CXLMDEV_MBOX_IF_READY);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, int index, int type, hwaddr size, const char *name, Error **errp)
{
    hwaddr aligned_size = pow2ceil(size);
    MemoryRegion *mr = &s->bar_regions[index];

    if (type == PCI_BASE_ADDRESS_SPACE_MEMORY) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, name, aligned_size);
    } else {
        error_setg(errp, "unsupported BAR type");
        return;
    }
    pci_register_bar(pdev, index, type, mr);
}

static int add_cxl_dvsec(PCIDevice *pdev, Error **errp)
{
    /* Add CXL Device DVSEC with register locator entries */
    uint8_t *conf = pdev->config;
    int pos;

    pos = pcie_add_capability(pdev, PCI_EXT_CAP_ID_DVSEC, 0, 0, 36);
    if (pos < 0) return pos;

    /* DVSEC Header */
    stw_le_p(conf + pos + 4, 0x0000); /* DVSEC Vendor ID (CXL) */
    stw_le_p(conf + pos + 6, 0x0000); /* DVSEC ID (CXL Device) */
    stw_le_p(conf + pos + 8, 0x000b); /* DVSEC rev */
    /* Register Locator Offset (10h) */
    stb_p(conf + pos + 12, 0x0a); /* number of register block entries (2) */

    /* Register block entry 0: MEMDEV (mailbox) */
    int entry0_off = pos + 16;
    stq_le_p(conf + entry0_off, 0); /* Register Offset Low (0) */
    stl_le_p(conf + entry0_off + 8, (CXL_REGLOC_RBI_MEMDEV << 8) | MBOX_BASE); /* RBI + Offset High */

    /* Register block entry 1: COMPONENT */
    int entry1_off = pos + 24;
    stq_le_p(conf + entry1_off, 0);
    stl_le_p(conf + entry1_off + 8, (CXL_REGLOC_RBI_COMPONENT << 8) | CM_BASE);

    return 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int pos;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_config_set_class(pci_conf, CLASS_ID);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, CXL_MEMORY_PROGIF);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Add DSN capability */
    pos = pci_add_capability(pdev, PCI_CAP_ID_DSN, 0, 8, errp);
    if (pos > 0) {
        stq_le_p(pci_conf + pos + 4, 0x0000000000000001);
    }

    /* Add MSI-X capability */
    s->has_msix = true;
    memory_region_init_ram(&s->msix_region, OBJECT(s), "cxl-msix", MSIX_BAR_SIZE, errp);
    msix_init(pdev, 16, &s->msix_region, MSIX_BAR_NR, 0, &s->msix_region, MSIX_BAR_NR, 0x1000, 0, errp);

    /* Also enable MSI for fallback */
    if (msi_init(pdev, 0, 16, true, false, errp)) {
        /* ignore if failed, MSI-X will be used */
    }

    /* Add CXL DVSEC */
    if (add_cxl_dvsec(pdev, errp) < 0) {
        return;
    }

    /* BAR0: CXL register space */
    pcibase_register_bar(pdev, s, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, BAR0_SIZE, "cxl-mmio", errp);
    /* BAR1: MSI-X */
    pci_register_bar(pdev, MSIX_BAR_NR, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_region);

    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->msix_region, &s->msix_region);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "cxl_pci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(mmio, PCIBaseState, BAR0_SIZE),
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
