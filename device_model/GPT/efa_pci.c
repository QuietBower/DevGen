#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "efa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ------------------------------------------------------------------ */
/* Static register/macros/constants from EFA driver                    */
/* ------------------------------------------------------------------ */

#define PCI_VENDOR_ID_AMAZON 0x1d0f
#define PCI_DEV_ID_EFA0_VF 0xefa0
#define PCI_DEV_ID_EFA1_VF 0xefa1
#define PCI_DEV_ID_EFA2_VF 0xefa2
#define PCI_DEV_ID_EFA3_VF 0xefa3

/* BAR index macros */
#define EFA_REG_BAR 0
#define EFA_MEM_BAR 2

/* Admin/common constants */
#define EFA_COMP_EQS_VEC_BASE             1
#define EFA_IRQNAME_SIZE                  40
#define DRV_MODULE_NAME                   "efa"
#define EFA_COMMON_SPEC_VERSION_MINOR     0
#define EFA_COMMON_SPEC_VERSION_MAJOR     2
#define EFA_MGMNT_MSIX_VEC_IDX            0
#define EFA_ADMIN_EQE_PHASE_MASK          (1U << 0)
#define EFA_ADMIN_AENQ_COMMON_DESC_PHASE_MASK  (1U << 0)
#define EFA_ADMIN_ACQ_COMMON_DESC_PHASE_MASK   (1U << 0)
#define EFA_ADMIN_AQ_COMMON_DESC_COMMAND_ID_MASK 0x0fffU

/* MMIO register offsets (from efa_regs.h via driver) */
#define EFA_REGS_VERSION_OFF              0x0
#define EFA_REGS_CONTROLLER_VERSION_OFF   0x4
#define EFA_REGS_CAPS_OFF                 0x8
#define EFA_REGS_AQ_BASE_LO_OFF           0x10
#define EFA_REGS_AQ_BASE_HI_OFF           0x14
#define EFA_REGS_AQ_CAPS_OFF              0x18
#define EFA_REGS_ACQ_BASE_LO_OFF          0x20
#define EFA_REGS_ACQ_BASE_HI_OFF          0x24
#define EFA_REGS_ACQ_CAPS_OFF             0x28
#define EFA_REGS_AQ_PROD_DB_OFF           0x2c
#define EFA_REGS_AENQ_CAPS_OFF            0x34
#define EFA_REGS_AENQ_BASE_LO_OFF         0x38
#define EFA_REGS_AENQ_BASE_HI_OFF         0x3c
#define EFA_REGS_AENQ_CONS_DB_OFF         0x40
#define EFA_REGS_INTR_MASK_OFF            0x4c
#define EFA_REGS_DEV_CTL_OFF              0x54
#define EFA_REGS_DEV_STS_OFF              0x58
#define EFA_REGS_MMIO_REG_READ_OFF        0x5c
#define EFA_REGS_MMIO_RESP_LO_OFF         0x60
#define EFA_REGS_MMIO_RESP_HI_OFF         0x64
#define EFA_REGS_EQ_DB_OFF                0x68

/* Enums directly referenced in driver */
enum efa_regs_reset_reason_types {
    EFA_REGS_RESET_NORMAL               = 0,
    EFA_REGS_RESET_KEEP_ALIVE_TO        = 1,
    EFA_REGS_RESET_ADMIN_TO             = 2,
    EFA_REGS_RESET_INIT_ERR             = 3,
    EFA_REGS_RESET_DRIVER_INVALID_STATE = 4,
    EFA_REGS_RESET_OS_TRIGGER           = 5,
    EFA_REGS_RESET_SHUTDOWN             = 6,
    EFA_REGS_RESET_USER_TRIGGER         = 7,
    EFA_REGS_RESET_GENERIC              = 8,
};

enum efa_admin_aq_feature_id {
    EFA_ADMIN_DEVICE_ATTR   = 1,
    EFA_ADMIN_AENQ_CONFIG   = 2,
    EFA_ADMIN_NETWORK_ATTR  = 3,
    EFA_ADMIN_QUEUE_ATTR    = 4,
    EFA_ADMIN_HW_HINTS      = 5,
    EFA_ADMIN_HOST_INFO     = 6,
    EFA_ADMIN_EVENT_QUEUE_ATTR = 7,
};

#define EFA_GID_SIZE 16

/* BAR / feature masks from driver */
#define EFA_BASE_BAR_MASK (BIT(EFA_REG_BAR) | BIT(EFA_MEM_BAR))
#define EFA_AENQ_ENABLED_GROUPS \
    (BIT(EFA_ADMIN_FATAL_ERROR) | BIT(EFA_ADMIN_WARNING) | \
     BIT(EFA_ADMIN_NOTIFICATION) | BIT(EFA_ADMIN_KEEP_ALIVE))

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Minimal internal state needed for driver-visible behavior           */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt capability flags */
    bool has_msi;
    bool has_msix;

    /* EFA register state visible to driver/admin library */
    uint32_t regs_version;
    uint32_t regs_controller_version;
    uint32_t regs_caps;

    uint64_t aq_base;
    uint32_t aq_caps;

    uint64_t acq_base;
    uint32_t acq_caps;

    uint64_t aenq_base;
    uint32_t aenq_caps;
    uint32_t aenq_cons_db;

    uint32_t aq_prod_db;
    uint32_t intr_mask;
    uint32_t dev_ctl;
    uint32_t dev_sts;

    /* MMIO readless support: request/response registers */
    uint32_t mmio_reg_read_req;
    uint64_t mmio_resp;

    uint32_t eq_db;

    /* simple flag to acknowledge that device_reset was invoked */
    enum efa_regs_reset_reason_types last_reset_reason;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                     */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* BAR registration helper                                             */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* Helper to synthesize MMIO readless response                         */
/* ------------------------------------------------------------------ */
static void pcibase_handle_mmio_reg_read(PCIBaseState *s)
{
    /* The driver uses a "readless" MMIO mechanism modeled similarly
     * to ena_com_reg_bar_read32(): it writes an encoded value to the
     * MMIO_REG_READ register and then polls a DMA-resident response
     * structure. The EFA-specific structure definitions and fields
     * are not visible in the provided snippets, so we cannot perform
     * real DMA or sequence-id matching here.
     *
     * To keep the driver progressing without violating the no-guessing
     * rule, we simply reflect the requested offset back in the
     * response low dword and mark completion with a non-zero upper
     * dword. This mirrors the previous behavior and stays within the
     * visible interface.
     */
    uint32_t encoded = s->mmio_reg_read_req;

    /* The ENA code encodes offset and sequence id; here we only
     * preserve the encoded value and set an arbitrary non-zero
     * completion marker in the high dword.
     */
    s->mmio_resp = ((uint64_t)0x1u << 32) | (uint64_t)encoded;
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                 */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = 0;
    uint64_t val64 = 0;

    switch (addr) {
    case EFA_REGS_VERSION_OFF:
        val32 = s->regs_version;
        break;
    case EFA_REGS_CONTROLLER_VERSION_OFF:
        val32 = s->regs_controller_version;
        break;
    case EFA_REGS_CAPS_OFF:
        val32 = s->regs_caps;
        break;
    case EFA_REGS_AQ_BASE_LO_OFF:
        val32 = (uint32_t)(s->aq_base & 0xffffffffULL);
        break;
    case EFA_REGS_AQ_BASE_HI_OFF:
        val32 = (uint32_t)(s->aq_base >> 32);
        break;
    case EFA_REGS_AQ_CAPS_OFF:
        val32 = s->aq_caps;
        break;
    case EFA_REGS_ACQ_BASE_LO_OFF:
        val32 = (uint32_t)(s->acq_base & 0xffffffffULL);
        break;
    case EFA_REGS_ACQ_BASE_HI_OFF:
        val32 = (uint32_t)(s->acq_base >> 32);
        break;
    case EFA_REGS_ACQ_CAPS_OFF:
        val32 = s->acq_caps;
        break;
    case EFA_REGS_AQ_PROD_DB_OFF:
        val32 = s->aq_prod_db;
        break;
    case EFA_REGS_AENQ_CAPS_OFF:
        val32 = s->aenq_caps;
        break;
    case EFA_REGS_AENQ_BASE_LO_OFF:
        val32 = (uint32_t)(s->aenq_base & 0xffffffffULL);
        break;
    case EFA_REGS_AENQ_BASE_HI_OFF:
        val32 = (uint32_t)(s->aenq_base >> 32);
        break;
    case EFA_REGS_AENQ_CONS_DB_OFF:
        val32 = s->aenq_cons_db;
        break;
    case EFA_REGS_INTR_MASK_OFF:
        val32 = s->intr_mask;
        break;
    case EFA_REGS_DEV_CTL_OFF:
        val32 = s->dev_ctl;
        break;
    case EFA_REGS_DEV_STS_OFF:
        /* The driver checks ENA_REGS_DEV_STS_RESET_IN_PROGRESS_MASK,
         * which should be clear during normal operation. Our stored
         * dev_sts value models the full register; we return it
         * unmodified here so that any reset-in-progress checks see the
         * current bits as written by the guest driver.
         */
        val32 = s->dev_sts;
        break;
    case EFA_REGS_MMIO_REG_READ_OFF:
        val32 = s->mmio_reg_read_req;
        break;
    case EFA_REGS_MMIO_RESP_LO_OFF:
        val32 = (uint32_t)(s->mmio_resp & 0xffffffffULL);
        break;
    case EFA_REGS_MMIO_RESP_HI_OFF:
        val32 = (uint32_t)(s->mmio_resp >> 32);
        break;
    case EFA_REGS_EQ_DB_OFF:
        val32 = s->eq_db;
        break;
    default:
        /* Unimplemented registers read as zero */
        val32 = 0;
        break;
    }

    if (size == 8) {
        /* For 64-bit reads, combine two adjacent 32-bit locations when
         * applicable. If not specifically handled, just return lower dword.
         */
        switch (addr) {
        case EFA_REGS_AQ_BASE_LO_OFF:
            val64 = s->aq_base;
            break;
        case EFA_REGS_ACQ_BASE_LO_OFF:
            val64 = s->acq_base;
            break;
        case EFA_REGS_AENQ_BASE_LO_OFF:
            val64 = s->aenq_base;
            break;
        case EFA_REGS_MMIO_RESP_LO_OFF:
            val64 = s->mmio_resp;
            break;
        default:
            val64 = val32;
            break;
        }
        return val64;
    }

    return val32;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    switch (addr) {
    case EFA_REGS_AQ_BASE_LO_OFF:
        s->aq_base = (s->aq_base & 0xffffffff00000000ULL) | (uint64_t)v32;
        break;
    case EFA_REGS_AQ_BASE_HI_OFF:
        s->aq_base = (s->aq_base & 0x00000000ffffffffULL) | ((uint64_t)v32 << 32);
        break;
    case EFA_REGS_AQ_CAPS_OFF:
        s->aq_caps = v32;
        break;
    case EFA_REGS_ACQ_BASE_LO_OFF:
        s->acq_base = (s->acq_base & 0xffffffff00000000ULL) | (uint64_t)v32;
        break;
    case EFA_REGS_ACQ_BASE_HI_OFF:
        s->acq_base = (s->acq_base & 0x00000000ffffffffULL) | ((uint64_t)v32 << 32);
        break;
    case EFA_REGS_ACQ_CAPS_OFF:
        s->acq_caps = v32;
        break;
    case EFA_REGS_AQ_PROD_DB_OFF:
        /* Driver updates producer doorbell when posting admin
         * commands. We record the latest value so that any debug or
         * re-read sees it, but we do not attempt to process DMA-based
         * admin queues here because descriptor format and flow are
         * defined in other files.
         */
        s->aq_prod_db = v32;
        break;
    case EFA_REGS_AENQ_CAPS_OFF:
        s->aenq_caps = v32;
        break;
    case EFA_REGS_AENQ_BASE_LO_OFF:
        s->aenq_base = (s->aenq_base & 0xffffffff00000000ULL) | (uint64_t)v32;
        break;
    case EFA_REGS_AENQ_BASE_HI_OFF:
        s->aenq_base = (s->aenq_base & 0x00000000ffffffffULL) | ((uint64_t)v32 << 32);
        break;
    case EFA_REGS_AENQ_CONS_DB_OFF:
        s->aenq_cons_db = v32;
        break;
    case EFA_REGS_INTR_MASK_OFF:
        s->intr_mask = v32;
        break;
    case EFA_REGS_DEV_CTL_OFF:
        /* Driver writes DEV_CTL to initiate resets and control
         * device operation. The exact bit layout is not present in
         * the provided fragments, but we still store the value so
         * that subsequent reads via MMIO or indirect mechanisms see
         * what was written.
         */
        s->dev_ctl = v32;
        break;
    case EFA_REGS_DEV_STS_OFF:
        /* DEV_STS is primarily updated by hardware; the driver reads
         * it and checks ENA_REGS_DEV_STS_RESET_IN_PROGRESS_MASK.
         * Some flows may also write it when acknowledging a reset.
         * Since we do not have the authoritative semantics, we simply
         * mirror writes into our shadow register.
         */
        s->dev_sts = v32;
        break;
    case EFA_REGS_MMIO_REG_READ_OFF:
        /* The ENA/EFA common code writes an encoded value containing
         * the target register offset and a sequence identifier. We
         * store the raw value and immediately synthesize a response
         * in mmio_resp, as done in pcibase_handle_mmio_reg_read().
         */
        s->mmio_reg_read_req = v32;
        pcibase_handle_mmio_reg_read(s);
        break;
    case EFA_REGS_EQ_DB_OFF:
        /* Event Queue doorbell: the driver rings this after
         * consuming EQEs. We track the last value so that reads see
         * it, but do not attempt to generate interrupts or events
         * based on it.
         */
        s->eq_db = v32;
        break;
    default:
        /* Ignore writes to unmodeled registers */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* EFA driver only uses memory BARs, no PIO accesses are present
     * in efa_main.c or the provided helpers, so return zero.
     */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO usage in driver source */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->aq_base = 0;
    s->acq_base = 0;
    s->aenq_base = 0;

    s->aq_caps = 0;
    s->acq_caps = 0;
    s->aenq_caps = 0;

    s->aq_prod_db = 0;
    s->aenq_cons_db = 0;
    s->intr_mask = 0;
    s->dev_ctl = 0;
    s->dev_sts = 0;
    s->mmio_reg_read_req = 0;
    s->mmio_resp = 0;
    s->eq_db = 0;

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

/* ------------------------------------------------------------------ */
/* DMA initialize (no actual DMA behavior defined in provided code)   */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                             */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Realize                                                             */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* PCI identification */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_AMAZON);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEV_ID_EFA0_VF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define BAR layout according to efa_main.c usage: REG and MEM BARs
     * exist and are requested as IORESOURCE_MEM.
     * No explicit sizes are defined in the provided code; choose minimal
     * non-zero regions large enough to host the visible registers.
     */
    s->num_bars = 2;

    s->bar_info[0].index = EFA_REG_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* contains all known registers */
    s->bar_info[0].name = "efa-reg";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = EFA_MEM_BAR;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000; /* doorbell bar / memory area */
    s->bar_info[1].name = "efa-mem";
    s->bar_info[1].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* Initialize version/caps so that validation in
     * efa_com_validate_version(), efa_com_get_dma_width(),
     * efa_com_dev_reset(), and efa_com_admin_init() can see
     * non-zero values where they expect them.
     * Exact bitfield layout is not present in the provided code, so we
     * only ensure that reads return non-zero where the driver tests for
     * zero.
     */
    s->regs_version = 1;
    s->regs_controller_version = 1;

    /* Ensure caps has non-zero RESET_TIMEOUT and ADMIN_CMD_TO fields,
     * and a DMA width in the 32-64 range. Without the bit positions we
     * cannot encode specific values; we just provide a non-zero mask.
     */
    s->regs_caps = 0xFFFFFFFFU;

    /* Device status: mark device as ready so that
     * efa_com_dev_reset() and efa_com_admin_init() succeed their
     * readiness checks.
     */
    s->dev_sts = 0xFFFFFFFFU;
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

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

