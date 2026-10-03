/*
 * QEMU model for Micron P320h (mtip32xx) PCIe SSD
 * Based on Linux driver mtip32xx.c
 * Implements necessary MMIO registers, command processing for probe.
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

/* AHCI-style register offsets (HBA space) */
#define HOST_CAP         0x00
#define HOST_CTL         0x04
#define HOST_IRQ_STAT    0x08
#define HOST_PORTS_IMPL  0x0C
#define HOST_HSORG       0xFC

/* Port registers (relative to port base = ABAR + PORT_OFFSET) */
#define PORT_OFFSET      0x100
#define PORT_LST_ADDR        0x00
#define PORT_LST_ADDR_HI     0x04
#define PORT_FIS_ADDR        0x08
#define PORT_FIS_ADDR_HI     0x0C
#define PORT_IRQ_STAT        0x10
#define PORT_IRQ_MASK        0x14
#define PORT_CMD             0x18
#define PORT_TFDATA          0x20
#define PORT_SIG             0x24
#define PORT_SSTS            0x28
#define PORT_SCTL            0x2C
#define PORT_SERR            0x30
#define PORT_SACT            0x34
#define PORT_CI              0x38
#define PORT_SDBV            0x7C

/* Per-group register stride */
#define PORT_GROUP_OFFSET    0x80

/* HOST_CTL bits */
#define HOST_RESET           0x01
#define HOST_IRQ_EN          0x02

/* HOST_CAP bits */
#define HOST_CAP_64          (1U << 31)
#define HOST_CAP_NZDMA       (1U << 19)

/* PORT_CMD bits */
#define PORT_CMD_START       0x0001
#define PORT_CMD_FIS_RX      0x0010
#define PORT_CMD_LIST_ON     0x8000

/* Port interrupt mask bits (AHCI standard) */
#define PORT_IRQ_HBUS_ERR        (1 << 0)
#define PORT_IRQ_IF_ERR          (1 << 1)
#define PORT_IRQ_CONNECT         (1 << 2)
#define PORT_IRQ_SDB_FIS         (1 << 3)
#define PORT_IRQ_UNK_FIS         (1 << 4)
#define PORT_IRQ_D2H_REG_FIS     (1 << 5)
#define PORT_IRQ_PIOS_FIS        (1 << 6)
#define PORT_IRQ_TF_ERR          (1 << 30)
#define PORT_IRQ_HBUS_DATA_ERR   (1 << 8)
#define PORT_IRQ_IF_NONFATAL     (1 << 9)
#define PORT_IRQ_OVERFLOW        (1 << 10)
#define PORT_IRQ_BAD_PMP         (1 << 11)
#define PORT_IRQ_PHYRDY          (1 << 12)

#define PORT_IRQ_ERR \
    (PORT_IRQ_HBUS_ERR | PORT_IRQ_IF_ERR | PORT_IRQ_CONNECT | \
     PORT_IRQ_PHYRDY | PORT_IRQ_UNK_FIS | PORT_IRQ_BAD_PMP | \
     PORT_IRQ_TF_ERR | PORT_IRQ_HBUS_DATA_ERR | PORT_IRQ_IF_NONFATAL | \
     PORT_IRQ_OVERFLOW)
#define PORT_IRQ_LEGACY \
    (PORT_IRQ_PIOS_FIS | PORT_IRQ_D2H_REG_FIS)
#define PORT_IRQ_HANDLED \
    (PORT_IRQ_SDB_FIS | PORT_IRQ_LEGACY | \
     PORT_IRQ_TF_ERR | PORT_IRQ_IF_ERR | \
     PORT_IRQ_CONNECT | PORT_IRQ_PHYRDY)
#define DEF_PORT_IRQ \
    (PORT_IRQ_ERR | PORT_IRQ_LEGACY | PORT_IRQ_SDB_FIS)

/* Maximum slot groups and slots */
#define MTIP_MAX_SLOT_GROUPS    8
#define MTIP_MAX_COMMAND_SLOTS  (MTIP_MAX_SLOT_GROUPS * 32)

/* Command table offsets */
#define AHCI_CMD_TBL_HDR_SZ     0x80
#define RX_FIS_D2H_REG          0x40
#define RX_FIS_PIO_SETUP        0x20

/* ATA commands */
#define ATA_CMD_ID_ATA          0xEC
#define ATA_CMD_READ_LOG_EXT    0x2F
#define ATA_CMD_SMART           0xB0

/* Recognize some other commands */
#define ATA_CMD_FPDMA_READ      0x60
#define ATA_CMD_FPDMA_WRITE     0x61
#define ATA_CMD_STANDBYNOW1     0xE0
#define ATA_CMD_SEC_ERASE_PREP  0x3C
#define ATA_CMD_SEC_ERASE_UNIT  0x3D
#define ATA_CMD_DOWNLOAD_MICRO  0x92
#define ATA_CMD_SET_FEATURES    0xEF

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

struct mtip_cmd_sg {
    uint32_t dba;
    uint32_t dba_upper;
    uint32_t reserved;
    uint32_t info;
};

struct host_to_dev_fis {
    unsigned char type;
    unsigned char opts;
    unsigned char command;
    unsigned char features;
    union {
        unsigned char lba_low;
        unsigned char sector;
    };
    union {
        unsigned char lba_mid;
        unsigned char cyl_low;
    };
    union {
        unsigned char lba_hi;
        unsigned char cyl_hi;
    };
    union {
        unsigned char device;
        unsigned char head;
    };
    union {
        unsigned char lba_low_ex;
        unsigned char sector_ex;
    };
    union {
        unsigned char lba_mid_ex;
        unsigned char cyl_low_ex;
    };
    union {
        unsigned char lba_hi_ex;
        unsigned char cyl_hi_ex;
    };
    unsigned char features_ex;
    unsigned char sect_count;
    unsigned char sect_cnt_ex;
    unsigned char res2;
    unsigned char control;
    unsigned int res3;
};

/* Identify device data (little-endian for capacity fields, big-endian for strings) */
static const uint16_t identify_data[256] = {
    [0]   = 0x0040,  /* non-removable ATA device */
    [1]   = 0x3FFF,  /* logical cylinders (obsolete) */
    [3]   = 0x0010,  /* heads */
    [6]   = 0x003F,  /* sectors per track */
    [10]  = 0x3231,  /* Serial number: "12" */
    [11]  = 0x3433,  /* "34" */
    [12]  = 0x3635,  /* "56" */
    [13]  = 0x3837,  /* "78" */
    [14]  = 0x3039,  /* "90" */
    [15]  = 0x3231,  /* repeat to fill 20 bytes */
    [16]  = 0x3433,
    [17]  = 0x3635,
    [18]  = 0x3837,
    [19]  = 0x3039,
    [23]  = 0x312E,  /* Firmware "1.0" */
    [24]  = 0x302E,
    [25]  = 0x2020,
    [26]  = 0x2020,
    /* Model string "Micron RealSSD P320h QEMU   " (big-endian words) */
    [27]  = 0x4D69,  /* "Mi" */
    [28]  = 0x6372,  /* "cr" */
    [29]  = 0x6F6E,  /* "on" */
    [30]  = 0x2052,  /* " R" */
    [31]  = 0x6561,  /* "ea" */
    [32]  = 0x6C53,  /* "lS" */
    [33]  = 0x5344,  /* "SD" */
    [34]  = 0x2050,  /* " P" */
    [35]  = 0x3332,  /* "32" */
    [36]  = 0x3068,  /* "0h" */
    [37]  = 0x2051,  /* " Q" */
    [38]  = 0x454D,  /* "EM" */
    [39]  = 0x5520,  /* "U " */
    [40]  = 0x2020,  /* spaces */
    [41]  = 0x2020,
    [42]  = 0x2020,
    [43]  = 0x2020,
    [44]  = 0x2020,
    [45]  = 0x2020,
    [46]  = 0x2020,
    [49]  = 0x0F00,  /* capabilities: DMA, LBA, IORDY */
    [50]  = 0x4000,  /* additional capabilities: LBA48 */
    [76]  = 0x0006,  /* SATA capabilities */
    [77]  = 0x0000,
    [78]  = 0x0008,
    [82]  = 0x346B,  /* command set supported: SMART, HPA, 48-bit */
    [83]  = 0x7C01,
    [84]  = 0x4023,
    [85]  = 0x3469,  /* command set enabled */
    [86]  = 0x3C01,
    [87]  = 0x4023,
    [100] = 0x4240,  /* LBA48 capacity: 1000000 sectors (0xF4240) LE */
    [101] = 0x000F,
    [102] = 0x0000,
    [103] = 0x0000,
    [106] = 0x4000,  /* physical sector size: 512 bytes */
    [117] = 0x0200,  /* words per logical sector: 256 (512 bytes) */
    [118] = 0x0000,
    [128] = 0x0000,  /* security status */
    [129] = 0x0000,
    [217] = 0x0001,  /* nominal media rotation rate */
};

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    int num_bars;
    bool msi_enabled;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Combined register file for HBA and port groups */
    uint8_t regs[0x1000];

    /* Shadow variables for important registers */
    uint32_t host_irq_status;
    uint64_t command_list_base;
    uint64_t fis_base;

    uint32_t s_active[MTIP_MAX_SLOT_GROUPS];
    uint32_t ci[MTIP_MAX_SLOT_GROUPS];
    uint32_t sdbv[MTIP_MAX_SLOT_GROUPS];

    uint32_t slot_groups;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int port_base = PORT_OFFSET;
    uint32_t irq_mask = *(uint32_t *)(s->regs + port_base + PORT_IRQ_MASK);
    uint32_t irq_stat = *(uint32_t *)(s->regs + port_base + PORT_IRQ_STAT);
    bool irq_active = false;

    if (irq_stat & irq_mask) {
        irq_active = true;
    }

    if (irq_active) {
        s->host_irq_status |= 1;  /* assuming port 0 */
    } else {
        s->host_irq_status &= ~1;
    }

    if ((*(uint32_t *)(s->regs + HOST_CTL) & HOST_IRQ_EN) && s->host_irq_status) {
        if (s->msi_enabled) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->msi_enabled) {
            pci_set_irq(pdev, 0);
        }
    }
}

static void process_single_cmd(PCIBaseState *s, int group, int tag)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    dma_addr_t cmd_hdr_addr = s->command_list_base + tag * sizeof(struct mtip_cmd_hdr);
    struct mtip_cmd_hdr hdr;
    uint8_t fis_buf[32];
    struct host_to_dev_fis *fis;
    int fis_len;
    dma_addr_t ctba;
    struct mtip_cmd_sg prd;
    dma_addr_t buf_addr;
    uint32_t buf_len;
    bool data_transfer = false;

    /* Read command header */
    pci_dma_read(pdev, cmd_hdr_addr, &hdr, sizeof(hdr));
    fis_len = hdr.opts & 0x1F;
    ctba = (dma_addr_t)hdr.ctba | ((dma_addr_t)hdr.ctbau << 32);

    /* Read FIS from command table */
    if (fis_len * 4 > sizeof(fis_buf)) {
        return;
    }
    pci_dma_read(pdev, ctba, fis_buf, fis_len * 4);
    fis = (struct host_to_dev_fis *)fis_buf;

    /* Determine if this is a known command we need to emulate */
    if (fis->command == ATA_CMD_ID_ATA) {
        /* Read PRDT entry to get buffer */
        pci_dma_read(pdev, ctba + AHCI_CMD_TBL_HDR_SZ, &prd, sizeof(prd));
        buf_addr = (dma_addr_t)prd.dba | ((dma_addr_t)prd.dba_upper << 32);
        buf_len = (prd.info & 0x3FFFFF) + 1;
        if (buf_len >= 512) {
            pci_dma_write(pdev, buf_addr, identify_data, 512);
        }
        data_transfer = true;
    } else if (fis->command == ATA_CMD_READ_LOG_EXT) {
        uint32_t sectors = (uint32_t)fis->sect_count | ((uint32_t)fis->sect_cnt_ex << 8);
        if (sectors > 0) {
            pci_dma_read(pdev, ctba + AHCI_CMD_TBL_HDR_SZ, &prd, sizeof(prd));
            buf_addr = (dma_addr_t)prd.dba | ((dma_addr_t)prd.dba_upper << 32);
            buf_len = (prd.info & 0x3FFFFF) + 1;
            /* Write zeros */
            if (buf_len >= sectors * 512) {
                uint8_t *zerobuf = g_malloc0(sectors * 512);
                pci_dma_write(pdev, buf_addr, zerobuf, sectors * 512);
                g_free(zerobuf);
            }
        }
        data_transfer = true;
    } else if (fis->command == ATA_CMD_SMART && fis->features == 0xD0) {
        pci_dma_read(pdev, ctba + AHCI_CMD_TBL_HDR_SZ, &prd, sizeof(prd));
        buf_addr = (dma_addr_t)prd.dba | ((dma_addr_t)prd.dba_upper << 32);
        buf_len = (prd.info & 0x3FFFFF) + 1;
        if (buf_len >= 512) {
            uint8_t zerobuf[512] = {0};
            pci_dma_write(pdev, buf_addr, zerobuf, 512);
        }
        data_transfer = true;
    } else {
        /* For unknown commands, do nothing (or complete with zero data) */
    }

    /* Write D2H Register FIS to receive area */
    if (s->fis_base) {
        struct host_to_dev_fis d2h;
        memset(&d2h, 0, sizeof(d2h));
        d2h.type = 0x34;
        d2h.command = 0x50;  /* status OK */
        d2h.features = 0x00; /* error */
        pci_dma_write(pdev, s->fis_base + RX_FIS_D2H_REG, &d2h, sizeof(d2h));
    }

    /* Mark command complete */
    s->s_active[group] &= ~(1 << tag);
    s->ci[group] &= ~(1 << tag);
    s->sdbv[group] |= (1 << tag);

    /* Trigger SDB interrupt */
    uint32_t *irq_stat = (uint32_t *)(s->regs + PORT_OFFSET + PORT_IRQ_STAT);
    *irq_stat |= PORT_IRQ_SDB_FIS;
    pcibase_update_irq(s);
}

static void process_group_ci_write(PCIBaseState *s, int group)
{
    uint32_t new_ci = s->ci[group];
    uint32_t prev_ci = *(uint32_t *)(s->regs + PORT_OFFSET + group * PORT_GROUP_OFFSET + PORT_CI);
    uint32_t to_issue = new_ci & ~prev_ci;

    for (int tag = 0; tag < 32; tag++) {
        if (to_issue & (1 << tag)) {
            process_single_cmd(s, group, tag);
        }
    }
    *(uint32_t *)(s->regs + PORT_OFFSET + group * PORT_GROUP_OFFSET + PORT_CI) = s->ci[group];
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < PORT_OFFSET) {
        /* HBA registers */
        switch (addr) {
        case HOST_CAP:
            val = HOST_CAP_64 | HOST_CAP_NZDMA | (1 << 0); /* also claim 1 port */
            break;
        case HOST_HSORG:
            /* Set ASIC style, hw rev 0x01, 1 slot group (0 group) */
            val = 0x108;
            break;
        default:
            memcpy(&val, s->regs + addr, MIN(size, 4));
            break;
        }
    } else {
        /* Port registers */
        uint32_t port_off = addr - PORT_OFFSET;
        uint32_t group = port_off / PORT_GROUP_OFFSET;
        uint32_t reg = port_off % PORT_GROUP_OFFSET;

        if (group >= s->slot_groups) {
            return ~0ULL;
        }

        switch (reg) {
        case PORT_SACT:
            val = s->s_active[group];
            break;
        case PORT_CI:
            val = s->ci[group];
            break;
        case PORT_SDBV:
            val = s->sdbv[group];
            break;
        case PORT_SSTS:
            val = 0x00000113;  /* device present, PHY up, speed Gen2 */
            break;
        default:
            memcpy(&val, s->regs + addr, MIN(size, 4));
            break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    if (addr < PORT_OFFSET) {
        /* HBA registers */
        switch (addr) {
        case HOST_CTL:
            if (val & HOST_RESET) {
                /* HBA reset: clear reset bit immediately */
                val &= ~HOST_RESET;
                /* Clear port state */
                s->ci[0] = 0;
                s->s_active[0] = 0;
                s->sdbv[0] = 0;
                s->host_irq_status = 0;
                memset(s->regs + PORT_OFFSET, 0, PORT_GROUP_OFFSET);
            }
            memcpy(s->regs + addr, &val, MIN(size, 4));
            /* After writing HOST_CTL, update IRQ */
            pcibase_update_irq(s);
            break;
        case HOST_IRQ_STAT:
            s->host_irq_status &= ~val;
            pcibase_update_irq(s);
            break;
        default:
            memcpy(s->regs + addr, &val, MIN(size, 4));
            break;
        }
    } else {
        /* Port registers */
        uint32_t port_off = addr - PORT_OFFSET;
        uint32_t group = port_off / PORT_GROUP_OFFSET;
        uint32_t reg = port_off % PORT_GROUP_OFFSET;

        if (group >= s->slot_groups) {
            return;
        }

        switch (reg) {
        case PORT_LST_ADDR:
            s->command_list_base = (s->command_list_base & ~0xFFFFFFFFULL) | (uint32_t)val;
            memcpy(s->regs + addr, &val, 4);
            break;
        case PORT_LST_ADDR_HI:
            s->command_list_base = (s->command_list_base & 0xFFFFFFFFULL) | ((uint64_t)(uint32_t)val << 32);
            memcpy(s->regs + addr, &val, 4);
            break;
        case PORT_FIS_ADDR:
            s->fis_base = (s->fis_base & ~0xFFFFFFFFULL) | (uint32_t)val;
            memcpy(s->regs + addr, &val, 4);
            break;
        case PORT_FIS_ADDR_HI:
            s->fis_base = (s->fis_base & 0xFFFFFFFFULL) | ((uint64_t)(uint32_t)val << 32);
            memcpy(s->regs + addr, &val, 4);
            break;
        case PORT_IRQ_STAT:
            *(uint32_t *)(s->regs + addr) &= ~(uint32_t)val;
            pcibase_update_irq(s);
            break;
        case PORT_SACT:
            s->s_active[group] = val;
            memcpy(s->regs + addr, &val, 4);
            break;
        case PORT_CI:
            s->ci[group] = val;
            process_group_ci_write(s, group);
            break;
        case PORT_SDBV:
            s->sdbv[group] &= ~val;
            memcpy(s->regs + addr, &val, 4);
            break;
        case PORT_SERR:
            /* Write to clear bits */
            memcpy(s->regs + addr, &val, 4);
            break;
        default:
            memcpy(s->regs + addr, &val, MIN(size, 4));
            break;
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    s->host_irq_status = 0;
    s->command_list_base = 0;
    s->fis_base = 0;
    memset(s->s_active, 0, sizeof(s->s_active));
    memset(s->ci, 0, sizeof(s->ci));
    memset(s->sdbv, 0, sizeof(s->sdbv));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1344);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x5150);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x010601);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    int ret = msi_init(pdev, 0, 1, true, false, errp);
    if (ret) {
        s->msi_enabled = false;
    } else {
        s->msi_enabled = true;
    }

    /* BAR 5: MMIO, size 0x1000 to cover HBA + port groups */
    memory_region_init_io(&s->bar_regions[5], OBJECT(s), &pcibase_mmio_ops, s,
                          "mtip32xx-mmio", 0x1000);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[5]);

    s->slot_groups = 1;
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
