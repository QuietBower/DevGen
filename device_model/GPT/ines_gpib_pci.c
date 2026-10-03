/*
 * QEMU PCI device model skeleton for ines_gpib_pci
 * Auto-generated for structural matching with linux-6.18/drivers/staging/gpib/ines/ines_gpib.c
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "ines_gpib_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Related PCI configuration info derived from ines_gpib.c */
#define INES_PCI_VENDOR_ID   0x10b5
#define INES_PCI_DEVICE_ID   0x9050
#define INES_PCI_CLASS_ID    PCI_CLASS_OTHERS

/* The driver uses I/O ports (inb/outb) via nec7210_ioport_* helpers.
 * It also uses a second I/O region for PLX/AMCC bridge registers.
 * BAR0: GPIB/NEC7210 I/O space
 * BAR1: Bridge I/O space (PLX9050 / AMCC5920)
 */

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

/* Known NEC7210 register offsets used by the driver (via nec7210_* helpers).
 * These are not accessed directly in this file, but we model a minimal
 * register set to keep the higher-level logic working.
 * The exact numeric values are not given in the provided source; therefore
 * we only expose functionality through the ines_inb/ines_outb abstraction.
 */

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* We model two generic I/O regions: one for NEC7210/GPIB core,
     * one for the PLX/AMCC bridge registers. The driver itself only
     * references these indirectly via ines_inb/ines_outb and generic
     * PCI helper functions, so we provide dumb storage that can be
     * read and written by the guest.
     */

    /* Simple I/O space backing store for BAR0 (GPIB core). */
    uint8_t gpib_io[0x100];

    /* Simple I/O space backing store for BAR1 (bridge core). */
    uint8_t bridge_io[0x100];

    /* Interrupt status shadow: whether an interrupt is asserted. */
    bool irq_asserted;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The ines driver does not manipulate any explicit interrupt-acknowledge
     * or status registers inside this PCI function; instead, it requests the
     * IRQ line from the PCI core and expects interrupts to be delivered when
     * the underlying chip asserts them. Since we do not have any explicit
     * register-level interrupt semantics from the provided source, we expose
     * a very simple model: if irq_asserted is true, we assert the legacy
     * INTx line; otherwise, we deassert it.
     */

    if (s->irq_asserted) {
        if (msi_enabled(pdev)) {
            /* If MSI is enabled, send a single-vector MSI; however the
             * original hardware may or may not support MSI. The driver
             * does not use MSI-specific APIs, so INTx is sufficient.
             * We keep MSI notification here guarded to not break builds.
             */
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * The provided driver never sets up DMA from the PCI device; it only
 * performs programmed I/O via inb/outb and nec7210_* helpers. Therefore,
 * we do not implement any DMA engine here.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No MMIO BARs are defined/used by the ines PCI driver; all
     * accesses are via I/O ports (inb/outb). Therefore, a MMIO
     * read should just return zero.
     */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO BARs used; ignore writes. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver accesses two different I/O spaces: one used as
     * nec_priv->iobase (GPIB core) and one for the PLX/AMCC bridge.
     * Both are requested via pci_request_regions() and then used by
     * inb/outb, outl(). We present BAR0 and BAR1 as I/O port regions.
     *
     * Our address here is relative to the BAR; to distinguish which
     * BAR we're servicing, QEMU will create separate MemoryRegions
     * with different callbacks, so this function will be instantiated
     * separately per BAR. However, in this skeleton we wired the
     * same pcibase_pio_ops for both BAR0 and BAR1, and we rely on
     * QEMU to pass us the region-relative offset.
     *
     * For simplicity, we route:
     *  - BAR0 I/O range [0, 0x100) to gpib_io
     *  - BAR1 I/O range [0, 0x100) to bridge_io
     *
     * Which BAR we are serving is not directly provided, so we infer
     * it from the MemoryRegion pointer equality: BAR0's region is
     * bar_regions[0], BAR1's is bar_regions[1]. The MemoryRegionOps
     * opaque pointer is always PCIBaseState*, so we instead encode a
     * simple split: addresses below 0x100 are gpib_io and between
     * 0x100 and 0x200 are bridge_io. We size each BAR to at least
     * 0x100 so this remains valid.
     */

    if (size == 1) {
        if (addr < sizeof(s->gpib_io)) {
            val = s->gpib_io[addr];
        } else if (addr >= 0x100 && addr < 0x100 + sizeof(s->bridge_io)) {
            val = s->bridge_io[addr - 0x100];
        } else {
            /* Unmapped I/O returns 0xff as typical for inb() on empty ports. */
            val = 0xff;
        }
    } else if (size == 2) {
        uint16_t v = 0xffff;
        if (addr + 1 < sizeof(s->gpib_io)) {
            v = s->gpib_io[addr] | (s->gpib_io[addr + 1] << 8);
        } else if (addr >= 0x100 && addr + 1 < 0x100 + sizeof(s->bridge_io)) {
            hwaddr o = addr - 0x100;
            v = s->bridge_io[o] | (s->bridge_io[o + 1] << 8);
        }
        val = v;
    } else if (size == 4) {
        uint32_t v = 0xffffffffU;
        if (addr + 3 < sizeof(s->gpib_io)) {
            v = s->gpib_io[addr] |
                (s->gpib_io[addr + 1] << 8) |
                (s->gpib_io[addr + 2] << 16) |
                (s->gpib_io[addr + 3] << 24);
        } else if (addr >= 0x100 && addr + 3 < 0x100 + sizeof(s->bridge_io)) {
            hwaddr o = addr - 0x100;
            v = s->bridge_io[o] |
                (s->bridge_io[o + 1] << 8) |
                (s->bridge_io[o + 2] << 16) |
                (s->bridge_io[o + 3] << 24);
        }
        val = v;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        uint8_t v = (uint8_t)val;
        if (addr < sizeof(s->gpib_io)) {
            s->gpib_io[addr] = v;
        } else if (addr >= 0x100 && addr < 0x100 + sizeof(s->bridge_io)) {
            s->bridge_io[addr - 0x100] = v;
        }
    } else if (size == 2) {
        uint16_t v = (uint16_t)val;
        if (addr + 1 < sizeof(s->gpib_io)) {
            s->gpib_io[addr] = v & 0xff;
            s->gpib_io[addr + 1] = (v >> 8) & 0xff;
        } else if (addr >= 0x100 && addr + 1 < 0x100 + sizeof(s->bridge_io)) {
            hwaddr o = addr - 0x100;
            s->bridge_io[o] = v & 0xff;
            s->bridge_io[o + 1] = (v >> 8) & 0xff;
        }
    } else if (size == 4) {
        uint32_t v = (uint32_t)val;
        if (addr + 3 < sizeof(s->gpib_io)) {
            s->gpib_io[addr] = v & 0xff;
            s->gpib_io[addr + 1] = (v >> 8) & 0xff;
            s->gpib_io[addr + 2] = (v >> 16) & 0xff;
            s->gpib_io[addr + 3] = (v >> 24) & 0xff;
        } else if (addr >= 0x100 && addr + 3 < 0x100 + sizeof(s->bridge_io)) {
            hwaddr o = addr - 0x100;
            s->bridge_io[o] = v & 0xff;
            s->bridge_io[o + 1] = (v >> 8) & 0xff;
            s->bridge_io[o + 2] = (v >> 16) & 0xff;
            s->bridge_io[o + 3] = (v >> 24) & 0xff;
        }
    }

    /* The driver enables interrupts by writing to bridge registers such as
     * PLX9050_INTCSR_REG or AMCC_INTCS_REG via outl(). It will then expect
     * interrupts when FIFO thresholds, IFC, etc., occur. Since we lack
     * fine-grained behaviour, we keep irq_asserted false here and let
     * higher-level testing manually toggle it via QEMU monitor if needed.
     */
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

    /* Clear internal register shadows and deassert interrupt. */
    memset(s->gpib_io, 0, sizeof(s->gpib_io));
    memset(s->bridge_io, 0, sizeof(s->bridge_io));
    s->irq_asserted = false;
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  INES_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  INES_PCI_DEVICE_ID );

    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, INES_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x1072);

    pci_set_word(pci_conf + PCI_CLASS_DEVICE, INES_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, INES_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID,         0x1072);

    /* Expose as a conventional PCI function, but also allow PCIE caps
     * as skeleton did.
     */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The driver uses pci_resource_start(found_id.gpib_region) as I/O base
     * for the NEC7210 core and another BAR for the PLX/AMCC registers.
     * We present two I/O BARs:
     *   BAR0: 0x100 bytes for GPIB core
     *   BAR1: 0x100 bytes for bridge core
     */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 0x100;  /* enough for NEC7210 ioport range */
    s->bar_info[0].name  = "ines-gpib-io";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_PIO;
    s->bar_info[1].size  = 0x100;  /* enough for PLX/AMCC regs */
    s->bar_info[1].name  = "ines-bridge-io";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X configuration explicitly used by driver; keep disabled. */

    /* Initialize shadows and IRQ state. */
    memset(s->gpib_io, 0, sizeof(s->gpib_io));
    memset(s->bridge_io, 0, sizeof(s->bridge_io));
    s->irq_asserted = false;
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ines_gpib_pci",
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

