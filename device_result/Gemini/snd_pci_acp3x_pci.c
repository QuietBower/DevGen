/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

/* Additional include files retrieved from driver context */
#ifndef ACP3x_PHY_BASE_ADDRESS
#define ACP3x_PHY_BASE_ADDRESS 0x1240000
#endif

#define TYPE_PCIBASE_DEVICE "snd_pci_acp3x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ACP_PGFSM_CNTL_POWER_ON_MASK	0x01
#define ACP_PGFSM_STATUS_MASK		0x03
#define ACP_POWER_ON_IN_PROGRESS	0x01
#define ACP_EXT_INTR_STAT_CLEAR_MASK 0xFFFFFFFF
#define I2S_MODE	0x04
#define ACP3x_I2S_MODE	0

#define mmACP_PME_EN                                    0x1241418
#define mmACP_I2S_PIN_CONFIG                            0x1241400
#define mmACP_PGFSM_STATUS                              0x1241420
#define mmACP_PGFSM_CONTROL                             0x124141C
#define mmACP_SOFT_RESET                                0x1241000
#define mmACP_EXTERNAL_INTR_ENB                         0x1241800
#define mmACP_EXTERNAL_INTR_STAT                        0x1241808
#define mmACP_EXTERNAL_INTR_CNTL                        0x1241804
#define ACP3x_REG_START	0x1240000
#define ACP3x_REG_END	0x1250200
#define ACP3x_I2STDM_REG_START	0x1242400
#define ACP3x_I2STDM_REG_END	0x1242410
#define ACP3x_BT_TDM_REG_START	0x1242800
#define ACP3x_BT_TDM_REG_END	0x1242810
#define ACP3x_SOFT_RESET__SoftResetAudDone_MASK	0x00010001

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
    uint32_t ext_intr_stat;
    uint32_t ext_intr_enb;
    uint32_t ext_intr_cntl;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t pgfsm_cntl;
    uint32_t pgfsm_status;
    uint32_t pme_en;
    uint32_t soft_reset;
    uint32_t i2s_pin_config;

    bool acp3x_audio_mode;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    hwaddr offset = addr + ACP3x_PHY_BASE_ADDRESS;

    switch (offset) {
    case mmACP_PGFSM_STATUS:
        val = s->pgfsm_status;
        break;
    case mmACP_PGFSM_CONTROL:
        val = s->pgfsm_cntl;
        break;
    case mmACP_PME_EN:
        val = s->pme_en;
        break;
    case mmACP_SOFT_RESET:
        val = s->soft_reset;
        break;
    case mmACP_EXTERNAL_INTR_ENB:
        val = s->ext_intr_enb;
        break;
    case mmACP_EXTERNAL_INTR_STAT:
        val = s->ext_intr_stat;
        break;
    case mmACP_EXTERNAL_INTR_CNTL:
        val = s->ext_intr_cntl;
        break;
    case mmACP_I2S_PIN_CONFIG:
        val = s->i2s_pin_config;
        break;
    default:
        val = 0;
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr offset = addr + ACP3x_PHY_BASE_ADDRESS;

    switch (offset) {
    case mmACP_PGFSM_CONTROL:
        s->pgfsm_cntl = val;
        if (val & ACP_PGFSM_CNTL_POWER_ON_MASK) {
            s->pgfsm_status = 0; /* Power on completes immediately */
        }
        break;
    case mmACP_PME_EN:
        s->pme_en = val;
        break;
    case mmACP_SOFT_RESET:
        if (val == 1) {
            s->soft_reset = 1 | ACP3x_SOFT_RESET__SoftResetAudDone_MASK;
        } else if (val == 0) {
            s->soft_reset = 0;
        } else {
            s->soft_reset = val;
        }
        break;
    case mmACP_EXTERNAL_INTR_ENB:
        s->ext_intr_enb = val;
        break;
    case mmACP_EXTERNAL_INTR_STAT:
        s->ext_intr_stat &= ~val; /* W1C */
        break;
    case mmACP_EXTERNAL_INTR_CNTL:
        s->ext_intr_cntl = val;
        break;
    case mmACP_I2S_PIN_CONFIG:
        s->i2s_pin_config = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->pgfsm_status = ACP_PGFSM_STATUS_MASK;
    s->pgfsm_cntl = 0;
    s->pme_en = 0;
    s->soft_reset = 0;
    s->ext_intr_enb = 0;
    s->ext_intr_stat = 0;
    s->ext_intr_cntl = 0;
    s->i2s_pin_config = I2S_MODE;
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMD );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x15e2 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0480 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x00);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = ACP3x_REG_END; /* Updated based on max register offset */
    s->bar_info[0].name = "acp3x-mmio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

     /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_pci_acp3x_pci",
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
