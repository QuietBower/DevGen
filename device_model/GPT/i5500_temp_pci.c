/*
 * QEMU PCI device model for Intel i5500 temp sensor (hwmon/i5500_temp)
 *
 * This file is auto-generated for behavioral emulation based solely on the
 * Linux driver drivers/hwmon/i5500_temp.c.
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

#define TYPE_PCIBASE_DEVICE "i5500_temp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define I5500_VENDOR_ID        PCI_VENDOR_ID_INTEL
#define I5500_DEVICE_ID        0x3438
#define I5500_CLASS_ID         PCI_CLASS_OTHERS

#define REG_TSTHRCATA  0xE2
#define REG_TSCTRL     0xE8
#define REG_TSTHRRPEX  0xEB
#define REG_TSTHRLO    0xEC
#define REG_TSTHRHI    0xEE
#define REG_CTHINT     0xF0
#define REG_TSFSC      0xF3
#define REG_CTSTS      0xF4
#define REG_TSTHRRQPI  0xF5
#define REG_CTCTRL     0xF7
#define REG_TSTIMER    0xF8

#define I5500_BAR_INDEX    0
#define I5500_BAR_SIZE     0x1000

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
    struct {
        uint8_t tsthrcata;
        uint8_t tsctrl;
        uint8_t tsthrrpex;
        uint8_t tsthrlo;
        uint8_t tsthrhi;
        uint8_t cthint;
        uint8_t tsfsc;
        uint8_t ctsts;
        uint8_t tsthrrqpi;
        uint8_t ctctrl;
        uint8_t tstimer;
    } regs;

};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The i5500_temp driver never uses interrupts, so we keep IRQ inactive. */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The i5500_temp driver does not use DMA; function intentionally empty. */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No MMIO accesses are used by the driver; return 0 for all. */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO accesses are used by the driver; ignore writes. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No PIO accesses are used by the driver; return 0 for all. */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* No PIO accesses are used by the driver; ignore writes. */
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

    /* Initialize configuration-space shadow registers to sane non-disabled values
     * consistent with the driver's expectations.
     *
     * Driver's probe() checks:
     *   pci_read_config_byte(pdev, REG_TSFSC, &tsfsc);
     *   pci_read_config_dword(pdev, REG_TSTIMER, &tstimer);
     *   if (tsfsc == 0x7F && tstimer == 0x07D30D40) -> sensor disabled.
     * We choose values different from that combination to allow probe success.
     */

    s->regs.tsthrcata = 0x50;   /* arbitrary 8-bit value for critical threshold */
    s->regs.tsctrl    = 0x00;   /* control defaults */
    s->regs.tsthrrpex = 0x00;   /* default */

    /* Threshold registers are word-sized in config space, but we keep
     * byte shadows corresponding to low/high bytes.
     * Example baseline: 80 C max, 70 C hyst, per 0.5-degree units (80*2=160).
     * The driver multiplies these by 500, but we only need internal shadows.
     */
    s->regs.tsthrlo   = 0x50;   /* low threshold (hysteresis) */
    s->regs.tsthrhi   = 0x60;   /* high threshold (max) */

    s->regs.cthint    = 0x00;   /* no interrupts configured */

    /* tsfsc must not be 0x7F plus special timer value together; choose 0 */
    s->regs.tsfsc     = 0x00;   /* no offset */

    /* Status bits: BIT(1) -> max_alarm, BIT(0) -> crit_alarm in driver.
     * Default: no alarms set.
     */
    s->regs.ctsts     = 0x00;

    s->regs.tsthrrqpi = 0x00;   /* default */
    s->regs.ctctrl    = 0x00;   /* default */

    /* TSTIMER is read as a dword at offset 0xF8. Our shadow is a byte, but we
     * will construct the dword read by putting this value in the low byte and
     * zeros in the high bytes. We must avoid 0x07D30D40 when combined with
     * tsfsc == 0x7F; since tsfsc is 0x00, any value is fine. Use 0 for now.
     */
    s->regs.tstimer   = 0x00;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  I5500_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  I5500_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, I5500_CLASS_ID );
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
    s->bar_info[0].index = I5500_BAR_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = I5500_BAR_SIZE;
    s->bar_info[0].name  = "i5500_temp-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize config-space shadow registers via reset helper. */
    pcibase_reset(DEVICE(pdev));
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
    .name = "i5500_temp_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

/*
 * Config-space access helpers
 *
 * The Linux i5500_temp driver uses pci_read_config_word/byte/dword on various
 * offsets. QEMU's core PCI layer provides default_read/write_config but we
 * want to reflect our shadow registers for those offsets so that reads return
 * consistent values.
 */

static uint8_t i5500_cfg_readb(PCIDevice *pdev, uint32_t addr)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (addr) {
    case REG_TSTHRCATA:
        return s->regs.tsthrcata;
    case REG_TSCTRL:
        return s->regs.tsctrl;
    case REG_TSTHRRPEX:
        return s->regs.tsthrrpex;
    case REG_TSTHRLO:
        return s->regs.tsthrlo;
    case REG_TSTHRHI:
        return s->regs.tsthrhi;
    case REG_CTHINT:
        return s->regs.cthint;
    case REG_TSFSC:
        return s->regs.tsfsc;
    case REG_CTSTS:
        return s->regs.ctsts;
    case REG_TSTHRRQPI:
        return s->regs.tsthrrqpi;
    case REG_CTCTRL:
        return s->regs.ctctrl;
    case REG_TSTIMER:
        return s->regs.tstimer;
    default:
        return pci_default_read_config(pdev, addr, 1);
    }
}

static uint16_t i5500_cfg_readw(PCIDevice *pdev, uint32_t addr)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (addr) {
    case REG_TSTHRLO: {
        /* word starting at 0xEC uses tsthrlo as low byte and tsthrhi as high */
        uint16_t lo = s->regs.tsthrlo;
        uint16_t hi = s->regs.tsthrhi;
        return lo | (hi << 8);
    }
    case REG_TSTHRHI: {
        /* word read at 0xEE; hardware would likely use tsthrhi plus next byte.
         * For our purposes we pack tsthrhi in low byte and 0 in high byte.
         */
        uint16_t lo = s->regs.tsthrhi;
        uint16_t hi = 0x00;
        return lo | (hi << 8);
    }
    case REG_TSTHRCATA: {
        /* word at 0xE2; we only have one byte shadow, mirror it */
        uint16_t lo = s->regs.tsthrcata;
        uint16_t hi = 0x00;
        return lo | (hi << 8);
    }
    default:
        return pci_default_read_config(pdev, addr, 2);
    }
}

static uint32_t i5500_cfg_readl(PCIDevice *pdev, uint32_t addr)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (addr) {
    case REG_TSTIMER: {
        /* dword at 0xF8: place tstimer in low byte, rest zero */
        uint32_t b0 = s->regs.tstimer;
        uint32_t b1 = 0;
        uint32_t b2 = 0;
        uint32_t b3 = 0;
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }
    default:
        return pci_default_read_config(pdev, addr, 4);
    }
}

static void i5500_cfg_writeb(PCIDevice *pdev, uint32_t addr, uint8_t val)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (addr) {
    case REG_TSTHRCATA:
        s->regs.tsthrcata = val;
        break;
    case REG_TSCTRL:
        s->regs.tsctrl = val;
        break;
    case REG_TSTHRRPEX:
        s->regs.tsthrrpex = val;
        break;
    case REG_TSTHRLO:
        s->regs.tsthrlo = val;
        break;
    case REG_TSTHRHI:
        s->regs.tsthrhi = val;
        break;
    case REG_CTHINT:
        s->regs.cthint = val;
        break;
    case REG_TSFSC:
        s->regs.tsfsc = val;
        break;
    case REG_CTSTS:
        s->regs.ctsts = val;
        break;
    case REG_TSTHRRQPI:
        s->regs.tsthrrqpi = val;
        break;
    case REG_CTCTRL:
        s->regs.ctctrl = val;
        break;
    case REG_TSTIMER:
        s->regs.tstimer = val;
        break;
    default:
        pci_default_write_config(pdev, addr, val, 1);
        break;
    }
}

static void i5500_cfg_writew(PCIDevice *pdev, uint32_t addr, uint16_t val)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (addr) {
    case REG_TSTHRLO:
        /* split into low/high threshold bytes */
        s->regs.tsthrlo = val & 0xFF;
        s->regs.tsthrhi = (val >> 8) & 0xFF;
        break;
    case REG_TSTHRHI:
        /* word write at 0xEE; update tsthrhi from low byte */
        s->regs.tsthrhi = val & 0xFF;
        break;
    case REG_TSTHRCATA:
        s->regs.tsthrcata = val & 0xFF;
        break;
    default:
        pci_default_write_config(pdev, addr, val, 2);
        break;
    }
}

static void i5500_cfg_writel(PCIDevice *pdev, uint32_t addr, uint32_t val)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (addr) {
    case REG_TSTIMER:
        s->regs.tstimer = val & 0xFF;
        break;
    default:
        pci_default_write_config(pdev, addr, val, 4);
        break;
    }
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    switch (len) {
    case 1:
        return i5500_cfg_readb(pdev, addr);
    case 2:
        return i5500_cfg_readw(pdev, addr);
    case 4:
        return i5500_cfg_readl(pdev, addr);
    default:
        /* Fallback to default handler for unusual sizes */
        return pci_default_read_config(pdev, addr, len);
    }
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    switch (len) {
    case 1:
        i5500_cfg_writeb(pdev, addr, (uint8_t)val);
        break;
    case 2:
        i5500_cfg_writew(pdev, addr, (uint16_t)val);
        break;
    case 4:
        i5500_cfg_writel(pdev, addr, val);
        break;
    default:
        pci_default_write_config(pdev, addr, val, len);
        break;
    }
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
