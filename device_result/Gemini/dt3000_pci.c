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

#define TYPE_PCIBASE_DEVICE "dt3000_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_DT		0x1116

#define DPR_DAC_BUFFER		(4 * 0x000)
#define DPR_ADC_BUFFER		(4 * 0x800)
#define DPR_COMMAND		(4 * 0xfd3)
#define DPR_SUBSYS		(4 * 0xfd3)
#define DPR_SUBSYS_AI		0
#define DPR_SUBSYS_AO		1
#define DPR_SUBSYS_DIN		2
#define DPR_SUBSYS_DOUT		3
#define DPR_SUBSYS_MEM		4
#define DPR_SUBSYS_CT		5
#define DPR_ENCODE		(4 * 0xfd4)
#define DPR_PARAMS(x)		(4 * (0xfd5 + (x)))
#define DPR_PARAM6_AD_DIFF		BIT(0)
#define DPR_TICK_REG_LO		(4 * 0xff5)
#define DPR_TICK_REG_HI		(4 * 0xff6)
#define DPR_DA_BUF_FRONT		(4 * 0xff7)
#define DPR_DA_BUF_REAR		(4 * 0xff8)
#define DPR_AD_BUF_FRONT		(4 * 0xff9)
#define DPR_AD_BUF_REAR		(4 * 0xffa)
#define DPR_INT_MASK		(4 * 0xffb)
#define DPR_INTR_FLAG		(4 * 0xffc)
#define DPR_INTR_CMDONE		BIT(7)
#define DPR_INTR_CTDONE		BIT(6)
#define DPR_INTR_DAHWERR	BIT(5)
#define DPR_INTR_DASWERR	BIT(4)
#define DPR_INTR_DAEMPTY	BIT(3)
#define DPR_INTR_ADHWERR	BIT(2)
#define DPR_INTR_ADSWERR	BIT(1)
#define DPR_INTR_ADFULL		BIT(0)
#define DPR_RESPONSE_MBX	(4 * 0xffe)
#define DPR_CMD_MBX		(4 * 0xfff)

#define DPR_AI_FIFO_DEPTH		2003

#define DPR_CMD_COMPLETION(x)	((x) << 8)
#define DPR_CMD(x)		((x) << 0)
#define DPR_PARAM5_AD_TRIG(x)		(((x) & 0x7) << 2)

#define DPR_CMD_COMPLETION_MASK	DPR_CMD_COMPLETION(0xff)
#define DPR_CMD_NOTPROCESSED	DPR_CMD_COMPLETION(0x00)
#define DPR_CMD_NOERROR		DPR_CMD_COMPLETION(0x55)
#define DPR_CMD_READSINGLE	DPR_CMD(5)
#define DPR_CMD_WRITESINGLE	DPR_CMD(6)
#define DPR_CMD_STOP		DPR_CMD(4)
#define DPR_CMD_CONFIG		DPR_CMD(1)
#define DPR_CMD_START		DPR_CMD(3)
#define DPR_CMD_READCODE	DPR_CMD(37)
#define DPR_PARAM5_AD_TRIG_INT_RETRIG	DPR_PARAM5_AD_TRIG(2)

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x1000];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intr_status & s->intr_mask) != 0;
    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t index = addr / 4;

    if (index < 0x1000) {
        val = s->regs[index];
    }

    if (addr == DPR_INTR_FLAG) {
        val = s->intr_status;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr / 4;

    if (index < 0x1000) {
        s->regs[index] = val;
    }

    if (addr == DPR_CMD_MBX) {
        /* Auto-complete commands to satisfy driver polling */
        s->regs[DPR_CMD_MBX / 4] = DPR_CMD_NOERROR;
    } else if (addr == DPR_INT_MASK) {
        s->intr_mask = val;
        pcibase_update_irq(s);
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
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_DT );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0022 ); /* Placeholder until pci_device_id is provided */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x4000, .name = "dt3000_mmio" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "dt3000_pci",
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
