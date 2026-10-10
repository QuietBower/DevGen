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

#define TYPE_PCIBASE_DEVICE "i82975x_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_82975_0 0x277c
#define I82975X_EAP 0x58
#define I82975X_DERRSYN 0x5c
#define I82975X_DES 0x5d
#define I82975X_ERRSTS 0xc8
#define I82975X_ERRCMD 0xca
#define I82975X_SMICMD 0xcc
#define I82975X_SCICMD 0xce
#define I82975X_XEAP 0xfc
#define I82975X_MCHBAR 0x44
#define I82975X_DRB 0x100
#define I82975X_DRB_CH0R0 0x100
#define I82975X_DRB_CH0R1 0x101
#define I82975X_DRB_CH0R2 0x102
#define I82975X_DRB_CH0R3 0x103
#define I82975X_DRB_CH1R0 0x180
#define I82975X_DRB_CH1R1 0x181
#define I82975X_DRB_CH1R2 0x182
#define I82975X_DRB_CH1R3 0x183
#define I82975X_DRA 0x108
#define I82975X_DRA_CH0R01 0x108
#define I82975X_DRA_CH0R23 0x109
#define I82975X_DRA_CH1R01 0x188
#define I82975X_DRA_CH1R23 0x189
#define I82975X_BNKARC 0x10e
#define I82975X_C0BNKARC 0x10e
#define I82975X_C1BNKARC 0x18e
#define I82975X_DRC 0x120
#define I82975X_DRC_CH0M0 0x120
#define I82975X_DRC_CH1M0 0x1A0
#define I82975X_DRC_M1 0x124
#define I82975X_DRC_CH0M1 0x124
#define I82975X_DRC_CH1M1 0x1A4

#define I82975X_DRB_SHIFT 25

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
    struct {
        uint32_t eap;
        uint8_t des;
        uint8_t derrsyn;
        uint16_t errsts;
        uint16_t errcmd;
        uint16_t smicmd;
        uint16_t scicmd;
        uint8_t xeap;
        uint32_t mchbar;
        uint8_t drb[2][4];
        uint8_t dra[2][2];
        uint16_t bnkarc[2];
        uint32_t drc[2][2];
    } regs;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case I82975X_DRB_CH0R0: val = s->regs.drb[0][0]; break;
    case I82975X_DRB_CH0R1: val = s->regs.drb[0][1]; break;
    case I82975X_DRB_CH0R2: val = s->regs.drb[0][2]; break;
    case I82975X_DRB_CH0R3: val = s->regs.drb[0][3]; break;
    case I82975X_DRB_CH1R0: val = s->regs.drb[1][0]; break;
    case I82975X_DRB_CH1R1: val = s->regs.drb[1][1]; break;
    case I82975X_DRB_CH1R2: val = s->regs.drb[1][2]; break;
    case I82975X_DRB_CH1R3: val = s->regs.drb[1][3]; break;
    case I82975X_DRC_CH0M0: val = s->regs.drc[0][0]; break;
    case I82975X_DRC_CH1M0: val = s->regs.drc[1][0]; break;
    case I82975X_C0BNKARC:  val = s->regs.bnkarc[0]; break;
    case I82975X_C1BNKARC:  val = s->regs.bnkarc[1]; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write at offset 0x%" HWADDR_PRIx "\n", __func__, addr);
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

    /* Initialize DRB to simulate 4 rows of memory per channel (128MB each row) */
    s->regs.drb[0][0] = 0x04;
    s->regs.drb[0][1] = 0x08;
    s->regs.drb[0][2] = 0x0C;
    s->regs.drb[0][3] = 0x10;
    s->regs.drb[1][0] = 0x04;
    s->regs.drb[1][1] = 0x08;
    s->regs.drb[1][2] = 0x0C;
    s->regs.drb[1][3] = 0x10;

    /* Initialize DRC to simulate ECC enabled */
    s->regs.drc[0][0] = (1 << 21);
    s->regs.drc[1][0] = (1 << 21);

    s->regs.bnkarc[0] = 0;
    s->regs.bnkarc[1] = 0;

    /* Initialize PCI config space registers */
    uint8_t *conf = s->parent_obj.config;
    pci_set_word(conf + I82975X_ERRSTS, 0);
    pci_set_long(conf + I82975X_EAP, 0);
    pci_set_byte(conf + I82975X_XEAP, 0);
    pci_set_byte(conf + I82975X_DES, 0);
    pci_set_byte(conf + I82975X_DERRSYN, 0);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    /* Handle ERRSTS W1C (Write 1 to Clear) */
    if (addr == I82975X_ERRSTS && len == 2) {
        uint16_t current = pci_get_word(pdev->config + I82975X_ERRSTS);
        current &= ~(val & 0x0003);
        pci_set_word(pdev->config + I82975X_ERRSTS, current);
        return;
    }

    pci_default_write_config(pdev, addr, val, len);

    /* Sync BAR0 to MCHBAR register */
    if (addr >= 0x10 && addr <= 0x13) {
        uint32_t bar0 = pci_default_read_config(pdev, 0x10, 4);
        if (bar0 != 0 && bar0 != 0xFFFFFFFF) {
            pci_set_long(pdev->config + I82975X_MCHBAR, (bar0 & 0xffffc000) | 1);
        } else {
            pci_set_long(pdev->config + I82975X_MCHBAR, 0);
        }
    }
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_82975_0 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
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
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "mchbar";

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
    .name = "i82975x_edac_pci",
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
    k->config_write = pcibase_config_write;
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
