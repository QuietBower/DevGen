/*
 * QEMU PCI device model for Intel uncore Home Agent (HA) performance monitoring unit.
 * Based on driver source: /home/eely/linux-7.1/arch/x86/events/intel/uncore_snbep.c
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

#define TYPE_PCIBASE_DEVICE "knl_uncore_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL_K     0x8086
#define PCI_DEVICE_ID_INTEL_UNC_HA 0x3c46
#define PCI_CLASS_UNCORE          0xFF0000

/* PCI configuration space register offsets for Home Agent uncore unit */
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0   0x40
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1   0x44
#define SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH  0x48
#define SNBEP_PCI_PMON_CTR0                0xa0
#define SNBEP_PCI_PMON_CTL0                0xd8
#define SNBEP_PCI_PMON_BOX_CTL             0xf4

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Hardware Register Shadows */
    struct {
        uint32_t addr_match0;
        uint32_t addr_match1;
        uint32_t opcode_match;
        uint32_t pmon_ctr0;
        uint32_t pmon_ctl0;
        uint32_t pmon_box_ctl;
    } regs;
};

/* Custom PCI config space read handler */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (addr) {
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0:
        val = s->regs.addr_match0;
        break;
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1:
        val = s->regs.addr_match1;
        break;
    case SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH:
        val = s->regs.opcode_match;
        break;
    case SNBEP_PCI_PMON_CTR0:
        val = s->regs.pmon_ctr0;
        break;
    case SNBEP_PCI_PMON_CTL0:
        val = s->regs.pmon_ctl0;
        break;
    case SNBEP_PCI_PMON_BOX_CTL:
        val = s->regs.pmon_box_ctl;
        break;
    default:
        break;
    }
    return val;
}

/* Custom PCI config space write handler */
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    switch (addr) {
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0:
        s->regs.addr_match0 = val;
        break;
    case SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1:
        s->regs.addr_match1 = val;
        break;
    case SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH:
        s->regs.opcode_match = val;
        break;
    case SNBEP_PCI_PMON_CTR0:
        s->regs.pmon_ctr0 = val;
        break;
    case SNBEP_PCI_PMON_CTL0:
        s->regs.pmon_ctl0 = val;
        break;
    case SNBEP_PCI_PMON_BOX_CTL:
        s->regs.pmon_box_ctl = val;
        break;
    default:
        pci_default_write_config(pdev, addr, val, len);
        break;
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all shadow registers to 0 */
    memset(&s->regs, 0, sizeof(s->regs));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL_K);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_UNC_HA);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_UNCORE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No resources to release */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "knl_uncore_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
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
