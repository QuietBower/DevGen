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

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "ni_660x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NI          0x1093
#define NI660X_DEVICE_ID          0x1310
#define NI660X_CLASS_ID           0x118000

#define NI660X_CLK_CFG_COUNTER_SWAP          BIT(21)
#define NI660X_GLOBAL_INT_COUNTER0           BIT(8)
#define NI660X_GLOBAL_INT_COUNTER1           BIT(9)
#define NI660X_GLOBAL_INT_COUNTER2           BIT(10)
#define NI660X_GLOBAL_INT_COUNTER3           BIT(11)
#define NI660X_GLOBAL_INT_CASCADE            BIT(29)
#define NI660X_GLOBAL_INT_GLOBAL_POL         BIT(30)
#define NI660X_GLOBAL_INT_GLOBAL             BIT(31)
#define NI660X_DMA_CFG_SEL(_c, _s)           (((_s) & 0x1f) << (8 * (_c)))
#define NI660X_DMA_CFG_SEL_MASK(_c)          NI660X_DMA_CFG_SEL((_c), 0x1f)
#define NI660X_DMA_CFG_SEL_NONE(_c)          NI660X_DMA_CFG_SEL((_c), 0x1f)
#define NI660X_DMA_CFG_RESET(_c)             NI660X_DMA_CFG_SEL((_c), 0x80)
#define NI660X_IO_CFG(x)                     (NI660X_IO_CFG_0_1 + ((x) / 2))
#define NI660X_IO_CFG_OUT_SEL(_c, _s)        (((_s) & 0x3) << (((_c) % 2) ? 0 : 8))
#define NI660X_IO_CFG_OUT_SEL_MASK(_c)       NI660X_IO_CFG_OUT_SEL((_c), 0x3)
#define NI660X_IO_CFG_IN_SEL(_c, _s)         (((_s) & 0x7) << (((_c) % 2) ? 4 : 12))
#define NI660X_IO_CFG_IN_SEL_MASK(_c)        NI660X_IO_CFG_IN_SEL((_c), 0x7)
#define NI660X_CHIP_OFFSET                   0x800
#define NI660X_NUM_PFI_CHANNELS              40
#define NI660X_MAX_DMA_CHANNEL               4
#define NI660X_COUNTERS_PER_CHIP             4
#define NI660X_MAX_CHIPS                     2
#define NI660X_MAX_COUNTERS                  (NI660X_MAX_CHIPS * NI660X_COUNTERS_PER_CHIP)
#define MAX_MITE_DMA_CHANNELS                8
#define MITE_IODWBSR                         0xc0
#define MITE_IODWBSR_1                       0xc4
#define WENAB                                 BIT(7)
#define MITE_IODWCR_1                        0xf4

/* GPCT Register Offsets */
#define NITIO_G0_INT_ACK_OFFSET              0x004
#define NITIO_G0_STATUS_OFFSET               0x004
#define NITIO_G1_INT_ACK_OFFSET              0x006
#define NITIO_G1_STATUS_OFFSET               0x006
#define NITIO_G01_STATUS_OFFSET              0x008
#define NITIO_G0_CMD_OFFSET                  0x00c
#define NI660X_STC_DIO_PARALLEL_INPUT_OFFSET  0x00e
#define NITIO_G1_CMD_OFFSET                  0x00e
#define NITIO_G0_HW_SAVE_OFFSET              0x010
#define NITIO_G1_HW_SAVE_OFFSET              0x014
#define NI660X_STC_DIO_OUTPUT_OFFSET         0x014
#define NI660X_STC_DIO_CONTROL_OFFSET        0x016
#define NITIO_G0_SW_SAVE_OFFSET              0x018
#define NITIO_G1_SW_SAVE_OFFSET              0x01c
#define NITIO_G0_MODE_OFFSET                 0x034
#define NITIO_G01_STATUS1_OFFSET             0x036
#define NITIO_G1_MODE_OFFSET                 0x036
#define NI660X_STC_DIO_SERIAL_INPUT_OFFSET   0x038
#define NITIO_G0_LOADA_OFFSET                0x038
#define NITIO_G01_STATUS2_OFFSET             0x03a
#define NITIO_G0_LOADB_OFFSET                0x03c
#define NITIO_G1_LOADA_OFFSET                0x040
#define NITIO_G1_LOADB_OFFSET                0x044
#define NITIO_G0_INPUT_SEL_OFFSET            0x048
#define NITIO_G1_INPUT_SEL_OFFSET            0x04a
#define NITIO_G0_AUTO_INC_OFFSET             0x088
#define NITIO_G1_AUTO_INC_OFFSET             0x08a
#define NITIO_G01_RESET_OFFSET               0x090
#define NITIO_G0_INT_ENA_OFFSET              0x092
#define NITIO_G1_INT_ENA_OFFSET              0x096
#define NITIO_G0_CNT_MODE_OFFSET             0x0b0
#define NITIO_G1_CNT_MODE_OFFSET             0x0b2
#define NITIO_G0_GATE2_OFFSET                0x0b4
#define NITIO_G1_GATE2_OFFSET                0x0b6
#define NITIO_G0_DMA_CFG_OFFSET              0x0b8
#define NITIO_G0_DMA_STATUS_OFFSET           0x0b8
#define NITIO_G1_DMA_CFG_OFFSET              0x0ba
#define NITIO_G1_DMA_STATUS_OFFSET           0x0ba
#define NITIO_G2_INT_ACK_OFFSET              0x104
#define NITIO_G2_STATUS_OFFSET               0x104
#define NITIO_G3_INT_ACK_OFFSET              0x106
#define NITIO_G3_STATUS_OFFSET               0x106
#define NITIO_G23_STATUS_OFFSET              0x108
#define NITIO_G2_CMD_OFFSET                  0x10c
#define NITIO_G3_CMD_OFFSET                  0x10e
#define NITIO_G2_HW_SAVE_OFFSET              0x110
#define NITIO_G3_HW_SAVE_OFFSET              0x114
#define NITIO_G2_SW_SAVE_OFFSET              0x118
#define NITIO_G3_SW_SAVE_OFFSET              0x11c
#define NITIO_G2_MODE_OFFSET                 0x134
#define NITIO_G23_STATUS1_OFFSET             0x136
#define NITIO_G3_MODE_OFFSET                 0x136
#define NITIO_G2_LOADA_OFFSET                0x138
#define NITIO_G23_STATUS2_OFFSET             0x13a
#define NITIO_G2_LOADB_OFFSET                0x13c
#define NITIO_G3_LOADA_OFFSET                0x140
#define NITIO_G3_LOADB_OFFSET                0x144
#define NITIO_G2_INPUT_SEL_OFFSET            0x148
#define NITIO_G3_INPUT_SEL_OFFSET            0x14a
#define NITIO_G2_AUTO_INC_OFFSET             0x188
#define NITIO_G3_AUTO_INC_OFFSET             0x18a
#define NITIO_G23_RESET_OFFSET               0x190
#define NITIO_G2_INT_ENA_OFFSET              0x192
#define NITIO_G3_INT_ENA_OFFSET              0x196
#define NITIO_G2_CNT_MODE_OFFSET             0x1b0
#define NITIO_G3_CNT_MODE_OFFSET             0x1b2
#define NITIO_G2_GATE2_OFFSET                0x1b4
#define NITIO_G3_GATE2_OFFSET                0x1b6
#define NITIO_G2_DMA_CFG_OFFSET              0x1b8
#define NITIO_G2_DMA_STATUS_OFFSET           0x1b8
#define NITIO_G3_DMA_CFG_OFFSET              0x1ba
#define NITIO_G3_DMA_STATUS_OFFSET           0x1ba
#define NI660X_DIO32_INPUT_OFFSET            0x414
#define NI660X_DIO32_OUTPUT_OFFSET           0x510
#define NI660X_CLK_CFG_OFFSET                0x73c
#define NI660X_GLOBAL_INT_STATUS_OFFSET      0x754
#define NI660X_DMA_CFG_OFFSET                0x76c
#define NI660X_GLOBAL_INT_CFG_OFFSET         0x770
#define NI660X_IO_CFG_0_1_OFFSET             0x77c
#define NI660X_IO_CFG_2_3_OFFSET             0x77e
#define NI660X_IO_CFG_4_5_OFFSET             0x780
#define NI660X_IO_CFG_6_7_OFFSET             0x782
#define NI660X_IO_CFG_8_9_OFFSET             0x784
#define NI660X_IO_CFG_10_11_OFFSET           0x786
#define NI660X_IO_CFG_12_13_OFFSET           0x788
#define NI660X_IO_CFG_14_15_OFFSET           0x78a
#define NI660X_IO_CFG_16_17_OFFSET           0x78c
#define NI660X_IO_CFG_18_19_OFFSET           0x78e
#define NI660X_IO_CFG_20_21_OFFSET           0x790
#define NI660X_IO_CFG_22_23_OFFSET           0x792
#define NI660X_IO_CFG_24_25_OFFSET           0x794
#define NI660X_IO_CFG_26_27_OFFSET           0x796
#define NI660X_IO_CFG_28_29_OFFSET           0x798
#define NI660X_IO_CFG_30_31_OFFSET           0x79a
#define NI660X_IO_CFG_32_33_OFFSET           0x79c
#define NI660X_IO_CFG_34_35_OFFSET           0x79e
#define NI660X_IO_CFG_36_37_OFFSET           0x7a0
#define NI660X_IO_CFG_38_39_OFFSET           0x7a2

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
    uint32_t global_int_status;
    uint32_t global_int_cfg;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t mmio_regs[0x1000];

    /* MITE memory region */
    MemoryRegion mite_mmio;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* No IRQ logic needed for probe; interrupts are not used nor triggered. */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size > 0x1000) {
        return 0xffffffffffffffffULL;
    }
    switch (size) {
    case 1:
        val = s->mmio_regs[addr];
        break;
    case 2:
        val = lduw_le_p(&s->mmio_regs[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->mmio_regs[addr]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "ni_660x: bad read size %u at 0x%x\n", size, (unsigned)addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size > 0x1000) {
        return;
    }
    /* Handle write-1-to-clear for interrupt ack registers */
    if (size == 2) {
        switch (addr) {
        case 0x004: /* G0 INT ACK */
        case 0x006: /* G1 INT ACK */
        case 0x104: /* G2 INT ACK */
        case 0x106: /* G3 INT ACK */
            {
                uint16_t current = lduw_le_p(&s->mmio_regs[addr]);
                current &= ~((uint16_t)val);
                stw_le_p(&s->mmio_regs[addr], current);
                return;
            }
        default:
            break;
        }
    }
    switch (size) {
    case 1:
        s->mmio_regs[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(&s->mmio_regs[addr], (uint16_t)val);
        break;
    case 4:
        stl_le_p(&s->mmio_regs[addr], (uint32_t)val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "ni_660x: bad write size %u at 0x%x\n", size, (unsigned)addr);
        break;
    }
}

/* MITE BAR handlers */
static uint64_t pcibase_mite_read(void *opaque, hwaddr addr, unsigned size)
{
    /* MITE registers: just return 0 */
    return 0;
}

static void pcibase_mite_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* ignore writes */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_mite_ops = {
    .read = pcibase_mite_read,
    .write = pcibase_mite_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    s->global_int_status = 0;
    s->global_int_cfg = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    const MemoryRegionOps *ops;

    if (bi->index == 1) {
        ops = &pcibase_mite_ops;
    } else {
        ops = &pcibase_mmio_ops;
    }

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  NI660X_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, NI660X_CLASS_ID );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "ni_660x-mmio";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "ni_660x-mite";

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
    .name = "ni_660x_pci",
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
