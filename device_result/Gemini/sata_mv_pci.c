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

#define TYPE_PCIBASE_DEVICE "sata_mv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define WINDOW_BASE(i)      (0x20034 + ((i) << 4))
#define WINDOW_CTRL(i)      (0x20030 + ((i) << 4))
#define ERR_IRQ(irq)        ((irq) << 16)
#define HC_CFG              0x0
#define SCR_STATUS          0x03000000

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
    uint32_t main_irq_mask;
    uint32_t main_irq_cause;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t irq_coal_cause;
    uint32_t hc_irq_cause;

    /* DMA Context */
    uint64_t crqb_dma;
    uint64_t crpb_dma;
    uint64_t sg_tbl_dma;
};

enum chip_type {
    chip_504x,
    chip_508x,
    chip_5080,
    chip_604x,
    chip_608x,
    chip_6042,
    chip_7042,
    chip_soc,
};

struct mv_crqb {
    uint32_t sg_addr;
    uint32_t sg_addr_hi;
    uint16_t ctrl_flags;
    uint16_t ata_cmd[11];
};

struct mv_crqb_iie {
    uint32_t addr;
    uint32_t addr_hi;
    uint32_t flags;
    uint32_t len;
    uint32_t ata_cmd[4];
};

struct mv_crpb {
    uint16_t id;
    uint16_t flags;
    uint32_t tmstmp;
};

struct mv_sg {
    uint32_t addr;
    uint32_t flags_size;
    uint32_t addr_hi;
    uint32_t reserved;
};

struct mv_cached_regs {
    uint32_t fiscfg;
    uint32_t ltmode;
    uint32_t haltcond;
    uint32_t unknown_rsvd;
};

struct asd_ddb_stp_sata_target_port {
    uint8_t     conn_type;
    uint8_t     conn_rate;
    uint16_t    init_conn_tag;
    uint8_t     dest_sas_addr[8];
    uint16_t    send_queue_head;
    uint8_t     sq_suspended;
    uint8_t     ddb_type;
    uint16_t    _r_a;
    uint16_t    awt_def;
    uint8_t     compat_features;
    uint8_t     pathway_blocked_count;
    uint16_t    arb_wait_time;
    uint32_t    more_compat_features;
    uint8_t     conn_mask;
    uint8_t     flags;
#define SATA_MULTIPORT     0x80
#define SUPPORTS_AFFIL     0x40
#define STP_AFFIL_POL      0x20
    uint8_t     _r_b;
    uint8_t     flags2;
#define STP_CL_POL_NO_TX    0x00
#define STP_CL_POL_BTW_CMDS 0x01
    uint16_t    exec_queue_tail;
    uint16_t    send_queue_tail;
    uint16_t    sister_ddb;
    uint16_t    ata_cmd_scbptr;
    uint32_t    sata_tag_alloc_mask;
    uint16_t    active_task_count;
    uint16_t    _r_c;
    uint32_t    sata_sactive;
    uint8_t     num_sata_tags;
    uint8_t     sata_status;
    uint8_t     sata_ending_status;
    uint8_t     itnl_reason;
    uint16_t    ncq_data_scb_ptr;
    uint16_t    itnl_timeout;
    uint32_t    itnl_timestamp;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x11ab );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x5040 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0106 );
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
    s->bar_info[0].size = 0x40000;
    s->bar_info[0].name = "sata_mv_mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "sata_mv_pci",
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
