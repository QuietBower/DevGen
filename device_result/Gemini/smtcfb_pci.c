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


#define TYPE_PCIBASE_DEVICE "smtcfb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SMTC_VENDOR_ID 0x126f
#define SMTC_DEVICE_ID 0x0710
#define SMTC_CLASS_ID  0x0300

#define MMIO_ADDR 0x00c00000
#define BIG_ADDR  0

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t chip_id;
    uint8_t chip_rev_id;
    uint32_t colreg[17];

    uint8_t *vram;
    uint32_t vp_regs[256];

    uint8_t seq_index;
    uint8_t seq_regs[256];
    uint8_t crtc_index;
    uint8_t crtc_regs[256];
    uint8_t grph_index;
    uint8_t grph_regs[256];
    uint8_t attr_index;
    uint8_t attr_regs[256];
    uint8_t attr_state;
    uint8_t misc_reg;
    uint8_t pel_mask;
    uint8_t dac_reg;
    uint8_t dac_val;
};

#define SIZE_CR30_CR4D      (0x4D - 0x30 + 1)
#define SIZE_SR10_SR24      (0x24 - 0x10 + 1)
#define SIZE_SRA0_SRAF      (0xAF - 0xA0 + 1)
#define SIZE_CR90_CRA7      (0xA7 - 0x90 + 1)
#define SIZE_SR00_SR04      (0x04 - 0x00 + 1)
#define SIZE_SR80_SR93      (0x93 - 0x80 + 1)
#define SIZE_CR00_CR18      (0x18 - 0x00 + 1)
#define SIZE_GR00_GR08      (0x08 - 0x00 + 1)
#define SIZE_AR00_AR14      (0x14 - 0x00 + 1)
#define SIZE_SR30_SR75      (0x75 - 0x30 + 1)

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x00400000) {
        if (s->vram) {
            if (size == 1) val = s->vram[addr];
            else if (size == 2) val = *(uint16_t *)(&s->vram[addr]);
            else if (size == 4) val = *(uint32_t *)(&s->vram[addr]);
        }
        return val;
    }

    if (addr >= 0x0040c000 && addr < 0x0040c400) {
        uint32_t idx = (addr - 0x0040c000) / 4;
        return s->vp_regs[idx];
    }

    if (addr >= 0x00700000 && addr < 0x00701000) {
        hwaddr offset = addr - 0x00700000;
        switch (offset) {
            case 0x3c4: val = s->seq_index; break;
            case 0x3c5: val = s->seq_regs[s->seq_index]; break;
            case 0x3ce: val = s->grph_index; break;
            case 0x3cf: val = s->grph_regs[s->grph_index]; break;
            case 0x3d4: val = s->crtc_index; break;
            case 0x3d5: val = s->crtc_regs[s->crtc_index]; break;
            case 0x3c0: val = s->attr_index; break;
            case 0x3c1: val = s->attr_regs[s->attr_index]; break;
            case 0x3da: s->attr_state = 0; val = 0; break;
            case 0x3c2: val = s->misc_reg; break;
            case 0x3c6: val = s->pel_mask; break;
            case 0x3c8: val = s->dac_reg; break;
            case 0x3c9: val = s->dac_val; break;
        }
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x00400000) {
        if (s->vram) {
            if (size == 1) s->vram[addr] = val;
            else if (size == 2) *(uint16_t *)(&s->vram[addr]) = val;
            else if (size == 4) *(uint32_t *)(&s->vram[addr]) = val;
        }
        return;
    }

    if (addr >= 0x0040c000 && addr < 0x0040c400) {
        uint32_t idx = (addr - 0x0040c000) / 4;
        s->vp_regs[idx] = val;
        return;
    }

    if (addr >= 0x00700000 && addr < 0x00701000) {
        hwaddr offset = addr - 0x00700000;
        switch (offset) {
            case 0x3c4: s->seq_index = val; break;
            case 0x3c5: s->seq_regs[s->seq_index] = val; break;
            case 0x3ce: s->grph_index = val; break;
            case 0x3cf: s->grph_regs[s->grph_index] = val; break;
            case 0x3d4: s->crtc_index = val; break;
            case 0x3d5: s->crtc_regs[s->crtc_index] = val; break;
            case 0x3c0:
                if (s->attr_state == 0) {
                    s->attr_index = val;
                    s->attr_state = 1;
                } else {
                    s->attr_regs[s->attr_index] = val;
                    s->attr_state = 0;
                }
                break;
            case 0x3c2: s->misc_reg = val; break;
            case 0x3c6: s->pel_mask = val; break;
            case 0x3c8: s->dac_reg = val; break;
            case 0x3c9: s->dac_val = val; break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case 0x3c4: val = s->seq_index; break;
        case 0x3c5: val = s->seq_regs[s->seq_index]; break;
        case 0x3c8: val = s->dac_reg; break;
        case 0x3c9: val = s->dac_val; break;
    }
    
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
        case 0x3c4: s->seq_index = val; break;
        case 0x3c5: s->seq_regs[s->seq_index] = val; break;
        case 0x3c8: s->dac_reg = val; break;
        case 0x3c9: s->dac_val = val; break;
    }
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

    s->chip_id = SMTC_DEVICE_ID;
    s->chip_rev_id = 0x01;
    memset(s->seq_regs, 0, sizeof(s->seq_regs));
    memset(s->crtc_regs, 0, sizeof(s->crtc_regs));
    memset(s->grph_regs, 0, sizeof(s->grph_regs));
    memset(s->attr_regs, 0, sizeof(s->attr_regs));
    memset(s->vp_regs, 0, sizeof(s->vp_regs));
    s->attr_state = 0;
    s->dac_reg = 0;
    s->dac_val = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  SMTC_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  SMTC_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, SMTC_CLASS_ID );
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
    s->bar_info[0].size = 0x01000000; /* 16MB to cover MMIO_ADDR (0x00c00000) */
    s->bar_info[0].name = "sm7xxfb";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->vram = g_malloc0(0x01000000);
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

    if (s->vram) {
        g_free(s->vram);
        s->vram = NULL;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "smtcfb_pci",
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
