/*
 * QEMU model for ALi M1563 SMBus controller
 * Based on driver i2c-ali1563.c
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

#define TYPE_PCIBASE_DEVICE "ali1563_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ALI1563_SMB_IOSIZE  16
#define SMB_HST_STS_OFFSET  0
#define SMB_HST_CNTL1_OFFSET 1
#define SMB_HST_CNTL2_OFFSET 2
#define SMB_HST_CMD_OFFSET  3
#define SMB_HST_ADD_OFFSET  4
#define SMB_HST_DAT0_OFFSET 5
#define SMB_HST_DAT1_OFFSET 6
#define SMB_BLK_DAT_OFFSET  7

#define HST_STS_BUSY   0x01
#define HST_STS_INTR   0x02
#define HST_STS_DEVERR 0x04
#define HST_STS_BUSERR 0x08
#define HST_STS_FAIL   0x10
#define HST_STS_DONE   0x80
#define HST_STS_BAD    0x1c

#define HST_CNTL1_TIMEOUT  0x80
#define HST_CNTL1_LAST     0x40
#define HST_CNTL2_KILL     0x04
#define HST_CNTL2_START    0x40
#define HST_CNTL2_QUICK    0x00
#define HST_CNTL2_BYTE     0x01
#define HST_CNTL2_BYTE_DATA    0x02
#define HST_CNTL2_WORD_DATA    0x03
#define HST_CNTL2_BLOCK    0x05
#define HST_CNTL2_SIZEMASK 0x38

#define ALI1563_SMBBA       0x80
#define ALI1563_SMB_HOSTEN  2
#define ALI1563_SMB_IOEN    1

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
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

    /* Hardware Register Shadows */
    uint8_t io_regs[ALI1563_SMB_IOSIZE];

    /* Transaction state */
    bool transaction_pending;
    uint16_t smb_config; /* shadow for ALI1563_SMBBA config register */
};

/* Internal helper for status-triggered signaling (unused) */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* No IRQ logic needed for this driver */
}

/* Device-initiated DMA logic based on driver access patterns (unused) */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* No DMA logic needed for this driver */
}

static void pcibase_complete_transaction(PCIBaseState *s)
{
    uint8_t status = s->io_regs[SMB_HST_STS_OFFSET];
    uint8_t cntl2 = s->io_regs[SMB_HST_CNTL2_OFFSET];
    uint8_t size = (cntl2 & HST_CNTL2_SIZEMASK) >> 3;
    uint8_t add = s->io_regs[SMB_HST_ADD_OFFSET];
    uint8_t rw = add & 0x01; /* 0 = write, 1 = read */

    /* Clear BUSY */
    status &= ~HST_STS_BUSY;
    /* Set DONE for block mode */
    if (size == HST_CNTL2_BLOCK) {
        status |= HST_STS_DONE;
    } else {
        status &= ~HST_STS_DONE;
    }

    /* For read transactions, provide dummy data */
    if (rw) {
        if (size == HST_CNTL2_BYTE || size == HST_CNTL2_BYTE_DATA) {
            s->io_regs[SMB_HST_DAT0_OFFSET] = 0x00;
        } else if (size == HST_CNTL2_WORD_DATA) {
            s->io_regs[SMB_HST_DAT0_OFFSET] = 0x00;
            s->io_regs[SMB_HST_DAT1_OFFSET] = 0x00;
        } else if (size == HST_CNTL2_BLOCK) {
            /* Set block length */
            s->io_regs[SMB_HST_DAT0_OFFSET] = 0x20; /* max 32 */
            s->io_regs[SMB_BLK_DAT_OFFSET] = 0x00;
        }
    }

    s->io_regs[SMB_HST_STS_OFFSET] = status;
    s->transaction_pending = false;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr == ALI1563_SMBBA) {
        /* Return I/O base from BAR0 with enable bits set */
        uint16_t iobase = pci_get_long(pdev->config + PCI_BASE_ADDRESS_0) & PCI_BASE_ADDRESS_IO_MASK;
        val = (iobase & ~(ALI1563_SMB_IOSIZE - 1)) | ALI1563_SMB_HOSTEN | ALI1563_SMB_IOEN;
        s->smb_config = val;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    pci_default_write_config(pdev, addr, val, len);

    if (addr == ALI1563_SMBBA) {
        /* Update shadow; driver may set IOEN */
        s->smb_config = val;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* MMIO not used by driver; no-op */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* MMIO not used by driver; no-op */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= ALI1563_SMB_IOSIZE) {
        return ~0ULL;
    }

    if (addr == SMB_HST_STS_OFFSET) {
        /* If transaction pending, complete it */
        if (s->transaction_pending) {
            pcibase_complete_transaction(s);
        }
        val = s->io_regs[addr];
    } else {
        val = s->io_regs[addr];
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= ALI1563_SMB_IOSIZE) {
        return;
    }

    if (addr == SMB_HST_STS_OFFSET) {
        /* W1C: clear bits set in val */
        s->io_regs[addr] &= ~(uint8_t)val;
    } else if (addr == SMB_HST_CNTL2_OFFSET) {
        uint8_t old = s->io_regs[addr];
        s->io_regs[addr] = (uint8_t)val;
        if (val & HST_CNTL2_START) {
            /* Start transaction */
            s->transaction_pending = true;
            s->io_regs[SMB_HST_STS_OFFSET] |= HST_STS_BUSY;
            s->io_regs[SMB_HST_STS_OFFSET] &= ~HST_STS_DONE; /* clear done */
        }
    } else {
        s->io_regs[addr] = (uint8_t)val;
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

    memset(s->io_regs, 0, sizeof(s->io_regs));
    s->transaction_pending = false;
    s->smb_config = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10b9 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1563 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_SMBUS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = ALI1563_SMB_IOSIZE;
    s->bar_info[0].name = "ali1563-smb";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize state */
    memset(s->io_regs, 0, sizeof(s->io_regs));
    s->transaction_pending = false;
    s->smb_config = 0;
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
    .name = "ali1563_smbus_pci",
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

    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
