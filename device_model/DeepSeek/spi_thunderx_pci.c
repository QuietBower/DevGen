/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
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
#include "hw/clock.h"
#include "hw/qdev-clock.h"

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "spi_thunderx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x177d
#define DEVICE_ID 0xa00b
#define CLASS_ID  0x0c80

/* Actual register offsets extracted from thunderx_spi_probe() */
#define CONFIG_OFFSET 0x1000
#define STATUS_OFFSET 0x1008
#define TX_OFFSET    0x1010
#define DATA_OFFSET  0x1080

/* BAR0 size placeholder: increased to 64KB to ensure sufficient resource mapping */
#define BAR0_SIZE 0x10000

/* Default system clock frequency (used when not provided via property) */
#define SYS_FREQ_DEFAULT 700000000

/* Register unions from driver source (little-endian bit ordering) */
union cvmx_mpi_cfg {
    uint64_t u64;
    struct {
        uint64_t enable:1;
        uint64_t idlelo:1;
        uint64_t clk_cont:1;
        uint64_t wireor:1;
        uint64_t lsbfirst:1;
        uint64_t int_ena:1;
        uint64_t csena:1;
        uint64_t cshi:1;
        uint64_t idleclks:2;
        uint64_t tritx:1;
        uint64_t cslate:1;
        uint64_t csena0:1;
        uint64_t csena1:1;
        uint64_t csena2:1;
        uint64_t csena3:1;
        uint64_t clkdiv:13;
        uint64_t reserved_29_63:35;
    } s;
};

union cvmx_mpi_sts {
    uint64_t u64;
    struct {
        uint64_t busy:1;
        uint64_t reserved_1_7:7;
        uint64_t rxnum:5;
        uint64_t reserved_13_63:51;
    } s;
};

union cvmx_mpi_tx {
    uint64_t u64;
    struct {
        uint64_t totnum:5;
        uint64_t reserved_5_7:3;
        uint64_t txnum:5;
        uint64_t reserved_13_15:3;
        uint64_t leavecs:1;
        uint64_t reserved_17_19:3;
        uint64_t csid:2;
        uint64_t reserved_22_63:42;
    } s;
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
    union cvmx_mpi_cfg cfg;
    union cvmx_mpi_sts sts;
    union cvmx_mpi_tx tx;
    uint64_t data;

    /* Clock input to provide system frequency (required by driver) */
    Clock *clk;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= CONFIG_OFFSET && addr < CONFIG_OFFSET + 8) {
        val = s->cfg.u64 >> ((addr - CONFIG_OFFSET) * 8);
    } else if (addr >= STATUS_OFFSET && addr < STATUS_OFFSET + 8) {
        val = s->sts.u64 >> ((addr - STATUS_OFFSET) * 8);
    } else if (addr >= TX_OFFSET && addr < TX_OFFSET + 8) {
        val = s->tx.u64 >> ((addr - TX_OFFSET) * 8);
    } else if (addr >= DATA_OFFSET && addr < DATA_OFFSET + 8) {
        val = s->data >> ((addr - DATA_OFFSET) * 8);
    }

    if (size < 8) {
        val &= (1ULL << (size * 8)) - 1;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t mask = (size < 8) ? ((1ULL << (size * 8)) - 1) : ~0ULL;
    uint64_t val_masked = val & mask;

    if (addr >= CONFIG_OFFSET && addr < CONFIG_OFFSET + 8) {
        int shift = (addr - CONFIG_OFFSET) * 8;
        s->cfg.u64 &= ~(mask << shift);
        s->cfg.u64 |= (val_masked << shift);
    } else if (addr >= STATUS_OFFSET && addr < STATUS_OFFSET + 8) {
        int shift = (addr - STATUS_OFFSET) * 8;
        s->sts.u64 &= ~(mask << shift);
        s->sts.u64 |= (val_masked << shift);
    } else if (addr >= TX_OFFSET && addr < TX_OFFSET + 8) {
        int shift = (addr - TX_OFFSET) * 8;
        s->tx.u64 &= ~(mask << shift);
        s->tx.u64 |= (val_masked << shift);
    } else if (addr >= DATA_OFFSET && addr < DATA_OFFSET + 8) {
        int shift = (addr - DATA_OFFSET) * 8;
        s->data &= ~(mask << shift);
        s->data |= (val_masked << shift);
    }
    /* ignore writes to other addresses */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used */
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

    s->cfg.u64 = 0;
    s->sts.u64 = 0;
    s->tx.u64 = 0;
    s->data = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: set up BAR0 with MMIO mapping to cover all registers */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize clock input to provide system frequency (fixes probe failure -2 ENOENT) */
    s->clk = qdev_init_clock_in(DEVICE(pdev), "clk", NULL, NULL, 0);
    if (!clock_has_source(s->clk)) {
        /* No parent clock connected internally, create a fixed clock source */
        Clock *fixed_clk = clock_new(OBJECT(pdev), "fixed-clk");
        clock_set_hz(fixed_clk, SYS_FREQ_DEFAULT);
        clock_set_source(s->clk, fixed_clk);
    } else {
        clock_set_hz(s->clk, clock_get_hz(s->clk) ?: SYS_FREQ_DEFAULT);
    }

    /* Field_Init_Real: initialize hardware register shadows */
    s->cfg.u64 = 0;
    s->sts.u64 = 0;
    s->tx.u64 = 0;
    s->data = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* Uninit_Func placeholder */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "spi_thunderx_pci",
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
