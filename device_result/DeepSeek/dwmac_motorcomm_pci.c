#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
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

#define TYPE_PCIBASE_DEVICE "dwmac_motorcomm_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_MOTORCOMM         0x1f0a
#define PCI_DEVICE_ID_MOTORCOMM         0x6801
#define PCI_CLASS_NETWORK_ETHERNET      0x0200

#define EPHY_CTRL                       0x1004
#define EPHY_MDIO_PHY_RESET             BIT(0)
#define OOB_WOL_CTRL                    0x1010
#define OOB_WOL_CTRL_DIS                BIT(0)
#define MGMT_INT_CTRL0                  0x1100
#define INT_MODERATION                  0x1108
#define INT_MODERATION_RX               GENMASK(11, 0)
#define INT_MODERATION_TX               GENMASK(27, 16)
#define EFUSE_OP_CTRL_0                 0x1500
#define EFUSE_OP_MODE                   GENMASK(1, 0)
#define EFUSE_OP_ROW_READ               0x1
#define EFUSE_OP_START                  BIT(2)
#define EFUSE_OP_ADDR                   GENMASK(15, 8)
#define EFUSE_OP_CTRL_1                 0x1504
#define EFUSE_OP_DONE                   BIT(1)
#define EFUSE_OP_RD_DATA                GENMASK(31, 24)
#define SYS_RESET                       0x152c
#define SYS_RESET_RESET                 BIT(31)
#define GMAC_OFFSET                     0x2000

#define EFUSE_READ_TIMEOUT_US           20000
#define EFUSE_PATCH_REGION_OFFSET       18
#define EFUSE_PATCH_MAX_NUM             39
#define EFUSE_ADDR_MACA0LR              0x1520
#define EFUSE_ADDR_MACA0HR              0x1524

/* bit field manipulation macros */
#define GENMASK(high, low) (((1ULL << ((high) - (low) + 1)) - 1) << (low))
#define FIELD_EX32(val, mask) (((val) & (mask)) >> ctz32(mask))
#define FIELD_DP32(base, mask, val) (((base) & ~(mask)) | ((((unsigned)(val)) << ctz32(mask)) & (mask)))

/* BAR types and info structure */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_RAM
} BARType;

typedef struct BARInfo {
    int index;
    BARType type;
    uint32_t size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows */
    uint32_t ephy_ctrl;
    uint32_t oob_wol_ctrl;
    uint32_t mgmt_int_ctrl0;
    uint32_t int_moderation;
    uint32_t sys_reset;
    uint32_t efuse_ctrl0;
    uint32_t efuse_ctrl1;

    /* Efuse simulation data */
    uint8_t efuse_patch_bytes[18 + 6 * 3]; /* enough for 3 patches */
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case EPHY_CTRL:
        val = s->ephy_ctrl;
        break;
    case OOB_WOL_CTRL:
        val = s->oob_wol_ctrl;
        break;
    case MGMT_INT_CTRL0:
        val = s->mgmt_int_ctrl0;
        break;
    case INT_MODERATION:
        val = s->int_moderation;
        break;
    case EFUSE_OP_CTRL_0:
        val = s->efuse_ctrl0;
        break;
    case EFUSE_OP_CTRL_1:
        val = s->efuse_ctrl1;
        break;
    case SYS_RESET:
        val = s->sys_reset;
        break;
    default:
        /* Allow reads to GMAC region (>=0x2000) and others to return 0 */
        if (addr >= GMAC_OFFSET) {
            val = 0;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "read to unknown offset 0x%" HWADDR_PRIx "\n", addr);
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case EPHY_CTRL:
        s->ephy_ctrl = val;
        break;
    case OOB_WOL_CTRL:
        s->oob_wol_ctrl = val;
        break;
    case MGMT_INT_CTRL0:
        s->mgmt_int_ctrl0 = val;
        break;
    case INT_MODERATION:
        s->int_moderation = val;
        break;
    case EFUSE_OP_CTRL_0:
        s->efuse_ctrl0 = val;
        if (val & EFUSE_OP_START) {
            /* Simulate efuse read */
            uint8_t offset = FIELD_EX32(val, EFUSE_OP_ADDR);
            uint8_t byte = 0;
            if (offset >= EFUSE_PATCH_REGION_OFFSET &&
                offset < EFUSE_PATCH_REGION_OFFSET + sizeof(s->efuse_patch_bytes)) {
                byte = s->efuse_patch_bytes[offset - EFUSE_PATCH_REGION_OFFSET];
            }
            s->efuse_ctrl1 = FIELD_DP32(0, EFUSE_OP_RD_DATA, byte) | EFUSE_OP_DONE;
        }
        break;
    case EFUSE_OP_CTRL_1:
        /* Read-only, ignore write */
        break;
    case SYS_RESET:
        s->sys_reset = val;
        break;
    default:
        if (addr >= GMAC_OFFSET) {
            /* Ignore writes to GMAC region */
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "write to unknown offset 0x%" HWADDR_PRIx " with value 0x%" PRIx64 "\n", addr, val);
        }
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_MOTORCOMM);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_MOTORCOMM);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x40);
    pcie_cap_flr_init(pdev);

    /* MSI-X initialization */
    memory_region_init_ram(&s->bar_regions[1], OBJECT(s), "msix-table", 4 * KiB, errp);
    if (msix_init(pdev, 6, &s->bar_regions[1], 1, 0, &s->bar_regions[1], 1, 0x100, 0x80, errp)) {
        return;
    }
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    /* BAR0: MMIO region */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 64 * KiB;
    s->bar_info[0].name = "dwmac-motorcomm-mmio";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    s->num_bars = 2;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[1], &s->bar_regions[1]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "dwmac_motorcomm_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->ephy_ctrl = 0;
    s->oob_wol_ctrl = 0;
    s->mgmt_int_ctrl0 = 0;
    s->int_moderation = 0;
    s->sys_reset = 0;
    s->efuse_ctrl0 = 0;
    s->efuse_ctrl1 = 0;

    /* Initialize efuse patch bytes */
    memset(s->efuse_patch_bytes, 0, sizeof(s->efuse_patch_bytes));
    /* Patch 0: addr EFUSE_ADDR_MACA0LR (0x1520), data 0x22334455 (little endian) */
    s->efuse_patch_bytes[0] = 0x20;
    s->efuse_patch_bytes[1] = 0x15;
    s->efuse_patch_bytes[2] = 0x55;
    s->efuse_patch_bytes[3] = 0x44;
    s->efuse_patch_bytes[4] = 0x33;
    s->efuse_patch_bytes[5] = 0x22;
    /* Patch 1: addr EFUSE_ADDR_MACA0HR (0x1524), data 0x00000011 (little endian) */
    s->efuse_patch_bytes[6] = 0x24;
    s->efuse_patch_bytes[7] = 0x15;
    s->efuse_patch_bytes[8] = 0x11;
    s->efuse_patch_bytes[9] = 0x00;
    s->efuse_patch_bytes[10] = 0x00;
    s->efuse_patch_bytes[11] = 0x00;
    /* Patch 2: sentinel, addr=0x0000, data=0x00000000 */
    s->efuse_patch_bytes[12] = 0x00;
    s->efuse_patch_bytes[13] = 0x00;
    s->efuse_patch_bytes[14] = 0x00;
    s->efuse_patch_bytes[15] = 0x00;
    s->efuse_patch_bytes[16] = 0x00;
    s->efuse_patch_bytes[17] = 0x00;
}

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
