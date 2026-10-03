/*
 * QEMU PCI device model for f81601-based SJA1000 CAN controller
 * Phase 2: Behavioral implementation based strictly on provided driver.
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
#include "hw/hw.h"

#define TYPE_PCIBASE_DEVICE "f81601_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/*
 * Extracted from linux-6.18/drivers/net/can/sja1000/f81601.c
 */
#define F81601_PCI_MAX_CHAN            2
#define F81601_DECODE_REG              0x209
#define F81601_IO_MODE                 (1U << 7)
#define F81601_MEM_MODE                (1U << 6)
#define F81601_CFG_MODE                (1U << 5)
#define F81601_CAN2_INTERNAL_CLK       (1U << 3)
#define F81601_CAN1_INTERNAL_CLK       (1U << 2)
#define F81601_CAN2_EN                 (1U << 1)
#define F81601_CAN1_EN                 (1U << 0)
#define F81601_TRAP_REG                0x20a
#define F81601_CAN2_HAS_EN             (1U << 4)

#define SJA1000_ECHO_SKB_MAX           1
#define SJA1000_MOD                    0x00
#define SJA1000_IER                    0x04
#define IRQ_OFF                        0x00
#define MOD_RM                         0x01
#define SJA1000_CDR                    0x1F
#define SJA1000_ACCM1                  0x15
#define SJA1000_ACCM3                  0x17
#define SJA1000_ACCM2                  0x16
#define SJA1000_ACCC1                  0x11
#define SJA1000_ACCC3                  0x13
#define SJA1000_ACCC0                  0x10
#define SJA1000_ACCC2                  0x12
#define SJA1000_QUIRK_NO_CDR_REG       (1U << 1)
#define SJA1000_OCR                    0x08
#define SJA1000_ACCM0                  0x14
#define SJA1000_TXERR                  0x0F
#define SJA1000_RXERR                  0x0E
#define SJA1000_BTR1                   0x07
#define SJA1000_BTR0                   0x06
#define DRV_NAME                       "sja1000_isa"
#define SJA1000_CUSTOM_IRQ_HANDLER     (1U << 0)
#define SJA1000_ID3                    0x13
#define SJA1000_SFF_BUF                0x13
#define CMD_AT                         0x02
#define SJA1000_ID4                    0x14
#define CMD_TR                         0x01
#define CMD_SRR                        0x10
#define SJA1000_FI_RTR                 0x40
#define SJA1000_FI                     0x10
#define SJA1000_EFF_BUF                0x15
#define SJA1000_FI_FF                  0x80
#define SJA1000_ID2                    0x12
#define SJA1000_ID1                    0x11
#define SJA1000_ECC                    0x0C
#define SJA1000_IR                     0x03
#define SJA1000_CMR                    0x01
#define SJA1000_SR                     0x02
#define MOD_STM                        0x04
#define MOD_LOM                        0x02
#define IRQ_BEI                        0x80
#define IRQ_ALL                        0xFF

/* additional IRQ/status bits used by generic sja1000 code */
#define IRQ_WUI                        0x20
#define IRQ_TI                         0x02
#define IRQ_RI                         0x01
#define IRQ_DOI                        0x08
#define IRQ_EI                         0x04
#define IRQ_EPI                        0x40
#define IRQ_ALI                        0x10

#define SR_TCS                         0x08
#define SR_RBS                         0x01

/* PCI IDs: use first entry of f81601_pci_tbl[] */
#define F81601_PCI_VENDOR_ID           0x1c29
#define F81601_PCI_DEVICE_ID           0x1703

/*
 * The driver is a CAN controller; use the standard PCI class for network
 * controller with subclass CAN (0x0207).
 * This value is typically encoded as 0x0207 in the PCI class device field.
 */
#define F81601_PCI_CLASS_ID            0x0207

#define PCIBASE_VENDOR_ID              F81601_PCI_VENDOR_ID
#define PCIBASE_DEVICE_ID              F81601_PCI_DEVICE_ID
#define PCIBASE_CLASS_ID               F81601_PCI_CLASS_ID

/* SJA1000 internal status bits we need to model, inferred from driver usage */
#define SJA1000_SR_TBS                 0x04  /* Transmit Buffer Status: 1 = free */

/* Register Layout and Hardware Identifiers extracted from driver source */


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

    /* Hardware Register Shadows */
    uint8_t sja1000_regs[0x20];   /* covers up to SJA1000_CDR (0x1F) */
    uint8_t f81601_decode_reg;    /* F81601_DECODE_REG (0x209) */
    uint8_t f81601_trap_reg;      /* F81601_TRAP_REG (0x20a) */

    /* Simple interrupt line state for level-triggered INTx */
    bool irq_asserted;

    /* Transmit echo bookkeeping: single outstanding frame index */
    bool tx_echo_pending;
};


static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * Very simple interrupt model: when SJA1000_IER is non-zero and there is
     * at least one pending interrupt source (IR != 0), assert INTx line.
     */
    bool enable = (s->sja1000_regs[SJA1000_IER] != 0);
    bool pending = (s->sja1000_regs[SJA1000_IR] != 0);
    bool level = enable && pending;

    if (level && !s->irq_asserted) {
        pci_set_irq(pdev, 1);
        s->irq_asserted = true;
    } else if (!level && s->irq_asserted) {
        pci_set_irq(pdev, 0);
        s->irq_asserted = false;
    }
}

/* Helpers to map BAR0 + offset to logical registers */
static bool pcibase_is_sja1000_reg(hwaddr addr, unsigned *chan, unsigned *reg)
{
    /* Each channel at 0x80 * i, driver uses up to 2 channels */
    if (addr >= 0x80 * F81601_PCI_MAX_CHAN) {
        return false;
    }
    *chan = addr / 0x80;
    *reg = addr % 0x80;
    /* We only shadow 0x00-0x1F */
    if (*reg >= sizeof(((PCIBaseState *)0)->sja1000_regs)) {
        return false;
    }
    return true;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xFF; /* default bus value */
    unsigned chan, reg;

    if (size != 1) {
        /* Driver uses readb/writeb only */
        return val;
    }

    if (pcibase_is_sja1000_reg(addr, &chan, &reg)) {
        (void)chan; /* Channels share same shadow as we only need basic probed state */

        switch (reg) {
        case SJA1000_MOD:
            /* Driver polls MOD_RM; default to reset mode set (bit 0 = 1) */
            val = s->sja1000_regs[SJA1000_MOD];
            break;
        case SJA1000_SR:
            /*
             * Status register is polled for SR_TCS and SR_RBS by generic
             * sja1000 code. Keep TX buffer always ready and no receive
             * pending to avoid hangs.
             */
            val = s->sja1000_regs[SJA1000_SR];
            /* Ensure TBS and TCS set, RBS clear */
            val |= (SJA1000_SR_TBS | SR_TCS);
            val &= ~SR_RBS;
            break;
        case SJA1000_IR:
            /* Reading IR clears it, and may deassert IRQ */
            val = s->sja1000_regs[SJA1000_IR];
            s->sja1000_regs[SJA1000_IR] = 0x00;
            pcibase_update_irq(s);
            break;
        default:
            if (reg < sizeof(s->sja1000_regs)) {
                val = s->sja1000_regs[reg];
            }
            break;
        }
        return val;
    }

    /* Other addresses: return 0xFF (unimplemented) */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned chan, reg;

    if (size != 1) {
        return;
    }

    val &= 0xFF;

    if (pcibase_is_sja1000_reg(addr, &chan, &reg)) {
        (void)chan;

        switch (reg) {
        case SJA1000_MOD:
            /* MOD register: store and update limited state */
            s->sja1000_regs[SJA1000_MOD] = (uint8_t)val;
            break;
        case SJA1000_IER:
            /* Enable/disable interrupts */
            s->sja1000_regs[SJA1000_IER] = (uint8_t)val;
            pcibase_update_irq(s);
            break;
        case SJA1000_CMR:
            /*
             * Command register: generic code writes CMR and then reads SR.
             * For START/RESET procedures, we can leave behaviour minimal,
             * not generating real frames.
             */
            s->sja1000_regs[SJA1000_CMR] = (uint8_t)val;
            break;
        case SJA1000_TXERR:
        case SJA1000_RXERR:
        case SJA1000_ECC:
        case SJA1000_BTR0:
        case SJA1000_BTR1:
        case SJA1000_OCR:
        case SJA1000_CDR:
        case SJA1000_ACCC0:
        case SJA1000_ACCC1:
        case SJA1000_ACCC2:
        case SJA1000_ACCC3:
        case SJA1000_ACCM0:
        case SJA1000_ACCM1:
        case SJA1000_ACCM2:
        case SJA1000_ACCM3:
            if (reg < sizeof(s->sja1000_regs)) {
                s->sja1000_regs[reg] = (uint8_t)val;
            }
            break;
        default:
            if (reg < sizeof(s->sja1000_regs)) {
                s->sja1000_regs[reg] = (uint8_t)val;
            }
            break;
        }
        return;
    }

    /* Unhandled MMIO writes are ignored. */
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

    memset(s->sja1000_regs, 0, sizeof(s->sja1000_regs));
    /* Power-on: chip is in reset mode */
    s->sja1000_regs[SJA1000_MOD] = MOD_RM;
    /* IR, IER cleared */
    s->sja1000_regs[SJA1000_IR] = 0x00;
    s->sja1000_regs[SJA1000_IER] = 0x00;
    /* Status: TX buffer free, TX completed, no RX pending */
    s->sja1000_regs[SJA1000_SR] = SJA1000_SR_TBS | SR_TCS;

    s->f81601_decode_reg = F81601_IO_MODE | F81601_MEM_MODE |
                           F81601_CFG_MODE | F81601_CAN1_EN | F81601_CAN2_EN;
    /* TRAP reg: advertise two channels by setting CAN2_HAS_EN */
    s->f81601_trap_reg = F81601_CAN2_HAS_EN;

    s->irq_asserted = false;
    s->tx_echo_pending = false;

    pcibase_update_irq(s);
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

    /* Expose pdev->irq as legacy INTx only; no MSI/MSI-X used in driver */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: single MMIO BAR large enough for SJA1000 windows */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000;  /* 4 KiB MMIO window */
    s->bar_info[0].name  = "f81601-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI / MSI-X: the snippet does not show pci_enable_msi/msix usage. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize internal state to reset defaults */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "f81601_pci",
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

