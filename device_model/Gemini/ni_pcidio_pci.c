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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "ni_pcidio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NI 0x1093
#define CHIP_VERSION 27
#define FIFO_RESET BIT(7)
#define START_DELAY 88
#define REQ BIT(2)
#define DMA_RESET BIT(6)
#define OP_MODE 65
#define BLOCK_MODE 71
#define WINDOW_ADDRESS 4
#define INTERRUPT_AND_WINDOW_STATUS 4
#define MASTER_DMA_AND_INTERRUPT_CONTROL 5
#define GROUP_STATUS 5
#define GROUP_1_FLAGS 6
#define GROUP_2_FLAGS 7
#define GROUP_1_FIRST_CLEAR 6
#define GROUP_2_FIRST_CLEAR 7
#define GROUP_1_FIFO 8
#define GROUP_2_FIFO 12
#define TRANSFER_COUNT 20
#define CHIP_ID_D 24
#define CHIP_ID_I 25
#define CHIP_ID_O 26
#define MASTER_CLOCK_ROUTING 45
#define GROUP_1_SECOND_CLEAR 46
#define GROUP_2_SECOND_CLEAR 47
#define DATA_PATH 64
#define PROTOCOL_REGISTER_1 65
#define PROTOCOL_REGISTER_2 66
#define PROTOCOL_REGISTER_3 67
#define PROTOCOL_REGISTER_14 68
#define PROTOCOL_REGISTER_4 70
#define PROTOCOL_REGISTER_5 71
#define FIFO_Control 72
#define PROTOCOL_REGISTER_6 73
#define PROTOCOL_REGISTER_7 74
#define INTERRUPT_CONTROL 75
#define DMA_LINE_CONTROL_GROUP1 76
#define DMA_LINE_CONTROL_GROUP2 108
#define TRANSFER_SIZE_CONTROL 77
#define PROTOCOL_REGISTER_15 79
#define PATTERN_DETECTION 81
#define PROTOCOL_REGISTER_9 82
#define PROTOCOL_REGISTER_10 83
#define PROTOCOL_REGISTER_11 84
#define PROTOCOL_REGISTER_12 85
#define PROTOCOL_REGISTER_13 86
#define PROTOCOL_REGISTER_8 88
#define MITE_IODWBSR 0xc0
#define MITE_IODWBSR_1 0xc4
#define MITE_IODWCR_1 0xf4

#define PORT_PIN_DIRECTIONS(x)      (32 + (x))
#define PORT_IO(x)                  (28 + (x))
#define PORT_PIN_MASK(x)            (36 + (x))
#define CLOCK_REG                   PROTOCOL_REGISTER_2
#define SEQUENCE                    PROTOCOL_REGISTER_3
#define REQ_REG                     PROTOCOL_REGISTER_4
#define LINE_POLARITIES             PROTOCOL_REGISTER_6
#define ACK_SER                     PROTOCOL_REGISTER_7
#define REQ_DELAY                   PROTOCOL_REGISTER_9
#define REQ_NOT_DELAY               PROTOCOL_REGISTER_10
#define ACK_DELAY                   PROTOCOL_REGISTER_11
#define ACK_NOT_DELAY               PROTOCOL_REGISTER_12
#define DATA_1_DELAY                PROTOCOL_REGISTER_13
#define CLOCK_SPEED                 PROTOCOL_REGISTER_14
#define DAQ_OPTIONS                 PROTOCOL_REGISTER_15
#define DATA_LEFT                   BIT(0)
#define TRANSFER_READY              BIT(0)
#define COUNT_EXPIRED               BIT(1)
#define CLEAR_EXPIRED               BIT(0)
#define WAITED                      BIT(5)
#define CLEAR_WAITED                BIT(3)
#define PRIMARY_TC                  BIT(6)
#define CLEAR_PRIMARY_TC            BIT(4)
#define SECONDARY_TC                BIT(7)
#define CLEAR_SECONDARY_TC          BIT(5)
#define INT_EN (COUNT_EXPIRED | WAITED | PRIMARY_TC | SECONDARY_TC)
#define TRANSFER_WIDTH(x)           ((x) & 3)
#define TRANSFER_LENGTH(x)          (((x) & 3) << 3)
#define DATA_LATCHING(x)            (((x) & 3) << 5)
#define RUN_MODE(x)                 ((x) & 7)
#define NUMBERED                    BIT(3)

enum pci_6534_firmware_registers {
    Firmware_Control_Register = 0x100,
    Firmware_Status_Register = 0x104,
    Firmware_Data_Register = 0x108,
    Firmware_Mask_Register = 0x10c,
    Firmware_Debug_Register = 0x110,
};

enum pci_6534_fpga_registers {
    FPGA_Control1_Register = 0x200,
    FPGA_Control2_Register = 0x204,
    FPGA_Irq_Mask_Register = 0x208,
    FPGA_Status_Register = 0x20c,
    FPGA_Signature_Register = 0x210,
    FPGA_SCALS_Counter_Register = 0x280,
    FPGA_SCAMS_Counter_Register = 0x284,
    FPGA_SCBLS_Counter_Register = 0x288,
    FPGA_SCBMS_Counter_Register = 0x28c,
    FPGA_Temp_Control_Register = 0x2a0,
    FPGA_DAR_Register = 0x2a8,
    FPGA_ELC_Read_Register = 0x2b8,
    FPGA_ELC_Write_Register = 0x2bc,
};

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
    uint8_t regs[0x1000];

    /* DMA Context */
    uint32_t dma_count;
    uint32_t dma_addr;
    uint32_t dma_next;
    uint32_t dma_dar;

    uint32_t status;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* IRQ logic not explicitly defined without INT_EN macro */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        if (size == 1) {
            val = s->regs[addr];
        } else if (size == 2) {
            val = lduw_le_p(&s->regs[addr]);
        } else if (size == 4) {
            val = ldl_le_p(&s->regs[addr]);
        }
    }

    if (addr == Firmware_Status_Register) {
        val = 0x3; /* Satisfy firmware load polling */
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->regs)) {
        if (size == 1) {
            s->regs[addr] = val;
        } else if (size == 2) {
            stw_le_p(&s->regs[addr], val);
        } else if (size == 4) {
            stl_le_p(&s->regs[addr], val);
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
    memset(s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1093 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1150 );
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
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "ni_pcidio_bar0";
    
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "ni_pcidio_bar1";
    
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ni_pcidio_pci",
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
