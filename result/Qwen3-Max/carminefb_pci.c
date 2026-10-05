/*
 * QEMU CarmineFB PCI device emulation
 *
 * Copyright (c) 2025 QEMU contributors
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

#define TYPE_PCIBASE_DEVICE "carminefb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define PCI_VENDOR_ID_FUJITU_LIMITED 0x10cf
#define CARMINE_DEVICE_ID 0x202b
#define CARMINE_CLASS_ID PCI_CLASS_DISPLAY_OTHER

/* Register offsets */
#define CARMINE_DISP_REG_H_TOTAL		(0x0004)
#define CARMINE_DISP_REG_H_PERIOD		(0x0008)
#define CARMINE_DISP_REG_V_H_W_H_POS		(0x000C)
#define CARMINE_DISP_REG_V_TOTAL		(0x0010)
#define CARMINE_DISP_REG_V_PERIOD_POS		(0x0014)
#define CARMINE_DISP_REG_L0_MODE_W_H		(0x0020)
#define CARMINE_DISP_REG_L0_DISP_ADR		(0x0028)
#define CARMINE_DISP_REG_L0_DISP_POS		(0x002C)
#define CARMINE_DISP_REG_L1_WIDTH		(0x0030)
#define CARMINE_DISP_REG_L1_ORG_ADR		(0x0034)
#define CARMINE_DISP_REG_L2_MODE_W_H		(0x0040)
#define CARMINE_DISP_REG_L2_ORG_ADR1		(0x0044)
#define CARMINE_DISP_REG_L2_DISP_POS		(0x0054)
#define CARMINE_DISP_REG_L2_DISP_ADR1		(0x0048)
#define CARMINE_DISP_REG_L3_MODE_W_H		(0x0058)
#define CARMINE_DISP_REG_L3_ORG_ADR1		(0x005C)
#define CARMINE_DISP_REG_L3_DISP_ADR1		(0x0060)
#define CARMINE_DISP_REG_L3_DISP_POS		(0x006C)
#define CARMINE_DISP_REG_L4_MODE_W_H		(0x0070)
#define CARMINE_DISP_REG_L4_ORG_ADR1		(0x0074)
#define CARMINE_DISP_REG_L4_DISP_ADR1		(0x0078)
#define CARMINE_DISP_REG_L4_DISP_POS		(0x0084)
#define CARMINE_DISP_REG_L5_MODE_W_H		(0x0088)
#define CARMINE_DISP_REG_L5_ORG_ADR1		(0x008C)
#define CARMINE_DISP_REG_L5_DISP_ADR1		(0x0090)
#define CARMINE_DISP_REG_L5_DISP_POS		(0x009C)
#define CARMINE_DISP_REG_CUR1_POS		(0x00A8)
#define CARMINE_DISP_REG_CURSOR_MODE		(0x00A0)
#define CARMINE_DISP_REG_CUR2_POS		(0x00B0)
#define CARMINE_DISP_REG_BLEND_MODE_L0		(0x00B4)
#define CARMINE_DISP_REG_C_TRANS		(0x00BC)
#define CARMINE_DISP_REG_MLMR_TRANS		(0x00C0)
#define CARMINE_DISP_REG_L0_WIN_POS		(0x0114)
#define CARMINE_DISP_REG_L0_WIN_SIZE		(0x0118)
#define CARMINE_DISP_REG_L0_EXT_MODE		(0x0110)
#define CARMINE_DISP_REG_L1_WIN_POS		(0x0124)
#define CARMINE_DISP_REG_L1_WIN_SIZE		(0x0128)
#define CARMINE_DISP_REG_L1_EXT_MODE		(0x0120)
#define CARMINE_DISP_REG_L2_WIN_POS		(0x0134)
#define CARMINE_DISP_REG_L2_WIN_SIZE		(0x0138)
#define CARMINE_DISP_REG_L2_EXT_MODE		(0x0130)
#define CARMINE_DISP_REG_L3_WIN_POS		(0x0144)
#define CARMINE_DISP_REG_L3_WIN_SIZE		(0x0148)
#define CARMINE_DISP_REG_L3_EXT_MODE		(0x0140)
#define CARMINE_DISP_REG_L4_WIN_POS		(0x0154)
#define CARMINE_DISP_REG_L4_WIN_SIZE		(0x0158)
#define CARMINE_DISP_REG_L4_EXT_MODE		(0x0150)
#define CARMINE_DISP_REG_L5_WIN_POS		(0x0164)
#define CARMINE_DISP_REG_L5_WIN_SIZE		(0x0168)
#define CARMINE_DISP_REG_L5_EXT_MODE		(0x0160)
#define CARMINE_DISP_REG_L0_TRANS		(0x01A0)
#define CARMINE_DISP_REG_L1_TRANS		(0x01A4)
#define CARMINE_DISP_REG_L2_TRANS		(0x01A8)
#define CARMINE_DISP_REG_L3_TRANS		(0x01AC)
#define CARMINE_DISP_REG_L4_TRANS		(0x01B0)
#define CARMINE_DISP_REG_L5_TRANS		(0x01B4)
#define CARMINE_DISP_REG_BLEND_MODE_L1		(0x0188)
#define CARMINE_DISP_REG_BLEND_MODE_L2		(0x018C)
#define CARMINE_DISP_REG_BLEND_MODE_L3		(0x0190)
#define CARMINE_DISP_REG_BLEND_MODE_L4		(0x0194)
#define CARMINE_DISP_REG_BLEND_MODE_L5		(0x0198)
#define CARMINE_DISP_REG_DCM1			(0x0100)

/* Control register offsets */
#define CARMINE_GRAPH_REG_DC_OFFSET_LX	(0x00040064)
#define CARMINE_GRAPH_REG_DC_OFFSET_LY	(0x00040068)
#define CARMINE_GRAPH_REG_DC_OFFSET_PX	(0x0004005C)
#define CARMINE_GRAPH_REG_DC_OFFSET_PY	(0x00040060)
#define CARMINE_GRAPH_REG_DC_OFFSET_TX	(0x0004006C)
#define CARMINE_GRAPH_REG_DC_OFFSET_TY	(0x00040070)
#define CARMINE_GRAPH_REG_VRINTM	(0x00028064)
#define CARMINE_GRAPH_REG_VRERRM	(0x0002806C)

/* DCTL register offsets */
#define CARMINE_DCTL_REG			(0x00300000)
#define CARMINE_DCTL_REG_MODE_ADD	(0x00)
#define CARMINE_DCTL_REG_IOCONT1_IOCONT0	(0x24)
#define CARMINE_DCTL_REG_DDRIF2_DDRIF1	(0x14)
#define CARMINE_DCTL_REG_RSV0_STATES	(0x0C)
#define CARMINE_DCTL_REG_RSV2_RSV1	(0x10)
#define CARMINE_DCTL_REG_SETTIME1_EMODE	(0x04)
#define CARMINE_DCTL_REG_REFRESH_SETTIME2	(0x08)

/* CTL register offsets */
#define CARMINE_CTL_REG				(0x00400000)
#define CARMINE_CTL_REG_CLOCK_ENABLE	(0x000C)
#define CARMINE_CTL_REG_SOFTWARE_RESET	(0x0010)

/* WB register offsets */
#define CARMINE_WB_REG				(0x00180000)
#define CARMINE_WB_REG_WBM			(0x0004)
#define CARMINE_WB_REG_WBM_DEFAULT	(0x0001c020)

/* Display register base offsets */
#define CARMINE_DISP0_REG			(0x00100000)
#define CARMINE_DISP1_REG			(0x00140000)
#define CARMINE_GRAPH_REG			(0x00000000)

/* Bit definitions */
#define CARMINE_L0E				(1 << 16)
#define CARMINE_DEN				(1 << 31)

/* Shift definitions */
#define CARMINE_DISP_WIDTH_SHIFT	(16)
#define CARMINE_DISP_WIN_H_SHIFT	(16)
#define CARMINE_DISP_HDB_SHIFT		(16)
#define CARMINE_DISP_HSW_SHIFT		(16)
#define CARMINE_DISP_HTP_SHIFT		(16)
#define CARMINE_DISP_VDP_SHIFT		(16)
#define CARMINE_DISP_VSW_SHIFT		(24)
#define CARMINE_DISP_VTR_SHIFT		(16)

/* Mask definitions */
#define CARMINE_DISP_DCM_MASK		(0x0000FFFF)

/* Default values */
#define CARMINE_DFLT_IP_CLOCK_ENABLE	(0x03ff)
#define CARMINE_DFLT_IP_DCTL_MODE	(0x0121)
#define CARMINE_DFLT_IP_DCTL_MODE_AFT_RST	(0x0021)
#define CARMINE_DFLT_IP_DCTL_STATES	(0x0003)
#define CARMINE_DFLT_IP_DCTL_STATES_AFT_RST	(0x0002)
#define CARMINE_DFLT_IP_DCTL_IO_CONT0	(0x0555)
#define CARMINE_DFLT_IP_DCTL_IO_CONT1	(0x0555)
#define CARMINE_DFLT_IP_DCTL_ADD	(0x05c3)
#define CARMINE_DFLT_IP_DCTL_DDRIF1	(0x6646)
#define CARMINE_DFLT_IP_DCTL_DDRIF2	(0x0055)
#define CARMINE_DFLT_IP_DCTL_EMODE	(0x8000)
#define CARMINE_DFLT_IP_DCTL_FIFO_DEPTH	(0x000f)
#define CARMINE_DFLT_IP_DCTL_REFRESH	(0x0042)
#define CARMINE_DFLT_IP_DCTL_SET_TIME1	(0x4749)
#define CARMINE_DFLT_IP_DCTL_SET_TIME2	(0x2a22)
#define CARMINE_DFLT_IP_DCTL_RESERVE0	(0x0020)
#define CARMINE_DFLT_IP_DCTL_RESERVE2	(0x0000)

/* Timing constants */
#define CARMINE_DCTL_INIT_WAIT_LIMIT	(5000)
#define CARMINE_DCTL_INIT_WAIT_INTERVAL	(1)

/* Color mode */
#define CARMINE_EXT_CMODE_DIRECT24_RGBA	(0xC0000000)

/* Memory BAR and Config BAR indices */
#define CARMINE_MEMORY_BAR	2
#define CARMINE_CONFIG_BAR	3

/* Display memory constants */
#define MAX_DISPLAY		2
#define CARMINE_DISPLAY_MEM	(800 * 600 * 4)
/* Round up to next power of two for PCI BAR alignment */
#define CARMINE_TOTAL_DISPLAY_MEM	(1 << 22) /* 4MB >= 800*600*4*2 = 3.66MB */

/* Mode constants */
#define CARMINE_WINDOW_MODE		(0x00000001)
#define CARMINE_OVERLAY_EXT_MODE	(0x00000002)
#define CARMINE_EXTEND_MODE		(CARMINE_WINDOW_MODE | \
					 CARMINE_OVERLAY_EXT_MODE)

/* Cursor constants */
#define CARMINE_CURSOR_CUTZ_MASK	(0x00000100)
#define CARMINE_CURSOR0_PRIORITY_MASK	(0x00010000)
#define CARMINE_CURSOR1_PRIORITY_MASK	(0x00020000)

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

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* DMA info placeholder */
    

    /* Register shadows */
    uint32_t regs[0x200000 / 4]; /* covers up to 0x200000 */

    /* Status fields */
    

    /* reset/probe state */
    

    /* power mgmt */
    

    /* other fields */
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
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size <= sizeof(s->regs)) {
        uint32_t *reg_ptr = &s->regs[addr / 4];
        switch (size) {
        case 1:
            val = *((uint8_t *)reg_ptr + (addr & 3));
            break;
        case 2:
            val = *((uint16_t *)((uint8_t *)reg_ptr + (addr & 2)));
            break;
        case 4:
            val = *reg_ptr;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid read size %u at addr=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
            return 0;
        }
        return val;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size <= sizeof(s->regs)) {
        uint32_t *reg_ptr = &s->regs[addr / 4];
        switch (size) {
        case 1:
            *((uint8_t *)reg_ptr + (addr & 3)) = val;
            break;
        case 2:
            *((uint16_t *)((uint8_t *)reg_ptr + (addr & 2))) = val;
            break;
        case 4:
            *reg_ptr = val;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "[%s] invalid write size %u at addr=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, size, (uint64_t)addr);
            return;
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    memset(s->regs, 0, sizeof(s->regs));

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

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
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_FUJITU_LIMITED );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CARMINE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CARMINE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Setup BARs */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_NONE;
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_NONE;
    s->bar_info[2].index = CARMINE_MEMORY_BAR;
    s->bar_info[2].type = BAR_TYPE_RAM;
    s->bar_info[2].size = CARMINE_TOTAL_DISPLAY_MEM;
    s->bar_info[2].name = "carminefb-memory";
    s->bar_info[3].index = CARMINE_CONFIG_BAR;
    s->bar_info[3].type = BAR_TYPE_MMIO;
    s->bar_info[3].size = 0x200000; /* covers all registers */
    s->bar_info[3].name = "carminefb-config";
    s->num_bars = 4;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize hardware defaults as per driver */
    /* Note: CARMINE_CTL_REG (0x00400000) exceeds regs array size (0x200000) */
    /* So we skip these initializations to avoid out-of-bounds access */
    /* s->regs[(CARMINE_CTL_REG + CARMINE_CTL_REG_CLOCK_ENABLE) / 4] = CARMINE_DFLT_IP_CLOCK_ENABLE; */
    /* s->regs[(CARMINE_WB_REG + CARMINE_WB_REG_WBM) / 4] = CARMINE_WB_REG_WBM_DEFAULT; */

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
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

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
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

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
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
