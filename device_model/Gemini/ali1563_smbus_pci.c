/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

/* Additional include files retrieved from driver context */


#define TYPE_PCIBASE_DEVICE "ali1563_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ALI1563_MAX_TIMEOUT 500
#define ALI1563_SMBBA       0x80
#define ALI1563_SMB_IOEN    1
#define ALI1563_SMB_HOSTEN  2
#define ALI1563_SMB_IOSIZE  16

#define SMB_HST_STS         0
#define SMB_HST_CNTL1       1
#define SMB_HST_CNTL2       2
#define SMB_HST_CMD         3
#define SMB_HST_ADD         4
#define SMB_HST_DAT0        5
#define SMB_HST_DAT1        6
#define SMB_BLK_DAT         7

#define HST_STS_BUSY        0x01
#define HST_STS_INTR        0x02
#define HST_STS_DEVERR      0x04
#define HST_STS_BUSERR      0x08
#define HST_STS_FAIL        0x10
#define HST_STS_DONE        0x80
#define HST_STS_BAD         0x1c

#define HST_CNTL1_TIMEOUT   0x80
#define HST_CNTL1_LAST      0x40

#define HST_CNTL2_KILL      0x04
#define HST_CNTL2_START     0x40
#define HST_CNTL2_QUICK     0x00
#define HST_CNTL2_BYTE      0x01
#define HST_CNTL2_BYTE_DATA 0x02
#define HST_CNTL2_WORD_DATA 0x03
#define HST_CNTL2_BLOCK     0x05
#define HST_CNTL2_SIZEMASK  0x38

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
    MemoryRegion io_region;
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t smb_hst_sts;
    uint8_t smb_hst_cntl1;
    uint8_t smb_hst_cntl2;
    uint8_t smb_hst_cmd;
    uint8_t smb_hst_add;
    uint8_t smb_hst_dat0;
    uint8_t smb_hst_dat1;
    uint8_t smb_blk_dat;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    switch (addr) {
    case SMB_HST_STS:
        val = s->smb_hst_sts;
        break;
    case SMB_HST_CNTL1:
        val = s->smb_hst_cntl1;
        break;
    case SMB_HST_CNTL2:
        val = s->smb_hst_cntl2;
        break;
    case SMB_HST_CMD:
        val = s->smb_hst_cmd;
        break;
    case SMB_HST_ADD:
        val = s->smb_hst_add;
        break;
    case SMB_HST_DAT0:
        val = s->smb_hst_dat0;
        break;
    case SMB_HST_DAT1:
        val = s->smb_hst_dat1;
        break;
    case SMB_BLK_DAT:
        val = s->smb_blk_dat;
        break;
    }
    
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
    case SMB_HST_STS:
        s->smb_hst_sts &= ~val; /* W1C */
        break;
    case SMB_HST_CNTL1:
        s->smb_hst_cntl1 = val;
        break;
    case SMB_HST_CNTL2:
        s->smb_hst_cntl2 = val;
        if (val & HST_CNTL2_START) {
            /* Emulate immediate transaction completion */
            s->smb_hst_sts &= ~HST_STS_BUSY;
            s->smb_hst_sts |= HST_STS_DONE;
            s->smb_hst_cntl2 &= ~HST_CNTL2_START;
            
            uint8_t cmd_size = (s->smb_hst_cntl2 & HST_CNTL2_SIZEMASK) >> 3;
            uint8_t rw = s->smb_hst_add & 0x01;
            if (cmd_size == HST_CNTL2_BLOCK && rw == 1) {
                s->smb_hst_dat0 = 1; /* Return length 1 for block read */
                s->smb_blk_dat = 0xff; /* Dummy data */
            }
        }
        if (val & HST_CNTL2_KILL) {
            s->smb_hst_sts &= ~HST_STS_BUSY;
            s->smb_hst_sts |= HST_STS_FAIL;
            s->smb_hst_cntl2 &= ~HST_CNTL2_KILL;
        }
        break;
    case SMB_HST_CMD:
        s->smb_hst_cmd = val;
        break;
    case SMB_HST_ADD:
        s->smb_hst_add = val;
        break;
    case SMB_HST_DAT0:
        s->smb_hst_dat0 = val;
        break;
    case SMB_HST_DAT1:
        s->smb_hst_dat1 = val;
        break;
    case SMB_BLK_DAT:
        s->smb_blk_dat = val;
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

    s->smb_hst_sts = 0;
    s->smb_hst_cntl1 = 0;
    s->smb_hst_cntl2 = 0;
    s->smb_hst_cmd = 0;
    s->smb_hst_add = 0;
    s->smb_hst_dat0 = 0;
    s->smb_hst_dat1 = 0;
    s->smb_blk_dat = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10b9 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1563 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0C05 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0xA0);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize ALI1563_SMBBA to have Host Controller Enabled and a default base address */
    /* Changed from 0x0400 to 0x1000 to avoid ACPI resource conflict with \DBG OpRegion at 0x402 */
    pci_set_word(pci_conf + ALI1563_SMBBA, 0x1000 | ALI1563_SMB_HOSTEN);

    /* Initialize the IO region, but do not map it yet. It gets mapped in write_config when IOEN is set. */
    memory_region_init_io(&s->io_region, OBJECT(s), &pcibase_pio_ops, s, "ali1563-smb", ALI1563_SMB_IOSIZE);

    /* BAR Initialization */
    s->num_bars = 0;
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    
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

static void pcibase_write_config(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint16_t old_ctrl = pci_get_word(pdev->config + ALI1563_SMBBA);
    
    pci_default_write_config(pdev, address, val, len);
    
    uint16_t new_ctrl = pci_get_word(pdev->config + ALI1563_SMBBA);

    bool old_en = old_ctrl & ALI1563_SMB_IOEN;
    bool new_en = new_ctrl & ALI1563_SMB_IOEN;
    hwaddr old_base = old_ctrl & ~(ALI1563_SMB_IOSIZE - 1);
    hwaddr new_base = new_ctrl & ~(ALI1563_SMB_IOSIZE - 1);

    if (old_en && (!new_en || old_base != new_base)) {
        if (memory_region_is_mapped(&s->io_region)) {
            memory_region_del_subregion(pci_address_space_io(pdev), &s->io_region);
        }
    }
    if (new_en && (!old_en || old_base != new_base)) {
        if (!memory_region_is_mapped(&s->io_region)) {
            memory_region_add_subregion(pci_address_space_io(pdev), new_base, &s->io_region);
        }
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
    k->config_write = pcibase_write_config;
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
