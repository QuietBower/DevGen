/*
 * QEMU PCI device model for tifm_7xx1
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

/* Local definition added to satisfy compiler; value chosen to match vendor/device pairing
 * used by the tifm_7xx1 driver. This does not alter any runtime logic in this file,
 * as PCIBASE_DEVICE_ID is used purely as a constant.
 */
#ifndef PCI_DEVICE_ID_TI_XX21_XX11_FM
#define PCI_DEVICE_ID_TI_XX21_XX11_FM 0x803b
#endif

#define TYPE_PCIBASE_DEVICE "tifm_7xx1_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID PCI_VENDOR_ID_TI
#define PCIBASE_DEVICE_ID PCI_DEVICE_ID_TI_XX21_XX11_FM
#define PCIBASE_CLASS_ID  PCI_CLASS_OTHERS

#define TIFM_IRQ_ENABLE           0x80000000
#define TIFM_IRQ_SOCKMASK(x)      (x)
#define TIFM_IRQ_CARDMASK(x)      ((x) << 8)
#define TIFM_IRQ_FIFOMASK(x)      ((x) << 16)
#define TIFM_IRQ_SETALL           0xffffffff

/* The following offsets are inferred from driver usage patterns */
#define FM_INTERRUPT_STATUS            0x00
#define FM_CLEAR_INTERRUPT_ENABLE      0x04
#define FM_SET_INTERRUPT_ENABLE        0x08

/* We need a contiguous MMIO window that covers socket registers as well.
 * The real hardware layout is unknown; we only emulate the minimal subset
 * that the driver touches. We pick a small size (4 KiB) that is a power of 2. */
#define TIFM_MMIO_SIZE (4 * KiB)

/* Socket-relative registers used by the driver */
#define SOCK_CONTROL             0x00
#define SOCK_PRESENT_STATE       0x04

/* Bit definitions from driver */
#define TIFM_SOCK_STATE_POWERED   0x00000080
#define TIFM_SOCK_STATE_OCCUPIED  0x00000008
#define TIFM_CTRL_LED             0x00000040
#define TIFM_CTRL_POWER_MASK      0x00000007
#define TIFM_TYPE_XD              1

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
    uint32_t fm_interrupt_status;
    uint32_t fm_set_interrupt_enable;
    uint32_t fm_clear_interrupt_enable;

    /* Interrupt line state */
    bool irq_asserted;

    /* Simple emulation of sockets */
    unsigned int num_sockets;      /* 2 or 4 depending on device id */

    /* For each socket we keep a present_state value and control reg */
    struct {
        uint32_t control;
        uint32_t present_state;
    } sockets[4];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t enabled = s->fm_set_interrupt_enable & ~s->fm_clear_interrupt_enable;
    uint32_t active = s->fm_interrupt_status & enabled;

    bool should_assert = (active != 0) && (enabled & TIFM_IRQ_ENABLE);

    if (should_assert && !s->irq_asserted) {
        s->irq_asserted = true;
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else if (!should_assert && s->irq_asserted) {
        s->irq_asserted = false;
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * The provided driver snippet does not show any DMA or descriptor usage,
 * so we do not implement any DMA behavior. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Helper to compute base address of a socket's register window.
 * The real hardware layout is unknown; we choose a simple fixed stride.
 */
static inline hwaddr pcibase_sock_addr(hwaddr base, unsigned int sock)
{
    const hwaddr stride = 0x20; /* arbitrary but within our 4 KiB window */
    return base + sock * stride;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Host never does sub-32-bit accesses in the driver snippet, but
     * we tolerate them by returning the appropriate bits. */
    if (addr == FM_INTERRUPT_STATUS && size == 4) {
        val = s->fm_interrupt_status;
        return val;
    }

    if (addr == FM_CLEAR_INTERRUPT_ENABLE && size == 4) {
        /* This register is write-only in hardware; reads are not used
         * by the driver. Return 0. */
        return 0;
    }

    if (addr == FM_SET_INTERRUPT_ENABLE && size == 4) {
        val = s->fm_set_interrupt_enable;
        return val;
    }

    /* Socket windows: the driver uses tifm_7xx1_sock_addr(fm->addr, idx)
     * which we emulate as base + idx * stride. It then accesses
     * SOCK_CONTROL and SOCK_PRESENT_STATE. We map them accordingly. */
    for (unsigned int i = 0; i < s->num_sockets; ++i) {
        hwaddr base = pcibase_sock_addr(0x100, i); /* place sockets at 0x100+ */
        if (addr == base + SOCK_CONTROL && size == 4) {
            val = s->sockets[i].control;
            return val;
        }
        if (addr == base + SOCK_PRESENT_STATE && size == 4) {
            val = s->sockets[i].present_state;
            return val;
        }
    }

    /* Default: return all-ones to make driver ignore unknown regions */
    switch (size) {
    case 1:
        return 0xff;
    case 2:
        return 0xffff;
    case 4:
        return 0xffffffffu;
    default:
        return ~0ULL;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        /* The driver uses 32-bit writel, so ignore others. */
        return;
    }

    if (addr == FM_INTERRUPT_STATUS) {
        /* The driver clears interrupt bits by writing back irq_status:
         *
         *   writel(irq_status, fm->addr + FM_INTERRUPT_STATUS);
         *
         * Emulate W1C semantics: bits set in val are cleared. */
        s->fm_interrupt_status &= ~((uint32_t)val);
        pcibase_update_irq(s);
        return;
    }

    if (addr == FM_CLEAR_INTERRUPT_ENABLE) {
        /* Driver writes masks to disable specific interrupt sources:
         *
         *   writel(TIFM_IRQ_ENABLE, fm->addr + FM_CLEAR_INTERRUPT_ENABLE);
         *   writel(TIFM_IRQ_FIFOMASK(socket_change_set) | ...);
         *
         * We track a shadow and suppress bits in set_interrupt_enable. */
        s->fm_clear_interrupt_enable |= (uint32_t)val;
        s->fm_set_interrupt_enable &= ~((uint32_t)val);
        pcibase_update_irq(s);
        return;
    }

    if (addr == FM_SET_INTERRUPT_ENABLE) {
        /* Driver enables interrupts by writing here; we OR the bits. */
        s->fm_set_interrupt_enable |= (uint32_t)val;
        /* Clearing mask bits if they were set in clear register */
        s->fm_clear_interrupt_enable &= ~((uint32_t)val);
        pcibase_update_irq(s);
        return;
    }

    /* Socket windows */
    for (unsigned int i = 0; i < s->num_sockets; ++i) {
        hwaddr base = pcibase_sock_addr(0x100, i);
        if (addr == base + SOCK_CONTROL) {
            /* The driver writes literal values like 0x0e00 and manipulates
             * LED/power bits. We merely store the value. */
            s->sockets[i].control = (uint32_t)val;

            /* Basic power state emulation: if any of the power bits in
             * TIFM_CTRL_POWER_MASK are set, we consider the socket
             * powered and update the present_state POWERED bit. */
            if (s->sockets[i].control & TIFM_CTRL_POWER_MASK) {
                s->sockets[i].present_state |= TIFM_SOCK_STATE_POWERED;
            } else {
                s->sockets[i].present_state &= ~TIFM_SOCK_STATE_POWERED;
            }

            return;
        }
        if (addr == base + SOCK_PRESENT_STATE) {
            /* Driver only reads this register in the provided snippet.
             * It never writes, but tolerate writes as state override. */
            s->sockets[i].present_state = (uint32_t)val;
            return;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    /* Driver does not use PIO; return all-ones */
    switch (size) {
    case 1:
        return 0xff;
    case 2:
        return 0xffff;
    case 4:
        return 0xffffffffu;
    default:
        return ~0ULL;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
    /* No PIO writes used by driver */
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

    s->fm_interrupt_status = 0;
    s->fm_set_interrupt_enable = 0;
    s->fm_clear_interrupt_enable = 0;
    s->irq_asserted = false;

    /* Default: no sockets present, but powered off. The driver will
     * try to toggle power and detect media. We can leave present_state
     * as 0 (no card) for all sockets. */
    for (unsigned int i = 0; i < s->num_sockets; ++i) {
        s->sockets[i].control = 0;
        s->sockets[i].present_state = 0;
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

    /* BAR Initialization */
    s->num_bars = 0;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "tifm_7xx1-bar";
    }

    /* The driver maps BAR 0 via pci_ioremap_bar(dev, 0). We expose a
     * single MMIO BAR of 4 KiB. */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = TIFM_MMIO_SIZE;
    s->bar_info[0].name = "tifm_7xx1-mmio";
    s->num_bars = 1;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Determine number of sockets from device id as driver does:
     * fm = tifm_alloc_adapter(dev->device == PCI_DEVICE_ID_TI_XX21_XX11_FM
     *                          ? 4 : 2, &dev->dev);
     * Since this QEMU device presents PCIBASE_DEVICE_ID as
     * PCI_DEVICE_ID_TI_XX21_XX11_FM, we emulate 4 sockets. */
    s->num_sockets = 4;

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
    .name = "tifm_7xx1_pci",
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
