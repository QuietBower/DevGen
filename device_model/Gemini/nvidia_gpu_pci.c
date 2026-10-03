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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "nvidia_gpu_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef GENMASK
#define GENMASK(h, l) (((1ULL << ((h) - (l) + 1)) - 1) << (l))
#endif

#define PCI_CLASS_SERIAL_UNKNOWN	0x0c80
#define I2C_MST_CNTL				0x00
#define I2C_MST_CNTL_GEN_START			BIT(0)
#define I2C_MST_CNTL_GEN_STOP			BIT(1)
#define I2C_MST_CNTL_CMD_READ			(1 << 2)
#define I2C_MST_CNTL_CMD_WRITE			(2 << 2)
#define I2C_MST_CNTL_BURST_SIZE_SHIFT		6
#define I2C_MST_CNTL_GEN_NACK			BIT(28)
#define I2C_MST_CNTL_STATUS			GENMASK(30, 29)
#define I2C_MST_CNTL_STATUS_OKAY		(0 << 29)
#define I2C_MST_CNTL_STATUS_NO_ACK		(1 << 29)
#define I2C_MST_CNTL_STATUS_TIMEOUT		(2 << 29)
#define I2C_MST_CNTL_STATUS_BUS_BUSY		(3 << 29)
#define I2C_MST_CNTL_CYCLE_TRIGGER		BIT(31)
#define I2C_MST_ADDR				0x04
#define I2C_MST_I2C0_TIMING				0x08
#define I2C_MST_I2C0_TIMING_SCL_PERIOD_100KHZ		0x10e
#define I2C_MST_I2C0_TIMING_TIMEOUT_CLK_CNT		16
#define I2C_MST_I2C0_TIMING_TIMEOUT_CLK_CNT_MAX		255
#define I2C_MST_I2C0_TIMING_TIMEOUT_CHECK		BIT(24)
#define I2C_MST_DATA					0x0c
#define I2C_MST_HYBRID_PADCTL				0x20
#define I2C_MST_HYBRID_PADCTL_MODE_I2C			BIT(0)
#define I2C_MST_HYBRID_PADCTL_I2C_SCL_INPUT_RCV		BIT(14)
#define I2C_MST_HYBRID_PADCTL_I2C_SDA_INPUT_RCV		BIT(15)

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
    
    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t i2c_mst_cntl;
    uint32_t i2c_mst_addr;
    uint32_t i2c_mst_i2c0_timing;
    uint32_t i2c_mst_data;
    uint32_t i2c_mst_hybrid_padctl;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case I2C_MST_CNTL:
        val = s->i2c_mst_cntl;
        break;
    case I2C_MST_ADDR:
        val = s->i2c_mst_addr;
        break;
    case I2C_MST_I2C0_TIMING:
        val = s->i2c_mst_i2c0_timing;
        break;
    case I2C_MST_DATA:
        val = s->i2c_mst_data;
        break;
    case I2C_MST_HYBRID_PADCTL:
        val = s->i2c_mst_hybrid_padctl;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case I2C_MST_CNTL:
        s->i2c_mst_cntl = val;
        /* Hardware immediately completes the command */
        s->i2c_mst_cntl &= ~I2C_MST_CNTL_CYCLE_TRIGGER;
        s->i2c_mst_cntl &= ~I2C_MST_CNTL_STATUS;
        s->i2c_mst_cntl |= I2C_MST_CNTL_STATUS_OKAY;
        break;
    case I2C_MST_ADDR:
        s->i2c_mst_addr = val;
        break;
    case I2C_MST_I2C0_TIMING:
        s->i2c_mst_i2c0_timing = val;
        break;
    case I2C_MST_DATA:
        s->i2c_mst_data = val;
        break;
    case I2C_MST_HYBRID_PADCTL:
        s->i2c_mst_hybrid_padctl = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
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

    s->i2c_mst_cntl = 0;
    s->i2c_mst_addr = 0;
    s->i2c_mst_i2c0_timing = 0;
    s->i2c_mst_data = 0;
    s->i2c_mst_hybrid_padctl = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NVIDIA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_UNKNOWN );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "i2c_mst_regs" };
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    if (s->has_msi) {
        msi_init(pdev, 0, 1, true, false, errp);
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
    .name = "nvidia_gpu_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(i2c_mst_cntl, PCIBaseState),
        VMSTATE_UINT32(i2c_mst_addr, PCIBaseState),
        VMSTATE_UINT32(i2c_mst_i2c0_timing, PCIBaseState),
        VMSTATE_UINT32(i2c_mst_data, PCIBaseState),
        VMSTATE_UINT32(i2c_mst_hybrid_padctl, PCIBaseState),
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
