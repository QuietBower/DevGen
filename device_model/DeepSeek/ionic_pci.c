/*
 * Ionic Ethernet PCI device model for QEMU 8.2.10
 * Generated from Linux driver: drivers/net/ethernet/pensando/ionic/ionic_bus_pci.c
 * Target: probe with driver ionic to reach "Kernel driver in use"
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

#define TYPE_PCIBASE_DEVICE "ionic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IONIC_BARS_MAX                      6
#define IONIC_PCI_BAR_DBELL                 1
#define IONIC_BAR0_SIZE                     0x8000
#define IONIC_BAR0_INTR_CTRL_OFFSET         0x2000
#define IONIC_BAR0_DEV_CMD_REGS_OFFSET      0x0800
#define IONIC_DEV_INFO_SIGNATURE            0x44455649
#define IONIC_BAR0_DEV_INFO_REGS_OFFSET     0x0000
#define IONIC_BAR0_INTR_STATUS_OFFSET       0x1000
#define IONIC_DEV_INFO_REG_COUNT            32
#define IONIC_DEV_CMD_REG_COUNT             32
#define IONIC_DEV_CMD_REG_VERSION           1
#define IONIC_INTR_CTRL_REGS_MAX            2048
#define IONIC_INTR_CTRL_COAL_MAX            0x3F
#define IONIC_DEF_TXRX_DESC                 1024
#define IONIC_MIN_TXRX_DESC                 64
#define IONIC_DRV_NAME                      "ionic"
#define PCI_DEVICE_ID_PENSANDO_IONIC_ETH_PF 0x1002
#define PCI_DEVICE_ID_PENSANDO_IONIC_ETH_VF 0x1003
#define IONIC_DEVINFO_SERIAL_BUFLEN         32
#define IONIC_DEVINFO_FWVERS_BUFLEN         32
#define IONIC_FW_STS_F_RUNNING              0x01
#define IONIC_FW_STS_F_GENERATION           0xF0
#define PCI_VENDOR_ID_PENSANDO              0x1dd8

#define PCIBASE_MAX_INTRS                   64   /* Increased to satisfy interrupt requests */

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

struct ionic_intr {
    uint32_t coal_init;
    uint32_t mask;
    uint32_t credits;
    uint32_t mask_assert;
    uint32_t coal;
    uint32_t rsvd[3];
};

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
    uint32_t dev_info_regs[IONIC_DEV_INFO_REG_COUNT];
    uint32_t dev_cmd_regs[IONIC_DEV_CMD_REG_COUNT];
    struct ionic_intr intr_ctrl[IONIC_INTR_CTRL_REGS_MAX];
    uint32_t intr_status_regs[IONIC_INTR_CTRL_REGS_MAX];

    /* DMA Context */
    uint64_t dma_mask;

    uint8_t fw_status;
    uint8_t fw_generation;
    bool fw_status_ready;

    bool reset_in_progress;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= IONIC_BAR0_DEV_INFO_REGS_OFFSET &&
        addr < IONIC_BAR0_DEV_INFO_REGS_OFFSET + IONIC_DEV_INFO_REG_COUNT * 4) {
        int reg = (addr - IONIC_BAR0_DEV_INFO_REGS_OFFSET) >> 2;
        if (reg < IONIC_DEV_INFO_REG_COUNT) {
            val = s->dev_info_regs[reg];
        }
    } else if (addr >= IONIC_BAR0_DEV_CMD_REGS_OFFSET &&
               addr < IONIC_BAR0_DEV_CMD_REGS_OFFSET + IONIC_DEV_CMD_REG_COUNT * 4) {
        int reg = (addr - IONIC_BAR0_DEV_CMD_REGS_OFFSET) >> 2;
        if (reg < IONIC_DEV_CMD_REG_COUNT) {
            val = s->dev_cmd_regs[reg];
        }
    } else if (addr >= IONIC_BAR0_INTR_STATUS_OFFSET &&
               addr < IONIC_BAR0_INTR_CTRL_OFFSET) {
        int index = (addr - IONIC_BAR0_INTR_STATUS_OFFSET) >> 2;
        if (index < IONIC_INTR_CTRL_REGS_MAX) {
            val = s->intr_status_regs[index];
        }
    } else if (addr >= IONIC_BAR0_INTR_CTRL_OFFSET) {
        hwaddr offset = addr - IONIC_BAR0_INTR_CTRL_OFFSET;
        int intr = offset / sizeof(struct ionic_intr);
        int sub = offset % sizeof(struct ionic_intr);
        if (intr < IONIC_INTR_CTRL_REGS_MAX) {
            struct ionic_intr *intr_reg = &s->intr_ctrl[intr];
            switch (sub) {
                case offsetof(struct ionic_intr, coal_init):
                    val = intr_reg->coal_init;
                    break;
                case offsetof(struct ionic_intr, mask):
                    val = intr_reg->mask;
                    break;
                case offsetof(struct ionic_intr, credits):
                    val = intr_reg->credits;
                    break;
                case offsetof(struct ionic_intr, mask_assert):
                    val = intr_reg->mask_assert;
                    break;
                case offsetof(struct ionic_intr, coal):
                    val = intr_reg->coal;
                    break;
                default:
                    if (sub < sizeof(struct ionic_intr) - 4) {
                        /* reading reserved fields */
                        val = 0;
                    }
            }
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= IONIC_BAR0_DEV_INFO_REGS_OFFSET &&
        addr < IONIC_BAR0_DEV_INFO_REGS_OFFSET + IONIC_DEV_INFO_REG_COUNT * 4) {
        int reg = (addr - IONIC_BAR0_DEV_INFO_REGS_OFFSET) >> 2;
        if (reg < IONIC_DEV_INFO_REG_COUNT) {
            /* dev_info is read-only for the driver, but allow writes for debugging */
            s->dev_info_regs[reg] = val;
        }
    } else if (addr >= IONIC_BAR0_DEV_CMD_REGS_OFFSET &&
               addr < IONIC_BAR0_DEV_CMD_REGS_OFFSET + IONIC_DEV_CMD_REG_COUNT * 4) {
        int reg = (addr - IONIC_BAR0_DEV_CMD_REGS_OFFSET) >> 2;
        if (reg < IONIC_DEV_CMD_REG_COUNT) {
            s->dev_cmd_regs[reg] = val;
            /* Emulate command completion: any write to doorbell (reg 0) immediately completes */
            if (reg == 0) {
                s->dev_cmd_regs[1] = 1;  /* set done bit */
            }
        }
    } else if (addr >= IONIC_BAR0_INTR_STATUS_OFFSET &&
               addr < IONIC_BAR0_INTR_CTRL_OFFSET) {
        int index = (addr - IONIC_BAR0_INTR_STATUS_OFFSET) >> 2;
        if (index < IONIC_INTR_CTRL_REGS_MAX) {
            /* W1C – clear bits written with '1' */
            s->intr_status_regs[index] &= ~val;
        }
    } else if (addr >= IONIC_BAR0_INTR_CTRL_OFFSET) {
        hwaddr offset = addr - IONIC_BAR0_INTR_CTRL_OFFSET;
        int intr = offset / sizeof(struct ionic_intr);
        int sub = offset % sizeof(struct ionic_intr);
        if (intr < IONIC_INTR_CTRL_REGS_MAX) {
            struct ionic_intr *intr_reg = &s->intr_ctrl[intr];
            switch (sub) {
                case offsetof(struct ionic_intr, coal_init):
                    intr_reg->coal_init = val;
                    break;
                case offsetof(struct ionic_intr, mask):
                    intr_reg->mask = val;
                    break;
                case offsetof(struct ionic_intr, credits):
                    /* hardware credits register: lower bits are credits to add, upper bits are flags */
                    {
                        uint32_t cred = val & 0xFFFF;  /* FIXME: should use actual CREDIT_COUNT mask */
                        intr_reg->credits += cred;     /* add credits (can overflow, real HW would cap) */
                        /* flags processing would go here (e.g., reset coalesce) */
                    }
                    break;
                case offsetof(struct ionic_intr, mask_assert):
                    intr_reg->mask_assert = val;
                    break;
                case offsetof(struct ionic_intr, coal):
                    intr_reg->coal = val;
                    break;
                default:
                    /* reserved fields ignored */
                    break;
            }
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
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

/* Doorbell BAR operations */
static uint64_t pcibase_doorbell_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_doorbell_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Doorbell writes are notifications; no action needed for probing. */
}

static const MemoryRegionOps pcibase_doorbell_ops = {
    .read = pcibase_doorbell_read,
    .write = pcibase_doorbell_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize device identity registers */
    memset(s->dev_info_regs, 0, sizeof(s->dev_info_regs));
    memset(s->dev_cmd_regs, 0, sizeof(s->dev_cmd_regs));
    memset(s->intr_ctrl, 0, sizeof(s->intr_ctrl));
    memset(s->intr_status_regs, 0, sizeof(s->intr_status_regs));

    /* Identity structure (version 1):
     * reg[0] signature, reg[1] version + asic + fw_status,
     * reg[2] fw_generation, reg[3] max_intr,
     * reg[8..15] serial_num (offset 0x20), reg[16..23] fw_version (offset 0x40)
     */
    s->dev_info_regs[0] = IONIC_DEV_INFO_SIGNATURE;
    /* reg[1]: byte 0 = version (1), byte 3 = fw_status (RUNNING) */
    s->dev_info_regs[1] = 1 | (IONIC_FW_STS_F_RUNNING << 24);
    s->dev_info_regs[2] = 0;                /* fw_generation */
    s->dev_info_regs[3] = PCIBASE_MAX_INTRS; /* max_intr */

    /* Set serial number string at offset 0x20 (reg indices 8 through 15) */
    char serial_str[IONIC_DEVINFO_SERIAL_BUFLEN] = "00000000";
    memcpy(&s->dev_info_regs[8], serial_str, sizeof(serial_str));

    /* Set firmware version string at offset 0x40 (reg indices 16 through 23) */
    char fw_ver_str[IONIC_DEVINFO_FWVERS_BUFLEN] = "1.0.0";
    memcpy(&s->dev_info_regs[16], fw_ver_str, sizeof(fw_ver_str));

    s->fw_status = IONIC_FW_STS_F_RUNNING;
    s->fw_generation = 0;
    s->fw_status_ready = true;
    s->reset_in_progress = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_PENSANDO);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x1002);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Setup BARs: BAR0 for registers, BAR1 for doorbell, BAR2 for MSI-X (exclusive) */
    s->num_bars = 2;

    /* BAR 0: Main device registers */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = IONIC_BAR0_SIZE;
    s->bar_info[0].name = "ionic-bar0";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
    if (*errp) {
        return;
    }

    /* BAR 1: Doorbell page with custom ops (registered before the loop to avoid override) */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_doorbell_ops,
                          s, "ionic-doorbell", 0x1000);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    /* BAR 2: MSI-X exclusive bar (automatically sized and registered) */
    if (msix_init_exclusive_bar(pdev, PCIBASE_MAX_INTRS, 2, errp)) {
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    msix_uninit_exclusive_bar(pdev);
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ionic_pci",
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
