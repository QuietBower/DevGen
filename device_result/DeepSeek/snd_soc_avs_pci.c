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

#define TYPE_PCIBASE_DEVICE "snd_soc_avs_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI IDs: will be defined by requested defines */
#define VENDOR_ID  0x8086
#define DEVICE_ID  0x9d70
#define CLASS_ID   0x0403  /* HD Audio Multimedia Device */

/* Register offsets from driver */
#define AVS_ADSP_GEN_BASE           0x0
#define AVS_ADSPCS_OFFSET           (AVS_ADSP_GEN_BASE + 0x04)
#define AVS_ADSPIC_OFFSET           (AVS_ADSP_GEN_BASE + 0x08)
#define AVS_ADSPIS_OFFSET           (AVS_ADSP_GEN_BASE + 0x0C)
#define SKL_ADSP_IPC_BASE           0x40
#define SKL_ADSP_HIPCT_OFFSET       (SKL_ADSP_IPC_BASE + 0x00)
#define SKL_ADSP_HIPCTE_OFFSET      (SKL_ADSP_IPC_BASE + 0x04)
#define SKL_ADSP_HIPCI_OFFSET       (SKL_ADSP_IPC_BASE + 0x08)
#define SKL_ADSP_HIPCIE_OFFSET      (SKL_ADSP_IPC_BASE + 0x0C)
#define SKL_ADSP_HIPCCTL_OFFSET     (SKL_ADSP_IPC_BASE + 0x10)
#define AZX_PCIREG_PGCTL_OFFSET     0x44   /* HDA BAR offset */
#define AZX_PCIREG_CGCTL_OFFSET     0x48   /* HDA BAR offset */

/* Standard HD Audio registers (offsets within BAR0) */
#define HDA_REG_GCAP                0x00
#define HDA_REG_INTCTL              0x20
#define HDA_REG_INTSTS              0x24
#define AZX_INT_GLOBAL_EN           BIT(31)

/* Bitfield masks */
#define AVS_ADSPCS_CRST_MASK(cm)    (cm)
#define AVS_ADSPCS_SPA_MASK(cm)     ((cm) << 16)
#define AVS_ADSPCS_CSTALL_MASK(cm)  ((cm) << 8)
#define AVS_ADSPCS_CPA_MASK(cm)     ((cm) << 24)
#define AVS_ADSP_ADSPIS_IPC          BIT(0)
#define AVS_ADSP_ADSPIS_CLDMA        BIT(1)
#define AVS_ADSP_ADSPIC_IPC          BIT(0)
#define AVS_ADSP_ADSPIC_CLDMA        BIT(1)
#define SKL_ADSP_HIPCT_BUSY          BIT(31)
#define SKL_ADSP_HIPCIE_DONE         BIT(30)
#define SKL_ADSP_HIPCI_BUSY          BIT(31)
#define AVS_ADSP_HIPCCTL_BUSY        BIT(0)
#define AVS_ADSP_HIPCCTL_DONE        BIT(1)
#define AZX_VS_EM2_L1SEN             BIT(13)
#define AZX_VS_EM2_DUM               BIT(23)

/* BAR sizes */
#define HDA_BAR0_SIZE               0x4000
#define DSP_BAR4_SIZE               0x1000

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
    struct avs_regs {
        uint32_t adspcs;
        uint32_t adspic;
        uint32_t adspis;
        uint32_t hipct;
        uint32_t hipcte;
        uint32_t hipci;
        uint32_t hipcie;
        uint32_t hipcctl;
        uint32_t pgctl;    /* PCI config at offset 0x44 */
        uint32_t cgctl;    /* PCI config at offset 0x48 */
        /* Standard HDA registers */
        uint32_t gcap;
        uint32_t intctl;
        uint32_t intsts;
    } regs;

    /* DMA Context */

};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & ~s->intr_mask;
    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (msi_enabled(pdev)) {
            ; /* MSI level doesn't need lowering */
        } else {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Phase 2: MMIO read handlers */
    switch (addr) {
    case AVS_ADSPCS_OFFSET:
        val = s->regs.adspcs;
        break;
    case AVS_ADSPIC_OFFSET:
        val = s->regs.adspic;
        break;
    case AVS_ADSPIS_OFFSET:
        val = s->regs.adspis;
        break;
    case SKL_ADSP_HIPCT_OFFSET:
        val = s->regs.hipct;
        break;
    case SKL_ADSP_HIPCTE_OFFSET:
        val = s->regs.hipcte;
        break;
    case SKL_ADSP_HIPCI_OFFSET:
        val = s->regs.hipci;
        break;
    case SKL_ADSP_HIPCIE_OFFSET:
        val = s->regs.hipcie;
        break;
    case SKL_ADSP_HIPCCTL_OFFSET:
        val = s->regs.hipcctl;
        break;
    case HDA_REG_GCAP:
        val = s->regs.gcap;
        break;
    case HDA_REG_INTCTL:
        val = s->regs.intctl;
        break;
    case HDA_REG_INTSTS:
        val = s->regs.intsts;
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

    switch (addr) {
    case AVS_ADSPCS_OFFSET:
        s->regs.adspcs = val;
        break;
    case AVS_ADSPIC_OFFSET:
        s->regs.adspic = val;
        break;
    case AVS_ADSPIS_OFFSET:
        /* W1C: bits written as 1 are cleared */
        s->regs.adspis &= ~val;
        break;
    case SKL_ADSP_HIPCT_OFFSET:
        s->regs.hipct = val;
        break;
    case SKL_ADSP_HIPCTE_OFFSET:
        s->regs.hipcte = val;
        break;
    case SKL_ADSP_HIPCI_OFFSET:
        s->regs.hipci = val;
        break;
    case SKL_ADSP_HIPCIE_OFFSET:
        s->regs.hipcie = val;
        break;
    case SKL_ADSP_HIPCCTL_OFFSET:
        s->regs.hipcctl = val;
        break;
    case HDA_REG_INTCTL:
        s->regs.intctl = val;
        pcibase_update_irq(s);
        break;
    case HDA_REG_INTSTS:
        /* W1C */
        s->regs.intsts &= ~val;
        pcibase_update_irq(s);
        break;
    case HDA_REG_GCAP:
        /* read-only, ignore write */
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void) opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void) opaque;
    
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

    /* Reset implementation in Phase 2 */
    memset(&s->regs, 0, sizeof(s->regs));
    /* Set GCAP to indicate 4 output, 4 input streams, 64-bit support */
    s->regs.gcap = 0x4401;
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

static uint32_t pcibase_pgctl_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (addr == AZX_PCIREG_PGCTL_OFFSET) {
        return s->regs.pgctl;
    } else if (addr == AZX_PCIREG_CGCTL_OFFSET) {
        return s->regs.cgctl;
    } else {
        return pci_default_read_config(pdev, addr, len);
    }
}

static void pcibase_pgctl_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (addr == AZX_PCIREG_PGCTL_OFFSET) {
        s->regs.pgctl = val;
    } else if (addr == AZX_PCIREG_CGCTL_OFFSET) {
        s->regs.cgctl = val;
    } else {
        pci_default_write_config(pdev, addr, val, len);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Enable MSI */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;

    /* Override PCI config read/write for PGCTL and CGCTL */
    pdev->config_read = pcibase_pgctl_read;
    pdev->config_write = pcibase_pgctl_write;

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = HDA_BAR0_SIZE, .name = "hda-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 4, .type = BAR_TYPE_MMIO, .size = DSP_BAR4_SIZE, .name = "dsp-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Field initialization */
    pcibase_reset(DEVICE(pdev));
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
    .name = "snd_soc_avs_pci",
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
