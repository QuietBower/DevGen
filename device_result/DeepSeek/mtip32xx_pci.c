/*
 * Minimal Micron RealSSD P320 compatible PCIe SSD.
 *
 * The mtip32xx interface looks like AHCI, with vendor-specific banks of
 * SActive, Command Issue and completion registers for the extra tag groups.
 * This model implements the register and DMA protocol used by the Linux
 * mtip32xx driver.  The media is a small volatile RAM disk.
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

#define TYPE_PCIBASE_DEVICE "mtip32xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_MICRON    0x1344
#define P320H_DEVICE_ID         0x5150
#define MTIP_ABAR               5

#define HOST_CAP               0x00
#define HOST_CTL               0x04
#define HOST_IRQ_STAT          0x08
#define HOST_PORTS_IMPL        0x0c
#define HOST_VERSION           0x10
#define PORT_OFFSET             0x100
#define HOST_HSORG              0xFC
#define PORT_LST_ADDR           0x00
#define PORT_LST_ADDR_HI        0x04
#define PORT_FIS_ADDR           0x08
#define PORT_FIS_ADDR_HI        0x0c
#define PORT_IRQ_STAT           0x10
#define PORT_IRQ_MASK           0x14
#define PORT_CMD                0x18
#define PORT_TFDATA             0x20
#define PORT_SIG                0x24
#define PORT_SCR_STAT           0x28
#define PORT_SCR_CTL            0x2c
#define PORT_SCR_ERR            0x30
#define PORT_SCR_ACT            0x34
#define PORT_COMMAND_ISSUE      0x38
#define PORT_SDBV               0x7C
#define PORT_MEM_SIZE           0x80
#define MTIP_MAX_SLOT_GROUPS    8

#define HOST_CTL_RESET          (1U << 0)
#define HOST_CTL_IRQ_EN         (1U << 1)
#define HOST_CAP_64             (1U << 31)
#define HOST_CAP_NCQ            (1U << 30)
#define HOST_CAP_NZDMA          (1 << 19)
#define PORT_IRQ_TF_ERR         (1U << 30)
#define PORT_IRQ_SDB_FIS        (1U << 3)
#define PORT_IRQ_PIOS_FIS       (1U << 1)
#define PORT_IRQ_D2H_REG_FIS    (1U << 0)

#define MTIP_MAX_COMMAND_SLOTS  (MTIP_MAX_SLOT_GROUPS * 32)
#define HSORG_DISABLE_SLOTGRP_INTR (1<<24)
#define HSORG_DISABLE_SLOTGRP_PXIS (1<<16)

#define MTIP_MAX_SG             504
#define MTIP_CMD_TABLE_SG_OFF   0x80
#define MTIP_SECTOR_SIZE        512
#define MTIP_CAPACITY_SECTORS   (128 * 1024ULL) /* 64 MiB */
#define MTIP_STORAGE_SIZE       (MTIP_CAPACITY_SECTORS * MTIP_SECTOR_SIZE)

#define RX_FIS_PIO_SETUP        0x20
#define RX_FIS_D2H_REG          0x40
#define RX_FIS_SDB              0x58

#define ATA_CMD_READ_LOG_EXT    0x2f
#define ATA_CMD_FPDMA_READ      0x60
#define ATA_CMD_FPDMA_WRITE     0x61
#define ATA_CMD_ID_ATA          0xec
#define ATA_CMD_SMART           0xb0

struct host_to_dev_fis {
    uint8_t type;
    uint8_t opts;
    uint8_t command;
    uint8_t features;

    union {
        uint8_t lba_low;
        uint8_t sector;
    };
    union {
        uint8_t lba_mid;
        uint8_t cyl_low;
    };
    union {
        uint8_t lba_hi;
        uint8_t cyl_hi;
    };
    union {
        uint8_t device;
        uint8_t head;
    };

    union {
        uint8_t lba_low_ex;
        uint8_t sector_ex;
    };
    union {
        uint8_t lba_mid_ex;
        uint8_t cyl_low_ex;
    };
    union {
        uint8_t lba_hi_ex;
        uint8_t cyl_hi_ex;
    };
    uint8_t features_ex;

    uint8_t sect_count;
    uint8_t sect_cnt_ex;
    uint8_t res2;
    uint8_t control;

    uint32_t res3;
};

struct mtip_cmd_sg {
    uint32_t dba;
    uint32_t dba_upper;
    uint32_t reserved;
    uint32_t info;
};

struct mtip_cmd_hdr {
    uint32_t opts;
    union {
        uint32_t byte_count;
        uint32_t status;
    };
    uint32_t ctba;
    uint32_t ctbau;
    uint32_t res[4];
};

#define AHCI_CMD_SLOT_SZ        sizeof(struct mtip_cmd_hdr)

struct smart_attr {
    uint8_t attr_id;
    uint16_t flags;
    uint8_t cur;
    uint8_t worst;
    uint32_t data;
    uint8_t res[3];
} QEMU_PACKED;

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
    bool irq_asserted;

    /* Global and port register shadows. */
    uint32_t host_cap;
    uint32_t host_ctl;
    uint32_t host_irq_stat;
    uint32_t host_hsorg;
    uint32_t port_irq_stat;
    uint32_t port_irq_mask;
    uint32_t port_cmd_reg;
    uint32_t port_scr_ctl_reg;
    uint32_t port_scr_err;
    uint32_t port_cmd_issue[MTIP_MAX_SLOT_GROUPS];
    uint32_t port_sactive[MTIP_MAX_SLOT_GROUPS];
    uint32_t port_sdbv[MTIP_MAX_SLOT_GROUPS];

    /* DMA context programmed through PxCLB/PxFB. */
    uint64_t command_list_dma;
    uint64_t rxfis_dma;

    uint8_t *storage;

    /* Convenient inspection points when debugging the device with GDB. */
    uint64_t read_count;
    uint64_t write_count;
    uint64_t last_lba;
    uint32_t last_sector_count;
    uint8_t last_command;
};

static void mtip_init_fake_storage(PCIBaseState *s)
{
    static const char banner[] =
        "QEMU MTIP32XX FAKE SSD - data supplied by the QEMU device model";
    uint64_t lba;

    for (lba = 0; lba < MTIP_CAPACITY_SECTORS; lba++) {
        uint8_t *sector = s->storage + lba * MTIP_SECTOR_SIZE;
        unsigned int i;

        /* A deterministic tail makes DMA/offset errors easy to recognize. */
        for (i = 0; i < MTIP_SECTOR_SIZE; i++) {
            sector[i] = (uint8_t)(lba + i);
        }
        snprintf((char *)sector, MTIP_SECTOR_SIZE,
                 "%s\nLBA=%" PRIu64 " (byte offset=%" PRIu64 ")\n",
                 banner, lba, lba * MTIP_SECTOR_SIZE);
    }
}

static void mtip_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    bool level = s->host_irq_stat && (s->host_ctl & HOST_CTL_IRQ_EN);

    if (level && !s->irq_asserted) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else if (!level && !msi_enabled(pdev)) {
        pci_set_irq(pdev, 0);
    }
    s->irq_asserted = level;
}

static void mtip_raise_irq(PCIBaseState *s, uint32_t status)
{
    s->port_irq_stat |= status;
    if (status & s->port_irq_mask) {
        s->host_irq_stat |= 1;
    }
    mtip_update_irq(s);
}

static void mtip_id_put_string(uint16_t *id, unsigned int word,
                               unsigned int bytes, const char *value)
{
    unsigned int i;

    for (i = 0; i < bytes; i += 2) {
        uint8_t a = value[i] ? value[i] : ' ';
        uint8_t b = value[i] && value[i + 1] ? value[i + 1] : ' ';

        id[word + i / 2] = cpu_to_le16((a << 8) | b);
        if (!value[i] || !value[i + 1]) {
            i += 2;
            while (i < bytes) {
                id[word + i / 2] = cpu_to_le16(0x2020);
                i += 2;
            }
            break;
        }
    }
}

static void mtip_build_identify(uint8_t data[MTIP_SECTOR_SIZE])
{
    uint16_t *id = (uint16_t *)data;
    uint64_t sectors = MTIP_CAPACITY_SECTORS;
    unsigned int i;
    uint8_t sum = 0;

    memset(data, 0, MTIP_SECTOR_SIZE);
    id[0] = cpu_to_le16(0x0040);
    mtip_id_put_string(id, 10, 20, "QEMU0000000000000001");
    mtip_id_put_string(id, 23, 8, "1.0     ");
    mtip_id_put_string(id, 27, 40, "QEMU Micron P320 compatible SSD");
    id[47] = cpu_to_le16(0x8001);
    id[49] = cpu_to_le16(1U << 9);       /* LBA supported */
    id[75] = cpu_to_le16(31);            /* queue depth minus one */
    id[76] = cpu_to_le16(1U << 8);       /* NCQ supported */
    id[80] = cpu_to_le16(1U << 8);       /* ATA8-ACS */
    id[82] = cpu_to_le16(1U << 0);       /* SMART supported */
    id[83] = cpu_to_le16(1U << 10);      /* 48-bit LBA supported */
    id[85] = cpu_to_le16(1U << 0);       /* SMART enabled */
    id[86] = cpu_to_le16(1U << 10);      /* 48-bit LBA enabled */
    id[100] = cpu_to_le16(sectors);
    id[101] = cpu_to_le16(sectors >> 16);
    id[102] = cpu_to_le16(sectors >> 32);
    id[103] = cpu_to_le16(sectors >> 48);

    /* ATA identify integrity word and checksum. */
    data[510] = 0xa5;
    for (i = 0; i < MTIP_SECTOR_SIZE - 1; i++) {
        sum += data[i];
    }
    data[511] = -sum;
}

static size_t mtip_prd_transfer(PCIBaseState *s, uint64_t table_addr,
                                unsigned int prdtl, uint8_t *buffer,
                                size_t length, bool to_guest)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    size_t done = 0;
    unsigned int i;

    prdtl = MIN(prdtl, MTIP_MAX_SG);
    for (i = 0; i < prdtl && done < length; i++) {
        struct mtip_cmd_sg sg;
        uint64_t dma_addr;
        size_t dma_len;
        MemTxResult result;

        result = pci_dma_read(pdev,
                              table_addr + MTIP_CMD_TABLE_SG_OFF +
                              i * sizeof(sg), &sg, sizeof(sg));
        if (result != MEMTX_OK) {
            break;
        }
        dma_addr = le32_to_cpu(sg.dba) |
                   ((uint64_t)le32_to_cpu(sg.dba_upper) << 32);
        dma_len = (le32_to_cpu(sg.info) & 0x3fffff) + 1;
        dma_len = MIN(dma_len, length - done);

        if (to_guest) {
            result = pci_dma_write(pdev, dma_addr, buffer + done, dma_len);
        } else {
            result = pci_dma_read(pdev, dma_addr, buffer + done, dma_len);
        }
        if (result != MEMTX_OK) {
            break;
        }
        done += dma_len;
    }
    return done;
}

static void mtip_write_received_fis(PCIBaseState *s, bool ncq, bool ok)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct host_to_dev_fis reply = { 0 };

    if (!s->rxfis_dma) {
        return;
    }

    if (ncq) {
        uint8_t sdb_fis[8] = { 0xa1, 0x40, ok ? 0x50 : 0x51,
                               ok ? 0x00 : 0x04 };

        pci_dma_write(pdev, s->rxfis_dma + RX_FIS_SDB,
                      sdb_fis, sizeof(sdb_fis));
        return;
    }

    /*
     * The Linux driver casts both areas to host_to_dev_fis and consumes
     * the status/error fields from byte 2/3.  Populate both so IDENTIFY,
     * SMART and HDIO_* passthrough commands all see a valid completion.
     */
    reply.type = 0x34; /* Register D2H FIS */
    reply.opts = 0x40; /* interrupt bit */
    reply.command = ok ? 0x50 : 0x51; /* ATA status */
    reply.features = ok ? 0x00 : 0x04; /* ATA error */
    reply.control = reply.command;
    pci_dma_write(pdev, s->rxfis_dma + RX_FIS_D2H_REG,
                  &reply, sizeof(reply));

    reply.type = 0x5f; /* PIO Setup FIS */
    pci_dma_write(pdev, s->rxfis_dma + RX_FIS_PIO_SETUP,
                  &reply, sizeof(reply));
}

static void mtip_complete_slot(PCIBaseState *s, unsigned int group,
                               unsigned int bit)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct mtip_cmd_hdr hdr;
    struct host_to_dev_fis fis;
    uint64_t hdr_addr, table_addr;
    uint32_t opts;
    unsigned int tag = group * 32 + bit;
    unsigned int prdtl;
    size_t expected = 0, transferred = 0;
    bool ncq = false, ok = true;

    if (!s->command_list_dma || tag >= MTIP_MAX_COMMAND_SLOTS) {
        ok = false;
        goto complete;
    }

    hdr_addr = s->command_list_dma + tag * sizeof(hdr);
    if (pci_dma_read(pdev, hdr_addr, &hdr, sizeof(hdr)) != MEMTX_OK) {
        ok = false;
        goto complete;
    }
    opts = le32_to_cpu(hdr.opts);
    prdtl = opts >> 16;
    table_addr = le32_to_cpu(hdr.ctba) |
                 ((uint64_t)le32_to_cpu(hdr.ctbau) << 32);
    if (!table_addr || pci_dma_read(pdev, table_addr, &fis, sizeof(fis)) !=
                       MEMTX_OK) {
        ok = false;
        goto complete;
    }

    switch (fis.command) {
    case ATA_CMD_ID_ATA: {
        uint8_t identify[MTIP_SECTOR_SIZE];

        mtip_build_identify(identify);
        expected = sizeof(identify);
        transferred = mtip_prd_transfer(s, table_addr, prdtl, identify,
                                        expected, true);
        break;
    }
    case ATA_CMD_READ_LOG_EXT: {
        uint8_t log[MTIP_SECTOR_SIZE] = { 0 };

        expected = sizeof(log);
        transferred = mtip_prd_transfer(s, table_addr, prdtl, log,
                                        expected, true);
        break;
    }
    case ATA_CMD_SMART: {
        uint8_t smart[MTIP_SECTOR_SIZE] = { 0 };
        struct smart_attr *attr = (struct smart_attr *)(smart + 2);

        /* Attribute 242 is queried during probe. */
        attr->attr_id = 242;
        attr->cur = 100;
        attr->worst = 100;
        expected = sizeof(smart);
        transferred = mtip_prd_transfer(s, table_addr, prdtl, smart,
                                        expected, true);
        break;
    }
    case ATA_CMD_FPDMA_READ:
    case ATA_CMD_FPDMA_WRITE: {
        uint64_t lba = fis.lba_low | ((uint64_t)fis.lba_mid << 8) |
                       ((uint64_t)fis.lba_hi << 16) |
                       ((uint64_t)fis.lba_low_ex << 24) |
                       ((uint64_t)fis.lba_mid_ex << 32) |
                       ((uint64_t)fis.lba_hi_ex << 40);
        unsigned int sectors = fis.features |
                               ((unsigned int)fis.features_ex << 8);
        uint64_t offset = lba * MTIP_SECTOR_SIZE;

        ncq = true;
        expected = (size_t)sectors * MTIP_SECTOR_SIZE;
        if (!sectors || lba >= MTIP_CAPACITY_SECTORS ||
            expected > (MTIP_CAPACITY_SECTORS - lba) * MTIP_SECTOR_SIZE) {
            ok = false;
            break;
        }
        transferred = mtip_prd_transfer(s, table_addr, prdtl,
                                        s->storage + offset, expected,
                                        fis.command == ATA_CMD_FPDMA_READ);
        s->last_command = fis.command;
        s->last_lba = lba;
        s->last_sector_count = sectors;
        if (transferred == expected) {
            if (fis.command == ATA_CMD_FPDMA_READ) {
                s->read_count++;
            } else {
                s->write_count++;
            }
        }
        break;
    }
    default:
        /* Commands without a data phase (flush, standby, features, etc.). */
        break;
    }

    if (expected && transferred != expected) {
        ok = false;
    }
    hdr.byte_count = cpu_to_le32(transferred);
    pci_dma_write(pdev, hdr_addr, &hdr, sizeof(hdr));

complete:
    s->port_cmd_issue[group] &= ~(1U << bit);
    s->port_sactive[group] &= ~(1U << bit);
    mtip_write_received_fis(s, ncq, ok);
    if (!ok) {
        mtip_raise_irq(s, PORT_IRQ_TF_ERR);
    } else if (ncq) {
        s->port_sdbv[group] |= 1U << bit;
        mtip_raise_irq(s, PORT_IRQ_SDB_FIS);
    } else {
        mtip_raise_irq(s, PORT_IRQ_D2H_REG_FIS | PORT_IRQ_PIOS_FIS);
    }
}

static void mtip_issue_slots(PCIBaseState *s, unsigned int group,
                             uint32_t slots)
{
    unsigned int bit;

    for (bit = 0; bit < 32; bit++) {
        if (slots & (1U << bit)) {
            mtip_complete_slot(s, group, bit);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case HOST_CAP:
        val = s->host_cap;
        break;
    case HOST_CTL:
        val = s->host_ctl;
        break;
    case HOST_IRQ_STAT:
        val = s->host_irq_stat;
        break;
    case HOST_PORTS_IMPL:
        val = 1;
        break;
    case HOST_VERSION:
        val = 0x00010300;
        break;
    case HOST_HSORG:
        val = s->host_hsorg;
        break;
    }

    if (addr >= PORT_OFFSET &&
        addr < PORT_OFFSET + MTIP_MAX_SLOT_GROUPS * PORT_MEM_SIZE) {
        hwaddr port_addr = addr - PORT_OFFSET;
        unsigned int group = port_addr / PORT_MEM_SIZE;
        hwaddr reg = port_addr % PORT_MEM_SIZE;

        switch (reg) {
        case PORT_LST_ADDR:
            val = s->command_list_dma;
            break;
        case PORT_LST_ADDR_HI:
            val = s->command_list_dma >> 32;
            break;
        case PORT_FIS_ADDR:
            val = s->rxfis_dma;
            break;
        case PORT_FIS_ADDR_HI:
            val = s->rxfis_dma >> 32;
            break;
        case PORT_IRQ_STAT:
            val = s->port_irq_stat;
            break;
        case PORT_IRQ_MASK:
            val = s->port_irq_mask;
            break;
        case PORT_CMD:
            val = s->port_cmd_reg;
            break;
        case PORT_TFDATA:
            val = 0x50; /* ATA device ready, seek complete */
            break;
        case PORT_SIG:
            val = 0x00000101; /* ATA disk signature */
            break;
        case PORT_SCR_STAT:
            val = 0x123; /* device present, 3 Gbit/s, active */
            break;
        case PORT_SCR_CTL:
            val = s->port_scr_ctl_reg;
            break;
        case PORT_SCR_ERR:
            val = s->port_scr_err;
            break;
        case PORT_SCR_ACT:
            val = s->port_sactive[group];
            break;
        case PORT_COMMAND_ISSUE:
            val = s->port_cmd_issue[group];
            break;
        case PORT_SDBV:
            val = s->port_sdbv[group];
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t value = val;

    switch (addr) {
    case HOST_CTL:
        if (value & HOST_CTL_RESET) {
            unsigned int i;

            s->host_irq_stat = 0;
            s->port_irq_stat = 0;
            s->port_scr_err = 0;
            for (i = 0; i < MTIP_MAX_SLOT_GROUPS; i++) {
                s->port_cmd_issue[i] = 0;
                s->port_sactive[i] = 0;
                s->port_sdbv[i] = 0;
            }
            value &= ~HOST_CTL_RESET; /* reset completes immediately */
        }
        s->host_ctl = value;
        mtip_update_irq(s);
        break;
    case HOST_IRQ_STAT:
        s->host_irq_stat &= ~value; /* W1C */
        mtip_update_irq(s);
        break;
    case HOST_HSORG:
        s->host_hsorg = value;
        break;
    }

    if (addr >= PORT_OFFSET &&
        addr < PORT_OFFSET + MTIP_MAX_SLOT_GROUPS * PORT_MEM_SIZE) {
        hwaddr port_addr = addr - PORT_OFFSET;
        unsigned int group = port_addr / PORT_MEM_SIZE;
        hwaddr reg = port_addr % PORT_MEM_SIZE;

        switch (reg) {
        case PORT_LST_ADDR:
            s->command_list_dma = (s->command_list_dma & 0xffffffff00000000ULL) |
                                  value;
            break;
        case PORT_LST_ADDR_HI:
            s->command_list_dma = (s->command_list_dma & 0xffffffffULL) |
                                  ((uint64_t)value << 32);
            break;
        case PORT_FIS_ADDR:
            s->rxfis_dma = (s->rxfis_dma & 0xffffffff00000000ULL) | value;
            break;
        case PORT_FIS_ADDR_HI:
            s->rxfis_dma = (s->rxfis_dma & 0xffffffffULL) |
                           ((uint64_t)value << 32);
            break;
        case PORT_IRQ_STAT:
            s->port_irq_stat &= ~value; /* W1C */
            break;
        case PORT_IRQ_MASK:
            s->port_irq_mask = value;
            break;
        case PORT_CMD:
            s->port_cmd_reg = value;
            break;
        case PORT_SCR_CTL:
            s->port_scr_ctl_reg = value;
            break;
        case PORT_SCR_ERR:
            s->port_scr_err &= ~value; /* W1C */
            break;
        case PORT_SCR_ACT:
            s->port_sactive[group] |= value;
            break;
        case PORT_COMMAND_ISSUE:
            s->port_cmd_issue[group] |= value;
            mtip_issue_slots(s, group, value);
            break;
        case PORT_SDBV:
            s->port_sdbv[group] &= ~value; /* W1C */
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    // PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    // PCIBaseState *s = opaque;
    
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
    unsigned int i;

    /* 64-bit DMA, NCQ and no-reset-required vendor capability. */
    s->host_cap = HOST_CAP_64 | HOST_CAP_NCQ | HOST_CAP_NZDMA | (31 << 8);
    s->host_ctl = 0;
    s->host_irq_stat = 0;
    s->host_hsorg = 0x8 | (0x1 << 8) | (MTIP_MAX_SLOT_GROUPS - 1);
    s->port_irq_stat = 0;
    s->port_irq_mask = 0;
    s->port_cmd_reg = 0;
    s->port_scr_ctl_reg = 0;
    s->port_scr_err = 0;
    s->command_list_dma = 0;
    s->rxfis_dma = 0;
    s->irq_asserted = false;
    s->read_count = 0;
    s->write_count = 0;
    s->last_lba = 0;
    s->last_sector_count = 0;
    s->last_command = 0;

    for (i = 0; i < MTIP_MAX_SLOT_GROUPS; i++) {
        s->port_cmd_issue[i] = 0;
        s->port_sactive[i] = 0;
        s->port_sdbv[i] = 0;
    }
    pci_set_irq(PCI_DEVICE(dev), 0);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MICRON );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  P320H_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_EXPRESS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;
    s->storage = g_malloc(MTIP_STORAGE_SIZE);
    mtip_init_fake_storage(s);

    /* BAR Initialization */
    s->num_bars = 6;
    s->bar_info[MTIP_ABAR].index = MTIP_ABAR;
    s->bar_info[MTIP_ABAR].type = BAR_TYPE_MMIO;
    s->bar_info[MTIP_ABAR].size = 0x1000; /* Set to 0x1000 to prevent pow2ceil(0) crash */
    s->bar_info[MTIP_ABAR].name = "mtip32xx-mmio";
  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    g_free(s->storage);
    s->storage = NULL;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }

}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mtip32xx_pci",
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
