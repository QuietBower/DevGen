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

#define TYPE_PCIBASE_DEVICE "ismt_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SMBBAR 0
#define PCI_DEVICE_ID_INTEL_S1200_SMT0 0x0c59

#define ISMT_DESC_ENTRIES 2
#define ISMT_MAX_RETRIES 3
#define ISMT_LOG_ENTRIES 3

#define ISMT_DESC_CWRL 0x01
#define ISMT_DESC_BLK 0X04
#define ISMT_DESC_FAIR 0x08
#define ISMT_DESC_PEC 0x10
#define ISMT_DESC_I2C 0x20
#define ISMT_DESC_INT 0x40
#define ISMT_DESC_SOE 0x80

#define ISMT_DESC_SCS 0x01
#define ISMT_DESC_DLTO 0x04
#define ISMT_DESC_NAK 0x08
#define ISMT_DESC_CRC 0x10
#define ISMT_DESC_CLTO 0x20
#define ISMT_DESC_COL 0x40
#define ISMT_DESC_LPR 0x80

#define ISMT_DESC_ADDR_RW(addr, rw) (((addr) << 1) | (rw))

#define ISMT_GR_GCTRL 0x000
#define ISMT_GR_SMTICL 0x008
#define ISMT_GR_ERRINTMSK 0x010
#define ISMT_GR_ERRAERMSK 0x014
#define ISMT_GR_ERRSTS 0x018
#define ISMT_GR_ERRINFO 0x01c

#define ISMT_MSTR_MDBA 0x100
#define ISMT_MSTR_MCTRL 0x108
#define ISMT_MSTR_MSTS 0x10c
#define ISMT_MSTR_MDS 0x110
#define ISMT_MSTR_RPOLICY 0x114
#define ISMT_SPGT 0x300

#define ISMT_GCTRL_TRST 0x04
#define ISMT_GCTRL_KILL 0x08
#define ISMT_GCTRL_SRST 0x40

#define ISMT_MCTRL_SS 0x01
#define ISMT_MCTRL_MEIE 0x10
#define ISMT_MCTRL_FMHP 0x00ff0000

#define ISMT_MSTS_HMTP 0xff0000
#define ISMT_MSTS_MIS 0x20
#define ISMT_MSTS_MEIS 0x10
#define ISMT_MSTS_IP 0x01

#define ISMT_MDS_MASK 0xff

#define ISMT_SPGT_SPD_MASK 0xc0000000
#define ISMT_SPGT_SPD_80K 0x00
#define ISMT_SPGT_SPD_100K (0x1 << 30)
#define ISMT_SPGT_SPD_400K (0x2U << 30)
#define ISMT_SPGT_SPD_1M (0x3U << 30)

#define ISMT_MSICTL_MSIE 0x01

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
    uint32_t errintmsk;
    uint32_t erraermsk;
    uint32_t errsts;
    uint32_t errinfo;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t gr_gctrl;
    uint64_t gr_smticl;
    uint64_t mstr_mdba;
    uint32_t mstr_mctrl;
    uint32_t mstr_msts;
    uint32_t mstr_mds;
    uint32_t mstr_rpolicy;
    uint32_t spgt;

    /* DMA Context */
    dma_addr_t io_rng_dma;
    dma_addr_t log_dma;
};

struct ismt_desc {
    uint8_t tgtaddr_rw;
    uint8_t wr_len_cmd;
    uint8_t rd_len;
    uint8_t control;
    uint8_t status;
    uint8_t retry;
    uint8_t rxbytes;
    uint8_t txbytes;
    uint32_t dptr_low;
    uint32_t dptr_high;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_pending = (s->mstr_msts & (ISMT_MSTS_MIS | ISMT_MSTS_MEIS)) != 0;
    bool irq_enabled = (s->mstr_mctrl & ISMT_MCTRL_MEIE) != 0;

    if (irq_pending && irq_enabled) {
        if (msi_enabled(pdev)) {
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

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct ismt_desc desc;
    uint32_t fmhp = (s->mstr_mctrl & ISMT_MCTRL_FMHP) >> 16;
    uint32_t head = (fmhp + ISMT_DESC_ENTRIES - 1) % ISMT_DESC_ENTRIES;
    dma_addr_t desc_addr = s->mstr_mdba + head * sizeof(struct ismt_desc);

    pci_dma_read(pdev, desc_addr, &desc, sizeof(desc));

    if (desc.rd_len > 0 && (desc.dptr_low != 0 || desc.dptr_high != 0)) {
        dma_addr_t data_addr = ((uint64_t)desc.dptr_high << 32) | desc.dptr_low;
        uint8_t dummy_data[256] = {0};
        /* Satisfy block read length checks */
        dummy_data[0] = desc.rd_len > 1 ? desc.rd_len - 1 : 0;
        pci_dma_write(pdev, data_addr, dummy_data, desc.rd_len);
    }

    /* Mark as successful */
    desc.status |= ISMT_DESC_SCS;
    desc.rxbytes = desc.rd_len;

    pci_dma_write(pdev, desc_addr, &desc, sizeof(desc));

    /* Clear Start bit and set Interrupt Status */
    s->mstr_mctrl &= ~ISMT_MCTRL_SS;
    s->mstr_msts |= ISMT_MSTS_MIS;

    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ISMT_GR_GCTRL:
        val = s->gr_gctrl;
        break;
    case ISMT_GR_SMTICL:
        val = s->gr_smticl;
        break;
    case ISMT_GR_SMTICL + 4:
        val = s->gr_smticl >> 32;
        break;
    case ISMT_GR_ERRINTMSK:
        val = s->errintmsk;
        break;
    case ISMT_GR_ERRAERMSK:
        val = s->erraermsk;
        break;
    case ISMT_GR_ERRSTS:
        val = s->errsts;
        break;
    case ISMT_GR_ERRINFO:
        val = s->errinfo;
        break;
    case ISMT_MSTR_MDBA:
        val = s->mstr_mdba;
        break;
    case ISMT_MSTR_MDBA + 4:
        val = s->mstr_mdba >> 32;
        break;
    case ISMT_MSTR_MCTRL:
        val = s->mstr_mctrl;
        break;
    case ISMT_MSTR_MSTS:
        val = s->mstr_msts;
        break;
    case ISMT_MSTR_MDS:
        val = s->mstr_mds;
        break;
    case ISMT_MSTR_RPOLICY:
        val = s->mstr_rpolicy;
        break;
    case ISMT_SPGT:
        val = s->spgt;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ISMT_GR_GCTRL:
        s->gr_gctrl = val;
        break;
    case ISMT_GR_SMTICL:
        if (size == 4) s->gr_smticl = (s->gr_smticl & 0xFFFFFFFF00000000ULL) | val;
        else s->gr_smticl = val;
        break;
    case ISMT_GR_SMTICL + 4:
        s->gr_smticl = (s->gr_smticl & 0xFFFFFFFFULL) | (val << 32);
        break;
    case ISMT_GR_ERRINTMSK:
        s->errintmsk = val;
        break;
    case ISMT_GR_ERRAERMSK:
        s->erraermsk = val;
        break;
    case ISMT_GR_ERRSTS:
        s->errsts = val;
        break;
    case ISMT_GR_ERRINFO:
        s->errinfo = val;
        break;
    case ISMT_MSTR_MDBA:
        if (size == 4) s->mstr_mdba = (s->mstr_mdba & 0xFFFFFFFF00000000ULL) | val;
        else s->mstr_mdba = val;
        break;
    case ISMT_MSTR_MDBA + 4:
        s->mstr_mdba = (s->mstr_mdba & 0xFFFFFFFFULL) | (val << 32);
        break;
    case ISMT_MSTR_MCTRL:
        s->mstr_mctrl = val;
        if (val & ISMT_MCTRL_SS) {
            pcibase_do_dma(s, true);
        }
        break;
    case ISMT_MSTR_MSTS:
        s->mstr_msts &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case ISMT_MSTR_MDS:
        s->mstr_mds = val;
        break;
    case ISMT_MSTR_RPOLICY:
        s->mstr_rpolicy = val;
        break;
    case ISMT_SPGT:
        s->spgt = val;
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

    s->gr_gctrl = 0;
    s->gr_smticl = 0;
    s->errintmsk = 0;
    s->erraermsk = 0;
    s->errsts = 0;
    s->errinfo = 0;
    s->mstr_mdba = 0;
    s->mstr_mctrl = 0;
    s->mstr_msts = 0;
    s->mstr_mds = 0;
    s->mstr_rpolicy = 0;
    s->spgt = 0;

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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_S1200_SMT0);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_SMBUS);
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
    s->bar_info[0].index = SMBBAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "ismt-smbus-bar0";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = false;
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
    .name = "ismt_smbus_pci",
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
