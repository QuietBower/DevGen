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


#define TYPE_PCIBASE_DEVICE "snd_via82xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VIA_REG_OFFSET_STATUS       0x00
#define VIA_REG_OFFSET_CONTROL      0x01
#define VIA_REG_OFFSET_TYPE         0x02
#define VIA_REG_OFFSET_TABLE_PTR    0x04
#define VIA_REG_OFFSET_CURR_PTR     0x04
#define VIA_REG_OFFSET_STOP_IDX     0x08
#define VIA_REG_OFFSET_CURR_COUNT   0x0c
#define VIA_REG_OFFSET_CURR_INDEX   0x0f
#define VIA_REG_AC97                0x80
#define VIA_REG_SGD_SHADOW          0x84
#define VIA_REG_GPI_STATUS          0x88
#define VIA_REG_GPI_INTR            0x8c
#define VIA_ACLINK_STAT             0x40
#define VIA_ACLINK_CTRL             0x41
#define VIA_FUNC_ENABLE             0x42
#define VIA_PNP_CONTROL             0x43
#define VIA_FM_NMI_CTRL             0x48
#define VIA_MAX_DEVS                7

#define VIA_FUNC_ENABLE_SB          0x01
#define VIA_FUNC_ENABLE_FM          0x04
#define VIA_REV_8237                0x60

#define VIA_REG_AC97_BUSY           (1<<24)
#define VIA_REG_AC97_PRIMARY_VALID  (1<<25)
#define VIA_REG_STAT_ACTIVE         0x80
#define VIA_REG_STAT_EOL            0x02
#define VIA_REG_STAT_FLAG           0x01
#define VIA8233_SHADOW_STAT_ACTIVE  0x08
#define VIA_REG_CTRL_START          0x80
#define VIA_REG_CTRL_TERMINATE      0x40
#define VIA_REG_CTRL_PAUSE          0x08
#define VIA_ACLINK_C00_READY        0x01

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
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[256];
    uint16_t ac97_regs[128];

    /* DMA Context */
    uint32_t table_ptr;
    uint32_t curr_ptr;

    uint32_t status;
    
    
    struct {
        uint32_t reg_offset;
        int direction;
        int running;
        uint32_t tbl_entries;
        uint32_t lastpos;
        uint32_t fragsize;
        uint32_t bufsize;
        uint32_t bufsize2;
        int hwptr_done;
        int in_interrupt;
        int shadow_shift;
    } devs[VIA_MAX_DEVS];
};

struct snd_via_sg_table {
    uint32_t offset;
    uint32_t size;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;
    for (int i = 0; i < VIA_MAX_DEVS; i++) {
        uint8_t status = s->regs[i * 0x10 + VIA_REG_OFFSET_STATUS];
        if (status & (VIA_REG_STAT_EOL | VIA_REG_STAT_FLAG)) {
            raise = true;
            break;
        }
    }
    if (raise) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    for (int i = 0; i < VIA_MAX_DEVS; i++) {
        if (s->devs[i].running) {
            s->regs[i * 0x10 + VIA_REG_OFFSET_STATUS] |= (VIA_REG_STAT_EOL | VIA_REG_STAT_FLAG);
        }
    }
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == VIA_REG_AC97) {
        val = *(uint32_t *)&s->regs[0x80];
        val &= ~VIA_REG_AC97_BUSY;
        val |= VIA_REG_AC97_PRIMARY_VALID;
        return val;
    }
    if (addr == VIA_REG_SGD_SHADOW) {
        uint32_t shadow = 0;
        for (int i = 0; i < VIA_MAX_DEVS; i++) {
            uint8_t status = s->regs[i * 0x10 + VIA_REG_OFFSET_STATUS];
            if (status & VIA_REG_STAT_ACTIVE) shadow |= VIA8233_SHADOW_STAT_ACTIVE;
            if (status & VIA_REG_STAT_EOL) shadow |= VIA_REG_STAT_EOL;
            if (status & VIA_REG_STAT_FLAG) shadow |= VIA_REG_STAT_FLAG;
        }
        return shadow;
    }

    if (addr < sizeof(s->regs)) {
        if (size == 1) val = s->regs[addr];
        else if (size == 2) val = *(uint16_t *)&s->regs[addr];
        else if (size == 4) val = *(uint32_t *)&s->regs[addr];
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == VIA_REG_AC97) {
        if (size == 4) {
            uint32_t cmd = val;
            uint8_t reg = (cmd >> 16) & 0x7F;
            bool is_read = (cmd & (1 << 23)) != 0;
            if (is_read) {
                uint16_t data = 0;
                if (reg == 0x00) data = 0x0d50;
                else if (reg == 0x26) data = 0x000f;
                else if (reg == 0x7c) data = 0x414c;
                else if (reg == 0x7e) data = 0x4730;
                else data = s->ac97_regs[reg];
                
                cmd = (cmd & 0xFFFF0000) | data;
            } else {
                s->ac97_regs[reg] = cmd & 0xFFFF;
            }
            cmd &= ~VIA_REG_AC97_BUSY;
            cmd |= VIA_REG_AC97_PRIMARY_VALID;
            *(uint32_t *)&s->regs[0x80] = cmd;
        }
        return;
    }

    if (addr < sizeof(s->regs)) {
        uint32_t dev_idx = addr / 0x10;
        uint32_t reg = addr % 0x10;

        if (dev_idx < VIA_MAX_DEVS && reg == VIA_REG_OFFSET_STATUS) {
            s->regs[addr] &= ~val; /* W1C */
            pcibase_update_irq(s);
            return;
        }

        if (dev_idx < VIA_MAX_DEVS && reg == VIA_REG_OFFSET_CONTROL) {
            s->regs[addr] = val;
            if (val & VIA_REG_CTRL_START) {
                s->devs[dev_idx].running = 1;
                s->regs[dev_idx * 0x10 + VIA_REG_OFFSET_STATUS] |= VIA_REG_STAT_ACTIVE;
                pcibase_do_dma(s, false);
            } else if (val & VIA_REG_CTRL_TERMINATE) {
                s->devs[dev_idx].running = 0;
                s->regs[dev_idx * 0x10 + VIA_REG_OFFSET_STATUS] &= ~VIA_REG_STAT_ACTIVE;
            } else if (val & VIA_REG_CTRL_PAUSE) {
                s->devs[dev_idx].running = 0;
            }
            return;
        }

        if (size == 1) s->regs[addr] = val;
        else if (size == 2) *(uint16_t *)&s->regs[addr] = val;
        else if (size == 4) *(uint32_t *)&s->regs[addr] = val;
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

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    for (int i = 0; i < VIA_MAX_DEVS; i++) {
        s->devs[i].running = 0;
    }
    pci_set_byte(PCI_DEVICE(dev)->config + VIA_ACLINK_STAT, VIA_ACLINK_C00_READY);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1106 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x3058 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
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
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "via82xx-pio";
      
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
    .name = "snd_via82xx_pci",
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
