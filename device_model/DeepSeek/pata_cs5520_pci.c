/*
 * QEMU PCI device model for CS5520 IDE controller (pata_cs5520 driver).
 * Emulates BMDMA registers and PCI configuration space required for driver probe.
 * Note: PCI class set to STORAGE_OTHER to avoid interference from pata_acpi.
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

#define TYPE_PCIBASE_DEVICE "pata_cs5520_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1078
#define DEVICE_ID 0x5510
#define CLASS_ID PCI_CLASS_STORAGE_OTHER

struct PCIBaseState {
    PCIDevice parent_obj;

    /* BMDMA per-channel registers */
    uint8_t bmdma_cmd[2];
    uint8_t bmdma_status[2];
    uint32_t bmdma_prd[2];
    MemoryRegion bmdma_region;

    /* Custom PCI configuration registers */
    uint8_t config_0x60;
    uint8_t cs5520_cfg_timing[10];  /* 0x62..0x6B */

    /* Dummy BARs to satisfy pcim_enable_device() */
    MemoryRegion bar0_region;
    MemoryRegion bar1_region;
};

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 8) {
        /* Primary channel */
        unsigned int reg = addr & 0x7;
        switch (reg) {
        case 0: return s->bmdma_cmd[0];
        case 2: return s->bmdma_status[0];
        case 4: return s->bmdma_prd[0];
        default: return 0;
        }
    } else if (addr < 16) {
        /* Secondary channel */
        unsigned int reg = (addr - 8) & 0x7;
        switch (reg) {
        case 0: return s->bmdma_cmd[1];
        case 2: return s->bmdma_status[1];
        case 4: return s->bmdma_prd[1];
        default: return 0;
        }
    }
    /* BAR0 and BAR1 accesses fall through to here, returning 0 */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 8) {
        /* Primary channel */
        unsigned int reg = addr & 0x7;
        switch (reg) {
        case 0: s->bmdma_cmd[0] = val; break;
        case 2: s->bmdma_status[0] = val; break;
        case 4: s->bmdma_prd[0] = val; break;
        }
        return;
    } else if (addr < 16) {
        /* Secondary channel */
        unsigned int reg = (addr - 8) & 0x7;
        switch (reg) {
        case 0: s->bmdma_cmd[1] = val; break;
        case 2: s->bmdma_status[1] = val; break;
        case 4: s->bmdma_prd[1] = val; break;
        }
        return;
    }
    /* BAR0 and BAR1 writes are ignored */
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Handle custom configuration space at 0x60..0x6B */
    if (address >= 0x60 && address < 0x6C) {
        if (len == 1) {
            if (address == 0x60) return s->config_0x60;
            int idx = address - 0x62;
            if (idx >= 0 && idx < 10) return s->cs5520_cfg_timing[idx];
        }
        return 0;
    }

    /* Let default handler manage BARs and others */
    return pci_default_read_config(pdev, address, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Handle custom configuration space at 0x60..0x6B */
    if (address >= 0x60 && address < 0x6C) {
        if (len == 1) {
            if (address == 0x60) {
                s->config_0x60 = val;
                return;
            }
            int idx = address - 0x62;
            if (idx >= 0 && idx < 10) {
                s->cs5520_cfg_timing[idx] = val;
                return;
            }
        }
        return;
    }

    /* Let default handler manage BARs and others */
    pci_default_write_config(pdev, address, val, len);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->config_0x60 = 0x03;  /* both IDE ports enabled, DMA disabled */
    memset(s->cs5520_cfg_timing, 0, sizeof(s->cs5520_cfg_timing));
    s->bmdma_cmd[0] = s->bmdma_cmd[1] = 0;
    s->bmdma_status[0] = s->bmdma_status[1] = 0;
    s->bmdma_prd[0] = s->bmdma_prd[1] = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0: Primary command block (8 bytes I/O) - dummy */
    memory_region_init_io(&s->bar0_region, OBJECT(s), &pcibase_pio_ops, s, "bar0-cmd", 8);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar0_region);

    /* BAR1: Primary control block (4 bytes I/O) - dummy */
    memory_region_init_io(&s->bar1_region, OBJECT(s), &pcibase_pio_ops, s, "bar1-ctl", 4);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar1_region);

    /* BAR2: BMDMA IO region (16 bytes) */
    memory_region_init_io(&s->bmdma_region, OBJECT(s), &pcibase_pio_ops, s, "bmdma", 16);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bmdma_region);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No special cleanup needed */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pata_cs5520_pci",
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
    k->exit = pcibase_uninit;
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
    k->vendor_id = VENDOR_ID;
    k->device_id = DEVICE_ID;
    k->revision = 0x01;
    k->class_id = CLASS_ID;
    dc->reset = pcibase_reset;
    dc->vmsd = &vmstate_pcibase;
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
