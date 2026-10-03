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

#define TYPE_PCIBASE_DEVICE "ftpci100_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define FTPCI_IOSIZE      0x00
#define FTPCI_PROT        0x04
#define FTPCI_CTRL        0x08
#define FTPCI_SOFTRST     0x10
#define FTPCI_CONFIG      0x28
#define FTPCI_DATA        0x2C
#define FARADAY_PCI_STATUS_CMD         0x04
#define FARADAY_PCI_PMC                0x40
#define FARADAY_PCI_PMCSR              0x44
#define FARADAY_PCI_CTRL1              0x48
#define FARADAY_PCI_CTRL2              0x4C
#define FARADAY_PCI_MEM1_BASE_SIZE     0x50
#define FARADAY_PCI_MEM2_BASE_SIZE     0x54
#define FARADAY_PCI_MEM3_BASE_SIZE     0x58

#define PCI_STATUS_66MHZ_CAPABLE       BIT(21)
#define PCI_CTRL2_INTSTS_SHIFT         28
#define PCI_CTRL2_INTMASK_CMDERR       BIT(27)
#define PCI_CTRL2_INTMASK_PARERR       BIT(26)
#define PCI_CTRL2_INTMASK_SHIFT        22
#define PCI_CTRL2_INTMASK_MABRT_RX     BIT(21)
#define PCI_CTRL2_INTMASK_TABRT_RX     BIT(20)
#define PCI_CTRL2_INTMASK_TABRT_TX     BIT(19)
#define PCI_CTRL2_INTMASK_RETRY4       BIT(18)
#define PCI_CTRL2_INTMASK_SERR_RX      BIT(17)
#define PCI_CTRL2_INTMASK_PERR_RX      BIT(16)
#define PCI_CTRL2_MSTPRI_REQ6          BIT(14)
#define PCI_CTRL2_MSTPRI_REQ5          BIT(13)
#define PCI_CTRL2_MSTPRI_REQ4          BIT(12)
#define PCI_CTRL2_MSTPRI_REQ3          BIT(11)
#define PCI_CTRL2_MSTPRI_REQ2          BIT(10)
#define PCI_CTRL2_MSTPRI_REQ1          BIT(9)
#define PCI_CTRL2_MSTPRI_REQ0          BIT(8)

#define FARADAY_PCI_MEMBASE_MASK      0xfff00000
#define FARADAY_PCI_MEMSIZE_1MB       0x0
#define FARADAY_PCI_MEMSIZE_2MB       0x1
#define FARADAY_PCI_MEMSIZE_4MB       0x2
#define FARADAY_PCI_MEMSIZE_8MB       0x3
#define FARADAY_PCI_MEMSIZE_16MB      0x4
#define FARADAY_PCI_MEMSIZE_32MB      0x5
#define FARADAY_PCI_MEMSIZE_64MB      0x6
#define FARADAY_PCI_MEMSIZE_128MB     0x7
#define FARADAY_PCI_MEMSIZE_256MB     0x8
#define FARADAY_PCI_MEMSIZE_512MB     0x9
#define FARADAY_PCI_MEMSIZE_1GB       0xa
#define FARADAY_PCI_MEMSIZE_2GB       0xb
#define FARADAY_PCI_MEMSIZE_SHIFT     16

#define FARADAY_PCI_DMA_MEM1_BASE     0x00000000
#define FARADAY_PCI_DMA_MEM2_BASE     0x00000000
#define FARADAY_PCI_DMA_MEM3_BASE     0x00000000

#define PCI_CONF1_ENABLE              BIT(31)
#define PCI_CONF1_FUNC_SHIFT          8
#define PCI_CONF1_FUNC_MASK           0x7
#define PCI_CONF1_DEV_MASK            0x1f
#define PCI_CONF1_DEV_SHIFT           11
#define PCI_CONF1_REG_MASK            0xfc
#define PCI_CONF1_BUS_MASK            0xff
#define PCI_CONF1_BUS_SHIFT           16

#define PCI_CONF1_ADDRESS(bus, dev, func, reg) \
	(PCI_CONF1_ENABLE | \
	 PCI_CONF1_BUS(bus) | \
	 PCI_CONF1_DEV(dev) | \
	 PCI_CONF1_FUNC(func) | \
	 PCI_CONF1_REG(reg))
#define PCI_CONF1_FUNC(x)  (((x) & PCI_CONF1_FUNC_MASK) << PCI_CONF1_FUNC_SHIFT)
#define PCI_CONF1_DEV(x)   (((x) & PCI_CONF1_DEV_MASK) << PCI_CONF1_DEV_SHIFT)
#define PCI_CONF1_REG(x)   ((x) & PCI_CONF1_REG_MASK)
#define PCI_CONF1_BUS(x)   (((x) & PCI_CONF1_BUS_MASK) << PCI_CONF1_BUS_SHIFT)

/* BAR size for memory-mapped register region */
#define FARADAY_REGS_SIZE 0x100

/* Structure mapping the Faraday PCI host controller registers */
typedef struct {
    uint32_t iosize;           /* 0x00 */
    uint32_t prot;             /* 0x04 */
    uint32_t ctrl;             /* 0x08 */
    uint32_t softrst;          /* 0x10 */
    uint32_t config;           /* 0x28 */
    uint32_t data;             /* 0x2C */
} FaradayRegs;


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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    FaradayRegs regs;

    /* Config space registers (emulated via config mechanism) */
    uint32_t config_regs[0x100 / 4];
};



/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t ctrl2 = s->config_regs[FARADAY_PCI_CTRL2 / 4];
    uint32_t status = (ctrl2 >> PCI_CTRL2_INTSTS_SHIFT) & 0xF;
    uint32_t mask = (ctrl2 >> PCI_CTRL2_INTMASK_SHIFT) & 0xF;
    bool irq_assert = (status & mask) != 0;

    if (irq_assert) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}


/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case FTPCI_IOSIZE:
        val = s->regs.iosize;
        break;
    case FTPCI_PROT:
        val = s->regs.prot;
        break;
    case FTPCI_CTRL:
        val = s->regs.ctrl;
        break;
    case FTPCI_SOFTRST:
        val = s->regs.softrst;
        break;
    case FTPCI_CONFIG:
        val = s->regs.config;
        break;
    case FTPCI_DATA:
        /* Perform config read based on stored config address */
        if (s->regs.config & PCI_CONF1_ENABLE) {
            int bus = (s->regs.config >> PCI_CONF1_BUS_SHIFT) & PCI_CONF1_BUS_MASK;
            int dev = (s->regs.config >> PCI_CONF1_DEV_SHIFT) & PCI_CONF1_DEV_MASK;
            int func = (s->regs.config >> PCI_CONF1_FUNC_SHIFT) & PCI_CONF1_FUNC_MASK;
            int reg = s->regs.config & PCI_CONF1_REG_MASK;

            if (bus == 0 && dev == 0 && func == 0) {
                /* Our own config space */
                int index = reg / 4;
                if (index < ARRAY_SIZE(s->config_regs)) {
                    val = s->config_regs[index];
                } else {
                    val = 0;
                }
            } else {
                /* No device */
                val = ~0U;
            }
        } else {
            val = s->regs.data; /* Return last written value */
        }
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

    switch (addr) {
    case FTPCI_IOSIZE:
        s->regs.iosize = val;
        break;
    case FTPCI_PROT:
        s->regs.prot = val;
        break;
    case FTPCI_CTRL:
        s->regs.ctrl = val;
        break;
    case FTPCI_SOFTRST:
        s->regs.softrst = val;
        break;
    case FTPCI_CONFIG:
        s->regs.config = val;
        break;
    case FTPCI_DATA:
        /* Perform config write based on stored config address */
        if (s->regs.config & PCI_CONF1_ENABLE) {
            int bus = (s->regs.config >> PCI_CONF1_BUS_SHIFT) & PCI_CONF1_BUS_MASK;
            int dev = (s->regs.config >> PCI_CONF1_DEV_SHIFT) & PCI_CONF1_DEV_MASK;
            int func = (s->regs.config >> PCI_CONF1_FUNC_SHIFT) & PCI_CONF1_FUNC_MASK;
            int reg = s->regs.config & PCI_CONF1_REG_MASK;

            if (bus == 0 && dev == 0 && func == 0) {
                int index = reg / 4;
                if (index < ARRAY_SIZE(s->config_regs)) {
                    /* Special handling for CTRL2 (W1C on status bits) */
                    if (reg == FARADAY_PCI_CTRL2) {
                        uint32_t old = s->config_regs[index];
                        uint32_t status_mask = 0xF << PCI_CTRL2_INTSTS_SHIFT;
                        uint32_t w1c = val & status_mask; /* bits to clear */
                        old &= ~w1c;
                        /* Preserve cleared status, apply non-status bits */
                        uint32_t non_status = ~status_mask;
                        old = (old & status_mask) | (val & non_status);
                        s->config_regs[index] = old;
                    } else if (size == 4) {
                        s->config_regs[index] = val;
                    } else {
                        /* Byte/halfword merge */
                        int byte_offset = reg & 3;
                        if (size == 2) {
                            uint16_t *p = (uint16_t *)&s->config_regs[index];
                            if (byte_offset == 0) p[0] = (uint16_t)val;
                            else p[1] = (uint16_t)val;
                        } else if (size == 1) {
                            uint8_t *p = (uint8_t *)&s->config_regs[index];
                            p[byte_offset] = (uint8_t)val;
                        }
                    }
                    /* After any CTRL2 write, update IRQ */
                    if (reg == FARADAY_PCI_CTRL2) {
                        pcibase_update_irq(s);
                    }
                }
            } else {
                /* Ignore writes to other devices */
            }
        } else {
            s->regs.data = val;
        }
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    memset(s->config_regs, 0, sizeof(s->config_regs));
    /* Set STATUS_CMD to indicate 66MHz capable */
    s->config_regs[FARADAY_PCI_STATUS_CMD / 4] = PCI_STATUS_66MHZ_CAPABLE;
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1057 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x01 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0600 );
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
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, FARADAY_REGS_SIZE, "bar0"};
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
    .name = "ftpci100_pci",
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
