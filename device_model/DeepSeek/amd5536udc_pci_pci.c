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

#define TYPE_PCIBASE_DEVICE "amd5536udc_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs */
#define PCI_VENDOR_ID_AMD 0x1022
#define AMD5536UDC_DEVICE_ID 0x2096
#define AMD5536UDC_CLASS_ID 0x0cfe  /* USB Device Controller (subclass 0xfe, base class 0x0c) */

/* Register offsets */
#define UDC_EPREGS_ADDR 0x000
#define UDC_DEVCFG_ADDR 0x400
#define UDC_CSR_ADDR    0x500
#define UDC_RXFIFO_ADDR 0x800
#define UDC_TXFIFO_ADDR 0xc00

/* Register block sizes */
#define UDC_EPREGS_SIZE 0x400
#define UDC_DEVCFG_SIZE 0x100
#define UDC_CSR_SIZE    0x100
#define UDC_RXFIFO_SIZE 0x400
#define UDC_TXFIFO_SIZE 0x600

/* Bit definitions */
#define UDC_BIT(bit) (1 << (bit))
#define AMD_BIT(bit_stub_name) (1 << bit_stub_name)

/* Device Configuration Register (DEVCFG) bits */
#define UDC_DEVCFG_SOFTRESET 31
#define UDC_DEVCFG_SP 3
#define UDC_DEVCFG_RWKP 2
#define UDC_DEVCFG_SPD_FS 0x1
#define UDC_DEVCFG_SPD_HS 0x0
#define UDC_DEVCFG_CSR_PRG 17

/* Device Control Register (DEVCTL) bits */
#define UDC_DEVCTL_SD 10
#define UDC_DEVCTL_TDE 3
#define UDC_DEVCTL_RDE 2
#define UDC_DEVCTL_RES 0
#define UDC_DEVCTL_MODE 9
#define UDC_DEVCTL_BF 6
#define UDC_DEVCTL_DU 4
#define UDC_DEVCTL_CSR_DONE 13

/* Device Status Register (DEVSTS) bits */
#define UDC_DEVSTS_ENUM_SPEED_HIGH 0
#define UDC_DEVSTS_ENUM_SPEED_FULL 1

/* Device Interrupt bits */
#define UDC_DEVINT_SC 0
#define UDC_DEVINT_SI 1
#define UDC_DEVINT_ES 2
#define UDC_DEVINT_UR 3
#define UDC_DEVINT_US 4
#define UDC_DEVINT_SOF 5
#define UDC_DEVINT_ENUM 6
#define UDC_DEVINT_SVC 7
#define UDC_DEV_MSK_DISABLE 0x7f

/* Endpoint Interrupt bits */
#define UDC_EPINT_IN_EP0 0
#define UDC_EPINT_OUT_EP0 16
#define UDC_EPINT_MSK_DISABLE_ALL 0xffffffff

/* Endpoint Control bits */
#define UDC_EPCTL_S 0
#define UDC_EPCTL_F 1
#define UDC_EPCTL_P 3
#define UDC_EPCTL_NAK 6
#define UDC_EPCTL_SNAK 7
#define UDC_EPCTL_CNAK 8

/* Endpoint Status bits */
#define UDC_EPSTS_IN 6

/* DMA bit fields */
#define UDC_DMA_OUT_STS_L 27
#define UDC_DMA_OUT_STS_BS_HOST_READY 0
#define UDC_DMA_STP_STS_BS_HOST_BUSY 3
#define UDC_DMA_IN_STS_BS_HOST_READY 0
#define UDC_DMA_IN_STS_L 27
#define UDC_DMA_STP_STS_BS_DMA_DONE 2
#define UDC_DMA_STP_STS_BS_HOST_READY 0

/* Endpoint count */
#define UDC_EP_NUM 32
#define UDC_EPIN_NUM 16
#define UDC_EP0OUT_IX 16
#define UDC_EP0IN_IX 0
#define UDC_EPOUT_IX 18
#define UDC_EPIN_IX 2

/* FIFO related */
#define UDC_TXFIFO_SIZE 0x600
#define UDC_RXFIFO_SIZE 0x400

/* Misc */
#define UDC_DWORD_BYTES 4
#define UDC_BITS_PER_BYTE 8
#define UDC_BYTE_MASK 0xff
#define DMA_DONT_USE (~(dma_addr_t)0)

/* Helper macros from driver */
#define AMD_ADDBITS(u32Val, bitfield_val, bitfield_stub_name) \
    (((u32Val) & (~((u32)bitfield_stub_name##_MASK))) | \
     (((bitfield_val) << ((u32)bitfield_stub_name##_OFS)) & ((u32)bitfield_stub_name##_MASK)))
#define AMD_GETBITS(u32Val, bitfield_stub_name) \
    ((u32Val & ((u32)bitfield_stub_name##_MASK)) >> ((u32)bitfield_stub_name##_OFS))
#define AMD_CLEAR_BIT(bit_stub_name) (~AMD_BIT(bit_stub_name))
#define AMD_UNMASK_BIT(bit_stub_name) (~AMD_BIT(bit_stub_name))

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t devcfg;
    uint32_t devctl;
    uint32_t devsts;
    uint32_t devirqsts;
    uint32_t devirqmsk;
    uint32_t epirqsts;
    uint32_t epirqmsk;
    uint32_t csr;
    uint32_t ep_regs[32][8];

    /* DMA Context */
    dma_addr_t dma_stp_addr;
    dma_addr_t dma_data_addr;
    uint32_t dma_cmd;
    uint32_t dma_status;

    /* Status flags handled in regs */
    uint32_t status_flags;

    /* State used to handle reset sequences */
    bool soft_reset;

    /* Flat MMIO region for all addresses */
    uint8_t mmio[0x2000];
};

/* MMIO Read/Write handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size <= 0x2000) {
        /* Since we declared DEVICE_LITTLE_ENDIAN, QEMU expects read to return
         * data in host endianness, but memory accesses are byte-based. We
         * treat the mmio array as little-endian and build the return value
         * accordingly. */
        switch (size) {
        case 1:
            val = s->mmio[addr];
            break;
        case 2:
            val = lduw_le_p(&s->mmio[addr]);
            break;
        case 4:
            val = ldl_le_p(&s->mmio[addr]);
            break;
        case 8:
            val = ldq_le_p(&s->mmio[addr]);
            break;
        default:
            memcpy(&val, &s->mmio[addr], size);
            break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > 0x2000) {
        return;
    }

    /* Handle soft reset trigger */
    if (addr == UDC_DEVCFG_ADDR && size == 4 && (val & AMD_BIT(UDC_DEVCFG_SOFTRESET))) {
        /* Perform soft reset - clear only the control register area (0x000-0x5FF),
         * preserving read-only registers that might be initialized elsewhere. */
        memset(&s->mmio[0x000], 0, 0x600);
        return; /* The SOFTRESET bit is not actually stored; cfg is cleared. */
    }

    /* Handle write-1-to-clear for device interrupt status */
    if (addr == UDC_DEVCFG_ADDR + 0xc && size == 4) {
        uint32_t old_val = ldl_le_p(&s->mmio[addr]);
        val = old_val & ~val; /* clear bits set in val */
    }
    /* Handle write-1-to-clear for endpoint interrupt status */
    if (addr == UDC_DEVCFG_ADDR + 0x14 && size == 4) {
        uint32_t old_val = ldl_le_p(&s->mmio[addr]);
        val = old_val & ~val; /* clear bits set in val */
    }

    /* Store the value into the mmio buffer */
    switch (size) {
    case 1:
        s->mmio[addr] = (uint8_t)val;
        break;
    case 2:
        stw_le_p(&s->mmio[addr], (uint16_t)val);
        break;
    case 4:
        stl_le_p(&s->mmio[addr], (uint32_t)val);
        break;
    case 8:
        stq_le_p(&s->mmio[addr], val);
        break;
    default:
        memcpy(&s->mmio[addr], &val, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO is not used by this driver; stub */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* PIO is not used */
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

    /* Clear the flat MMIO region */
    memset(s->mmio, 0, sizeof(s->mmio));
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

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000;
    s->bar_info[0].name = "amd5536udc-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize mmio region to zeros */
    memset(s->mmio, 0, sizeof(s->mmio));

    /* Set interrupt pin to INTA# so PCI subsystem assigns an IRQ */
    pci_config_set_interrupt_pin(pci_conf, 1);
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
    .name = "amd5536udc_pci_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_BUFFER(mmio, PCIBaseState),
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

    /* Set PCI identification data to match the expected driver IDs */
    k->vendor_id = PCI_VENDOR_ID_AMD;
    k->device_id = AMD5536UDC_DEVICE_ID;
    k->class_id  = AMD5536UDC_CLASS_ID;
    k->revision  = 0x01;
    k->subsystem_vendor_id = PCI_VENDOR_ID_AMD;
    k->subsystem_id        = AMD5536UDC_DEVICE_ID;
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
