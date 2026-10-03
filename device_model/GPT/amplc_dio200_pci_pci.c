/*
 * QEMU PCI device model for amplc_dio200_pci (minimal behavior for probe)
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

#define TYPE_PCIBASE_DEVICE "amplc_dio200_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DIO200_ENHANCE        0x20
#define DIO200_INT_SCE        0x1e
#define DIO200_TS_COUNT       0x602
#define DIO200_TS_CONFIG      0x600
#define DIO200_CLK_SCE(x)     (0x18 + (x))
#define DIO200_GAT_SCE(x)     (0x1b + (x))

#define DIO200_MAX_SUBDEVS    8
#define DIO200_MAX_ISNS       6
#define TS_CONFIG_MAX_CLK_SRC 2

#define DIO200_PCI_VENDOR_ID  0x14dc
#define DIO200_PCI_DEVICE_ID  0x0011
#define DIO200_PCI_CLASS_ID   PCI_CLASS_OTHERS

/* TS_CONFIG bits actually used by driver */
#define TS_CONFIG_CLK_SRC_MASK 0x0FFu
#define TS_CONFIG_RESET        0x4u

/* Simple software timer emulation for TS_COUNT: 1 MHz increment */
#define DIO200_TS_TICK_NS 1000ULL

/* Basic BAR description */
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
    /* We implement only the registers actually touched by the driver. */
    uint8_t  regs8[0x800];   /* 8-bit view up to at least 0x602 */
    uint32_t regs32[0x800 / 4];

    /* Enhanced feature flag (DIO200_ENHANCE) */
    uint8_t enhance;

    /* Interrupt source enable and status emulation for DIO200_INT_SCE */
    uint8_t int_sce_enable;  /* what driver writes into DIO200_INT_SCE */
    uint8_t int_sce_status;  /* what driver reads from DIO200_INT_SCE */

    /* Timestamp / timer registers */
    uint32_t ts_config;      /* DIO200_TS_CONFIG shadow */
    uint32_t ts_count;       /* DIO200_TS_COUNT shadow */

    /* Simple timer to update ts_count */
    QEMUTimer ts_timer;
    bool ts_running;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The Linux driver only uses level-triggered shared IRQ and
     * checks DIO200_INT_SCE as interrupt source register via
     * dio200_subdev_intr_insn_bits(). It enables sources by writing
     * to DIO200_INT_SCE and reads it back, but no explicit
     * interrupt generation path is shown here. For probe success,
     * simply keep IRQ deasserted.
     */
    pci_set_irq(pdev, 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Driver does not set up any DMA buffers or descriptors directly.
     * No DMA emulation is required for probe/attach. */
    (void)pdev;
    (void)is_write;
}

/* Simple periodic timer callback to emulate TS_COUNT increment. */
static void pcibase_ts_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!s->ts_running) {
        return;
    }

    /* Increment 32-bit counter; wrap naturally. */
    s->ts_count++;

    /* Re-arm timer for next tick. */
    timer_mod(&s->ts_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + DIO200_TS_TICK_NS);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Map only the registers that the driver accesses. */
    switch (size) {
    case 1:
        if (addr < sizeof(s->regs8)) {
            /* Special cases for 8-bit registers we know. */
            if (addr == DIO200_ENHANCE) {
                val = s->enhance;
            } else if (addr == DIO200_INT_SCE) {
                /* Interrupt source status; AND with enabled mask
                 * to mirror driver behavior using valid_isns. */
                val = s->int_sce_status & s->int_sce_enable;
            } else {
                val = s->regs8[addr];
            }
        }
        break;
    case 2:
        if (addr + 1 < sizeof(s->regs8)) {
            val = s->regs8[addr] | ((uint16_t)s->regs8[addr + 1] << 8);
        }
        break;
    case 4:
        /* 32-bit access path; only a few offsets are used. */
        if (addr == DIO200_TS_CONFIG) {
            val = s->ts_config;
        } else if (addr == DIO200_TS_COUNT) {
            val = s->ts_count;
        } else if (addr + 3 < sizeof(s->regs8)) {
            val = s->regs8[addr] |
                  ((uint32_t)s->regs8[addr + 1] << 8) |
                  ((uint32_t)s->regs8[addr + 2] << 16) |
                  ((uint32_t)s->regs8[addr + 3] << 24);
        }
        break;
    case 8:
        /* Not used by driver; return zero safely. */
        val = 0;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 1:
        if (addr < sizeof(s->regs8)) {
            if (addr == DIO200_ENHANCE) {
                /* amplc_dio200_set_enhance writes here. */
                s->enhance = (uint8_t)val;
                s->regs8[addr] = (uint8_t)val;
            } else if (addr == DIO200_INT_SCE) {
                /* Interrupt source enable register: driver writes
                 * mask of enabled interrupt sources. */
                s->int_sce_enable = (uint8_t)val;
                s->regs8[addr] = (uint8_t)val;
                /* Update interrupt line state if needed. */
                pcibase_update_irq(s);
            } else if (addr >= DIO200_CLK_SCE(0) && addr <= DIO200_CLK_SCE(3)) {
                /* Clock source select for 8254 timers; just store. */
                s->regs8[addr] = (uint8_t)val;
            } else if (addr >= DIO200_GAT_SCE(0) && addr <= DIO200_GAT_SCE(3)) {
                /* Gate source select for 8254 timers; just store. */
                s->regs8[addr] = (uint8_t)val;
            } else {
                s->regs8[addr] = (uint8_t)val;
            }
        }
        break;
    case 2:
        if (addr + 1 < sizeof(s->regs8)) {
            s->regs8[addr] = (uint8_t)(val & 0xff);
            s->regs8[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        }
        break;
    case 4:
        if (addr == DIO200_TS_CONFIG) {
            /*
             * Driver uses:
             *  - write src (0..TS_CONFIG_MAX_CLK_SRC)
             *  - TS_CONFIG_RESET bit to reset counter.
             * It also reads TS_CONFIG and masks with TS_CONFIG_CLK_SRC_MASK.
             */
            uint32_t new_cfg = (uint32_t)val;
            uint32_t clk = new_cfg & TS_CONFIG_CLK_SRC_MASK;

            s->ts_config &= ~TS_CONFIG_CLK_SRC_MASK;
            s->ts_config |= clk;

            if (new_cfg & TS_CONFIG_RESET) {
                /* Reset TS_COUNT */
                s->ts_count = 0;
            }

            /* Start timer whenever a valid clock src is configured. */
            if (clk <= TS_CONFIG_MAX_CLK_SRC) {
                if (!s->ts_running) {
                    s->ts_running = true;
                    timer_mod(&s->ts_timer,
                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                              DIO200_TS_TICK_NS);
                }
            } else {
                /* Unsupported clock source: stop counting. */
                s->ts_running = false;
            }
        } else if (addr == DIO200_TS_COUNT) {
            /* Driver only reads TS_COUNT, but allow writes to set value. */
            s->ts_count = (uint32_t)val;
        } else if (addr + 3 < sizeof(s->regs8)) {
            s->regs8[addr]     = (uint8_t)(val & 0xff);
            s->regs8[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->regs8[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->regs8[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        }
        break;
    case 8:
        /* Not used by driver; ignore. */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Legacy I/O-port access mirrors the MMIO register file. */
    switch (size) {
    case 1:
        if (addr < sizeof(s->regs8)) {
            if (addr == DIO200_ENHANCE) {
                val = s->enhance;
            } else if (addr == DIO200_INT_SCE) {
                val = s->int_sce_status & s->int_sce_enable;
            } else {
                val = s->regs8[addr];
            }
        }
        break;
    case 2:
        if (addr + 1 < sizeof(s->regs8)) {
            val = s->regs8[addr] | ((uint16_t)s->regs8[addr + 1] << 8);
        }
        break;
    case 4:
        if (addr == DIO200_TS_CONFIG) {
            val = s->ts_config;
        } else if (addr == DIO200_TS_COUNT) {
            val = s->ts_count;
        } else if (addr + 3 < sizeof(s->regs8)) {
            val = s->regs8[addr] |
                  ((uint32_t)s->regs8[addr + 1] << 8) |
                  ((uint32_t)s->regs8[addr + 2] << 16) |
                  ((uint32_t)s->regs8[addr + 3] << 24);
        }
        break;
    default:
        val = 0;
        break;
    }

    return val;  /* Logic for Port I/O (Legacy support) */
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Mirror MMIO semantics for port I/O. */
    switch (size) {
    case 1:
        if (addr < sizeof(s->regs8)) {
            if (addr == DIO200_ENHANCE) {
                s->enhance = (uint8_t)val;
                s->regs8[addr] = (uint8_t)val;
            } else if (addr == DIO200_INT_SCE) {
                s->int_sce_enable = (uint8_t)val;
                s->regs8[addr] = (uint8_t)val;
                pcibase_update_irq(s);
            } else if (addr >= DIO200_CLK_SCE(0) && addr <= DIO200_CLK_SCE(3)) {
                s->regs8[addr] = (uint8_t)val;
            } else if (addr >= DIO200_GAT_SCE(0) && addr <= DIO200_GAT_SCE(3)) {
                s->regs8[addr] = (uint8_t)val;
            } else {
                s->regs8[addr] = (uint8_t)val;
            }
        }
        break;
    case 2:
        if (addr + 1 < sizeof(s->regs8)) {
            s->regs8[addr] = (uint8_t)(val & 0xff);
            s->regs8[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        }
        break;
    case 4:
        if (addr == DIO200_TS_CONFIG) {
            uint32_t new_cfg = (uint32_t)val;
            uint32_t clk = new_cfg & TS_CONFIG_CLK_SRC_MASK;

            s->ts_config &= ~TS_CONFIG_CLK_SRC_MASK;
            s->ts_config |= clk;

            if (new_cfg & TS_CONFIG_RESET) {
                s->ts_count = 0;
            }

            if (clk <= TS_CONFIG_MAX_CLK_SRC) {
                if (!s->ts_running) {
                    s->ts_running = true;
                    timer_mod(&s->ts_timer,
                              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                              DIO200_TS_TICK_NS);
                }
            } else {
                s->ts_running = false;
            }
        } else if (addr == DIO200_TS_COUNT) {
            s->ts_count = (uint32_t)val;
        } else if (addr + 3 < sizeof(s->regs8)) {
            s->regs8[addr]     = (uint8_t)(val & 0xff);
            s->regs8[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->regs8[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->regs8[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        }
        break;
    default:
        break;
    }

      /* Logic for Port I/O (Legacy support) */
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

    /* Revert registers to power-on defaults */
    memset(s->regs8, 0, sizeof(s->regs8));
    memset(s->regs32, 0, sizeof(s->regs32));

    s->enhance = 0;
    s->int_sce_enable = 0;
    s->int_sce_status = 0;
    s->ts_config = 0;
    s->ts_count = 0;
    s->ts_running = false;

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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  DIO200_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DIO200_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, DIO200_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    /* The driver uses BAR0 as the Avalon-MM bridge and a board-specific
     * mainbar (often BAR2) for the device register space. For simplicity
     * we expose a single MMIO BAR large enough for all used offsets. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* Must be at least 0x4000 for PCIe bridge plus room up to 0x602 */
    s->bar_info[0].size = 0x8000;
    s->bar_info[0].name = "amplc_dio200_pci-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize simple timer for TS_COUNT emulation. */
    timer_init_ns(&s->ts_timer, QEMU_CLOCK_VIRTUAL, pcibase_ts_timer_cb, s);
    s->ts_running = false;

      /* msi_init or msix_init calls */
      /* Not strictly required by driver; leave MSI/MSI-X disabled. */
       /* Set DMA masks or ring buffer limits */
      /* Initialize internal hardware timers if used */
      /* Final state initialization before the device is 'live' */

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

    /* Stop timers */
    timer_del(&s->ts_timer);

     /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "amplc_dio200_pci_pci",
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

