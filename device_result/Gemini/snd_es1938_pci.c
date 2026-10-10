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


#define TYPE_PCIBASE_DEVICE "snd_es1938_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ESS		0x125d
#define SL_PCI_LEGACYCONTROL		0x40
#define SL_PCI_CONFIG			0x50
#define SL_PCI_DDMACONTROL		0x60
#define ESSIO_REG_AUDIO2DMAADDR		0
#define ESSIO_REG_AUDIO2DMACOUNT	4
#define ESSIO_REG_AUDIO2MODE		6
#define ESSIO_REG_IRQCONTROL		7
#define ESSDM_REG_DMAADDR		0x00
#define ESSDM_REG_DMACOUNT		0x04
#define ESSDM_REG_DMACOMMAND		0x08
#define ESSDM_REG_DMASTATUS		0x08
#define ESSDM_REG_DMAMODE		0x0b
#define ESSDM_REG_DMACLEAR		0x0d
#define ESSDM_REG_DMAMASK		0x0f
#define ESSSB_REG_FMLOWADDR		0x00
#define ESSSB_REG_FMHIGHADDR		0x02
#define ESSSB_REG_MIXERADDR		0x04
#define ESSSB_REG_MIXERDATA		0x05
#define ESSSB_IREG_AUDIO1		0x14
#define ESSSB_IREG_MICMIX		0x1a
#define ESSSB_IREG_RECSRC		0x1c
#define ESSSB_IREG_MASTER		0x32
#define ESSSB_IREG_FM			0x36
#define ESSSB_IREG_AUXACD		0x38
#define ESSSB_IREG_AUXB			0x3a
#define ESSSB_IREG_PCSPEAKER		0x3c
#define ESSSB_IREG_LINE			0x3e
#define ESSSB_IREG_SPATCONTROL		0x50
#define ESSSB_IREG_SPATLEVEL		0x52
#define ESSSB_IREG_MASTER_LEFT		0x60
#define ESSSB_IREG_MASTER_RIGHT		0x62
#define ESSSB_IREG_MPU401CONTROL	0x64
#define ESSSB_IREG_MICMIXRECORD		0x68
#define ESSSB_IREG_AUDIO2RECORD		0x69
#define ESSSB_IREG_AUXACDRECORD		0x6a
#define ESSSB_IREG_FMRECORD		0x6b
#define ESSSB_IREG_AUXBRECORD		0x6c
#define ESSSB_IREG_MONO			0x6d
#define ESSSB_IREG_LINERECORD		0x6e
#define ESSSB_IREG_MONORECORD		0x6f
#define ESSSB_IREG_AUDIO2SAMPLE		0x70
#define ESSSB_IREG_AUDIO2MODE		0x71
#define ESSSB_IREG_AUDIO2FILTER		0x72
#define ESSSB_IREG_AUDIO2TCOUNTL	0x74
#define ESSSB_IREG_AUDIO2TCOUNTH	0x76
#define ESSSB_IREG_AUDIO2CONTROL1	0x78
#define ESSSB_IREG_AUDIO2CONTROL2	0x7a
#define ESSSB_IREG_AUDIO2		0x7c
#define ESSSB_REG_RESET			0x06
#define ESSSB_REG_READDATA		0x0a
#define ESSSB_REG_WRITEDATA		0x0c
#define ESSSB_REG_READSTATUS		0x0c
#define ESSSB_REG_STATUS		0x0e
#define ESS_CMD_EXTSAMPLERATE		0xa1
#define ESS_CMD_FILTERDIV		0xa2
#define ESS_CMD_DMACNTRELOADL		0xa4
#define ESS_CMD_DMACNTRELOADH		0xa5
#define ESS_CMD_ANALOGCONTROL		0xa8
#define ESS_CMD_IRQCONTROL		0xb1
#define ESS_CMD_DRQCONTROL		0xb2
#define ESS_CMD_RECLEVEL		0xb4
#define ESS_CMD_SETFORMAT		0xb6
#define ESS_CMD_SETFORMAT2		0xb7
#define ESS_CMD_DMACONTROL		0xb8
#define ESS_CMD_DMATYPE			0xb9
#define ESS_CMD_OFFSETLEFT		0xba
#define ESS_CMD_OFFSETRIGHT		0xbb
#define ESS_CMD_READREG			0xc0
#define ESS_CMD_ENABLEEXT		0xc6
#define ESS_CMD_PAUSEDMA		0xd0
#define ESS_CMD_ENABLEAUDIO1		0xd1
#define ESS_CMD_STOPAUDIO1		0xd3
#define ESS_CMD_AUDIO1STATUS		0xd8
#define ESS_CMD_CONTDMA			0xd4
#define ESS_CMD_TESTIRQ			0xf2
#define ESS_RECSRC_MIC		0
#define ESS_RECSRC_AUXACD	2
#define ESS_RECSRC_AUXB		5
#define ESS_RECSRC_LINE		6
#define ESS_RECSRC_NONE		7

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
    uint8_t irqmask;
    uint8_t irq_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t essio_regs[0x10];
    uint8_t essdm_regs[0x10];
    uint8_t esssb_regs[0x80];
    uint8_t mixer_regs[0x80];

    /* DMA Context */
    uint32_t dma1_size;
    uint32_t dma2_size;
    uint32_t dma1_start;
    uint32_t dma2_start;
    uint32_t dma1_shift;
    uint32_t dma2_shift;
    uint32_t last_capture_dmaaddr;

    uint32_t active;
    
    
    uint8_t revision;
    uint8_t saved_regs[32];
};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->irq_status & s->irqmask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->dma1_size > 0) {
        uint8_t buf[64] = {0};
        uint32_t len = s->dma1_size > sizeof(buf) ? sizeof(buf) : s->dma1_size;
        if (is_write) {
            pci_dma_write(pdev, s->dma1_start, buf, len);
        } else {
            pci_dma_read(pdev, s->dma1_start, buf, len);
        }
    }
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
        case 0x00:
            if (size == 4) val = s->dma1_start;
            break;
        case 0x04:
            if (size == 2) val = s->dma1_size;
            else if (size == 1) val = s->esssb_regs[0x04];
            break;
        case 0x05:
            if (size == 1) val = s->mixer_regs[s->esssb_regs[0x04] & 0x7f];
            break;
        case 0x06:
            if (size == 1) val = s->esssb_regs[0x06];
            break;
        case 0x07:
            if (size == 1) val = s->irq_status;
            break;
        case 0x08:
            if (size == 1 || size == 4) val = s->essdm_regs[0x08];
            break;
        case 0x0a:
            if (size == 1) {
                val = s->esssb_regs[0x0a];
                s->esssb_regs[0x0e] &= ~0x80;
            }
            break;
        case 0x0b:
            if (size == 1) val = s->essdm_regs[0x0b];
            break;
        case 0x0c:
            if (size == 1) val = 0x00;
            break;
        case 0x0d:
            if (size == 1) val = s->essdm_regs[0x0d];
            break;
        case 0x0e:
            if (size == 1) {
                val = s->esssb_regs[0x0e];
                s->irq_status &= ~0x10;
                pcibase_update_irq(s);
            }
            break;
        case 0x0f:
            if (size == 1) val = s->essdm_regs[0x0f];
            break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
        case 0x00:
            if (size == 4) s->dma1_start = val;
            break;
        case 0x04:
            if (size == 2) s->dma1_size = val;
            else if (size == 1) s->esssb_regs[0x04] = val;
            break;
        case 0x05:
            if (size == 1) {
                uint8_t idx = s->esssb_regs[0x04] & 0x7f;
                s->mixer_regs[idx] = val;
                if (idx == 0x66 && val == 0x00) {
                    s->irq_status &= ~0x40;
                    pcibase_update_irq(s);
                }
                if (idx == 0x7a && (val & 0x80) == 0) {
                    s->irq_status &= ~0x20;
                    pcibase_update_irq(s);
                }
            }
            break;
        case 0x06:
            if (size == 1) {
                s->esssb_regs[0x06] = val;
                if (val == 0) {
                    s->esssb_regs[0x0e] |= 0x80;
                    s->esssb_regs[0x0a] = 0xaa;
                }
            }
            break;
        case 0x07:
            if (size == 1) {
                s->irqmask = val;
                pcibase_update_irq(s);
            }
            break;
        case 0x08:
            if (size == 1) s->essdm_regs[0x08] = val;
            break;
        case 0x0b:
            if (size == 1) s->essdm_regs[0x0b] = val;
            break;
        case 0x0c:
            if (size == 1) {
                uint8_t cmd = val;
                if (s->esssb_regs[0x10] == 0) {
                    if (cmd == 0xc0) {
                        s->esssb_regs[0x10] = 1;
                    } else if (cmd >= 0xa0) {
                        s->esssb_regs[0x11] = cmd;
                        s->esssb_regs[0x10] = 2;
                        if (cmd == 0xc6 || cmd == 0xd1 || cmd == 0xd3 || cmd == 0xd4 || cmd == 0xf2) {
                            s->esssb_regs[0x10] = 0;
                        }
                    }
                } else if (s->esssb_regs[0x10] == 1) {
                    uint8_t reg = cmd;
                    if (reg >= 0x80) {
                        s->esssb_regs[0x0a] = s->esssb_regs[reg - 0x80];
                    }
                    s->esssb_regs[0x0e] |= 0x80;
                    s->esssb_regs[0x10] = 0;
                } else if (s->esssb_regs[0x10] == 2) {
                    uint8_t reg = s->esssb_regs[0x11];
                    if (reg >= 0x80) {
                        s->esssb_regs[reg - 0x80] = cmd;
                    }
                    s->esssb_regs[0x10] = 0;
                }
            }
            break;
        case 0x0d:
            if (size == 1) s->essdm_regs[0x0d] = val;
            break;
        case 0x0f:
            if (size == 1) s->essdm_regs[0x0f] = val;
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

    memset(s->essio_regs, 0, sizeof(s->essio_regs));
    memset(s->essdm_regs, 0, sizeof(s->essdm_regs));
    memset(s->esssb_regs, 0, sizeof(s->esssb_regs));
    memset(s->mixer_regs, 0, sizeof(s->mixer_regs));
    s->irqmask = 0;
    s->irq_status = 0;
    s->dma1_start = 0;
    s->dma1_size = 0;
    s->dma2_start = 0;
    s->dma2_size = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ESS);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x1969);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_PIO, 256, "io_port"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_PIO, 256, "sb_port"};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_PIO, 256, "vc_port"};
    s->bar_info[3] = (BARInfo){3, BAR_TYPE_PIO, 256, "mpu_port"};
    s->bar_info[4] = (BARInfo){4, BAR_TYPE_PIO, 256, "game_port"};
    
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
    .name = "snd_es1938_pci",
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
