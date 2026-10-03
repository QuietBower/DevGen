/* Integrated QEMU PCI device template (QEMU 8.2.10). Implemented for vt8231 SMBus controller emulation. Phase 4 runtime fixes applied. */
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
#include "qemu/bswap.h"

#define TYPE_PCIBASE_DEVICE "vt8231_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_VIA     0x1106
#define PCI_DEVICE_ID_VIA_8231_4 0x8235
#define PCI_CLASS_ID          0x0C05

#define VT8231_EXTENT         0x80
#define VT8231_REG_CONFIG     0x40
#define VT8231_REG_ALARM1     0x41
#define VT8231_REG_ALARM2     0x42
#define VT8231_REG_FANDIV     0x47
#define VT8231_REG_UCH_CONFIG 0x4a
#define VT8231_REG_TEMP1_CONFIG 0x4b
#define VT8231_REG_TEMP2_CONFIG 0x4c
#define VT8231_REG_TEMP_LOW01  0x49
#define VT8231_REG_TEMP_LOW25  0x4d
#define VT8231_REG_FAN_MIN(nr) (0x3b + (nr))
#define VT8231_REG_FAN(nr)     (0x29 + (nr))

#define VT8231_REG_TEMP1      0x1f
#define VT8231_REG_TEMP2      0x21
#define VT8231_REG_TEMP3      0x22
#define VT8231_REG_TEMP4      0x23
#define VT8231_REG_TEMP5      0x24
#define VT8231_REG_TEMP6      0x25
#define VT8231_REG_TEMPMAX1   0x39
#define VT8231_REG_TEMPMAX2   0x3d
#define VT8231_REG_TEMPMAX3   0x2b
#define VT8231_REG_TEMPMAX4   0x2d
#define VT8231_REG_TEMPMAX5   0x2f
#define VT8231_REG_TEMPMAX6   0x31
#define VT8231_REG_VOLT1      0x21
#define VT8231_REG_VOLT2      0x22
#define VT8231_REG_VOLT3      0x23
#define VT8231_REG_VOLT4      0x24
#define VT8231_REG_VOLT5      0x25
#define VT8231_REG_VOLT6      0x26
#define VT8231_REG_VOLTMAX1   0x3d
#define VT8231_REG_VOLTMAX2   0x2b
#define VT8231_REG_VOLTMAX3   0x2d
#define VT8231_REG_VOLTMAX4   0x2f
#define VT8231_REG_VOLTMAX5   0x31
#define VT8231_REG_VOLTMAX6   0x33
#define VT8231_REG_VOLTMIN1   0x3e
#define VT8231_REG_VOLTMIN2   0x2c
#define VT8231_REG_VOLTMIN3   0x2e
#define VT8231_REG_VOLTMIN4   0x30
#define VT8231_REG_VOLTMIN5   0x32
#define VT8231_REG_VOLTMIN6   0x34
#define VT8231_REG_TEMPMIN1   0x3a
#define VT8231_REG_TEMPMIN2   0x3e
#define VT8231_REG_TEMPMIN3   0x2c
#define VT8231_REG_TEMPMIN4   0x2e
#define VT8231_REG_TEMPMIN5   0x30
#define VT8231_REG_TEMPMIN6   0x32

#define VT8231_BASE_REG       0x70
#define VT8231_ENABLE_REG     0x74

#define SMBBA 0x70
#define SMBHSTCFG 0x74

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
    MemoryRegion smbus_mr;
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[0x80];

    /* Custom PCI configuration registers */
    uint16_t base_reg;
    uint16_t enable_reg;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x80) {
        return val;
    }

    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        val = lduw_le_p(s->regs + addr);
        break;
    case 4:
        val = ldl_le_p(s->regs + addr);
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x80) {
        return;
    }

    switch (size) {
    case 1:
        s->regs[addr] = val;
        break;
    case 2:
        stw_le_p(s->regs + addr, val);
        break;
    case 4:
        stl_le_p(s->regs + addr, val);
        break;
    default:
        break;
    }
}

static uint64_t smbus_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void smbus_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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

static const MemoryRegionOps smbus_ops = {
    .read = smbus_read,
    .write = smbus_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    /* Provide a plausible default configuration register */
    s->regs[VT8231_REG_UCH_CONFIG] = 0x3F; /* Temporary: enable all voltage and temp channels */
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

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Handle custom configuration registers 0x70-0x77 (SMBus I/O Base and PM I/O Base) */
    if (addr >= 0x70 && addr + len <= 0x78) {
        uint32_t val = 0;

        /* SMBus I/O Base at 0x70-0x71 */
        if (addr == 0x70 && len == 1) {
            val = s->base_reg & 0xFF;
        } else if (addr == 0x71 && len == 1) {
            val = (s->base_reg >> 8) & 0xFF;
        } else if (addr == 0x70 && len == 2) {
            val = s->base_reg;
        } else if (addr == 0x70 && len == 4) {
            val = s->base_reg; /* only lower 16 bits, upper 16 return 0 */
        }
        /* PM I/O Base (Enable) at 0x74-0x75 */
        else if (addr == 0x74 && len == 1) {
            val = s->enable_reg & 0xFF;
        } else if (addr == 0x75 && len == 1) {
            val = (s->enable_reg >> 8) & 0xFF;
        } else if (addr == 0x74 && len == 2) {
            val = s->enable_reg;
        } else if (addr == 0x74 && len == 4) {
            val = s->enable_reg; /* only lower 16 bits */
        }
        return val;
    }

    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Handle custom configuration registers 0x70-0x77 (SMBus I/O Base and PM I/O Base) */
    if (addr >= 0x70 && addr + len <= 0x78) {

        /* SMBus I/O Base address write (0x70-0x71) */
        if (addr == 0x70 || addr == 0x71) {
            if (len == 4) {
                s->base_reg = val & 0xFFFF;
            } else if (len == 2) {
                s->base_reg = val;
            } else if (len == 1) {
                if (addr == 0x70) {
                    s->base_reg = (s->base_reg & 0xFF00) | (val & 0xFF);
                } else { /* addr == 0x71 */
                    s->base_reg = (s->base_reg & 0x00FF) | ((val & 0xFF) << 8);
                }
            }
            return;
        }

        /* PM I/O Base (Enable) register write (0x74-0x75) */
        if (addr == 0x74 || addr == 0x75) {
            if (len == 4) {
                s->enable_reg = val & 0xFFFF;
            } else if (len == 2) {
                s->enable_reg = val;
            } else if (len == 1) {
                if (addr == 0x74) {
                    s->enable_reg = (s->enable_reg & 0xFF00) | (val & 0xFF);
                } else { /* addr == 0x75 */
                    s->enable_reg = (s->enable_reg & 0x00FF) | ((val & 0xFF) << 8);
                }
            }
            return;
        }
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_VIA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_VIA_8231_4);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: BAR0 for hardware monitor (unused by SMBus driver) */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x80;
    s->bar_info[0].name = "vt8231-io";

    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* BAR4 for SMBus I/O space - required for driver probe */
    memory_region_init_io(&s->smbus_mr, OBJECT(s), &smbus_ops, s, "vt8231-smbus", 0x20);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->smbus_mr);

    memset(s->regs, 0, sizeof(s->regs));
    s->base_reg = 0xE001;
    pci_set_word(pci_conf + 0x70, 0xE001);
    s->enable_reg = 0x0001;
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
    .name = "vt8231_pci",
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
