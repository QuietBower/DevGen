/*
 * QEMU Silicon Image 3124/3132 SATA controller emulation
 *
 * Copyright (c) 2025 Your Name
 *
 * Based on Linux kernel driver: drivers/ata/sata_sil24.c
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

#define TYPE_PCIBASE_DEVICE "sata_sil24_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver */
#define VENDOR_ID 0x1095
#define DEVICE_ID 0x3124
#define CLASS_ID PCI_CLASS_STORAGE_SATA

#define SIL24_MAX_CMDS		32
#define SIL24_MAX_SGE		32

/* Register offsets and bit definitions */
#define HOST_CTRL			0x00
#define HOST_IRQ_STAT			0x04
#define HOST_FLASH_CMD			0x08

#define PORT_REGS_SIZE			0x80
#define PORT_LRAM			0x000
#define PORT_LRAM_SLOT_SZ		0x100
#define PORT_CTRL_STAT			0x400
#define PORT_CTRL_CLR			0x404
#define PORT_IRQ_STAT			0x408
#define PORT_IRQ_ENABLE_SET		0x40c
#define PORT_IRQ_ENABLE_CLR		0x410
#define PORT_SLOT_STAT			0x414
#define PORT_CMD_ACTIVATE		0x418
#define PORT_SCONTROL			0x440
#define PORT_PHY_CFG			0x470
#define PORT_DECODE_ERR_THRESH		0x480
#define PORT_CRC_ERR_THRESH		0x482
#define PORT_HSHK_ERR_THRESH		0x484
#define PORT_DECODE_ERR_CNT		0x486
#define PORT_CRC_ERR_CNT		0x488
#define PORT_HSHK_ERR_CNT		0x48a
#define PORT_CMD_ERR			0x490
#define PORT_CONTEXT			0x494
#define PORT_PMP			0x500
#define PORT_PMP_SIZE			0x10

/* HOST_CTRL bits */
#define HOST_CTRL_GLOBAL_RST		0x00000001
#define HOST_CTRL_TRDY			0x00000002
#define HOST_CTRL_STOP			0x00000004
#define HOST_CTRL_DEVSEL		0x00000008

/* PORT_CTRL_STAT/CLR bits */
#define PORT_CS_PORT_RST		0x00000001
#define PORT_CS_DEV_RST			0x00000002
#define PORT_CS_INIT			0x00000004
#define PORT_CS_RDY			0x00000008
#define PORT_CS_IRQ_WOC			0x00000010
#define PORT_CS_32BIT_ACTV		0x00000020
#define PORT_CS_PMP_EN			0x00000040
#define PORT_CS_PMP_RESUME		0x00000080
#define PORT_CS_CDB16			0x00000100

/* IRQ bits */
#define PORT_IRQ_COMPLETE		0x00000001
#define PORT_IRQ_ERROR			0x00000002
#define PORT_IRQ_SDB_NOTIFY		0x00000004
#define PORT_IRQ_PHYRDY_CHG		0x00000008
#define PORT_IRQ_DEV_XCHG		0x00000010
#define PORT_IRQ_UNK_FIS		0x00000020
#define PORT_IRQ_RAW_SHIFT		16

#define DEF_PORT_IRQ			(PORT_IRQ_COMPLETE | PORT_IRQ_ERROR | \
					 PORT_IRQ_SDB_NOTIFY | PORT_IRQ_PHYRDY_CHG | \
					 PORT_IRQ_DEV_XCHG | PORT_IRQ_UNK_FIS)

#define IRQ_STAT_4PORTS			0x0000000f

/* PRB control bits */
#define PRB_CTRL_SRST			0x0004
#define PRB_CTRL_PROTOCOL		0x0001
#define PRB_CTRL_PACKET_READ		0x0002
#define PRB_CTRL_PACKET_WRITE		0x0003

/* PRB protocol bits */
#define PRB_PROT_NCQ			0x0001
#define PRB_PROT_WRITE			0x0002
#define PRB_PROT_READ			0x0004

/* SGE flags */
#define SGE_TRM				0x80000000

struct sil24_prb {
	uint16_t ctrl;
	uint16_t prot;
	uint32_t rx_cnt;
	uint8_t fis[24];
};

struct sil24_sge {
	uint64_t addr;
	uint32_t cnt;
	uint32_t flags;
};

struct sil24_ata_block {
	struct sil24_prb prb;
	struct sil24_sge sge[SIL24_MAX_SGE];
};

struct sil24_atapi_block {
	struct sil24_prb prb;
	uint8_t cdb[16];
	struct sil24_sge sge[SIL24_MAX_SGE];
};

union sil24_cmd_block {
	struct sil24_ata_block ata;
	struct sil24_atapi_block atapi;
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

    /* Host and port BAR backing storage */
    uint8_t *host_backing;
    size_t host_backing_size;
    uint8_t *port_backing;
    size_t port_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* DMA info */
    uint64_t dma_mask;

    /* Register shadows */
    uint32_t host_ctrl;
    uint32_t port_ctrl_stat[4];
    uint32_t port_irq_enable[4];
    uint32_t port_slot_stat[4];
    
    /* Command blocks for each port */
    union sil24_cmd_block cmd_blocks[4][SIL24_MAX_CMDS];
    dma_addr_t cmd_block_dma[4];
    
    /* Status fields */
    bool do_port_rst[4];
    
    /* reset/probe state */
    bool initialized;
    
    /* other fields */
    qemu_irq irq;
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

    /* Determine which BAR we're accessing */
    if (addr < s->host_backing_size) {
        /* Host BAR access */
        switch (addr) {
        case HOST_CTRL:
            val = s->host_ctrl;
            break;
        case HOST_IRQ_STAT:
            /* Return IRQ status for all ports */
            val = 0;
            for (int i = 0; i < 4; i++) {
                if (s->port_irq_enable[i] & PORT_IRQ_COMPLETE) {
                    val |= (1 << i);
                }
            }
            val &= IRQ_STAT_4PORTS;
            break;
        default:
            if (addr < s->host_backing_size) {
                memcpy(&val, s->host_backing + addr, size);
            }
            break;
        }
    } else {
        /* Port BAR access */
        hwaddr port_offset = addr - s->host_backing_size;
        int port = port_offset / PORT_REGS_SIZE;
        hwaddr reg = port_offset % PORT_REGS_SIZE;
        
        if (port >= 4) {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s] Invalid port access: addr=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, addr);
            return 0;
        }
        
        switch (reg) {
        case PORT_CTRL_STAT:
            val = s->port_ctrl_stat[port];
            break;
        case PORT_IRQ_STAT:
            /* Return raw IRQ status */
            val = (uint64_t)s->port_irq_enable[port] << PORT_IRQ_RAW_SHIFT;
            break;
        case PORT_SLOT_STAT:
            val = s->port_slot_stat[port];
            break;
        case PORT_SCONTROL:
        case PORT_SCONTROL + 4:
        case PORT_SCONTROL + 8:
        case PORT_SCONTROL + 12:
            /* SCR registers - just return 0 for now */
            val = 0;
            break;
        case PORT_CMD_ERR:
            /* Return 0 for no error */
            val = 0;
            break;
        case PORT_CONTEXT:
            /* Return context with PMP=0 */
            val = 0;
            break;
        default:
            if (addr < s->host_backing_size + s->port_backing_size) {
                memcpy(&val, s->port_backing + (addr - s->host_backing_size), size);
            }
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Determine which BAR we're accessing */
    if (addr < s->host_backing_size) {
        /* Host BAR access */
        switch (addr) {
        case HOST_CTRL:
            s->host_ctrl = val;
            if (val & HOST_CTRL_GLOBAL_RST) {
                /* Global reset - handled in reset function */
                pcibase_reset(DEVICE(s));
            }
            break;
        case HOST_FLASH_CMD:
            /* GPIO off - ignore */
            break;
        default:
            if (addr + size <= s->host_backing_size) {
                memcpy(s->host_backing + addr, &val, size);
            }
            break;
        }
    } else {
        /* Port BAR access */
        hwaddr port_offset = addr - s->host_backing_size;
        int port = port_offset / PORT_REGS_SIZE;
        hwaddr reg = port_offset % PORT_REGS_SIZE;
        
        if (port >= 4) {
            qemu_log_mask(LOG_GUEST_ERROR, "[%s] Invalid port access: addr=%" PRIx64 " val=%" PRIx64 "\n", TYPE_PCIBASE_DEVICE, addr, val);
            return;
        }
        
        switch (reg) {
        case PORT_CTRL_STAT:
            s->port_ctrl_stat[port] |= val;
            if (val & PORT_CS_PORT_RST) {
                /* Port reset */
                s->port_ctrl_stat[port] &= ~PORT_CS_PORT_RST;
                s->port_ctrl_stat[port] |= PORT_CS_RDY;
                s->do_port_rst[port] = false;
            }
            if (val & PORT_CS_DEV_RST) {
                /* Device reset */
                s->port_ctrl_stat[port] &= ~PORT_CS_DEV_RST;
                /* Simulate device becoming ready */
                s->port_ctrl_stat[port] |= PORT_CS_RDY;
            }
            if (val & PORT_CS_INIT) {
                /* Initialize port */
                s->port_ctrl_stat[port] &= ~PORT_CS_INIT;
                s->port_ctrl_stat[port] |= PORT_CS_RDY;
            }
            if (val & PORT_CS_PMP_RESUME) {
                /* PMP resume */
                s->port_ctrl_stat[port] |= PORT_CS_PMP_RESUME;
            }
            break;
        case PORT_CTRL_CLR:
            s->port_ctrl_stat[port] &= ~val;
            break;
        case PORT_IRQ_STAT:
            /* Clear IRQ status */
            if (val & (PORT_IRQ_COMPLETE << PORT_IRQ_RAW_SHIFT)) {
                s->port_irq_enable[port] &= ~PORT_IRQ_COMPLETE;
            }
            break;
        case PORT_IRQ_ENABLE_SET:
            s->port_irq_enable[port] |= val;
            break;
        case PORT_IRQ_ENABLE_CLR:
            s->port_irq_enable[port] &= ~val;
            break;
        case PORT_CMD_ACTIVATE ... PORT_CMD_ACTIVATE + 7:
            /* Handle command activation */
            if ((reg - PORT_CMD_ACTIVATE) % 8 == 0) {
                /* Lower 32 bits of DMA address */
                uint32_t tag = (reg - PORT_CMD_ACTIVATE) / 8;
                if (tag < SIL24_MAX_CMDS) {
                    /* Mark command as active */
                    s->port_slot_stat[port] |= (1 << tag);
                    /* Generate completion interrupt after a short delay */
                    s->port_irq_enable[port] |= PORT_IRQ_COMPLETE;
                    qemu_irq_raise(s->irq);
                }
            }
            break;
        case PORT_SCONTROL ... PORT_SCONTROL + 15:
            /* SCR write - ignore for now */
            break;
        default:
            if (addr < s->host_backing_size + s->port_backing_size) {
                memcpy(s->port_backing + (addr - s->host_backing_size), &val, size);
            }
            break;
        }
    }
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
/* Reset                                                               */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    /* Reset host controller */
    s->host_ctrl = 0;
    
    /* Reset all ports */
    for (int i = 0; i < 4; i++) {
        s->port_ctrl_stat[i] = PORT_CS_RDY;
        s->port_irq_enable[i] = 0;
        s->port_slot_stat[i] = 0;
        s->do_port_rst[i] = false;
    }
    
    s->initialized = false;
    
    if (s->host_backing && s->host_backing_size)
        memset(s->host_backing, 0, s->host_backing_size);
    if (s->port_backing && s->port_backing_size)
        memset(s->port_backing, 0, s->port_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    s->dma_mask = UINT64_MAX;
    
    /* Allocate command block DMA areas for each port */
    for (int i = 0; i < 4; i++) {
        s->cmd_block_dma[i] = 0x10000000 + i * 0x1000000; /* Fixed DMA addresses for simplicity */
    }
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                             */
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
    
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                               */
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
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000; /* Host BAR */
    s->bar_info[0].name = "sil24-host";
    
    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 0x800; /* Port BAR (2 ports * 0x80 per port) */
    s->bar_info[2].name = "sil24-port";
    
    s->num_bars = 3; /* Only BAR0 and BAR2 are used */
    
    /* Allocate backing storage */
    s->host_backing_size = 0x4000;
    s->host_backing = g_malloc0(s->host_backing_size);
    
    s->port_backing_size = 0x800;
    s->port_backing = g_malloc0(s->port_backing_size);
    
    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config */
    s->irq = pci_allocate_irq(pdev);
    
    /* Try MSI */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* DMA init */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    /* Initialize controller state */
    s->host_ctrl = IRQ_STAT_4PORTS; /* Enable interrupts for all ports */
    for (int i = 0; i < 4; i++) {
        s->port_ctrl_stat[i] = PORT_CS_RDY;
        s->port_irq_enable[i] = DEF_PORT_IRQ;
    }
    
    s->initialized = true;
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

    if (s->host_backing) {
        g_free(s->host_backing);
        s->host_backing = NULL;
    }
    
    if (s->port_backing) {
        g_free(s->port_backing);
        s->port_backing = NULL;
    }
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
