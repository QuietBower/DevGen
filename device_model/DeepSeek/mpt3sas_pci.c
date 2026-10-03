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

#define TYPE_PCIBASE_DEVICE "mpt3sas_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1000
#define DEVICE_ID 0x0072
#define CLASS_ID 0x0107

typedef struct {
    uint32_t Doorbell;                 /* 0x00 */
    uint32_t WriteSequence;            /* 0x04 */
    uint32_t HostDiagnostic;           /* 0x08 */
    uint32_t Reserved1;                /* 0x0C */
    uint32_t DiagRWData;               /* 0x10 */
    uint32_t DiagRWAddressLow;         /* 0x14 */
    uint32_t DiagRWAddressHigh;        /* 0x18 */
    uint32_t Reserved2[5];             /* 0x1C */
    uint32_t HostInterruptStatus;      /* 0x30 */
    uint32_t HostInterruptMask;        /* 0x34 */
    uint32_t DCRData;                  /* 0x38 */
    uint32_t DCRAddress;               /* 0x3C */
    uint32_t Reserved3[2];             /* 0x40 */
    uint32_t ReplyFreeHostIndex;       /* 0x48 */
    uint32_t Reserved4[8];             /* 0x4C */
    uint32_t ReplyPostHostIndex;       /* 0x6C */
    uint32_t Reserved5;                /* 0x70 */
    uint32_t HCBSize;                  /* 0x74 */
    uint32_t HCBAddressLow;            /* 0x78 */
    uint32_t HCBAddressHigh;           /* 0x7C */
    uint32_t Reserved6[12];            /* 0x80 */
    uint32_t Scratchpad[4];            /* 0xB0 */
    uint32_t RequestDescriptorPostLow; /* 0xC0 */
    uint32_t RequestDescriptorPostHigh;/* 0xC4 */
    uint32_t AtomicRequestDescriptorPost; /* 0xC8 */
    uint32_t Reserved7[13];            /* 0xCC */
} Mpi2SystemInterfaceRegs;

#define MPI2_IOC_STATE_READY                (0x10000000)
#define MPI2_IOC_STATE_MASK                 (0xF0000000)
#define MPI2_DOORBELL_USED                  (0x08000000)
#define MPI2_HIM_DIM                        (0x00000002)
#define MPI2_HIM_RIM                        (0x00000008)
#define MPI2_HIM_RESET_IRQ_MASK             (0x00010000)
#define MPI2_HIS_REPLY_INTERRUPT            (0x00000008)
#define MPI2_HIS_DOORBELL_INTERRUPT         (0x00000002)

#define MPI2_FUNCTION_IOC_INIT              0x02
#define MPI2_FUNCTION_IOC_FACTS             0x03
#define MPI2_FUNCTION_PORT_FACTS            0x05

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    Mpi2SystemInterfaceRegs regs;

    uint64_t pending_request_addr;
    bool request_valid;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void process_request_descriptor(PCIBaseState *s, uint64_t desc_addr)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t request_header[64];
    uint8_t reply_buffer[128];
    uint64_t reply_addr;
    uint8_t function;
    uint8_t reply_function;
    uint8_t msg_length;

    pci_dma_read(pdev, desc_addr, request_header, sizeof(request_header));

    function = request_header[0];
    reply_addr = ldl_le_p(&request_header[0x0C]);
    reply_addr |= (uint64_t)ldl_le_p(&request_header[0x10]) << 32;

    memset(reply_buffer, 0, sizeof(reply_buffer));
    reply_function = function | 0x80;

    if (function == MPI2_FUNCTION_IOC_INIT) {
        msg_length = 0x14;
        reply_buffer[0x02] = msg_length;
        reply_buffer[0x03] = reply_function;
        pci_dma_write(pdev, reply_addr, reply_buffer, msg_length);
    } else if (function == MPI2_FUNCTION_IOC_FACTS) {
        msg_length = 0x44;
        reply_buffer[0x02] = msg_length;
        reply_buffer[0x03] = reply_function;
        stw_le_p(&reply_buffer[0x04], 0x0200);
        reply_buffer[0x06] = 0;
        reply_buffer[0x07] = 0;
        reply_buffer[0x08] = 0;
        reply_buffer[0x09] = 0;
        reply_buffer[0x0C] = 0; reply_buffer[0x0D] = 0;
        stw_le_p(&reply_buffer[0x0E], 0x0000);
        stl_le_p(&reply_buffer[0x10], 0);
        reply_buffer[0x14] = 8;
        reply_buffer[0x15] = 0;
        reply_buffer[0x16] = 1;
        reply_buffer[0x17] = 1;
        stw_le_p(&reply_buffer[0x18], 256);
        stw_le_p(&reply_buffer[0x1A], 0x0000);
        stl_le_p(&reply_buffer[0x1C], 0x00000000);
        stl_le_p(&reply_buffer[0x20], 0x02000000);
        stw_le_p(&reply_buffer[0x24], 128);
        stw_le_p(&reply_buffer[0x26], 128);
        stw_le_p(&reply_buffer[0x28], 256);
        stw_le_p(&reply_buffer[0x2A], 256);
        stw_le_p(&reply_buffer[0x2C], 1);
        stw_le_p(&reply_buffer[0x2E], 1);
        stw_le_p(&reply_buffer[0x30], 0x0000);
        stw_le_p(&reply_buffer[0x32], 256);
        stw_le_p(&reply_buffer[0x34], 64);
        reply_buffer[0x36] = 64;
        reply_buffer[0x37] = 64;
        stw_le_p(&reply_buffer[0x38], 64);
        stw_le_p(&reply_buffer[0x3A], 64);
        stw_le_p(&reply_buffer[0x3C], 1);
        reply_buffer[0x3E] = 12;
        reply_buffer[0x3F] = 0;
        reply_buffer[0x40] = 0; reply_buffer[0x41] = 0; reply_buffer[0x42] = 0; reply_buffer[0x43] = 0;
        pci_dma_write(pdev, reply_addr, reply_buffer, msg_length);
    } else if (function == MPI2_FUNCTION_PORT_FACTS) {
        msg_length = 0x1B;
        reply_buffer[0x02] = msg_length;
        reply_buffer[0x03] = reply_function;
        reply_buffer[0x06] = 0;
        reply_buffer[0x07] = 0;
        reply_buffer[0x08] = 0;
        reply_buffer[0x09] = 0;
        stw_le_p(&reply_buffer[0x0E], 0x0000);
        stl_le_p(&reply_buffer[0x10], 0);
        reply_buffer[0x14] = 0;
        reply_buffer[0x15] = 0;
        stw_le_p(&reply_buffer[0x16], 0);
        stw_le_p(&reply_buffer[0x18], 128);
        stw_le_p(&reply_buffer[0x1A], 0);
        pci_dma_write(pdev, reply_addr, reply_buffer, msg_length);
    } else {
        msg_length = 4;
        reply_buffer[0x02] = msg_length;
        reply_buffer[0x03] = reply_function;
        pci_dma_write(pdev, reply_addr, reply_buffer, msg_length);
    }

    s->regs.ReplyPostHostIndex++;
    s->regs.HostInterruptStatus |= MPI2_HIS_REPLY_INTERRUPT;
    s->intr_status |= MPI2_HIS_REPLY_INTERRUPT;
    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    switch (addr) {
    case 0x00:
        val = s->regs.Doorbell;
        break;
    case 0x04:
        val = s->regs.WriteSequence;
        break;
    case 0x08:
        val = s->regs.HostDiagnostic;
        break;
    case 0x30:
        val = s->regs.HostInterruptStatus;
        break;
    case 0x34:
        val = s->regs.HostInterruptMask;
        break;
    case 0x6C:
        val = s->regs.ReplyPostHostIndex;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00: /* Doorbell */
        /* Writing to Doorbell may trigger request processing if a pending descriptor exists.
         * For now, we keep existing logic; a future enhancement can support direct command submission.
         */
        if (s->request_valid) {
            process_request_descriptor(s, s->pending_request_addr);
            s->request_valid = false;
        }
        break;
    case 0x04: /* WriteSequence */
        if (size == 4) {
            s->regs.WriteSequence = (uint32_t)val;
            if (s->regs.WriteSequence == 0x0000000d) {
                s->regs.Doorbell = MPI2_IOC_STATE_READY;
                s->regs.HostInterruptStatus |= MPI2_HIM_RESET_IRQ_MASK;
                s->intr_status |= MPI2_HIM_RESET_IRQ_MASK;
                pcibase_update_irq(s);
            }
        }
        break;
    case 0x30: /* HostInterruptStatus - W1C */
        s->regs.HostInterruptStatus &= ~val;
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case 0x34: /* HostInterruptMask */
        s->regs.HostInterruptMask = val;
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case 0x48: /* ReplyFreeHostIndex */
        s->regs.ReplyFreeHostIndex = val;
        break;
    case 0x6C: /* ReplyPostHostIndex */
        s->regs.ReplyPostHostIndex = val;
        break;
    case 0x74: /* HCBSize */
        s->regs.HCBSize = val;
        break;
    case 0x78: /* HCBAddressLow */
        s->regs.HCBAddressLow = val;
        break;
    case 0x7C: /* HCBAddressHigh */
        s->regs.HCBAddressHigh = val;
        break;
    case 0xC0: /* RequestDescriptorPostLow */
        s->regs.RequestDescriptorPostLow = val;
        s->pending_request_addr = (s->pending_request_addr & 0xFFFFFFFF00000000ULL) | val;
        break;
    case 0xC4: /* RequestDescriptorPostHigh */
        s->regs.RequestDescriptorPostHigh = val;
        s->pending_request_addr = (s->pending_request_addr & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
        s->request_valid = true;
        /* Hardware typically rings the doorbell automatically when the high part is written,
         * so process the request immediately.
         */
        if (s->request_valid) {
            process_request_descriptor(s, s->pending_request_addr);
            s->request_valid = false;
        }
        break;
    case 0xC8: /* AtomicRequestDescriptorPost */
        s->regs.AtomicRequestDescriptorPost = val;
        /* Atomic post is a 32-bit descriptor mechanism; not fully implemented but may be used.
         * For now, we ignore it.
         */
        break;
    default:
        /* Scratchpad and others */
        if (addr >= 0xB0 && addr < 0xC0) {
            int idx = (addr - 0xB0) / 4;
            s->regs.Scratchpad[idx] = val;
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.Doorbell = MPI2_IOC_STATE_READY | MPI2_DOORBELL_USED;
    s->intr_status = 0;
    s->intr_mask = MPI2_HIM_DIM | MPI2_HIM_RIM | MPI2_HIM_RESET_IRQ_MASK;
    s->regs.HostInterruptMask = s->intr_mask;
    s->request_valid = false;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x200;
    s->bar_info[0].name = "mpt3sas-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->intr_mask = MPI2_HIM_DIM | MPI2_HIM_RIM | MPI2_HIM_RESET_IRQ_MASK;
    s->regs.HostInterruptMask = s->intr_mask;
    s->regs.Doorbell = MPI2_IOC_STATE_READY | MPI2_DOORBELL_USED;
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
    .name = "mpt3sas_pci",
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
