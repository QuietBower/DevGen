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
#include <string.h>

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "qlcnic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_QLOGIC 0x1077
#define PCI_DEVICE_ID_QLE824X 0x8020
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Register offsets from BAR0 (extracted from qlcnic_reg_tbl and driver defines) */
#define PEG_HALT_STAT1     0x1B20A8
#define PEG_HALT_STAT2     0x1B20AC
#define FW_HEARTBEAT       0x1B20B0
#define LOCK_ID            0x1B2100
#define FW_CAPABILITIES    0x1B2128
#define DRV_ACTIVE         0x1B2138
#define DEV_STATE          0x1B2140
#define DRV_STATE          0x1B2144
#define DRV_SCRATCH        0x1B2148
#define DEV_PARTITION_INFO 0x1B214C
#define DRV_IDC_VER        0x1B2174
#define FW_VERSION_MAJOR   0x1B2150
#define FW_VERSION_MINOR   0x1B2154
#define FW_VERSION_SUB     0x1B2158
#define NPAR_STATE         0x1B219C
#define FW_IMG_VALID       0x1B21FC
#define CMD_PEG_STATE      0x1B2250
#define RCV_PEG_STATE      0x1B233C
#define ASIC_TEMP          0x1B23B4
#define FW_API             0x1B216C
#define DRV_OP_MODE        0x1B2170
#define FLASH_LOCK         0x13C010
#define FLASH_UNLOCK       0x13C014

/* New driver-provided device state constants */
#define QLCNIC_DEV_COLD          0x1
#define QLCNIC_DEV_INITIALIZING  0x2
#define QLCNIC_DEV_READY         0x3
#define QLCNIC_DEV_NEED_RESET    0x4
#define QLCNIC_DEV_NEED_QUISCENT 0x5
#define QLCNIC_DEV_FAILED        0x6

#define QLCNIC_DEV_NPAR_OPER     1

/* Temperature state/value extraction macros */
#define qlcnic_get_temp_state(x) ((x) & 0xffff)
#define qlcnic_get_temp_val(x)   ((x) >> 16)

/* Initial register values for a ready device */
#define QLCNIC_FW_API_VERSION     3
#define QLCNIC_FW_VERSION_MAJOR_VAL 5
#define QLCNIC_FW_VERSION_MINOR_VAL 0
#define QLCNIC_FW_VERSION_SUB_VAL   0
/* Set ASIC_TEMP to a nominal temperature (30°C) with state 0 (normal) */
#define QLCNIC_ASIC_TEMP_NORMAL   0x001E0000

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
    uint32_t intr_status;
    uint32_t intr_mask;
    bool msix_enabled;
    bool msi_enabled;

    /* Placeholder register struct not used, kept for compatibility */
    struct {
        uint32_t peg_halt_stat1;
        uint32_t peg_halt_stat2;
        uint32_t fw_heartbeat;
        uint32_t lock_id;
        uint32_t fw_capabilities;
        uint32_t drv_active;
        uint32_t dev_state;
        uint32_t drv_state;
        uint32_t drv_scratch;
        uint32_t dev_partition_info;
        uint32_t drv_idc_ver;
        uint32_t fw_version_major;
        uint32_t fw_version_minor;
        uint32_t fw_version_sub;
        uint32_t npar_state;
        uint32_t fw_img_valid;
        uint32_t cmd_peg_state;
        uint32_t rcv_peg_state;
        uint32_t asic_temp;
        uint32_t fw_api;
        uint32_t drv_op_mode;
        uint32_t flash_lock;
        uint32_t flash_unlock;
    } regs;

    /* MMIO buffer for entire BAR0 space (2MB) */
    uint8_t mmio_buf[0x200000];
    uint32_t heartbeat;   /* incremented on each read from FW_HEARTBEAT */

    uint32_t status;      /* Operational status flags */
    uint32_t reset_state; /* State used to handle reset sequences */
    uint32_t pm_state;    /* Power management state (D0-D3) */
};

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == FW_HEARTBEAT && size == 4) {
        val = s->heartbeat++;
        return val;
    }

    if (addr + size <= sizeof(s->mmio_buf)) {
        memcpy(&val, &s->mmio_buf[addr], size);
    } else {
        val = 0xFFFFFFFF;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size <= sizeof(s->mmio_buf)) {
        memcpy(&s->mmio_buf[addr], &val, size);
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

    /* Re-initialize the MMIO buffer to starting state */
    memset(s->mmio_buf, 0, sizeof(s->mmio_buf));

    stl_le_p(s->mmio_buf + DEV_STATE, QLCNIC_DEV_READY);
    stl_le_p(s->mmio_buf + FW_API, QLCNIC_FW_API_VERSION);
    stl_le_p(s->mmio_buf + FW_VERSION_MAJOR, QLCNIC_FW_VERSION_MAJOR_VAL);
    stl_le_p(s->mmio_buf + FW_VERSION_MINOR, QLCNIC_FW_VERSION_MINOR_VAL);
    stl_le_p(s->mmio_buf + FW_VERSION_SUB, QLCNIC_FW_VERSION_SUB_VAL);
    stl_le_p(s->mmio_buf + FW_IMG_VALID, 1);
    stl_le_p(s->mmio_buf + NPAR_STATE, QLCNIC_DEV_NPAR_OPER);
    stl_le_p(s->mmio_buf + ASIC_TEMP, QLCNIC_ASIC_TEMP_NORMAL);
    s->heartbeat = 0;
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
        /* PIO not used by driver, fallback to MMIO */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1077 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8020 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x00200000; /* QLCNIC_82XX_BAR0_LENGTH */
    s->bar_info[0].name = "bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msix_init(pdev, 8, &s->bar_regions[0], 0, 0x44, &s->bar_regions[0], 0, 0xC4, 0x90, errp)) {
        if (msi_init(pdev, 0, 1, true, false, errp)) {
            /* fallback to legacy interrupt */
        }
    }

    /* Initialize MMIO buffer with default register values */
    memset(s->mmio_buf, 0, sizeof(s->mmio_buf));
    stl_le_p(s->mmio_buf + DEV_STATE, QLCNIC_DEV_READY);
    stl_le_p(s->mmio_buf + FW_API, QLCNIC_FW_API_VERSION);
    stl_le_p(s->mmio_buf + FW_VERSION_MAJOR, QLCNIC_FW_VERSION_MAJOR_VAL);
    stl_le_p(s->mmio_buf + FW_VERSION_MINOR, QLCNIC_FW_VERSION_MINOR_VAL);
    stl_le_p(s->mmio_buf + FW_VERSION_SUB, QLCNIC_FW_VERSION_SUB_VAL);
    stl_le_p(s->mmio_buf + FW_IMG_VALID, 1);
    stl_le_p(s->mmio_buf + NPAR_STATE, QLCNIC_DEV_NPAR_OPER);
    stl_le_p(s->mmio_buf + ASIC_TEMP, QLCNIC_ASIC_TEMP_NORMAL);
    s->heartbeat = 0;
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
    .name = "qlcnic_pci",
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
