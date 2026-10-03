/*
 * QEMU PCI device model for Intel PCH Thermal controller
 * Generated to satisfy linux-6.18/drivers/thermal/intel/intel_pch_thermal.c
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

#define TYPE_PCIBASE_DEVICE "intel_pch_thermal_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Basic PCI identifiers and register offsets derived from driver */
#define PCI_VENDOR_ID_INTEL        0x8086
#define PCI_CLASS_OTHERS           0xff

#define PCH_THERMAL_DID_HSW_1 0x9C24
#define PCH_THERMAL_DID_HSW_2 0x8C24
#define PCH_THERMAL_DID_WPT   0x9CA4
#define PCH_THERMAL_DID_SKL   0x9D31
#define PCH_THERMAL_DID_SKL_H 0xA131
#define PCH_THERMAL_DID_CNL   0x9Df9
#define PCH_THERMAL_DID_CNL_H 0xA379
#define PCH_THERMAL_DID_CNL_LP 0x02F9
#define PCH_THERMAL_DID_CML_H 0X06F9
#define PCH_THERMAL_DID_LWB   0xA1B1
#define PCH_THERMAL_DID_WBG   0x8D24

/* MMIO register offsets and bit definitions used by the driver */
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

#define PCH_TEMP_OFFSET    (-50)
#define WPT_TEMP_OFFSET    (PCH_TEMP_OFFSET * 1000)

#define PCH_VENDOR_ID  PCI_VENDOR_ID_INTEL
#define PCH_DEVICE_ID  PCH_THERMAL_DID_HSW_1
#define PCH_CLASS_ID   PCI_CLASS_OTHERS

/* Simple temperature conversion matching GET_WPT_TEMP/GET_PCH_TEMP usage. */
/* Driver uses GET_WPT_TEMP(WPT_TEMP_TSR & readw(...)) and then reports in mC.
 * We emulate by storing a raw sensor value (in degrees C) and encoding as
 * (temp_c - PCH_TEMP_OFFSET) so that GET_WPT_TEMP would yield degrees C.
 * Here we only need consistent self-contained behavior inside the device. */

/* BAR description */
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

    /* Hardware Register Shadows */
    /* All registers driver touches are in BAR 0 */
    uint8_t  tsel;      /* WPT_TSEL (8-bit) */
    uint16_t temp;      /* WPT_TEMP (16-bit) */
    uint16_t ctt;       /* WPT_CTT (16-bit) critical trip */
    uint16_t phl;       /* WPT_PHL (16-bit) hot trip */
    uint16_t tspm;      /* WPT_TSPM (16-bit) threshold for suspend path */

    /* We keep current temperature in degrees C */
    int16_t current_temp_c;
};

/* Internal helper for IRQ signaling. The driver never uses interrupts,
 * so this is effectively a no-op but kept for completeness. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    /* No interrupt registers are referenced by the driver, so we do nothing. */
}

/* No DMA usage in driver; keep helper but do nothing. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)s;
    (void)pdev;
    (void)is_write;
}

/* Helper to encode temperature into register representation. */
static uint16_t pcibase_encode_temp_from_c(int16_t temp_c)
{
    /* The driver masks value with WPT_TEMP_TSR (0x1ff). We emulate by:
     * encoded = temp_c - PCH_TEMP_OFFSET (so 0C -> 50, 100C -> 150 etc.). */
    int32_t enc = (int32_t)temp_c - PCH_TEMP_OFFSET;
    if (enc < 0) {
        enc = 0;
    }
    if (enc > WPT_TEMP_TSR) {
        enc = WPT_TEMP_TSR;
    }
    return (uint16_t)enc;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case WPT_TEMP:
        /* 16-bit temperature register */
        if (size == 1) {
            uint16_t t = s->temp;
            val = t & 0xff;
        } else if (size == 2) {
            val = s->temp;
        } else if (size == 4) {
            val = s->temp;
        }
        break;
    case WPT_TSEL:
        /* 8-bit sensor enable / policy bits */
        if (size == 1) {
            val = s->tsel;
        } else if (size == 2) {
            val = s->tsel;
        } else if (size == 4) {
            val = s->tsel;
        }
        break;
    case WPT_CTT:
        if (size == 1) {
            val = s->ctt & 0xff;
        } else if (size == 2) {
            val = s->ctt;
        } else if (size == 4) {
            val = s->ctt;
        }
        break;
    case WPT_PHL:
        if (size == 1) {
            val = s->phl & 0xff;
        } else if (size == 2) {
            val = s->phl;
        } else if (size == 4) {
            val = s->phl;
        }
        break;
    case WPT_TSPM:
        if (size == 1) {
            val = s->tspm & 0xff;
        } else if (size == 2) {
            val = s->tspm;
        } else if (size == 4) {
            val = s->tspm;
        }
        break;
    default:
        /* Unimplemented offsets return 0. Driver only touches the above. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case WPT_TSEL:
        /* Driver performs:
         *   tsel = readb(TSEL);
         *   writeb(tsel | ETS, TSEL);
         * and later clears ETS in suspend when bios_enabled is false.
         * We implement a simple R/W for the low 8 bits. PLDB behavior is
         * not modeled except that the bit is preserved. */
        if (size == 1) {
            uint8_t new_val = (uint8_t)val;
            /* For now, TSEL is freely writable; driver checks PLDB but we
             * do not enforce lock-down semantics. */
            s->tsel = new_val;
        } else if (size == 2) {
            s->tsel = (uint8_t)(val & 0xff);
        } else if (size == 4) {
            s->tsel = (uint8_t)(val & 0xff);
        }
        break;
    case WPT_CTT:
        /* Trip temperature critical threshold; writable by guest even though
         * current mainline driver only reads it. */
        if (size == 1) {
            s->ctt = (s->ctt & 0xff00u) | ((uint16_t)val & 0x00ffu);
        } else if (size == 2) {
            s->ctt = (uint16_t)val;
        } else if (size == 4) {
            s->ctt = (uint16_t)val;
        }
        break;
    case WPT_PHL:
        if (size == 1) {
            s->phl = (s->phl & 0xff00u) | ((uint16_t)val & 0x00ffu);
        } else if (size == 2) {
            s->phl = (uint16_t)val;
        } else if (size == 4) {
            s->phl = (uint16_t)val;
        }
        break;
    case WPT_TSPM:
        if (size == 1) {
            s->tspm = (s->tspm & 0xff00u) | ((uint16_t)val & 0x00ffu);
        } else if (size == 2) {
            s->tspm = (uint16_t)val;
        } else if (size == 4) {
            s->tspm = (uint16_t)val;
        }
        break;
    case WPT_TEMP:
        /* Not normally written by software; allow writes for flexibility. */
        if (size == 1) {
            s->temp = (s->temp & 0xff00u) | ((uint16_t)val & 0x00ffu);
        } else if (size == 2) {
            s->temp = (uint16_t)val;
        } else if (size == 4) {
            s->temp = (uint16_t)val;
        }
        break;
    default:
        /* All other offsets ignored. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    (void)s;
    (void)addr;
    (void)size;
    /* No port I/O usage in the driver. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* No port I/O usage in the driver. */
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

    /* Power-on defaults chosen to satisfy driver probe path:
     * - TSEL: ETS bit clear, PLDB clear.
     * - TEMP/CTT/PHL/TSPM: some non-zero but within mask range. */
    s->current_temp_c = 40; /* 40C as a reasonable default */
    s->temp = pcibase_encode_temp_from_c(s->current_temp_c);

    /* Choose critical and hot trips above current temp so driver sees them. */
    s->ctt = pcibase_encode_temp_from_c(95);  /* 95C */
    s->phl = pcibase_encode_temp_from_c(85);  /* 85C */

    /* Suspend threshold: somewhere between current and critical */
    s->tspm = pcibase_encode_temp_from_c(60); /* 60C */

    s->tsel = 0x00; /* sensor disabled until driver enables it */
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

    /* Static PCI configuration using first id_table entry from driver
     * (Vendor=0x8086, Device=PCH_THERMAL_DID_HSW_1). */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCH_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCH_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCH_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Device uses one MMIO BAR (index 0). Driver calls pci_ioremap_bar(pdev, 0). */
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    /* Size must cover highest offset used (0x84) plus some margin. */
    s->bar_info[0].size  = 0x100;
    s->bar_info[0].name  = "pch-thermal-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal register state */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "intel_pch_thermal_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(tsel, PCIBaseState),
        VMSTATE_UINT16(temp, PCIBaseState),
        VMSTATE_UINT16(ctt, PCIBaseState),
        VMSTATE_UINT16(phl, PCIBaseState),
        VMSTATE_UINT16(tspm, PCIBaseState),
        VMSTATE_INT16(current_temp_c, PCIBaseState),
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

