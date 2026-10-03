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

#define TYPE_PCIBASE_DEVICE "pch_phub_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PHUB_STATUS 0x00
#define PHUB_CONTROL 0x04
#define PHUB_TIMEOUT 0x05
#define PCH_PHUB_ROM_WRITE_ENABLE 0x01
#define PCH_PHUB_ROM_WRITE_DISABLE 0x00
#define PCH_PHUB_MAC_START_ADDR_EG20T 0x14
#define PCH_PHUB_MAC_START_ADDR_ML7223 0x20C
#define PCH_PHUB_ROM_START_ADDR_EG20T 0x80
#define PCH_PHUB_ROM_START_ADDR_ML7213 0x400
#define PCH_PHUB_ROM_START_ADDR_ML7223 0x400
#define MAX_NUM_INT_REDUCE_CONTROL_REG 128
#define PCI_DEVICE_ID_PCH1_PHUB 0x8801
#define PCH_MINOR_NOS 1
#define CLKCFG_CAN_50MHZ 0x12000000
#define CLKCFG_CANCLK_MASK 0xFF000000
#define CLKCFG_UART_MASK 0xFFFFFF
#define CLKCFG_UART_48MHZ (1 << 16)
#define CLKCFG_UART_25MHZ (2 << 16)
#define CLKCFG_BAUDDIV (2 << 20)
#define CLKCFG_PLL2VCO (8 << 9)
#define CLKCFG_UARTCLKSEL (1 << 18)
#define PCH_WORD_ADDR_MASK (~((1 << 2) - 1))
#define PCH_PHUB_ID_REG 0x0000
#define PCH_PHUB_QUEUE_PRI_VAL_REG 0x0004
#define PCH_PHUB_RC_QUEUE_MAXSIZE_REG 0x0008
#define PCH_PHUB_BRI_QUEUE_MAXSIZE_REG 0x000C
#define PCH_PHUB_COMP_RESP_TIMEOUT_REG 0x0010
#define PCH_PHUB_BUS_SLAVE_CONTROL_REG 0x0014
#define PCH_PHUB_DEADLOCK_AVOID_TYPE_REG 0x0018
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG0 0x0020
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG1 0x0024
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG2 0x0028
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG3 0x002C
#define PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE 0x0040
#define CLKCFG_REG_OFFSET 0x500
#define FUNCSEL_REG_OFFSET 0x508
#define PCH_PHUB_OROM_SIZE 15360

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t phub_id_reg;
    uint32_t q_pri_val_reg;
    uint32_t rc_q_maxsize_reg;
    uint32_t bri_q_maxsize_reg;
    uint32_t comp_resp_timeout_reg;
    uint32_t bus_slave_control_reg;
    uint32_t deadlock_avoid_type_reg;
    uint32_t intpin_reg_wpermit_reg0;
    uint32_t intpin_reg_wpermit_reg1;
    uint32_t intpin_reg_wpermit_reg2;
    uint32_t intpin_reg_wpermit_reg3;
    uint32_t int_reduce_control_reg[MAX_NUM_INT_REDUCE_CONTROL_REG];
    uint32_t clkcfg_reg;
    uint32_t funcsel_reg;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PCH_PHUB_ID_REG:
        val = s->phub_id_reg;
        break;
    case PCH_PHUB_QUEUE_PRI_VAL_REG:
        val = s->q_pri_val_reg;
        break;
    case PCH_PHUB_RC_QUEUE_MAXSIZE_REG:
        val = s->rc_q_maxsize_reg;
        break;
    case PCH_PHUB_BRI_QUEUE_MAXSIZE_REG:
        val = s->bri_q_maxsize_reg;
        break;
    case PCH_PHUB_COMP_RESP_TIMEOUT_REG:
        val = s->comp_resp_timeout_reg;
        break;
    case PCH_PHUB_BUS_SLAVE_CONTROL_REG:
        val = s->bus_slave_control_reg;
        break;
    case PCH_PHUB_DEADLOCK_AVOID_TYPE_REG:
        val = s->deadlock_avoid_type_reg;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG0:
        val = s->intpin_reg_wpermit_reg0;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG1:
        val = s->intpin_reg_wpermit_reg1;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG2:
        val = s->intpin_reg_wpermit_reg2;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG3:
        val = s->intpin_reg_wpermit_reg3;
        break;
    case CLKCFG_REG_OFFSET:
        val = s->clkcfg_reg;
        break;
    case FUNCSEL_REG_OFFSET:
        val = s->funcsel_reg;
        break;
    default:
        if (addr >= PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE &&
            addr < PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE + 4 * MAX_NUM_INT_REDUCE_CONTROL_REG) {
            int idx = (addr - PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE) / 4;
            val = s->int_reduce_control_reg[idx];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        }
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PCH_PHUB_ID_REG:
        s->phub_id_reg = val;
        break;
    case PCH_PHUB_QUEUE_PRI_VAL_REG:
        s->q_pri_val_reg = val;
        break;
    case PCH_PHUB_RC_QUEUE_MAXSIZE_REG:
        s->rc_q_maxsize_reg = val;
        break;
    case PCH_PHUB_BRI_QUEUE_MAXSIZE_REG:
        s->bri_q_maxsize_reg = val;
        break;
    case PCH_PHUB_COMP_RESP_TIMEOUT_REG:
        s->comp_resp_timeout_reg = val;
        break;
    case PCH_PHUB_BUS_SLAVE_CONTROL_REG:
        s->bus_slave_control_reg = val;
        break;
    case PCH_PHUB_DEADLOCK_AVOID_TYPE_REG:
        s->deadlock_avoid_type_reg = val;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG0:
        s->intpin_reg_wpermit_reg0 = val;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG1:
        s->intpin_reg_wpermit_reg1 = val;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG2:
        s->intpin_reg_wpermit_reg2 = val;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG3:
        s->intpin_reg_wpermit_reg3 = val;
        break;
    case CLKCFG_REG_OFFSET:
        s->clkcfg_reg = val;
        break;
    case FUNCSEL_REG_OFFSET:
        s->funcsel_reg = val;
        break;
    default:
        if (addr >= PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE &&
            addr < PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE + 4 * MAX_NUM_INT_REDUCE_CONTROL_REG) {
            int idx = (addr - PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE) / 4;
            s->int_reduce_control_reg[idx] = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->phub_id_reg = 0;
    s->q_pri_val_reg = 0;
    s->rc_q_maxsize_reg = 0;
    s->bri_q_maxsize_reg = 0;
    s->comp_resp_timeout_reg = 0;
    s->bus_slave_control_reg = 0;
    s->deadlock_avoid_type_reg = 0;
    s->intpin_reg_wpermit_reg0 = 0;
    s->intpin_reg_wpermit_reg1 = 0;
    s->intpin_reg_wpermit_reg2 = 0;
    s->intpin_reg_wpermit_reg3 = 0;
    s->clkcfg_reg = 0;
    s->funcsel_reg = 0;
    memset(s->int_reduce_control_reg, 0, sizeof(s->int_reduce_control_reg));
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
        /* PIO operations removed as they are not used */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8801 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
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
    s->bar_info[0] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "pch_phub_bar1" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pch_phub_pci",
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
