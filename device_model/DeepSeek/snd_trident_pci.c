/*
 * QEMU 8.2.10 PCI device model for Trident 4DWave DX/NX audio.
 * Generated strictly from Linux kernel driver and provided register offsets.
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

#define TYPE_PCIBASE_DEVICE "snd_trident_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1023
#define DEVICE_ID 0x2000
#define CLASS_ID 0x0401

/* Register offsets */
#define T4D_MPU401_BASE             0x20
#define T4D_LFO_GC_CIR               0xa0
#define T4D_MISCINT                  0xb0
#define NX_SPCSTATUS                0x64
#define NX_ACR0_AC97_COM_STAT       0x40
#define NX_SPCTRL_SPCSO             0x24
#define NX_TLBC                     0x6c
#define SI_SPDIF_CS                 0x70
#define DX_ACR2_AC97_COM_STAT       0x48
#define SI_SERIAL_INTF_CTRL         0x48
#define GAMEPORT_LEGACY             0x31
#define GAMEPORT_AXES               0x34
#define GAMEPORT_MODE_ADC           0x80
#define GAMEPORT_GCR                0x30
#define T4D_AINTEN_B                0xdc
#define T4D_STOP_B                  0xb8
#define T4D_AINTEN_A                0x9c
#define T4D_STOP_A                  0x84
#define SI_AC97_GPIO                0x4c
#define DX_ACR0_AC97_W             0x40
#define NX_ACR1_AC97_W             0x44
#define SI_AC97_WRITE               0x40
#define NX_ACR2_AC97_R_PRIMARY      0x48
#define SI_AC97_READ                0x44
#define NX_ACR3_AC97_R_SECONDARY    0x4c
#define DX_ACR1_AC97_R             0x44

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
    uint8_t ioreg[256];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No-op: MMIO not used by this device */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < ARRAY_SIZE(s->ioreg)) {
        switch (size) {
        case 1:
            val = s->ioreg[addr];
            break;
        case 2:
            val = s->ioreg[addr] | (s->ioreg[addr + 1] << 8);
            break;
        case 4:
            val = s->ioreg[addr] | (s->ioreg[addr + 1] << 8) |
                  (s->ioreg[addr + 2] << 16) | (s->ioreg[addr + 3] << 24);
            break;
        default:
            break;
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < ARRAY_SIZE(s->ioreg)) {
        switch (size) {
        case 1:
            s->ioreg[addr] = val & 0xFF;
            break;
        case 2:
            s->ioreg[addr] = val & 0xFF;
            s->ioreg[addr + 1] = (val >> 8) & 0xFF;
            break;
        case 4:
            s->ioreg[addr] = val & 0xFF;
            s->ioreg[addr + 1] = (val >> 8) & 0xFF;
            s->ioreg[addr + 2] = (val >> 16) & 0xFF;
            s->ioreg[addr + 3] = (val >> 24) & 0xFF;
            break;
        default:
            break;
        }

        /* Preserve AC'97 codec ready bits to ensure probe succeeds */
        if (addr <= 0x40 && addr + (size - 1) >= 0x40) {
            s->ioreg[0x40] |= 0x01;
        }
        if (addr <= 0x48 && addr + (size - 1) >= 0x48) {
            s->ioreg[0x48] |= 0x01;
        }
        if (addr <= 0x49 && addr + (size - 1) >= 0x49) {
            s->ioreg[0x49] |= 0x01;
        }
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

    memset(s->ioreg, 0, sizeof(s->ioreg));

    /* AC'97 codec ready: set primary and secondary codec ready bits */
    s->ioreg[0x40] = 0x01;  /* DX_ACR0_AC97_W or NX_ACR0_AC97_COM_STAT */
    s->ioreg[0x48] = 0x01;  /* DX_ACR2_AC97_COM_STAT bit0 (primary ready) */
    s->ioreg[0x49] = 0x01;  /* secondary codec ready bit (bit8 of 16-bit status) */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1023 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x2000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0401 );
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
    s->bar_info[0].name = "trident-pio";
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
    .name = "snd_trident_pci",
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
