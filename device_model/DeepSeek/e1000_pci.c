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

#define TYPE_PCIBASE_DEVICE "e1000_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs extracted from driver */
#define PCI_VENDOR_ID_INTEL 0x8086
#define E1000_DEV_ID_82542 0x1000
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Register offsets (all 32-bit registers) */
#define E1000_CTRL     0x00000
#define E1000_STATUS   0x00008
#define E1000_EERD     0x00014
#define E1000_EEWR     0x00018
#define E1000_ICR      0x000C0
#define E1000_ICS      0x000C8
#define E1000_IMS      0x000D0
#define E1000_IMC      0x000D8
#define E1000_RCTL     0x00100
#define E1000_TCTL     0x00400
#define E1000_TIPG     0x00410
#define E1000_TDBAL    0x03800
#define E1000_TDBAH    0x03804
#define E1000_TDLEN    0x03808
#define E1000_RDBAL    0x02800
#define E1000_RDBAH    0x02804
#define E1000_RDLEN    0x02808
#define E1000_RDH      0x02810
#define E1000_RDT      0x02818
#define E1000_TDH      0x03810
#define E1000_TDT      0x03818
/* 82542-specific offsets */
#define E1000_82542_TDT 0x00438
#define E1000_82542_TDH 0x00430
#define E1000_82542_RDT 0x00128
#define E1000_82542_RDH 0x00120

/* EEPROM */
#define E1000_EEPROM_SIZE 64

/* EERD bits from supplementary source */
#define E1000_EERD_START      0x00000001
#define E1000_EERD_DONE       0x00000010
#define E1000_EERD_ADDR_SHIFT 8

/* Revision IDs from supplementary source */
#define E1000_82542_2_0_REV_ID 0
#define E1000_82542_2_1_REV_ID 1

/* Enum definitions from e1000 driver hardware types */
typedef enum {
    e1000_82571,
    e1000_82572,
    e1000_82573,
    e1000_82574,
    e1000_82583,
    e1000_80003es2lan,
    e1000_ich8lan,
    e1000_ich9lan,
    e1000_ich10lan,
    e1000_pchlan,
    e1000_pch2lan,
    e1000_pch_lpt,
    e1000_pch_spt,
    e1000_pch_cnp,
    e1000_pch_tgp,
    e1000_pch_adp,
    e1000_pch_mtp,
    e1000_pch_lnp,
    e1000_pch_ptp,
    e1000_pch_nvp,
} e1000_mac_type;

typedef enum {
    e1000_phy_unknown = 0,
    e1000_phy_none,
    e1000_phy_m88,
    e1000_phy_igp,
    e1000_phy_igp_2,
    e1000_phy_gg82563,
    e1000_phy_igp_3,
    e1000_phy_ife,
    e1000_phy_bm,
    e1000_phy_82578,
    e1000_phy_82577,
    e1000_phy_82579,
    e1000_phy_i217,
} e1000_phy_type;

typedef enum {
    e1000_media_type_unknown = 0,
    e1000_media_type_copper = 1,
    e1000_media_type_fiber = 2,
    e1000_media_type_internal_serdes = 3,
    e1000_num_media_types
} e1000_media_type;

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

#define E1000_MMIO_SIZE (128 * 1024)

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion mmio;
    uint32_t regs[E1000_MMIO_SIZE / 4];
    uint32_t eeprom[E1000_EEPROM_SIZE];
    bool eeprom_read_pending;
    uint8_t eeprom_read_addr;
    uint32_t ims_mask;
    bool has_msi;
    bool has_msix;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t icr = s->regs[E1000_ICR >> 2];
    if (icr & s->ims_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr & 0xFFFF;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned read at 0x%" HWADDR_PRIx ", size=%d\n",
                      __func__, addr, size);
        return 0;
    }

    switch (offset) {
    case E1000_CTRL:
        val = s->regs[offset >> 2];
        break;
    case E1000_STATUS:
        /* report link up, full duplex, 1000Mb, etc. */
        val = 0x00008380; /* typical value for 82542 */
        break;
    case E1000_ICR:
        val = s->regs[offset >> 2];
        s->regs[offset >> 2] = 0; /* read clears */
        pcibase_update_irq(s);
        break;
    case E1000_ICS:
        val = s->regs[E1000_ICR >> 2]; /* ICS reads ICR? */
        break;
    case E1000_IMS:
        val = s->ims_mask;
        break;
    case E1000_IMC:
        val = 0; /* IMC reads zero */
        break;
    case E1000_EERD:
        if (s->eeprom_read_pending) {
            uint16_t data = s->eeprom[s->eeprom_read_addr];
            val = (data << 16) | E1000_EERD_DONE;
            s->eeprom_read_pending = false;
        } else {
            val = 0;
        }
        break;
    case E1000_EEWR:
        val = 0;
        break;
    case E1000_TIPG:
    case E1000_TDBAL:
    case E1000_TDBAH:
    case E1000_TDLEN:
    case E1000_RDBAL:
    case E1000_RDBAH:
    case E1000_RDLEN:
    case E1000_RDH:
    case E1000_RDT:
    case E1000_TDH:
    case E1000_TDT:
    case E1000_82542_TDT:
    case E1000_82542_TDH:
    case E1000_82542_RDT:
    case E1000_82542_RDH:
    case E1000_RCTL:
    case E1000_TCTL:
    default:
        if (offset < E1000_MMIO_SIZE) {
            val = s->regs[offset >> 2];
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr & 0xFFFF;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned write at 0x%" HWADDR_PRIx ", size=%d\n",
                      __func__, addr, size);
        return;
    }

    switch (offset) {
    case E1000_CTRL:
        s->regs[offset >> 2] = (uint32_t)val;
        break;
    case E1000_ICR:
        /* ICR is read-only, ignore writes */
        break;
    case E1000_ICS:
        /* writing sets bits (write-1-to-set) */
        s->regs[E1000_ICR >> 2] |= (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case E1000_IMS:
        s->ims_mask |= (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case E1000_IMC:
        s->ims_mask &= ~(uint32_t)val;
        pcibase_update_irq(s);
        break;
    case E1000_EERD:
        /* initiate EEPROM read: start bit at bit 0, addr in bits 8-15 */
        if (val & E1000_EERD_START) {
            s->eeprom_read_addr = (val >> E1000_EERD_ADDR_SHIFT) & 0x3F;
            s->eeprom_read_pending = true;
        }
        break;
    case E1000_EEWR:
        /* ignore writes to EEWR for now */
        break;
    case E1000_TIPG:
    case E1000_TDBAL:
    case E1000_TDBAH:
    case E1000_TDLEN:
    case E1000_RDBAL:
    case E1000_RDBAH:
    case E1000_RDLEN:
    case E1000_RDH:
    case E1000_RDT:
    case E1000_TDH:
    case E1000_TDT:
    case E1000_82542_TDT:
    case E1000_82542_TDH:
    case E1000_82542_RDT:
    case E1000_82542_RDH:
    case E1000_RCTL:
    case E1000_TCTL:
    default:
        if (offset < E1000_MMIO_SIZE) {
            s->regs[offset >> 2] = (uint32_t)val;
        }
        break;
    case E1000_STATUS:
        /* STATUS is read-only, ignore writes */
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
    /* Clear register array */
    memset(s->regs, 0, sizeof(s->regs));
    s->ims_mask = 0;
    s->eeprom_read_pending = false;
    /* Reset interrupt */
    pci_set_irq(PCI_DEVICE(dev), 0);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int i;
    uint32_t sum = 0;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  E1000_DEV_ID_82542 );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, E1000_DEV_ID_82542);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, E1000_82542_2_0_REV_ID);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Initialize EEPROM with valid signature, MAC, and checksum */
    memset(s->eeprom, 0, sizeof(s->eeprom));
    s->eeprom[0] = 0x1234;         /* EEPROM signature */
    s->eeprom[1] = 0x5452;         /* MAC bytes 0x52, 0x54 */
    s->eeprom[2] = 0x0012;         /* MAC bytes 0x00, 0x12 */
    s->eeprom[3] = 0x5634;         /* MAC bytes 0x34, 0x56 */
    for (i = 0; i < 63; i++) {
        sum += s->eeprom[i];
    }
    s->eeprom[63] = (0xBABA - sum) & 0xFFFF;

    /* Set STATUS register to a valid initial state */
    s->regs[E1000_STATUS >> 2] = 0x00008380;

    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "e1000-mmio", E1000_MMIO_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
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
    .name = "e1000_pci",
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
