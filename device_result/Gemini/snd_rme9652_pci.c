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

#define TYPE_PCIBASE_DEVICE "snd_rme9652_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define RME9652_VENDOR_ID 0x10ee
#define RME9652_DEVICE_ID 0x3fc4
#define RME9652_CLASS_ID  0x0401

#define RME9652_IO_EXTENT 1024

#define RME9652_init_buffer       0
#define RME9652_play_buffer       32
#define RME9652_rec_buffer        36
#define RME9652_control_register  64
#define RME9652_irq_clear         96
#define RME9652_time_code         100
#define RME9652_thru_base         128
#define RME9652_status_register   0

#define RME9652_IRQ	   (1<<0)
#define RME9652_start_bit	   (1<<0)
#define RME9652_IE		   (1<<5)
#define RME9652_latency            0x0e
#define RME9652_buf_pos	  0x000FFC0
#define RME9652_buffer_id  (1<<26)
#define RME9652_fs48	   (1<<4)
#define RME9652_freq		   (1<<6)
#define RME9652_DS                 (1<<8)
#define RME9652_inp_0		   (1<<14)
#define RME9652_inp_1		   (1<<15)
#define RME9652_opt_out	           (1<<12)
#define RME9652_wsel		   (1<<13)
#define RME9652_Master		   (1<<4)
#define RME9652_SyncPref_ADAT1	   0
#define RME9652_SyncPref_ADAT2	   (1<<16)
#define RME9652_SyncPref_ADAT3	   (1<<17)
#define RME9652_SyncPref_Mask      (RME9652_SyncPref_ADAT2|RME9652_SyncPref_ADAT3)
#define RME9652_SyncPref_SPDIF	   (RME9652_SyncPref_ADAT2|RME9652_SyncPref_ADAT3)
#define RME9652_ADAT1_INTERNAL     (1<<22)
#define RME9652_SPDIF_RESET        (1<<18)
#define RME9652_SPDIF_SELECT       (1<<19)
#define RME9652_SPDIF_CLOCK        (1<<20)
#define RME9652_SPDIF_WRITE        (1<<21)
#define RME9652_F_0	   (1<<22)
#define RME9652_F_1	   (1<<23)
#define RME9652_F_2	   (1<<24)
#define RME9652_SPDIF_READ (1<<28)
#define RME9652_ERF	   (1<<25)
#define RME9652_PRO		   (1<<9)
#define RME9652_Dolby		   (1<<11)
#define RME9652_EMP		   (1<<10)
#define RME9652_lock_0	   (1<<3)
#define RME9652_sync_0	   (1<<18)
#define RME9652_lock_1	   (1<<2)
#define RME9652_sync_1	   (1<<17)
#define RME9652_lock_2	   (1<<1)
#define RME9652_sync_2	   (1<<16)
#define RME9652_tc_valid   (1<<27)
#define RME9652_wsel_rd	   (1<<5)

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t irq_status;

    uint32_t control_register;
    uint32_t status_register;
    uint32_t thru_bits;
    uint32_t time_code;

    uint32_t play_buffer;
    uint32_t rec_buffer;
    uint32_t init_buffer;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->irq_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case RME9652_status_register:
        val = s->status_register;
        break;
    case RME9652_time_code:
        val = s->time_code;
        break;
    case RME9652_control_register:
        val = s->control_register;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= RME9652_thru_base && addr < RME9652_thru_base + 26 * 4) {
        int ch = (addr - RME9652_thru_base) / 4;
        if (val) {
            s->thru_bits |= (1 << ch);
        } else {
            s->thru_bits &= ~(1 << ch);
        }
        return;
    }

    if (addr >= 0 && addr <= 28) {
        /* reset hw pointer */
        return;
    }

    switch (addr) {
    case RME9652_control_register:
        s->control_register = val;
        break;
    case RME9652_irq_clear:
        s->irq_status = 0;
        pcibase_update_irq(s);
        break;
    case RME9652_play_buffer:
        s->play_buffer = val;
        break;
    case RME9652_rec_buffer:
        s->rec_buffer = val;
        break;
    default:
        break;
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

    s->control_register = 0;
    s->status_register = 0;
    s->thru_bits = 0;
    s->time_code = 0;
    s->play_buffer = 0;
    s->rec_buffer = 0;
    s->init_buffer = 0;
    s->irq_status = 0;
    pcibase_update_irq(s);
}

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  RME9652_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RME9652_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RME9652_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x04);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = RME9652_IO_EXTENT;
    s->bar_info[0].name = "rme9652-mmio";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_rme9652_pci",
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
