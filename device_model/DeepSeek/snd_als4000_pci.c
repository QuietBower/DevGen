/*
 * QEMU PCI device model for ALS4000 (Vendor 0x4005, Device 0x4000)
 * Updated: Fixed PCI class initialization, added CR access via ports 0x0E/0x0F.
 * Phase 4 fix: Initialize CR registers for chip detection (CR18/CR19) in cr_regs, not mixer_regs.
 * Phase 4 iteration: Set GCR 0x8C chip revision; add gameport/OPL dummy handlers.
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

#define TYPE_PCIBASE_DEVICE "snd_als4000_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define PCI_VENDOR_ID_ALS4000     0x4005
#define PCI_DEVICE_ID_ALS4000     0x4000
#define PCI_CLASS_ID_ALS4000      0x040100

/* I/O port register offsets (PIO BAR) */
#define ALS4K_IOD_00_AC97_ACCESS        0x00
#define ALS4K_IOW_04_AC97_READ          0x04
#define ALS4K_IOB_06_AC97_STATUS        0x06
#define ALS4K_IOB_07_IRQSTATUS          0x07
#define ALS4K_IOD_08_GCR_DATA           0x08
#define ALS4K_IOB_0C_GCR_INDEX          0x0c
#define ALS4K_IOB_0E_IRQTYPE_SB_CR1E_MPU 0x0e
#define ALS4K_IOB_0F_CR_DATA            0x0f
#define ALS4K_IOB_10_ADLIB_ADDR0        0x10
#define ALS4K_IOB_11_ADLIB_ADDR1        0x11
#define ALS4K_IOB_12_ADLIB_ADDR2        0x12
#define ALS4K_IOB_13_ADLIB_ADDR3        0x13
#define ALS4K_IOB_14_MIXER_INDEX        0x14
#define ALS4K_IOB_15_MIXER_DATA         0x15
#define ALS4K_IOB_16_ESP_RESET          0x16
#define ALS4K_IOB_18_OPL_ADDR0          0x18
#define ALS4K_IOB_19_OPL_ADDR1          0x19
#define ALS4K_IOB_1A_ESP_RD_DATA        0x1a
#define ALS4K_IOB_1C_ESP_CMD_DATA       0x1c
#define ALS4K_IOB_1E_ESP_RD_STATUS8     0x1e
#define ALS4K_IOB_1F_ESP_RD_STATUS16    0x1f
#define ALS4K_IOB_20_ESP_GAMEPORT_200   0x20
#define ALS4K_IOB_21_ESP_GAMEPORT_201   0x21
#define ALS4K_IOB_30_MIDI_DATA          0x30
#define ALS4K_IOB_31_MIDI_STATUS        0x31

/* GCR register indices (32-bit) */
#define ALS4K_GCR8C_MISC_CTRL               0x8c
#define ALS4K_GCR90_TEST_MODE_REG           0x90
#define ALS4K_GCR91_DMA0_ADDR               0x91
#define ALS4K_GCR92_DMA0_MODE_COUNT         0x92
#define ALS4K_GCR93_DMA1_ADDR               0x93
#define ALS4K_GCR94_DMA1_MODE_COUNT         0x94
#define ALS4K_GCR95_DMA3_ADDR               0x95
#define ALS4K_GCR96_DMA3_MODE_COUNT         0x96
#define ALS4K_GCR99_DMA_EMULATION_CTRL      0x99
#define ALS4K_GCRA0_FIFO1_CURRENT_ADDR      0xa0
#define ALS4K_GCRA1_FIFO1_STATUS_BYTECOUNT  0xa1
#define ALS4K_GCRA2_FIFO2_PCIADDR           0xa2
#define ALS4K_GCRA3_FIFO2_COUNT             0xa3
#define ALS4K_GCRA4_FIFO2_CURRENT_ADDR      0xa4
#define ALS4K_GCRA5_FIFO1_STATUS_BYTECOUNT  0xa5
#define ALS4K_GCRA6_PM_CTRL                 0xa6
#define ALS4K_GCRA7_PCI_ACCESS_STORAGE      0xa7
#define ALS4K_GCRA8_LEGACY_CFG1             0xa8
#define ALS4K_GCRA9_LEGACY_CFG2             0xa9
#define ALS4K_GCRFF_DUMMY_SCRATCH           0xff

/* GCR8C bit definitions */
#define ALS4K_GCR8C_IRQ_MASK_CTRL_ENABLE  0x8000
#define ALS4K_GCR8C_CHIP_REV_MASK         0xf0000

/* CR register indices (8-bit) */
#define ALS4K_CR0_SB_CONFIG                0x00
#define ALS4K_CR2_MISC_CONTROL             0x02
#define ALS4K_CR3_CONFIGURATION            0x03
#define ALS4K_CR17_FIFO_STATUS             0x17
#define ALS4K_CR18_ESP_MAJOR_VERSION       0x18
#define ALS4K_CR19_ESP_MINOR_VERSION       0x19
#define ALS4K_CR1A_MPU401_UART_MODE_CONTROL 0x1a
#define ALS4K_CR1C_FIFO2_BLOCK_LENGTH_LO   0x1c
#define ALS4K_CR1D_FIFO2_BLOCK_LENGTH_HI   0x1d
#define ALS4K_CR1E_FIFO2_CONTROL           0x1e
#define ALS4K_CR3A_MISC_CONTROL            0x3a
#define ALS4K_CR3B_CRC32_BYTE0             0x3b
#define ALS4K_CR3C_CRC32_BYTE1             0x3c
#define ALS4K_CR3D_CRC32_BYTE2             0x3d
#define ALS4K_CR3E_CRC32_BYTE3             0x3e

/* CR0 bits */
#define ALS4K_CR0_DMA_CONTIN_MODE_CTRL     0x02
#define ALS4K_CR0_DMA_90H_MODE_CTRL        0x04
#define ALS4K_CR0_MX80_81_REG_WRITE_ENABLE 0x80

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
    uint8_t irq_status;

    uint8_t gcr_index;
    uint32_t gcr_regs[256];

    /* Mixer (SB) emulation */
    uint8_t mixer_index;
    uint8_t mixer_regs[256];

    /* IRQ pending mask (register 0x0E) */
    uint8_t irq_pending;

    /* CR (Configuration Register) access */
    uint8_t cr_index;
    uint8_t cr_regs[256];

    /* DSP reset state machine */
    uint8_t dsp_reset_state; /* 0 = idle, 1 = reset active, 2 = ready to read AA */
    uint8_t dsp_data_to_read; /* byte to return on read from 0x1A */
    bool dsp_data_pending;   /* true if dsp_data_to_read has valid data */
    uint8_t dsp_version_state; /* 0=none, 1=next is major, 2=next is minor */

    /* MPU401 state */
    uint8_t mpu_status; /* reflected at 0x31 */
    uint8_t mpu_data;   /* last command written */
    bool mpu_data_pending;
    uint8_t mpu_response;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t gcr_misc = s->gcr_regs[ALS4K_GCR8C_MISC_CTRL];
    bool enabled = (gcr_misc & ALS4K_GCR8C_IRQ_MASK_CTRL_ENABLE) != 0;
    int level = (enabled && s->irq_pending != 0) ? 1 : 0;
    pci_set_irq(pdev, level);
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

    switch (addr) {
    case ALS4K_IOB_07_IRQSTATUS:
        val = s->irq_pending;
        break;

    case ALS4K_IOB_0C_GCR_INDEX:
        val = s->gcr_index;
        break;

    case ALS4K_IOD_08_GCR_DATA:
        if (size == 4) {
            val = s->gcr_regs[s->gcr_index];
        }
        break;

    case ALS4K_IOB_0E_IRQTYPE_SB_CR1E_MPU:
        val = s->irq_pending;
        break;

    case ALS4K_IOB_0F_CR_DATA:
        val = s->cr_regs[s->cr_index];
        break;

    case ALS4K_IOB_14_MIXER_INDEX:
        val = s->mixer_index;
        break;

    case ALS4K_IOB_15_MIXER_DATA:
        val = s->mixer_regs[s->mixer_index];
        /* No auto-increment on read, preserves index */
        break;

    case ALS4K_IOB_1A_ESP_RD_DATA:
        if (s->dsp_reset_state == 2) {
            val = 0xAA;
            s->dsp_reset_state = 0;
            s->dsp_data_pending = false;
        } else {
            val = s->dsp_data_to_read;
            if (s->dsp_version_state) {
                if (s->dsp_version_state == 1) {
                    s->dsp_data_to_read = 0x05;
                    s->dsp_version_state = 2;
                    s->dsp_data_pending = true;
                } else {
                    s->dsp_data_pending = false;
                    s->dsp_version_state = 0;
                }
            } else {
                s->dsp_data_pending = false;
            }
        }
        break;

    case ALS4K_IOB_1E_ESP_RD_STATUS8:
    case ALS4K_IOB_1F_ESP_RD_STATUS16:
        val = 0x40; /* Write buffer empty always */
        if (s->dsp_data_pending) {
            val |= 0x80;
        }
        break;

    case ALS4K_IOB_30_MIDI_DATA:
        if (s->mpu_data_pending) {
            val = s->mpu_response;
            s->mpu_data_pending = false;
        } else {
            val = 0;
        }
        break;

    case ALS4K_IOB_31_MIDI_STATUS:
        val = 0x40; /* Input buffer empty */
        if (s->mpu_data_pending) {
            val |= 0x80;
        }
        break;

    /* Gameport and OPL: return 0xFF/0 to prevent potential hangs */
    case ALS4K_IOB_20_ESP_GAMEPORT_200:
    case ALS4K_IOB_21_ESP_GAMEPORT_201:
        val = 0xFF; /* typical gameport read value (no button) */
        break;

    case ALS4K_IOB_10_ADLIB_ADDR0:
    case ALS4K_IOB_11_ADLIB_ADDR1:
    case ALS4K_IOB_12_ADLIB_ADDR2:
    case ALS4K_IOB_13_ADLIB_ADDR3:
    case ALS4K_IOB_18_OPL_ADDR0:
    case ALS4K_IOB_19_OPL_ADDR1:
        val = 0; /* OPL registers read back 0 */
        break;

    default:
        /* All other registers return 0 */
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ALS4K_IOB_0C_GCR_INDEX:
        if (size == 1) {
            s->gcr_index = (uint8_t)val;
        }
        break;

    case ALS4K_IOD_08_GCR_DATA:
        if (size == 4) {
            s->gcr_regs[s->gcr_index] = (uint32_t)val;
        }
        break;

    case ALS4K_IOB_0E_IRQTYPE_SB_CR1E_MPU:
        if (size == 1) {
            /* Write-1-to-clear for IRQ pending, or CR index selection */
            /* According to the register name, it may serve both purposes.
             * We treat the written value as CR index, but also clear IRQ
             * bits if the corresponding bits are set.
             */
            s->cr_index = (uint8_t)val;
            s->irq_pending &= ~((uint8_t)val);
            pcibase_update_irq(s);
        }
        break;

    case ALS4K_IOB_0F_CR_DATA:
        if (size == 1) {
            s->cr_regs[s->cr_index] = (uint8_t)val;
        }
        break;

    case ALS4K_IOB_14_MIXER_INDEX:
        if (size == 1) {
            s->mixer_index = (uint8_t)val;
        }
        break;

    case ALS4K_IOB_15_MIXER_DATA:
        if (size == 1) {
            s->mixer_regs[s->mixer_index] = (uint8_t)val;
            s->mixer_index++;
        }
        break;

    case ALS4K_IOB_16_ESP_RESET:
        if (size == 1) {
            if (val == 1) {
                s->dsp_reset_state = 1;
            } else if (val == 0 && s->dsp_reset_state == 1) {
                s->dsp_reset_state = 2; /* ready to return 0xAA */
                s->dsp_data_to_read = 0xAA;
                s->dsp_data_pending = true;
                s->dsp_version_state = 0;
            }
        }
        break;

    case ALS4K_IOB_1C_ESP_CMD_DATA:
        if (size == 1) {
            uint8_t cmd = (uint8_t)val;
            switch (cmd) {
            case 0xE1: /* Get version */
                s->dsp_data_to_read = 0x04;
                s->dsp_version_state = 1;
                s->dsp_data_pending = true;
                break;
            case 0x82: /* SB_DSP4_IRQSTATUS */
                s->dsp_data_to_read = s->irq_pending;
                s->dsp_data_pending = true;
                s->dsp_version_state = 0;
                break;
            default:
                s->dsp_data_pending = false;
                s->dsp_version_state = 0;
                break;
            }
        }
        break;

    case ALS4K_IOB_30_MIDI_DATA:
        if (size == 1) {
            uint8_t cmd = (uint8_t)val;
            if (cmd == 0xFF || cmd == 0x3F) {
                s->mpu_response = 0xFE;
                s->mpu_data_pending = true;
            } else {
                /* Other commands: just ACK */
                s->mpu_response = 0xFE;
                s->mpu_data_pending = true;
            }
        }
        break;

    /* Gameport writes: ignore */
    case ALS4K_IOB_20_ESP_GAMEPORT_200:
    case ALS4K_IOB_21_ESP_GAMEPORT_201:
        break;

    default:
        /* Unused registers: ignore writes */
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

    /* Reset all registers */
    memset(s->gcr_regs, 0, sizeof(s->gcr_regs));
    s->gcr_index = 0;
    s->irq_status = 0;

    /* Set chip revision in GCR8C (bits 16-19 = 4) */
    s->gcr_regs[ALS4K_GCR8C_MISC_CTRL] = 0x00040000;

    /* CR registers */
    s->cr_index = 0;
    memset(s->cr_regs, 0, sizeof(s->cr_regs));
    /* Initialize chip identification values */
    s->cr_regs[ALS4K_CR18_ESP_MAJOR_VERSION] = 0x02;
    s->cr_regs[ALS4K_CR19_ESP_MINOR_VERSION] = 0x00;

    /* Mixer */
    s->mixer_index = 0;
    memset(s->mixer_regs, 0, sizeof(s->mixer_regs));

    /* IRQ */
    s->irq_pending = 0;
    pcibase_update_irq(s);

    /* DSP */
    s->dsp_reset_state = 0;
    s->dsp_data_to_read = 0;
    s->dsp_data_pending = false;
    s->dsp_version_state = 0;

    /* MPU401 */
    s->mpu_status = 0x80;
    s->mpu_data = 0;
    s->mpu_data_pending = false;
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

    /* PCI IDs are set via class_init, do not override here */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100; /* covers all offsets up to 0x31 */
    s->bar_info[0].name = "als4000-pio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize register state (same as reset) */
    memset(s->gcr_regs, 0, sizeof(s->gcr_regs));
    s->gcr_index = 0;
    s->irq_status = 0;

    /* Set chip revision in GCR8C */
    s->gcr_regs[ALS4K_GCR8C_MISC_CTRL] = 0x00040000;

    /* CR registers */
    s->cr_index = 0;
    memset(s->cr_regs, 0, sizeof(s->cr_regs));
    /* Initialize chip identification values */
    s->cr_regs[ALS4K_CR18_ESP_MAJOR_VERSION] = 0x02;
    s->cr_regs[ALS4K_CR19_ESP_MINOR_VERSION] = 0x00;

    /* Mixer */
    memset(s->mixer_regs, 0, sizeof(s->mixer_regs));
    s->mixer_index = 0;

    /* IRQ */
    s->irq_pending = 0;
    pcibase_update_irq(s);

    /* DSP */
    s->dsp_reset_state = 0;
    s->dsp_data_to_read = 0;
    s->dsp_data_pending = false;
    s->dsp_version_state = 0;

    /* MPU401 */
    s->mpu_status = 0x80;
    s->mpu_data = 0;
    s->mpu_data_pending = false;
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
    .name = "snd_als4000_pci",
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
    k->vendor_id = PCI_VENDOR_ID_ALS4000;
    k->device_id = PCI_DEVICE_ID_ALS4000;
    k->class_id = PCI_CLASS_ID_ALS4000;
    k->revision = 0x01;
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
