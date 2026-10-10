/*
 * QEMU 8.2.10 PCI device model for Cirrus PD6729 PCMCIA bridge
 * Derived from Linux driver /home/eely/linux-7.1/drivers/pcmcia/pd6729.c
 * Phase 2: Full functional implementation
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

#define TYPE_PCIBASE_DEVICE "pd6729_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification and Register Offsets from driver */
#define PCI_VENDOR_ID_CIRRUS         0x1013
#define PCI_DEVICE_ID_CIRRUS_6729    0x6729
#define PCI_CLASS_ID                 0x060700  /* Bridge, CardBus */

/* Register indices (indirect access via index/data ports) */
#define I365_STATUS             0x01
#define I365_POWER              0x02
#define I365_INTCTL             0x03
#define I365_CSC                0x04
#define I365_CSCINT             0x05
#define I365_ADDRWIN            0x06
#define I365_IOCTL              0x07
#define I365_GENCTL             0x16
#define I365_GBLCTL             0x1E
#define PD67_EXT_INDEX          0x2E
#define PD67_EXT_DATA           0x2F

/* Register bit masks and constants */
#define I365_CSC_STSCHG         0x01
#define I365_CSC_BVD2           0x02
#define I365_PC_IOCARD          0x20
#define I365_CSC_BVD1           0x01
#define I365_CSC_DETECT         0x08
#define I365_CSC_READY          0x04
#define I365_CS_BVD2            0x02
#define I365_CS_POWERON         0x40
#define PD67_EXD_VS1(s)         (0x01 << ((s) << 1))
#define I365_CS_BVD1            0x01
#define I365_CS_DETECT          0x0C
#define PD67_EXTERN_DATA        0x0A
#define I365_CS_STSCHG          0x01
#define I365_CS_WRPROT          0x10
#define I365_CS_READY           0x20
#define PD67_MISC_CTL_1         0x16
#define I365_PC_RESET           0x40
#define I365_VPP1_5V            0x01
#define PD67_EXT_CTL_1          0x03
#define I365_PWR_NORESET        0x40
#define I365_POWER              0x02
#define I365_VCC_5V             0x10
#define I365_VPP1_12V           0x02
#define I365_PWR_AUTO           0x20
#define I365_GBLCTL             0x1E
#define I365_CSCINT             0x05
#define PD67_EC1_INV_MGMT_IRQ   0x10
#define I365_GENCTL             0x16
#define PD67_MC1_VCC_3V         0x02
#define PD67_EC1_INV_CARD_IRQ   0x08
#define I365_PWR_OUT            0x80
#define I365_IOCTL_0WS(map)     (0x04 << ((map) << 2))
#define I365_W_START            0
#define I365_W_STOP             2
#define I365_IOCTL              0x07
#define I365_ADDRWIN            0x06
#define I365_ENA_IO(map)        (0x40 << (map))
#define I365_IOCTL_MASK(map)    (0x0F << ((map) << 2))
#define I365_IO(map)            (0x08 + ((map) << 2))
#define I365_IOCTL_IOCS16(map)  (0x02 << ((map) << 2))
#define I365_IOCTL_16BIT(map)   (0x01 << ((map) << 2))
#define I365_MEM_REG            0x4000
#define I365_MEM(map)           (0x10 + ((map) << 3))
#define I365_MEM_WRPROT         0x8000
#define I365_MEM_WS1            0x8000
#define I365_MEM_0WS            0x4000
#define I365_MEM_WS0            0x4000
#define PD67_MEM_PAGE(n)        ((n) + 5)
#define I365_MEM_16BIT          0x8000
#define I365_W_OFF              4
#define I365_ENA_MEM(map)       (0x01 << (map))
#define PD67_MASK               0x0EB8

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

    /* Indirect register set */
    uint8_t index_reg;
    uint8_t indirect_regs[256];
    uint8_t ext_index;
    uint8_t ext_indirect_regs[256];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = false;
    for (int i = 0; i < 2; i++) {
        int base = i * 0x40;
        uint8_t csc = s->indirect_regs[base + I365_CSC];
        uint8_t cscint = s->indirect_regs[base + I365_CSCINT];
        if (csc & cscint) {
            pending = true;
            break;
        }
    }
    pci_set_irq(pdev, pending ? 1 : 0);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    val = 0; /* mmio read */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* mmio write */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == 0) {
        return 0;
    } else if (addr == 1) {
        uint8_t idx = s->index_reg;
        if (idx == 0x2F || idx == 0x6F) {
            return s->ext_indirect_regs[s->ext_index] & 0xFF;
        } else if (idx == 0x30 || idx == 0x70) {
            return (s->ext_indirect_regs[s->ext_index] >> 8) & 0xFF;
        } else {
            uint8_t val = s->indirect_regs[idx];
            if (idx == I365_CSC || idx == I365_CSC + 0x40) {
                s->indirect_regs[idx] = 0;
                pcibase_update_irq(s);
            }
            return val;
        }
    }
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr == 0) {
        s->index_reg = val;
    } else if (addr == 1) {
        s->indirect_regs[s->index_reg] = val;
        if (s->index_reg == 0x2E || s->index_reg == 0x6E) {
            s->ext_index = val;
        } else if (s->index_reg == 0x2F || s->index_reg == 0x6F) {
            s->ext_indirect_regs[s->ext_index] =
                (s->ext_indirect_regs[s->ext_index] & 0xFF00) | val;
        } else if (s->index_reg == 0x30 || s->index_reg == 0x70) {
            s->ext_indirect_regs[s->ext_index] =
                (s->ext_indirect_regs[s->ext_index] & 0x00FF) | (val << 8);
        }
        pcibase_update_irq(s);
    }
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
    s->index_reg = 0;
    s->ext_index = 0;
    memset(s->indirect_regs, 0, sizeof(s->indirect_regs));
    memset(s->ext_indirect_regs, 0, sizeof(s->ext_indirect_regs));
    pcibase_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CIRRUS);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_CIRRUS_6729);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = 0x1000,
        .name = "pd6729-pio"
    };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    memset(s->indirect_regs, 0, sizeof(s->indirect_regs));
    memset(s->ext_indirect_regs, 0, sizeof(s->ext_indirect_regs));
    s->index_reg = 0;
    s->ext_index = 0;
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
    .name = "pd6729_pci",
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
