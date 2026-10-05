/*
 * QEMU PCI SATA SiI 3124 (sata_sil24) minimal device model
 *
 * Implements only behavior directly visible in linux-6.18/drivers/ata/sata_sil24.c
 *
 * NOTE: This is intentionally minimal and only aims to let the Linux driver
 *       probe, initialize ports, and exercise basic interrupt paths.
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

#define DRV_NAME "sata_sil24"
#define DRV_VERSION "1.1"
#define SIL24_NPORTS2FLAG(nports) ((((unsigned)(nports) - 1) & 0x3) << 30)
#define SIL24_FLAG2NPORTS(flag) ((((flag) >> 30) & 0x3) + 1)

#define SIL24_PCI_VENDOR_ID 0x1095
#define SIL24_PCI_DEVICE_ID 0x3124

#define PCI_CLASS_STORAGE_RAID 0x0104

#define ATA_DMA_BOUNDARY 0xffffUL

/* BAR indices used by the driver */
#define SIL24_HOST_BAR 0
#define SIL24_PORT_BAR 2

/* Host register offsets */
#define HOST_FLASH_CMD   0x00
#define HOST_CTRL        0x04
#define HOST_IRQ_STAT    0x08

/* HOST_CTRL bits (subset used) */
#define HOST_CTRL_GLOBAL_RST (1U << 0)

/* IRQ status mask for up to 4 ports */
#define IRQ_STAT_4PORTS  0x0000000fU

/* PORT register offsets (from driver usage) */
#define PORT_REGS_SIZE        0x200
#define PORT_CTRL_STAT        0x04
#define PORT_CTRL_CLR         0x08
#define PORT_PHY_CFG          0x10
#define PORT_IRQ_ENABLE_SET   0x14
#define PORT_IRQ_ENABLE_CLR   0x18
#define PORT_IRQ_STAT         0x1c
#define PORT_CMD_ACTIVATE     0x20
#define PORT_CONTEXT          0x24
#define PORT_CMD_ERR          0x28
#define PORT_SLOT_STAT        0x2c
#define PORT_DECODE_ERR_THRESH 0x30
#define PORT_CRC_ERR_THRESH    0x32
#define PORT_HSHK_ERR_THRESH   0x34
#define PORT_DECODE_ERR_CNT    0x36
#define PORT_CRC_ERR_CNT       0x38
#define PORT_HSHK_ERR_CNT      0x3a
#define PORT_SCONTROL         0x40

#define PORT_LRAM             0x100
#define PORT_LRAM_SLOT_SZ     0x80

#define PORT_PMP              0x400
#define PORT_PMP_SIZE         0x80
#define PORT_PMP_STATUS       0x00
#define PORT_PMP_QACTIVE      0x04

/* PORT_CTRL_STAT/CLR bits (subset from driver) */
#define PORT_CS_PORT_RST    (1U << 0)
#define PORT_CS_INIT        (1U << 1)
#define PORT_CS_RDY         (1U << 2)
#define PORT_CS_DEV_RST     (1U << 3)
#define PORT_CS_PMP_EN      (1U << 4)
#define PORT_CS_PMP_RESUME  (1U << 5)
#define PORT_CS_IRQ_WOC     (1U << 6)
#define PORT_CS_32BIT_ACTV  (1U << 7)
#define PORT_CS_CDB16       (1U << 8)

/* IRQ bits (subset from driver text) */
#define PORT_IRQ_COMPLETE      (1U << 0)
#define PORT_IRQ_ERROR         (1U << 1)
#define PORT_IRQ_SDB_NOTIFY    (1U << 2)
#define PORT_IRQ_PHYRDY_CHG    (1U << 3)
#define PORT_IRQ_DEV_XCHG      (1U << 4)
#define PORT_IRQ_UNK_FIS       (1U << 5)
#define PORT_IRQ_RAW_SHIFT     16

/* Default port IRQ enable mask used by thaw() */
#define DEF_PORT_IRQ (PORT_IRQ_COMPLETE | PORT_IRQ_ERROR | \
                      PORT_IRQ_SDB_NOTIFY | PORT_IRQ_PHYRDY_CHG | \
                      PORT_IRQ_DEV_XCHG | PORT_IRQ_UNK_FIS)

/* Maximum numbers derived from driver: 4 ports, 32 commands */
#define SIL24_MAX_PORTS 4
#define SIL24_MAX_CMDS  32

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                            */
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

/* Simple PRB representation: we only need FIS area and control/prot */
typedef struct Sil24Prb {
    uint16_t ctrl;
    uint16_t prot;
    uint8_t  fis[24];
} Sil24Prb;

/* Port runtime state */
typedef struct Sil24PortState {
    uint32_t ctrl_stat;
    uint32_t irq_enable;
    uint32_t irq_stat;
    uint32_t slot_stat;
    uint32_t cmd_err;
    uint32_t context;
    uint32_t scontrol[4]; /* via sil24_scr_map[] */

    /* LRAM: one PRB per tag, minimal */
    Sil24Prb prb[SIL24_MAX_CMDS];

    /* PMP region */
    uint32_t pmp_status[16];
    uint32_t pmp_qactive[16];

    /* error counters */
    uint16_t decode_err_thresh;
    uint16_t crc_err_thresh;
    uint16_t hshk_err_thresh;
    uint16_t decode_err_cnt;
    uint16_t crc_err_cnt;
    uint16_t hshk_err_cnt;
} Sil24PortState;

/* ------------------------------------------------------------------ */
/* Device State                                                       */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* host registers */
    uint32_t host_flash_cmd;
    uint32_t host_ctrl;
    uint32_t host_irq_stat;

    /* ports */
    unsigned int n_ports;
    Sil24PortState port[SIL24_MAX_PORTS];
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
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
/* MemoryRegionOps                                                    */
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
/* Helper: register a BAR                                             */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

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
/* Helper: raise/lower legacy INTx based on host_irq_stat             */
/* ------------------------------------------------------------------ */
static void sil24_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->host_irq_stat & IRQ_STAT_4PORTS) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void sil24_port_set_irq(PCIBaseState *s, unsigned int port, uint32_t mask)
{
    if (port >= s->n_ports) {
        return;
    }

    Sil24PortState *p = &s->port[port];

    p->irq_stat |= (mask & p->irq_enable);

    if (p->irq_stat & PORT_IRQ_COMPLETE) {
        s->host_irq_stat |= (1U << port);
    }

    sil24_update_irq(s);
}

static void sil24_port_clear_irq(PCIBaseState *s, unsigned int port, uint32_t mask)
{
    if (port >= s->n_ports) {
        return;
    }

    Sil24PortState *p = &s->port[port];

    p->irq_stat &= ~mask;

    /* If no more IRQ condition, clear host bit */
    if (!(p->irq_stat & PORT_IRQ_COMPLETE)) {
        s->host_irq_stat &= ~(1U << port);
    }

    sil24_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* MMIO handlers                                                       */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Host register block at HOST_BAR (BAR0) base */
    if (addr < 0x1000) {
        switch (addr) {
        case HOST_FLASH_CMD:
            return s->host_flash_cmd;
        case HOST_CTRL:
            return s->host_ctrl;
        case HOST_IRQ_STAT:
            return s->host_irq_stat;
        default:
            break;
        }
        return 0;
    }

    /* Port registers are accessed via PORT_BAR. We model BAR2 as a
     * dedicated region starting at offset 0. However, because QEMU
     * uses a single callback for all MMIO, compute relative offset
     * assuming BAR0 is 0x0 and BAR2 is placed after 0x1000. This
     * is internal to this model; the guest only sees separate BARs.
     */

    /* Assume BAR2 mapped at offset 0x1000 in our container. */
    hwaddr port_base_off = 0x1000;

    if (addr >= port_base_off) {
        hwaddr poff = addr - port_base_off;
        unsigned int port = poff / PORT_REGS_SIZE;
        hwaddr preg = poff % PORT_REGS_SIZE;

        if (port >= s->n_ports) {
            return 0;
        }

        Sil24PortState *p = &s->port[port];

        switch (preg) {
        case PORT_CTRL_STAT:
            return p->ctrl_stat;
        case PORT_CTRL_CLR:
            return 0;
        case PORT_PHY_CFG:
            /* driver writes constant, never reads in provided code */
            return 0;
        case PORT_IRQ_ENABLE_SET:
        case PORT_IRQ_ENABLE_CLR:
            return 0;
        case PORT_IRQ_STAT:
            return p->irq_stat << PORT_IRQ_RAW_SHIFT;
        case PORT_CMD_ERR:
            return p->cmd_err;
        case PORT_SLOT_STAT:
            return p->slot_stat;
        case PORT_DECODE_ERR_THRESH:
            return p->decode_err_thresh;
        case PORT_CRC_ERR_THRESH:
            return p->crc_err_thresh;
        case PORT_HSHK_ERR_THRESH:
            return p->hshk_err_thresh;
        case PORT_DECODE_ERR_CNT:
            return p->decode_err_cnt;
        case PORT_CRC_ERR_CNT:
            return p->crc_err_cnt;
        case PORT_HSHK_ERR_CNT:
            return p->hshk_err_cnt;
        default:
            break;
        }

        /* SControl / SStatus / SError / SActive mapped via scr_map */
        if (preg >= PORT_SCONTROL && preg < PORT_SCONTROL + 16) {
            unsigned int idx = (preg - PORT_SCONTROL) / 4;
            if (idx < 4) {
                return p->scontrol[idx];
            }
        }

        /* PORT_LRAM: PRB/FIS in LRAM */
        if (preg >= PORT_LRAM && preg < PORT_LRAM + PORT_LRAM_SLOT_SZ * SIL24_MAX_CMDS) {
            hwaddr lram_off = preg - PORT_LRAM;
            unsigned int tag = lram_off / PORT_LRAM_SLOT_SZ;
            hwaddr tag_off = lram_off % PORT_LRAM_SLOT_SZ;
            if (tag < SIL24_MAX_CMDS) {
                /* Only FIS bytes used by driver to read TF. */
                if (tag_off >= offsetof(Sil24Prb, fis) &&
                    tag_off < offsetof(Sil24Prb, fis) + sizeof(p->prb[tag].fis)) {
                    hwaddr fis_off = tag_off - offsetof(Sil24Prb, fis);
                    uint8_t tmp[8] = {0};
                    unsigned int i;
                    uint64_t val = 0;
                    for (i = 0; i < size && fis_off + i < sizeof(p->prb[tag].fis); i++) {
                        tmp[i] = p->prb[tag].fis[fis_off + i];
                    }
                    memcpy(&val, tmp, size);
                    return val;
                }
            }
        }

        /* PORT_PMP region */
        if (preg >= PORT_PMP && preg < PORT_PMP + PORT_PMP_SIZE * 16) {
            hwaddr pmp_off = preg - PORT_PMP;
            unsigned int pmp = pmp_off / PORT_PMP_SIZE;
            hwaddr pmp_reg = pmp_off % PORT_PMP_SIZE;
            if (pmp < 16) {
                if (pmp_reg == PORT_PMP_STATUS) {
                    return p->pmp_status[pmp];
                } else if (pmp_reg == PORT_PMP_QACTIVE) {
                    return p->pmp_qactive[pmp];
                }
            }
        }

        return 0;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Host registers */
    if (addr < 0x1000) {
        switch (addr) {
        case HOST_FLASH_CMD:
            s->host_flash_cmd = val;
            break;
        case HOST_CTRL:
            s->host_ctrl = val;
            /* Driver writes GLOBAL_RST on resume; we don't model side effects. */
            break;
        case HOST_IRQ_STAT:
            /* Not used by driver for writes */
            break;
        default:
            break;
        }
        return;
    }

    /* PORT BAR region starting at offset 0x1000 */
    hwaddr port_base_off = 0x1000;
    if (addr >= port_base_off) {
        hwaddr poff = addr - port_base_off;
        unsigned int port = poff / PORT_REGS_SIZE;
        hwaddr preg = poff % PORT_REGS_SIZE;

        if (port >= s->n_ports) {
            return;
        }

        Sil24PortState *p = &s->port[port];

        switch (preg) {
        case PORT_CTRL_STAT:
            /* Writing bits sets them */
            p->ctrl_stat |= (uint32_t)val;
            if (val & PORT_CS_INIT) {
                /* sil24_init_port() waits for INIT to clear, RDY set */
                p->ctrl_stat &= ~PORT_CS_INIT;
                p->ctrl_stat |= PORT_CS_RDY;
            }
            if (val & PORT_CS_DEV_RST) {
                /* emulate immediate completion */
                p->ctrl_stat &= ~PORT_CS_DEV_RST;
            }
            if (val & PORT_CS_PORT_RST) {
                /* port reset toggling; we clear it when CLR written */
            }
            if (val & PORT_CS_PMP_RESUME) {
                /* nothing specific needed */
            }
            break;
        case PORT_CTRL_CLR:
            /* Writing bits clears them */
            p->ctrl_stat &= ~((uint32_t)val);
            break;
        case PORT_PHY_CFG:
            /* store for completeness */
            break;
        case PORT_IRQ_ENABLE_SET:
            p->irq_enable |= (uint32_t)val;
            break;
        case PORT_IRQ_ENABLE_CLR:
            p->irq_enable &= ~((uint32_t)val);
            if (!(p->irq_enable & PORT_IRQ_COMPLETE)) {
                s->host_irq_stat &= ~(1U << port);
                sil24_update_irq(s);
            }
            break;
        case PORT_IRQ_STAT: {
            /* driver writes irq_mask already shifted */
            uint32_t raw = (uint32_t)val >> PORT_IRQ_RAW_SHIFT;
            sil24_port_clear_irq(s, port, raw);
            break;
        }
        case PORT_CMD_ACTIVATE:
        case PORT_CMD_ACTIVATE + 4: {
            /* Command activation: for now emulate immediate completion */
            /* Determine tag from address */
            unsigned int tag = (preg - PORT_CMD_ACTIVATE) / 8;
            if (tag >= SIL24_MAX_CMDS) {
                tag = 0;
            }
            p->slot_stat |= (1U << tag);
            /* completion IRQ */
            sil24_port_set_irq(s, port, PORT_IRQ_COMPLETE);
            break;
        }
        case PORT_CMD_ERR:
            p->cmd_err = (uint32_t)val;
            break;
        case PORT_SLOT_STAT:
            /* driver only reads; allow clear on write */
            p->slot_stat &= ~((uint32_t)val);
            break;
        case PORT_DECODE_ERR_THRESH:
            p->decode_err_thresh = (uint16_t)val;
            break;
        case PORT_CRC_ERR_THRESH:
            p->crc_err_thresh = (uint16_t)val;
            break;
        case PORT_HSHK_ERR_THRESH:
            p->hshk_err_thresh = (uint16_t)val;
            break;
        case PORT_DECODE_ERR_CNT:
            p->decode_err_cnt = (uint16_t)val;
            break;
        case PORT_CRC_ERR_CNT:
            p->crc_err_cnt = (uint16_t)val;
            break;
        case PORT_HSHK_ERR_CNT:
            p->hshk_err_cnt = (uint16_t)val;
            break;
        default:
            break;
        }

        if (preg >= PORT_SCONTROL && preg < PORT_SCONTROL + 16) {
            unsigned int idx = (preg - PORT_SCONTROL) / 4;
            if (idx < 4) {
                p->scontrol[idx] = (uint32_t)val;
            }
            return;
        }

        if (preg >= PORT_LRAM && preg < PORT_LRAM + PORT_LRAM_SLOT_SZ * SIL24_MAX_CMDS) {
            hwaddr lram_off = preg - PORT_LRAM;
            unsigned int tag = lram_off / PORT_LRAM_SLOT_SZ;
            hwaddr tag_off = lram_off % PORT_LRAM_SLOT_SZ;
            if (tag < SIL24_MAX_CMDS) {
                if (tag_off >= offsetof(Sil24Prb, ctrl) && tag_off < offsetof(Sil24Prb, ctrl) + sizeof(uint16_t) && size == 2) {
                    uint16_t v = (uint16_t)val;
                    p->prb[tag].ctrl = le16_to_cpu(cpu_to_le16(v));
                    return;
                }
                if (tag_off >= offsetof(Sil24Prb, prot) && tag_off < offsetof(Sil24Prb, prot) + sizeof(uint16_t) && size == 2) {
                    uint16_t v = (uint16_t)val;
                    p->prb[tag].prot = le16_to_cpu(cpu_to_le16(v));
                    return;
                }
                if (tag_off >= offsetof(Sil24Prb, fis) && tag_off < offsetof(Sil24Prb, fis) + sizeof(p->prb[tag].fis)) {
                    hwaddr fis_off = tag_off - offsetof(Sil24Prb, fis);
                    uint8_t tmp[8];
                    unsigned int i;
                    memcpy(tmp, &val, size);
                    for (i = 0; i < size && fis_off + i < sizeof(p->prb[tag].fis); i++) {
                        p->prb[tag].fis[fis_off + i] = tmp[i];
                    }
                    return;
                }
            }
            return;
        }

        if (preg >= PORT_PMP && preg < PORT_PMP + PORT_PMP_SIZE * 16) {
            hwaddr pmp_off = preg - PORT_PMP;
            unsigned int pmp = pmp_off / PORT_PMP_SIZE;
            hwaddr pmp_reg = pmp_off % PORT_PMP_SIZE;
            if (pmp < 16) {
                if (pmp_reg == PORT_PMP_STATUS) {
                    p->pmp_status[pmp] = (uint32_t)val;
                } else if (pmp_reg == PORT_PMP_QACTIVE) {
                    p->pmp_qactive[pmp] = (uint32_t)val;
                }
            }
            return;
        }

        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* not used by driver */
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    unsigned int i, pmp;

    pci_device_reset(pdev);

    s->host_flash_cmd = 0;
    s->host_ctrl = 0;
    s->host_irq_stat = 0;

    for (i = 0; i < s->n_ports; i++) {
        Sil24PortState *p = &s->port[i];
        memset(p, 0, sizeof(*p));
        p->ctrl_stat = PORT_CS_RDY;
        p->irq_enable = DEF_PORT_IRQ;
        for (pmp = 0; pmp < 16; pmp++) {
            p->pmp_status[pmp] = 0;
            p->pmp_qactive[pmp] = 0;
        }
    }
}

/* ------------------------------------------------------------------ */
/* DMA initialize (no explicit DMA format in driver)                  */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                            */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
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
/* Realize (device init)                                              */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    unsigned int i;

    /* Basic PCI IDs */
    pci_set_word(pci_conf + PCI_VENDOR_ID, SIL24_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, SIL24_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_RAID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* 4 ports as typical */
    s->n_ports = SIL24_FLAG2NPORTS(SIL24_NPORTS2FLAG(4));

    /* BAR layout: BAR0 host regs, BAR2 ports (as MMIO) */
    s->num_bars = 0;

    s->bar_info[s->num_bars].index = SIL24_HOST_BAR;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    /* 0x1000 is already a power-of-two, keep as is */
    s->bar_info[s->num_bars].size = 0x1000;
    s->bar_info[s->num_bars].name = "sil24-host";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    s->bar_info[s->num_bars].index = SIL24_PORT_BAR;
    s->bar_info[s->num_bars].type = BAR_TYPE_MMIO;
    /* PORT_REGS_SIZE * SIL24_MAX_PORTS + 0x1000 = 0x200*4 + 0x1000 = 0x1800 (not power of 2)
     * Round up to next power of two (0x2000) to satisfy pci_register_bar() assertion.
     * This does not add any new driver-visible registers. */
    s->bar_info[s->num_bars].size = 0x2000;
    s->bar_info[s->num_bars].name = "sil24-port";
    s->bar_info[s->num_bars].sparse = false;
    s->num_bars++;

    for (i = 0; i < (unsigned)s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* No MSI/MSI-X setup here; driver can still use legacy INTx. */

    for (i = 0; i < s->n_ports; i++) {
        Sil24PortState *p = &s->port[i];
        memset(p, 0, sizeof(*p));
        p->ctrl_stat = PORT_CS_RDY;
        p->irq_enable = DEF_PORT_IRQ;
    }
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                     */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
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
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                     */
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

