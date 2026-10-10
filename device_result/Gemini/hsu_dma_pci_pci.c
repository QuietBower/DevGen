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

#define TYPE_PCIBASE_DEVICE "hsu_dma_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_MFLD_HSU_DMA 0x081e
#define PCI_DEVICE_ID_INTEL_MRFLD_HSU_DMA 0x1192

#define HSU_PCI_DMASR       0x00
#define HSU_PCI_DMAISR      0x04
#define HSU_PCI_CHAN_OFFSET 0x100

#define HSU_CH_SR           0x00
#define HSU_CH_SR_DESCE_ANY GENMASK(19, 16)
#define HSU_CH_SR_CDESC_ANY GENMASK(31, 30)
#define HSU_CH_SR_DESCTO_ANY GENMASK(11, 8)
#define HSU_CH_SR_CHE       BIT(15)

#define HSU_CH_CR           0x04
#define HSU_CH_CR_CHA       BIT(0)
#define HSU_CH_CR_CHD       BIT(1)

#define HSU_CH_DCR          0x08
#define HSU_CH_DCR_DESCA(x) BIT(0 + (x))
#define HSU_CH_DCR_CHSOD(x) BIT(8 + (x))
#define HSU_CH_DCR_CHSOE    BIT(15)
#define HSU_CH_DCR_CHDI(x)  BIT(16 + (x))
#define HSU_CH_DCR_CHEI     BIT(23)
#define HSU_CH_DCR_CHTOI(x) BIT(24 + (x))

#define HSU_CH_BSR          0x10
#define HSU_CH_MTSR         0x14
#define HSU_CH_DxSAR(x)     (0x20 + 8 * (x))
#define HSU_CH_DxTSR(x)     (0x24 + 8 * (x))
#define HSU_CH_DxTSR_MASK   GENMASK(15, 0)

#define HSU_DMA_CHAN_LENGTH 0x40
#define HSU_DMA_CHAN_NR_DESC 4

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
    uint32_t dmaisr;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t dmasr;
    uint8_t chan_regs[0x1000 - HSU_PCI_CHAN_OFFSET];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->dmaisr != 0) {
        if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msi || !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == HSU_PCI_DMASR) {
        val = s->dmasr;
    } else if (addr == HSU_PCI_DMAISR) {
        val = s->dmaisr;
    } else if (addr >= HSU_PCI_CHAN_OFFSET && addr + size <= 0x1000) {
        memcpy(&val, &s->chan_regs[addr - HSU_PCI_CHAN_OFFSET], size);
        
        /* Reading HSU_CH_SR clears the IRQ status */
        if (size == 4 && ((addr - HSU_PCI_CHAN_OFFSET) % HSU_DMA_CHAN_LENGTH) == HSU_CH_SR) {
            uint32_t zero = 0;
            memcpy(&s->chan_regs[addr - HSU_PCI_CHAN_OFFSET], &zero, 4);
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == HSU_PCI_DMASR) {
        s->dmasr = val;
    } else if (addr == HSU_PCI_DMAISR) {
        s->dmaisr = val;
        pcibase_update_irq(s);
    } else if (addr >= HSU_PCI_CHAN_OFFSET && addr + size <= 0x1000) {
        memcpy(&s->chan_regs[addr - HSU_PCI_CHAN_OFFSET], &val, size);
        
        if (size == 4) {
            uint32_t chan_offset = (addr - HSU_PCI_CHAN_OFFSET) % HSU_DMA_CHAN_LENGTH;
            uint32_t chan_idx = (addr - HSU_PCI_CHAN_OFFSET) / HSU_DMA_CHAN_LENGTH;
            
            if (chan_offset == HSU_CH_CR) {
                if (val & HSU_CH_CR_CHA) {
                    uint32_t dcr;
                    memcpy(&dcr, &s->chan_regs[chan_idx * HSU_DMA_CHAN_LENGTH + HSU_CH_DCR], 4);
                    
                    for (int i = 0; i < HSU_DMA_CHAN_NR_DESC; i++) {
                        if (dcr & HSU_CH_DCR_DESCA(i)) {
                            uint32_t sar, tsr;
                            memcpy(&sar, &s->chan_regs[chan_idx * HSU_DMA_CHAN_LENGTH + HSU_CH_DxSAR(i)], 4);
                            memcpy(&tsr, &s->chan_regs[chan_idx * HSU_DMA_CHAN_LENGTH + HSU_CH_DxTSR(i)], 4);
                            
                            uint32_t len = tsr & 0xFFFF; /* HSU_CH_DxTSR_MASK */
                            if (len > 0) {
                                uint32_t chunk = len > 4096 ? 4096 : len;
                                uint8_t *dummy = g_malloc0(chunk);
                                if (!(val & HSU_CH_CR_CHD)) {
                                    pci_dma_read(PCI_DEVICE(s), sar, dummy, chunk);
                                } else {
                                    pci_dma_write(PCI_DEVICE(s), sar, dummy, chunk);
                                }
                                g_free(dummy);
                            }
                            
                            uint32_t zero = 0;
                            memcpy(&s->chan_regs[chan_idx * HSU_DMA_CHAN_LENGTH + HSU_CH_DxTSR(i)], &zero, 4);
                        }
                    }
                }
            }
        }
    }
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

    s->dmasr = 0;
    s->dmaisr = 0;
    memset(s->chan_regs, 0, sizeof(s->chan_regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_MFLD_HSU_DMA );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SYSTEM_DMA );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "hsu_dma-bar0";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
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
    .name = "hsu_dma_pci_pci",
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
