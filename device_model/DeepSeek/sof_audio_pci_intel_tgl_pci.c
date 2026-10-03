/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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
/* none */

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_tgl_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x0  /* NEED: define:HDA_TGL_LP */

/* BAR indices */
#define HDA_DSP_HDA_BAR 0
#define HDA_DSP_PP_BAR 1
#define HDA_DSP_SPIB_BAR 2
#define HDA_DSP_BAR 4

/* BAR sizes - NEED actual defines */
#define HDA_BAR_SIZE (16 * 1024)      /* GUESS, NEED: define:HDA_BAR_SIZE */
#define PP_BAR_SIZE  (4 * 1024)       /* GUESS, NEED: define:PP_BAR_SIZE */
#define SPIB_BAR_SIZE (4 * 1024)      /* GUESS, NEED: define:SPIB_BAR_SIZE */
#define DSP_BAR_SIZE (4 * 1024 * 1024) /* GUESS, NEED: define:DSP_BAR_SIZE */

/* Register offsets */
#define HDA_DSP_GEN_BASE 0x0
#define HDA_DSP_REG_ADSPCS (HDA_DSP_GEN_BASE + 0x04)
#define HDA_DSP_REG_ADSPIC (HDA_DSP_GEN_BASE + 0x08)
#define HDA_DSP_REG_ADSPIS (HDA_DSP_GEN_BASE + 0x0C)
#define HDA_DSP_REG_ADSPIC2 (HDA_DSP_GEN_BASE + 0x10)
#define HDA_DSP_REG_ADSPIS2 (HDA_DSP_GEN_BASE + 0x14)
#define SOF_HDA_INTCTL 0x20
#define SOF_HDA_INTSTS 0x24
#define CNL_DSP_IPC_BASE 0xC0
#define CNL_DSP_REG_HIPCTDR (CNL_DSP_IPC_BASE + 0x00)
#define CNL_DSP_REG_HIPCTDA (CNL_DSP_IPC_BASE + 0x04)
#define CNL_DSP_REG_HIPCTDD (CNL_DSP_IPC_BASE + 0x08)
#define CNL_DSP_REG_HIPCIDR (CNL_DSP_IPC_BASE + 0x10)
#define CNL_DSP_REG_HIPCIDA (CNL_DSP_IPC_BASE + 0x14)
#define CNL_DSP_REG_HIPCIDD (CNL_DSP_IPC_BASE + 0x18)
#define CNL_DSP_REG_HIPCCTL (CNL_DSP_IPC_BASE + 0x28)
#define HDA_DSP_MBOX_OFFSET SRAM_WINDOW_OFFSET(0)
#define SRAM_WINDOW_OFFSET(x) (0x80000 + (x) * 0x20000)

/* Bitfields */
#define HDA_DSP_REG_HIPCCTL_DONE  BIT(1)
#define HDA_DSP_REG_HIPCCTL_BUSY  BIT(0)
#define HDA_DSP_REG_HIPCIDR_BUSY  BIT(31)
#define HDA_DSP_REG_HIPCTDR_BUSY  BIT(31)

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

typedef struct {
    uint32_t regs[DSP_BAR_SIZE / sizeof(uint32_t)];
} DspRegs;

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
    DspRegs dsp_regs;

    /* DMA Context deleted - not used by driver */
    /* timer removed - not used */

    uint32_t status;     /* Operational status flags */
    /* reset_dummy removed */
    uint32_t pm_state;   /* Power management state (D0-D3) */
    /* other_dummy removed */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t adspis = s->dsp_regs.regs[HDA_DSP_REG_ADSPIS / 4];
    uint32_t adspic = s->dsp_regs.regs[HDA_DSP_REG_ADSPIC / 4];
    uint32_t intsts = s->dsp_regs.regs[SOF_HDA_INTSTS / 4];
    uint32_t intctl = s->dsp_regs.regs[SOF_HDA_INTCTL / 4];

    bool raise = false;
    if ((adspis & adspic) || (intsts & intctl)) {
        raise = true;
    }
    if (raise) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* No DMA logic needed - driver does not use DMA from this device */

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= DSP_BAR_SIZE) {
        return ~0ULL;
    }

    uint32_t *regp = &s->dsp_regs.regs[addr / 4];

    if (size == 4) {
        val = *regp;
    } else if (size == 2) {
        uint32_t shift = (addr & 3) * 8;
        val = (*regp >> shift) & 0xFFFF;
    } else if (size == 1) {
        uint32_t shift = (addr & 3) * 8;
        val = (*regp >> shift) & 0xFF;
    } else if (size == 8) {
        uint64_t lo = (uint64_t)regp[0];
        uint64_t hi = (uint64_t)regp[1];
        val = lo | (hi << 32);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t *regp = &s->dsp_regs.regs[addr / 4];
    uint32_t new_val;

    if (addr >= DSP_BAR_SIZE) {
        return;
    }

    if (size == 4) {
        new_val = val;
    } else if (size == 2) {
        uint32_t shift = (addr & 3) * 8;
        new_val = (*regp & ~(0xFFFF << shift)) | ((val & 0xFFFF) << shift);
    } else if (size == 1) {
        uint32_t shift = (addr & 3) * 8;
        new_val = (*regp & ~(0xFF << shift)) | ((val & 0xFF) << shift);
    } else if (size == 8) {
        regp[0] = val & 0xFFFFFFFF;
        regp[1] = val >> 32;
        return;
    } else {
        return;
    }

    /* Handle W1C and special registers */
    switch (addr) {
    case HDA_DSP_REG_ADSPIS:
    case HDA_DSP_REG_ADSPIS2:
    case SOF_HDA_INTSTS:
        *regp = *regp & ~new_val;
        pcibase_update_irq(s);
        return;
    case CNL_DSP_REG_HIPCCTL:
        if (new_val & HDA_DSP_REG_HIPCCTL_BUSY) {
            /* Simulate immediate IPC completion: set DONE, clear BUSY */
            new_val = (new_val & ~HDA_DSP_REG_HIPCCTL_BUSY) | HDA_DSP_REG_HIPCCTL_DONE;
            /* Optionally set interrupt status bit to trigger IRQ? Driver may handle separately */
            s->dsp_regs.regs[HDA_DSP_REG_ADSPIS / 4] |= BIT(0); /* set IPC interrupt */
        }
        *regp = new_val;
        pcibase_update_irq(s);
        return;
    default:
        *regp = new_val;
        /* Update interrupts on any write to interrupt-related registers */
        if (addr == HDA_DSP_REG_ADSPIC || addr == SOF_HDA_INTCTL) {
            pcibase_update_irq(s);
        }
        break;
    }
}

/* PIO handlers: unused by driver, minimal empty implementation */
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

    /* Reset register defaults */
    memset(&s->dsp_regs, 0, sizeof(s->dsp_regs));
    s->dsp_regs.regs[CNL_DSP_REG_HIPCCTL / 4] = HDA_DSP_REG_HIPCCTL_DONE; /* IPC ready */
    /* Other reset values could be added if driver expects them */
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
    pci_set_byte(pci_conf + 0x09, 0x04);         /* base class Multimedia audio */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0300); /* subclass 0x03, prog-if 0x00 */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){ .index = HDA_DSP_HDA_BAR, .type = BAR_TYPE_MMIO, .size = HDA_BAR_SIZE, .name = "hda-bar" };
    s->bar_info[1] = (BARInfo){ .index = HDA_DSP_PP_BAR, .type = BAR_TYPE_MMIO, .size = PP_BAR_SIZE, .name = "pp-bar" };
    s->bar_info[2] = (BARInfo){ .index = HDA_DSP_SPIB_BAR, .type = BAR_TYPE_MMIO, .size = SPIB_BAR_SIZE, .name = "spib-bar" };
    s->bar_info[3] = (BARInfo){ .index = HDA_DSP_BAR, .type = BAR_TYPE_MMIO, .size = DSP_BAR_SIZE, .name = "dsp-bar" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI init: enable MSI */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;

    /* DMA configuration not needed */

    /* no timer config needed */

    /* Final state initialization */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No DMA buffers or timers to clean up */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sof_audio_pci_intel_tgl_pci",
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

static void pcibase_instance_init(Object *obj)
{
    PCIBaseState *s = PCIBASE_DEVICE(obj);
    /* Clear state */
    memset(s, 0, sizeof(*s));
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
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
