/* * QEMU IVTV PCI device model for QEMU 8.2.10 * Based on Linux ivtv driver analysis */

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

#define TYPE_PCIBASE_DEVICE "ivtv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware Identifiers derived from driver source */
#define VENDOR_ID 0x4444
#define DEVICE_ID 0x0803
#define CLASS_ID  0x048000

/* Subsystem IDs for Hauppauge WinTV PVR-150 */
#define SUBSYSTEM_VENDOR_ID 0x0070
#define SUBSYSTEM_ID        0x4000

/* Memory layout: single contiguous BAR0, size rounded to power of two */
#define IVTV_ENCODER_SIZE      0x00800000
#define IVTV_DECODER_OFFSET    0x01000000
#define IVTV_DECODER_SIZE      0x00800000
#define IVTV_REG_OFFSET        0x02000000
#define IVTV_REG_SIZE          0x00010000
#define IVTV_TOTAL_SIZE        0x04000000  /* 64 MB, rounded up from 0x02010000 */

/* Register offsets (within MMIO region, relative to reg subregion base) */
#define IVTV_REG_DMAXFER       0x0000
#define IVTV_REG_DECDMAADDR    0x0008
#define IVTV_REG_DMACONTROL    0x0010
#define IVTV_REG_IRQMASK       0x0048

/* Interrupt bit definitions */
#define IVTV_IRQ_DMA_ERR              BIT(18)
#define IVTV_IRQ_ENC_DMA_COMPLETE     BIT(27)
#define IVTV_IRQ_ENC_PIO_COMPLETE     BIT(25)
#define IVTV_IRQ_DMA_READ             BIT(16)
#define IVTV_IRQ_DEC_VSYNC            BIT(10)
#define IVTV_IRQ_DEC_DATA_REQ         BIT(22)
#define IVTV_IRQ_DEC_AUD_MODE_CHG     BIT(24)
#define IVTV_IRQ_ENC_VBI_CAP          BIT(29)
#define IVTV_IRQ_ENC_START_CAP        BIT(31)
#define IVTV_IRQ_ENC_EOS              BIT(30)
#define IVTV_IRQ_ENC_VIM_RST          BIT(28)
#define IVTV_IRQ_DEC_VBI_RE_INSERT    BIT(19)

#define IVTV_IRQ_MASK_INIT (IVTV_IRQ_DMA_ERR | IVTV_IRQ_ENC_DMA_COMPLETE | \
                            IVTV_IRQ_DMA_READ | IVTV_IRQ_ENC_PIO_COMPLETE)

struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR0 container and sub-regions */
    MemoryRegion bar0_container;
    MemoryRegion encoder_mem;
    MemoryRegion decoder_mem;
    MemoryRegion regs_mem;

    /* Interrupt State */
    uint32_t irq_mask;
    uint32_t irq_status;

    /* Hardware Register Shadows */
    struct {
        uint32_t dmaxfer;
        uint32_t decdmaaddr;
        uint32_t dmacontrol;
        uint32_t irqmask;
    } regs;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t unmasked = s->irq_status & ~s->irq_mask;
    if (unmasked) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case IVTV_REG_DMAXFER:
        val = s->regs.dmaxfer;
        break;
    case IVTV_REG_DECDMAADDR:
        val = s->regs.decdmaaddr;
        break;
    case IVTV_REG_DMACONTROL:
        val = s->regs.dmacontrol;
        break;
    case IVTV_REG_IRQMASK:
        val = s->regs.irqmask;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ivtv: unimplemented MMIO read at addr 0x%" PRIx64 "\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case IVTV_REG_DMAXFER:
        s->regs.dmaxfer = val;
        break;
    case IVTV_REG_DECDMAADDR:
        s->regs.decdmaaddr = val;
        break;
    case IVTV_REG_DMACONTROL:
        s->regs.dmacontrol = val;
        break;
    case IVTV_REG_IRQMASK:
        s->regs.irqmask = val;
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "ivtv: unimplemented MMIO write at addr 0x%" PRIx64 " val 0x%" PRIx64 "\n", addr, val);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(&s->regs, 0, sizeof(s->regs));
    s->irq_status = 0;
    s->irq_mask = 0xffffffff;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x4444);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0803);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x048000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x0070);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x4000);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Create single BAR0 containing all resources, size rounded to 64MB */
    memory_region_init(&s->bar0_container, OBJECT(s), "ivtv-bar0", IVTV_TOTAL_SIZE);

    memory_region_init_ram(&s->encoder_mem, OBJECT(s), "ivtv-encoder", IVTV_ENCODER_SIZE, &error_fatal);
    memory_region_add_subregion(&s->bar0_container, 0, &s->encoder_mem);

    memory_region_init_ram(&s->decoder_mem, OBJECT(s), "ivtv-decoder", IVTV_DECODER_SIZE, &error_fatal);
    memory_region_add_subregion(&s->bar0_container, IVTV_DECODER_OFFSET, &s->decoder_mem);

    memory_region_init_io(&s->regs_mem, OBJECT(s), &pcibase_mmio_ops, s, "ivtv-regs", IVTV_REG_SIZE);
    memory_region_add_subregion(&s->bar0_container, IVTV_REG_OFFSET, &s->regs_mem);

    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0_container);
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
    .name = "ivtv_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(irq_mask, PCIBaseState),
        VMSTATE_UINT32(irq_status, PCIBaseState),
        VMSTATE_UINT32(regs.dmaxfer, PCIBaseState),
        VMSTATE_UINT32(regs.decdmaaddr, PCIBaseState),
        VMSTATE_UINT32(regs.dmacontrol, PCIBaseState),
        VMSTATE_UINT32(regs.irqmask, PCIBaseState),
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
