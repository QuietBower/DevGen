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

#define TYPE_PCIBASE_DEVICE "pcie_mp2_amd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define AMD_C2P_MSG0 0x10500
#define AMD_C2P_MSG1 0x10504
#define AMD_C2P_MSG2 0x10508
#define AMD_P2C_MSG3 0x1068C
#define AMD_P2C_MSG(regno) (0x10680 + ((regno) * 4))
#define AMD_C2P_MSG(regno) (0x10500 + ((regno) * 4))
#define AMD_P2C_MSG_V1(regno) (0x10500 + ((regno) * 4))
#define MAX_HID_DEVICES 7

#define V2_STATUS 0x2
#define SENSOR_DISCOVERY_STATUS_MASK (((1UL << (5 - 3 + 1)) - 1) << 3)
#define SENSOR_DISCOVERY_STATUS_SHIFT 3
#define ACEL_EN (1UL << 0)
#define GYRO_EN (1UL << 1)
#define MAGNO_EN (1UL << 2)
#define OP_EN (1UL << 15)
#define ALS_EN (1UL << 19)
#define HPD_EN (1UL << 16)
#define ACS_EN (1UL << 22)
#define HPD_IDX 16
#define ACS_IDX 22

union sfh_cmd_param {
    uint32_t ul;
    struct {
        uint32_t buf_layout : 2;
        uint32_t buf_length : 6;
        uint32_t rsvd : 24;
    } s;
};

union sfh_cmd_base {
    uint32_t ul;
    struct {
        uint32_t cmd_id : 8;
        uint32_t sensor_id : 8;
        uint32_t period : 16;
    } s;
    struct {
        uint32_t cmd_id : 4;
        uint32_t intr_disable : 1;
        uint32_t rsvd1 : 3;
        uint32_t length : 7;
        uint32_t mem_type : 1;
        uint32_t sensor_id : 8;
        uint32_t period : 8;
    } cmd_v2;
};

union cmd_response {
    uint32_t resp;
    struct {
        uint32_t status : 2;
        uint32_t out_in_c2p : 1;
        uint32_t rsvd1 : 1;
        uint32_t response : 4;
        uint32_t sub_cmd : 8;
        uint32_t sensor_id : 6;
        uint32_t rsvd2 : 10;
    } response_v2;
};

#define SENSOR_DISABLED 5

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t amd_c2p_msg0;
    uint32_t amd_c2p_msg1;
    uint32_t amd_c2p_msg2;
    uint32_t amd_p2c_msg3;
    uint32_t amd_c2p_msg[256];
    uint32_t amd_p2c_msg[256];

    /* DMA Context */
    dma_addr_t sensor_dma_addr[MAX_HID_DEVICES];

    uint32_t mp2_acs;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    pci_set_irq(pdev, 1);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x10500 && addr < 0x10500 + 256 * 4) {
        int idx = (addr - 0x10500) / 4;
        val = s->amd_c2p_msg[idx];
    } else if (addr >= 0x10680 && addr < 0x10680 + 256 * 4) {
        int idx = (addr - 0x10680) / 4;
        val = s->amd_p2c_msg[idx];
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x10500 && addr < 0x10500 + 256 * 4) {
        int idx = (addr - 0x10500) / 4;
        s->amd_c2p_msg[idx] = val;
    } else if (addr >= 0x10680 && addr < 0x10680 + 256 * 4) {
        int idx = (addr - 0x10680) / 4;
        s->amd_p2c_msg[idx] = val;
        
        /* Clear interrupt logic from amd_sfh_clear_intr_v2 */
        if (idx == 4 && val == 0) {
            pci_set_irq(PCI_DEVICE(s), 0);
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    memset(s->amd_c2p_msg, 0, sizeof(s->amd_c2p_msg));
    memset(s->amd_p2c_msg, 0, sizeof(s->amd_p2c_msg));
    
    s->mp2_acs = 0;
    s->amd_p2c_msg[3] = s->mp2_acs;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1022 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x15E4 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100000;
    s->bar_info[0].name = "amd_sfh_mmio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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

static const VMStateDescription vmstate_pcibase = {
    .name = "pcie_mp2_amd_pci",
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
