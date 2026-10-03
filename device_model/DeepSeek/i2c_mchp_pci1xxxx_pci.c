/*
 * QEMU PCI device model for Microchip PCI1xxxx I2C/SMBus controller
 * Based on driver: i2c-mchp-pci1xxxx.c
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

/* Vendor ID extracted from driver source */
#define PCI_VENDOR_ID_EFAR                0x1055
#define PCI1XXXX_I2C_MMIO_SIZE            0x8000  /* 32K per driver comment */

/* Register offsets from driver (i2c-mchp-pci1xxxx.c) */
#define SMBUS_MAST_CORE_ADDR_BASE        0x00000
#define SMBUS_MAST_SYS_REG_ADDR_BASE     0x01000
#define SMB_CORE_CTRL_REG_OFF            (SMBUS_MAST_CORE_ADDR_BASE + 0x00)
#define SMB_CORE_CTRL_ESO                BIT(6)
#define SMB_CORE_CTRL_FW_ACK             BIT(4)
#define SMB_CORE_CTRL_ACK                BIT(0)
#define SMB_CORE_CMD_REG_OFF3            (SMBUS_MAST_CORE_ADDR_BASE + 0x0F)
#define SMB_CORE_CMD_REG_OFF2            (SMBUS_MAST_CORE_ADDR_BASE + 0x0E)
#define SMB_CORE_CMD_REG_OFF1            (SMBUS_MAST_CORE_ADDR_BASE + 0x0D)
#define SMB_CORE_CMD_READM               BIT(4)
#define SMB_CORE_CMD_STOP                BIT(2)
#define SMB_CORE_CMD_START               BIT(0)
#define SMB_CORE_CMD_REG_OFF0            (SMBUS_MAST_CORE_ADDR_BASE + 0x0C)
#define SMB_CORE_CMD_M_PROCEED           BIT(1)
#define SMB_CORE_CMD_M_RUN               BIT(0)
#define SMB_CORE_SR_HOLD_TIME_REG_OFF    (SMBUS_MAST_CORE_ADDR_BASE + 0x18)
#define SR_HOLD_TIME_100K_TICKS          150
#define SR_HOLD_TIME_400K_TICKS          20
#define SR_HOLD_TIME_1000K_TICKS         12
#define SMB_CORE_COMPLETION_REG_OFF3     (SMBUS_MAST_CORE_ADDR_BASE + 0x23)
#define COMPLETION_MDONE                 BIT(6)
#define COMPLETION_IDLE                  BIT(5)
#define COMPLETION_MNAKX                 BIT(0)
#define SMB_CORE_IDLE_SCALING_REG_OFF    (SMBUS_MAST_CORE_ADDR_BASE + 0x24)
#define FAIR_BUS_IDLE_MIN_100K_TICKS     992
#define FAIR_BUS_IDLE_MIN_400K_TICKS     500
#define FAIR_BUS_IDLE_MIN_1000K_TICKS    500
#define FAIR_IDLE_DELAY_100K_TICKS       963
#define FAIR_IDLE_DELAY_400K_TICKS       156
#define FAIR_IDLE_DELAY_1000K_TICKS      156
#define SMB_IDLE_SCALING_100K            ((FAIR_IDLE_DELAY_100K_TICKS << 16) | FAIR_BUS_IDLE_MIN_100K_TICKS)
#define SMB_IDLE_SCALING_400K            ((FAIR_IDLE_DELAY_400K_TICKS << 16) | FAIR_BUS_IDLE_MIN_400K_TICKS)
#define SMB_IDLE_SCALING_1000K           ((FAIR_IDLE_DELAY_1000K_TICKS << 16) | FAIR_BUS_IDLE_MIN_1000K_TICKS)
#define SMB_CORE_CONFIG_REG3             (SMBUS_MAST_CORE_ADDR_BASE + 0x2B)
#define SMB_CONFIG3_ENMI                 BIT(6)
#define SMB_CONFIG3_ENIDI                BIT(5)
#define SMB_CORE_CONFIG_REG2             (SMBUS_MAST_CORE_ADDR_BASE + 0x2A)
#define SMB_CORE_CONFIG_REG1             (SMBUS_MAST_CORE_ADDR_BASE + 0x29)
#define SMB_CONFIG1_ASR                  BIT(7)
#define SMB_CONFIG1_ENAB                 BIT(2)
#define SMB_CONFIG1_RESET                BIT(1)
#define SMB_CONFIG1_FEN                  BIT(0)
#define SMB_CORE_BUS_CLK_REG_OFF         (SMBUS_MAST_CORE_ADDR_BASE + 0x2C)
#define BUS_CLK_100K_LOW_PERIOD_TICKS    156
#define BUS_CLK_400K_LOW_PERIOD_TICKS    41
#define BUS_CLK_1000K_LOW_PERIOD_TICKS   15
#define BUS_CLK_100K_HIGH_PERIOD_TICKS   154
#define BUS_CLK_400K_HIGH_PERIOD_TICKS   35
#define BUS_CLK_1000K_HIGH_PERIOD_TICKS  14
#define BUS_CLK_100K                     ((BUS_CLK_100K_HIGH_PERIOD_TICKS << 8) | BUS_CLK_100K_LOW_PERIOD_TICKS)
#define BUS_CLK_400K                     ((BUS_CLK_400K_HIGH_PERIOD_TICKS << 8) | BUS_CLK_400K_LOW_PERIOD_TICKS)
#define BUS_CLK_1000K                    ((BUS_CLK_1000K_HIGH_PERIOD_TICKS << 8) | BUS_CLK_1000K_LOW_PERIOD_TICKS)
#define SMB_CORE_CLK_SYNC_REG_OFF        (SMBUS_MAST_CORE_ADDR_BASE + 0x3C)
#define CLK_SYNC_100K                    4
#define CLK_SYNC_400K                    4
#define CLK_SYNC_1000K                   4
#define SMB_CORE_DATA_TIMING_REG_OFF     (SMBUS_MAST_CORE_ADDR_BASE + 0x40)
#define FIRST_START_HOLD_100K_TICKS      23
#define FIRST_START_HOLD_400K_TICKS      8
#define FIRST_START_HOLD_1000K_TICKS     12
#define STOP_SETUP_100K_TICKS            150
#define STOP_SETUP_400K_TICKS            20
#define STOP_SETUP_1000K_TICKS           12
#define RESTART_SETUP_100K_TICKS         156
#define RESTART_SETUP_400K_TICKS         20
#define RESTART_SETUP_1000K_TICKS        12
#define DATA_HOLD_100K_TICKS             12
#define DATA_HOLD_400K_TICKS             2
#define DATA_HOLD_1000K_TICKS            2
#define DATA_TIMING_100K                 ((FIRST_START_HOLD_100K_TICKS << 24) | (STOP_SETUP_100K_TICKS << 16) | (RESTART_SETUP_100K_TICKS << 8) | DATA_HOLD_100K_TICKS)
#define DATA_TIMING_400K                 ((FIRST_START_HOLD_400K_TICKS << 24) | (STOP_SETUP_400K_TICKS << 16) | (RESTART_SETUP_400K_TICKS << 8) | DATA_HOLD_400K_TICKS)
#define DATA_TIMING_1000K                ((FIRST_START_HOLD_1000K_TICKS << 24) | (STOP_SETUP_1000K_TICKS << 16) | (RESTART_SETUP_1000K_TICKS << 8) | DATA_HOLD_1000K_TICKS)
#define SMB_CORE_TO_SCALING_REG_OFF      (SMBUS_MAST_CORE_ADDR_BASE + 0x44)
#define BUS_IDLE_MIN_100K_TICKS          36UL
#define BUS_IDLE_MIN_400K_TICKS          10UL
#define BUS_IDLE_MIN_1000K_TICKS         4UL
#define CTRL_CUM_TIME_OUT_100K_TICKS     76
#define CTRL_CUM_TIME_OUT_400K_TICKS     76
#define CTRL_CUM_TIME_OUT_1000K_TICKS    76
#define TARGET_CUM_TIME_OUT_100K_TICKS   95
#define TARGET_CUM_TIME_OUT_400K_TICKS   95
#define TARGET_CUM_TIME_OUT_1000K_TICKS  95
#define CLOCK_HIGH_TIME_OUT_100K_TICKS   97
#define CLOCK_HIGH_TIME_OUT_400K_TICKS   97
#define CLOCK_HIGH_TIME_OUT_1000K_TICKS  97
#define TO_SCALING_100K                  ((BUS_IDLE_MIN_100K_TICKS << 24) | (CTRL_CUM_TIME_OUT_100K_TICKS << 16) | (TARGET_CUM_TIME_OUT_100K_TICKS << 8) | CLOCK_HIGH_TIME_OUT_100K_TICKS)
#define TO_SCALING_400K                  ((BUS_IDLE_MIN_400K_TICKS << 24) | (CTRL_CUM_TIME_OUT_400K_TICKS << 16) | (TARGET_CUM_TIME_OUT_400K_TICKS << 8) | CLOCK_HIGH_TIME_OUT_400K_TICKS)
#define TO_SCALING_1000K                 ((BUS_IDLE_MIN_1000K_TICKS << 24) | (CTRL_CUM_TIME_OUT_1000K_TICKS << 16) | (TARGET_CUM_TIME_OUT_1000K_TICKS << 8) | CLOCK_HIGH_TIME_OUT_1000K_TICKS)
#define I2C_SCL_PAD_CTRL_REG_OFF         (SMBUS_MAST_CORE_ADDR_BASE + 0x100)
#define I2C_SDA_PAD_CTRL_REG_OFF         (SMBUS_MAST_CORE_ADDR_BASE + 0x101)
#define I2C_FOD_EN                       BIT(4)
#define I2C_PULL_UP_EN                   BIT(3)
#define I2C_PULL_DOWN_EN                 BIT(2)
#define I2C_INPUT_EN                     BIT(1)
#define I2C_OUTPUT_EN                    BIT(0)
#define SMBUS_CONTROL_REG_OFF            (SMBUS_MAST_CORE_ADDR_BASE + 0x200)
#define CTL_RESET_COUNTERS               BIT(3)
#define CTL_TRANSFER_DIR                 BIT(2)
#define CTL_HOST_FIFO_ENTRY              BIT(1)
#define CTL_RUN                          BIT(0)
#define I2C_DIRN_WRITE                   0
#define I2C_DIRN_READ                    1
#define SMBUS_STATUS_REG_OFF             (SMBUS_MAST_CORE_ADDR_BASE + 0x204)
#define STA_DMA_TERM                     BIT(7)
#define STA_DMA_REQ                      BIT(6)
#define STA_THRESHOLD                    BIT(2)
#define STA_BUF_FULL                     BIT(1)
#define STA_BUF_EMPTY                    BIT(0)
#define SMBUS_INTR_STAT_REG_OFF          (SMBUS_MAST_CORE_ADDR_BASE + 0x208)
#define INTR_STAT_DMA_TERM               BIT(7)
#define INTR_STAT_THRESHOLD              BIT(2)
#define INTR_STAT_BUF_FULL               BIT(1)
#define INTR_STAT_BUF_EMPTY              BIT(0)
#define SMBUS_INTR_MSK_REG_OFF           (SMBUS_MAST_CORE_ADDR_BASE + 0x20C)
#define INTR_MSK_DMA_TERM                BIT(7)
#define INTR_MSK_THRESHOLD               BIT(2)
#define INTR_MSK_BUF_FULL                BIT(1)
#define INTR_MSK_BUF_EMPTY               BIT(0)
#define ALL_NW_LAYER_INTERRUPTS          (INTR_MSK_DMA_TERM | INTR_MSK_THRESHOLD | INTR_MSK_BUF_FULL | INTR_MSK_BUF_EMPTY)
#define SMBUS_MCU_COUNTER_REG_OFF        (SMBUS_MAST_CORE_ADDR_BASE + 0x214)
#define SMBALERT_MST_PAD_CTRL_REG_OFF    (SMBUS_MAST_CORE_ADDR_BASE + 0x230)
#define SMBALERT_MST_PU                  BIT(0)
#define SMBUS_GEN_INT_STAT_REG_OFF       (SMBUS_MAST_CORE_ADDR_BASE + 0x23C)
#define SMBUS_GEN_INT_MASK_REG_OFF       (SMBUS_MAST_CORE_ADDR_BASE + 0x240)
#define SMBALERT_INTR_MASK               BIT(10)
#define I2C_BUF_MSTR_INTR_MASK           BIT(9)
#define I2C_INTR_MASK                    BIT(8)
#define SMBALERT_WAKE_INTR_MASK          BIT(2)
#define I2C_BUF_MSTR_WAKE_INTR_MASK      BIT(1)
#define I2C_WAKE_INTR_MASK               BIT(0)
#define ALL_HIGH_LAYER_INTR              (SMBALERT_INTR_MASK | I2C_BUF_MSTR_INTR_MASK | I2C_INTR_MASK | SMBALERT_WAKE_INTR_MASK | I2C_BUF_MSTR_WAKE_INTR_MASK | I2C_WAKE_INTR_MASK)
#define SMBUS_RESET_REG                  (SMBUS_MAST_CORE_ADDR_BASE + 0x248)
#define PERI_SMBUS_D3_RESET_DIS          BIT(16)
#define SMBUS_MST_BUF                    (SMBUS_MAST_CORE_ADDR_BASE + 0x280)
#define SMBUS_BUF_MAX_SIZE               0x80
#define I2C_FLAGS_DIRECT_MODE            BIT(7)
#define I2C_FLAGS_POLLING_MODE           BIT(6)
#define I2C_FLAGS_STOP                   BIT(5)
#define I2C_FLAGS_SMB_BLK_READ           BIT(4)
#define PCI1XXXX_I2C_TIMEOUT_MS          1000
#define SMB_GPR_REG                      (SMBUS_MAST_CORE_ADDR_BASE + 0x1000 + 0x0c00 + 0x00)
#define SMB_GPR_LOCK_REG                 (SMBUS_MAST_CORE_ADDR_BASE + 0x1000 + 0x0000 + 0x00A0)
#define SMBUS_PERI_LOCK                  BIT(3)

#define TYPE_PCIBASE_DEVICE "i2c_mchp_pci1xxxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_stat;
    uint32_t intr_mask;
    uint32_t gen_intr_stat;
    uint32_t gen_intr_mask;

    uint8_t regs[PCI1XXXX_I2C_MMIO_SIZE];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending;

    /* Check both low-level and high-level interrupt status */
    pending = (s->intr_stat & ~s->intr_mask) || (s->gen_intr_stat & ~s->gen_intr_mask);

    if (msi_enabled(pdev)) {
        if (pending) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, pending ? 1 : 0);
    }
}

/* Forward declare MMIO ops and read/write handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Placeholder PIO ops (unused but referenced by generic bar registration) */
static const MemoryRegionOps pcibase_pio_ops = {
    .read = NULL,
    .write = NULL,
    .endianness = DEVICE_LITTLE_ENDIAN,
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_EFAR);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0xA003);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0C05);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Interrupt configuration: attempt MSI first; fall back to INTx if not possible */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        /* MSI init failed; will rely on INTx via pci_set_irq */
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = PCI1XXXX_I2C_MMIO_SIZE, .name = "i2c-mmio" };
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all registers and interrupt state */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_stat = 0;
    s->intr_mask = 0xFF;  /* all interrupts disabled by default */
    s->gen_intr_stat = 0;
    s->gen_intr_mask = 0xFFFF;

    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > PCI1XXXX_I2C_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read out of bounds: addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return 0;
    }

    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        val = lduw_le_p(&s->regs[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->regs[addr]);
        break;
    case 8:
        val = ldq_le_p(&s->regs[addr]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported read size: addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > PCI1XXXX_I2C_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write out of bounds: addr=0x%" HWADDR_PRIx " size=%u val=0x%" PRIx64 "\n",
                      __func__, addr, size, val);
        return;
    }

    /* Handle special registers with write-1-to-clear behavior first */
    switch (addr) {
    case SMBUS_INTR_STAT_REG_OFF:
        if (size == 1) {
            s->intr_stat &= ~(uint8_t)val;
            pcibase_update_irq(s);
            return;
        }
        break;
    case SMBUS_GEN_INT_STAT_REG_OFF:
        if (size == 2) {
            s->gen_intr_stat &= ~(uint16_t)val;
            pcibase_update_irq(s);
            return;
        } else if (size == 1) {
            /* Allow byte access to the low byte of 16-bit reg */
            s->gen_intr_stat &= ~((uint16_t)val & 0xFF);
            pcibase_update_irq(s);
            return;
        }
        break;
    case SMBUS_STATUS_REG_OFF:
        if (size == 1) {
            /* W1C: each bit written with 1 clears the corresponding status bit */
            s->regs[addr] &= ~(uint8_t)val;
            /* No interrupt update needed for STATUS register */
            return;
        }
        break;
    case SMB_CORE_COMPLETION_REG_OFF3:
        if (size == 1) {
            s->regs[addr] &= ~(uint8_t)val;
            return;
        }
        break;
    default:
        break;
    }

    /* Generic write to the register array */
    switch (size) {
    case 1:
        s->regs[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(&s->regs[addr], (uint16_t)val);
        break;
    case 4:
        stl_le_p(&s->regs[addr], (uint32_t)val);
        break;
    case 8:
        stq_le_p(&s->regs[addr], val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: unsupported write size: addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return;
    }

    /* After a write, check if interrupt masking may need to update the IRQ */
    if (addr == SMBUS_INTR_MSK_REG_OFF || addr == SMBUS_GEN_INT_MASK_REG_OFF) {
        pcibase_update_irq(s);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "i2c_mchp_pci1xxxx_pci",
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
