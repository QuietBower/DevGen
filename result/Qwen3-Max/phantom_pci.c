/*
 * QEMU Phantom PCI device emulation
 *
 * Based on Linux driver: /linux-6.18/drivers/misc/phantom.c
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "phantom_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define PCI_VENDOR_ID_PLX 0x10b5
#define PCI_DEVICE_ID_PLX_9050 0x9050
#define PHN_IRQCTL 0x4c
#define PHB_RUNNING 1
#define PHB_NOT_OH 2

/* Phantom register offsets */
#define PHN_CONTROL 0
#define PHN_CTL_IRQ (1 << 31)
#define PHN_CTL_AMP (1 << 30)

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* Register shadows */
    uint32_t ctl_reg;
    uint32_t status;
    
    /* Input registers (BAR2) */
    uint32_t input_regs[8];
    
    /* Output registers (BAR3) */
    uint32_t output_regs[8];
    
    /* IRQ control register (BAR0 offset 0x4c) */
    uint32_t irqctl_reg;
    
    /* Counter for poll */
    uint32_t counter;
};

/* Phantom device register layout from driver */
struct phm_regs {
    uint32_t count;
    uint32_t mask;
    uint32_t values[8];
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid read size %u at addr=%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return 0;
    }

    /* Determine which BAR we're in based on the address range */
    if (addr >= 0x1000) {
        /* This should not happen with proper BAR setup */
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] unexpected MMIO read at addr=%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        return 0;
    }

    /* Handle BAR0 (config space) */
    if (addr == PHN_IRQCTL) {
        val = s->irqctl_reg;
    } else if (addr < 0x20) {
        /* Input registers (BAR2) - but accessed through BAR0? */
        /* Actually, the driver uses separate BARs, so this shouldn't happen */
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] unexpected config space read at addr=%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        return 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid write size %u at addr=%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return;
    }

    /* Determine which BAR we're in based on the address range */
    if (addr >= 0x1000) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] unexpected MMIO write at addr=%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        return;
    }

    /* Handle BAR0 (config space) */
    if (addr == PHN_IRQCTL) {
        s->irqctl_reg = val;
        
        /* Check if we need to trigger an interrupt */
        if ((val & 0x43) == 0x43) {
            /* Simulate interrupt generation */
            s->counter++;
            pci_set_irq(&s->parent_obj, 1);
            /* Immediately lower it since it's level-triggered and we don't have real hardware timing */
            pci_set_irq(&s->parent_obj, 0);
        }
    } else if (addr < 0x20) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] unexpected config space write at addr=%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid PIO read size %u at addr=%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return 0;
    }

    /* Handle BAR2 (input registers) */
    if (addr < 0x20) {
        uint32_t reg = addr >> 2;
        if (reg < 8) {
            val = s->input_regs[reg];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid input register read at addr=%" PRIx64 "\n", 
                          TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        }
    }
    /* Handle BAR3 (output registers) */
    else if (addr >= 0x100 && addr < 0x120) {
        uint32_t reg = (addr - 0x100) >> 2;
        if (reg < 8) {
            val = s->output_regs[reg];
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid output register read at addr=%" PRIx64 "\n", 
                          TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] unexpected PIO read at addr=%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr);
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid PIO write size %u at addr=%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
        return;
    }

    /* Handle BAR2 (input registers) */
    if (addr < 0x20) {
        uint32_t reg = addr >> 2;
        if (reg < 8) {
            s->input_regs[reg] = val;
            
            /* Special handling for PHN_CONTROL (reg 0) */
            if (reg == PHN_CONTROL) {
                s->ctl_reg = val;
                
                /* Check if IRQ is being enabled/disabled */
                if (val & PHN_CTL_IRQ) {
                    /* Enable running state */
                    s->status |= PHB_RUNNING;
                    s->counter = 0;
                    
                    /* Set up IRQ control */
                    s->irqctl_reg = 0x43;
                } else {
                    /* Disable running state */
                    s->status &= ~PHB_RUNNING;
                    s->irqctl_reg = 0;
                }
                
                /* Handle NOT_OH mode */
                if ((s->status & PHB_NOT_OH) && (reg == PHN_CONTROL)) {
                    /* Toggle AMP bit */
                    s->ctl_reg ^= PHN_CTL_AMP;
                    s->input_regs[PHN_CONTROL] = s->ctl_reg;
                    
                    /* Generate interrupt */
                    s->counter++;
                    pci_set_irq(&s->parent_obj, 1);
                    pci_set_irq(&s->parent_obj, 0);
                }
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid input register write at addr=%" PRIx64 "\n", 
                          TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        }
    }
    /* Handle BAR3 (output registers) */
    else if (addr >= 0x100 && addr < 0x120) {
        uint32_t reg = (addr - 0x100) >> 2;
        if (reg < 8) {
            s->output_regs[reg] = val;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid output register write at addr=%" PRIx64 "\n", 
                          TYPE_PCIBASE_DEVICE, (uint64_t)addr);
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] unexpected PIO write at addr=%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr);
    }
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    
    /* Reset device state */
    s->ctl_reg = 0;
    s->status = 0;
    s->irqctl_reg = 0;
    s->counter = 0;
    
    memset(s->input_regs, 0, sizeof(s->input_regs));
    memset(s->output_regs, 0, sizeof(s->output_regs));
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_PLX);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PLX_9050);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_PLX);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID,        PCI_DEVICE_ID_PLX_9050);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Set up BARs based on driver usage */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;  /* Config space */
    s->bar_info[0].name = "phantom-config";
    
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 0x20;    /* Input registers */
    s->bar_info[2].name = "phantom-input";
    
    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = 0x20;    /* Output registers */
    s->bar_info[3].name = "phantom-output";
    
    s->num_bars = 4; /* We have BARs 0, 2, 3 */
    
    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].type != BAR_TYPE_NONE) {
            pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        }
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    k->vendor_id = PCI_VENDOR_ID_PLX;
    k->device_id = PCI_DEVICE_ID_PLX_9050;
    k->revision = 0x01;
    k->class_id = PCI_CLASS_BRIDGE_OTHER;
    
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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
