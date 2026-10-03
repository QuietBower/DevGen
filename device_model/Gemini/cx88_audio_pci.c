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


#define TYPE_PCIBASE_DEVICE "cx88_audio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CX88_VENDOR_ID 0x14f1
#define CX88_DEVICE_ID 0x8801

#define MO_AUD_INTMSK       0x200060
#define MO_DEV_CNTRL2       0x200034
#define MO_PCI_INTMSK       0x200040
#define MO_AUDD_GPCNTRL     0x32C030
#define MO_AUD_INTSTAT      0x200064
#define MO_AUDD_LNGTH       0x32C048
#define MO_AUD_DMACNTRL     0x32C040
#define MO_AUDD_GPCNT       0x32C020
#define MO_PCI_INTSTAT      0x200044
#define AUD_BAL_CTL         0x320598
#define AUD_VOL_CTL         0x320594
#define MO_SAMPLE_IO        0x35C058
#define MO_VID_INTSTAT      0x200054
#define MO_GP2_IO           0x350018
#define MO_GP0_IO           0x350010
#define MO_GP1_IO           0x350014
#define MO_SRST_IO          0x35C05C
#define MO_VID_DMACNTRL     0x31C040
#define MO_VID_INTMSK       0x200050
#define VID_CAPTURE_CONTROL 0x310180
#define MO_GPHST_INTMSK     0x200090
#define MO_VIP_INTMSK       0x200080
#define MO_GPHST_DMACNTRL   0x38C040
#define MO_TS_DMACNTRL      0x33C040
#define MO_VIP_DMACNTRL     0x34C040
#define MO_TS_INTMSK        0x200070
#define MO_I2C              0x368000

#define AUD_INT_DN_SYNC     (1 << 12)
#define AUD_INT_DN_RISCI2   (1 <<  4)
#define AUD_INT_DN_RISCI1   (1 <<  0)
#define PCI_INT_AUDINT      (1 <<  1)
#define AUD_INT_OPC_ERR     (1 << 16)
#define PCI_INT_IR_SMPINT   (1 << 18)
#define PCI_INT_RISC_RD_BERRINT (1 << 10)
#define PCI_INT_RISC_WR_BERRINT (1 << 11)
#define PCI_INT_SRC_DMA_BERRINT (1 << 13)
#define PCI_INT_IPB_DMA_BERRINT (1 << 15)
#define PCI_INT_DST_DMA_BERRINT (1 << 14)
#define PCI_INT_BRDG_BERRINT    (1 << 12)

#define RISC_JUMP           0x70000000
#define RISC_IRQ1           0x01000000
#define RISC_CNT_INC        0x00010000
#define RISC_WRITEC         0x50000000
#define RISC_WRITE          0x10000000
#define RISC_SKIP           0x20000000
#define RISC_SYNC           0x80000000
#define RISC_WRITERM        0xB0000000
#define RISC_WRITECM        0xC0000000
#define RISC_READC          0xA0000000
#define RISC_READ           0x90000000
#define RISC_WRITECR        0xD0000000
#define RISC_EOL            0x04000000
#define RISC_RESYNC         0x80008000
#define RISC_SOL            0x08000000

#define GP_COUNT_CONTROL_RESET 0x3
#define SRAM_CH25 4

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
    uint32_t pci_intmsk;
    uint32_t pci_intstat;
    uint32_t aud_intmsk;
    uint32_t aud_intstat;
    uint32_t vid_intmsk;
    uint32_t vid_intstat;
    uint32_t ts_intmsk;
    uint32_t vip_intmsk;
    uint32_t gphst_intmsk;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t dev_cntrl2;
    uint32_t audd_gpcntrl;
    uint32_t audd_length;
    uint32_t aud_dmacntrl;
    uint32_t audd_gpcnt;
    uint32_t aud_bal_ctl;
    uint32_t aud_vol_ctl;
    uint32_t sample_io;
    uint32_t gp0_io;
    uint32_t gp1_io;
    uint32_t gp2_io;
    uint32_t srst_io;
    uint32_t vid_dmacntrl;
    uint32_t vid_capture_control;
    uint32_t gphst_dmacntrl;
    uint32_t ts_dmacntrl;
    uint32_t vip_dmacntrl;
    uint32_t i2c;

    /* DMA Context */
    dma_addr_t risc_dma_addr;
    uint32_t risc_instructions[1024];

};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = false;

    if (s->aud_intstat & s->aud_intmsk) {
        s->pci_intstat |= PCI_INT_AUDINT;
    } else {
        s->pci_intstat &= ~PCI_INT_AUDINT;
    }

    if (s->pci_intstat & s->pci_intmsk) {
        level = true;
    }

    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* DMA descriptor base is set via SRAM channels not directly exposed in MMIO macros here. */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MO_AUD_INTMSK:
        val = s->aud_intmsk;
        break;
    case MO_DEV_CNTRL2:
        val = s->dev_cntrl2;
        break;
    case MO_PCI_INTMSK:
        val = s->pci_intmsk;
        break;
    case MO_AUDD_GPCNTRL:
        val = s->audd_gpcntrl;
        break;
    case MO_AUD_INTSTAT:
        val = s->aud_intstat;
        break;
    case MO_AUDD_LNGTH:
        val = s->audd_length;
        break;
    case MO_AUD_DMACNTRL:
        val = s->aud_dmacntrl;
        break;
    case MO_AUDD_GPCNT:
        val = s->audd_gpcnt;
        break;
    case MO_PCI_INTSTAT:
        val = s->pci_intstat;
        break;
    case AUD_BAL_CTL:
        val = s->aud_bal_ctl;
        break;
    case AUD_VOL_CTL:
        val = s->aud_vol_ctl;
        break;
    case MO_SAMPLE_IO:
        val = s->sample_io;
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
    case MO_AUD_INTMSK:
        s->aud_intmsk = val;
        pcibase_update_irq(s);
        break;
    case MO_DEV_CNTRL2:
        s->dev_cntrl2 = val;
        break;
    case MO_PCI_INTMSK:
        s->pci_intmsk = val;
        pcibase_update_irq(s);
        break;
    case MO_AUDD_GPCNTRL:
        s->audd_gpcntrl = val;
        if (val == GP_COUNT_CONTROL_RESET) {
            s->audd_gpcnt = 0;
        }
        break;
    case MO_AUD_INTSTAT:
        s->aud_intstat &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case MO_AUDD_LNGTH:
        s->audd_length = val;
        break;
    case MO_AUD_DMACNTRL:
        s->aud_dmacntrl = val;
        pcibase_do_dma(s, true);
        break;
    case MO_PCI_INTSTAT:
        s->pci_intstat &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case AUD_BAL_CTL:
        s->aud_bal_ctl = val;
        break;
    case AUD_VOL_CTL:
        s->aud_vol_ctl = val;
        break;
    case MO_SAMPLE_IO:
        s->sample_io = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    
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

    s->aud_intmsk = 0;
    s->dev_cntrl2 = 0;
    s->pci_intmsk = 0;
    s->audd_gpcntrl = 0;
    s->aud_intstat = 0;
    s->audd_length = 0;
    s->aud_dmacntrl = 0;
    s->audd_gpcnt = 0;
    s->pci_intstat = 0;
    s->aud_bal_ctl = 0;
    s->aud_vol_ctl = 0;
    s->sample_io = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x14f1 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8801 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_OTHER );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x400000;
    s->bar_info[0].name = "cx88-mmio";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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

    
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cx88_audio_pci",
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
