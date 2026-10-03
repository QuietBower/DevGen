/*
 * QEMU PCI device model for NI Lab-PC-PCI (labpc_pci)
 * Generated to satisfy Linux comedi driver drivers/comedi/drivers/ni_labpc_pci.c
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

#define TYPE_PCIBASE_DEVICE "labpc_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define LABPC_PCI_VENDOR_ID 0x1093
#define LABPC_PCI_DEVICE_ID 0x0161
#define LABPC_PCI_CLASS_ID  0x0000

#define MITE_IODWBSR 0xc0
#define WENAB        (1U << 7)

/* Board register offsets directly visible in provided driver code */
#define CMD1_REG              0x00
#define CMD2_REG              0x01
#define CMD3_REG              0x02
#define CMD4_REG              0x0f
#define CMD5_REG              0x1c
#define CMD6_REG              0x0e
#define COUNTER_A_BASE_REG    0x14
#define COUNTER_B_BASE_REG    0x18
#define DIO_BASE_REG          0x10

/* Newly provided status and control register offsets/macros */
#define STAT1_REG             0x00
#define STAT2_REG             0x1d

#define STAT1_GATA0           (1U << 5)
#define STAT1_CNTINT          (1U << 3)
#define STAT1_OVERFLOW        (1U << 2)
#define STAT1_OVERRUN         (1U << 1)
#define STAT1_DAVAIL          (1U << 0)

#define STAT2_OUTA1           (1U << 1)
#define STAT2_FIFONHF         (1U << 2)

#define ADC_FIFO_CLEAR_REG    0x08
#define TIMER_CLEAR_REG       0x0c
#define ADC_START_CONVERT_REG 0x03

#define INTERVAL_COUNT_REG    0x1e
#define INTERVAL_STROBE_REG   0x1f

#define DAC_LSB_REG(x)        (0x04 + 2 * (x))
#define DAC_MSB_REG(x)        (0x05 + 2 * (x))


enum labpc_pci_boardid {
    BOARD_NI_PCI1200,
};

enum transfer_type {
    fifo_not_empty_transfer,
    fifo_half_full_transfer,
    isa_dma_transfer,
};

struct labpc_boardinfo {
    const char *name;
    int ai_speed;            /* maximum input speed in ns */
    unsigned ai_scan_up:1;   /* can auto scan up in ai channels */
    unsigned has_ao:1;       /* has analog outputs */
    unsigned is_labpc1200:1; /* has extra regs compared to pc+ */
};

static const struct labpc_boardinfo labpc_pci_boards[] G_GNUC_UNUSED = {
    [BOARD_NI_PCI1200] = {
        .name          = "ni_pci-1200",
        .ai_speed      = 10000,
        .ai_scan_up    = 1,
        .has_ao        = 1,
        .is_labpc1200  = 1,
    },
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

    /* Hardware Register Shadows (The 'Identity' of the device) */

    /* BAR0: minimal MITE register window used by driver for MITE_IODWBSR */
    uint32_t bar0_mite_iodwbsr;

    /* BAR1: we now maintain simple shadows for the command registers that
     * the driver uses (CMD1-6) and additional status/control registers.
     */
    uint8_t cmd1_shadow;
    uint8_t cmd2_shadow;
    uint8_t cmd3_shadow;
    uint8_t cmd4_shadow;
    uint8_t cmd5_shadow;
    uint8_t cmd6_shadow;

    /* Newly modeled registers based on additional driver definitions */
    uint8_t stat1_shadow;
    uint8_t stat2_shadow;
    uint8_t adc_fifo_clear_shadow;
    uint8_t timer_clear_shadow;
    uint8_t adc_start_convert_shadow;
    uint8_t interval_count_shadow;
    uint8_t interval_strobe_shadow;

    /* DAC registers: the driver may access DAC_LSB/MSB for small channel
     * indices. We store up to 4 channels explicitly without inventing
     * behaviour beyond readback of written values.
     */
    uint8_t dac_lsb_shadow[4];
    uint8_t dac_msb_shadow[4];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /*
     * The provided ni_labpc_pci.c and labpc_common_* snippets only show
     * that an interrupt handler (labpc_interrupt) can be requested for
     * the shared PCI IRQ line, but they do not expose any on-device
     * interrupt cause/acknowledge registers or how/when interrupts
     * should be raised by the hardware.
     *
     * Without that information, we cannot safely emulate device-side
     * interrupt behaviour, so this helper intentionally does nothing.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /*
     * The provided ni_labpc_pci.c and labpc_common_* snippets do not
     * configure or initiate any PCI DMA transfers directly; all such
     * behaviour is encapsulated in other sources (e.g. labpc_common_*),
     * which have not been provided.
     *
     * We therefore leave this function empty to avoid inventing DMA
     * semantics.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* BAR0 and BAR1 both use this handler; distinguish by configured sizes
     * and addresses in the PCI layer, not here.
     */

    /* Only implement BAR0's MITE_IODWBSR if access is 4 bytes. */
    if (size == 4 && addr == MITE_IODWBSR) {
        val = s->bar0_mite_iodwbsr;
        return val;
    }

    /* BAR1 register shadows for simple 8-bit command and status/control
     * registers used by the driver via devpriv->write_byte()/read_byte().
     */

    if (size == 1) {
        switch (addr) {
        case CMD1_REG:
            val = s->cmd1_shadow;
            break;
        case CMD2_REG:
            val = s->cmd2_shadow;
            break;
        case CMD3_REG:
            val = s->cmd3_shadow;
            break;
        case CMD4_REG:
            val = s->cmd4_shadow;
            break;
        case CMD5_REG:
            val = s->cmd5_shadow;
            break;
        case CMD6_REG:
            val = s->cmd6_shadow;
            break;
        case STAT2_REG:
            val = s->stat2_shadow;
            break;
        case ADC_FIFO_CLEAR_REG:
            val = s->adc_fifo_clear_shadow;
            break;
        case TIMER_CLEAR_REG:
            val = s->timer_clear_shadow;
            break;
        case ADC_START_CONVERT_REG:
            val = s->adc_start_convert_shadow;
            break;
        case INTERVAL_COUNT_REG:
            val = s->interval_count_shadow;
            break;
        case INTERVAL_STROBE_REG:
            val = s->interval_strobe_shadow;
            break;
        default:
            /* DAC registers: check small channel indices explicitly. */
            if (addr >= DAC_LSB_REG(0) && addr <= DAC_LSB_REG(3)) {
                int ch = (addr - DAC_LSB_REG(0)) / 2;
                if (ch >= 0 && ch < 4 && addr == (hwaddr)DAC_LSB_REG(ch)) {
                    val = s->dac_lsb_shadow[ch];
                    break;
                }
            }
            if (addr >= DAC_MSB_REG(0) && addr <= DAC_MSB_REG(3)) {
                int ch = (addr - DAC_MSB_REG(0)) / 2;
                if (ch >= 0 && ch < 4 && addr == (hwaddr)DAC_MSB_REG(ch)) {
                    val = s->dac_msb_shadow[ch];
                    break;
                }
            }
            /* For any unmapped/unknown location return 0 to keep driver happy. */
            val = 0;
            break;
        }
        return val;
    }

    /* For any other MMIO location/size that we do not know about, return 0. */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle BAR0 MITE register write sequence from labpc_pci_mite_init():
     *   main_phys_addr = pci_resource_start(pcidev, 1);
     *   writel(main_phys_addr | WENAB, mite_base + MITE_IODWBSR);
     * The driver does not read back the value, but we store it for
     * completeness.
     */
    if (size == 4 && addr == MITE_IODWBSR) {
        s->bar0_mite_iodwbsr = (uint32_t)val;
        return;
    }

    /* BAR1 8-bit command and status/control registers written through
     * devpriv->write_byte(). We maintain simple shadows so software can
     * observe its own settings if it ever reads them back.
     */
    if (size == 1) {
        switch (addr) {
        case CMD1_REG:
            s->cmd1_shadow = (uint8_t)val;
            return;
        case CMD2_REG:
            s->cmd2_shadow = (uint8_t)val;
            return;
        case CMD3_REG:
            s->cmd3_shadow = (uint8_t)val;
            return;
        case CMD4_REG:
            s->cmd4_shadow = (uint8_t)val;
            return;
        case CMD5_REG:
            s->cmd5_shadow = (uint8_t)val;
            return;
        case CMD6_REG:
            s->cmd6_shadow = (uint8_t)val;
            return;
        case STAT2_REG:
            s->stat2_shadow = (uint8_t)val;
            return;
        case ADC_FIFO_CLEAR_REG:
            s->adc_fifo_clear_shadow = (uint8_t)val;
            return;
        case TIMER_CLEAR_REG:
            s->timer_clear_shadow = (uint8_t)val;
            return;
        case ADC_START_CONVERT_REG:
            s->adc_start_convert_shadow = (uint8_t)val;
            return;
        case INTERVAL_COUNT_REG:
            s->interval_count_shadow = (uint8_t)val;
            return;
        case INTERVAL_STROBE_REG:
            s->interval_strobe_shadow = (uint8_t)val;
            return;
        default:
            /* DAC registers: check small channel indices explicitly. */
            if (addr >= DAC_LSB_REG(0) && addr <= DAC_LSB_REG(3)) {
                int ch = (addr - DAC_LSB_REG(0)) / 2;
                if (ch >= 0 && ch < 4 && addr == (hwaddr)DAC_LSB_REG(ch)) {
                    s->dac_lsb_shadow[ch] = (uint8_t)val;
                    return;
                }
            }
            if (addr >= DAC_MSB_REG(0) && addr <= DAC_MSB_REG(3)) {
                int ch = (addr - DAC_MSB_REG(0)) / 2;
                if (ch >= 0 && ch < 4 && addr == (hwaddr)DAC_MSB_REG(ch)) {
                    s->dac_msb_shadow[ch] = (uint8_t)val;
                    return;
                }
            }
            /* Unknown 8-bit register: ignore to avoid inventing behaviour. */
            break;
        }
    }

    /* All other MMIO writes are not described in the provided driver code.
     * The labpc_common and other helpers access additional registers via
     * dev->mmio, but their offsets and semantics are not given. To avoid
     * inventing behaviour, we simply ignore these writes while still
     * providing an MMIO region for the driver to map.
     */
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* ni_labpc_pci.c uses MMIO for the PCI variant (dev->mmio). The
     * labpc_inb/labpc_outb helpers are used for ISA-like devices that
     * rely on I/O ports. The PCI device therefore does not expect any
     * PIO activity. We safely return 0 here.
     */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO accesses are used by the PCI variant in the provided driver.
     * Ignore writes.
     */
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

    /* Reset shadow registers to power-on defaults. The only register we
     * explicitly expose on BAR0 is MITE_IODWBSR, which comes up as 0.
     * For BAR1, we clear all command and status/control register shadows.
     */
    s->bar0_mite_iodwbsr = 0;
    s->cmd1_shadow = 0;
    s->cmd2_shadow = 0;
    s->cmd3_shadow = 0;
    s->cmd4_shadow = 0;
    s->cmd5_shadow = 0;
    s->cmd6_shadow = 0;

    s->stat1_shadow = 0;
    s->stat2_shadow = 0;
    s->adc_fifo_clear_shadow = 0;
    s->timer_clear_shadow = 0;
    s->adc_start_convert_shadow = 0;
    s->interval_count_shadow = 0;
    s->interval_strobe_shadow = 0;

    for (int ch = 0; ch < 4; ch++) {
        s->dac_lsb_shadow[ch] = 0;
        s->dac_msb_shadow[ch] = 0;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  LABPC_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  LABPC_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, LABPC_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * From ni_labpc_pci.c and labpc_common_attach():
     *   - BAR0 is used for MITE registers (temporarily remapped).
     *   - BAR1 is used for main device registers (dev->mmio) which are
     *     then consumed by labpc_common_attach() and other helpers.
     * No other BARs are referenced.
     */
    s->num_bars = 2;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    /* BAR0: small MMIO window including MITE_IODWBSR at offset 0xC0.
     * We expose a 256-byte region which safely covers this offset.
     */
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x100; /* 256 bytes */
    s->bar_info[0].name  = "labpc-pci-mite";

    /* BAR1: main device register block. Size is not specified by the
     * provided driver; we expose a modest 4 KiB region which is sufficient
     * for typical register maps while respecting the no-invention rule on
     * individual registers (we do not define them here beyond CMDx and
     * the explicitly provided status/control/DAC registers).
     */
    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = 0x1000; /* 4 KiB */
    s->bar_info[1].name  = "labpc-pci-main";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize runtime state to reset defaults. */
    s->bar0_mite_iodwbsr = 0;
    s->cmd1_shadow = 0;
    s->cmd2_shadow = 0;
    s->cmd3_shadow = 0;
    s->cmd4_shadow = 0;
    s->cmd5_shadow = 0;
    s->cmd6_shadow = 0;

    s->stat1_shadow = 0;
    s->stat2_shadow = 0;
    s->adc_fifo_clear_shadow = 0;
    s->timer_clear_shadow = 0;
    s->adc_start_convert_shadow = 0;
    s->interval_count_shadow = 0;
    s->interval_strobe_shadow = 0;

    for (int ch = 0; ch < 4; ch++) {
        s->dac_lsb_shadow[ch] = 0;
        s->dac_msb_shadow[ch] = 0;
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
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "labpc_pci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(bar0_mite_iodwbsr, PCIBaseState),
        VMSTATE_UINT8(cmd1_shadow, PCIBaseState),
        VMSTATE_UINT8(cmd2_shadow, PCIBaseState),
        VMSTATE_UINT8(cmd3_shadow, PCIBaseState),
        VMSTATE_UINT8(cmd4_shadow, PCIBaseState),
        VMSTATE_UINT8(cmd5_shadow, PCIBaseState),
        VMSTATE_UINT8(cmd6_shadow, PCIBaseState),
        VMSTATE_UINT8(stat1_shadow, PCIBaseState),
        VMSTATE_UINT8(stat2_shadow, PCIBaseState),
        VMSTATE_UINT8(adc_fifo_clear_shadow, PCIBaseState),
        VMSTATE_UINT8(timer_clear_shadow, PCIBaseState),
        VMSTATE_UINT8(adc_start_convert_shadow, PCIBaseState),
        VMSTATE_UINT8(interval_count_shadow, PCIBaseState),
        VMSTATE_UINT8(interval_strobe_shadow, PCIBaseState),
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

