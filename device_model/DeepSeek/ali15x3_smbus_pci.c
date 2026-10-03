/*
 * QEMU model of ALI15x3 SMBus controller
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

/* PCI IDs */
#define PCI_VENDOR_ID_ALI15X3        0x10b9
#define PCI_DEVICE_ID_ALI15X3        0x7101

/* BAR0 size */
#define ALI15X3_SMB_IOSIZE           32
#define ALI15X3_SMB_DEFAULTBASE      0xE800

/* Register offsets from smba base (I/O space) */
#define SMBHSTSTS_OFFSET  0
#define SMBHSTCNT_OFFSET  1
#define SMBHSTSTART_OFFSET  2
#define SMBHSTADD_OFFSET  3
#define SMBHSTDAT0_OFFSET 4
#define SMBHSTDAT1_OFFSET 5
#define SMBBLKDAT_OFFSET  6
#define SMBHSTCMD_OFFSET  7

/* PCI config registers (not standard) */
#define SMBCOM   0x004
#define SMBREV   0x008
#define SMBBA    0x014
#define SMBHSTCFG 0x0E0
#define SMBCLK   0x0E2
#define SMBATPC  0x05B
#define SMBSLVC  0x0E1

/* Command and status bits */
#define ALI15X3_LOCK      0x06
#define ALI15X3_ABORT     0x02
#define ALI15X3_T_OUT     0x04

/* Transfer size commands */
#define ALI15X3_QUICK      0x00
#define ALI15X3_BYTE       0x10
#define ALI15X3_BYTE_DATA  0x20
#define ALI15X3_WORD_DATA  0x30
#define ALI15X3_BLOCK_DATA 0x40
#define ALI15X3_BLOCK_CLR  0x80

/* Status flags */
#define ALI15X3_STS_IDLE   0x04
#define ALI15X3_STS_BUSY   0x08
#define ALI15X3_STS_DONE   0x10
#define ALI15X3_STS_DEV    0x20
#define ALI15X3_STS_COLL   0x40
#define ALI15X3_STS_TERM   0x80
#define ALI15X3_STS_ERR    0xE0

/* IO register struct */
typedef struct {
    uint8_t sts;
    uint8_t cnt;
    uint8_t start;
    uint8_t add;
    uint8_t dat0;
    uint8_t dat1;
    uint8_t blkdat;
    uint8_t cmd;
} ALI15X3IORegs;

#define TYPE_PCIBASE_DEVICE "ali15x3_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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
    ALI15X3IORegs smb_io;
    uint8_t block_data[32];
    int block_idx;
};

G_GNUC_UNUSED static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* MMIO not used */
    return 0;
}

G_GNUC_UNUSED static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* MMIO not used */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= ALI15X3_SMB_IOSIZE) {
        return ~0ULL;
    }

    switch (addr) {
    case SMBHSTSTS_OFFSET:
        val = s->smb_io.sts;
        /* IDLE is computed as inverse of BUSY */
        if (val & ALI15X3_STS_BUSY) {
            val &= ~ALI15X3_STS_IDLE;
        } else {
            val |= ALI15X3_STS_IDLE;
        }
        break;
    case SMBHSTCNT_OFFSET:
        val = s->smb_io.cnt;
        break;
    case SMBHSTSTART_OFFSET:
        /* Write-only, reads return 0 */
        break;
    case SMBHSTADD_OFFSET:
        val = s->smb_io.add;
        break;
    case SMBHSTDAT0_OFFSET:
        val = s->smb_io.dat0;
        break;
    case SMBHSTDAT1_OFFSET:
        val = s->smb_io.dat1;
        break;
    case SMBBLKDAT_OFFSET:
        if (s->block_idx < 32) {
            val = s->block_data[s->block_idx];
            s->block_idx++;
        }
        break;
    case SMBHSTCMD_OFFSET:
        val = s->smb_io.cmd;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= ALI15X3_SMB_IOSIZE) {
        return;
    }

    switch (addr) {
    case SMBHSTSTS_OFFSET:
        /* Write-1-to-Clear: clear bits that are set in val */
        s->smb_io.sts &= ~((uint8_t)val);
        break;
    case SMBHSTCNT_OFFSET:
        s->smb_io.cnt = (uint8_t)val;
        /* Check for block clear */
        if (val & ALI15X3_BLOCK_CLR) {
            s->block_idx = 0;
        }
        /* Check for T_OUT or ABORT: clear BUSY */
        if (val & (ALI15X3_T_OUT | ALI15X3_ABORT)) {
            s->smb_io.sts &= ~ALI15X3_STS_BUSY;
        }
        break;
    case SMBHSTSTART_OFFSET:
        /* Start transaction: set BUSY, then immediately complete with DONE */
        s->smb_io.sts |= ALI15X3_STS_BUSY;
        /* Simulate immediate completion */
        s->smb_io.sts = (s->smb_io.sts & ~ALI15X3_STS_BUSY) | ALI15X3_STS_DONE;
        break;
    case SMBHSTADD_OFFSET:
        s->smb_io.add = (uint8_t)val;
        break;
    case SMBHSTDAT0_OFFSET:
        s->smb_io.dat0 = (uint8_t)val;
        break;
    case SMBHSTDAT1_OFFSET:
        s->smb_io.dat1 = (uint8_t)val;
        break;
    case SMBBLKDAT_OFFSET:
        if (s->block_idx < 32) {
            s->block_data[s->block_idx] = (uint8_t)val;
            s->block_idx++;
        }
        break;
    case SMBHSTCMD_OFFSET:
        s->smb_io.cmd = (uint8_t)val;
        break;
    default:
        break;
    }
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
    pci_device_reset(PCI_DEVICE(dev));

    memset(&s->smb_io, 0, sizeof(s->smb_io));
    s->smb_io.sts = ALI15X3_STS_IDLE;
    s->block_idx = 0;
    memset(s->block_data, 0, sizeof(s->block_data));
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    uint32_t val = pci_default_read_config(pdev, address, len);
    if (address == SMBBA && len == 2) {
        uint32_t bar0 = pci_get_long(pdev->config + PCI_BASE_ADDRESS_0);
        if (bar0 & PCI_BASE_ADDRESS_SPACE_IO) {
            uint16_t io_addr = bar0 & ~0x3;
            val = io_addr;
        }
    } else if (address == SMBBA && len == 1) {
        uint32_t bar0 = pci_get_long(pdev->config + PCI_BASE_ADDRESS_0);
        if (bar0 & PCI_BASE_ADDRESS_SPACE_IO) {
            uint16_t io_addr = bar0 & ~0x3;
            val = (io_addr >> ((address - SMBBA) * 8)) & 0xff;
        }
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    /* Prevent writes to SMBBA from changing BAR base */
    if (address == SMBBA || (address < SMBBA + 2 && address + len > SMBBA)) {
        return;
    }
    pci_default_write_config(pdev, address, val, len);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ALI15X3);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_ALI15X3);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_SMBUS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Custom config registers */
    pci_set_byte(pci_conf + SMBATPC, ALI15X3_LOCK);  /* Locked initially */
    pci_set_byte(pci_conf + SMBREV, 0x01);

    /* Initialize BAR0 */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = ALI15X3_SMB_IOSIZE,
        .name = "ali15x3-smba"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Override config read/write */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;
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
    .name = "ali15x3_smbus_pci",
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
