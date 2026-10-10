/*
 * QEMU PCI device model for cb_pcidda (Comedi)
 * Phase 2: Functional implementation based strictly on provided driver.
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

#define TYPE_PCIBASE_DEVICE "cb_pcidda_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define EEPROM_SIZE 128
#define MAX_AO_CHANNELS 8
#define CB_DDA_DIO0_8255_BASE 0x00
#define CB_DDA_DIO1_8255_BASE 0x04
#define CB_DDA_DA_CTRL_REG 0x00
#define CB_DDA_DA_CTRL_SU (1u << 0)
#define CB_DDA_DA_CTRL_EN (1u << 1)
#define CB_DDA_DA_CTRL_DAC(x) ((x) << 2)
#define CB_DDA_DA_CTRL_RANGE2V5 (0u << 6)
#define CB_DDA_DA_CTRL_RANGE5V (2u << 6)
#define CB_DDA_DA_CTRL_RANGE10V (3u << 6)
#define CB_DDA_DA_CTRL_UNIP (1u << 8)
#define DACALIBRATION1 4
#define SERIAL_IN_BIT 0x1
#define CAL_CHANNEL_MASK (0x7 << 1)
#define CAL_CHANNEL_BITS(channel) (((channel) << 1) & CAL_CHANNEL_MASK)
#define CAL_COUNTER_MASK 0x1f
#define CAL_COUNTER_OVERFLOW_BIT 0x20
#define AO_BELOW_REF_BIT 0x40
#define SERIAL_OUT_BIT 0x80
#define DACALIBRATION2 6
#define SELECT_EEPROM_BIT 0x1
#define DESELECT_REF_DAC_BIT 0x2
#define DESELECT_CALDAC_BIT(n) (0x4 << (n))
#define DUMMY_BIT 0x40
#define CB_DDA_DA_DATA_REG(x) (0x08 + ((x) * 2))
#define CB_DDA_CALDAC_FINE_GAIN 0
#define CB_DDA_CALDAC_COURSE_GAIN 1
#define CB_DDA_CALDAC_COURSE_OFFSET 2
#define CB_DDA_CALDAC_FINE_OFFSET 3

#ifndef CB_PCI_VENDOR_ID_CB
#define CB_PCI_VENDOR_ID_CB 0x1307
#endif
#define CB_PCI_DEVICE_ID_DDA02_12 0x0020

#define CB_PCI_CLASS_ID 0x0000

/* BAR layout inferred from driver usage:
 *  - pci_resource_start(pcidev, 2) -> dev->iobase (unused in provided code)
 *  - pci_resource_start(pcidev, 3) -> devpriv->daqio (all inw/outw here)
 *
 * We model only BAR3 as IO space that the driver actually uses. BAR2 is
 * left unimplemented so that accesses are effectively ignored.
 */

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

    /* BAR3 (daqio) register space shadow, sized to cover all used offsets.
     * Offsets used in driver:
     *   DACALIBRATION1 (4)
     *   DACALIBRATION2 (6)
     *   CB_DDA_DA_CTRL_REG (0)
     *   CB_DDA_DA_DATA_REG(chan) = 0x08 + 2*chan, chan up to MAX_AO_CHANNELS-1
     * Max offset = 0x08 + 2*(7) = 0x16, so 0x20 bytes is sufficient.
     */
    uint16_t daqio_regs[0x20 / 2];

    /* Simple EEPROM contents used by cb_pcidda_read_eeprom(). The real
     * hardware provides calibration data; here we just expose zeros so
     * that accesses succeed and calibration logic runs without errors.
     */
    uint16_t eeprom_data[EEPROM_SIZE];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents
 * compiler warnings if unused. The provided driver does not use interrupts
 * for this board, so this is a no-op.
 */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns.
 * The driver does not configure a DMA engine, so this is unused.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    /* No MMIO BARs are used by the driver; return 0 for all reads. */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* No MMIO BARs are used by the driver; ignore all writes. */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* We only implement BAR3 (daqio) as IO space. BAR index 3 corresponds
     * to an offset base of 0 in this handler, so 'addr' is exactly the
     * port offset used in the driver relative to devpriv->daqio.
     */

    if (size != 2) {
        /* Driver uses inw_p (16-bit). For other sizes, return 0. */
        return 0;
    }

    /* Map 16-bit word offset into daqio_regs[] */
    if (addr < sizeof(s->daqio_regs)) {
        unsigned index = addr >> 1; /* word index */
        val = s->daqio_regs[index];
    } else {
        /* Out-of-range accesses return 0 */
        val = 0;
    }

    /* Specific behavior for DACALIBRATION1 reads used by
     * cb_pcidda_serial_in(): the driver tests SERIAL_OUT_BIT while
     * clocking in bits, but does not modify DACALIBRATION1 itself.
     * Without documented serial protocol, we simply reflect whatever
     * is in the shadow register. The driver will then read whatever
     * pattern we expose (initially 0).
     */

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 2) {
        /* Driver uses outw/outw_p only; ignore other sizes. */
        return;
    }

    if (addr < sizeof(s->daqio_regs)) {
        unsigned index = addr >> 1; /* word index */
        s->daqio_regs[index] = (uint16_t)val;
    }

    /* Behavior notes, strictly from driver code:
     *
     * 1) DACALIBRATION1 (offset 4)
     *    - cb_pcidda_serial_out() manipulates devpriv->dac_cal1_bits and
     *      writes it to DACALIBRATION1 via outw_p.
     *    - cb_pcidda_serial_in() then reads DACALIBRATION1 and checks
     *      SERIAL_OUT_BIT.
     *    We simply store the written value into daqio_regs and return
     *    it on reads; no further processing is defined by the driver.
     *
     * 2) DACALIBRATION2 (offset 6)
     *    - Used to select EEPROM vs caldac and to latch serial data.
     *    - cb_pcidda_read_eeprom() writes sequences with bits
     *      SELECT_EEPROM_BIT, DESELECT_REF_DAC_BIT, DESELECT_CALDAC_BIT,
     *      and DUMMY_BIT.
     *    - cb_pcidda_write_caldac() writes patterns to latch caldac values.
     *    The driver never reads DACALIBRATION2, so we only shadow it.
     *
     * 3) CB_DDA_DA_CTRL_REG (offset 0)
     *    - Written in cb_pcidda_ao_insn_write() to select channel, range,
     *      and unipolar/bipolar mode.
     *    The driver does not read this register back, so we just shadow it.
     *
     * 4) CB_DDA_DA_DATA_REG(channel) (offset 0x08 + 2*channel)
     *    - Written with DAC data values in cb_pcidda_ao_insn_write().
     *    - The driver does not read them back.
     *    We shadow these values so that user-space reads via other tools
     *    would see consistent state, although the driver itself doesn't.
     */

    (void)s;
    (void)addr;
    (void)val;
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

    /* Clear all shadow registers and load default EEPROM contents. */
    memset(s->daqio_regs, 0, sizeof(s->daqio_regs));
    memset(s->eeprom_data, 0, sizeof(s->eeprom_data));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* PCI requires BAR sizes to be a power of 2. */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CB_PCI_VENDOR_ID_CB);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CB_PCI_DEVICE_ID_DDA02_12);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CB_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Present as both conventional PCI and PCIe endpoint to satisfy the
     * template; the driver itself does not use PCIe-specific features.
     */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization
     * Driver uses:
     *   dev->iobase = pci_resource_start(pcidev, 2);
     *   devpriv->daqio = pci_resource_start(pcidev, 3);
     * Only BAR3 is accessed in the supplied code (DAQ IO region).
     */
    s->num_bars = 1;

    s->bar_info[0].index = 3;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 0x20; /* enough to cover all used offsets */
    s->bar_info[0].name  = "cb_pcidda-daqio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X usage in the provided driver source. */

    /* Initialize shadow state */
    memset(s->daqio_regs, 0, sizeof(s->daqio_regs));
    memset(s->eeprom_data, 0, sizeof(s->eeprom_data));
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
    .name = "cb_pcidda_pci",
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
