/*
 * QEMU SiI3124 PCI-X Serial ATA Controller Emulation
 *
 * Based on Linux driver: drivers/ata/sata_sil24.c
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
#define CLASS_ID  PCI_CLASS_STORAGE_SATA

#define SIL24_NPORTS2FLAG(nports)       ((((unsigned)(nports) - 1) & 0x3) << 30)
#define SIL24_FLAG2NPORTS(flag)         ((((flag) >> 30) & 0x3) + 1)

/* Driver-specific constants inferred from usage */
#define SIL24_HOST_BAR          0
#define SIL24_PORT_BAR          2
#define SIL24_MAX_PORTS         4
#define SIL24_MAX_SGE           32

/* Host Registers (BAR0) */
#define HOST_SLOT_STAT          0x00
#define HOST_CTRL               0x40
#define HOST_IRQ_STAT           0x44
#define HOST_FLASH_CMD          0x70

#define HOST_CTRL_GLOBAL_RST    (1U << 31)
#define IRQ_STAT_4PORTS         0xF

/* Port Registers (BAR2) */
#define PORT_REGS_SIZE          0x2000
#define PORT_CTRL_STAT          0x00
#define PORT_CTRL_CLR           0x04
#define PORT_IRQ_STAT           0x08
#define PORT_IRQ_ENABLE_SET     0x10
#define PORT_IRQ_ENABLE_CLR     0x14
#define PORT_CMD_ACTIVATE       0x1C
#define PORT_CMD_ERR            0x24
#define PORT_CONTEXT            0x28
#define PORT_DECODE_ERR_THRESH  0x40
#define PORT_CRC_ERR_THRESH     0x42
#define PORT_HSHK_ERR_THRESH    0x44
#define PORT_DECODE_ERR_CNT     0x48
#define PORT_CRC_ERR_CNT        0x4A
#define PORT_HSHK_ERR_CNT       0x4C
#define PORT_SCONTROL           0x100
#define PORT_PHY_CFG            0x148
#define PORT_SLOT_STAT          0x180
#define PORT_LRAM               0x1000
#define PORT_LRAM_SLOT_SZ       0x80

/* Port Control/Status Bits */
#define PORT_CS_RDY             (1U << 22)
#define PORT_CS_INIT            (1U << 21)
#define PORT_CS_DEV_RST         (1U << 20)
#define PORT_CS_PORT_RST        (1U << 19)
#define PORT_CS_PMP_RESUME      (1U << 18)
#define PORT_CS_PMP_EN          (1U << 17)
#define PORT_CS_32BIT_ACTV      (1U << 16)
#define PORT_CS_IRQ_WOC         (1U << 12)

/* Port IRQ Bits */
#define PORT_IRQ_COMPLETE       (1U << 0)
#define PORT_IRQ_ERROR          (1U << 1)
#define PORT_IRQ_PORTRDY_CHG    (1U << 2)
#define PORT_IRQ_SDB_NOTIFY     (1U << 11)
#define PORT_IRQ_UNK_FIS        (1U << 25)

/* PRB Control */
#define PRB_CTRL_SRST           0x40

struct sil24_prb {
    uint16_t ctrl;
    uint16_t prot;
    uint32_t rx_cnt;
    uint8_t fis[6 * 4];
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

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */

typedef struct SiI24Port {
    int id;
    struct PCIBaseState *parent;

    uint32_t ctrl_stat;
    uint32_t irq_stat;
    uint32_t irq_enable;
    uint64_t cmd_activate;
    uint32_t scontrol;
    uint32_t phy_cfg;
    uint32_t slot_stat;
    
    /* Error counters */
    uint16_t decode_err_thresh;
    uint16_t crc_err_thresh;
    uint16_t hshk_err_thresh;
    uint16_t decode_err_cnt;
    uint16_t crc_err_cnt;
    uint16_t hshk_err_cnt;

    /* LRAM (Local RAM) for PRBs/FIS */
    uint8_t lram[PORT_LRAM_SLOT_SZ * 32];
} SiI24Port;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_host;
    MemoryRegion bar_port;

    uint32_t host_ctrl;
    uint32_t host_irq_stat;
    uint32_t host_flash_cmd;

    SiI24Port ports[SIL24_MAX_PORTS];
    bool has_msi;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static void pcibase_update_irq(PCIBaseState *s);

/* ------------------------------------------------------------------ */
/* Helper: Port Reset                                                  */
/* ------------------------------------------------------------------ */
static void sil24_port_reset(SiI24Port *p)
{
    p->ctrl_stat = PORT_CS_RDY;
    p->irq_stat = 0;
    p->irq_enable = 0;
    p->cmd_activate = 0;
    p->scontrol = 0;
    p->phy_cfg = 0;
    p->slot_stat = 0;
    p->decode_err_thresh = 0;
    p->crc_err_thresh = 0;
    p->hshk_err_thresh = 0;
    p->decode_err_cnt = 0;
    p->crc_err_cnt = 0;
    p->hshk_err_cnt = 0;
    memset(p->lram, 0, sizeof(p->lram));
}

/* ------------------------------------------------------------------ */
/* MMIO handlers - Host BAR0                                           */
/* ------------------------------------------------------------------ */

static uint64_t sil24_host_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case HOST_SLOT_STAT:
        /* Global slot status - not explicitly used by driver in host_intr, 
         * but driver reads PORT_SLOT_STAT. We return 0 here. */
        val = 0;
        break;
    case HOST_CTRL:
        val = s->host_ctrl;
        break;
    case HOST_IRQ_STAT:
        val = s->host_irq_stat;
        break;
    case HOST_FLASH_CMD:
        val = s->host_flash_cmd;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] host_read unimp addr=0x%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, addr);
        break;
    }
    return val;
}

static void sil24_host_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case HOST_CTRL:
        if (val & HOST_CTRL_GLOBAL_RST) {
            /* Global Reset */
            for (int i = 0; i < SIL24_MAX_PORTS; i++) {
                sil24_port_reset(&s->ports[i]);
            }
            s->host_ctrl = 0;
            s->host_irq_stat = 0;
        } else {
            s->host_ctrl = val;
        }
        pcibase_update_irq(s);
        break;
    case HOST_IRQ_STAT:
        /* Write 1 to clear? Driver doesn't write to HOST_IRQ_STAT, 
         * it reads it. But usually these are W1C or RO. 
         * Driver code: readl(host_base + HOST_IRQ_STAT). 
         * No write seen in sil24_interrupt. */
        break;
    case HOST_FLASH_CMD:
        s->host_flash_cmd = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] host_write unimp addr=0x%" PRIx64 " val=0x%" PRIx64 "\n", 
                      TYPE_PCIBASE_DEVICE, addr, val);
        break;
    }
}

static const MemoryRegionOps sil24_host_ops = {
    .read = sil24_host_read,
    .write = sil24_host_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* MMIO handlers - Port BAR2                                           */
/* ------------------------------------------------------------------ */

static void sil24_execute_command(SiI24Port *p)
{
    PCIBaseState *s = p->parent;
    dma_addr_t prb_addr = p->cmd_activate;
    struct sil24_prb prb;
    
    /* Read PRB from guest memory */
    if (dma_memory_read(&address_space_memory, prb_addr, &prb, sizeof(prb), 
                        MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
        p->irq_stat |= PORT_IRQ_ERROR;
        pcibase_update_irq(s);
        return;
    }

    uint16_t ctrl = le16_to_cpu(prb.ctrl);

    if (ctrl & PRB_CTRL_SRST) {
        /* Soft Reset - Simulate device signature */
        /* Standard SATA signature for disk: 0x00000101 (LBA=1, Count=1) in FIS? 
         * Actually, the driver reads the FIS from LRAM. 
         * We populate LRAM slot 0 with a dummy D2H Register FIS. */
        
        /* FIS Type 0x34 (D2H Register), Interrupt bit, Status=0x50 (Ready) */
        /* Offset 0: Type (0x34) */
        /* Offset 1: PM/Flags (0x00) */
        /* Offset 2: Status (0x50) */
        /* Offset 3: Error (0x00) */
        /* Offset 4: LBA Low (0x01) */
        /* Offset 5: LBA Mid (0x00) */
        /* Offset 6: LBA High (0x00) */
        /* Offset 7: Device (0x00) */
        /* Offset 12: Count (0x01) */
        
        memset(p->lram, 0, 20);
        p->lram[0] = 0x34;
        p->lram[2] = 0x50; /* DRDY | DSC */
        p->lram[4] = 0x01;
        p->lram[12] = 0x01;
        
        p->irq_stat |= PORT_IRQ_COMPLETE;
    } else {
        /* Normal command - Simulate completion */
        p->irq_stat |= PORT_IRQ_COMPLETE;
        p->slot_stat = 0; /* Clear active tags */
    }

    pcibase_update_irq(s);
}

static uint64_t sil24_port_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    int port_idx = (addr / PORT_REGS_SIZE);
    hwaddr port_addr = addr % PORT_REGS_SIZE;
    
    if (port_idx >= SIL24_MAX_PORTS) return 0;
    SiI24Port *p = &s->ports[port_idx];

    if (port_addr >= PORT_LRAM) {
        int offset = port_addr - PORT_LRAM;
        if (offset < sizeof(p->lram)) {
            /* Support 1, 2, 4 byte reads from LRAM */
            uint64_t val = 0;
            memcpy(&val, &p->lram[offset], size);
            return val;
        }
        return 0;
    }

    switch (port_addr) {
    case PORT_CTRL_STAT:
        return p->ctrl_stat;
    case PORT_IRQ_STAT:
        return p->irq_stat;
    case PORT_IRQ_ENABLE_SET:
    case PORT_IRQ_ENABLE_CLR:
        return p->irq_enable;
    case PORT_CMD_ACTIVATE:
        return p->cmd_activate & 0xFFFFFFFF;
    case PORT_CMD_ACTIVATE + 4:
        return p->cmd_activate >> 32;
    case PORT_DECODE_ERR_THRESH: return p->decode_err_thresh;
    case PORT_CRC_ERR_THRESH:    return p->crc_err_thresh;
    case PORT_HSHK_ERR_THRESH:   return p->hshk_err_thresh;
    case PORT_DECODE_ERR_CNT:    return p->decode_err_cnt;
    case PORT_CRC_ERR_CNT:       return p->crc_err_cnt;
    case PORT_HSHK_ERR_CNT:      return p->hshk_err_cnt;
    case PORT_SCONTROL:
        return p->scontrol;
    case PORT_PHY_CFG:
        return p->phy_cfg;
    case PORT_SLOT_STAT:
        return p->slot_stat;
    default:
        return 0;
    }
}

static void sil24_port_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port_idx = (addr / PORT_REGS_SIZE);
    hwaddr port_addr = addr % PORT_REGS_SIZE;

    if (port_idx >= SIL24_MAX_PORTS) return;
    SiI24Port *p = &s->ports[port_idx];

    if (port_addr >= PORT_LRAM) {
        /* Driver doesn't typically write LRAM via MMIO, but for completeness */
        int offset = port_addr - PORT_LRAM;
        if (offset < sizeof(p->lram)) {
            memcpy(&p->lram[offset], &val, size);
        }
        return;
    }

    switch (port_addr) {
    case PORT_CTRL_STAT:
        /* Driver writes PORT_CS_INIT, PORT_CS_DEV_RST, PORT_CS_PORT_RST */
        if (val & PORT_CS_PORT_RST) {
            sil24_port_reset(p);
            p->ctrl_stat |= PORT_CS_PORT_RST;
        }
        if (val & PORT_CS_DEV_RST) {
            p->ctrl_stat |= PORT_CS_DEV_RST;
            /* Auto-clear DEV_RST after a bit? Driver waits for it. 
             * We clear it immediately to simulate fast reset. */
            p->ctrl_stat &= ~PORT_CS_DEV_RST;
            p->ctrl_stat |= PORT_CS_RDY;
        }
        if (val & PORT_CS_INIT) {
            p->ctrl_stat |= PORT_CS_INIT;
            /* Auto-clear INIT and set RDY */
            p->ctrl_stat &= ~PORT_CS_INIT;
            p->ctrl_stat |= PORT_CS_RDY;
        }
        if (val & PORT_CS_IRQ_WOC) {
            p->ctrl_stat |= PORT_CS_IRQ_WOC;
        }
        break;
    case PORT_CTRL_CLR:
        /* Clears bits in CTRL_STAT */
        p->ctrl_stat &= ~val;
        break;
    case PORT_IRQ_STAT:
        /* W1C (Write 1 to Clear) */
        p->irq_stat &= ~val;
        pcibase_update_irq(s);
        break;
    case PORT_IRQ_ENABLE_SET:
        p->irq_enable |= val;
        pcibase_update_irq(s);
        break;
    case PORT_IRQ_ENABLE_CLR:
        p->irq_enable &= ~val;
        pcibase_update_irq(s);
        break;
    case PORT_CMD_ACTIVATE:
        p->cmd_activate = (p->cmd_activate & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFF);
        break;
    case PORT_CMD_ACTIVATE + 4:
        p->cmd_activate = (p->cmd_activate & 0x00000000FFFFFFFFULL) | ((val & 0xFFFFFFFF) << 32);
        /* Trigger command execution on high write (or when address is valid) */
        sil24_execute_command(p);
        break;
    case PORT_DECODE_ERR_THRESH: p->decode_err_thresh = val; break;
    case PORT_CRC_ERR_THRESH:    p->crc_err_thresh = val; break;
    case PORT_HSHK_ERR_THRESH:   p->hshk_err_thresh = val; break;
    case PORT_DECODE_ERR_CNT:    p->decode_err_cnt = val; break;
    case PORT_CRC_ERR_CNT:       p->crc_err_cnt = val; break;
    case PORT_HSHK_ERR_CNT:      p->hshk_err_cnt = val; break;
    case PORT_SCONTROL:
        p->scontrol = val;
        break;
    case PORT_PHY_CFG:
        p->phy_cfg = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps sil24_port_ops = {
    .read = sil24_port_read,
    .write = sil24_port_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* IRQ Logic                                                           */
/* ------------------------------------------------------------------ */
static void pcibase_update_irq(PCIBaseState *s)
{
    uint32_t global_stat = 0;
    int i;

    for (i = 0; i < SIL24_MAX_PORTS; i++) {
        if (s->ports[i].irq_stat & s->ports[i].irq_enable) {
            global_stat |= (1 << i);
        }
    }

    s->host_irq_stat = global_stat;

    /* Check if global interrupts are enabled via HOST_CTRL */
    if (s->host_irq_stat && (s->host_ctrl & IRQ_STAT_4PORTS)) {
        if (s->has_msi) {
            msi_notify(&s->parent_obj, 0);
        } else {
            pci_set_irq(&s->parent_obj, 1);
        }
    } else {
        if (!s->has_msi) {
            pci_set_irq(&s->parent_obj, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    int i;

    s->host_ctrl = 0;
    s->host_irq_stat = 0;
    s->host_flash_cmd = 0;

    for (i = 0; i < SIL24_MAX_PORTS; i++) {
        sil24_port_reset(&s->ports[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int i;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize Ports */
    for (i = 0; i < SIL24_MAX_PORTS; i++) {
        s->ports[i].id = i;
        s->ports[i].parent = s;
        sil24_port_reset(&s->ports[i]);
    }

    /* BAR0: Host Controller Registers */
    memory_region_init_io(&s->bar_host, OBJECT(s), &sil24_host_ops, s, 
                          "sil24-host", 0x100);
    pci_register_bar(pdev, SIL24_HOST_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_host);

    /* BAR2: Port Registers */
    memory_region_init_io(&s->bar_port, OBJECT(s), &sil24_port_ops, s, 
                          "sil24-ports", PORT_REGS_SIZE * SIL24_MAX_PORTS);
    pci_register_bar(pdev, SIL24_PORT_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_port);

    /* MSI init */
    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

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
