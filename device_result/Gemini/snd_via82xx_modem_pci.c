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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "snd_via82xx_modem_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VIA_REG_OFFSET_STATUS		0x00
#define VIA_REG_STAT_ACTIVE		0x80
#define VIA_REG_STAT_PAUSED		0x40
#define VIA_REG_STAT_TRIGGER_QUEUED	0x08
#define VIA_REG_STAT_STOPPED		0x04
#define VIA_REG_STAT_EOL		0x02
#define VIA_REG_STAT_FLAG		0x01
#define VIA_REG_OFFSET_CONTROL		0x01
#define VIA_REG_CTRL_START		0x80
#define VIA_REG_CTRL_TERMINATE	0x40
#define VIA_REG_CTRL_AUTOSTART	0x20
#define VIA_REG_CTRL_PAUSE		0x08
#define VIA_REG_CTRL_INT_STOP		0x04
#define VIA_REG_CTRL_INT_EOL		0x02
#define VIA_REG_CTRL_INT_FLAG		0x01
#define VIA_REG_CTRL_RESET		0x01
#define VIA_REG_CTRL_INT (VIA_REG_CTRL_INT_FLAG | VIA_REG_CTRL_INT_EOL | VIA_REG_CTRL_AUTOSTART)
#define VIA_REG_OFFSET_TYPE		0x02
#define VIA_REG_TYPE_AUTOSTART	0x80
#define VIA_REG_TYPE_16BIT		0x20
#define VIA_REG_TYPE_STEREO		0x10
#define VIA_REG_TYPE_INT_LLINE	0x00
#define VIA_REG_TYPE_INT_LSAMPLE	0x04
#define VIA_REG_TYPE_INT_LESSONE	0x08
#define VIA_REG_TYPE_INT_MASK		0x0c
#define VIA_REG_TYPE_INT_EOL		0x02
#define VIA_REG_TYPE_INT_FLAG		0x01
#define VIA_REG_OFFSET_TABLE_PTR	0x04
#define VIA_REG_OFFSET_CURR_PTR		0x04
#define VIA_REG_OFFSET_STOP_IDX		0x08
#define VIA_REG_OFFSET_CURR_COUNT	0x0c
#define VIA_REG_OFFSET_CURR_INDEX	0x0f
#define VIA_REG_AC97			0x80
#define VIA_REG_AC97_CODEC_ID_MASK	(3<<30)
#define VIA_REG_AC97_CODEC_ID_SHIFT	30
#define VIA_REG_AC97_CODEC_ID_PRIMARY	0x00
#define VIA_REG_AC97_CODEC_ID_SECONDARY 0x01
#define VIA_REG_AC97_SECONDARY_VALID	(1<<27)
#define VIA_REG_AC97_PRIMARY_VALID	(1<<25)
#define VIA_REG_AC97_BUSY		(1<<24)
#define VIA_REG_AC97_READ		(1<<23)
#define VIA_REG_AC97_CMD_SHIFT	16
#define VIA_REG_AC97_CMD_MASK		0x7e
#define VIA_REG_AC97_DATA_SHIFT	0
#define VIA_REG_AC97_DATA_MASK	0xffff
#define VIA_REG_SGD_SHADOW		0x84
#define VIA_REG_SGD_STAT_PB_FLAG	(1<<0)
#define VIA_REG_SGD_STAT_CP_FLAG	(1<<1)
#define VIA_REG_SGD_STAT_FM_FLAG	(1<<2)
#define VIA_REG_SGD_STAT_PB_EOL	(1<<4)
#define VIA_REG_SGD_STAT_CP_EOL	(1<<5)
#define VIA_REG_SGD_STAT_FM_EOL	(1<<6)
#define VIA_REG_SGD_STAT_PB_STOP	(1<<8)
#define VIA_REG_SGD_STAT_CP_STOP	(1<<9)
#define VIA_REG_SGD_STAT_FM_STOP	(1<<10)
#define VIA_REG_SGD_STAT_PB_ACTIVE	(1<<12)
#define VIA_REG_SGD_STAT_CP_ACTIVE	(1<<13)
#define VIA_REG_SGD_STAT_FM_ACTIVE	(1<<14)
#define VIA_REG_GPI_STATUS		0x88
#define VIA_REG_GPI_INTR		0x8c
#define VIA_TBL_BIT_FLAG	0x40000000
#define VIA_TBL_BIT_EOL		0x80000000
#define VIA_ACLINK_STAT		0x40
#define VIA_ACLINK_C11_READY	0x20
#define VIA_ACLINK_C10_READY	0x10
#define VIA_ACLINK_C01_READY	0x04
#define VIA_ACLINK_LOWPOWER	0x02
#define VIA_ACLINK_C00_READY	0x01
#define VIA_ACLINK_CTRL		0x41
#define VIA_ACLINK_CTRL_ENABLE	0x80
#define VIA_ACLINK_CTRL_RESET	0x40
#define VIA_ACLINK_CTRL_SYNC	0x20
#define VIA_ACLINK_CTRL_SDO	0x10
#define VIA_ACLINK_CTRL_VRA	0x08
#define VIA_ACLINK_CTRL_PCM	0x04
#define VIA_ACLINK_CTRL_FM	0x02
#define VIA_ACLINK_CTRL_SB	0x01
#define VIA_ACLINK_CTRL_INIT	(VIA_ACLINK_CTRL_ENABLE|\
				 VIA_ACLINK_CTRL_RESET|\
				 VIA_ACLINK_CTRL_PCM)
#define VIA_FUNC_ENABLE		0x42
#define VIA_FUNC_MIDI_PNP	0x80
#define VIA_FUNC_MIDI_IRQMASK	0x40
#define VIA_FUNC_RX2C_WRITE	0x20
#define VIA_FUNC_SB_FIFO_EMPTY	0x10
#define VIA_FUNC_ENABLE_GAME	0x08
#define VIA_FUNC_ENABLE_FM	0x04
#define VIA_FUNC_ENABLE_MIDI	0x02
#define VIA_FUNC_ENABLE_SB	0x01
#define VIA_PNP_CONTROL		0x43
#define VIA_TABLE_SIZE	255
#define VIA_REG_SGD_STAT_MR_FLAG      (1<<16)
#define VIA_REG_SGD_STAT_MW_FLAG      (1<<17)
#define VIA_REG_SGD_STAT_MR_EOL       (1<<20)
#define VIA_REG_SGD_STAT_MW_EOL       (1<<21)
#define VIA_REG_SGD_STAT_MR_STOP      (1<<24)
#define VIA_REG_SGD_STAT_MW_STOP      (1<<25)
#define VIA_REG_SGD_STAT_MR_ACTIVE    (1<<28)
#define VIA_REG_SGD_STAT_MW_ACTIVE    (1<<29)
#define VIA_MC97_CTRL		0x44
#define VIA_MC97_CTRL_ENABLE   0x80
#define VIA_MC97_CTRL_SECONDARY 0x40
#define VIA_MC97_CTRL_INIT     (VIA_MC97_CTRL_ENABLE|\
                                 VIA_MC97_CTRL_SECONDARY)
#define VIA_MAX_MODEM_DEVS	2
#define AC97_GPIO_STATUS	0x54

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
    bool has_msi;
    bool has_msix;
    uint32_t intr_mask;
    uint32_t sgd_shadow;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[256];

    /* DMA Context */
    struct {
        uint32_t table_ptr;
        uint32_t curr_ptr;
        uint32_t stop_idx;
        uint32_t curr_count;
        uint8_t status;
        uint8_t control;
        uint8_t type;
    } devs[VIA_MAX_MODEM_DEVS];

    uint32_t ac97_reg;
    uint8_t aclink_stat;
    uint8_t aclink_ctrl;
    uint8_t func_enable;
    uint8_t mc97_ctrl;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t status = 0;

    if (s->devs[0].status & VIA_REG_STAT_FLAG) status |= VIA_REG_SGD_STAT_MR_FLAG;
    if (s->devs[0].status & VIA_REG_STAT_EOL) status |= VIA_REG_SGD_STAT_MR_EOL;

    if (s->devs[1].status & VIA_REG_STAT_FLAG) status |= VIA_REG_SGD_STAT_MW_FLAG;
    if (s->devs[1].status & VIA_REG_STAT_EOL) status |= VIA_REG_SGD_STAT_MW_EOL;

    s->sgd_shadow = status;

    if (status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    for (int i = 0; i < VIA_MAX_MODEM_DEVS; i++) {
        if (s->devs[i].control & VIA_REG_CTRL_START) {
            uint32_t desc_addr = s->devs[i].curr_ptr;
            if (!desc_addr) {
                desc_addr = s->devs[i].table_ptr;
            }

            if (desc_addr) {
                uint32_t addr = 0, len_flag = 0;
                pci_dma_read(pdev, desc_addr, &addr, 4);
                pci_dma_read(pdev, desc_addr + 4, &len_flag, 4);
                
                addr = le32_to_cpu(addr);
                len_flag = le32_to_cpu(len_flag);

                uint32_t len = len_flag & 0x00FFFFFF;
                uint32_t flag = len_flag & 0xFF000000;

                s->devs[i].curr_count = len;
                s->devs[i].curr_ptr = desc_addr + 8;
                s->devs[i].status |= VIA_REG_STAT_ACTIVE;

                if (flag & VIA_TBL_BIT_FLAG) {
                    s->devs[i].status |= VIA_REG_STAT_FLAG;
                }
                if (flag & VIA_TBL_BIT_EOL) {
                    s->devs[i].status |= VIA_REG_STAT_EOL;
                    s->devs[i].status &= ~VIA_REG_STAT_ACTIVE;
                    s->devs[i].control &= ~VIA_REG_CTRL_START;
                }
            }
        }
    }
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case VIA_REG_AC97:
        val = s->ac97_reg;
        val &= ~VIA_REG_AC97_BUSY;
        val |= VIA_REG_AC97_PRIMARY_VALID | VIA_REG_AC97_SECONDARY_VALID;
        break;
    case VIA_REG_SGD_SHADOW:
        val = s->sgd_shadow;
        break;
    case VIA_REG_GPI_STATUS:
        val = 0;
        break;
    case VIA_REG_GPI_INTR:
        val = 0;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
    case VIA_REG_AC97:
        s->ac97_reg = val;
        break;
    case VIA_REG_GPI_STATUS:
        break;
    case VIA_REG_GPI_INTR:
        break;
    default:
        break;
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

    uint8_t *pci_conf = PCI_DEVICE(dev)->config;
    pci_conf[VIA_MC97_CTRL] = VIA_MC97_CTRL_INIT;
    pci_conf[VIA_ACLINK_STAT] = VIA_ACLINK_C00_READY;
    pci_conf[VIA_ACLINK_CTRL] = VIA_ACLINK_CTRL_INIT;

    for (int i = 0; i < VIA_MAX_MODEM_DEVS; i++) {
        s->devs[i].status = 0;
        s->devs[i].control = 0;
        s->devs[i].type = 0;
        s->devs[i].table_ptr = 0;
        s->devs[i].curr_ptr = 0;
        s->devs[i].curr_count = 0;
    }
    s->ac97_reg = 0;
    s->sgd_shadow = 0;
    s->intr_mask = 0x330000;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_VIA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x3068 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_COMMUNICATION_MODEM );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
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
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "via82xx_modem_pio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

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
    .name = "snd_via82xx_modem_pci",
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
