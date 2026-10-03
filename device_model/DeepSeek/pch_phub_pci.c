/*
 * QEMU PCI device model for pch_phub
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
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x8801
#define CLASS_ID 0xFF00

/* Register offsets */
#define PHUB_STATUS                     0x00
#define PHUB_CONTROL                    0x04
#define PHUB_TIMEOUT                    0x05
#define PCH_PHUB_ROM_WRITE_ENABLE       0x01
#define PCH_PHUB_ROM_WRITE_DISABLE      0x00
#define PCH_PHUB_MAC_START_ADDR_EG20T   0x14
#define PCH_PHUB_MAC_START_ADDR_ML7223  0x20C
#define PCH_PHUB_ROM_START_ADDR_EG20T   0x80
#define PCH_PHUB_ROM_START_ADDR_ML7213  0x400
#define PCH_PHUB_ROM_START_ADDR_ML7223  0x400
#define PCH_PHUB_OROM_SIZE              15360
#define NUM_INT_REDUCE_CONTROL          128

/* Clock configuration register (0x500) bit definitions */
#define CLKCFG_REG_OFFSET               0x500
#define CLKCFG_CAN_50MHZ                0x12000000
#define CLKCFG_CANCLK_MASK              0xFF000000
#define CLKCFG_UART_48MHZ               (1 << 16)
#define CLKCFG_BAUDDIV                  (2 << 20)
#define CLKCFG_PLL2VCO                  (8 << 9)
#define CLKCFG_UARTCLKSEL               (1 << 18)
#define CLKCFG_UART_MASK                0xFFFFFF
#define CLKCFG_UART_25MHZ               (2 << 16)

/* Register layout structure. Packed to ensure exact offsets. */
typedef struct __attribute__((packed)) {
    uint32_t phub_id_reg;                     /* 0x0000 */
    uint32_t q_pri_val_reg;                   /* 0x0004 */
    uint32_t rc_q_maxsize_reg;                /* 0x0008 */
    uint32_t bri_q_maxsize_reg;               /* 0x000C */
    uint32_t comp_resp_timeout_reg;           /* 0x0010 */
    uint32_t bus_slave_control_reg;           /* 0x0014 */
    uint32_t deadlock_avoid_type_reg;         /* 0x0018 */
    uint32_t pad_001C;
    uint32_t intpin_reg_wpermit_reg0;         /* 0x0020 */
    uint32_t intpin_reg_wpermit_reg1;         /* 0x0024 */
    uint32_t intpin_reg_wpermit_reg2;         /* 0x0028 */
    uint32_t intpin_reg_wpermit_reg3;         /* 0x002C */
    uint32_t pad_0030[4];                     /* 0x0030-0x003C */
    uint32_t int_reduce_control_reg[NUM_INT_REDUCE_CONTROL]; /* 0x0040 */
    uint32_t pad_0240[176];                   /* 0x0240-0x04FF */
    uint32_t clkcfg_reg;                      /* 0x500 */
    uint32_t pad_0504;                        /* 0x504 */
    uint32_t funcsel_reg;                     /* 0x508 */
} PhubRegs;

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
    PhubRegs regs;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return 0;
    }

    if (addr < sizeof(PhubRegs)) {
        val = *(uint32_t *)((uint8_t *)&s->regs + addr);
    } else {
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    if (addr < sizeof(PhubRegs)) {
        *(uint32_t *)((uint8_t *)&s->regs + addr) = (uint32_t)val;
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

    memset(&s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x4000, .name = "phub-mmio" };
  
    for (int i = 0; i < 6; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }

    /* Ensure struct is packed correctly */
    QEMU_BUILD_BUG_ON(offsetof(PhubRegs, bus_slave_control_reg) != 0x14);
    QEMU_BUILD_BUG_ON(offsetof(PhubRegs, int_reduce_control_reg) != 0x40);
    QEMU_BUILD_BUG_ON(offsetof(PhubRegs, clkcfg_reg) != 0x500);
    QEMU_BUILD_BUG_ON(offsetof(PhubRegs, funcsel_reg) != 0x508);
}

static void pcibase_uninit(PCIDevice *pdev)
{
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
