/*
 * Generated QEMU PCI device model for arcmsr (Type A, VID=0x17d3, DID=0x1110)
 * Phase 2: Full functional implementation for probe.
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "arcmsr_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification extracted from driver pci_device_id table, first entry */
#define PCI_VENDOR_ID_ARECA          0x17D3
#define PCI_DEVICE_ID_ARECA_1110     0x1110

/* BAR resources: type A uses only BAR0, size derived from struct MessageUnit_A */
#define BAR0_SIZE                    0x1000

/* Register Offsets from MessageUnit_A structure */
enum ArcmsrRegOffsets {
    REG_INBOUND_MSGADDR0  = 0x0010,
    REG_INBOUND_MSGADDR1  = 0x0014,
    REG_OUTBOUND_MSGADDR0 = 0x0018,
    REG_OUTBOUND_MSGADDR1 = 0x001C,
    REG_INBOUND_DOORBELL  = 0x0020,
    REG_INBOUND_INTSTATUS = 0x0024,
    REG_INBOUND_INTMASK   = 0x0028,
    REG_OUTBOUND_DOORBELL = 0x002C,
    REG_OUTBOUND_INTSTATUS= 0x0030,
    REG_OUTBOUND_INTMASK  = 0x0034,
    REG_INBOUND_QUEUEPORT = 0x0040,
    REG_OUTBOUND_QUEUEPORT= 0x0044,
    REG_MESSAGE_RWBUFFER  = 0x0A00,
    REG_MESSAGE_WBUFFER   = 0x0E00,
    REG_MESSAGE_RBUFFER   = 0x0F00
};

/* Firmware-related definitions from driver */
#define ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK    0x80000000
#define ARCMSR_MU_OUTBOUND_MESSAGE0_INT      0x01
#define ARCMSR_SIGNATURE_GET_CONFIG          0x87974060

/* Internal state maintained for the virtual device */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[1];   /* only one BAR used (index 0) */
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t outbound_intstatus;
    uint32_t outbound_intmask;
    uint32_t inbound_doorbell;
    uint32_t outbound_doorbell;

    /* Hardware Register Shadows */
    uint32_t mmio_regs[BAR0_SIZE / 4];

    /* DMA Context */
    uint64_t dma_mask;
    struct {
        dma_addr_t addr;
        size_t size;
    } dma_buf;   /* placeholder for DMA transfers */

    uint32_t status;      /* Operational status flags */
    uint8_t reset_state;
    uint8_t power_state;
    uint32_t fw_ready;     /* Firmware ready flag */
};

/* Forward declarations */
static void pcibase_update_irq(PCIBaseState *s);

/* Helper to handle inbound message writes to trigger firmware response */
static void arcmsr_handle_inbound_msg(PCIBaseState *s, uint32_t val)
{
    /* Any write to inbound_msgaddr0 signals the firmware to set MESSAGE0_INT */
    s->mmio_regs[REG_OUTBOUND_INTSTATUS / 4] |= ARCMSR_MU_OUTBOUND_MESSAGE0_INT;
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t idx = addr >> 2;

    if (addr >= BAR0_SIZE) {
        return ~0ULL;
    }

    switch (addr) {
    case REG_OUTBOUND_MSGADDR1:
        val = s->mmio_regs[idx] & ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK;
        break;
    case REG_OUTBOUND_INTSTATUS:
        val = s->mmio_regs[idx] & s->mmio_regs[REG_OUTBOUND_INTMASK / 4];
        break;
    case REG_OUTBOUND_QUEUEPORT:
        val = 0xFFFFFFFF;   /* empty completion queue */
        break;
    default:
        val = s->mmio_regs[idx];
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t idx = addr >> 2;

    if (addr >= BAR0_SIZE) {
        return;
    }

    switch (addr) {
    case REG_INBOUND_MSGADDR0:
        s->mmio_regs[idx] = val;
        arcmsr_handle_inbound_msg(s, val);
        break;
    case REG_INBOUND_MSGADDR1:
    case REG_OUTBOUND_MSGADDR0:
    case REG_INBOUND_DOORBELL:
    case REG_INBOUND_INTSTATUS:
    case REG_INBOUND_INTMASK:
        s->mmio_regs[idx] = val;
        break;
    case REG_OUTBOUND_DOORBELL:
        /* Driver writes the read value back to clear bits */
        s->mmio_regs[idx] &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case REG_OUTBOUND_INTSTATUS:
        /* Write-1-to-clear */
        s->mmio_regs[idx] &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case REG_OUTBOUND_INTMASK:
        s->mmio_regs[idx] = val;
        pcibase_update_irq(s);
        break;
    case REG_INBOUND_QUEUEPORT:
        /* Ignore command posting during probe */
        break;
    default:
        /* Allow write to message buffers etc. */
        s->mmio_regs[idx] = val;
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

/* Interrupt update logic */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t intstatus = s->mmio_regs[REG_OUTBOUND_INTSTATUS / 4];
    uint32_t intmask   = s->mmio_regs[REG_OUTBOUND_INTMASK / 4];
    if (intstatus & intmask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Initialize configuration data in message_rwbuffer */
static void arcmsr_init_config_area(PCIBaseState *s)
{
    /* Offsets within message_rwbuffer (0xA00 base) */
    uint32_t *buf = &s->mmio_regs[REG_MESSAGE_RWBUFFER / 4];
    int i;

    /* Zero entire area first */
    for (i = 0; i < (0x400 / 4); i++) {
        buf[i] = 0;
    }

    /* Fill known structure (index -> offset in buf) */
    buf[0]  = ARCMSR_SIGNATURE_GET_CONFIG;  /* signature */
    buf[1]  = 0;                            /* firm_request_len */
    buf[2]  = 128;                          /* firm_numbers_queue */
    buf[3]  = 0;                            /* firm_sdram_size */
    buf[4]  = 0;                            /* firm_hd_channels */
    /* model: 2 DWORDs at index 15-16 */
    buf[15] = 0x30304952; /* "RI00" little endian */
    buf[16] = 0x30303031; /* "1000" */
    /* version: 4 DWORDs at index 17-20 */
    buf[17] = 0x302e3156; /* "V1.0" */
    buf[18] = 0x0;
    buf[19] = 0x0;
    buf[20] = 0x0;
    /* device_map: 4 DWORDs at index 21-24 */
    buf[21] = 0;
    buf[22] = 0;
    buf[23] = 0;
    buf[24] = 0;
    buf[25] = 0x00000103;  /* firm_cfg_version */
    /* buf[30] is firm_PicStatus, unused for Type A */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    s->status = 0;
    s->fw_ready = 0;

    /* Set firmware ready flag */
    s->mmio_regs[REG_OUTBOUND_MSGADDR1 / 4] = ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK;

    /* Pre-fill message_rwbuffer with configuration */
    arcmsr_init_config_area(s);

    /* Interrupt mask initially all zero */
    s->mmio_regs[REG_OUTBOUND_INTMASK / 4] = 0;
    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, int idx, Error **errp)
{
    MemoryRegion *mr = &s->bar_regions[idx];
    memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, "arcmsr-mmio", BAR0_SIZE);
    pci_register_bar(pdev, idx, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ARECA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_ARECA_1110);
    pci_config_set_class(pci_conf, PCI_CLASS_STORAGE_RAID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    pcibase_register_bar(pdev, s, 0, errp);

    /* Interrupt support: no MSI/MSI-X requested by driver yet */
    s->has_msi = s->has_msix = false;

    /* DMA configuration placeholder */
    s->dma_mask = (1ULL << 32) - 1;  /* 32-bit default */

    /* Trigger reset to set initial state */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    /* Free buffers, stop timers, etc. -- none needed for Type A probe */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "arcmsr_pci",
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
    static const TypeInfo pcibase_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init = pcibase_class_init,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)
