/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Altera CvP device model based on Linux driver altera-cvp.c.
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

#define TYPE_PCIBASE_DEVICE "altera_cvp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ALTERA_VENDOR_ID         0x1172
#define ALTERA_DEVICE_ID         0x0000
#define ALTERA_CLASS_ID          0x00FF00 /* TODO: request exact class */

#define CVP_TIMEOUT_US          2000
#define CVP_DUMMY_WR            244

/* VSE CVP register offsets */
#define VSE_CVP_STATUS           0x1c
#define VSE_CVP_STATUS_CFG_RDY   (1 << 18)
#define VSE_CVP_STATUS_CFG_ERR   (1 << 19)
#define VSE_CVP_STATUS_CVP_EN    (1 << 20)
#define VSE_CVP_STATUS_USERMODE  (1 << 21)
#define VSE_CVP_STATUS_CFG_DONE  (1 << 23)
#define VSE_CVP_STATUS_PLD_CLK_IN_USE (1 << 24)
#define VSE_CVP_MODE_CTRL        0x20
#define VSE_CVP_MODE_CTRL_CVP_MODE     (1 << 0)
#define VSE_CVP_MODE_CTRL_HIP_CLK_SEL  (1 << 1)
#define VSE_CVP_MODE_CTRL_NUMCLKS_OFF  8
#define VSE_CVP_MODE_CTRL_NUMCLKS_MASK 0xFF00
#define VSE_CVP_DATA             0x28
#define VSE_CVP_PROG_CTRL        0x2c
#define VSE_CVP_PROG_CTRL_CONFIG      (1 << 0)
#define VSE_CVP_PROG_CTRL_START_XFER  (1 << 1)
#define VSE_CVP_PROG_CTRL_MASK        0x3
#define VSE_UNCOR_ERR_STATUS     0x34
#define VSE_UNCOR_ERR_CVP_CFG_ERR      (1 << 5)
#define VSE_CVP_TX_CREDITS       0x49

#define V1_VSEC_OFFSET           0x200
#define V1_POLL_TIMEOUT_US       10
#define V2_CREDIT_TIMEOUT_US     40000
#define V2_CHECK_CREDIT_US       10
#define V2_POLL_TIMEOUT_US       1000000
#define V2_USER_TIMEOUT_US       500000

#define ALTERA_CVP_V1_SIZE       4
#define ALTERA_CVP_V2_SIZE       4096

/* BAR0 size: the driver maps this BAR for MMIO writes; a small size suffices */
#define ALTERA_CVP_BAR0_SIZE     0x1000

/* Driver internal constants, not hardware registers, but captured for completeness */
#define ALTERA_CVP_MGR_NAME      "Altera CvP FPGA Manager"

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

    /* Hardware Register Shadows (optional, kept for completeness) */
    struct cvp_regs_t {
        uint32_t status;
        uint32_t mode_ctrl;
        uint32_t data;
        uint32_t prog_ctrl;
        uint32_t uncor_err_status;
        uint32_t tx_credits;
    } regs;
};

/* MMIO Handlers: the driver writes data to BAR0 via writel() */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The driver never reads from this BAR; return 0 */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* The driver sends bitstream data via 32-bit writes; accept and ignore */
    return;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not used */
    return ~0ULL;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not used */
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
    PCIDevice *pdev = PCI_DEVICE(s);

    pci_device_reset(pdev);

    /* Reset shadow registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.status = VSE_CVP_STATUS_CVP_EN;
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

    /* Ensure PCIe extended config space is available */
    pdev->config_size = PCIE_CONFIG_SPACE_SIZE;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ALTERA_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ALTERA_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ALTERA_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Add Vendor-Specific Extended Capability at offset 0x200 */
    pcie_add_capability(pdev, PCI_EXT_CAP_ID_VNDR, 1, 0x200, 0x50);
    /* Vendor-Specific Header: vendor ID = 0x1172 (upper 16 bits),
     * VSEC ID = 0x1172 (lower 16 bits) */
    pci_set_long(pci_conf + 0x200 + 4, (0x1172 << 16) | 0x1172);
    /* Status register: CVP_EN bit must be set for driver probe to succeed */
    pci_set_long(pci_conf + 0x200 + 0x1c, VSE_CVP_STATUS_CVP_EN);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type  = BAR_TYPE_MMIO,
        .size  = ALTERA_CVP_BAR0_SIZE,
        .name  = "altera-cvp-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI or MSI-X */
    /* No DMA configuration */
    /* No timer configuration */
    /* No additional field init */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No additional uninit needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "altera_cvp_pci",
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