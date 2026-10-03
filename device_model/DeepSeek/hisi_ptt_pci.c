/*
 * QEMU PCI device model for HiSilicon PCIe Tune and Trace device (hisi_ptt)
 * Based on Linux driver: drivers/hwtracing/ptt/hisi_ptt.c
 * QEMU 8.2.10 compatible
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

#define TYPE_PCIBASE_DEVICE "hisi_ptt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_HUAWEI  0x19e5
#define VENDOR_ID  PCI_VENDOR_ID_HUAWEI
#define DEVICE_ID  0xa12e
#define CLASS_ID   PCI_CLASS_OTHERS

/* Register offsets (from hisi_ptt.c) */
#define HISI_PTT_TUNING_CTRL            0x0000
#define HISI_PTT_TUNING_DATA            0x0004
#define HISI_PTT_TUNING_INT_STAT        0x0898
#define HISI_PTT_TUNING_INT_STAT_MASK   BIT(0)
#define HISI_PTT_TUNING_CTRL_SUB        GENMASK(23, 16)
#define HISI_PTT_TUNING_DATA_VAL_MASK   GENMASK(15, 0)
#define HISI_PTT_TUNING_CTRL_CODE       GENMASK(15, 0)

#define HISI_PTT_TRACE_CTRL             0x0850
#define HISI_PTT_TRACE_CTRL_EN          BIT(0)
#define HISI_PTT_TRACE_CTRL_RST         BIT(1)
#define HISI_PTT_TRACE_CTRL_RXTX_SEL    GENMASK(3, 2)
#define HISI_PTT_TRACE_CTRL_TYPE_SEL    GENMASK(7, 4)
#define HISI_PTT_TRACE_CTRL_DATA_FORMAT BIT(14)
#define HISI_PTT_TRACE_CTRL_FILTER_MODE BIT(15)
#define HISI_PTT_TRACE_CTRL_TARGET_SEL  GENMASK(31, 16)

#define HISI_PTT_TRACE_ADDR_SIZE        0x0800
#define HISI_PTT_TRACE_ADDR_BASE_LO_0   0x0810
#define HISI_PTT_TRACE_ADDR_BASE_HI_0   0x0814
#define HISI_PTT_TRACE_ADDR_STRIDE      0x8

#define HISI_PTT_TRACE_WR_STS           0x08a0
#define HISI_PTT_TRACE_WR_STS_WRITE     GENMASK(27, 0)

#define HISI_PTT_TRACE_STS              0x08b0
#define HISI_PTT_TRACE_IDLE             BIT(0)

#define HISI_PTT_TRACE_INT_STAT         0x0890
#define HISI_PTT_TRACE_INT_STAT_MASK    GENMASK(3, 0)
#define HISI_PTT_TRACE_INT_MASK         0x0894
#define HISI_PTT_TRACE_INT_MASK_ALL     GENMASK(3, 0)

#define HISI_PTT_DEVICE_RANGE           0x0fe0
#define HISI_PTT_DEVICE_RANGE_UPPER     GENMASK(31, 16)
#define HISI_PTT_DEVICE_RANGE_LOWER     GENMASK(15, 0)

#define HISI_PTT_LOCATION               0x0fe8
#define HISI_PTT_SICL_ID                GENMASK(31, 16)
#define HISI_PTT_CORE_ID                GENMASK(15, 0)

#define HISI_PTT_TRACE_BUF_CNT          4
#define HISI_PTT_TRACE_BUF_SIZE         (4 * 1024 * 1024)  /* SZ_4M */
#define HISI_PTT_TRACE_TOTAL_BUF_SIZE   (HISI_PTT_TRACE_BUF_SIZE * HISI_PTT_TRACE_BUF_CNT)

/* Bit manipulation helpers */
#define BIT(nr) (1UL << (nr))
#define GENMASK(msb, lsb) (((1UL << ((msb) - (lsb) + 1)) - 1) << (lsb))

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
    uint32_t tuning_ctrl;
    uint32_t tuning_data;
    uint32_t tuning_int_stat;
    uint32_t trace_ctrl;
    uint32_t trace_addr_size;
    uint32_t trace_addr_base_lo[4];
    uint32_t trace_addr_base_hi[4];
    uint32_t trace_wr_sts;
    uint32_t trace_sts;
    uint32_t trace_int_stat;
    uint32_t trace_int_mask;
    uint32_t device_range;
    uint32_t location;

    /* DMA Context */
    struct hisi_ptt_dma_buffer {
        dma_addr_t dma;
        void *addr;
    } trace_buf[4];
    int current_buf_index;

    /* Operational status flags */
    uint32_t status;

    /* Reset state */
    bool in_reset;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->trace_int_stat & ~s->trace_int_mask;
    if (pending) {
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

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA not required for probe; placeholder removed */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x1000) {
        return 0;
    }

    switch (addr) {
    case HISI_PTT_TUNING_CTRL:
        val = s->tuning_ctrl;
        break;
    case HISI_PTT_TUNING_DATA:
        val = s->tuning_data;
        break;
    case HISI_PTT_TUNING_INT_STAT:
        val = s->tuning_int_stat;
        break;
    case HISI_PTT_TRACE_CTRL:
        val = s->trace_ctrl;
        break;
    case HISI_PTT_TRACE_ADDR_SIZE:
        val = s->trace_addr_size;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_LO_0:
        val = s->trace_addr_base_lo[0];
        break;
    case HISI_PTT_TRACE_ADDR_BASE_HI_0:
        val = s->trace_addr_base_hi[0];
        break;
    case HISI_PTT_TRACE_ADDR_BASE_LO_0 + 8:
        val = s->trace_addr_base_lo[1];
        break;
    case HISI_PTT_TRACE_ADDR_BASE_HI_0 + 8:
        val = s->trace_addr_base_hi[1];
        break;
    case HISI_PTT_TRACE_ADDR_BASE_LO_0 + 16:
        val = s->trace_addr_base_lo[2];
        break;
    case HISI_PTT_TRACE_ADDR_BASE_HI_0 + 16:
        val = s->trace_addr_base_hi[2];
        break;
    case HISI_PTT_TRACE_ADDR_BASE_LO_0 + 24:
        val = s->trace_addr_base_lo[3];
        break;
    case HISI_PTT_TRACE_ADDR_BASE_HI_0 + 24:
        val = s->trace_addr_base_hi[3];
        break;
    case HISI_PTT_TRACE_WR_STS:
        val = s->trace_wr_sts;
        break;
    case HISI_PTT_TRACE_STS:
        val = s->trace_sts;
        break;
    case HISI_PTT_TRACE_INT_STAT:
        val = s->trace_int_stat;
        break;
    case HISI_PTT_TRACE_INT_MASK:
        val = s->trace_int_mask;
        break;
    case HISI_PTT_DEVICE_RANGE:
        val = s->device_range;
        break;
    case HISI_PTT_LOCATION:
        val = s->location;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown read @ 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x1000) {
        return;
    }

    switch (addr) {
    case HISI_PTT_TUNING_CTRL:
        s->tuning_ctrl = val;
        break;
    case HISI_PTT_TUNING_DATA:
        s->tuning_data = val & HISI_PTT_TUNING_DATA_VAL_MASK;
        /* Tuning operation completes immediately; clear status */
        s->tuning_int_stat &= ~HISI_PTT_TUNING_INT_STAT_MASK;
        break;
    case HISI_PTT_TUNING_INT_STAT:
        /* Read-only status, ignore writes */
        break;
    case HISI_PTT_TRACE_CTRL:
        s->trace_ctrl = val;
        break;
    case HISI_PTT_TRACE_ADDR_SIZE:
        s->trace_addr_size = val;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_LO_0:
        s->trace_addr_base_lo[0] = val;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_HI_0:
        s->trace_addr_base_hi[0] = val;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_LO_0 + 8:
        s->trace_addr_base_lo[1] = val;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_HI_0 + 8:
        s->trace_addr_base_hi[1] = val;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_LO_0 + 16:
        s->trace_addr_base_lo[2] = val;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_HI_0 + 16:
        s->trace_addr_base_hi[2] = val;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_LO_0 + 24:
        s->trace_addr_base_lo[3] = val;
        break;
    case HISI_PTT_TRACE_ADDR_BASE_HI_0 + 24:
        s->trace_addr_base_hi[3] = val;
        break;
    case HISI_PTT_TRACE_WR_STS:
        /* Read-only status; ignore writes */
        break;
    case HISI_PTT_TRACE_STS:
        /* Read-only status; ignore writes */
        break;
    case HISI_PTT_TRACE_INT_STAT:
        /* Write-1-to-clear */
        s->trace_int_stat &= ~val;
        pcibase_update_irq(s);
        break;
    case HISI_PTT_TRACE_INT_MASK:
        s->trace_int_mask = val;
        pcibase_update_irq(s);
        break;
    case HISI_PTT_DEVICE_RANGE:
        /* Read-only register; ignore writes */
        break;
    case HISI_PTT_LOCATION:
        /* Read-only register; ignore writes */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unknown write @ 0x%" HWADDR_PRIx
                      " value 0x%" PRIx64 "\n", __func__, addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
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

    /* Reset all shadow registers to power-on defaults */
    s->tuning_ctrl = 0;
    s->tuning_data = 0;
    s->tuning_int_stat = 0;
    s->trace_ctrl = 0;
    s->trace_addr_size = 0;
    for (int i = 0; i < 4; i++) {
        s->trace_addr_base_lo[i] = 0;
        s->trace_addr_base_hi[i] = 0;
    }
    s->trace_wr_sts = 0;
    s->trace_sts = HISI_PTT_TRACE_IDLE; /* idle */
    s->trace_int_stat = 0;
    s->trace_int_mask = 0; /* interrupts unmasked by default */
    s->device_range = 0x00000000; /* no filters */
    s->location = 0x00000000;     /* sicl=0, core=0 */
    s->current_buf_index = 0;
    s->in_reset = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
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
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* 4KB, covers all registers */
    s->bar_info[0].name = "bar2";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;
    s->has_msix = false;
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
    .name = "hisi_ptt_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(tuning_ctrl, PCIBaseState),
        VMSTATE_UINT32(tuning_data, PCIBaseState),
        VMSTATE_UINT32(tuning_int_stat, PCIBaseState),
        VMSTATE_UINT32(trace_ctrl, PCIBaseState),
        VMSTATE_UINT32(trace_addr_size, PCIBaseState),
        VMSTATE_UINT32_ARRAY(trace_addr_base_lo, PCIBaseState, 4),
        VMSTATE_UINT32_ARRAY(trace_addr_base_hi, PCIBaseState, 4),
        VMSTATE_UINT32(trace_wr_sts, PCIBaseState),
        VMSTATE_UINT32(trace_sts, PCIBaseState),
        VMSTATE_UINT32(trace_int_stat, PCIBaseState),
        VMSTATE_UINT32(trace_int_mask, PCIBaseState),
        VMSTATE_UINT32(device_range, PCIBaseState),
        VMSTATE_UINT32(location, PCIBaseState),
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
