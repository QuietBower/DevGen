/*
 * QEMU CS5535 Audio device model (Phase 2: Implementation)
 * Based on Linux driver /home/eely/linux-7.1/sound/pci/cs5535audio/cs5535audio.c
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

#define TYPE_PCIBASE_DEVICE "snd_cs5535audio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs from supplementary driver source */
#define PCI_VENDOR_ID_NS          0x100b
#define PCI_DEVICE_ID_NS_CS5535_AUDIO 0x002e
#define CLASS_ID 0x040100  /* PCI_CLASS_MULTIMEDIA_AUDIO */

/* Register offsets (from driver) */
#define ACC_GPIO_STATUS         0x00
#define ACC_CODEC_STATUS        0x08
#define ACC_CODEC_CNTL          0x0C
#define ACC_IRQ_STATUS          0x12
#define ACC_BM0_CMD             0x20
#define ACC_BM0_STATUS          0x21
#define ACC_BM0_PRD             0x24
#define ACC_BM0_PNTR            0x60
#define ACC_BM1_CMD             0x28
#define ACC_BM1_STATUS          0x29
#define ACC_BM1_PRD             0x2C
#define ACC_BM1_PNTR            0x64

/* Bit definitions */
#define CMD_NEW                 0x00010000
#define STS_NEW                 0x00020000
#define ACC_CODEC_CNTL_RD_CMD   0x80000000
#define ACC_CODEC_CNTL_WR_CMD   (~0x80000000)
#define CMD_MASK                0xFF00FFFF
#define EOP                     (1<<0)
#define BM_CTL_EN               0x01
#define BM_CTL_PAUSE            0x03

/* Interrupt bits */
#define IRQ_STS                 0
#define WU_IRQ_STS              1
#define BM0_IRQ_STS             2
#define BM1_IRQ_STS             3

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

#define NUM_CS5535AUDIO_DMAS 2 /* Two DMA engines (BM0, BM1) */

/* DMA channel state (based on cs5535audio_dma) */
typedef struct {
    uint32_t bm_cmd;      /* BMx_CMD */
    uint32_t bm_status;   /* BMx_STATUS */
    uint32_t bm_prd;      /* BMx_PRD (descriptor address) */
    uint32_t bm_pntr;     /* BMx_PNTR (pointer) */
} CS5535AUDIODmaState;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint16_t irq_status;  /* ACC_IRQ_STATUS */

    /* Hardware Register Shadows */
    uint8_t regs[0x100];  /* I/O space registers */

    /* DMA Context */
    CS5535AUDIODmaState dma[2];  /* Two channels (BM0, BM1) */

    /* Additional state for behavior */
    uint32_t codec_cntl;
    uint32_t codec_status;
    uint32_t gpio_status;
    uint16_t codec_regs[128];
    bool codec_read_pending;
    uint32_t pending_read_reg;
};

/* Update PCI IRQ line based on irq_status */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->irq_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic - not used (driver DMA setup not provided) */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* No implementation needed for probe */
}

/* MMIO Handlers (unused by driver, but kept for template compatibility) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No MMIO registers; return 0 */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No MMIO registers; do nothing */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ACC_GPIO_STATUS:
        if (size == 4) {
            val = s->gpio_status;
            /* Reading GPIO_STATUS clears IRQ_STS and WU_IRQ_STS */
            s->irq_status &= ~((1 << IRQ_STS) | (1 << WU_IRQ_STS));
            pcibase_update_irq(s);
        }
        break;
    case ACC_CODEC_STATUS:
        if (size == 4) {
            val = s->codec_status;
            /* After reading, clear STS_NEW and pending */
            s->codec_status &= ~STS_NEW;
            s->codec_read_pending = false;
        }
        break;
    case ACC_CODEC_CNTL:
        if (size == 4) {
            val = s->codec_cntl;
        }
        break;
    case ACC_IRQ_STATUS:
        if (size == 2) {
            val = s->irq_status;
            /* Reading IRQ_STATUS does not clear bits; clearing via source registers */
        }
        break;
    case ACC_BM0_CMD:
        if (size == 1) {
            val = s->dma[0].bm_cmd;
        }
        break;
    case ACC_BM0_STATUS:
        if (size == 1) {
            val = s->dma[0].bm_status;
            /* Reading BM0_STATUS clears BM0_IRQ_STS */
            s->irq_status &= ~(1 << BM0_IRQ_STS);
            pcibase_update_irq(s);
        }
        break;
    case ACC_BM0_PRD:
        if (size == 4) {
            val = s->dma[0].bm_prd;
        }
        break;
    case ACC_BM0_PNTR:
        if (size == 4) {
            val = s->dma[0].bm_pntr;
        }
        break;
    case ACC_BM1_CMD:
        if (size == 1) {
            val = s->dma[1].bm_cmd;
        }
        break;
    case ACC_BM1_STATUS:
        if (size == 1) {
            val = s->dma[1].bm_status;
            /* Reading BM1_STATUS clears BM1_IRQ_STS */
            s->irq_status &= ~(1 << BM1_IRQ_STS);
            pcibase_update_irq(s);
        }
        break;
    case ACC_BM1_PRD:
        if (size == 4) {
            val = s->dma[1].bm_prd;
        }
        break;
    case ACC_BM1_PNTR:
        if (size == 4) {
            val = s->dma[1].bm_pntr;
        }
        break;
    default:
        if (addr < sizeof(s->regs)) {
            val = s->regs[addr];
            if (size == 2) {
                val = *(uint16_t *)&s->regs[addr];
            } else if (size == 4) {
                val = *(uint32_t *)&s->regs[addr];
            }
        }
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ACC_GPIO_STATUS:
        /* Guess: may be writable? Not used for probe. Ignore. */
        if (size == 4) {
            s->gpio_status = val;
        }
        break;
    case ACC_CODEC_CNTL:
        if (size == 4) {
            /* Process codec command */
            s->codec_cntl = val;
            if (val & CMD_NEW) {
                uint32_t cmd = val;
                unsigned int reg = (cmd >> 24) & 0x7F;
                if (cmd & ACC_CODEC_CNTL_RD_CMD) {
                    /* Read command */
                    /* Store pending read data */
                    s->codec_status = STS_NEW | (reg << 24) | s->codec_regs[reg];
                    s->codec_read_pending = true;
                    s->pending_read_reg = reg;
                } else {
                    /* Write command */
                    uint16_t data = cmd & 0xFFFF;
                    s->codec_regs[reg] = data;
                }
                /* Clear CMD_NEW after processing */
                s->codec_cntl &= ~CMD_NEW;
            }
        }
        break;
    case ACC_IRQ_STATUS:
        /* Driver does not write to IRQ_STATUS; maybe write to clear? Not used. */
        if (size == 2) {
            s->irq_status = val;
            pcibase_update_irq(s);
        }
        break;
    case ACC_BM0_CMD:
        if (size == 1) {
            s->dma[0].bm_cmd = val & 0xFF;
        }
        break;
    case ACC_BM0_STATUS:
        if (size == 1) {
            s->dma[0].bm_status = val & 0xFF;
        }
        break;
    case ACC_BM0_PRD:
        if (size == 4) {
            s->dma[0].bm_prd = val;
        }
        break;
    case ACC_BM0_PNTR:
        if (size == 4) {
            s->dma[0].bm_pntr = val;
        }
        break;
    case ACC_BM1_CMD:
        if (size == 1) {
            s->dma[1].bm_cmd = val & 0xFF;
        }
        break;
    case ACC_BM1_STATUS:
        if (size == 1) {
            s->dma[1].bm_status = val & 0xFF;
        }
        break;
    case ACC_BM1_PRD:
        if (size == 4) {
            s->dma[1].bm_prd = val;
        }
        break;
    case ACC_BM1_PNTR:
        if (size == 4) {
            s->dma[1].bm_pntr = val;
        }
        break;
    default:
        if (addr < sizeof(s->regs)) {
            if (size == 1) {
                s->regs[addr] = (uint8_t)val;
            } else if (size == 2) {
                *(uint16_t *)&s->regs[addr] = (uint16_t)val;
            } else if (size == 4) {
                *(uint32_t *)&s->regs[addr] = (uint32_t)val;
            }
        }
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
    
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->dma, 0, sizeof(s->dma));
    s->irq_status = 0;
    s->codec_cntl = 0;
    s->codec_status = 0;
    s->gpio_status = 0;
    s->codec_read_pending = false;
    s->pending_read_reg = 0;
    /* Initialize codec registers with some AC'97 defaults */
    memset(s->codec_regs, 0, sizeof(s->codec_regs));
    /* Reset register */
    s->codec_regs[0x00] = 0x0090;
    /* Master Volume: 0 gain, mute off */
    s->codec_regs[0x02] = 0x0000;
    /* Vendor ID1 */
    s->codec_regs[0x7C] = 0x8384;
    /* Vendor ID2 */
    s->codec_regs[0x7E] = 0x7608;
    /* Some other typical registers could be set, but not needed for probe */
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_NS);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_NS_CS5535_AUDIO);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;  /* BAR0 size: 256 bytes I/O */
    s->bar_info[0].name = "cs5535audio-io";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X */
    s->has_msi = false;
    s->has_msix = false;

    /* Field init: clear registers */
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->dma, 0, sizeof(s->dma));
    s->irq_status = 0;
    s->codec_cntl = 0;
    s->codec_status = 0;
    s->gpio_status = 0;
    s->codec_read_pending = false;
    s->pending_read_reg = 0;
    memset(s->codec_regs, 0, sizeof(s->codec_regs));
    /* Codec registers are initialized in reset, not here. */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* Cleanup: none needed */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_cs5535audio_pci",
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
