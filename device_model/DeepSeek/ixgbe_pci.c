/* QEMU Intel 82598 10GbE network device emulation */
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

#define TYPE_PCIBASE_DEVICE "ixgbe_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets for 82598 */
#define IXGBE_CTRL      0x00000
#define IXGBE_STATUS    0x00008
#define IXGBE_CTRL_EXT  0x00018
#define IXGBE_ESDP      0x00020
#define IXGBE_EODSDP    0x00028
#define IXGBE_FRTIMER   0x00048
#define IXGBE_TCPTIMER  0x0004C
#define IXGBE_LEDCTL    0x00200
#define IXGBE_MSIXT     0x00000
#define IXGBE_MSIXPBA   0x02000
#define IXGBE_EEC       0x10010
#define IXGBE_FLA       0x1001C
#define IXGBE_GRC       0x10020
#define IXGBE_FWSM      0x10030
#define IXGBE_FACTPS    0x10034
#define IXGBE_EERD      0x10014
#define IXGBE_EEMNGCTL  0x10110
#define IXGBE_EEMNGDATA 0x10114
#define IXGBE_FLMNGCTL  0x10118
#define IXGBE_FLMNGCNT  0x10120
#define IXGBE_FLMNGDATA 0x1011C
#define IXGBE_FLOP      0x1013C
#define IXGBE_EICR      0x00800
#define IXGBE_EICS      0x00808
#define IXGBE_EIMS      0x00880
#define IXGBE_EIMC      0x00888
#define IXGBE_EIAC      0x00810
#define IXGBE_EIAM      0x00890
#define IXGBE_EITR(_i)  (((_i) <= 23) ? (0x00820 + ((_i) * 4)) : \
                         (0x012300 + (((_i) - 24) * 4)))
#define IXGBE_EITRSEL   0x00894
#define IXGBE_IVAR(_i)  (0x00900 + ((_i) * 4))
#define IXGBE_IVAR_MISC 0x00A00
#define IXGBE_GPIE      0x00898
#define IXGBE_EICS_EX(_i)   (0x00A90 + (_i) * 4)
#define IXGBE_EIMS_EX(_i)   (0x00AA0 + (_i) * 4)
#define IXGBE_EIMC_EX(_i)   (0x00AB0 + (_i) * 4)
#define IXGBE_EIAM_EX(_i)   (0x00AD0 + (_i) * 4)

/* EEPROM size */
#define EEPROM_SIZE 256

/* Device state */
struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    MemoryRegion msix_mr;

    uint16_t eeprom[EEPROM_SIZE];
    bool eerd_active;
    uint32_t eerd_addr;

    uint32_t ctrl;
    uint32_t status;
    uint32_t ctrl_ext;
    uint32_t esdp;
    uint32_t fwsm;
    uint32_t gpie;
    uint32_t eitr[64];
    uint32_t ivar_misc;
    uint32_t ivar[32];
};

/* Helper to compute EITR index */
static int ixgbe_eitr_idx(hwaddr addr) {
    if (addr >= 0x00820 && addr < 0x00820 + 24 * 4) {
        return (addr - 0x00820) / 4;
    } else if (addr >= 0x012300 && addr < 0x012300 + 40 * 4) {
        return 24 + (addr - 0x012300) / 4;
    }
    return -1;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return 0xFFFFFFFF;
    }

    switch (addr) {
    case IXGBE_CTRL:
        return s->ctrl;
    case IXGBE_STATUS:
        return 0;
    case IXGBE_CTRL_EXT:
        return s->ctrl_ext;
    case IXGBE_ESDP:
        return s->esdp;
    case IXGBE_EEC:
        return 0x100;
    case IXGBE_FWSM:
        return 0x800E;
    case IXGBE_EERD:
        if (s->eerd_active) {
            uint16_t data = s->eeprom[s->eerd_addr & (EEPROM_SIZE - 1)];
            s->eerd_active = false;
            return (uint32_t)(data << 16) | 1;
        }
        return 0;
    case IXGBE_GPIE:
        return s->gpie;
    default:
        if (addr >= IXGBE_EITR(0) && addr < IXGBE_EITR(64)) {
            int idx = ixgbe_eitr_idx(addr);
            if (idx >= 0 && idx < 64) {
                return s->eitr[idx];
            }
        }
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case IXGBE_CTRL:
        s->ctrl = val;
        if (val & 0x00000001) {
            memset(s->eitr, 0, sizeof(s->eitr));
            memset(s->ivar, 0, sizeof(s->ivar));
            s->ctrl = 0;
            s->status = 0;
            s->ctrl_ext = 0;
            s->esdp = 0;
            s->gpie = 0;
        }
        break;
    case IXGBE_STATUS:
        s->status = val;
        break;
    case IXGBE_CTRL_EXT:
        s->ctrl_ext = val;
        break;
    case IXGBE_ESDP:
        s->esdp = val;
        break;
    case IXGBE_EERD:
        s->eerd_addr = (val >> 2) & (EEPROM_SIZE - 1);
        s->eerd_active = true;
        break;
    case IXGBE_GPIE:
        s->gpie = val;
        break;
    default:
        if (addr >= IXGBE_EITR(0) && addr < IXGBE_EITR(64)) {
            int idx = ixgbe_eitr_idx(addr);
            if (idx >= 0 && idx < 64) {
                s->eitr[idx] = val;
            }
        }
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
    memset(s->eitr, 0, sizeof(s->eitr));
    memset(s->ivar, 0, sizeof(s->ivar));
    s->ctrl = 0;
    s->status = 0;
    s->ctrl_ext = 0;
    s->esdp = 0;
    s->gpie = 0;
    s->eerd_active = false;
    pci_device_reset(PCI_DEVICE(dev));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x10B6);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS | QEMU_PCI_CAP_MSIX;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* MSI-X BAR */
    memory_region_init(&s->msix_mr, OBJECT(s), "ixgbe-msix", 0x4000);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_mr);
    int msix_ret = msix_init(pdev, 64, &s->msix_mr, 0, IXGBE_MSIXT,
                             &s->msix_mr, 0, IXGBE_MSIXPBA, 0, errp);
    if (msix_ret < 0) {
        return;
    }

    /* Initialize EEPROM */
    memset(s->eeprom, 0, sizeof(s->eeprom));
    s->eeprom[0x00] = 0x5254;
    s->eeprom[0x01] = 0x0012;
    s->eeprom[0x02] = 0x3456;
    uint16_t sum = 0;
    for (int i = 0; i < EEPROM_SIZE - 1; i++) {
        sum += s->eeprom[i];
    }
    s->eeprom[EEPROM_SIZE - 1] = 0xBABA - sum;

    /* Register BAR0 for MMIO */
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "ixgbe-mmio", 0x20000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    msix_uninit(pdev, NULL, NULL);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "ixgbe_pci",
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