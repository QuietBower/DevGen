/*
 * QEMU PCI device model for Intel Tangier (SOF Audio)
 *
 * Derived strictly from Linux driver: sound/soc/sof/intel/pci-tng.c
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "sof_audio_pci_intel_tng_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register definitions from driver */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x1170
#define CLASS_ID PCI_CLASS_MULTIMEDIA_AUDIO

#define PCI_BAR_SIZE		0x200000
#define IMR_BAR		2
#define DSP_BAR		0

#define SHIM_OFFSET		0x140000
#define SHIM_SIZE_BYT		0x100
#define MBOX_OFFSET		0x144000

#define IRAM_OFFSET		0x0C0000
#define IRAM_SIZE		(80 * 1024)
#define DRAM_OFFSET		0x100000
#define DRAM_SIZE		(160 * 1024)

#define DMAC0_OFFSET		0x098000
#define DMAC1_OFFSET		0x09c000
#define DMAC_SIZE		0x420

#define SSP0_OFFSET		0x0a0000
#define SSP1_OFFSET		0x0a1000
#define SSP2_OFFSET		0x0a2000
#define SSP_SIZE		0x100

#define SHIM_CSR		(SHIM_OFFSET + 0x00)
#define SHIM_IMRD		(SHIM_OFFSET + 0x30)
#define SHIM_IMRX		(SHIM_OFFSET + 0x28)
#define SHIM_IPCD		(SHIM_OFFSET + 0x40)
#define SHIM_IPCX		(SHIM_OFFSET + 0x38)

#define SHIM_BYT_CSR_RST	BIT(0)
#define SHIM_BYT_CSR_VECTOR_SEL	BIT(1)
#define SHIM_BYT_CSR_STALL	BIT(2)
#define SHIM_BYT_CSR_PWAITMODE	BIT(3)

#define SHIM_IMRX_DONE		BIT(0)
#define SHIM_IMRX_BUSY		BIT(1)
#define SHIM_IMRD_DONE		BIT(0)
#define SHIM_IMRD_BUSY		BIT(1)

#define SHIM_IPCX_DONE		BIT(30)
#define SHIM_IPCX_BUSY		BIT(31)
#define SHIM_BYT_IPCX_DONE	BIT_ULL(62)
#define SHIM_BYT_IPCX_BUSY	BIT_ULL(63)

#define SHIM_IPCD_DONE		BIT(30)
#define SHIM_IPCD_BUSY		BIT(31)
#define SHIM_BYT_IPCD_DONE	BIT_ULL(62)
#define SHIM_BYT_IPCD_BUSY	BIT_ULL(63)

/* BAR metadata definition */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* Device State */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* Register shadows */
    uint32_t shim_csr;
    uint32_t shim_imrd;
    uint32_t shim_imrx;
    uint64_t shim_ipcx;
    uint64_t shim_ipcd;

    /* Status fields */
    bool dsp_booted;

    /* Interrupt state */
    qemu_irq irq;
};

/* Forward declarations */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* MemoryRegionOps */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Helper: register a BAR */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* MMIO handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SHIM_CSR:
        val = s->shim_csr;
        break;
    case SHIM_IMRD:
        val = s->shim_imrd;
        break;
    case SHIM_IMRX:
        val = s->shim_imrx;
        break;
    case SHIM_IPCX:
        val = s->shim_ipcx;
        break;
    case SHIM_IPCD:
        val = s->shim_ipcd;
        break;
    default:
        /* Return 0 for unimplemented registers */
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SHIM_CSR:
        s->shim_csr = val;
        /* Handle reset */
        if (val & SHIM_BYT_CSR_RST) {
            pcibase_reset(DEVICE(s));
        }
        break;
    case SHIM_IMRD:
        s->shim_imrd = val;
        break;
    case SHIM_IMRX:
        s->shim_imrx = val;
        break;
    case SHIM_IPCX:
        s->shim_ipcx = val;
        /* Trigger interrupt if DONE bit is set */
        if (val & SHIM_IPCX_DONE) {
            qemu_irq_raise(s->irq);
        }
        break;
    case SHIM_IPCD:
        s->shim_ipcd = val;
        break;
    default:
        break;
    }
}

/* Reset */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    /* Reset register shadows */
    s->shim_csr = 0;
    s->shim_imrd = 0;
    s->shim_imrx = 0;
    s->shim_ipcx = 0;
    s->shim_ipcd = 0;
    s->dsp_booted = false;
}

/* Realize */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Register BARs */
    pcibase_register_bar(pdev, s, &(BARInfo){0, BAR_TYPE_MMIO, PCI_BAR_SIZE, "dsp-bar", false}, errp);
    pcibase_register_bar(pdev, s, &(BARInfo){2, BAR_TYPE_MMIO, PCI_BAR_SIZE, "imr-bar", false}, errp);

    /* Initialize IRQ */
    s->irq = pci_allocate_irq(pdev);

    /* Initial register state - enable BUSY and disable DONE by default */
    s->shim_imrx = SHIM_IMRX_DONE;
}

/* Uninit/cleanup */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Free IRQ */
    if (s->irq) {
        pci_set_irq(pdev, 0);
        qemu_free_irq(s->irq);
    }
}

/* Class init / type registration */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;

    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
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
