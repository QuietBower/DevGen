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


#define TYPE_PCIBASE_DEVICE "amd8111_smbus2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define AMD_PCI_MISC	0x48
#define AMD_PCI_MISC_SCI	0x04
#define AMD_PCI_MISC_INT	0x02
#define AMD_PCI_MISC_SPEEDUP	0x01
#define AMD_EC_DATA	0x00
#define AMD_EC_SC	0x04
#define AMD_EC_CMD	0x04
#define AMD_EC_ICR	0x08
#define AMD_EC_SC_SMI	0x04
#define AMD_EC_SC_SCI	0x02
#define AMD_EC_SC_BURST	0x01
#define AMD_EC_SC_CMD	0x08
#define AMD_EC_SC_IBF	0x02
#define AMD_EC_SC_OBF	0x01
#define AMD_EC_CMD_RD	0x80
#define AMD_EC_CMD_WR	0x81
#define AMD_EC_CMD_BE	0x82
#define AMD_EC_CMD_BD	0x83
#define AMD_EC_CMD_QR	0x84
#define AMD_SMB_PRTCL	0x00
#define AMD_SMB_STS	0x01
#define AMD_SMB_ADDR	0x02
#define AMD_SMB_CMD	0x03
#define AMD_SMB_DATA	0x04
#define AMD_SMB_BCNT	0x24
#define AMD_SMB_ALRM_A	0x25
#define AMD_SMB_ALRM_D	0x26
#define AMD_SMB_STS_DONE	0x80
#define AMD_SMB_STS_ALRM	0x40
#define AMD_SMB_STS_RES		0x20
#define AMD_SMB_STS_STATUS	0x1f
#define AMD_SMB_STATUS_OK	0x00
#define AMD_SMB_STATUS_FAIL	0x07
#define AMD_SMB_STATUS_DNAK	0x10
#define AMD_SMB_STATUS_DERR	0x11
#define AMD_SMB_STATUS_CMD_DENY	0x12
#define AMD_SMB_STATUS_UNKNOWN	0x13
#define AMD_SMB_STATUS_ACC_DENY	0x17
#define AMD_SMB_STATUS_TIMEOUT	0x18
#define AMD_SMB_STATUS_NOTSUP	0x19
#define AMD_SMB_STATUS_BUSY	0x1A
#define AMD_SMB_STATUS_PEC	0x1F
#define AMD_SMB_PRTCL_WRITE		0x00
#define AMD_SMB_PRTCL_READ		0x01
#define AMD_SMB_PRTCL_QUICK		0x02
#define AMD_SMB_PRTCL_BYTE		0x04
#define AMD_SMB_PRTCL_BYTE_DATA		0x06
#define AMD_SMB_PRTCL_WORD_DATA		0x08
#define AMD_SMB_PRTCL_BLOCK_DATA	0x0a
#define AMD_SMB_PRTCL_PROC_CALL		0x0c
#define AMD_SMB_PRTCL_BLOCK_PROC_CALL	0x0d
#define AMD_SMB_PRTCL_I2C_BLOCK_DATA	0x4a
#define AMD_SMB_PRTCL_PEC		0x80

#ifndef PCI_VENDOR_ID_AMD
#define PCI_VENDOR_ID_AMD 0x1022
#endif

#ifndef PCI_DEVICE_ID_AMD_8111_SMBUS2
#define PCI_DEVICE_ID_AMD_8111_SMBUS2 0x746a
#endif

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t smb_regs[0x100];
    
    int ec_state;
    uint8_t ec_cmd;
    uint8_t ec_addr;
    uint8_t ec_data_out;
    uint8_t ec_status;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

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
    uint64_t val = 0;

    switch (addr) {
    case AMD_EC_DATA:
        val = s->ec_data_out;
        s->ec_status &= ~AMD_EC_SC_OBF;
        break;
    case AMD_EC_SC:
        val = s->ec_status;
        break;
    default:
        val = 0xffffffff;
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case AMD_EC_DATA:
        if (s->ec_state == 1) {
            s->ec_addr = val;
            s->ec_data_out = s->smb_regs[s->ec_addr];
            s->ec_status |= AMD_EC_SC_OBF;
            s->ec_state = 0;
        } else if (s->ec_state == 2) {
            s->ec_addr = val;
            s->ec_state = 3;
        } else if (s->ec_state == 3) {
            s->smb_regs[s->ec_addr] = val;
            if (s->ec_addr == AMD_SMB_PRTCL) {
                s->smb_regs[AMD_SMB_STS] = AMD_SMB_STS_DONE | AMD_SMB_STATUS_OK;
            }
            s->ec_state = 0;
        }
        break;
    case AMD_EC_CMD:
        s->ec_cmd = val;
        if (val == AMD_EC_CMD_RD) {
            s->ec_state = 1;
        } else if (val == AMD_EC_CMD_WR) {
            s->ec_state = 2;
        }
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
    
    memset(s->smb_regs, 0, sizeof(s->smb_regs));
    s->ec_state = 0;
    s->ec_cmd = 0;
    s->ec_addr = 0;
    s->ec_data_out = 0;
    s->ec_status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMD );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_AMD_8111_SMBUS2 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0C05 );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 0x100, .name = "amd8111-smbus-io" };
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "amd8111_smbus2_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(smb_regs, PCIBaseState, 0x100),
        VMSTATE_INT32(ec_state, PCIBaseState),
        VMSTATE_UINT8(ec_cmd, PCIBaseState),
        VMSTATE_UINT8(ec_addr, PCIBaseState),
        VMSTATE_UINT8(ec_data_out, PCIBaseState),
        VMSTATE_UINT8(ec_status, PCIBaseState),
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
