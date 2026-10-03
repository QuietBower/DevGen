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

#define TYPE_PCIBASE_DEVICE "sata_sis_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1039
#define DEVICE_ID 0x0180
#define CLASS_ID  0x0106

/* SiS-specific registers (from kernel sata_sis.h) */
#define SIS_SCR_PCI_BAR      5
#define SIS_SCR_BASE         0xC0
#define SIS_GENCTL           0x54
#define SIS_PMR              0x90
#define GENCTL_IOMAPPED_SCR  (1 << 26)
#define SIS_PMR_COMBINED     (1 << 3)
#define SIS180_SATA1_OFS     0x20
#define SIS182_SATA1_OFS     0x40

/* Standard ATA BMDMA register offsets */
#define BMDMA_PRIMARY   0x00
#define BMDMA_SECONDARY 0x08

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
    const MemoryRegionOps *ops;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;
    uint32_t sis_genctl;
    uint8_t sis_pmr;
    uint32_t scr[2][4];
    int port2_start;
    struct {
        uint8_t cmd;
        uint8_t status;
        uint32_t prdt;
    } bmdma[2];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int irq_level = 0;
    if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
        irq_level = (s->bmdma[0].status & 0x04) || (s->bmdma[1].status & 0x04);
        pci_set_irq(pdev, irq_level);
    }
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;
    if (ranges_overlap(addr, len, SIS_GENCTL, 4)) {
        val = s->sis_genctl;
    } else if (ranges_overlap(addr, len, SIS_PMR, 1)) {
        val = s->sis_pmr;
    } else if (ranges_overlap(addr, len, SIS_SCR_BASE, 0x40)) {
        unsigned offset = addr - SIS_SCR_BASE;
        int port, reg;
        if (offset < 0x10) {
            port = 0;
        } else if (offset >= s->port2_start && offset < s->port2_start + 0x10) {
            port = 1;
        } else {
            val = 0xFFFFFFFF;
            return val;
        }
        reg = (offset % 0x10) / 4;
        val = s->scr[port][reg];
    } else {
        val = pci_default_read_config(pdev, addr, len);
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (ranges_overlap(addr, len, SIS_GENCTL, 4)) {
        s->sis_genctl = val;
    } else if (ranges_overlap(addr, len, SIS_PMR, 1)) {
        s->sis_pmr = val & 0xff;
    } else if (ranges_overlap(addr, len, SIS_SCR_BASE, 0x40)) {
        unsigned offset = addr - SIS_SCR_BASE;
        int port, reg;
        if (offset < 0x10) {
            port = 0;
        } else if (offset >= s->port2_start && offset < s->port2_start + 0x10) {
            port = 1;
        } else {
            return;
        }
        reg = (offset % 0x10) / 4;
        s->scr[port][reg] = val;
    } else {
        pci_default_write_config(pdev, addr, val, len);
    }
}

/* Command block task file registers (BAR0, BAR2) */
static uint64_t pcibase_cmd_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* Minimal ATA task file emulation: all read-zero except status register */
    if (addr == 7) { /* Status register offset */
        return 0x50; /* RDY | DSC */
    }
    return 0;
}

static void pcibase_cmd_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignore writes to command block registers */
}

static const MemoryRegionOps pcibase_cmd_io_ops = {
    .read = pcibase_cmd_io_read,
    .write = pcibase_cmd_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* BMDMA registers (BAR4, only offsets 0x00-0x0F) */
static uint64_t pcibase_bmdma_io_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t offset = addr;

    if (offset < 0x10) {
        int channel = (offset >= BMDMA_SECONDARY) ? 1 : 0;
        uint16_t base = channel ? BMDMA_SECONDARY : BMDMA_PRIMARY;
        uint16_t reg_off = offset - base;
        switch (reg_off) {
        case 0:
            val = s->bmdma[channel].cmd;
            break;
        case 2:
            val = s->bmdma[channel].status;
            break;
        case 4:
            val = s->bmdma[channel].prdt;
            break;
        }
        return val;
    }
    return 0xFFFFFFFF;
}

static void pcibase_bmdma_io_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr;

    if (offset < 0x10) {
        int channel = (offset >= BMDMA_SECONDARY) ? 1 : 0;
        uint16_t base = channel ? BMDMA_SECONDARY : BMDMA_PRIMARY;
        uint16_t reg_off = offset - base;
        switch (reg_off) {
        case 0:
            s->bmdma[channel].cmd = val & 0xff;
            if (s->bmdma[channel].cmd & 1) {
                /* START: simulate immediate completion */
                s->bmdma[channel].status |= 0x04; /* BMIS_INT */
                pcibase_update_irq(s);
                s->bmdma[channel].cmd &= ~1;
            }
            break;
        case 2:
            /* Write-1-to-clear status bits */
            s->bmdma[channel].status &= ~(val & 0x06);
            pcibase_update_irq(s);
            break;
        case 4:
            s->bmdma[channel].prdt = val;
            break;
        }
    }
}

static const MemoryRegionOps pcibase_bmdma_io_ops = {
    .read = pcibase_bmdma_io_read,
    .write = pcibase_bmdma_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* SCR MMIO (BAR5) */
static uint64_t pcibase_scr_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xFFFFFFFF;
    uint32_t offset = addr;
    int port, reg;
    if (offset < 0x10) {
        port = 0;
    } else if (offset >= s->port2_start && offset < s->port2_start + 0x10) {
        port = 1;
        offset -= s->port2_start;
    } else {
        return 0xFFFFFFFF;
    }
    reg = offset / 4;
    if (reg < 4) {
        val = s->scr[port][reg];
    }
    return val;
}

static void pcibase_scr_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr;
    int port, reg;
    if (offset < 0x10) {
        port = 0;
    } else if (offset >= s->port2_start && offset < s->port2_start + 0x10) {
        port = 1;
        offset -= s->port2_start;
    } else {
        return;
    }
    reg = offset / 4;
    if (reg < 4) {
        s->scr[port][reg] = val;
    }
}

static const MemoryRegionOps pcibase_scr_mmio_ops = {
    .read = pcibase_scr_mmio_read,
    .write = pcibase_scr_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    s->sis_genctl = GENCTL_IOMAPPED_SCR;
    s->sis_pmr = 0;
    memset(s->scr, 0, sizeof(s->scr));
    s->port2_start = 64;
    for (int i = 0; i < 2; i++) {
        s->bmdma[i].cmd = 0;
        s->bmdma[i].status = 0;
        s->bmdma[i].prdt = 0;
    }
    pci_set_irq(PCI_DEVICE(dev), 0);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), bi->ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), bi->ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1039);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0180);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0106);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }
    s->sis_genctl = GENCTL_IOMAPPED_SCR;
    s->sis_pmr = 0;
    s->port2_start = 64;
    memset(s->scr, 0, sizeof(s->scr));
    for (int i = 0; i < 2; i++) {
        s->bmdma[i].cmd = 0;
        s->bmdma[i].status = 0;
        s->bmdma[i].prdt = 0;
    }

    /* Setup BARs: 0-primary cmd, 2-secondary cmd, 4-BMDMA, 5-SCR */
    s->num_bars = 4;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 8, .name = "cmd0", .ops = &pcibase_cmd_io_ops };
    s->bar_info[1] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 8, .name = "cmd1", .ops = &pcibase_cmd_io_ops };
    s->bar_info[2] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 0x10, .name = "bmdma", .ops = &pcibase_bmdma_io_ops };
    s->bar_info[3] = (BARInfo){ .index = 5, .type = BAR_TYPE_MMIO, .size = 256, .name = "scr", .ops = &pcibase_scr_mmio_ops };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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
    .name = "sata_sis_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
