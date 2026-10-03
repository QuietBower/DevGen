/* QEMU model of Marvell Octeon EP VF PCI device */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TYPE_OCTEP_VF_PCI "octep-vf-pci"
OBJECT_DECLARE_SIMPLE_TYPE(OctepVFState, OCTEP_VF_PCI)

/* Register offsets - inferred from provided source */
#define OCTEP_VF_MBOX_DATA(x)   (0x00010210 | ((x) << 17))

/*
 * The following register offsets are missing from provided sources.
 * Placeholder definitions allow compilation but produce incorrect behavior.
 * Real values are required to pass the mailbox version check.
 */
#define OCTEP_VF_MBOX_CTRL             0xdead
#define OCTEP_VF_MBOX_STATUS           0xdead
#define OCTEP_VF_MBOX_PF_VERSION       0xdead
#define OCTEP_VF_MBOX_CTRL_SEND        0xdead
#define OCTEP_VF_MBOX_STATUS_DONE      0xdead
#define OCTEP_VF_MBOX_STATUS_NACK      0xdead

#define BAR_0_SIZE (1 * MiB)
#define OCTEP_VF_MBOX_DATA_COUNT 32

typedef struct OctepVFState {
    PCIDevice pdev;
    MemoryRegion mmio;
    uint32_t mbox_data[OCTEP_VF_MBOX_DATA_COUNT];
    uint32_t mbox_status;
    uint32_t mbox_ctrl;
} OctepVFState;

static void process_mbox_command(OctepVFState *s)
{
    /* Handle mailbox command: currently only version request is handled */
    /* Set the version in the first data register (response) */
    s->mbox_data[0] = OCTEP_VF_MBOX_PF_VERSION;
    /* Mark command as done */
    s->mbox_status |= OCTEP_VF_MBOX_STATUS_DONE;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    OctepVFState *s = opaque;
    uint64_t val = 0;

    if (addr >= OCTEP_VF_MBOX_DATA(0) && addr < OCTEP_VF_MBOX_DATA(OCTEP_VF_MBOX_DATA_COUNT)) {
        int index = (addr - OCTEP_VF_MBOX_DATA(0)) >> 17;
        if (index < OCTEP_VF_MBOX_DATA_COUNT) {
            val = s->mbox_data[index];
        }
    } else if (addr == OCTEP_VF_MBOX_STATUS) {
        val = s->mbox_status;
    } else if (addr == OCTEP_VF_MBOX_CTRL) {
        val = s->mbox_ctrl;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    OctepVFState *s = opaque;

    if (addr >= OCTEP_VF_MBOX_DATA(0) && addr < OCTEP_VF_MBOX_DATA(OCTEP_VF_MBOX_DATA_COUNT)) {
        int index = (addr - OCTEP_VF_MBOX_DATA(0)) >> 17;
        if (index < OCTEP_VF_MBOX_DATA_COUNT) {
            s->mbox_data[index] = val;
        }
    } else if (addr == OCTEP_VF_MBOX_CTRL) {
        s->mbox_ctrl = val;
        if (val & OCTEP_VF_MBOX_CTRL_SEND) {
            process_mbox_command(s);
        }
    } else if (addr == OCTEP_VF_MBOX_STATUS) {
        s->mbox_status &= ~(val & (OCTEP_VF_MBOX_STATUS_DONE | OCTEP_VF_MBOX_STATUS_NACK));
        /* Clear SEND bit? No, the driver toggles that via control. */
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void octep_vf_pci_realize(PCIDevice *pdev, Error **errp)
{
    OctepVFState *s = OCTEP_VF_PCI(pdev);

    pci_set_word(pdev->config + PCI_COMMAND, PCI_COMMAND_MEMORY);
    pci_set_long(pdev->config + PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_SPACE_MEMORY);

    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "octep-vf-mmio", BAR_0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    /* MSI-X not used */
}

static void octep_vf_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);
    k->realize = octep_vf_pci_realize;
    k->vendor_id = 0x177d;
    k->device_id = 0xa0a3;
    k->revision = 0;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    k->conventional = true;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo octep_vf_pci_info = {
    .name = TYPE_OCTEP_VF_PCI,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(OctepVFState),
    .class_init = octep_vf_pci_class_init,
};

static void octep_vf_pci_register_types(void)
{
    type_register_static(&octep_vf_pci_info);
}
type_init(octep_vf_pci_register_types)
