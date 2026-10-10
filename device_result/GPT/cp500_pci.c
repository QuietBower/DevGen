/*
 * QEMU PCI device model for KEBA CP500 (Functional Implementation)
 * Generated based on linux-6.18/drivers/misc/keba/cp500.c
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
#include "hw/hw.h"

#define TYPE_PCIBASE_DEVICE "cp500_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CP500_PCI_VENDOR_ID        0xCEBA
#define CP500_PCI_DEVICE_ID_CP035  0x2706
#define CP500_PCI_CLASS_ID         PCI_CLASS_OTHERS

#define CP500_SYS_BAR              0
#define CP500_ECM_BAR              1

#define CP500_VERSION_REG          0x00
#define CP500_RECONFIG_REG         0x11
#define CP500_PRESENT_REG          0x20
#define CP500_AXI_REG              0x40

#define CP500_BUILD_TEST           0x8000
#define CP500_RECFG_REQ            0x01
#define CP500_PRESENT_FAN0         0x01

#define CP500_AXI_MSIX             3
#define CP500_RFB_UART_MSIX        4
#define CP500_DEBUG_UART_MSIX      5
#define CP500_SI1_UART_MSIX        6

#define CP500_NUM_MSIX             8
#define CP500_NUM_MSIX_NO_MMI      2
#define CP500_NUM_MSIX_NO_AXI      3

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

typedef struct CP500VersionRegs {
    int32_t major;
    int32_t minor;
    int32_t build;
} CP500VersionRegs;

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
    CP500VersionRegs version_regs;

    /* Simple register shadows for SYS BAR */
    uint8_t  reconfig_reg;
    uint8_t  present_reg;
    uint32_t axi_reg;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The provided driver only requests an MSI-X vector and installs
     * cp500_axi_handler(), but never programs device interrupt enable/
     * status registers or expects the device to actually raise an
     * interrupt.  Therefore we intentionally do not assert any IRQ
     * line here to avoid surprising the guest.
     */
    (void)s;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only CP500_SYS_BAR registers have defined semantics in the
     * provided driver snippet: VERSION, RECONFIG, PRESENT, AXI.
     * All of them are accessed with ioread8/ioread32, so we
     * support 1-byte and 4-byte loads for these offsets.
     */

    switch (addr) {
    case CP500_VERSION_REG:
        /* 32-bit FPGA version register layout as used in cp500_probe():
         *   bits  7:0   major
         *   bits 15:8   minor
         *   bits 31:16  build
         */
        if (size == 4) {
            uint32_t vers = 0;
            vers |= (uint32_t)(s->version_regs.major & 0xff);
            vers |= (uint32_t)(s->version_regs.minor & 0xff) << 8;
            vers |= (uint32_t)(s->version_regs.build & 0xffff) << 16;
            val = vers;
        } else {
            /* For non-32-bit accesses we just return 0, as the
             * driver only uses ioread32() here.
             */
            val = 0;
        }
        break;

    case CP500_RECONFIG_REG:
        /* 8-bit RECONFIG register accessed via ioread8() in
         * keep_cfg_show().
         */
        if (size == 1) {
            val = s->reconfig_reg;
        } else {
            val = 0;
        }
        break;

    case CP500_PRESENT_REG:
        /* 8-bit PRESENT register accessed via ioread8() in
         * cp500_register_auxiliary_devs().  We expose FAN0 as
         * present so that the fan auxiliary device is registered.
         */
        if (size == 1) {
            val = s->present_reg;
        } else {
            val = 0;
        }
        break;

    case CP500_AXI_REG:
        /* 32-bit AXI address register read in cp500_axi_handler().
         * The real hardware reports the offending AXI address on
         * an error.  We do not emulate AXI errors, so just return
         * the stored shadow (initialized to 0).
         */
        if (size == 4) {
            val = s->axi_reg;
        } else {
            val = 0;
        }
        break;

    default:
        /* Unimplemented offsets return 0; driver never relies on
         * any other location in the provided code.
         */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Only CP500_RECONFIG_REG is written by the driver via iowrite8()
     * in keep_cfg_store().  Other writes are not used in the provided
     * source, so we ignore them.
     */

    switch (addr) {
    case CP500_RECONFIG_REG:
        if (size == 1) {
            /* Store the new value; the semantics in the driver are:
             *   write 0                -> keep configuration (bit cleared)
             *   write CP500_RECFG_REQ  -> request reconfiguration
             * QEMU does not need to act on this beyond preserving the
             * shadow so that subsequent reads reflect the last value.
             */
            s->reconfig_reg = (uint8_t)(val & 0xff);
        }
        break;

    case CP500_VERSION_REG:
    case CP500_PRESENT_REG:
    case CP500_AXI_REG:
        /* These locations are never written in the provided driver
         * code; treat writes as no-ops to keep behavior simple and
         * deterministic.
         */
        break;

    default:
        /* Ignore any other writes within the BAR range. */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The cp500 driver does not use legacy port I/O; we expose a
     * dummy region that always reads as zero.
     */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* The cp500 driver does not use legacy port I/O; ignore writes. */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Revert registers to power-on defaults that are relevant for
     * the provided driver.
     */

    /* Present all base functionality and FAN0 so that the driver
     * sees the expected capabilities and registers auxiliary
     * devices (I2C, SPI, FAN, BATT, UARTs).
     *
     * The driver only checks CP500_PRESENT_FAN0; it registers other
     * auxiliary devices unconditionally, using hard-coded offsets.
     */
    s->present_reg = CP500_PRESENT_FAN0;

    /* Default RECONFIG register: configuration kept (bit cleared). */
    s->reconfig_reg = 0x00;

    /* Default AXI register: no error, 0 address. */
    s->axi_reg = 0x00000000;

    /* Provide a plausible, stable FPGA version that identifies the
     * device as CP035 in cp500_get_fpga_version():
     *
     *   CP500_IS_CP035() is not included in the snippet, but the
     *   driver otherwise falls back to a generic CP500 string.
     *   The exact values are not interpreted further, so we can
     *   choose a simple non-test build.
     */
    s->version_regs.major = 1;
    s->version_regs.minor = 0;
    s->version_regs.build = 0;  /* CP500_BUILD_TEST bit cleared */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CP500_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CP500_PCI_DEVICE_ID_CP035 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CP500_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = CP500_SYS_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000; /* 64 KiB to cover sub-functions SZ_4K windows */
    s->bar_info[0].name = "cp500-sys";

    s->bar_info[1].index = CP500_ECM_BAR;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x10000; /* size not explicitly defined, conservative */
    s->bar_info[1].name = "cp500-ecm";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* The driver uses pci_alloc_irq_vectors(..., PCI_IRQ_MSIX) to obtain
     * MSI-X vectors.  We only need to expose a MSI-X capability so that
     * the core PCI layer in the guest can successfully allocate them.
     */
    s->has_msix = true;
    s->has_msi = false;

    if (msix_init_exclusive_bar(pdev, CP500_NUM_MSIX, CP500_ECM_BAR, errp)) {
        /* If MSI-X initialization fails, clear the flag but otherwise
         * keep the device functional.  The driver will then fail to
         * allocate MSI-X vectors and abort probe, matching real
         * hardware behavior in such a situation.
         */
        s->has_msix = false;
    }

    /* Initialize power-on register defaults. */
    pcibase_reset(DEVICE(pdev));

    /* Final state initialization before the device is 'live' */
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

    /* Free buffers, stop timers, etc. (none allocated in this model). */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cp500_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(reconfig_reg, PCIBaseState),
        VMSTATE_UINT8(present_reg, PCIBaseState),
        VMSTATE_UINT32(axi_reg, PCIBaseState),
        VMSTATE_INT32(version_regs.major, PCIBaseState),
        VMSTATE_INT32(version_regs.minor, PCIBaseState),
        VMSTATE_INT32(version_regs.build, PCIBaseState),
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

