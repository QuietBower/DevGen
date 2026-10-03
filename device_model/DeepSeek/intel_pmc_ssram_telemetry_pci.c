
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

#define TYPE_PCIBASE_DEVICE "intel_pmc_ssram_telemetry_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SSRAM_HDR_SIZE      0x100
#define SSRAM_PWRM_OFFSET   0x14
#define SSRAM_DVSEC_OFFSET  0x1C
#define SSRAM_DVSEC_SIZE    0x10
#define SSRAM_PCH_OFFSET    0x60
#define SSRAM_IOE_OFFSET    0x68
#define SSRAM_DEVID_OFFSET  0x70
#define MAX_NUM_PMC         3
#define PMC_DEVID_LNL_SOCM  0xa87f
#define PMC_DEVID_WCL_PCDN  0x4d7f
#define PMC_DEVID_ARL_SOCS  0xae7f
#define PMC_DEVID_PTL_PCDP  0xe47f
#define PMC_DEVID_MTL_SOCM  0x7e7f
#define PMC_DEVID_PTL_PCDH  0xe37f
#define PMC_DEVID_ARL_SOCM  0x777f

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

    /* SSRAM data store: 4KB to cover BAR0 region */
    uint8_t ssram_data[4096];
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > sizeof(s->ssram_data)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds read at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return ~0ULL;
    }

    switch (size) {
    case 1:
        val = s->ssram_data[addr];
        break;
    case 2:
        val = lduw_le_p(s->ssram_data + addr);
        break;
    case 4:
        val = ldl_le_p(s->ssram_data + addr);
        break;
    case 8:
        val = ldq_le_p(s->ssram_data + addr);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported read size %u\n", __func__, size);
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > sizeof(s->ssram_data)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds write at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return;
    }

    switch (size) {
    case 1:
        s->ssram_data[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(s->ssram_data + addr, (uint16_t)val);
        break;
    case 4:
        stl_le_p(s->ssram_data + addr, (uint32_t)val);
        break;
    case 8:
        stq_le_p(s->ssram_data + addr, val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write size %u\n", __func__, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO BAR configured; returns all ones for unmapped I/O */
    return ~0ULL;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO BAR configured; ignore writes */
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

    /* Reset shadow registers to power-on defaults */
    memset(s->ssram_data, 0, sizeof(s->ssram_data));
    stl_le_p(s->ssram_data + SSRAM_DVSEC_OFFSET, 0x200);
    stq_le_p(s->ssram_data + SSRAM_PWRM_OFFSET, 0xFE000000ULL);
    stw_le_p(s->ssram_data + SSRAM_DEVID_OFFSET, 0x7e7f);
    stq_le_p(s->ssram_data + SSRAM_IOE_OFFSET, 0);
    stq_le_p(s->ssram_data + SSRAM_PCH_OFFSET, 0);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x7e7f );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0880);
    pci_set_byte(pci_conf + 0x09, 0x00); /* prog-if = 0 */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Add Intel DVSEC extended capability */
    #define PCI_DVSEC_HEADER1          0x4
    #define PCI_DVSEC_HEADER2          0x8
    #define INTEL_DVSEC_ENTRIES        0xA
    #define INTEL_DVSEC_SIZE           0xB
    #define INTEL_DVSEC_TABLE          0xC
    #define PCI_DVSEC_HEADER1_REV(x)   (((x) >> 16) & 0xf)
    #define PCI_DVSEC_HEADER1_LEN(x)   (((x) >> 20) & 0xfff)
    #define INTEL_DVSEC_TABLE_BAR(x)   ((x) & 7)
    #define INTEL_DVSEC_TABLE_OFFSET(x) ((x) & ~7)

    uint32_t dvsect_len = 0x14; /* 20 bytes: 8 byte header + 3 DWORDs */
    pcie_add_capability(pdev, PCI_EXT_CAP_ID_DVSEC, 0, 0x100, dvsect_len);
    uint16_t dvsect_off = pcie_find_capability(pdev, PCI_EXT_CAP_ID_DVSEC);
    if (dvsect_off) {
        uint8_t *dvsect_conf = pci_conf + dvsect_off;
        /* Header1: revision 1, length = dvsect_len */
        stl_le_p(dvsect_conf + PCI_DVSEC_HEADER1, (1 << 16) | (dvsect_len << 20));
        /* Header2: Intel vendor ID (0x8086) */
        stw_le_p(dvsect_conf + PCI_DVSEC_HEADER2, 0x8086);
        /* Intel-specific DVSEC registers: entries, size, table bar/offset */
        stl_le_p(dvsect_conf + INTEL_DVSEC_ENTRIES, 0);   /* TODO: expected value needed */
        stl_le_p(dvsect_conf + INTEL_DVSEC_SIZE, 0);      /* TODO: expected value needed */
        stl_le_p(dvsect_conf + INTEL_DVSEC_TABLE, 0);     /* bar=0, offset=0 */
    }

    /* BAR Configuration */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;  /* 4KB, power of two */
    s->bar_info[0].name = "ssram-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize SSRAM data with default register values */
    memset(s->ssram_data, 0, sizeof(s->ssram_data));
    stl_le_p(s->ssram_data + SSRAM_DVSEC_OFFSET, 0x200);   /* dvsec offset */
    stq_le_p(s->ssram_data + SSRAM_PWRM_OFFSET, 0xFE000000ULL); /* pwrm base */
    stw_le_p(s->ssram_data + SSRAM_DEVID_OFFSET, 0x7e7f);  /* devid */
    stq_le_p(s->ssram_data + SSRAM_IOE_OFFSET, 0);         /* secondary IOE base (none) */
    stq_le_p(s->ssram_data + SSRAM_PCH_OFFSET, 0);         /* secondary PCH base (none) */
    /* DVSEC region at offset 0x200 currently all zeros; requires definitions */
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "intel_pmc_ssram_telemetry_pci",
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
