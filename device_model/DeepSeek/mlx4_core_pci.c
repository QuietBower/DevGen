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
#include "hw/pci/pci_ids.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "mlx4_core_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x15b3
#define DEVICE_ID 0x6340

/* Missing defines, will be provided in needed_sources */
#define MLX4_OWNER_BASE   0x8069c
#define MLX4_OWNER_SIZE   4
#define MLX4_HCR_BASE     0x1000
#define MLX4_HCR_SIZE     0x80
#define HCR_INPARAM_OFFSET   0x00
#define HCR_OUTPARAM_OFFSET  0x04
#define HCR_CMD_OFFSET       0x08
#define HCR_STATUS_OFFSET    0x0c
#define HCR_GO_OFFSET        0x10
#define HCR_CLEAR_OFFSET     0x14

/* Command opcodes – placeholder values */
#define CMD_QUERY_FW          0
#define CMD_QUERY_DEV_CAP     1
#define CMD_INIT_HCA          2
#define CMD_QUERY_HCA         3
#define CMD_QUERY_ADAPTER     4
#define CMD_CLOSE_HCA         5
#define CMD_SET_ICM_SIZE      6
#define CMD_MAP_ICM_AUX       7
#define CMD_MAP_FA            8
#define CMD_RUN_FW            9
#define CMD_UNMAP_FA          10
#define CMD_UNMAP_ICM_AUX     11
#define CMD_NOP               12

/* Fake command response structs – sizes guessed */
typedef struct {
    uint8_t data[1024];
} mlx4_dev_cap_t;

typedef struct {
    uint8_t data[512];
} mlx4_init_hca_param_t;

typedef struct {
    uint8_t data[64];
} mlx4_adapter_t;

/* New structs from driver source */
typedef struct {
    uint8_t  link_state;
    uint8_t  supported_port_types;
    uint8_t  suggested_type;
    uint8_t  default_sense;
    uint8_t  log_max_macs;
    uint8_t  log_max_vlans;
    int      ib_mtu;
    int      max_port_width;
    int      max_vl;
    int      max_tc_eth;
    int      max_gids;
    int      max_pkeys;
    uint64_t def_mac;
    uint16_t eth_mtu;
    int      trans_type;
    int      vendor_oui;
    uint16_t wavelength;
    uint64_t trans_code;
    uint8_t  dmfs_optimized_state;
} mlx4_port_cap_t;

typedef struct {
    uint16_t num_rates;
    uint8_t  min_unit;
    uint16_t min_val;
    uint8_t  max_unit;
    uint16_t max_val;
} mlx4_rate_limit_caps_t;

/* BAR type definitions */
typedef enum {
    BAR_TYPE_NONE = -1,
    BAR_TYPE_MMIO = 0
} BARType;

typedef struct BARInfo {
    uint8_t index;
    BARType type;
    hwaddr size;
    const char *name;
} BARInfo;

/* Hardware Register Structures */
typedef struct RegStru {
    uint32_t hcr_inparam;
    uint32_t hcr_outparam;
    uint32_t hcr_cmd;
    uint32_t hcr_status;
    uint32_t ownership;
} RegStru;

typedef struct StatusStru {
    bool cmd_in_progress;
    uint32_t current_cmd;
} StatusStru;

typedef struct PowManStru {
    uint8_t pm_state; /* D0/D3 */
} PowManStru;

/* Complete device state */
struct PCIBaseState {
    PCIDevice parent_obj;

    RegStru shadow;
    StatusStru status;
    PowManStru pm;
    uint32_t intr_status;
    uint32_t intr_mask;

    int num_bars;
    BARInfo bar_info[2];
    MemoryRegion bar_regions[2];
    bool has_msi;
    bool has_msix;
};

/* MMIO Read/Write Handlers */
static const MemoryRegionOps pcibase_mmio_ops;

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (addr >= MLX4_OWNER_BASE && addr < MLX4_OWNER_BASE + MLX4_OWNER_SIZE) {
        val = s->shadow.ownership;
    } else if (addr >= MLX4_HCR_BASE && addr < MLX4_HCR_BASE + MLX4_HCR_SIZE) {
        hwaddr off = addr - MLX4_HCR_BASE;
        switch (off) {
        case HCR_INPARAM_OFFSET:
            val = s->shadow.hcr_inparam;
            break;
        case HCR_OUTPARAM_OFFSET:
            val = s->shadow.hcr_outparam;
            break;
        case HCR_CMD_OFFSET:
            val = s->shadow.hcr_cmd;
            break;
        case HCR_STATUS_OFFSET:
            val = s->shadow.hcr_status;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: unimplemented HCR read at 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
            break;
        }
    }
    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= MLX4_OWNER_BASE && addr < MLX4_OWNER_BASE + MLX4_OWNER_SIZE) {
        s->shadow.ownership = val;
        return;
    }
    if (addr >= MLX4_HCR_BASE && addr < MLX4_HCR_BASE + MLX4_HCR_SIZE) {
        hwaddr off = addr - MLX4_HCR_BASE;
        switch (off) {
        case HCR_INPARAM_OFFSET:
            s->shadow.hcr_inparam = val;
            break;
        case HCR_OUTPARAM_OFFSET:
            s->shadow.hcr_outparam = val;
            break;
        case HCR_CMD_OFFSET:
            s->shadow.hcr_cmd = val;
            break;
        case HCR_STATUS_OFFSET:
            s->shadow.hcr_status = val;
            break;
        case HCR_GO_OFFSET:
            /* Command go bit: when written with 1, execute command */
            if (val & 1) {
                s->status.current_cmd = s->shadow.hcr_cmd;
                s->status.cmd_in_progress = true;
                /* Emulate command completion immediately */
                switch (s->status.current_cmd) {
                case CMD_QUERY_FW:
                    pci_dma_write(PCI_DEVICE(s),
                                  s->shadow.hcr_outparam,
                                  (void*)"Mlx4Fake", 8);
                    break;
                case CMD_QUERY_DEV_CAP:
                    {
                        mlx4_dev_cap_t fake_cap;
                        memset(&fake_cap, 0, sizeof(fake_cap));
                        *(uint32_t*)(fake_cap.data) = 1;
                        *(uint32_t*)(fake_cap.data + 4) = 0x1000;
                        *(uint32_t*)(fake_cap.data + 8) = 0;
                        *(uint32_t*)(fake_cap.data + 12) = 0;
                        *(uint32_t*)(fake_cap.data + 16) = 4096;
                        pci_dma_write(PCI_DEVICE(s),
                                      s->shadow.hcr_outparam,
                                      &fake_cap, sizeof(fake_cap));
                    }
                    break;
                case CMD_INIT_HCA:
                    break;
                case CMD_QUERY_HCA:
                    {
                        mlx4_init_hca_param_t fake_param;
                        memset(&fake_param, 0, sizeof(fake_param));
                        pci_dma_write(PCI_DEVICE(s),
                                      s->shadow.hcr_outparam,
                                      &fake_param, sizeof(fake_param));
                    }
                    break;
                case CMD_QUERY_ADAPTER:
                    {
                        mlx4_adapter_t fake_adapter;
                        memset(&fake_adapter, 0, sizeof(fake_adapter));
                        pci_dma_write(PCI_DEVICE(s),
                                      s->shadow.hcr_outparam,
                                      &fake_adapter, sizeof(fake_adapter));
                    }
                    break;
                default:
                    qemu_log_mask(LOG_UNIMP, "Unhandled command %d\n",
                                  s->status.current_cmd);
                    break;
                }
                s->shadow.hcr_status = 0;
                s->status.cmd_in_progress = false;
                s->intr_status |= 1;
                pcibase_update_irq(s);
            }
            break;
        case HCR_CLEAR_OFFSET:
            s->intr_status &= ~val;
            pcibase_update_irq(s);
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: unimplemented HCR write at 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
            break;
        }
        return;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Reset function */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(&s->shadow, 0, sizeof(s->shadow));
    s->shadow.ownership = 1;
    s->intr_status = 0;
    s->intr_mask = 1;
    s->status.cmd_in_progress = false;
}

/* BAR registration helper */
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
    }
}

/* Realize function */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    /* Set network class: base 0x02, subclass 0x00, prog-if 0x00 */
    pci_set_byte(pci_conf + 0x0b, 0x02);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 1 * MiB;
    s->bar_info[0].name = "bar0";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 1 * MiB;
    s->bar_info[1].name = "bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization: support 2 vectors */
    if (msix_init(pdev, 2, &s->bar_regions[0], 0, 0, &s->bar_regions[0], 0, 0x1000, 0, NULL)) {
        if (msi_init(pdev, 0, 2, true, false, errp)) {
            /* fallback to INTx, already configured */
        } else {
            s->has_msi = true;
        }
    } else {
        s->has_msix = true;
    }

    pcibase_reset(DEVICE(pdev));
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mlx4_core_pci",
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
type_init(pcibase_register_types)
