/*
 * QEMU PCI device model for pwm-dwc
 * Phase 2: Behavioral implementation based on Linux driver pwm-dwc.c
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

#define TYPE_PCIBASE_DEVICE "pwm_dwc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID      PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID      0x4bb7
#define PCIBASE_CLASS_ID       PCI_CLASS_OTHERS

#define DWC_TIM_LD_CNT(n)   ((n) * 0x14)
#define DWC_TIM_LD_CNT2(n)  (((n) * 4) + 0xb0)
#define DWC_TIM_CTRL(n)     (((n) * 0x14) + 0x08)
#define DWC_TIMERS_TOTAL    8
#define DWC_TIM_CTRL_PWM    (1U << 3)
#define DWC_TIM_CTRL_EN     (1U << 0)
#define SZ_4K               0x00001000

struct dwc_pwm_ctx_shadow {
    uint32_t cnt;
    uint32_t cnt2;
    uint32_t ctrl;
};

struct dwc_pwm_shadow {
    uint32_t clk_ns;
    struct dwc_pwm_ctx_shadow ctx[DWC_TIMERS_TOTAL];
};

struct dwc_pwm_info_shadow {
    unsigned int nr;
    unsigned int size;
};

struct dwc_pwm_drvdata_shadow {
    struct dwc_pwm_info_shadow info;
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
    struct dwc_pwm_shadow pwm_state;
    struct dwc_pwm_drvdata_shadow drvdata;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver uses readl(), i.e. 32-bit little-endian accesses. */
    if (size != 4) {
        return 0;
    }

    /* PWM timer load count registers */
    for (int i = 0; i < DWC_TIMERS_TOTAL; i++) {
        hwaddr off_cnt  = DWC_TIM_LD_CNT(i);
        hwaddr off_cnt2 = DWC_TIM_LD_CNT2(i);
        hwaddr off_ctrl = DWC_TIM_CTRL(i);

        if (addr == off_cnt) {
            return s->pwm_state.ctx[i].cnt;
        }
        if (addr == off_cnt2) {
            return s->pwm_state.ctx[i].cnt2;
        }
        if (addr == off_ctrl) {
            return s->pwm_state.ctx[i].ctrl;
        }
    }

    /* Unimplemented/unknown offsets read as 0 */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The driver uses writel(), i.e. 32-bit little-endian accesses. */
    if (size != 4) {
        return;
    }

    uint32_t v32 = (uint32_t)val;

    /* PWM timer load count registers */
    for (int i = 0; i < DWC_TIMERS_TOTAL; i++) {
        hwaddr off_cnt  = DWC_TIM_LD_CNT(i);
        hwaddr off_cnt2 = DWC_TIM_LD_CNT2(i);
        hwaddr off_ctrl = DWC_TIM_CTRL(i);

        if (addr == off_cnt) {
            s->pwm_state.ctx[i].cnt = v32;
            return;
        }
        if (addr == off_cnt2) {
            s->pwm_state.ctx[i].cnt2 = v32;
            return;
        }
        if (addr == off_ctrl) {
            s->pwm_state.ctx[i].ctrl = v32;
            return;
        }
    }

    /* Writes to unknown offsets are ignored */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

     /* Logic for Port I/O (Legacy support) - not used by driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;

     /* Logic for Port I/O (Legacy support) - not used by driver */
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
    s->pwm_state.clk_ns = 0;
    for (int i = 0; i < DWC_TIMERS_TOTAL; i++) {
        s->pwm_state.ctx[i].cnt = 0;
        s->pwm_state.ctx[i].cnt2 = 0;
        s->pwm_state.ctx[i].ctrl = 0;
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

    /* Initialize BAR layout expected by driver */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    /* Driver uses ehl_pwm_info: .size = SZ_4K, nr = 2; each instance is SZ_4K.
     * We expose a single BAR of SZ_4K which matches pcim_iomap_region(pci, 0, "pwm-dwc").
     */
    s->bar_info[0].size  = SZ_4K;
    s->bar_info[0].name  = "pwm-dwc-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X, DMA, or timers are used by the current driver */

    /* Initialize internal pwm_state / drvdata shadows consistent with driver info */
    s->drvdata.info.nr = 2;       /* ehl_pwm_info.nr */
    s->drvdata.info.size = SZ_4K; /* ehl_pwm_info.size */
    s->pwm_state.clk_ns = 0;
    for (int i = 0; i < DWC_TIMERS_TOTAL; i++) {
        s->pwm_state.ctx[i].cnt = 0;
        s->pwm_state.ctx[i].cnt2 = 0;
        s->pwm_state.ctx[i].ctrl = 0;
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

     /* Free buffers, stop timers, etc. */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pwm_dwc_pci",
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
