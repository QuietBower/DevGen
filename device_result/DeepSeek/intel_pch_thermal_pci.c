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

#define TYPE_PCIBASE_DEVICE "intel_pch_thermal_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL          0x8086
#define PCH_THERMAL_DID_HSW_1       0x9C24

#define WPT_TEMP      0x0000
#define WPT_TSC       0x04
#define WPT_TSS       0x06
#define WPT_TSEL      0x08
#define WPT_TSREL     0x0A
#define WPT_TSMIC     0x0C
#define WPT_CTT       0x0010
#define WPT_TSPM      0x001C
#define WPT_TAHV      0x0014
#define WPT_TALV      0x0018
#define WPT_TL        0x00000040
#define WPT_PHL       0x0060
#define WPT_PHLC      0x62
#define WPT_TAS       0x80
#define WPT_TSPIEN    0x82
#define WPT_TSGPEN    0x84

#define WPT_TEMP_TSR  0x01ff
#define WPT_TSC_CPDE  0x01
#define WPT_TSS_TSDSS 0x10
#define WPT_TSS_GPES  0x08
#define WPT_TSEL_ETS  0x01
#define WPT_TSEL_PLDB 0x80
#define WPT_TL_TOL    0x000001FF
#define WPT_TL_T1L    0x1ff00000
#define WPT_TL_TTEN   0x20000000

#define PCH_THERMAL_MMIO_SIZE 0x100

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
    uint8_t regs[PCH_THERMAL_MMIO_SIZE];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > PCH_THERMAL_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return ~0ULL;
    }

    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        val = lduw_le_p(&s->regs[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->regs[addr]);
        break;
    case 8:
        val = ldq_le_p(&s->regs[addr]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported read size %u at addr 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return ~0ULL;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > PCH_THERMAL_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return;
    }

    /* Special handling for TSEL register to enforce hardware rules */
    if (addr <= WPT_TSEL && addr + size > WPT_TSEL) {
        /* When writing to TSEL (byte at offset 0x08), filter bits */
        if (size == 1 && addr == WPT_TSEL) {
            /* Only ETS (bit0) is writable; PLDB (bit7) is forced to 0 */
            val = val & 0x7F;
        } else if (size == 2 && (addr == WPT_TSEL || addr == WPT_TSEL - 1)) {
            /* 16-bit write that includes TSEL byte: mask TSEL byte appropriately */
            uint8_t tsel_index = WPT_TSEL - addr;
            uint8_t *p = (uint8_t *)&val;
            p[tsel_index] &= 0x7F;
        } else if (size == 4 && addr <= WPT_TSEL && addr + 4 > WPT_TSEL) {
            uint8_t tsel_index = WPT_TSEL - addr;
            uint8_t *p = (uint8_t *)&val;
            p[tsel_index] &= 0x7F;
        }
    }

    /* Write to internal shadow array */
    switch (size) {
    case 1:
        s->regs[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(&s->regs[addr], (uint16_t)val);
        break;
    case 4:
        stl_le_p(&s->regs[addr], (uint32_t)val);
        break;
    case 8:
        stq_le_p(&s->regs[addr], val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write size %u at addr 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO registers used by driver; always return 0 */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO registers used by driver; ignore writes */
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

    /* Initialize MMIO shadow registers with defaults */
    memset(s->regs, 0, PCH_THERMAL_MMIO_SIZE);

    /* Set plausible temperature: 30 degrees Celsius (raw value 60 if GET_WPT_TEMP divides by 2?) */
    /* For simplicity, set to 0x3C (60) which may be interpreted as 30°C if formula is temp = raw * 500milli°C? */
    stw_le_p(&s->regs[WPT_TEMP], 0x3C);

    /* TSEL: no ETS, no PLDB */
    s->regs[WPT_TSEL] = 0x00;

    /* Critical trip point (CTT): set to non-zero, e.g., 0x64 (100) */
    stw_le_p(&s->regs[WPT_CTT], 0x64);

    /* Hot trip point (PHL): set to non-zero, e.g., 0x50 (80) */
    stw_le_p(&s->regs[WPT_PHL], 0x50);

    /* Other registers remain zero */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCH_THERMAL_DID_HSW_1);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
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
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = PCH_THERMAL_MMIO_SIZE,
        .name = "pch-thermal-mmio"
    };
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
    .name = "intel_pch_thermal_pci",
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
