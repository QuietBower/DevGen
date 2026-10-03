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

#define TYPE_PCIBASE_DEVICE "pata_oldpiix_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x1230
#define CLASS_ID 0x0000

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
    /* Timing registers in PCI config space (0x40-0x47) */
    uint8_t timing_regs[8];

    /* Primary command block registers (BAR0) */
    uint8_t pri_cmd[8];
    /* Primary control block registers (BAR1) */
    uint8_t pri_ctl[4];
    /* Secondary command block registers (BAR2) */
    uint8_t sec_cmd[8];
    /* Secondary control block registers (BAR3) */
    uint8_t sec_ctl[4];

    /* BMDMA registers (BAR4) */
    uint8_t bm_command[2];  /* per channel: 0=primary, 1=secondary */
    uint8_t bm_status[2];
    uint32_t bm_prdt[2];

    /* DMA Context */

    /* Operational status flags */

    /* State used to handle reset sequences */

    /* Power management state (D0-D3) */

};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* ------------------------------------------------------------------------
 * PCI Configuration Space Handlers (for IDE timing registers)
 * ------------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;

    /* If the access falls entirely within the device-specific timing regs */
    if (address >= 0x40 && address + len <= 0x48) {
        val = 0;
        memcpy(&val, &s->timing_regs[address - 0x40], len);
        return val;
    }
    return pci_default_read_config(pdev, address, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (address >= 0x40 && address + len <= 0x48) {
        memcpy(&s->timing_regs[address - 0x40], &val, len);
        return;
    }
    pci_default_write_config(pdev, address, val, len);
}

/* ------------------------------------------------------------------------
 * PIO Handlers for the three BAR types
 * ------------------------------------------------------------------------ */

/* Command Block (8 bytes) – BAR0 and BAR2 */
static uint64_t pcibase_cmd_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint8_t *regs = opaque;
    uint64_t val = 0;

    if (addr + size <= 8) {
        memcpy(&val, &regs[addr], size);
    }
    return val;
}

static void pcibase_cmd_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    uint8_t *regs = opaque;

    if (addr + size <= 8) {
        memcpy(&regs[addr], &val, size);
    }
}

static const MemoryRegionOps pcibase_cmd_pio_ops = {
    .read = pcibase_cmd_pio_read,
    .write = pcibase_cmd_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Control Block (4 bytes) – BAR1 and BAR3 */
static uint64_t pcibase_ctl_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint8_t *regs = opaque;
    uint64_t val = 0;

    if (addr + size <= 4) {
        memcpy(&val, &regs[addr], size);
    }
    return val;
}

static void pcibase_ctl_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    uint8_t *regs = opaque;

    if (addr + size <= 4) {
        memcpy(&regs[addr], &val, size);
    }
}

static const MemoryRegionOps pcibase_ctl_pio_ops = {
    .read = pcibase_ctl_pio_read,
    .write = pcibase_ctl_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* BMDMA Block (16 bytes) – BAR4 */
static uint64_t pcibase_bm_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Minimally emulate the two channels */
    if (addr < 8) {
        /* Primary channel (offset 0..7) */
        if (addr == 0 && size == 1) {
            val = s->bm_command[0];
        } else if (addr == 2 && size == 1) {
            val = s->bm_status[0];
        } else if (addr == 4 && size == 4) {
            val = s->bm_prdt[0];
        }
    } else {
        /* Secondary channel (offset 8..15) */
        if (addr == 8 && size == 1) {
            val = s->bm_command[1];
        } else if (addr == 10 && size == 1) {
            val = s->bm_status[1];
        } else if (addr == 12 && size == 4) {
            val = s->bm_prdt[1];
        }
    }
    return val;
}

static void pcibase_bm_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 8) {
        if (addr == 0 && size == 1) {
            s->bm_command[0] = val;
        } else if (addr == 2 && size == 1) {
            s->bm_status[0] = val;
        } else if (addr == 4 && size == 4) {
            s->bm_prdt[0] = val;
        }
    } else {
        if (addr == 8 && size == 1) {
            s->bm_command[1] = val;
        } else if (addr == 10 && size == 1) {
            s->bm_status[1] = val;
        } else if (addr == 12 && size == 4) {
            s->bm_prdt[1] = val;
        }
    }
}

static const MemoryRegionOps pcibase_bm_pio_ops = {
    .read = pcibase_bm_pio_read,
    .write = pcibase_bm_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* MMIO Handlers (not used) */
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

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Original PIO ops (not used directly by BARs any more, but kept for safety) */
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

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------------
 * Reset and Initialization
 * ------------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    int i;
    uint32_t bar_addrs[5] = { 0xC001, 0xC009, 0xC011, 0xC019, 0xC021 };

    pci_device_reset(pdev);

    /* Set port enable bits in timing registers (PIIX specific) */
    s->timing_regs[1] = 0x80;  /* offset 0x41: primary enable */
    s->timing_regs[3] = 0x80;  /* offset 0x43: secondary enable */

    /* Clear other state */
    memset(s->pri_cmd, 0, sizeof(s->pri_cmd));
    memset(s->pri_ctl, 0, sizeof(s->pri_ctl));
    memset(s->sec_cmd, 0, sizeof(s->sec_cmd));
    memset(s->sec_ctl, 0, sizeof(s->sec_ctl));
    memset(s->bm_command, 0, sizeof(s->bm_command));
    memset(s->bm_status, 0, sizeof(s->bm_status));
    memset(s->bm_prdt, 0, sizeof(s->bm_prdt));

    /* Restore fixed BAR addresses to prevent 'not assigned' error */
    for (i = 0; i < 5; i++) {
        pci_default_write_config(pdev, PCI_BASE_ADDRESS_0 + i * 4,
                                 bar_addrs[i], 4);
    }
}

/* ------------------------------------------------------------------------
 * BAR Registration (modified to accept opaque and ops per BAR)
 * ------------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi,
                                  MemoryRegionOps *ops, void *opaque, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, opaque, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, opaque, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------------
 * PCI Realize
 * ------------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int i;
    uint32_t bar_addrs[5] = { 0xC001, 0xC009, 0xC011, 0xC019, 0xC021 };

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE); /* override template's CLASS_ID */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize timing registers with port enable bits */
    s->timing_regs[1] = 0x80;
    s->timing_regs[3] = 0x80;

    /* Define BARs */
    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 8, .name = "pri-cmd" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 4, .name = "pri-ctl" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 8, .name = "sec-cmd" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 4, .name = "sec-ctl" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 16, .name = "bmdma" };

    /* Register each BAR with the appropriate ops and opaque */
    void *opaque_cmd[] = { s->pri_cmd, s->sec_cmd };
    void *opaque_ctl[] = { s->pri_ctl, s->sec_ctl };
    for (int i = 0; i < s->num_bars; i++) {
        MemoryRegionOps *ops;
        void *opaque;

        switch (i) {
        case 0: /* Primary command block */
            ops = (MemoryRegionOps *)&pcibase_cmd_pio_ops;
            opaque = s->pri_cmd;
            break;
        case 1: /* Primary control block */
            ops = (MemoryRegionOps *)&pcibase_ctl_pio_ops;
            opaque = s->pri_ctl;
            break;
        case 2: /* Secondary command block */
            ops = (MemoryRegionOps *)&pcibase_cmd_pio_ops;
            opaque = s->sec_cmd;
            break;
        case 3: /* Secondary control block */
            ops = (MemoryRegionOps *)&pcibase_ctl_pio_ops;
            opaque = s->sec_ctl;
            break;
        case 4: /* BMDMA */
            ops = (MemoryRegionOps *)&pcibase_bm_pio_ops;
            opaque = s;
            break;
        default:
            ops = NULL;
            opaque = NULL;
            break;
        }
        pcibase_register_bar(pdev, s, &s->bar_info[i], ops, opaque, errp);
    }

    /* Set default IO addresses using proper config write to update BAR mapping */
    for (i = 0; i < 5; i++) {
        pci_default_write_config(pdev, PCI_BASE_ADDRESS_0 + i * 4,
                                 bar_addrs[i], 4);
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
    .name = "pata_oldpiix_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(timing_regs, PCIBaseState, 8),
        VMSTATE_UINT8_ARRAY(pri_cmd, PCIBaseState, 8),
        VMSTATE_UINT8_ARRAY(pri_ctl, PCIBaseState, 4),
        VMSTATE_UINT8_ARRAY(sec_cmd, PCIBaseState, 8),
        VMSTATE_UINT8_ARRAY(sec_ctl, PCIBaseState, 4),
        VMSTATE_UINT8_ARRAY(bm_command, PCIBaseState, 2),
        VMSTATE_UINT8_ARRAY(bm_status, PCIBaseState, 2),
        VMSTATE_UINT32_ARRAY(bm_prdt, PCIBaseState, 2),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->config_read = pcibase_config_read;
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