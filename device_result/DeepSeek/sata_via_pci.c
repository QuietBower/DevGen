/*
 * QEMU VIA SATA controller device model for sata_via.ko
 * QEMU 8.2.10 compatible.
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define PCI_VENDOR_ID_VIA          0x1106
#define PCI_DEVICE_ID_VIA_5337     0x5337
#define PCI_CLASS_STORAGE_IDE      0x0101
#define PCI_PROGIF_NATIVE_MODE     0x8f

#define TYPE_PCIBASE_DEVICE "sata_via_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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
} BARInfo;

typedef struct BAROpaque {
    PCIBaseState *pci;
    int bar;
} BAROpaque;

#define MAX_PORTS 2

typedef struct {
    uint8_t data;
    uint8_t feat_err;
    uint8_t nsector;
    uint8_t sector;
    uint8_t lbal;
    uint8_t lbam;
    uint8_t lbah;
    uint8_t status_cmd;
} ATAChannelRegs;

typedef struct {
    uint8_t dev_ctrl;
} ATACtrlRegs;

typedef struct {
    uint32_t scr_status;
    uint32_t scr_error;
    uint32_t scr_control;
} SCRRegs;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    BAROpaque *bar_opaques[6];

    ATAChannelRegs cmd_regs[MAX_PORTS];
    ATACtrlRegs ctrl_regs[MAX_PORTS];
    uint8_t bmdma_cmd[MAX_PORTS];
    uint8_t bmdma_status[MAX_PORTS];
    uint32_t bmdma_prdt[MAX_PORTS];
    SCRRegs scr;

    bool has_msi;
    bool has_msix;
    uint32_t irq_status;
    uint8_t pm_state;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = false;

    /* Combine interrupt sources: any BMDMA interrupt pending */
    for (int i = 0; i < MAX_PORTS; i++) {
        if (s->bmdma_status[i] & 0x04) { /* bit 2: INTR */
            pending = true;
            break;
        }
    }
    /* Add other interrupt sources if needed */

    pci_set_irq(pdev, pending ? 1 : 0);
}

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA not implemented: no driver-initiated DMA during probe */
    PCIDevice *pdev = PCI_DEVICE(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case 0x00: /* SCR_STATUS */
        val = s->scr.scr_status;
        break;
    case 0x04: /* SCR_ERROR */
        val = s->scr.scr_error;
        break;
    case 0x08: /* SCR_CONTROL */
        val = s->scr.scr_control;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case 0x00:
        s->scr.scr_status = val;
        break;
    case 0x04:
        s->scr.scr_error = val;
        break;
    case 0x08:
        s->scr.scr_control = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    BAROpaque *o = opaque;
    PCIBaseState *s = o->pci;
    int bar = o->bar;
    uint64_t val = 0;
    int port;

    if (size != 1 && size != 4) {
        return 0;
    }

    switch (bar) {
    case 0: /* Primary command block */
        port = 0;
        if (addr >= 8) return 0;
        if (size == 1) {
            switch (addr) {
            case 0: val = s->cmd_regs[port].data; break;
            case 1: val = s->cmd_regs[port].feat_err; break;
            case 2: val = s->cmd_regs[port].nsector; break;
            case 3: val = s->cmd_regs[port].sector; break;
            case 4: val = s->cmd_regs[port].lbal; break;
            case 5: val = s->cmd_regs[port].lbam; break;
            case 6: val = s->cmd_regs[port].lbah; break;
            case 7: val = s->cmd_regs[port].status_cmd; break;
            }
        } else if (size == 4) {
            /* 32-bit reads not typically used on these ports, but return 0 */
            val = 0;
        }
        break;
    case 1: /* Primary control block */
        port = 0;
        if (addr >= 4) return 0;
        if (size == 1 && addr == 2) {
            val = s->cmd_regs[port].status_cmd; /* alt status = 0x50 */
        }
        break;
    case 2: /* Secondary command block */
        port = 1;
        if (addr >= 8) return 0;
        if (size == 1) {
            switch (addr) {
            case 0: val = s->cmd_regs[port].data; break;
            case 1: val = s->cmd_regs[port].feat_err; break;
            case 2: val = s->cmd_regs[port].nsector; break;
            case 3: val = s->cmd_regs[port].sector; break;
            case 4: val = s->cmd_regs[port].lbal; break;
            case 5: val = s->cmd_regs[port].lbam; break;
            case 6: val = s->cmd_regs[port].lbah; break;
            case 7: val = s->cmd_regs[port].status_cmd; break;
            }
        }
        break;
    case 3: /* Secondary control block */
        port = 1;
        if (addr >= 4) return 0;
        if (size == 1 && addr == 2) {
            val = s->cmd_regs[port].status_cmd;
        }
        break;
    case 4: /* BMDMA */
        port = (addr >= 8) ? 1 : 0;
        addr &= 0x7;
        if (size == 1) {
            switch (addr) {
            case 0: val = s->bmdma_cmd[port]; break;
            case 2: val = s->bmdma_status[port]; break;
            default: val = 0; break;
            }
        } else if (size == 4) {
            switch (addr) {
            case 4: val = s->bmdma_prdt[port]; break;
            default: val = 0; break;
            }
        }
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    BAROpaque *o = opaque;
    PCIBaseState *s = o->pci;
    int bar = o->bar;
    int port;

    if (size != 1 && size != 4) {
        return;
    }

    switch (bar) {
    case 0:
        port = 0;
        if (addr >= 8) return;
        if (size == 1) {
            switch (addr) {
            case 0: s->cmd_regs[port].data = val; break;
            case 1: s->cmd_regs[port].feat_err = val; break;
            case 2: s->cmd_regs[port].nsector = val; break;
            case 3: s->cmd_regs[port].sector = val; break;
            case 4: s->cmd_regs[port].lbal = val; break;
            case 5: s->cmd_regs[port].lbam = val; break;
            case 6: s->cmd_regs[port].lbah = val; break;
            case 7: s->cmd_regs[port].status_cmd = val; break;
            }
        }
        break;
    case 1:
        port = 0;
        if (addr >= 4) return;
        if (size == 1 && addr == 2) {
            s->ctrl_regs[port].dev_ctrl = val;
        }
        break;
    case 2:
        port = 1;
        if (addr >= 8) return;
        if (size == 1) {
            switch (addr) {
            case 0: s->cmd_regs[port].data = val; break;
            case 1: s->cmd_regs[port].feat_err = val; break;
            case 2: s->cmd_regs[port].nsector = val; break;
            case 3: s->cmd_regs[port].sector = val; break;
            case 4: s->cmd_regs[port].lbal = val; break;
            case 5: s->cmd_regs[port].lbam = val; break;
            case 6: s->cmd_regs[port].lbah = val; break;
            case 7: s->cmd_regs[port].status_cmd = val; break;
            }
        }
        break;
    case 3:
        port = 1;
        if (addr >= 4) return;
        if (size == 1 && addr == 2) {
            s->ctrl_regs[port].dev_ctrl = val;
        }
        break;
    case 4:
        port = (addr >= 8) ? 1 : 0;
        addr &= 0x7;
        if (size == 1) {
            switch (addr) {
            case 0:
                s->bmdma_cmd[port] = val;
                if (val & 0x01) { /* Start/Stop DMA */
                    /* DMA start; we ignore for probe */
                }
                break;
            case 2:
                /* BMDMA status: write-1-to-clear */
                s->bmdma_status[port] &= ~(val & 0x06); /* bits 1 (ERR) and 2 (INTR) */
                pcibase_update_irq(s);
                break;
            default:
                break;
            }
        } else if (size == 4) {
            switch (addr) {
            case 4:
                s->bmdma_prdt[port] = val;
                break;
            default:
                break;
            }
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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

    /* Initialize ATA registers to power-on defaults */
    for (int i = 0; i < MAX_PORTS; i++) {
        s->cmd_regs[i].data = 0;
        s->cmd_regs[i].feat_err = 0;
        s->cmd_regs[i].nsector = 1;
        s->cmd_regs[i].sector = 1;
        s->cmd_regs[i].lbal = 0;
        s->cmd_regs[i].lbam = 0;
        s->cmd_regs[i].lbah = 0;
        s->cmd_regs[i].status_cmd = 0x50; /* DRDY | DSC */
        s->ctrl_regs[i].dev_ctrl = 0;
        s->bmdma_cmd[i] = 0;
        s->bmdma_status[i] = 0;
        s->bmdma_prdt[i] = 0;
    }

    /* SCR defaults: indicate no device on init, phy offline */
    s->scr.scr_status = 0x00000001; /* DET=1 (no device), SPD=0, IPM=0 */
    s->scr.scr_error = 0;
    s->scr.scr_control = 0x00000300; /* enable phy? */

    s->irq_status = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    BAROpaque *opaque = s->bar_opaques[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, opaque, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, opaque, bi->name, aligned_size);
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

    /* Set PCI IDs */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_VIA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_VIA_5337);
    /* Set class to IDE and programming interface to native mode */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, PCI_PROGIF_NATIVE_MODE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Do not use MSI/MSI-X; legacy IRQ only */
    s->has_msi = false;
    s->has_msix = false;

    /* Static BAR configuration (svia_bar_sizes) */
    {
        unsigned int svia_bar_sizes[] = {8, 4, 8, 4, 16, 256};
        s->num_bars = 6;
        for (int i = 0; i < s->num_bars; i++) {
            s->bar_info[i].index = i;
            s->bar_info[i].size = svia_bar_sizes[i];
            if (i == 5) {
                s->bar_info[i].type = BAR_TYPE_MMIO;
                s->bar_info[i].name = "bar5_mmio";
            } else {
                s->bar_info[i].type = BAR_TYPE_PIO;
                s->bar_info[i].name = "bar_pio";
            }

            /* Create per-BAR opaque */
            s->bar_opaques[i] = g_new0(BAROpaque, 1);
            s->bar_opaques[i]->pci = s;
            s->bar_opaques[i]->bar = i;
        }
    }

    /* Register all BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (*errp) {
            return;
        }
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    for (int i = 0; i < s->num_bars; i++) {
        g_free(s->bar_opaques[i]);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "sata_via_pci",
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