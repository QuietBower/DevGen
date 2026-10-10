/*
 * QEMU PCI device model skeleton for pch_phub_pci
 * Phase 2: behavioral implementation based strictly on pch_phub.c
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "pch_phub_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PHUB_STATUS                             0x00
#define PHUB_CONTROL                            0x04
#define PCH_PHUB_ROM_WRITE_ENABLE               0x01
#define PCH_PHUB_ROM_WRITE_DISABLE              0x00
#define PCH_PHUB_MAC_START_ADDR_EG20T           0x14
#define PCH_PHUB_MAC_START_ADDR_ML7223          0x20C
#define PCH_PHUB_ROM_START_ADDR_EG20T           0x80
#define PCH_PHUB_ROM_START_ADDR_ML7213          0x400
#define PCH_PHUB_ROM_START_ADDR_ML7223          0x400
#define MAX_NUM_INT_REDUCE_CONTROL_REG          128
#define PCI_DEVICE_ID_PCH1_PHUB                 0x8801
#define PCH_MINOR_NOS                           1
#define CLKCFG_CAN_50MHZ                        0x12000000
#define CLKCFG_CANCLK_MASK                      0xFF000000
#define CLKCFG_UART_MASK                        0x00FFFFFF
#define CLKCFG_UART_48MHZ                       (1 << 16)
#define CLKCFG_UART_25MHZ                       (2 << 16)
#define CLKCFG_BAUDDIV                          (2 << 20)
#define CLKCFG_PLL2VCO                          (8 << 9)
#define CLKCFG_UARTCLKSEL                       (1 << 18)
#define PCI_DEVICE_ID_ROHM_ML7213_PHUB          0x801A
#define PCI_DEVICE_ID_ROHM_ML7223_mPHUB         0x8012
#define PCI_DEVICE_ID_ROHM_ML7223_nPHUB         0x8002
#define PCI_DEVICE_ID_ROHM_ML7831_PHUB          0x8801
#define PCH_WORD_ADDR_MASK                      (~((1 << 2) - 1))
#define PCH_PHUB_ID_REG                         0x0000
#define PCH_PHUB_QUEUE_PRI_VAL_REG              0x0004
#define PCH_PHUB_RC_QUEUE_MAXSIZE_REG           0x0008
#define PCH_PHUB_BRI_QUEUE_MAXSIZE_REG          0x000C
#define PCH_PHUB_COMP_RESP_TIMEOUT_REG          0x0010
#define PCH_PHUB_BUS_SLAVE_CONTROL_REG          0x0014
#define PCH_PHUB_DEADLOCK_AVOID_TYPE_REG        0x0018
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG0        0x0020
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG1        0x0024
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG2        0x0028
#define PCH_PHUB_INTPIN_REG_WPERMIT_REG3        0x002C
#define PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE    0x0040
#define CLKCFG_REG_OFFSET                       0x500
#define FUNCSEL_REG_OFFSET                      0x508
#define PCH_PHUB_OROM_SIZE                      15360

#define PCIBASE_VENDOR_ID                       PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID                       PCI_DEVICE_ID_PCH1_PHUB
#define PCIBASE_CLASS_ID                        PCI_CLASS_OTHERS

/* We choose a 4KB BAR for MMIO because size is not specified in the driver.
 * This only affects QEMU's address decoding range; internal logic uses the
 * explicit offsets from the driver. */
#define PCIBASE_MMIO_BAR_SIZE                   0x1000

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct pch_phub_reg_shadow {
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
        void *pch_phub_base_address;
        void *pch_phub_extrom_base_address;
        uint32_t pch_mac_start_address;
        uint32_t pch_opt_rom_start_address;
        int ioh_type;
        /* On-chip ROM window content (byte-addressable) */
        uint8_t rom[PCH_PHUB_OROM_SIZE];
        /* Control/status for ROM write emulation */
        uint32_t phub_status;
        uint32_t phub_control;
    } regs;
};

/* Internal helper for status-triggered signaling. Not used by driver. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns
 * No DMA usage is visible in pch_phub.c, so this function is empty. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver always uses 32-bit or 8-bit accesses via ioread32/ioread8. */
    switch (addr) {
    case PCH_PHUB_ID_REG:
        val = s->regs.phub_id_reg;
        break;
    case PCH_PHUB_QUEUE_PRI_VAL_REG:
        val = s->regs.q_pri_val_reg;
        break;
    case PCH_PHUB_RC_QUEUE_MAXSIZE_REG:
        val = s->regs.rc_q_maxsize_reg;
        break;
    case PCH_PHUB_BRI_QUEUE_MAXSIZE_REG:
        val = s->regs.bri_q_maxsize_reg;
        break;
    case PCH_PHUB_COMP_RESP_TIMEOUT_REG:
        val = s->regs.comp_resp_timeout_reg;
        break;
    case PCH_PHUB_BUS_SLAVE_CONTROL_REG:
        val = s->regs.bus_slave_control_reg;
        break;
    case PCH_PHUB_DEADLOCK_AVOID_TYPE_REG:
        val = s->regs.deadlock_avoid_type_reg;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG0:
        val = s->regs.intpin_reg_wpermit_reg0;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG1:
        val = s->regs.intpin_reg_wpermit_reg1;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG2:
        val = s->regs.intpin_reg_wpermit_reg2;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG3:
        val = s->regs.intpin_reg_wpermit_reg3;
        break;
    case CLKCFG_REG_OFFSET:
        val = s->regs.clkcfg_reg;
        break;
    case FUNCSEL_REG_OFFSET:
        val = s->regs.funcsel_reg;
        break;
    default:
        /* INT_REDUCE_CONTROL window: base 0x40, 4-byte stride */
        if (addr >= PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE &&
            addr < PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE + 4 * MAX_NUM_INT_REDUCE_CONTROL_REG) {
            unsigned int index = (addr - PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE) / 4;
            val = s->regs.int_reduce_control_reg[index];
        } else {
            /* For ROM accesses, the driver uses pci_map_rom(), not MMIO. */
            val = 0;
        }
        break;
    }

    /* Mask return value according to requested size */
    if (size == 1) {
        val &= 0xff;
    } else if (size == 2) {
        val &= 0xffff;
    } else if (size == 4) {
        val &= 0xffffffffu;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Only 32-bit writes are used in the driver for MMIO config registers
     * and PHUB_CONTROL. PHUB_STATUS is only read. */
    switch (addr) {
    case PCH_PHUB_ID_REG:
        s->regs.phub_id_reg = (uint32_t)val;
        break;
    case PCH_PHUB_QUEUE_PRI_VAL_REG:
        s->regs.q_pri_val_reg = (uint32_t)val;
        break;
    case PCH_PHUB_RC_QUEUE_MAXSIZE_REG:
        s->regs.rc_q_maxsize_reg = (uint32_t)val;
        break;
    case PCH_PHUB_BRI_QUEUE_MAXSIZE_REG:
        s->regs.bri_q_maxsize_reg = (uint32_t)val;
        break;
    case PCH_PHUB_COMP_RESP_TIMEOUT_REG:
        s->regs.comp_resp_timeout_reg = (uint32_t)val;
        break;
    case PCH_PHUB_BUS_SLAVE_CONTROL_REG:
        s->regs.bus_slave_control_reg = (uint32_t)val;
        break;
    case PCH_PHUB_DEADLOCK_AVOID_TYPE_REG:
        s->regs.deadlock_avoid_type_reg = (uint32_t)val;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG0:
        s->regs.intpin_reg_wpermit_reg0 = (uint32_t)val;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG1:
        s->regs.intpin_reg_wpermit_reg1 = (uint32_t)val;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG2:
        s->regs.intpin_reg_wpermit_reg2 = (uint32_t)val;
        break;
    case PCH_PHUB_INTPIN_REG_WPERMIT_REG3:
        s->regs.intpin_reg_wpermit_reg3 = (uint32_t)val;
        break;
    case CLKCFG_REG_OFFSET:
        /* read-modify-write is done in the driver using mask/value; here we store final value */
        s->regs.clkcfg_reg = (uint32_t)val;
        break;
    case FUNCSEL_REG_OFFSET:
        s->regs.funcsel_reg = (uint32_t)val;
        break;
    default:
        /* INT_REDUCE_CONTROL window */
        if (addr >= PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE &&
            addr < PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE + 4 * MAX_NUM_INT_REDUCE_CONTROL_REG) {
            unsigned int index = (addr - PCH_PHUB_INT_REDUCE_CONTROL_REG_BASE) / 4;
            if (index < MAX_NUM_INT_REDUCE_CONTROL_REG) {
                s->regs.int_reduce_control_reg[index] = (uint32_t)val;
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Initialize all shadow registers to 0 on reset; the driver does not rely
     * on any specific power-on value except that the ROM contents must be
     * readable and PHUB_STATUS should be 0 when idle. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The driver calls pci_iomap(pdev, 1, 0), so we must provide at least BAR1
     * with MMIO space. BAR0 is left unused. */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_NONE;
    s->bar_info[0].size  = 0;
    s->bar_info[0].name  = "pch_phub_bar0_unused";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = PCIBASE_MMIO_BAR_SIZE;
    s->bar_info[1].name  = "pch_phub_mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X usage observed in provided source */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize ROM content to a minimal valid image with 0xAA55 signature.
     * The driver reads signature at pch_opt_rom_start_address and then length
     * at +2, where length is in 512-byte units. */
    memset(s->regs.rom, 0, sizeof(s->regs.rom));
    /* Place the signature at the start (offset 0). */
    s->regs.rom[0] = 0x55; /* low byte of 0xAA55 */
    s->regs.rom[1] = 0xAA; /* high byte */
    /* Length field at offset 2: choose 0x30 => 0x30 * 512 = 24576 bytes.
     * The driver will only use up to PCH_PHUB_OROM_SIZE (15360). */
    s->regs.rom[2] = 0x30;

    /* Initialize status/control registers */
    s->regs.phub_status = 0x00;
    s->regs.phub_control = 0x00;

    /* Default configuration registers to 0. Specific values are written
     * by the driver during probe via iowrite32. */
    s->regs.phub_id_reg = 0;
    s->regs.q_pri_val_reg = 0;
    s->regs.rc_q_maxsize_reg = 0;
    s->regs.bri_q_maxsize_reg = 0;
    s->regs.comp_resp_timeout_reg = 0;
    s->regs.bus_slave_control_reg = 0;
    s->regs.deadlock_avoid_type_reg = 0;
    s->regs.intpin_reg_wpermit_reg0 = 0;
    s->regs.intpin_reg_wpermit_reg1 = 0;
    s->regs.intpin_reg_wpermit_reg2 = 0;
    s->regs.intpin_reg_wpermit_reg3 = 0;
    memset(s->regs.int_reduce_control_reg, 0,
           sizeof(s->regs.int_reduce_control_reg));
    s->regs.clkcfg_reg = 0;
    s->regs.funcsel_reg = 0;
    s->regs.pch_mac_start_address = 0;
    s->regs.pch_opt_rom_start_address = 0;
    s->regs.ioh_type = 0;
    s->regs.pch_phub_base_address = NULL;
    s->regs.pch_phub_extrom_base_address = NULL;
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

    (void)s;
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
