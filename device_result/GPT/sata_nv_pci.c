/*
 * QEMU NVIDIA SATA Controller Emulation (sata_nv.c)
 *
 * Copyright (c) 2025 QEMU Team
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "sata_nv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets and constants from sata_nv.c */
#define VENDOR_ID 0x10de
#define DEVICE_ID 0x008e
#define CLASS_ID PCI_CLASS_STORAGE_SATA

/* BAR definitions */
#define NV_MMIO_BAR 5

/* Interrupt register offsets */
#define NV_INT_STATUS_CK804     0x00
#define NV_INT_ENABLE_CK804     0x01
#define NV_INT_STATUS_MCP55     0x08
#define NV_INT_ENABLE_MCP55     0x0c
#define NV_CTL_MCP55            0x1c

/* ADMA register offsets */
#define NV_ADMA_PORT            0x40
#define NV_ADMA_PORT_SIZE       0x40
#define NV_ADMA_GEN             0xc0
#define NV_ADMA_NOTIFIER_CLEAR  0x10

#define NV_ADMA_STAT            0x00
#define NV_ADMA_CTL             0x02
#define NV_ADMA_CPB_BASE_LOW    0x04
#define NV_ADMA_CPB_BASE_HIGH   0x08
#define NV_ADMA_CPB_COUNT       0x0c
#define NV_ADMA_APPEND          0x0e
#define NV_ADMA_NEXT_CPB_IDX    0x0f
#define NV_ADMA_NOTIFIER        0x10
#define NV_ADMA_NOTIFIER_ERROR  0x14

#define NV_ADMA_GEN_CTL         0x00

/* Interrupt bits */
#define NV_INT_DEV              0x01
#define NV_INT_ADDED            0x02
#define NV_INT_REMOVED          0x04
#define NV_INT_ALL              0x07
#define NV_INT_MASK             0x07
#define NV_INT_PORT_SHIFT       4

#define NV_INT_ALL_MCP55        0x00fd
#define NV_INT_MASK_MCP55       0x00fd
#define NV_INT_PORT_SHIFT_MCP55 16

/* ADMA control/status bits */
#define NV_ADMA_CTL_GO                  0x0001
#define NV_ADMA_CTL_AIEN                0x0002
#define NV_ADMA_CTL_HOTPLUG_IEN         0x0004
#define NV_ADMA_CTL_CHANNEL_RESET       0x0008

#define NV_ADMA_STAT_IDLE               0x0001
#define NV_ADMA_STAT_LEGACY             0x0002
#define NV_ADMA_STAT_DONE               0x0004
#define NV_ADMA_STAT_CPBERR             0x0008
#define NV_ADMA_STAT_CMD_COMPLETE       0x0010
#define NV_ADMA_STAT_HOTPLUG            0x0020
#define NV_ADMA_STAT_HOTUNPLUG          0x0040
#define NV_ADMA_STAT_TIMEOUT            0x0080
#define NV_ADMA_STAT_SERROR             0x0100

#define NV_ADMA_CHECK_INTR(gen_ctl, port) \
    (((gen_ctl) >> ((port) * 2)) & 0x3)

/* MCP SATA config register */
#define NV_MCP_SATA_CFG_20              0x50
#define NV_MCP_SATA_CFG_20_SATA_SPACE_EN 0x01
#define NV_MCP_SATA_CFG_20_PORT0_EN      0x02
#define NV_MCP_SATA_CFG_20_PORT0_PWB_EN  0x04
#define NV_MCP_SATA_CFG_20_PORT1_EN      0x08
#define NV_MCP_SATA_CFG_20_PORT1_PWB_EN  0x10

/* SWNCQ control bits */
#define NV_CTL_PRI_SWNCQ                0x00010000
#define NV_CTL_SEC_SWNCQ                0x00020000

/* SWNCQ interrupt bits */
#define NV_SWNCQ_IRQ_DHREGFIS           0x0001
#define NV_SWNCQ_IRQ_DMASETUP           0x0002
#define NV_SWNCQ_IRQ_SDBFIS             0x0004
#define NV_SWNCQ_IRQ_BACKOUT            0x0008
#define NV_SWNCQ_IRQ_HOTPLUG            0x0010
#define NV_SWNCQ_IRQ_ADDED              0x0020
#define NV_SWNCQ_IRQ_REMOVED            0x0040

/* Port SCR register offsets */
#define NV_PORT0_SCR_REG_OFFSET         0x100
#define NV_PORT1_SCR_REG_OFFSET         0x180

/* Device types */
enum {
    GENERIC,
    NF2,
    CK804,
    ADMA,
    SWNCQ,
    MCP5x
};

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* MMIO backing store */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* Device type */
    uint32_t device_type;
    
    /* ADMA state */
    uint16_t adma_stat[2];
    uint16_t adma_ctl[2];
    uint32_t adma_notifier[2];
    uint32_t adma_notifier_error[2];
    uint32_t adma_gen_ctl;
    
    /* Interrupt status registers */
    uint8_t int_status_ck804;
    uint8_t int_enable_ck804;
    uint32_t int_status_mcp55;
    uint32_t int_enable_mcp55;
    
    /* MCP SATA config */
    uint32_t mcp_sata_cfg_20;
    
    /* SWNCQ state */
    uint32_t ctl_mcp55;
};


/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                 */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case NV_INT_STATUS_CK804:
        val = s->int_status_ck804;
        break;
    case NV_INT_ENABLE_CK804:
        val = s->int_enable_ck804;
        break;
    case NV_INT_STATUS_MCP55:
        val = s->int_status_mcp55;
        break;
    case NV_INT_ENABLE_MCP55:
        val = s->int_enable_mcp55;
        break;
    case NV_CTL_MCP55:
        val = s->ctl_mcp55;
        break;
        
    /* ADMA registers for port 0 */
    case NV_ADMA_PORT + 0x00 ... NV_ADMA_PORT + 0x3f:
        if (addr - NV_ADMA_PORT == NV_ADMA_STAT) {
            val = s->adma_stat[0];
        } else if (addr - NV_ADMA_PORT == NV_ADMA_CTL) {
            val = s->adma_ctl[0];
        } else if (addr - NV_ADMA_PORT == NV_ADMA_NOTIFIER) {
            val = s->adma_notifier[0];
        } else if (addr - NV_ADMA_PORT == NV_ADMA_NOTIFIER_ERROR) {
            val = s->adma_notifier_error[0];
        }
        break;
        
    /* ADMA registers for port 1 */
    case NV_ADMA_PORT + 0x40 ... NV_ADMA_PORT + 0x7f:
        if (addr - (NV_ADMA_PORT + 0x40) == NV_ADMA_STAT) {
            val = s->adma_stat[1];
        } else if (addr - (NV_ADMA_PORT + 0x40) == NV_ADMA_CTL) {
            val = s->adma_ctl[1];
        } else if (addr - (NV_ADMA_PORT + 0x40) == NV_ADMA_NOTIFIER) {
            val = s->adma_notifier[1];
        } else if (addr - (NV_ADMA_PORT + 0x40) == NV_ADMA_NOTIFIER_ERROR) {
            val = s->adma_notifier_error[1];
        }
        break;
        
    /* ADMA general registers */
    case NV_ADMA_GEN + 0x00 ... NV_ADMA_GEN + 0x0f:
        if (addr - NV_ADMA_GEN == NV_ADMA_GEN_CTL) {
            val = s->adma_gen_ctl;
        }
        break;
        
    /* SCR registers for port 0 */
    case NV_PORT0_SCR_REG_OFFSET ... NV_PORT0_SCR_REG_OFFSET + 0x1f:
        /* Return dummy values for SCR registers */
        val = 0;
        if ((addr - NV_PORT0_SCR_REG_OFFSET) % 4 == 0) {
            /* SCR_STATUS - indicate device detection */
            if ((addr - NV_PORT0_SCR_REG_OFFSET) == 0x4) {
                val = 0x3; /* Device detected */
            }
        }
        break;
        
    /* SCR registers for port 1 */
    case NV_PORT1_SCR_REG_OFFSET ... NV_PORT1_SCR_REG_OFFSET + 0x1f:
        /* Return dummy values for SCR registers */
        val = 0;
        if ((addr - NV_PORT1_SCR_REG_OFFSET) % 4 == 0) {
            /* SCR_STATUS - indicate device detection */
            if ((addr - NV_PORT1_SCR_REG_OFFSET) == 0x4) {
                val = 0x3; /* Device detected */
            }
        }
        break;
        
    default:
        qemu_log_mask(LOG_UNIMP, "[sata_nv_pci] unhandled mmio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case NV_INT_STATUS_CK804:
        s->int_status_ck804 = val;
        break;
    case NV_INT_ENABLE_CK804:
        s->int_enable_ck804 = val;
        break;
    case NV_INT_STATUS_MCP55:
        s->int_status_mcp55 = val;
        break;
    case NV_INT_ENABLE_MCP55:
        s->int_enable_mcp55 = val;
        break;
    case NV_CTL_MCP55:
        s->ctl_mcp55 = val;
        break;
        
    /* ADMA registers for port 0 */
    case NV_ADMA_PORT + 0x00 ... NV_ADMA_PORT + 0x3f:
        if (addr - NV_ADMA_PORT == NV_ADMA_STAT) {
            s->adma_stat[0] = val;
        } else if (addr - NV_ADMA_PORT == NV_ADMA_CTL) {
            s->adma_ctl[0] = val;
        } else if (addr - NV_ADMA_PORT == NV_ADMA_NOTIFIER) {
            s->adma_notifier[0] = val;
        } else if (addr - NV_ADMA_PORT == NV_ADMA_NOTIFIER_ERROR) {
            s->adma_notifier_error[0] = val;
        } else if (addr - NV_ADMA_PORT == NV_ADMA_CPB_COUNT) {
            /* Clear CPB count */
        }
        break;
        
    /* ADMA registers for port 1 */
    case NV_ADMA_PORT + 0x40 ... NV_ADMA_PORT + 0x7f:
        if (addr - (NV_ADMA_PORT + 0x40) == NV_ADMA_STAT) {
            s->adma_stat[1] = val;
        } else if (addr - (NV_ADMA_PORT + 0x40) == NV_ADMA_CTL) {
            s->adma_ctl[1] = val;
        } else if (addr - (NV_ADMA_PORT + 0x40) == NV_ADMA_NOTIFIER) {
            s->adma_notifier[1] = val;
        } else if (addr - (NV_ADMA_PORT + 0x40) == NV_ADMA_NOTIFIER_ERROR) {
            s->adma_notifier_error[1] = val;
        } else if (addr - (NV_ADMA_PORT + 0x40) == NV_ADMA_CPB_COUNT) {
            /* Clear CPB count */
        }
        break;
        
    /* ADMA general registers */
    case NV_ADMA_GEN + 0x00 ... NV_ADMA_GEN + 0x0f:
        if (addr - NV_ADMA_GEN == NV_ADMA_GEN_CTL) {
            s->adma_gen_ctl = val;
        }
        break;
        
    /* Notifier clear registers */
    case NV_ADMA_GEN + NV_ADMA_NOTIFIER_CLEAR:
    case NV_ADMA_GEN + NV_ADMA_NOTIFIER_CLEAR + 4:
        /* Clear notifier bits */
        break;
        
    default:
        qemu_log_mask(LOG_UNIMP, "[sata_nv_pci] unhandled mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[sata_nv_pci] pio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[sata_nv_pci] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
        
    /* Reset ADMA state */
    s->adma_stat[0] = NV_ADMA_STAT_IDLE | NV_ADMA_STAT_LEGACY;
    s->adma_stat[1] = NV_ADMA_STAT_IDLE | NV_ADMA_STAT_LEGACY;
    s->adma_ctl[0] = NV_ADMA_CTL_AIEN | NV_ADMA_CTL_HOTPLUG_IEN;
    s->adma_ctl[1] = NV_ADMA_CTL_AIEN | NV_ADMA_CTL_HOTPLUG_IEN;
    s->adma_notifier[0] = 0;
    s->adma_notifier[1] = 0;
    s->adma_notifier_error[0] = 0;
    s->adma_notifier_error[1] = 0;
    s->adma_gen_ctl = 0;
    
    /* Reset interrupt registers */
    s->int_status_ck804 = 0;
    s->int_enable_ck804 = 0;
    s->int_status_mcp55 = 0;
    s->int_enable_mcp55 = 0;
    
    /* Reset MCP SATA config */
    s->mcp_sata_cfg_20 = NV_MCP_SATA_CFG_20_SATA_SPACE_EN |
                         NV_MCP_SATA_CFG_20_PORT0_EN |
                         NV_MCP_SATA_CFG_20_PORT0_PWB_EN |
                         NV_MCP_SATA_CFG_20_PORT1_EN |
                         NV_MCP_SATA_CFG_20_PORT1_PWB_EN;
    
    /* Reset SWNCQ control */
    s->ctl_mcp55 = NV_CTL_PRI_SWNCQ | NV_CTL_SEC_SWNCQ;
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    /* optional: override certain config offsets if needed */

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    
    if (addr == NV_MCP_SATA_CFG_20 && len == 4) {
        s->mcp_sata_cfg_20 = val;
    }
    
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
    
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* Set up BARs */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 8;
    s->bar_info[0].name = "sata_nv-pio0";
    
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = 4;
    s->bar_info[1].name = "sata_nv-pio1";
    
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 8;
    s->bar_info[2].name = "sata_nv-pio2";
    
    s->bar_info[3].index = 3;
    s->bar_info[3].type = BAR_TYPE_PIO;
    s->bar_info[3].size = 4;
    s->bar_info[3].name = "sata_nv-pio3";
    
    s->bar_info[4].index = 4;
    s->bar_info[4].type = BAR_TYPE_PIO;
    s->bar_info[4].size = 16;
    s->bar_info[4].name = "sata_nv-pio4";
    
    s->bar_info[5].index = 5;
    s->bar_info[5].type = BAR_TYPE_MMIO;
    s->bar_info[5].size = 0x200;
    s->bar_info[5].name = "sata_nv-mmio";
    
    s->num_bars = 6;
    
    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config */
    s->has_msi = true;
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = false;
    }

    /* DMA init */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    /* Initialize device state */
    s->device_type = ADMA; /* Default to ADMA mode */
    pcibase_reset(DEVICE(pdev));
    
    qemu_log_mask(LOG_UNIMP, "[sata_nv_pci] device realized\n");
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev,NULL,0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    /* free mmio_backing or other allocations */
    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[sata_nv_pci] device uninit\n");
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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
