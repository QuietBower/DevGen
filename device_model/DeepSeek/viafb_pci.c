/*
 * QEMU VIA Framebuffer PCI Device Model (viafb)
 *
 * Emulates the basic functionality required for the viafb driver to probe and initialize.
 * Includes function 0 (video/engine) and function 3 (config for FB size).
 *
 * Based on driver source from drivers/video/fbdev/via/via-core.c
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
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

#define TYPE_PCIBASE_DEVICE "viafb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_VIA        0x1106
#define UNICHROME_CLE266_DID     0x3122
#define DEVICE_ID                UNICHROME_CLE266_DID
#define CLASS_ID                 0x0300   /* PCI_CLASS_DISPLAY_VGA */

/* MMIO register offsets from driver source */
#define VDE_INTERRUPT            0x200  /* Interrupt enable/mask register */
#define VDMA_CSR0                0xe04  /* DMA control/status register */
#define VDMA_MR0                 0xe00  /* DMA mode register */
#define VDMA_DPRL0               0xe34  /* DMA descriptor low address */
#define VDMA_DPRH0               0xe38  /* DMA descriptor high address */
#define VDMA_DQWCR0              0xe2c  /* DMA queue write count (?) */

/* Bit definitions from driver source */
#define VDE_I_ENABLE             0x80000000
#define VDE_I_DMA0TDEN           0x00200000
#define VDMA_C_DONE              0x08
#define VDMA_C_ENABLE            0x01
#define VDMA_C_START             0x02
#define VDMA_MR_TDIE             0x02
#define VDMA_MR_CHAIN            0x01

/* Other driver constants */
#define VIAFB_DMA_MAGIC          0x01
#define VIAFB_DMA_FINAL_SEGMENT  0x02

/* PCI IDs for function 3 (CLE266) */
#define CLE266_FUNCTION3_DID     0x3123  /* Assumption: typical +1 */

/* BAR sizes */
#define BAR0_SIZE                (512 * MiB)  /* Framebuffer RAM */
#define BAR1_SIZE                (16 * KiB)   /* Engine MMIO */

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint32_t vde_interrupt;
    uint32_t vdma_csr0;
    uint32_t vdma_mr0;
    uint32_t vdma_dprl0;
    uint32_t vdma_dprh0;
    uint32_t vdma_dqwcr0;

    /* DMA context */
    QEMUTimer dma_timer;

    uint32_t status;

    uint8_t pm_state;
};

/* Function 3 device: simple PCI configuration-only device */
#define TYPE_VIAFB_FUNC3_DEVICE "viafb_func3"
typedef struct VIAFBFunc3State VIAFBFunc3State;
DECLARE_INSTANCE_CHECKER(VIAFBFunc3State, VIAFB_FUNC3,
                         TYPE_VIAFB_FUNC3_DEVICE)

struct VIAFBFunc3State {
    PCIDevice pdev;
    /* No BARs needed */
};

/* Forward declarations */
static void pcibase_update_irq(PCIBaseState *s);

/* Internal helper for status-triggered signaling (engine MMIO) */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_assert = false;

    if ((s->vde_interrupt & VDE_I_ENABLE) &&
        (s->vde_interrupt & VDE_I_DMA0TDEN) &&
        (s->vdma_csr0 & VDMA_C_DONE)) {
        irq_assert = true;
    }

    if (irq_assert) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void viafb_dma_complete(void *opaque)
{
    PCIBaseState *s = opaque;

    s->vdma_csr0 |= VDMA_C_DONE;
    if (s->vdma_mr0 & VDMA_MR_TDIE) {
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "viafb: MMIO read with size %d\n", size);
        return 0;
    }

    switch (addr) {
    case VDE_INTERRUPT:
        val = s->vde_interrupt;
        break;
    case VDMA_CSR0:
        val = s->vdma_csr0;
        break;
    case VDMA_MR0:
        val = s->vdma_mr0;
        break;
    case VDMA_DPRL0:
        val = s->vdma_dprl0;
        break;
    case VDMA_DPRH0:
        val = s->vdma_dprh0;
        break;
    case VDMA_DQWCR0:
        val = s->vdma_dqwcr0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "viafb: unknown MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "viafb: MMIO write with size %d\n", size);
        return;
    }

    switch (addr) {
    case VDE_INTERRUPT:
        s->vde_interrupt = val;
        pcibase_update_irq(s);
        break;
    case VDMA_CSR0:
        if (val & VDMA_C_DONE) {
            s->vdma_csr0 &= ~VDMA_C_DONE;
        }
        if (val & VDMA_C_ENABLE) {
            s->vdma_csr0 |= VDMA_C_ENABLE;
        }
        if (val & VDMA_C_START) {
            s->vdma_csr0 &= ~VDMA_C_DONE;
            timer_mod(&s->dma_timer,
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
        }
        pcibase_update_irq(s);
        break;
    case VDMA_MR0:
        s->vdma_mr0 = val;
        break;
    case VDMA_DPRL0:
        s->vdma_dprl0 = val;
        break;
    case VDMA_DPRH0:
        s->vdma_dprh0 = val;
        break;
    case VDMA_DQWCR0:
        s->vdma_dqwcr0 = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "viafb: unknown MMIO write at 0x%" HWADDR_PRIx " = 0x%" PRIx64 "\n", addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "viafb: unexpected PIO read at 0x%" HWADDR_PRIx "\n", addr);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "viafb: unexpected PIO write at 0x%" HWADDR_PRIx " = 0x%" PRIx64 "\n", addr, val);
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

    s->vde_interrupt = 0;
    s->vdma_csr0 = 0;
    s->vdma_mr0 = 0;
    s->vdma_dprl0 = 0;
    s->vdma_dprh0 = 0;
    s->vdma_dqwcr0 = 0;
    pci_set_irq(PCI_DEVICE(dev), 0);
}

static void func3_realize(PCIDevice *pdev, Error **errp)
{
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_VIA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, CLE266_FUNCTION3_DID);
    /* Set the framebuffer size indication at offset 0xE0 for CLE266 */
    pci_set_long(pci_conf + 0xE0, 0x00005000);  /* 32MB */
}

static void func3_exit(PCIDevice *pdev)
{
}

static void func3_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = func3_realize;
    k->exit = func3_exit;
}

static void func3_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo func3_info = {
        .name = TYPE_VIAFB_FUNC3_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(VIAFBFunc3State),
        .class_init = func3_class_init,
        .interfaces = interfaces,
    };
    type_register_static(&func3_info);
}

type_init(func3_register_types);

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
    int pm_pos;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_VIA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_RAM,
        .size = BAR0_SIZE,
        .name = "viafb-framebuffer"
    };
    s->bar_info[1] = (BARInfo) {
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = BAR1_SIZE,
        .name = "viafb-mmio"
    };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    timer_init_ms(&s->dma_timer, QEMU_CLOCK_VIRTUAL, viafb_dma_complete, s);
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
    timer_del(&s->dma_timer);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "viafb_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(vde_interrupt, PCIBaseState),
        VMSTATE_UINT32(vdma_csr0, PCIBaseState),
        VMSTATE_UINT32(vdma_mr0, PCIBaseState),
        VMSTATE_UINT32(vdma_dprl0, PCIBaseState),
        VMSTATE_UINT32(vdma_dprh0, PCIBaseState),
        VMSTATE_UINT32(vdma_dqwcr0, PCIBaseState),
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
