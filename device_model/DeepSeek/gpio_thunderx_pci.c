/*
 * QEMU model for Cavium ThunderX GPIO controller (gpio-thunderx.c)
 * Modeled after driver analysis, strictly implementing required behavior.
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

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_CAVIUM           0x177d
#define PCI_DEVICE_ID_THUNDERX_GPIO    0xa00a
#define PCI_CLASS_ID                   0xff00

#define GPIO_RX_DAT       0x0
#define GPIO_TX_SET       0x8
#define GPIO_TX_CLR       0x10
#define GPIO_CONST        0x90
#define GPIO_CONST_GPIOS_MASK 0xff

#define GPIO_BIT_CFG      0x400
#define GPIO_BIT_CFG_TX_OE        BIT(0)
#define GPIO_BIT_CFG_PIN_XOR      BIT(1)
#define GPIO_BIT_CFG_INT_EN       BIT(2)
#define GPIO_BIT_CFG_INT_TYPE     BIT(3)
#define GPIO_BIT_CFG_FIL_MASK     GENMASK(11, 4)
#define GPIO_BIT_CFG_FIL_CNT_SHIFT 4
#define GPIO_BIT_CFG_FIL_SEL_SHIFT 8
#define GPIO_BIT_CFG_TX_OD        BIT(12)
#define GPIO_BIT_CFG_PIN_SEL_MASK GENMASK(25, 16)

#define GPIO_INTR_BASE    0x800
#define GPIO_INTR_INTR          BIT(0)
#define GPIO_INTR_INTR_W1S      BIT(1)
#define GPIO_INTR_ENA_W1C       BIT(2)
#define GPIO_INTR_ENA_W1S       BIT(3)

#define GPIO_2ND_BANK     0x1400

#define MSIX_TABLE_OFFSET 0x2000
#define MSIX_PBA_OFFSET   0x3000

#define BAR0_SIZE 0x10000

#define GENMASK(h, l) (((1ULL << ((h) - (l) + 1)) - 1) << (l))

#define TYPE_PCIBASE_DEVICE "gpio_thunderx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

#define MAX_GPIO 256

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

    /* GPIO specific state */
    uint32_t ngpio;
    uint32_t base_msi;
    uint64_t bit_cfg[MAX_GPIO];
    uint64_t intr_reg_val[MAX_GPIO];
    bool intr_status_gpio[MAX_GPIO];
    bool intr_enable[MAX_GPIO];
    bool intr_raised[MAX_GPIO];
    uint64_t output_state[4];
    uint64_t input_state[4];
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;
    for (i = 0; i < s->ngpio; i++) {
        bool active = s->intr_status_gpio[i] && s->intr_enable[i];
        if (active && !s->intr_raised[i]) {
            uint32_t vector = s->base_msi + 2 * i;
            if (msix_enabled(pdev)) {
                msix_notify(pdev, vector);
            }
            s->intr_raised[i] = true;
        } else if (!active) {
            s->intr_raised[i] = false;
        }
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: unimplemented read size %d at addr 0x%"HWADDR_PRIx"\n", size, addr);
        return 0;
    }

    if (addr >= GPIO_CONST && addr < GPIO_CONST + 8) {
        /* GPIO_CONST register */
        val = s->ngpio | (s->base_msi << 8);
        return val;
    }

    if (addr >= GPIO_BIT_CFG && addr < GPIO_BIT_CFG + 8 * s->ngpio) {
        unsigned int line = (addr - GPIO_BIT_CFG) / 8;
        val = s->bit_cfg[line];
        return val;
    }

    if (addr >= GPIO_INTR_BASE && addr < GPIO_INTR_BASE + 8 * s->ngpio) {
        unsigned int line = (addr - GPIO_INTR_BASE) / 8;
        val = 0;
        if (s->intr_status_gpio[line]) val |= GPIO_INTR_INTR;
        if (s->intr_enable[line]) val |= BIT(2);
        return val;
    }

    if (addr < GPIO_CONST) {
        /* Banked registers (bank 0 only for simplicity) */
        if (addr == GPIO_RX_DAT) {
            val = s->output_state[0];
            return val;
        } else if (addr == GPIO_TX_SET || addr == GPIO_TX_CLR) {
            return 0;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR, "pcibase: read at unimplemented addr 0x%"HWADDR_PRIx"\n", addr);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: unimplemented write size %d at addr 0x%"HWADDR_PRIx"\n", size, addr);
        return;
    }

    if (addr >= GPIO_CONST && addr < GPIO_CONST + 8) {
        /* GPIO_CONST is read-only, ignore writes */
        return;
    }

    if (addr >= GPIO_BIT_CFG && addr < GPIO_BIT_CFG + 8 * s->ngpio) {
        unsigned int line = (addr - GPIO_BIT_CFG) / 8;
        s->bit_cfg[line] = val;
        return;
    }

    if (addr >= GPIO_INTR_BASE && addr < GPIO_INTR_BASE + 8 * s->ngpio) {
        unsigned int line = (addr - GPIO_INTR_BASE) / 8;
        if (val & GPIO_INTR_INTR) {
            s->intr_status_gpio[line] = false;
        }
        if (val & GPIO_INTR_ENA_W1C) {
            s->intr_enable[line] = false;
        }
        if (val & GPIO_INTR_ENA_W1S) {
            s->intr_enable[line] = true;
        }
        pcibase_update_irq(s);
        return;
    }

    if (addr < GPIO_CONST) {
        if (addr == GPIO_TX_SET) {
            s->output_state[0] |= val;
            return;
        } else if (addr == GPIO_TX_CLR) {
            s->output_state[0] &= ~val;
            return;
        } else if (addr == GPIO_RX_DAT) {
            return;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR, "pcibase: write at unimplemented addr 0x%"HWADDR_PRIx" val=0x%"PRIx64"\n", addr, val);
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

    /* Initialize GPIO state */
    s->ngpio = 8;
    s->base_msi = 0;
    memset(s->bit_cfg, 0, sizeof(s->bit_cfg));
    memset(s->intr_status_gpio, 0, sizeof(s->intr_status_gpio));
    memset(s->intr_enable, 0, sizeof(s->intr_enable));
    memset(s->intr_raised, 0, sizeof(s->intr_raised));
    memset(s->output_state, 0, sizeof(s->output_state));
    memset(s->input_state, 0, sizeof(s->input_state));
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

/* Maximum MSI-X vectors, safe upper bound given GPIO_CONST mask (0xff) */
#define MAX_MSIX_VECTORS 256

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CAVIUM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_THUNDERX_GPIO );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_byte(pci_conf + PCI_INTERRUPT_PIN, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "gpio-thunderx-mmio" };
    s->has_msix = true;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X Initialization */
    if (s->has_msix) {
        msix_init(pdev, MAX_MSIX_VECTORS,
                  &s->bar_regions[0], 0, MSIX_TABLE_OFFSET,
                  &s->bar_regions[0], 0, MSIX_PBA_OFFSET, 0, errp);
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
    .name = "gpio_thunderx_pci",
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

type_init(pcibase_register_types)
