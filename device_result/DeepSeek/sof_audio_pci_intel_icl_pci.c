/*
 * QEMU model for Intel Ice Lake (ICL) Audio DSP (SOF)
 * Based on Linux driver sound/soc/sof/intel/pci-icl.c
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "migration/vmstate.h"
#include "sysemu/dma.h"

#define TYPE_SOF_AUDIO_PCI_INTEL_ICL_PCI "sof_audio_pci_intel_icl_pci"
#define SOF_AUDIO_PCI_INTEL_ICL_PCI(obj) \
    OBJECT_CHECK(SOFIntelICLState, (obj), TYPE_SOF_AUDIO_PCI_INTEL_ICL_PCI)

/* PCI vendor and device IDs from pci-icl.c */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_ICL_AUDIO 0x34c8

/* BAR indices */
#define SOF_ICL_MMIO_BAR 0
#define SOF_ICL_MEM_BAR 2

/* MMIO region size (typical 4k) */
#define SOF_ICL_MMIO_SIZE 0x1000
/* Memory window size (typical 4MB) */
#define SOF_ICL_MEM_SIZE (4 * 1024 * 1024)

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio;
    MemoryRegion mem;
    /* No internal state registers for minimal probe */
} SOFIntelICLState;

static uint64_t sof_icl_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    SOFIntelICLState *s = opaque;
    /* The driver does not read MMIO during probe, but we must return 0 */
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: unexpected read at addr 0x%" HWADDR_PRIx " size %u\n",
                  __func__, addr, size);
    return 0;
}

static void sof_icl_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    SOFIntelICLState *s = opaque;
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: unexpected write at addr 0x%" HWADDR_PRIx " size %u val 0x%" PRIx64 "\n",
                  __func__, addr, size, val);
}

static const MemoryRegionOps sof_icl_mmio_ops = {
    .read = sof_icl_mmio_read,
    .write = sof_icl_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void sof_icl_realize(PCIDevice *pdev, Error **errp)
{
    SOFIntelICLState *s = SOF_AUDIO_PCI_INTEL_ICL_PCI(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Set PCI class code: Multimedia audio controller */
    pci_config_set_class(pci_conf, PCI_CLASS_MULTIMEDIA_AUDIO);

    /* Configure BAR0: MMIO registers */
    memory_region_init_io(&s->mmio, OBJECT(s), &sof_icl_mmio_ops, s,
                          "sof-icl-mmio", SOF_ICL_MMIO_SIZE);
    pci_register_bar(pdev, SOF_ICL_MMIO_BAR,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    /* Configure BAR2: Memory window */
    memory_region_init_ram(&s->mem, OBJECT(s), "sof-icl-mem",
                           SOF_ICL_MEM_SIZE, &error_abort);
    pci_register_bar(pdev, SOF_ICL_MEM_BAR,
                     PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mem);
}

static void sof_icl_reset(DeviceState *dev)
{
    /* Nothing specific to reset */
}

static void sof_icl_init(Object *obj)
{
    /* No additional object initialization needed */
}

static void sof_icl_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = sof_icl_realize;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = PCI_DEVICE_ID_INTEL_ICL_AUDIO;
    k->revision = 0; /* Unknown revision */
    dc->reset = sof_icl_reset;
    dc->desc = "Intel Ice Lake Audio DSP (SOF)";
    /* No specific VM state migration needed for minimal probe */
}

static const TypeInfo sof_icl_info = {
    .name = TYPE_SOF_AUDIO_PCI_INTEL_ICL_PCI,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(SOFIntelICLState),
    .instance_init = sof_icl_init,
    .class_init = sof_icl_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void sof_icl_register_types(void)
{
    type_register_static(&sof_icl_info);
}

type_init(sof_icl_register_types)
