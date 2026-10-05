/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 1: fill register macros, BAR list, BAR sizes, structs, enums.
 * Phase 2: implement MMIO/PIO read/write, DMA init, IRQ init, reset, config hooks
 * Phase 3 (Repair Phase)：correct syntax, types, includes, missing symbols, remove dead code
 * Phase 4 (Debug & Update Phase)：Based on actual kernel logs from QEMU boot, repair the virtual hardware behavior
 *
 * Replace #PLACEHOLDER# blocks with device-specific code/data.
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

/* Related register/macros/constants from driver */
#define PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA 0x008e

/* 
 * Missing macros defined for compilation based on driver usage patterns.
 * These values are inferred to create a functional register map.
 */
#define NV_MMIO_BAR 5
#define NV_MCP_SATA_CFG_20 0x50
#define NV_MCP_SATA_CFG_20_SATA_SPACE_EN (1 << 0)
#define NV_MCP_SATA_CFG_20_PORT0_EN (1 << 1)
#define NV_MCP_SATA_CFG_20_PORT1_EN (1 << 2)

#define NV_PORT0_SCR_REG_OFFSET 0x000
#define NV_PORT1_SCR_REG_OFFSET 0x040

#define NV_INT_STATUS_CK804 0x400
#define NV_INT_ENABLE_CK804 0x401
#define NV_INT_STATUS_MCP55 0x440
#define NV_INT_ENABLE_MCP55 0x444
#define NV_CTL_MCP55        0x450

#define NV_ADMA_GEN         0x600
#define NV_ADMA_GEN_CTL     0x00
#define NV_ADMA_NOTIFIER_CLEAR 0x30

#define NV_ADMA_PORT        0x480
#define NV_ADMA_PORT_SIZE   0x100

/* ADMA Port Registers (Offsets relative to NV_ADMA_PORT + port * SIZE) */
#define NV_ADMA_CTL         0x00
#define NV_ADMA_STAT        0x02
#define NV_ADMA_CPB_COUNT   0x04
#define NV_ADMA_NEXT_CPB_IDX 0x05
#define NV_ADMA_CPB_BASE_LOW 0x08
#define NV_ADMA_CPB_BASE_HIGH 0x0C
#define NV_ADMA_APPEND      0x10
#define NV_ADMA_NOTIFIER    0x14
#define NV_ADMA_NOTIFIER_ERROR 0x18

/* ADMA Control Bits */
#define NV_ADMA_CTL_GO              (1 << 0)
#define NV_ADMA_CTL_AIEN            (1 << 1)
#define NV_ADMA_CTL_HOTPLUG_IEN     (1 << 2)
#define NV_ADMA_CTL_CHANNEL_RESET   (1 << 3)

/* ADMA Status Bits */
#define NV_ADMA_STAT_DONE           (1 << 0)
#define NV_ADMA_STAT_CPBERR         (1 << 1)
#define NV_ADMA_STAT_CMD_COMPLETE   (1 << 2)
#define NV_ADMA_STAT_IDLE           (1 << 3)
#define NV_ADMA_STAT_LEGACY         (1 << 4)

#define NV_ADMA_DMA_BOUNDARY		0xffffffffUL
#define NV_ADMA_CHECK_INTR(GCTL, PORT) ((GCTL) & (1 << (19 + (12 * (PORT)))))

enum nv_adma_regbits {
	CMDEND	= (1 << 15),		/* end of command list */
	WNB	= (1 << 14),		/* wait-not-BSY */
	IGN	= (1 << 13),		/* ignore this entry */
	CS1n	= (1 << (4 + 8)),	/* std. PATA signals follow... */
	DA2	= (1 << (2 + 8)),
	DA1	= (1 << (1 + 8)),
	DA0	= (1 << (0 + 8)),
};

enum ncq_saw_flag_list {
	ncq_saw_d2h	= (1U << 0),
	ncq_saw_dmas	= (1U << 1),
	ncq_saw_sdb	= (1U << 2),
	ncq_saw_backout	= (1U << 3),
};

enum nv_host_type
{
	GENERIC,
	NFORCE2,
	NFORCE3 = NFORCE2,	/* NF2 == NF3 as far as sata_nv is concerned */
	CK804,
	ADMA,
	MCP5x,
	SWNCQ,
};

struct nv_adma_prd {
	uint64_t			addr;
	uint32_t			len;
	uint8_t			flags;
	uint8_t			packet_len;
	uint16_t			reserved;
};

struct nv_adma_cpb {
	uint8_t			resp_flags;    /* 0 */
	uint8_t			reserved1;     /* 1 */
	uint8_t			ctl_flags;     /* 2 */
	/* len is length of taskfile in 64 bit words */
	uint8_t			len;		/* 3  */
	uint8_t			tag;           /* 4 */
	uint8_t			next_cpb_idx;  /* 5 */
	uint16_t			reserved2;     /* 6-7 */
	uint16_t			tf[12];        /* 8-31 */
	struct nv_adma_prd	aprd[5];       /* 32-111 */
	uint64_t			next_aprd;     /* 112-119 */
	uint64_t			reserved3;     /* 120-127 */
};

struct nv_host_priv {
	unsigned long		type;
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

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;

    /* Device Registers */
    uint32_t cfg_20;
    uint8_t int_status_ck804;
    uint8_t int_enable_ck804;
    uint32_t int_status_mcp55;
    uint32_t int_enable_mcp55;
    uint32_t ctl_mcp55;

    /* Port State (2 ports) */
    struct {
        uint32_t scr[16]; /* SCR registers */
        
        /* ADMA Registers */
        uint16_t adma_ctl;
        uint16_t adma_stat;
        uint8_t adma_cpb_count;
        uint8_t adma_next_cpb_idx;
        uint64_t adma_cpb_base;
        uint32_t adma_notifier;
        uint32_t adma_notifier_error;
        uint16_t adma_append;
        
        QEMUTimer *dma_timer;
        uint32_t pending_tag;
    } ports[2];

    uint32_t adma_gen_ctl;
    uint32_t adma_notifier_clear[2];
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* Internal Logic                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_update_irq(PCIBaseState *s)
{
    int level = 0;
    
    /* Simple OR logic for legacy interrupts based on status and enable masks */
    if (s->int_status_ck804 & s->int_enable_ck804) {
        level = 1;
    }
    
    /* ADMA interrupts */
    for (int i = 0; i < 2; i++) {
        if ((s->ports[i].adma_stat & NV_ADMA_STAT_DONE) && 
            (s->ports[i].adma_ctl & NV_ADMA_CTL_AIEN)) {
            level = 1;
        }
    }

    if (s->has_msi) {
        if (level) msi_notify(&s->parent_obj, 0);
    } else {
        pci_set_irq(&s->parent_obj, level);
    }
}

static void pcibase_adma_timer_cb(void *opaque)
{
    PCIBaseState *s = (PCIBaseState *)opaque;
    /* Assume port 0 for simplicity or iterate. The timer opaque should ideally carry port info. */
    /* For this implementation, we'll check both ports pending tags */
    
    for (int i = 0; i < 2; i++) {
        if (s->ports[i].pending_tag != 0xFFFFFFFF) {
            uint32_t tag = s->ports[i].pending_tag;
            
            /* Simulate completion */
            s->ports[i].adma_stat |= NV_ADMA_STAT_DONE | NV_ADMA_STAT_CMD_COMPLETE;
            s->ports[i].adma_notifier |= (1 << tag);
            
            /* Clear pending */
            s->ports[i].pending_tag = 0xFFFFFFFF;
        }
    }
    
    pcibase_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                 */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int port = -1;

    /* Identify Port for ADMA/SCR ranges */
    if (addr >= NV_PORT0_SCR_REG_OFFSET && addr < NV_PORT0_SCR_REG_OFFSET + 0x40) {
        port = 0;
        int reg = (addr - NV_PORT0_SCR_REG_OFFSET) / 4;
        if (reg < 16) val = s->ports[0].scr[reg];
    } else if (addr >= NV_PORT1_SCR_REG_OFFSET && addr < NV_PORT1_SCR_REG_OFFSET + 0x40) {
        port = 1;
        int reg = (addr - NV_PORT1_SCR_REG_OFFSET) / 4;
        if (reg < 16) val = s->ports[1].scr[reg];
    } else if (addr >= NV_ADMA_PORT && addr < NV_ADMA_PORT + 2 * NV_ADMA_PORT_SIZE) {
        int offset = addr - NV_ADMA_PORT;
        port = offset / NV_ADMA_PORT_SIZE;
        int reg = offset % NV_ADMA_PORT_SIZE;
        
        switch (reg) {
            case NV_ADMA_CTL: val = s->ports[port].adma_ctl; break;
            case NV_ADMA_STAT: val = s->ports[port].adma_stat; break;
            case NV_ADMA_CPB_COUNT: val = s->ports[port].adma_cpb_count; break;
            case NV_ADMA_NEXT_CPB_IDX: val = s->ports[port].adma_next_cpb_idx; break;
            case NV_ADMA_CPB_BASE_LOW: val = s->ports[port].adma_cpb_base & 0xFFFFFFFF; break;
            case NV_ADMA_CPB_BASE_HIGH: val = s->ports[port].adma_cpb_base >> 32; break;
            case NV_ADMA_APPEND: val = s->ports[port].adma_append; break;
            case NV_ADMA_NOTIFIER: val = s->ports[port].adma_notifier; break;
            case NV_ADMA_NOTIFIER_ERROR: val = s->ports[port].adma_notifier_error; break;
        }
    } else {
        switch (addr) {
            case NV_INT_STATUS_CK804: val = s->int_status_ck804; break;
            case NV_INT_ENABLE_CK804: val = s->int_enable_ck804; break;
            case NV_INT_STATUS_MCP55: val = s->int_status_mcp55; break;
            case NV_INT_ENABLE_MCP55: val = s->int_enable_mcp55; break;
            case NV_CTL_MCP55: val = s->ctl_mcp55; break;
            case NV_ADMA_GEN + NV_ADMA_GEN_CTL: val = s->adma_gen_ctl; break;
            case NV_ADMA_GEN + NV_ADMA_NOTIFIER_CLEAR:
                val = s->adma_notifier_clear[0];
                break;
            case NV_ADMA_GEN + NV_ADMA_NOTIFIER_CLEAR + 4:
                val = s->adma_notifier_clear[1];
                break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port = -1;

    if (addr >= NV_PORT0_SCR_REG_OFFSET && addr < NV_PORT0_SCR_REG_OFFSET + 0x40) {
        port = 0;
        int reg = (addr - NV_PORT0_SCR_REG_OFFSET) / 4;
        if (reg < 16) s->ports[0].scr[reg] = val;
    } else if (addr >= NV_PORT1_SCR_REG_OFFSET && addr < NV_PORT1_SCR_REG_OFFSET + 0x40) {
        port = 1;
        int reg = (addr - NV_PORT1_SCR_REG_OFFSET) / 4;
        if (reg < 16) s->ports[1].scr[reg] = val;
    } else if (addr >= NV_ADMA_PORT && addr < NV_ADMA_PORT + 2 * NV_ADMA_PORT_SIZE) {
        int offset = addr - NV_ADMA_PORT;
        port = offset / NV_ADMA_PORT_SIZE;
        int reg = offset % NV_ADMA_PORT_SIZE;
        
        switch (reg) {
            case NV_ADMA_CTL:
                s->ports[port].adma_ctl = val;
                if (val & NV_ADMA_CTL_CHANNEL_RESET) {
                    s->ports[port].adma_stat = NV_ADMA_STAT_IDLE | NV_ADMA_STAT_LEGACY;
                    s->ports[port].adma_cpb_count = 0;
                }
                break;
            case NV_ADMA_STAT:
                /* Write to clear status bits */
                s->ports[port].adma_stat &= ~val;
                pcibase_update_irq(s);
                break;
            case NV_ADMA_CPB_COUNT: s->ports[port].adma_cpb_count = val; break;
            case NV_ADMA_NEXT_CPB_IDX: s->ports[port].adma_next_cpb_idx = val; break;
            case NV_ADMA_CPB_BASE_LOW:
                s->ports[port].adma_cpb_base &= 0xFFFFFFFF00000000ULL;
                s->ports[port].adma_cpb_base |= (val & 0xFFFFFFFF);
                break;
            case NV_ADMA_CPB_BASE_HIGH:
                s->ports[port].adma_cpb_base &= 0x00000000FFFFFFFFULL;
                s->ports[port].adma_cpb_base |= ((val & 0xFFFFFFFF) << 32);
                break;
            case NV_ADMA_APPEND:
                s->ports[port].adma_append = val;
                s->ports[port].pending_tag = val & 0xFF; /* Tag is lower 8 bits */
                /* Schedule completion */
                timer_mod(s->ports[port].dma_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
                break;
            case NV_ADMA_NOTIFIER: s->ports[port].adma_notifier = val; break;
            case NV_ADMA_NOTIFIER_ERROR: s->ports[port].adma_notifier_error = val; break;
        }
    } else {
        switch (addr) {
            case NV_INT_STATUS_CK804: 
                s->int_status_ck804 &= ~val; 
                pcibase_update_irq(s);
                break;
            case NV_INT_ENABLE_CK804: 
                s->int_enable_ck804 = val; 
                pcibase_update_irq(s);
                break;
            case NV_INT_STATUS_MCP55: 
                s->int_status_mcp55 &= ~val; 
                pcibase_update_irq(s);
                break;
            case NV_INT_ENABLE_MCP55: 
                s->int_enable_mcp55 = val; 
                pcibase_update_irq(s);
                break;
            case NV_CTL_MCP55: s->ctl_mcp55 = val; break;
            case NV_ADMA_GEN + NV_ADMA_GEN_CTL: s->adma_gen_ctl = val; break;
            case NV_ADMA_GEN + NV_ADMA_NOTIFIER_CLEAR:
                s->adma_notifier_clear[0] = val;
                /* Writing notifier clear also clears the notifier status in the port */
                if (val) s->ports[0].adma_notifier &= ~val;
                break;
            case NV_ADMA_GEN + NV_ADMA_NOTIFIER_CLEAR + 4:
                s->adma_notifier_clear[1] = val;
                if (val) s->ports[1].adma_notifier &= ~val;
                break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Standard ATA PIO not fully emulated here, returning 0xFF or 0 */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Standard ATA PIO stub */
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
        
    s->cfg_20 = 0;
    s->int_status_ck804 = 0;
    s->int_enable_ck804 = 0;
    s->int_status_mcp55 = 0;
    s->int_enable_mcp55 = 0;
    s->ctl_mcp55 = 0;
    s->adma_gen_ctl = 0;
    
    for (int i = 0; i < 2; i++) {
        memset(s->ports[i].scr, 0, sizeof(s->ports[i].scr));
        s->ports[i].adma_ctl = 0;
        s->ports[i].adma_stat = NV_ADMA_STAT_IDLE | NV_ADMA_STAT_LEGACY;
        s->ports[i].adma_cpb_count = 0;
        s->ports[i].adma_next_cpb_idx = 0;
        s->ports[i].adma_cpb_base = 0;
        s->ports[i].adma_notifier = 0;
        s->ports[i].adma_notifier_error = 0;
        s->ports[i].pending_tag = 0xFFFFFFFF;
        timer_del(s->ports[i].dma_timer);
    }
}

/* ------------------------------------------------------------------ */
/* DMA initialize                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    
    for (int i = 0; i < 2; i++) {
        s->ports[i].dma_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, pcibase_adma_timer_cb, s);
        s->ports[i].pending_tag = 0xFFFFFFFF;
    }
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                             */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);
    
    if (addr == NV_MCP_SATA_CFG_20) {
        val = s->cfg_20;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    
    pci_default_write_config(pdev, addr, val, len);
    
    if (addr == NV_MCP_SATA_CFG_20) {
        s->cfg_20 = val;
    }
}

/* ------------------------------------------------------------------ */
/* Realize                                                             */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NVIDIA);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SATA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR setup based on driver requirement: "NVIDIA SATA controllers will always have six bars" */
    /* BAR 0-4: Standard Legacy/BMDMA PIO ports */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 8, .name = "sata-nv-bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 4, .name = "sata-nv-bar1" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_PIO, .size = 8, .name = "sata-nv-bar2" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_PIO, .size = 4, .name = "sata-nv-bar3" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_PIO, .size = 16, .name = "sata-nv-bar4" };
    
    /* BAR 5: MMIO (SCR registers, etc.) - Size placeholder 4K */
    s->bar_info[5] = (BARInfo){ .index = 5, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "sata-nv-bar5" };
    
    s->num_bars = 6;

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA init */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    /* MSI init */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    for (int i = 0; i < 2; i++) {
        if (s->ports[i].dma_timer) {
            timer_del(s->ports[i].dma_timer);
            timer_free(s->ports[i].dma_timer);
            s->ports[i].dma_timer = NULL;
        }
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init                                                          */
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