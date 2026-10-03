/*
 * QEMU model of the ALi M5451 audio controller (ali5451) and companion M1533.
 * Based on Linux driver snd-ali5451.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/pci/pci_regs.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "snd_ali5451_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs extracted from driver */
#define ALI5451_VENDOR_ID 0x10b9
#define ALI5451_DEVICE_ID 0x5451
#define ALI5451_CLASS_ID PCI_CLASS_MULTIMEDIA_AUDIO

/* Register offsets from driver */
#define ALI_LEGACY_DMAR0          0x00
#define ALI_LEGACY_DMAR4          0x04
#define ALI_LEGACY_DMAR11         0x0b
#define ALI_LEGACY_DMAR15         0x0f
#define ALI_MPUR0                 0x20
#define ALI_MPUR1                 0x21
#define ALI_MPUR2                 0x22
#define ALI_MPUR3                 0x23
#define ALI_AC97_WRITE            0x40
#define ALI_AC97_READ             0x44
#define ALI_SCTRL                 0x48
#define ALI_AC97_GPIO             0x4c
#define ALI_SPDIF_CS              0x70
#define ALI_SPDIF_CTRL            0x74
#define ALI_START                 0x80
#define ALI_STOP                  0x84
#define ALI_CSPF                  0x90
#define ALI_AINT                  0x98
#define ALI_GC_CIR                0xa0
#define ALI_AINTEN                0xa4
#define ALI_VOLUME                0xa8
#define ALI_SBDELTA_DELTA_R       0xac
#define ALI_MISCINT               0xb0
#define ALI_SBBL_SBCL             0xc0
#define ALI_SBCTRL_SBE2R_SBDD     0xc4
#define ALI_STIMER                0xc8
#define ALI_GLOBAL_CONTROL        0xd4
#define ALI_CSO_ALPHA_FMS         0xe0
#define ALI_LBA                   0xe4
#define ALI_ESO_DELTA             0xe8
#define ALI_GVSEL_PAN_VOC_CTRL_EC 0xf0
#define ALI_EBUF1                 0xf4
#define ALI_EBUF2                 0xf8

/* Bit definitions used from driver */
#define ALI_SCTRL_CODEC1_READY    (1 << 24)
#define ALI_SCTRL_CODEC2_READY    (1 << 25)
#define ALI_SPDIF_OUT_ENABLE      0x20
#define ALI_AC97_GPIO_ENABLE      0x8000
#define ALI_AC97_GPIO_DATA_SHIFT  16

/* Codec protocol flag */
#define AC97_CMD_READY            0x8000

/* Interrupt bits */
#define ADDRESS_IRQ               0x00000020
#define TARGET_REACHED            0x00008000
#define MIXER_OVERFLOW            0x00000800
#define MIXER_UNDERFLOW           0x00000400

/* Number of audio channels */
#define ALI_CHANNELS              32

/* Per-port codec command state */
typedef struct {
    bool pending;
    uint8_t reg_idx;
} CodecPort;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* PIO memory region (BAR0) */
    MemoryRegion bar0;

    /* Global registers, 0x00..0xdf (56 dwords) */
    uint32_t global_regs[0xe0 / 4];

    /* Per-channel banked registers, 0xe0..0xff (8 dwords per channel) */
    uint32_t bank_regs[ALI_CHANNELS][0x20 / 4];

    /* Selected channel for banked access (via ALI_GC_CIR write) */
    uint8_t current_channel;

    /* Special registers with W1C behavior */
    uint32_t aint;    /* 0x98 */
    uint32_t ainten;  /* 0xa4 */
    uint32_t miscint; /* 0xb0 */

    /* AC97 register file (16-bit x 128) */
    uint16_t ac97_regs[0x80];

    /* Codec command state for the two command ports */
    CodecPort codec_port_write; /* for 0x40 */
    CodecPort codec_port_read;  /* for 0x44 */
};

/* ----------------------------------------------------------------- */
/* ALi M1533 companion device type (definition moved before use)     */
/* ----------------------------------------------------------------- */
#define TYPE_ALI1533_DEVICE "ali1533"
typedef struct ALi1533State ALi1533State;
OBJECT_DECLARE_SIMPLE_TYPE(ALi1533State, ALI1533_DEVICE)

struct ALi1533State {
    PCIDevice parent_obj;
};

/* ----------------------------------------------------------------- */
/* PIO read/write handlers                                           */
/* ----------------------------------------------------------------- */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr & 0xff;

    /* Banked region 0xe0..0xff */
    if (offset >= 0xe0) {
        int ch = s->current_channel & 0x1f;
        int idx = (offset - 0xe0) >> 2;
        uint32_t data = s->bank_regs[ch][idx];
        switch (size) {
        case 1:
            return (data >> ((offset & 3) * 8)) & 0xff;
        case 2:
            return (data >> ((offset & 2) * 8)) & 0xffff;
        case 4:
            return data;
        default:
            return ~0ULL;
        }
    }

    /* Specific registers with non-trivial behavior */
    switch (offset) {
    case 0x40: /* ALI_AC97_WRITE */
        if (s->codec_port_write.pending) {
            s->codec_port_write.pending = false;
            return 0; /* ready, bit15 cleared */
        }
        return 0;

    case 0x44: /* ALI_AC97_READ */
        if (s->codec_port_read.pending) {
            uint16_t val = s->ac97_regs[s->codec_port_read.reg_idx];
            s->codec_port_read.pending = false;
            return (uint32_t)val << 16;
        }
        return 0;

    case 0x98: /* ALI_AINT */
        return s->aint;

    case 0xa4: /* ALI_AINTEN */
        return s->ainten;

    case 0xb0: /* ALI_MISCINT */
        return s->miscint;

    case 0xc8: /* ALI_STIMER */
        /* Return a free-running 32-bit counter based on the virtual clock. */
        return (uint32_t)(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) >> 10);

    default:
        /* All other global registers (0x00..0xdf) */
        {
            int idx = offset >> 2;
            uint32_t data = s->global_regs[idx];
            switch (size) {
            case 1:
                return (data >> ((offset & 3) * 8)) & 0xff;
            case 2:
                return (data >> ((offset & 2) * 8)) & 0xffff;
            case 4:
                return data;
            default:
                return ~0ULL;
            }
        }
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr & 0xff;

    /* Banked region 0xe0..0xff */
    if (offset >= 0xe0) {
        int ch = s->current_channel & 0x1f;
        int idx = (offset - 0xe0) >> 2;
        uint32_t data = s->bank_regs[ch][idx];
        switch (size) {
        case 1:
            data = (data & ~(0xff << ((offset & 3) * 8)))
                | ((val & 0xff) << ((offset & 3) * 8));
            break;
        case 2:
            data = (data & ~(0xffff << ((offset & 2) * 8)))
                | ((val & 0xffff) << ((offset & 2) * 8));
            break;
        case 4:
            data = (uint32_t)val;
            break;
        }
        s->bank_regs[ch][idx] = data;
        return;
    }

    switch (offset) {
    case 0x40: /* ALI_AC97_WRITE */
        if (val & AC97_CMD_READY) {
            uint8_t reg = val & 0x7f;
            uint16_t data = (val >> 16) & 0xffff;
            s->ac97_regs[reg] = data;
            s->codec_port_write.pending = true;
        }
        break;

    case 0x44: /* ALI_AC97_READ */
        if (val & AC97_CMD_READY) {
            uint8_t reg = val & 0x7f;
            s->codec_port_read.reg_idx = reg;
            s->codec_port_read.pending = true;
        }
        break;

    case 0x98: /* ALI_AINT (write-1-to-clear) */
        s->aint &= ~(uint32_t)val;
        break;

    case 0xa0: /* ALI_GC_CIR - channel select */
        s->current_channel = val & 0x1f;
        break;

    case 0xa4: /* ALI_AINTEN */
        s->ainten = (uint32_t)val;
        break;

    case 0xb0: /* ALI_MISCINT (write-1-to-clear) */
        s->miscint &= ~(uint32_t)val;
        break;

    case 0xc8: /* ALI_STIMER - read-only in practice, ignore writes */
        break;

    default:
        /* All other global registers */
        {
            int idx = offset >> 2;
            uint32_t data = s->global_regs[idx];
            switch (size) {
            case 1:
                data = (data & ~(0xff << ((offset & 3) * 8)))
                    | ((val & 0xff) << ((offset & 3) * 8));
                break;
            case 2:
                data = (data & ~(0xffff << ((offset & 2) * 8)))
                    | ((val & 0xffff) << ((offset & 2) * 8));
                break;
            case 4:
                data = (uint32_t)val;
                break;
            }
            s->global_regs[idx] = data;
        }
        break;
    }
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ----------------------------------------------------------------- */
/* Reset                                                             */
/* ----------------------------------------------------------------- */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear all state */
    memset(s->global_regs, 0, sizeof(s->global_regs));
    memset(s->bank_regs, 0, sizeof(s->bank_regs));
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->aint = 0;
    s->ainten = 0;
    s->miscint = 0;
    s->current_channel = 0;
    s->codec_port_write.pending = false;
    s->codec_port_read.pending = false;

    /* Set default values required for probe success */

    /* ALI_SCTRL[24] (CODEC1_READY) must be set. */
    s->global_regs[ALI_SCTRL >> 2] = ALI_SCTRL_CODEC1_READY;

    /* AC97 codec: POWERDOWN register (0x26) must indicate ready (bits 0-3 = 1) */
    s->ac97_regs[0x26] = 0x000F;
}

/* ----------------------------------------------------------------- */
/* BAR registration helper                                           */
/* ----------------------------------------------------------------- */
static void pcibase_register_bar(PCIDevice *pdev, int index,
                                 hwaddr size, const char *name,
                                 Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* Ensure BAR size is a power of 2 */
    hwaddr aligned_size = pow2ceil(size);
    memory_region_init_io(&s->bar0, OBJECT(s), &pcibase_pio_ops, s, name, aligned_size);
    pci_register_bar(pdev, index, PCI_BASE_ADDRESS_SPACE_IO, &s->bar0);
}

/* ----------------------------------------------------------------- */
/* Realize function                                                  */
/* ----------------------------------------------------------------- */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    uint8_t *pci_conf = pdev->config;

    /* Mandatory PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ALI5451_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ALI5451_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ALI5451_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR 0: PIO, 256 bytes */
    pcibase_register_bar(pdev, 0, 0x100, "ali5451-io", errp);

    /* Set multifunction bit to indicate companion device on function 1 */
    pci_set_byte(pci_conf + PCI_HEADER_TYPE,
                 pci_get_byte(pci_conf + PCI_HEADER_TYPE) | PCI_HEADER_TYPE_MULTI_FUNCTION);

    /* Create companion M1533 device on function 1 of the same slot */
    {
        PCIBus *bus = pci_get_bus(pdev);
        uint8_t slot = PCI_SLOT(pdev->devfn);
        pci_create_simple(bus, PCI_DEVFN(slot, 1), TYPE_ALI1533_DEVICE);
    }

    /* Power management capability (optional) */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }
}

/* ----------------------------------------------------------------- */
/* Uninitialization                                                  */
/* ----------------------------------------------------------------- */
static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* ----------------------------------------------------------------- */
/* VMState description (minimal)                                     */
/* ----------------------------------------------------------------- */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_ali5451_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

/* ----------------------------------------------------------------- */
/* ALi M1533 companion device                                        */
/* ----------------------------------------------------------------- */
static void ali1533_realize(PCIDevice *pdev, Error **errp)
{
    /* Minimal device; only IDs are needed */
}

static InterfaceInfo ali1533_interfaces[] = {
    { INTERFACE_CONVENTIONAL_PCI_DEVICE },
    { },
};

static void ali1533_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = ali1533_realize;
    k->vendor_id = 0x10b9;
    k->device_id = 0x1533;
    k->revision = 0x00;
    k->class_id = 0x0601; /* PCI_CLASS_BRIDGE_ISA */
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

/* ----------------------------------------------------------------- */
/* Class initialization & type registration                         */
/* ----------------------------------------------------------------- */
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

    static const TypeInfo ali1533_info = {
        .name = TYPE_ALI1533_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(ALi1533State),
        .class_init = ali1533_class_init,
        .interfaces = ali1533_interfaces,
    };

    type_register_static(&pcibase_info);
    type_register_static(&ali1533_info);
}

type_init(pcibase_register_types);
