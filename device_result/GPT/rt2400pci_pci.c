/*
 * QEMU PCI device model for rt2400pci (Phase 2: Functional Behavior)
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

#define TYPE_PCIBASE_DEVICE "rt2400pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor / Device from first pci_device_id entry: { PCI_DEVICE(0x1814, 0x0101) } */
#define RT2400PCI_PCI_VENDOR_ID 0x1814
#define RT2400PCI_PCI_DEVICE_ID 0x0101

/* Class: generic network controller */
#define RT2400PCI_PCI_CLASS_ID PCI_CLASS_NETWORK_ETHERNET

/* CSR / RF / BBP / EEPROM layout from driver macros */
#define CSR_REG_BASE   0x0000
#define CSR_REG_SIZE   0x014c
#define RF_BASE        0x0004
#define RF_SIZE        0x000c
#define BBP_BASE       0x0000
#define BBP_SIZE       0x0020
#define EEPROM_BASE    0x0000
#define EEPROM_SIZE    0x0100

/* Selected register offsets used by driver (subset, for identity only) */
#define CSR0           0x0000
#define CSR1           0x0004
#define CSR3           0x000c
#define CSR5           0x0014
#define CSR7           0x001c
#define CSR8           0x0020
#define CSR9           0x0024
#define CSR11          0x002c
#define CSR12          0x0030
#define CSR14          0x0038
#define CSR15          0x003c
#define CSR16          0x0040
#define CSR17          0x0044
#define CSR18          0x0048
#define CSR19          0x004c
#define CSR20          0x0050
#define CSR21          0x0054
#define TXCSR0         0x0060
#define TXCSR1         0x0064
#define TXCSR2         0x0068
#define TXCSR3         0x006c
#define TXCSR4         0x0070
#define TXCSR5         0x0074
#define TXCSR6         0x0078
#define RXCSR0         0x0080
#define RXCSR1         0x0084
#define RXCSR2         0x0088
#define RXCSR3         0x0090
#define ARCSR0         0x0098
#define ARCSR1         0x009c
#define CNT0           0x00a0
#define CNT3           0x00b8
#define CNT4           0x00bc
#define PWRCSR0        0x00c4
#define PSCSR0         0x00c8
#define PSCSR1         0x00cc
#define PSCSR2         0x00d0
#define PSCSR3         0x00d4
#define PWRCSR1        0x00d8
#define TIMECSR        0x00dc
#define MACCSR0        0x00e0
#define MACCSR1        0x00e4
#define RALINKCSR      0x00e8
#define BBPCSR         0x00f0
#define RFCSR          0x00f4
#define LEDCSR         0x00f8
#define GPIOCSR        0x0120
#define BCNCSR1        0x0130
#define MACCSR2        0x0134
#define ARCSR2         0x013c
#define ARCSR3         0x0140
#define ARCSR4         0x0144
#define ARCSR5         0x0148

/* Interrupt-related CSRs used by driver */
#define CSR7_TBCN_EXPIRE        0x00000001
#define CSR7_TXDONE_TXRING      0x00000008
#define CSR7_TXDONE_ATIMRING    0x00000010
#define CSR7_TXDONE_PRIORING    0x00000020
#define CSR7_RXDONE             0x00000040

#define CSR8_TBCN_EXPIRE        0x00000001
#define CSR8_TXDONE_TXRING      0x00000008
#define CSR8_TXDONE_ATIMRING    0x00000010
#define CSR8_TXDONE_PRIORING    0x00000020
#define CSR8_RXDONE             0x00000040

/* Basic control bits (subset) */
#define CSR1_SOFT_RESET         0x00000001
#define CSR1_BBP_RESET          0x00000002
#define CSR1_HOST_READY         0x00000004

#define RXCSR0_DISABLE_RX       0x00000001

/* BAR / queue layout related */
#define NUM_TX_QUEUES           2


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
    uint32_t intr_status; /* mirrors CSR7 */
    uint32_t intr_mask;   /* mirrors CSR8 */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t csr_regs[CSR_REG_SIZE / sizeof(uint32_t)];

    /* DMA Context */
    /* The driver sets up descriptor rings in system memory, but
     * it never lets the device autonomously read/write them in
     * a way we must emulate for probe/bringup. We therefore do
     * not implement active DMA here.
     */

    /* Operational status flags */
    uint32_t dev_state;

    /* State used to handle reset sequences */
    bool did_pci_reset;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* Other additional per-device static info */
    uint8_t reserved;
};

static inline uint32_t pcibase_csr_read(PCIBaseState *s, hwaddr addr)
{
    unsigned index = (addr - CSR_REG_BASE) >> 2;
    if (index >= ARRAY_SIZE(s->csr_regs)) {
        return 0;
    }
    return s->csr_regs[index];
}

static inline void pcibase_csr_write(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    unsigned index = (addr - CSR_REG_BASE) >> 2;
    if (index >= ARRAY_SIZE(s->csr_regs)) {
        return;
    }
    s->csr_regs[index] = val;
}

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Interrupts are enabled when the respective bit in CSR8 is 0
     * (driver writes 0 to enable, 1 to mask). Only raise irq for
     * enabled and pending sources.
     */
    uint32_t pending = s->intr_status & ~s->intr_mask;

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = 0;

    /* Device is little-endian and driver uses 32-bit accesses. */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case CSR7:
        /* Interrupt status register */
        val32 = s->intr_status;
        break;
    case CSR8:
        /* Interrupt mask register */
        val32 = s->intr_mask;
        break;
    default:
        val32 = pcibase_csr_read(s, addr);
        break;
    }

    return val32;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v = (uint32_t)val;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case CSR1: {
        /* Soft reset / host ready are bitfields; we simply store
         * the value into the shadow CSR region so future reads
         * see the updated state.
         */
        pcibase_csr_write(s, addr, v);
        break;
    }
    case CSR7:
        /* Driver clears pending interrupts by writing back CSR7. */
        s->intr_status &= ~v;
        pcibase_csr_write(s, addr, s->intr_status);
        pcibase_update_irq(s);
        break;
    case CSR8:
        /* Interrupt mask register; driver writes 0 to enable, 1 to mask. */
        s->intr_mask = v;
        pcibase_csr_write(s, addr, v);
        pcibase_update_irq(s);
        break;
    default:
        pcibase_csr_write(s, addr, v);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Driver uses MMIO only; return 0 for any PIO access. */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Driver uses MMIO only; ignore PIO writes. */
    (void)opaque;
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

    memset(s->csr_regs, 0, sizeof(s->csr_regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->dev_state = 0;
    s->did_pci_reset = true;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  RT2400PCI_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RT2400PCI_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RT2400PCI_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: one MMIO BAR for CSR region. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = CSR_REG_SIZE;
    s->bar_info[0].name  = "rt2400pci-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI / MSI-X disabled, driver uses legacy INTx. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize CSR shadows to known reset values that the driver
     * explicitly writes in its init path so subsequent reads see
     * consistent data. We only set fields when the driver uses
     * literal constants; the rest remain 0.
     */
    memset(s->csr_regs, 0, sizeof(s->csr_regs));
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
    .name = "rt2400pci_pci",
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
