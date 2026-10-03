/*
 * QEMU model of HP Smart Array Controller (hpsa) for guest driver probing
 *
 * Based on Linux driver drivers/scsi/hpsa.c (init/probe path)
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

#define TYPE_PCIBASE_DEVICE "hpsa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware IDs extracted from driver pci_device_id table (first entry) */
#define HP_VENDOR_ID 0x103C
#define HP_CISSE_DEVICE_ID 0x323a
#define CLASS_ID PCI_CLASS_STORAGE_RAID

/* Register Offsets */
#define SA5_REQUEST_PORT_OFFSET  0x40
#define SA5_REPLY_PORT_OFFSET    0x44
#define SA5_DOORBELL             0x20
#define SA5_SCRATCHPAD_OFFSET    0xB0
#define SA5_CTMEM_OFFSET         0xB8
#define SA5_CTCFG_OFFSET         0xB4
#define SA5_REPLY_INTR_MASK_OFFSET 0x34
#define SA5_INTR_OFF             0x08

/* Doorbell bits */
#define CFGTBL_ChangeReq         0x00000001
#define DOORBELL_CLEAR_EVENTS    0x00000040

/* Transport modes */
#define CFGTBL_Trans_Simple      0x00000002

/* Firmware ready signature */
#define HPSA_FIRMWARE_READY      0xffff0000

/* Configuration table signature */
#define CISS_SIGNATURE "CISS"

/* BAR layout */
#define BAR0_SIZE               0x4000
#define REG_REGION_SIZE         0x2000
#define CFGTABLE_OFFSET         0x2000

/* State structure */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar0;
    uint8_t *cfgmem;
    size_t cfgmem_size;

    /* Interrupt state */
    bool has_msi;
    uint32_t intr_mask;
};

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (addr < s->cfgmem_size) {
        switch (size) {
        case 1:
            val = s->cfgmem[addr];
            break;
        case 2:
            val = lduw_le_p(&s->cfgmem[addr]);
            break;
        case 4:
            val = ldl_le_p(&s->cfgmem[addr]);
            break;
        case 8:
            val = ldq_le_p(&s->cfgmem[addr]);
            break;
        default:
            val = 0;
        }
    }
    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < s->cfgmem_size) {
        /* Write to internal buffer */
        switch (size) {
        case 1:
            s->cfgmem[addr] = (uint8_t)val;
            break;
        case 2:
            stw_le_p(&s->cfgmem[addr], val);
            break;
        case 4:
            stl_le_p(&s->cfgmem[addr], val);
            break;
        case 8:
            stq_le_p(&s->cfgmem[addr], val);
            break;
        default:
            break;
        }

        /* Handle side effects for doorbell register */
        if (addr == SA5_DOORBELL && size == 4) {
            uint32_t doorbell_val = ldl_le_p(&s->cfgmem[SA5_DOORBELL]);
            if (doorbell_val & CFGTBL_ChangeReq) {
                /* Process mode change: copy TransportRequest to TransportActive */
                uint32_t trans_req = ldl_le_p(&s->cfgmem[CFGTABLE_OFFSET + 16]);
                stl_le_p(&s->cfgmem[CFGTABLE_OFFSET + 12], trans_req & CFGTBL_Trans_Simple);
                /* Clear the ChangeReq bit (controller acknowledges) */
                doorbell_val &= ~CFGTBL_ChangeReq;
                stl_le_p(&s->cfgmem[SA5_DOORBELL], doorbell_val);
            }
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize the CFGMEM buffer with default values */
    memset(s->cfgmem, 0, s->cfgmem_size);

    /* Registers area defaults */
    /* SA5_SCRATCHPAD => FIRMWARE_READY */
    stl_le_p(&s->cfgmem[SA5_SCRATCHPAD_OFFSET], HPSA_FIRMWARE_READY);
    /* SA5_CTCFG => BAR0 low 16 bits (0x0010) */
    stl_le_p(&s->cfgmem[SA5_CTCFG_OFFSET], 0x00000010);
    /* SA5_CTMEM => offset to config table (0x2000) */
    stl_le_p(&s->cfgmem[SA5_CTMEM_OFFSET], CFGTABLE_OFFSET);
    /* SA5_REPLY_INTR_MASK_OFFSET initial value 0 (interrupts disabled) */
    stl_le_p(&s->cfgmem[SA5_REPLY_INTR_MASK_OFFSET], 0);

    /* Configuration table area (starting at CFGTABLE_OFFSET) */
    /* Signature "CISS" */
    memcpy(&s->cfgmem[CFGTABLE_OFFSET], CISS_SIGNATURE, 4);
    /* TransportSupport: simple mode only */
    stl_le_p(&s->cfgmem[CFGTABLE_OFFSET + 8], CFGTBL_Trans_Simple);
    /* TransportActive: initially 0 */
    stl_le_p(&s->cfgmem[CFGTABLE_OFFSET + 12], 0);
    /* CmdsOutMax: some arbitrary value */
    stl_le_p(&s->cfgmem[CFGTABLE_OFFSET + 32], 1024);
    /* MaxScatterGatherElements */
    stl_le_p(&s->cfgmem[CFGTABLE_OFFSET + 68], 32);
    /* MaxPerformantModeCommands */
    stl_le_p(&s->cfgmem[CFGTABLE_OFFSET + 84], 1024);

    s->intr_mask = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  HP_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  HP_CISSE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Enable MSI to satisfy guest IRQ allocation */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        /* Proceed even if MSI fails; guest will use INTx */
    }
    s->has_msi = msi_enabled(pdev);

    /* Allocate the combined MMIO + config table region */
    s->cfgmem_size = BAR0_SIZE;
    s->cfgmem = g_malloc0(s->cfgmem_size);

    memory_region_init_io(&s->bar0, OBJECT(s), &pcibase_mmio_ops, s,
                          "hpsa-mmio", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    g_free(s->cfgmem);
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "hpsa_pci",
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
